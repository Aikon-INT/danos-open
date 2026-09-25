#!/bin/bash
# Prepare a reproducible QEMU FRR-1/FRR-2 + DANOS/VPP topology.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
BASE="${FRR_BASE_IMAGE:-$BUILD/debian-13-generic-amd64.full.qcow2}"
OUT="${QEMU_TOPOLOGY_DIR:-$BUILD/qemu-frr-vpp-topology}"
mkdir -p "$OUT/seed-r1" "$OUT/seed-r2"
test -f "$BASE" || { echo "[BLOCKED] base image missing: $BASE"; exit 2; }
BASE="$(realpath "$BASE")"
qemu-img check "$BASE" >/dev/null || { echo "[BLOCKED] base image failed qemu-img check"; exit 2; }

make_seed() {
    local node="$1" hostname="$2" address="$3" peer="$4" asn="$5" peer_asn="$6" prefix="$7"
    local dir="$OUT/seed-$node"
    local bgp_addr=172.31.0.2 bgp_peer=172.31.0.3
    [ "$node" = r2 ] && bgp_addr=172.31.0.3 && bgp_peer=172.31.0.2
    cat > "$dir/meta-data" <<EOF
instance-id: danos-$node
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
package_update: true
packages: [frr, frr-pythontools, iproute2, iputils-ping, socat]
runcmd:
  # Some generic Debian cloud images do not run the package stage reliably
  # when the seed is attached as a second CD-ROM.  Install explicitly before
  # touching FRR configuration so a missing package cannot masquerade as a
  # zserv/client failure.
  - [sh, -c, "for i in 1 2 3; do apt-get update && apt-get install -y frr frr-pythontools iproute2 iputils-ping socat && break; sleep 5; done"]
  - [sh, -c, "sed -i 's/^zebra=no/zebra=yes/; s/^bgpd=no/bgpd=yes/; s/^ospfd=no/ospfd=yes/' /etc/frr/daemons"]
  - [sh, -c, "cat > /etc/frr/frr.conf <<'CFG'"]
  - [sh, -c, "printf 'frr version 10.3\\nfrr defaults traditional\\nlog file /var/log/frr/frr.log debugging\\nhostname $hostname\\nip route $prefix blackhole\\nrouter bgp $asn\\n bgp router-id $bgp_addr\\n no bgp ebgp-requires-policy\\n no bgp suppress-fib-pending\\n neighbor $bgp_peer remote-as $peer_asn\\n address-family ipv4 unicast\\n  neighbor $bgp_peer activate\\n  network $prefix\\n exit-address-family\\nrouter ospf\\n ospf router-id $bgp_addr\\n network 172.31.0.0/24 area 0\\n' >> /etc/frr/frr.conf"]
  - [sh, -c, "chown frr:frr /etc/frr/frr.conf; systemctl restart frr.service"]
  - [sh, -c, "cat > /usr/local/sbin/danos-frr-zapi-proxy <<'SCRIPT'"]
  - [sh, -c, "printf '#!/bin/sh\nset -eu\nfor i in $(seq 1 60); do test -S /var/run/frr/zserv.api && break; sleep 1; done\ntest -S /var/run/frr/zserv.api\nexec socat -d -d -v TCP-LISTEN:2600,bind=0.0.0.0,reuseaddr,fork,keepalive UNIX-CONNECT:/var/run/frr/zserv.api,keepalive 2>&1 | tee -a /var/log/danos-frr-zapi.log /dev/ttyS0 >/dev/null\n' >> /usr/local/sbin/danos-frr-zapi-proxy; chmod +x /usr/local/sbin/danos-frr-zapi-proxy"]
  - [sh, -c, "cat > /etc/systemd/system/danos-frr-zapi.service <<'UNIT'"]
  - [sh, -c, "printf '[Unit]\nRequires=frr.service\nAfter=network-online.target frr.service\nWants=network-online.target\nPartOf=frr.service\n[Service]\nType=simple\nExecStart=/usr/local/sbin/danos-frr-zapi-proxy\nRestart=always\nRestartSec=2\nStandardOutput=append:/var/log/danos-frr-zapi.log\nStandardError=append:/var/log/danos-frr-zapi.log\n[Install]\nWantedBy=multi-user.target\n' >> /etc/systemd/system/danos-frr-zapi.service"]
  - [sh, -c, "systemctl daemon-reload; systemctl enable --now danos-frr-zapi.service"]
  - [sh, -c, "if [ '$node' = r1 ]; then ip link set ens6 up; ip addr replace 10.0.3.2/24 dev ens6; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then ip addr replace 30.30.30.2/24 dev ens3; ip addr replace 30.30.30.4/24 dev ens3; else ip addr replace 30.30.30.3/24 dev ens3; ip addr replace 30.30.30.5/24 dev ens3; fi"]
  - [sh, -c, "if [ '$node' = r2 ]; then sleep 240; echo '--- BGP DYNAMIC ROUTE CYCLE ---' > /dev/ttyS0; ip addr replace 198.19.0.1/24 dev ens3; vtysh -c 'conf t' -c 'router bgp 65002' -c 'no bgp suppress-fib-pending' -c 'address-family ipv4 unicast' -c 'network 198.19.0.0/24' -c 'exit-address-family' > /dev/ttyS0 2>&1; echo bgp-add-rc=\$? > /dev/ttyS0; sleep 15; vtysh -c 'show ip route 198.19.0.0/24' > /dev/ttyS0 2>&1 || true; vtysh -c 'show bgp ipv4 unicast 198.19.0.0/24' > /dev/ttyS0 2>&1 || true; sleep 60; vtysh -c 'clear bgp 172.31.0.2' > /dev/ttyS0 2>&1; sleep 30; vtysh -c 'show bgp ipv4 unicast 198.19.0.0/24' > /dev/ttyS0 2>&1 || true; vtysh -c 'conf t' -c 'router bgp 65002' -c 'address-family ipv4 unicast' -c 'no network 198.19.0.0/24' -c 'exit-address-family' > /dev/ttyS0 2>&1; ip addr del 198.19.0.1/24 dev ens3; echo bgp-withdraw-rc=\$? > /dev/ttyS0; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then printf '#!/bin/sh\nset -u\nLOG=/var/log/danos-frr-route-cycle.log\nexec >>\$LOG 2>&1\necho cycle-start\nsleep 45\nvtysh -c \"conf t\" -c \"ip route $prefix blackhole\" -c \"router bgp $asn\" -c \"address-family ipv4 unicast\" -c \"network $prefix\"; echo add-rc=\$?\nvtysh -c \"show ip route $prefix\"\nsleep 15\nvtysh -c \"conf t\" -c \"router bgp $asn\" -c \"address-family ipv4 unicast\" -c \"no network $prefix\" -c \"exit-address-family\" -c \"no ip route $prefix blackhole\"; echo withdraw-rc=\$?\nvtysh -c \"show ip route $prefix\"\nsleep 15\nvtysh -c \"conf t\" -c \"ip route $prefix blackhole\" -c \"router bgp $asn\" -c \"address-family ipv4 unicast\" -c \"network $prefix\"; echo restore-rc=\$?\nvtysh -c \"show ip route $prefix\"\necho cycle-done\n' >> /usr/local/sbin/danos-frr-route-cycle; chmod +x /usr/local/sbin/danos-frr-route-cycle; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then printf '[Unit]\nRequires=frr.service\nAfter=frr.service danos-frr-zapi.service\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/danos-frr-route-cycle\nStandardOutput=append:/var/log/danos-frr-route-cycle.log\nStandardError=append:/var/log/danos-frr-route-cycle.log\n[Install]\nWantedBy=multi-user.target\n' >> /etc/systemd/system/danos-frr-route-cycle.service; systemctl daemon-reload; systemctl enable --now danos-frr-route-cycle.service; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then systemctl --no-pager status frr.service danos-frr-zapi.service || true; ls -l /var/run/frr/zserv.api || true; ss -ltnp | grep ':2600' || true; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then sleep 2; echo '--- DANOS FRR ROUTE CYCLE LOG ---'; cat /var/log/danos-frr-route-cycle.log || true; echo '--- FRR ROUTE STATE ---'; vtysh -c 'show ip route 198.51.100.0/24' || true; fi"]
  - [sh, -c, "if [ '$node' = r1 ]; then sleep 8; echo '--- FRR BGP SUMMARY ---'; vtysh -c 'show bgp summary' || true; echo '--- FRR LEARNED BGP PREFIX ---'; vtysh -c 'show bgp ipv4 unicast 198.19.0.0/24' || true; vtysh -c 'show ip route 198.19.0.0/24' || true; sleep 300; echo '--- FRR DELAYED LEARNED BGP PREFIX ---'; vtysh -c 'show bgp ipv4 unicast 198.19.0.0/24' || true; vtysh -c 'show ip route 198.19.0.0/24' || true; echo '--- FRR OSPF NEIGHBORS ---'; vtysh -c 'show ip ospf neighbor' || true; tail -n 80 /var/log/frr/frr.log || true; tail -n 80 /var/log/danos-frr-zapi.log || true; fi"]
EOF
    xorriso -as mkisofs -quiet -V CIDATA -o "$OUT/$node-seed.iso" "$dir"
}

qemu-img create -f qcow2 -F qcow2 -b "$BASE" "$OUT/frr-1.qcow2" 8G >/dev/null
qemu-img create -f qcow2 -F qcow2 -b "$BASE" "$OUT/frr-2.qcow2" 8G >/dev/null
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
bgpd/ospfd on first boot.
EOF
echo "[PASS] topology prepared: $OUT"
