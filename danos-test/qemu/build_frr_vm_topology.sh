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
    dhcp4: true
  ens4:
    dhcp4: false
    addresses: [$address/24]
  ens5:
    dhcp4: false
    addresses: [$bgp_addr/24]
EOF
    cat > "$dir/user-data" <<EOF
#cloud-config
package_update: true
packages: [frr, frr-pythontools, iproute2, iputils-ping, socat]
runcmd:
  - [sh, -c, "sed -i 's/^zebra=no/zebra=yes/; s/^bgpd=no/bgpd=yes/; s/^ospfd=no/ospfd=yes/' /etc/frr/daemons"]
  - [sh, -c, "cat > /etc/frr/frr.conf <<'CFG'"]
  - [sh, -c, "printf 'frr version 10.3\\nfrr defaults traditional\\nhostname $hostname\\nrouter bgp $asn\\n bgp router-id $bgp_addr\\n no bgp ebgp-requires-policy\\n neighbor $bgp_peer remote-as $peer_asn\\n address-family ipv4 unicast\\n  neighbor $bgp_peer activate\\n  network $prefix\\n exit-address-family\\n' >> /etc/frr/frr.conf"]
  - [sh, -c, "chown frr:frr /etc/frr/frr.conf; /usr/lib/frr/frrinit.sh restart"]
EOF
    xorriso -as mkisofs -quiet -V CIDATA -o "$OUT/$node-seed.iso" "$dir"
}

qemu-img create -f qcow2 -F qcow2 -b "$BASE" "$OUT/frr-1.qcow2" 8G >/dev/null
qemu-img create -f qcow2 -F qcow2 -b "$BASE" "$OUT/frr-2.qcow2" 8G >/dev/null
make_seed r1 frr-1 10.10.0.2 10.10.0.1 65001 65002 198.51.100.0/24
make_seed r2 frr-2 10.20.0.2 10.20.0.1 65002 65001 203.0.113.0/24
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
