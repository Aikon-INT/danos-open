#!/usr/bin/env bash
# Build the physical two-port Intel I211 polling runner with VPP ping support.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/danos-open-v0.16.0-rc1-i211-dpdk-polling-runner-r10.iso}"

: "${VPP_IMAGE:=danos-vpp-runtime-recover:local}"
: "${VPP_DPDK_ENABLE:=1}"
: "${VPP_DPDK_DEVICE:=i211}"
: "${VPP_DPDK_PORTS:=0000:01:00.0 0000:02:00.0}"
: "${VPP_DPDK_NO_RX_INTERRUPTS:=1}"
: "${VPP_DPDK_BIND_DRIVER:=uio_pci_generic}"
: "${VPP_DPDK_EXPECTED_PCI_ID:=8086:1539}"
: "${VPP_DPDK_TRAFFIC_TEST:=0}"
: "${VPP_PING_ENABLE:=1}"
: "${VPP_AUTOSTART:=1}"
: "${DANOS_VPP_RESTART_TEST:=0}"

export VPP_IMAGE VPP_DPDK_ENABLE VPP_DPDK_DEVICE VPP_DPDK_PORTS
export VPP_DPDK_NO_RX_INTERRUPTS VPP_DPDK_BIND_DRIVER VPP_DPDK_EXPECTED_PCI_ID
export VPP_DPDK_TRAFFIC_TEST VPP_PING_ENABLE VPP_AUTOSTART DANOS_VPP_RESTART_TEST

exec bash "$ROOT/danos-test/live/build_live_iso.sh" "$OUT"
