#!/bin/bash
# Prepare a reproducible QEMU FRR-1/FRR-2 + DANOS/VPP topology.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
BASE="${FRR_BASE_IMAGE:-$BUILD/debian-13-generic-amd64.full.qcow2}"
OUT="${QEMU_TOPOLOGY_DIR:-$BUILD/qemu-frr-vpp-topology}"
FRR_REUSE_DISK_DIR="${FRR_REUSE_DISK_DIR:-}"
FRR_TRAFFIC_READY_PORT="${FRR_TRAFFIC_READY_PORT:-2603}"
mkdir -p "$OUT/seed-r1" "$OUT/seed-r2"
test -f "$BASE" || { echo "[BLOCKED] base image missing: $BASE"; exit 2; }
BASE="$(realpath "$BASE")"
qemu-img check "$BASE" >/dev/null || { echo "[BLOCKED] base image failed qemu-img check"; exit 2; }

make_seed() {
    local node="$1" hostname="$2" address="$3" peer="$4" asn="$5" peer_asn="$6" prefix="$7"
    local dir="$OUT/seed-$node"
    local instance_id="danos-${node}-$(basename "$OUT")"
    local bgp_addr=172.31.0.2 bgp_peer=172.31.0.3
    [ "$node" = r2 ] && bgp_addr=172.31.0.3 && bgp_peer=172.31.0.2
    cat > "$dir/meta-data" <<EOF
instance-id: $instance_id
local-hostname: $hostname
EOF
    cat > "$dir/network-config" <<EOF
version: 2
ethernets:
  ens3:
    dhcp4: false
    addresses: [$address/24]
  ens4:
    dhcp4: false
    addresses: [$bgp_addr/24]
  ens5:
    dhcp4: true
  ens6:
    dhcp4: false
    addresses: [10.0.3.2/24]
EOF
    cat > "$dir/user-data" <<EOF
#cloud-config
bootcmd:
  # The generic Debian image enables apt-daily timers. On the internet-backed
  # FRR-1 guest these can hold cloud-final behind a slow mirror for minutes,
  # preventing the dataplane-ready endpoint from coming up within the QEMU
  # traffic gate. These guests are disposable test nodes; package installation
  # remains explicit and is still performed below when FRR is absent.
  - [sh, -c, "systemctl mask --runtime --now apt-daily.service apt-daily-upgrade.service apt-daily.timer apt-daily-upgrade.timer || true"]
  - [sh, -c, "echo 1 > /proc/sys/net/ipv4/ip_forward"]
  # Stage dataplane aliases early, but never publish the readiness socket here:
  # reused disks may contain an old enabled listener. The new per-run listener
  # is created in runcmd only after FRR-2's BGP route has been learned.
  - [sh, -c, "if [ '$node' = r1 ] && command -v vtysh >/dev/null 2>&1 && test -S /var/run/frr/zserv.api; then for address in 30.30.30.2 30.30.30.3 30.30.30.4 30.30.30.5; do ip addr replace \$address/24 dev ens3; done; ip route replace 10.20.0.0/24 via 172.31.0.3; for setting in all default ens3 ens4; do echo 0 > /proc/sys/net/ipv4/conf/\$setting/rp_filter; done; fi"]
runcmd:
  # Some generic Debian cloud images do not run the package stage reliably
  # when the seed is attached as a second CD-ROM.  Install explicitly before
  # touching FRR configuration so a missing package cannot masquerade as a
  # zserv/client failure.
  - [sh, -c, "if command -v vtysh >/dev/null 2>&1; then exit 0; fi; rm -f /etc/apt/sources.list /etc/apt/sources.list.d/debian.sources; printf 'Types: deb\\nURIs: http://repo.huaweicloud.com/debian\\nSuites: trixie trixie-updates\\nComponents: main\\nSigned-By: /usr/share/keyrings/debian-archive-keyring.gpg\\n' > /etc/apt/sources.list.d/danos-test.sources; for i in 1 2 3; do apt-get update -o Acquire::Languages=none -o Acquire::Retries=2 && apt-get install -y --no-install-recommends frr frr-pythontools iproute2 iputils-ping socat && break; sleep 5; done"]
  - [sh, -c, "sed -i 's/^zebra=no/zebra=yes/; s/^bgpd=no/bgpd=yes/; s/^ospfd=no/ospfd=yes/' /etc/frr/daemons"]
  - [sh, -c, "cat > /etc/frr/frr.conf <<'CFG'"]
  - [sh, -c, "printf 'frr version 10.3\\nfrr defaults traditional\\nlog file /var/log/frr/frr.log debugging\\ndebug zebra redistribute\\ndebug zebra rib\\ndebug zebra events\\nhostname $hostname\\nip route $prefix blackhole\\nrouter bgp $asn\\n bgp router-id $bgp_addr\\n no bgp ebgp-requires-policy\\n no bgp suppress-fib-pending\\n neighbor $bgp_peer remote-as $peer_asn\\n address-family ipv4 unicast\\n  neighbor $bgp_peer activate\\n  network $prefix\\n exit-address-family\\nrouter ospf\\n ospf router-id $bgp_addr\\n network 172.31.0.0/24 area 0\\n' >> /etc/frr/frr.conf"]
  - [sh, -c, "chown frr:frr /etc/frr/frr.conf; systemctl restart frr.service"]
  - [sh, -c, "cat > /usr/local/sbin/danos-frr-zapi-proxy <<'SCRIPT'"]
  - [sh, -c, "printf '#!/bin/sh\nset -eu\nfor i in $(seq 1 60); do test -S /var/run/frr/zserv.api && break; sleep 1; done\ntest -S /var/run/frr/zserv.api\nexec socat -d -d -v TCP-LISTEN:2600,bind=0.0.0.0,reuseaddr,fork,keepalive UNIX-CONNECT:/var/run/frr/zserv.api,keepalive 2>&1 | tee -a /var/log/danos-frr-zapi.log /dev/ttyS0 >/dev/null\n' >> /usr/local/sbin/danos-frr-zapi-proxy; chmod +x /usr/local/sbin/danos-frr-zapi-proxy"]
  - [sh, -c, "cat > /etc/systemd/system/danos-frr-zapi.service <<'UNIT'"]
  - [sh, -c, "printf '[Unit]\nRequires=frr.service\nAfter=network-online.target frr.service\nWants=network-online.target\nPartOf=frr.service\n[Service]\nType=simple\nExecStart=/usr/local/sbin/danos-frr-zapi-proxy\nRestart=always\nRestartSec=2\nStandardOutput=append:/var/log/danos-frr-zapi.log\nStandardError=append:/var/log/danos-frr-zapi.log\n[Install]\nWantedBy=multi-user.target\n' >> /etc/systemd/system/danos-frr-zapi.service"]
  - [sh, -c, "systemctl daemon-reload; systemctl enable --now danos-frr-zapi.service"]
  - [sh, -c, "if [ '$node' = r1 ]; then ip link set ens6 up; ip addr replace 10.0.3.2/24 dev ens6; fi"]
  # ECMP probes use a VPP loopback source (30.30.30.1). Force replies back
  # over each FRR node's own DANOS-facing link instead of relying on an
  # overlapping connected test subnet or the FRR peer link.
  - [sh, -c, "if [ '$node' = r1 ]; then ip route replace 30.30.30.1/32 via 10.10.0.1 dev ens3; else ip route replace 30.30.30.1/32 via 10.20.0.1 dev ens3; fi"]
  # Put the same four test endpoints on both isolated DANOS-facing links.
  # ECMP is free to select either next hop for any 5-tuple; unique endpoint
  # ownership on one path makes half the hashed flows fail for topology reasons.
  - [sh, -c, "for address in 30.30.30.2 30.30.30.3 30.30.30.4 30.30.30.5; do ip addr replace \$address/24 dev ens3; done; if [ '$node' = r1 ]; then ip route replace 10.20.0.0/24 via 172.31.0.3; ip neigh replace 10.10.0.1 lladdr 52:54:00:10:01:01 nud permanent dev ens3; else ip route replace 10.10.0.0/24 via 172.31.0.2; ip neigh replace 10.20.0.1 lladdr 52:54:00:10:02:01 nud permanent dev ens3; fi; for setting in all default ens3 ens4; do echo 0 > /proc/sys/net/ipv4/conf/\$setting/rp_filter; done; echo 1 > /proc/sys/net/ipv4/ip_forward; echo FRR-DP-READY node=$node > /dev/ttyS0; ip -4 addr show dev ens3 > /dev/ttyS0; ip route show table main > /dev/ttyS0"]
  # Publish a response token only after this boot has installed all dataplane
  # addresses. Reused disks can autostart an obsolete empty listener earlier.
  - [sh, -c, "if [ '$node' = r1 ]; then peer_route_ready=0; for ready_wait in \$(seq 1 180); do if vtysh -c 'show ip route 198.18.0.0/24' 2>/dev/null | grep -q '172.31.0.3'; then peer_route_ready=1; break; fi; sleep 1; done; if [ \$peer_route_ready -eq 1 ]; then printf '[Unit]\\nAfter=network-online.target\\nWants=network-online.target\\n[Service]\\nType=simple\\nExecStart=/usr/bin/socat TCP-LISTEN:$FRR_TRAFFIC_READY_PORT,bind=10.0.3.2,reuseaddr,fork SYSTEM:\"echo FRR-DP-READY\"\\nRestart=always\\nRestartSec=1\\n[Install]\\nWantedBy=multi-user.target\\n' > /etc/systemd/system/danos-frr-dp-readiness.service; systemctl stop danos-frr-dp-readiness.service 2>/dev/null || true; systemctl daemon-reload; systemctl enable --now danos-frr-dp-readiness.service; listener_ready=0; for listener_wait in \$(seq 1 10); do if timeout 1 bash -c '</dev/tcp/10.0.3.2/$FRR_TRAFFIC_READY_PORT' 2>/dev/null; then listener_ready=1; break; fi; sleep 1; done; if [ \$listener_ready -eq 1 ]; then echo FRR-PEER-ROUTE-READY PASS > /dev/ttyS0; else echo FRR-PEER-ROUTE-READY FAIL > /dev/ttyS0; fi; else echo FRR-PEER-ROUTE-READY FAIL > /dev/ttyS0; fi; fi"]
  - [sh, -c, "sleep 8; echo '--- dataplane peer diagnostics ---' > /dev/ttyS0; ip -s link show dev ens3 > /dev/ttyS0; ip neigh show dev ens3 > /dev/ttyS0; ip route get 30.30.30.1 > /dev/ttyS0 2>&1 || true; for setting in all default ens3; do printf 'rp_filter.%s=' \$setting >> /dev/ttyS0; cat /proc/sys/net/ipv4/conf/\$setting/rp_filter >> /dev/ttyS0; done; echo 1 > /proc/sys/net/ipv4/ip_forward"]
  - [sh, -c, "echo '--- ICMP and return-path diagnostics ---' > /dev/ttyS0; printf 'icmp_echo_ignore_all=' > /dev/ttyS0; cat /proc/sys/net/ipv4/icmp_echo_ignore_all > /dev/ttyS0; ip route get 30.30.30.2 > /dev/ttyS0 2>&1 || true; ip route get 30.30.30.1 from 30.30.30.2 > /dev/ttyS0 2>&1 || true; cat /proc/net/ip_tables_names > /dev/ttyS0 2>&1 || true; iptables -S > /dev/ttyS0 2>&1 || true; nft list ruleset > /dev/ttyS0 2>&1 || true; ping -c 2 -W 1 -I 30.30.30.2 30.30.30.1 > /dev/ttyS0 2>&1 || true"]
  - [sh, -c, "if [ '$node' = r2 ]; then sleep 180; echo '--- BGP DYNAMIC ROUTE CYCLE ---' > /dev/ttyS0; ip addr replace 198.19.0.1/24 dev ens3; vtysh -c 'conf t' -c 'router bgp 65002' -c 'no bgp suppress-fib-pending' -c 'address-family ipv4 unicast' -c 'network 198.19.0.0/24' -c 'exit-address-family' > /dev/ttyS0 2>&1; echo bgp-add-rc=\$? > /dev/ttyS0; sleep 15; vtysh -c 'show ip route 198.19.0.0/24' > /dev/ttyS0 2>&1 || true; vtysh -c 'show bgp ipv4 unicast 198.19.0.0/24' > /dev/ttyS0 2>&1 || true; echo '--- BGP PEER ADVERTISED ROUTES ---' > /dev/ttyS0; vtysh -c 'show bgp ipv4 unicast neighbors 172.31.0.2 advertised-routes' > /dev/ttyS0 2>&1 || true; sleep 120; vtysh -c 'clear bgp 172.31.0.2' > /dev/ttyS0 2>&1; sleep 30; vtysh -c 'show bgp ipv4 unicast 198.19.0.0/24' > /dev/ttyS0 2>&1 || true; vtysh -c 'conf t' -c 'router bgp 65002' -c 'address-family ipv4 unicast' -c 'no network 198.19.0.0/24' -c 'exit-address-family' > /dev/ttyS0 2>&1; ip addr del 198.19.0.1/24 dev ens3; echo bgp-withdraw-rc=\$? > /dev/ttyS0; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then printf '#!/bin/sh\nset -u\nLOG=/var/log/danos-frr-route-cycle.log\nexec >>\$LOG 2>&1\necho cycle-start\n# Allow DANOS to register its persistent zebra client before route changes.\nsleep 300\nvtysh -c \"conf t\" -c \"ip route $prefix blackhole\" -c \"router bgp $asn\" -c \"address-family ipv4 unicast\" -c \"network $prefix\"; op_rc=\$?; echo add-rc=\$op_rc; echo FRR-ROUTE-CYCLE add-rc=\$op_rc > /dev/ttyS0\nvtysh -c \"show ip route $prefix\"\nsleep 15\nvtysh -c \"conf t\" -c \"router bgp $asn\" -c \"address-family ipv4 unicast\" -c \"no network $prefix\" -c \"exit-address-family\" -c \"no ip route $prefix blackhole\"; op_rc=\$?; echo withdraw-rc=\$op_rc; echo FRR-ROUTE-CYCLE withdraw-rc=\$op_rc > /dev/ttyS0\nvtysh -c \"show ip route $prefix\"\nsleep 15\nvtysh -c \"conf t\" -c \"ip route $prefix blackhole\" -c \"router bgp $asn\" -c \"address-family ipv4 unicast\" -c \"network $prefix\"; op_rc=\$?; echo restore-rc=\$op_rc; echo FRR-ROUTE-CYCLE restore-rc=\$op_rc > /dev/ttyS0\nvtysh -c \"show ip route $prefix\"\necho cycle-done; echo FRR-ROUTE-CYCLE cycle-done > /dev/ttyS0\n' >> /usr/local/sbin/danos-frr-route-cycle; chmod +x /usr/local/sbin/danos-frr-route-cycle; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then printf '[Unit]\nRequires=frr.service\nAfter=frr.service danos-frr-zapi.service\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/danos-frr-route-cycle\nStandardOutput=append:/var/log/danos-frr-route-cycle.log\nStandardError=append:/var/log/danos-frr-route-cycle.log\n[Install]\nWantedBy=multi-user.target\n' >> /etc/systemd/system/danos-frr-route-cycle.service; systemctl daemon-reload; systemctl enable --now danos-frr-route-cycle.service; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then sleep 120; echo FRR-RESTART-BEGIN > /dev/ttyS0; systemctl restart --no-block frr.service; restart_rc=\$?; echo FRR-RESTART-REQUEST rc=\$restart_rc > /dev/ttyS0; restart_ok=0; for restart_wait in $(seq 1 90); do if test -S /var/run/frr/zserv.api && vtysh -c 'show bgp summary' 2>/dev/null | grep -q '172.31.0.3'; then restart_ok=1; break; fi; sleep 1; done; if [ \$restart_ok -eq 1 ]; then echo FRR-RESTART-TEST PASS > /dev/ttyS0; else echo FRR-RESTART-TEST FAIL > /dev/ttyS0; fi; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then systemctl --no-pager status frr.service danos-frr-zapi.service || true; ls -l /var/run/frr/zserv.api || true; ss -ltnp | grep ':2600' || true; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then sleep 2; echo '--- DANOS FRR ROUTE CYCLE LOG ---'; cat /var/log/danos-frr-route-cycle.log || true; echo '--- FRR ROUTE STATE ---'; vtysh -c 'show ip route 198.51.100.0/24' || true; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then sleep 8; echo '--- FRR BGP SUMMARY ---'; vtysh -c 'show bgp summary' || true; echo '--- FRR LEARNED BGP PREFIX ---'; vtysh -c 'show bgp ipv4 unicast 198.19.0.0/24' || true; vtysh -c 'show ip route 198.19.0.0/24' || true; sleep 140; echo '--- FRR DELAYED LEARNED BGP PREFIX ---'; vtysh -c 'show bgp ipv4 unicast 198.19.0.0/24' || true; vtysh -c 'show ip route 198.19.0.0/24' || true; echo '--- FRR PEER RECEIVED ROUTES ---'; vtysh -c 'show bgp ipv4 unicast neighbors 172.31.0.3 received-routes' || true; vtysh -c 'show bgp summary' || true; echo '--- FRR OSPF NEIGHBORS ---'; vtysh -c 'show ip ospf neighbor' || true; tail -n 80 /var/log/frr/frr.log || true; tail -n 80 /var/log/danos-frr-zapi.log || true; fi"]
EOF
    xorriso -as mkisofs -quiet -V CIDATA -o "$OUT/$node-seed.iso" "$dir"
}

