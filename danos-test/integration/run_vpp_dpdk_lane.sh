#!/bin/bash
# VPP+DPDK dedicated lane preflight. Missing PCI/user-space driver is an explicit SKIP.
set -euo pipefail
RESULT_FILE="${DPDK_RESULT_FILE:-}"
PCI_COUNT=0
VMXNET3_COUNT=0
TARGET_PATH=""
finish_result() {
    local rc=$?
    test -n "$RESULT_FILE" || return "$rc"
    {
        printf 'status=%s\n' "$([ "$rc" -eq 0 ] && echo PASS || ([ "$rc" -eq 2 ] && echo SKIP || echo FAIL))"
        printf 'exit_code=%s\n' "$rc"
        printf 'vpp_bin=%q\n' "${VPP_BIN:-}"
        printf 'vppctl=%q\n' "${VPPCTL:-}"
        printf 'dpdk_plugin=%q\n' "${PLUGIN:-}"
        printf 'pci_driver=%q\n' "${PCI_DRIVER:-}"
        printf 'target_bdf=%q\n' "${TARGET_BDF:-}"
        printf 'pci_ethernet_count=%s\nvmxnet3_count=%s\n' "$PCI_COUNT" "$VMXNET3_COUNT"
        printf 'hugepages_total=%s\n' "$(awk '/^HugePages_Total:/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
        printf 'host_utc=%q\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > "$RESULT_FILE"
    return "$rc"
}
trap finish_result EXIT
VPP_BIN="${VPP_BIN:-}"
VPPCTL="${VPPCTL:-}"
if test -z "$VPP_BIN"; then
    VPP_BIN=$(command -v vpp 2>/dev/null || true)
    test -n "$VPP_BIN" || VPP_BIN=/opt/vpp/build-root/install-vpp-native/vpp/bin/vpp
fi
if test -z "$VPPCTL"; then
    VPPCTL=$(command -v vppctl 2>/dev/null || true)
    test -n "$VPPCTL" || VPPCTL=/opt/vpp/build-root/install-vpp-native/vpp/bin/vppctl
fi
PLUGIN="${VPP_DPDK_PLUGIN:-/usr/lib/x86_64-linux-gnu/vpp_plugins/dpdk_plugin.so}"
test -r "$PLUGIN" || PLUGIN=/opt/vpp/build-root/install-vpp-native/vpp/lib/x86_64-linux-gnu/vpp_plugins/dpdk_plugin.so
TARGET_BDF="${DPDK_PCI_BDF:-}"
PCI_DRIVER="${DPDK_PCI_DRIVER:-vfio-pci}"
test -x "$VPP_BIN" || { echo "[SKIP] vpp binary unavailable"; exit 2; }
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
if test "$PCI_DRIVER" = vfio-pci; then
    test -e /dev/vfio/vfio || { echo "[SKIP] /dev/vfio/vfio unavailable"; exit 2; }
fi
test -d "/sys/bus/pci/drivers/$PCI_DRIVER" || {
    echo "[SKIP] requested PCI driver unavailable: $PCI_DRIVER"; exit 2;
}
if test -n "$TARGET_BDF"; then
    test -d "${TARGET_PATH:-}" || { echo "[SKIP] requested PCI BDF not found: $TARGET_BDF"; exit 2; }
    DRIVER=$(basename "$(readlink "$TARGET_PATH/driver" 2>/dev/null || echo unbound)")
    test "$DRIVER" = "$PCI_DRIVER" || { echo "[SKIP] $TARGET_BDF bound to $DRIVER, expected $PCI_DRIVER"; exit 2; }
    echo "[INFO] target $TARGET_BDF bound to $PCI_DRIVER"
fi
grep -Eq 'HugePages_Total:[[:space:]]+[1-9]' /proc/meminfo || { echo "[SKIP] hugepages not configured"; exit 2; }
test -S /run/vpp/api.sock || { echo "[FAIL] VPP API socket unavailable"; exit 1; }
test -x "$VPPCTL" || { echo "[FAIL] vppctl unavailable: $VPPCTL"; exit 1; }
"$VPPCTL" show plugins | grep -qi dpdk || { echo "[FAIL] VPP running without DPDK plugin"; exit 1; }
echo "[PASS] DPDK plugin, PCI/$PCI_DRIVER and hugepage preflight passed"
test "$VMXNET3_COUNT" -gt 0 && echo "[INFO] VMXNET3 PCI device detected (15ad:07b0)"
echo "[INFO] traffic generator must now verify the configured 64-byte single-core target"
