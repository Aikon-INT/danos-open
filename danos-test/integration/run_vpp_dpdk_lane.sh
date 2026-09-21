#!/bin/bash
# VPP+DPDK dedicated lane preflight. Missing PCI/VFIO is an explicit SKIP.
set -euo pipefail
PLUGIN="${VPP_DPDK_PLUGIN:-/usr/lib/x86_64-linux-gnu/vpp_plugins/dpdk_plugin.so}"
TARGET_BDF="${DPDK_PCI_BDF:-}"
command -v vpp >/dev/null 2>&1 || { echo "[SKIP] vpp binary unavailable"; exit 2; }
test -r "$PLUGIN" || { echo "[SKIP] DPDK plugin unavailable: $PLUGIN"; exit 2; }
PCI_COUNT=0
VMXNET3_COUNT=0
for d in /sys/bus/pci/devices/*; do test -e "$d" || continue; PCI_COUNT=$((PCI_COUNT + 1)); done
PCI_COUNT=0
for d in /sys/bus/pci/devices/*; do
    test -e "$d/class" || continue
    test "$(cat "$d/class")" = "0x020000" || continue
    PCI_COUNT=$((PCI_COUNT + 1))
    if test -r "$d/vendor" && test -r "$d/device" &&
       test "$(cat "$d/vendor")" = "0x15ad" && test "$(cat "$d/device")" = "0x07b0"; then
        VMXNET3_COUNT=$((VMXNET3_COUNT + 1))
    fi
    if test -n "$TARGET_BDF" && test "$(basename "$d")" = "$TARGET_BDF"; then
        TARGET_PATH="$d"
    fi
done
test "$PCI_COUNT" -gt 0 || { echo "[SKIP] no PCI Ethernet device exposed; VMXNET3 requires a VMware-presented PCI NIC"; exit 2; }
test -e /dev/vfio/vfio || { echo "[SKIP] /dev/vfio/vfio unavailable"; exit 2; }
if test -n "$TARGET_BDF"; then
    test -d "${TARGET_PATH:-}" || { echo "[SKIP] requested PCI BDF not found: $TARGET_BDF"; exit 2; }
    DRIVER=$(basename "$(readlink "$TARGET_PATH/driver" 2>/dev/null || echo unbound)")
    test "$DRIVER" = "vfio-pci" || { echo "[SKIP] $TARGET_BDF bound to $DRIVER, expected vfio-pci"; exit 2; }
    echo "[INFO] target $TARGET_BDF bound to vfio-pci"
fi
grep -Eq 'HugePages_Total:[[:space:]]+[1-9]' /proc/meminfo || { echo "[SKIP] hugepages not configured"; exit 2; }
test -S /run/vpp/api.sock || { echo "[FAIL] VPP API socket unavailable"; exit 1; }
vppctl show plugins | grep -qi dpdk || { echo "[FAIL] VPP running without DPDK plugin"; exit 1; }
echo "[PASS] DPDK plugin, PCI/VFIO and hugepage preflight passed"
test "$VMXNET3_COUNT" -gt 0 && echo "[INFO] VMXNET3 PCI device detected (15ad:07b0)"
echo "[INFO] traffic generator must now verify the configured 64-byte single-core target"
