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

# Exercise a bind-stage failure and require enough live sysfs state to tell
# whether the device remained bound to its native driver or became unbound.
mkdir -p "$tmp/sys/bus/pci/devices/0000:02:00.0"
printf '0x8086\n' > "$tmp/sys/bus/pci/devices/0000:02:00.0/vendor"
printf '0x1539\n' > "$tmp/sys/bus/pci/devices/0000:02:00.0/device"
touch "$tmp/sys/bus/pci/devices/0000:02:00.0/driver_override"
touch "$tmp/sys/bus/pci/drivers/uio_pci_generic/new_id"
mkdir "$tmp/sys/bus/pci/drivers/uio_pci_generic/bind"
if DANOS_DPDK_SYSFS_ROOT="$tmp/sys" \
   DANOS_BUILD_DPDK_BIND_DRIVER=uio_pci_generic \
   DANOS_BUILD_DPDK_EXPECTED_PCI_ID=8086:1539 \
   DANOS_BUILD_DPDK_PORTS=0000:02:00.0 \
   sh "$bind_script" > "$tmp/out" 2>&1; then
    echo 'FAIL: bind-stage error was not detected'
    exit 1
fi
grep -q 'DPDK-PCI-BIND FAIL bind=0000:02:00.0 driver=uio_pci_generic rc=' "$tmp/out"
grep -q 'DPDK-PCI-DIAG bdf=0000:02:00.0 stage=bind current_driver=unbound driver_override=uio_pci_generic' "$tmp/out"
test ! -s "$tmp/sys/bus/pci/drivers/uio_pci_generic/new_id"

# A pre-existing driver_override must be the only matching mechanism: adding
# new_id would register the PCI ID globally and can bind unselected I211 NICs.
mkdir -p "$tmp/sys/bus/pci/devices/0000:03:00.0"
printf '0x8086\n' > "$tmp/sys/bus/pci/devices/0000:03:00.0/vendor"
printf '0x1539\n' > "$tmp/sys/bus/pci/devices/0000:03:00.0/device"
if DANOS_DPDK_SYSFS_ROOT="$tmp/sys" \
   DANOS_BUILD_DPDK_BIND_DRIVER=uio_pci_generic \
   DANOS_BUILD_DPDK_EXPECTED_PCI_ID=8086:1539 \
   DANOS_BUILD_DPDK_PORTS=0000:03:00.0 \
   sh "$bind_script" > "$tmp/out" 2>&1; then
    echo 'FAIL: bound through a global PCI ID without per-device driver_override'
    exit 1
fi
grep -q 'driver-override-unavailable=0000:03:00.0' "$tmp/out"
test ! -s "$tmp/sys/bus/pci/drivers/uio_pci_generic/new_id"

if DANOS_DPDK_SYSFS_ROOT="$tmp/sys" \
   DANOS_BUILD_DPDK_BIND_DRIVER=unknown \
   DANOS_BUILD_DPDK_PORTS=0000:01:00.0 \
   sh "$bind_script" > "$tmp/out" 2>&1; then
    echo 'FAIL: accepted unsupported driver'
    exit 1
fi
grep -q 'unsupported-driver=unknown' "$tmp/out"
grep -Fq 'DANOS_BUILD_DPDK_BIND_DRIVER="$DANOS_BUILD_DPDK_BIND_DRIVER"' \
    "$ROOT/danos-test/live/init"
grep -Fq 'DANOS_BUILD_DPDK_EXPECTED_PCI_ID="$DANOS_BUILD_DPDK_EXPECTED_PCI_ID"' \
    "$ROOT/danos-test/live/init"
echo 'PASS: DPDK PCI binding fails closed and reports bind-stage sysfs diagnostics'
