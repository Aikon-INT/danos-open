#!/bin/bash
# Unified v0.16.0-rc1 gate. DPDK hardware absence is recorded as OPEN/SKIP.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RESULT_FILE="${V016_GATE_RESULT_FILE:-$ROOT/build/v016-release-gate.env}"
QEMU_TOPOLOGY_DIR="${QEMU_TOPOLOGY_DIR:-$ROOT/build/qemu-frr-vpp-topology-89}"
VMWARE_MIN_PPS="${VMWARE_MIN_PPS:-50}"
failures=0
run_gate() {
    local name="$1"; shift
    echo "=== $name ==="
    "$@" || { echo "[FAIL] $name"; failures=$((failures + 1)); return 0; }
    echo "[PASS] $name"
}

run_gate backend-contract bash "$ROOT/danos-test/integration/run_backend_contract.sh"
run_gate ctest ctest --test-dir "$ROOT/build" --output-on-failure
run_gate qemu-frr-vpp env QEMU_TOPOLOGY_DIR="$QEMU_TOPOLOGY_DIR" \
    bash "$ROOT/danos-test/qemu/verify_frr_vpp_topology.sh"
run_gate vmware-vmxnet3 env VMWARE_MIN_PPS="$VMWARE_MIN_PPS" \
    bash "$ROOT/danos-test/vmware/verify_vmxnet3_packet_baseline.sh"

echo '=== pci-dpdk-preflight ==='
set +e
DPDK_RESULT_FILE="$ROOT/build/v016-dpdk-preflight.env" \
    bash "$ROOT/danos-test/integration/run_vpp_dpdk_lane.sh"
dpdk_rc=$?
set -e
if test "$dpdk_rc" -eq 0; then
    dpdk_status=PASS
    echo '[PASS] pci-dpdk-preflight'
elif test "$dpdk_rc" -eq 2; then
    dpdk_status=ENVIRONMENT-OPEN
    echo '[OPEN] pci-dpdk-preflight (structured SKIP)'
else
    dpdk_status=FAIL
    echo '[FAIL] pci-dpdk-preflight'
    failures=$((failures + 1))
fi

if test "$failures" -eq 0; then overall=PASS; else overall=FAIL; fi
{
    printf 'status=%s\n' "$overall"
    printf 'dpdk_status=%s\n' "$dpdk_status"
    printf 'qemu_topology=%q\n' "$QEMU_TOPOLOGY_DIR"
    printf 'vmware_min_pps=%q\n' "$VMWARE_MIN_PPS"
    printf 'git_commit=%q\n' "$(git -C "$ROOT" rev-parse HEAD)"
    printf 'utc=%q\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$RESULT_FILE"
echo "[${overall}] v0.16.0-rc1 gate; result=$RESULT_FILE"
test "$overall" = PASS
