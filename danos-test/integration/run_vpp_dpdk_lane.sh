#!/bin/bash
# VPP+DPDK dedicated lane preflight. Missing PCI/VFIO is an explicit SKIP.
set -euo pipefail
PLUGIN="${VPP_DPDK_PLUGIN:-/usr/lib/x86_64-linux-gnu/vpp_plugins/dpdk_plugin.so}"
command -v vpp >/dev/null 2>&1 || { echo "[SKIP] vpp binary unavailable"; exit 2; }
test -r "$PLUGIN" || { echo "[SKIP] DPDK plugin unavailable: $PLUGIN"; exit 2; }
PCI_COUNT=0
for d in /sys/bus/pci/devices/*; do test -e "$d" || continue; PCI_COUNT=$((PCI_COUNT + 1)); done
test "$PCI_COUNT" -gt 0 || { echo "[SKIP] no PCI device exposed; bind NIC/VF to vfio-pci"; exit 2; }
test -e /dev/vfio/vfio || { echo "[SKIP] /dev/vfio/vfio unavailable"; exit 2; }
grep -Eq 'HugePages_Total:[[:space:]]+[1-9]' /proc/meminfo || { echo "[SKIP] hugepages not configured"; exit 2; }
test -S /run/vpp/api.sock || { echo "[FAIL] VPP API socket unavailable"; exit 1; }
vppctl show plugins | grep -qi dpdk || { echo "[FAIL] VPP running without DPDK plugin"; exit 1; }
echo "[PASS] DPDK plugin, PCI/VFIO and hugepage preflight passed"
echo "[INFO] traffic generator must now verify the configured 64-byte single-core target"
