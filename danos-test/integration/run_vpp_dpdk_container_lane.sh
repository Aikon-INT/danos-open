#!/bin/bash
# Run the DPDK preflight inside a privileged Debian trixie VPP container.
set -euo pipefail
CONTAINER="${DPDK_CONTAINER:-danos-vpp-host-1789981447}"
TARGET_BDF="${DPDK_PCI_BDF:-}"
PCI_DRIVER="${DPDK_PCI_DRIVER:-vfio-pci}"
RESULT_FILE="${DPDK_RESULT_FILE:-}"
STATUS=FAIL
finish_result() {
    local rc=$?
    test -n "$RESULT_FILE" || return "$rc"
    {
        printf 'status=%s\n' "$STATUS"
        printf 'lane=pci-dpdk\n'
        printf 'commit=%s\n' "$(git rev-parse HEAD 2>/dev/null || true)"
        printf 'iso_sha256=\npacket_size_bytes=64\nflows=\npackets_tx=\npackets_rx=\n'
        printf 'loss_pct=\nduration_ms=\npps=\nmbps=\nrtt_p50_us=\nrtt_p99_us=\ncpu_pct=\n'
        printf 'ecmp_bucket_0=\necmp_bucket_1=\nrestart_replay=SKIP\n'
        printf 'exit_code=%s\n' "$rc"
        printf 'container=%q\n' "$CONTAINER"
        printf 'pci_driver=%q\n' "$PCI_DRIVER"
        printf 'target_bdf=%q\n' "$TARGET_BDF"
        printf 'host_utc=%q\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > "$RESULT_FILE"
    return "$rc"
}
trap finish_result EXIT
skip() { STATUS=SKIP; echo "$1"; exit 2; }
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
if [ -n "$TARGET_BDF" ]; then
    printf '%s\n' "$PCI" | grep -qx "$TARGET_BDF" || {
        skip "[SKIP] requested PCI BDF not exposed: $TARGET_BDF"
    }
    DRIVER=$(run "readlink -f /sys/bus/pci/devices/$TARGET_BDF/driver 2>/dev/null | xargs -r basename || true")
    test "$DRIVER" = "$PCI_DRIVER" || {
        skip "[SKIP] $TARGET_BDF bound to ${DRIVER:-unbound}, expected $PCI_DRIVER"
    }
    echo "[INFO] target $TARGET_BDF bound to $PCI_DRIVER"
fi
run 'vppctl show plugins | grep -qi dpdk' || {
    skip '[SKIP] running VPP has no loaded DPDK plugin'
}
STATUS=PASS
echo "[PASS] container DPDK plugin, PCI/$PCI_DRIVER and hugepage preflight passed"
