#!/bin/bash
# Run the DPDK preflight inside a privileged Debian trixie VPP container.
set -euo pipefail
CONTAINER="${DPDK_CONTAINER:-danos-vpp-host-1789981447}"
docker inspect "$CONTAINER" >/dev/null 2>&1 || {
    echo "[SKIP] DPDK container unavailable: $CONTAINER"; exit 2;
}
run() { docker exec "$CONTAINER" sh -lc "$1"; }
run 'command -v vpp >/dev/null' || { echo "[SKIP] VPP binary unavailable"; exit 2; }
run 'test -r /usr/lib/x86_64-linux-gnu/vpp_plugins/dpdk_plugin.so' || {
    echo "[SKIP] DPDK plugin unavailable"; exit 2;
}
run 'test -e /dev/vfio/vfio' || { echo "[SKIP] VFIO unavailable"; exit 2; }
run "grep -Eq 'HugePages_Total:[[:space:]]+[1-9]' /proc/meminfo" || {
    echo "[SKIP] hugepages not configured"; exit 2;
}
PCI=$(run "for d in /sys/bus/pci/devices/*; do test -e \"\$d/class\" || continue; test \"\$(cat \"\$d/class\")\" = 0x020000 && basename \"\$d\"; done" || true)
test -n "$PCI" || { echo "[SKIP] no PCI Ethernet device exposed"; exit 2; }
echo "[INFO] PCI Ethernet: $PCI"
run 'vppctl show plugins | grep -qi dpdk' || {
    echo "[SKIP] running VPP has no loaded DPDK plugin"; exit 2;
}
echo "[PASS] container DPDK plugin, PCI, VFIO and hugepage preflight passed"
