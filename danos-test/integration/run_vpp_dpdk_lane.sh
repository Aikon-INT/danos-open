#!/bin/bash
# VPP+DPDK dedicated lane preflight. Missing PCI/user-space driver is an explicit SKIP.
set -euo pipefail
RESULT_FILE="${DPDK_RESULT_FILE:-}"
PCI_COUNT=0
VMXNET3_COUNT=0
TARGET_PATH=""
TARGET_VENDOR=""
TARGET_DEVICE=""
TARGET_BOUND_DRIVER=""
ISO_SHA256=""
PREFLIGHT_STATUS=NOT-RUN
finish_result() {
    local rc=$?
    test -n "$RESULT_FILE" || return "$rc"
    {
        result_status="$([ "$rc" -eq 0 ] && echo PASS || ([ "$rc" -eq 2 ] && echo SKIP || echo FAIL))"
        test "$PREFLIGHT_STATUS" != PASS || result_status=ENVIRONMENT-OPEN
        preflight_status="$PREFLIGHT_STATUS"
        test "$preflight_status" != NOT-RUN || preflight_status="$([ "$rc" -eq 2 ] && echo SKIP || echo FAIL)"
        printf 'status=%s\npreflight_status=%s\nperformance_status=ENVIRONMENT-OPEN\nstage=preflight\n' \
            "$result_status" "$preflight_status"
        printf 'lane=pci-dpdk\n'
        printf 'commit=%s\n' "$(git rev-parse HEAD 2>/dev/null || true)"
        printf 'iso_sha256=%s\npacket_size_bytes=64\nflows=\npackets_tx=\n' "$ISO_SHA256"
        printf 'packets_rx=\n'
        printf 'loss_pct=\nduration_ms=\npps=\nmbps=\nrtt_p50_us=\nrtt_p99_us=\ncpu_pct=\n'
        printf 'ecmp_bucket_0=\necmp_bucket_1=\nrestart_replay=SKIP\n'
        printf 'exit_code=%s\n' "$rc"
        printf 'vpp_bin=%q\n' "${VPP_BIN:-}"
        printf 'vppctl=%q\n' "${VPPCTL:-}"
        printf 'dpdk_plugin=%q\n' "${PLUGIN:-}"
        printf 'pci_driver=%q\n' "${PCI_DRIVER:-}"
        printf 'target_bdf=%q\n' "${TARGET_BDF:-}"
        printf 'pci_vendor_id=%q\npci_device_id=%q\npci_bound_driver=%q\n' \
            "$TARGET_VENDOR" "$TARGET_DEVICE" "$TARGET_BOUND_DRIVER"
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
DPDK_ISO="${DPDK_ISO:-}"
if test -n "$DPDK_ISO" && test -r "$DPDK_ISO"; then
    ISO_SHA256=$(sha256sum "$DPDK_ISO" | awk '{print $1}')
fi
test -n "$TARGET_BDF" || { echo "[SKIP] DPDK_PCI_BDF must identify the NIC under test"; exit 2; }
[[ "$TARGET_BDF" =~ ^[[:xdigit:]]{4}:[[:xdigit:]]{2}:[[:xdigit:]]{2}\.[0-7]$ ]] || {
    echo "[SKIP] invalid PCI BDF: $TARGET_BDF"; exit 2;
}
case "$PCI_DRIVER" in
    vfio-pci|uio_pci_generic|igb_uio) ;;
    *) echo "[SKIP] unsupported DPDK PCI binding driver: $PCI_DRIVER"; exit 2 ;;
esac
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
test "$PCI_COUNT" -gt 0 || { echo "[SKIP] no PCI Ethernet device exposed"; exit 2; }
test -d "${TARGET_PATH:-}" || { echo "[SKIP] requested PCI BDF not found or is not Ethernet: $TARGET_BDF"; exit 2; }
TARGET_VENDOR=$(cat "$TARGET_PATH/vendor" 2>/dev/null || true)
TARGET_DEVICE=$(cat "$TARGET_PATH/device" 2>/dev/null || true)
TARGET_BOUND_DRIVER=$(basename "$(readlink "$TARGET_PATH/driver" 2>/dev/null || echo unbound)")
test "$TARGET_BOUND_DRIVER" = "$PCI_DRIVER" || {
    echo "[SKIP] $TARGET_BDF bound to $TARGET_BOUND_DRIVER, expected $PCI_DRIVER"; exit 2;
}
test -x "$VPP_BIN" || { echo "[SKIP] vpp binary unavailable"; exit 2; }
test -r "$PLUGIN" || { echo "[SKIP] DPDK plugin unavailable: $PLUGIN"; exit 2; }
if test "$PCI_DRIVER" = vfio-pci; then
    test -e /dev/vfio/vfio || { echo "[SKIP] /dev/vfio/vfio unavailable"; exit 2; }
fi
test -d "/sys/bus/pci/drivers/$PCI_DRIVER" || {
    echo "[SKIP] requested PCI driver unavailable: $PCI_DRIVER"; exit 2;
}
echo "[INFO] target $TARGET_BDF id=$TARGET_VENDOR:$TARGET_DEVICE bound to $TARGET_BOUND_DRIVER"
grep -Eq 'HugePages_Total:[[:space:]]+[1-9]' /proc/meminfo || { echo "[SKIP] hugepages not configured"; exit 2; }
test -S /run/vpp/api.sock || { echo "[FAIL] VPP API socket unavailable"; exit 1; }
test -x "$VPPCTL" || { echo "[FAIL] vppctl unavailable: $VPPCTL"; exit 1; }
"$VPPCTL" show plugins | grep -qi dpdk || { echo "[FAIL] VPP running without DPDK plugin"; exit 1; }
echo "[PASS] DPDK plugin, PCI/$PCI_DRIVER and hugepage preflight passed"
test "$VMXNET3_COUNT" -gt 0 && echo "[INFO] VMXNET3 PCI device detected (15ad:07b0)"
PREFLIGHT_STATUS=PASS
echo "[OPEN] no real traffic-generator result supplied; 64-byte performance acceptance remains open"
exit 2