if [ -n "$FRR_REUSE_DISK_DIR" ]; then
  for node in 1 2; do
    source_disk="$FRR_REUSE_DISK_DIR/frr-$node.qcow2"
    test -f "$source_disk" || { echo "[BLOCKED] reusable FRR disk missing: $source_disk"; exit 2; }
    cp --reflink=auto "$source_disk" "$OUT/frr-$node.qcow2"
  done
else
  qemu-img create -f qcow2 -F qcow2 -b "$BASE" "$OUT/frr-1.qcow2" 8G >/dev/null
  qemu-img create -f qcow2 -F qcow2 -b "$BASE" "$OUT/frr-2.qcow2" 8G >/dev/null
fi
make_seed r1 frr-1 10.10.0.2 10.10.0.1 65001 65002 198.51.100.0/24
make_seed r2 frr-2 10.20.0.2 10.20.0.1 65002 65001 198.18.0.0/24
cat > "$OUT/README" <<EOF
QEMU topology artifacts

FRR-1: frr-1.qcow2 + r1-seed.iso, network 10.10.0.0/24
FRR-2: frr-2.qcow2 + r2-seed.iso, network 10.20.0.0/24
DANOS: $ROOT/build/danos-vpp-dpdk-e1000-2port-traffic.iso

Each FRR VM needs one DANOS-facing NIC and one FRR peer NIC. Use separate
QEMU socket/netdev segments for 10.10.0.0/24, 10.20.0.0/24 and the shared
FRR peer segment 172.31.0.0/24. Cloud-init installs FRR and starts
bgpd/ospfd on first boot. FRR-1 publishes dataplane readiness on TCP
$FRR_TRAFFIC_READY_PORT only after it learns FRR-2's BGP prefix.
EOF
if [ -n "$FRR_REUSE_DISK_DIR" ]; then
  printf 'Reused pre-provisioned FRR disks from %s; per-topology cloud-init instance IDs force seed config replay.\n' \
    "$(realpath "$FRR_REUSE_DISK_DIR")" >> "$OUT/README"
fi
echo "[PASS] topology prepared: $OUT"
