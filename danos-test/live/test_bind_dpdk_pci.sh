#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bind_script="$ROOT/danos-test/live/bind_dpdk_pci.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/sys/bus/pci/drivers/uio_pci_generic" "$tmp/sys/bus/pci/devices/0000:01:00.0"
printf '0x10ec\n' > "$tmp/sys/bus/pci/devices/0000:01:00.0/vendor"
printf '0x8168\n' > "$tmp/sys/bus/pci/devices/0000:01:00.0/device"
touch "$tmp/sys/bus/pci/drivers/uio_pci_generic/new_id"

if DANOS_DPDK_SYSFS_ROOT="$tmp/sys" \
   DANOS_BUILD_DPDK_BIND_DRIVER=uio_pci_generic \
   DANOS_BUILD_DPDK_EXPECTED_PCI_ID=8086:1539 \
   DANOS_BUILD_DPDK_PORTS=0000:01:00.0 \
   sh "$bind_script" > "$tmp/out" 2>&1; then
    echo 'FAIL: refused to reject non-I211 PCI device'
    exit 1
fi
grep -q 'unexpected-device=0000:01:00.0:0x10ec:0x8168' "$tmp/out"
test ! -e "$tmp/sys/bus/pci/devices/0000:01:00.0/driver_override"

if DANOS_DPDK_SYSFS_ROOT="$tmp/sys" \
   DANOS_BUILD_DPDK_BIND_DRIVER=uio_pci_generic \
   DANOS_BUILD_DPDK_EXPECTED_PCI_ID=8086:1539 \
   DANOS_BUILD_DPDK_PORTS=not-a-bdf \
   sh "$bind_script" > "$tmp/out" 2>&1; then
    echo 'FAIL: accepted malformed BDF'
    exit 1
fi
grep -q 'invalid-BDF=not-a-bdf' "$tmp/out"

if DANOS_DPDK_SYSFS_ROOT="$tmp/sys" \
   DANOS_BUILD_DPDK_BIND_DRIVER=unknown \
   DANOS_BUILD_DPDK_PORTS=0000:01:00.0 \
   sh "$bind_script" > "$tmp/out" 2>&1; then
    echo 'FAIL: accepted unsupported driver'
    exit 1
fi
grep -q 'unsupported-driver=unknown' "$tmp/out"
echo 'PASS: DPDK PCI binding fails closed on wrong device, malformed BDF and unsupported driver'
