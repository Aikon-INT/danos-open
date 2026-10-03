#!/usr/bin/env bash
# Build a dedicated physical I211 functional traffic/ECMP qualification ISO.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/danos-open-v0.16.0-rc1-i211-dpdk-traffic-runner-r1.iso}"

: "${VPP_IMAGE:=danos-vpp-runtime-recover:local}"
: "${VPP_DPDK_ENABLE:=1}"
: "${VPP_DPDK_DEVICE:=i211}"
: "${VPP_DPDK_PORTS:=0000:01:00.0 0000:02:00.0}"
: "${VPP_DPDK_NO_RX_INTERRUPTS:=1}"
: "${VPP_DPDK_BIND_DRIVER:=uio_pci_generic}"
: "${VPP_DPDK_EXPECTED_PCI_ID:=8086:1539}"
: "${VPP_DPDK_TRAFFIC_TEST:=1}"
: "${VPP_PING_ENABLE:=1}"
: "${VPP_ECMP_SOAK_COUNT:=1000}"
: "${VPP_ECMP_SOAK_INTERVAL:=0.01}"
: "${VPP_TRAFFIC_TEST_DELAY:=15}"
: "${VPP_STATIC_NEIGHBORS:=0}"
: "${VPP_AUTOSTART:=1}"
: "${DANOS_VPP_RESTART_TEST:=0}"
: "${VPP_PEER_IP1:=10.10.0.2}"
: "${VPP_PEER_IP2:=10.20.0.2}"
: "${VPP_IF1_ADDR:=10.10.0.1/24}"
: "${VPP_IF2_ADDR:=10.20.0.1/24}"
test "$VPP_STATIC_NEIGHBORS" = 0 || {
    echo 'ERROR: physical I211 traffic profile must use dynamic ARP, not fixture MACs' >&2
    exit 2
}

if ! [[ "$VPP_ECMP_SOAK_COUNT" =~ ^[0-9]+$ ]] ||
   (( 10#$VPP_ECMP_SOAK_COUNT < 1000 )); then
    echo 'ERROR: I211 traffic qualification requires at least 1000 probes per ECMP destination' >&2
    exit 2
fi
if test "$VPP_DPDK_TRAFFIC_TEST" != 1 || test "$VPP_PING_ENABLE" != 1; then
    echo 'ERROR: traffic test and ping plugin must remain enabled' >&2
    exit 2
fi

export VPP_IMAGE VPP_DPDK_ENABLE VPP_DPDK_DEVICE VPP_DPDK_PORTS
export VPP_DPDK_NO_RX_INTERRUPTS VPP_DPDK_BIND_DRIVER VPP_DPDK_EXPECTED_PCI_ID
export VPP_DPDK_TRAFFIC_TEST VPP_PING_ENABLE VPP_ECMP_SOAK_COUNT
export VPP_ECMP_SOAK_INTERVAL VPP_AUTOSTART DANOS_VPP_RESTART_TEST
export VPP_STATIC_NEIGHBORS
export VPP_TRAFFIC_TEST_DELAY VPP_PEER_IP1 VPP_PEER_IP2 VPP_IF1_ADDR VPP_IF2_ADDR
export VPP_PEER_MAC1 VPP_PEER_MAC2
exec bash "$ROOT/danos-test/live/build_live_iso.sh" "$OUT"
