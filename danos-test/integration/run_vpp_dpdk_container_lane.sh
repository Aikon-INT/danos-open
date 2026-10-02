#!/bin/bash
# Run the DPDK preflight inside a privileged Debian trixie VPP container.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONTAINER="${DPDK_CONTAINER:-danos-vpp-host-1789981447}"
TARGET_BDF="${DPDK_PCI_BDF:-}"
PCI_DRIVER="${DPDK_PCI_DRIVER:-vfio-pci}"
RESULT_FILE="${DPDK_RESULT_FILE:-}"
STATUS=FAIL
TARGET_VENDOR=""
TARGET_DEVICE=""
TARGET_BOUND_DRIVER=""
ISO_SHA256=""
ISO_BUILD_COMMIT=""
ISO_SOURCE_DIRTY=""
RUNNER_COMMIT="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)"
DPDK_ISO="${DPDK_ISO:-}"
if test -n "$DPDK_ISO" && test -r "$DPDK_ISO"; then
    ISO_SHA256=$(sha256sum "$DPDK_ISO" | awk '{print $1}')
fi
finish_result() {
    local rc=$?
    test -n "$RESULT_FILE" || return "$rc"
    {
        printf 'status=%s\n' "$STATUS"
        preflight_status="$STATUS"
        test "$STATUS" != ENVIRONMENT-OPEN || preflight_status=PASS
        printf 'preflight_status=%s\nperformance_status=ENVIRONMENT-OPEN\nstage=preflight\n' \
            "$preflight_status"
        printf 'lane=pci-dpdk\n'
        printf 'commit=%s\niso_build_commit=%s\niso_source_dirty=%s\nrunner_commit=%s\n' \
            "$ISO_BUILD_COMMIT" "$ISO_BUILD_COMMIT" "$ISO_SOURCE_DIRTY" "$RUNNER_COMMIT"
        printf 'iso_sha256=%s\npacket_size_bytes=64\nflows=\npackets_tx=\n' "$ISO_SHA256"
        printf 'packets_rx=\n'
        printf 'loss_pct=\nduration_ms=\npps=\nmbps=\nrtt_p50_us=\nrtt_p99_us=\ncpu_pct=\n'
        printf 'ecmp_bucket_0=\necmp_bucket_1=\nrestart_replay=SKIP\n'
        printf 'exit_code=%s\n' "$rc"
        printf 'container=%q\n' "$CONTAINER"
        printf 'pci_driver=%q\n' "$PCI_DRIVER"
        printf 'target_bdf=%q\n' "$TARGET_BDF"
        printf 'pci_vendor_id=%q\npci_device_id=%q\npci_bound_driver=%q\n' \
            "$TARGET_VENDOR" "$TARGET_DEVICE" "$TARGET_BOUND_DRIVER"
        printf 'host_utc=%q\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > "$RESULT_FILE"
    return "$rc"
}
trap finish_result EXIT
skip() { STATUS=SKIP; echo "$1"; exit 2; }
test -n "$TARGET_BDF" || skip '[SKIP] DPDK_PCI_BDF must identify the NIC under test'
[[ "$TARGET_BDF" =~ ^[[:xdigit:]]{4}:[[:xdigit:]]{2}:[[:xdigit:]]{2}\.[0-7]$ ]] || {
    skip "[SKIP] invalid PCI BDF: $TARGET_BDF"
}
case "$PCI_DRIVER" in
    vfio-pci|uio_pci_generic|igb_uio) ;;
    *) skip "[SKIP] unsupported DPDK PCI binding driver: $PCI_DRIVER" ;;
esac
test -n "$DPDK_ISO" && test -r "$DPDK_ISO" || {
    skip '[SKIP] DPDK_ISO must identify the booted/qualified image'
}
ISO_IDENTITY=$(python3 "$ROOT/danos-test/integration/read_live_iso_identity.py" "$DPDK_ISO") || {
    skip '[SKIP] ISO build provenance unavailable; rebuild with embedded build-info.env'
}
ISO_BUILD_COMMIT=$(printf '%s\n' "$ISO_IDENTITY" | sed -n 's/^iso_build_commit=//p')
ISO_SOURCE_DIRTY=$(printf '%s\n' "$ISO_IDENTITY" | sed -n 's/^iso_source_dirty=//p')
test "$ISO_SOURCE_DIRTY" = 0 || skip '[SKIP] ISO was built from a dirty source tree'
docker inspect "$CONTAINER" >/dev/null 2>&1 || {
    skip "[SKIP] DPDK container unavailable: $CONTAINER"
}
run() { docker exec "$CONTAINER" sh -lc "$1"; }
run 'command -v vpp >/dev/null' || skip '[SKIP] VPP binary unavailable'
run 'test -r /usr/lib/x86_64-linux-gnu/vpp_plugins/dpdk_plugin.so' || {
    skip '[SKIP] DPDK plugin unavailable'
}
if [ "$PCI_DRIVER" = vfio-pci ]; then
    run 'test -e /dev/vfio/vfio' || skip '[SKIP] VFIO unavailable'
fi
run "test -d /sys/bus/pci/drivers/$PCI_DRIVER" || {
    skip "[SKIP] requested PCI driver unavailable: $PCI_DRIVER"
}
run "grep -Eq 'HugePages_Total:[[:space:]]+[1-9]' /proc/meminfo" || {
    skip '[SKIP] hugepages not configured'
}
PCI=$(run "for d in /sys/bus/pci/devices/*; do test -e \"\$d/class\" || continue; test \"\$(cat \"\$d/class\")\" = 0x020000 && basename \"\$d\"; done" || true)
test -n "$PCI" || skip '[SKIP] no PCI Ethernet device exposed'
echo "[INFO] PCI Ethernet: $PCI"
printf '%s\n' "$PCI" | grep -qx "$TARGET_BDF" || {
    skip "[SKIP] requested PCI BDF not exposed as Ethernet: $TARGET_BDF"
}
TARGET_VENDOR=$(run "cat /sys/bus/pci/devices/$TARGET_BDF/vendor" 2>/dev/null || true)
TARGET_DEVICE=$(run "cat /sys/bus/pci/devices/$TARGET_BDF/device" 2>/dev/null || true)
TARGET_BOUND_DRIVER=$(run "readlink -f /sys/bus/pci/devices/$TARGET_BDF/driver 2>/dev/null | xargs -r basename || true")
test "$TARGET_BOUND_DRIVER" = "$PCI_DRIVER" || {
    skip "[SKIP] $TARGET_BDF bound to ${TARGET_BOUND_DRIVER:-unbound}, expected $PCI_DRIVER"
}
echo "[INFO] target $TARGET_BDF id=$TARGET_VENDOR:$TARGET_DEVICE bound to $TARGET_BOUND_DRIVER"
run 'vppctl show plugins | grep -qi dpdk' || {
    skip '[SKIP] running VPP has no loaded DPDK plugin'
}
STATUS=ENVIRONMENT-OPEN
echo "[PASS] container DPDK plugin, PCI/$PCI_DRIVER and hugepage preflight passed"
echo "[OPEN] no real traffic-generator result supplied; 64-byte performance acceptance remains open"
exit 2
