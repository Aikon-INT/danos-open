#!/bin/bash
# Unified v0.16.0-rc1 gate. DPDK hardware absence is recorded as OPEN/SKIP.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RESULT_FILE="${V016_GATE_RESULT_FILE:-$ROOT/build/v016-release-gate.env}"
QEMU_TOPOLOGY_DIR="${QEMU_TOPOLOGY_DIR:-$ROOT/build/qemu-frr-vpp-topology-110}"
VMWARE_MIN_PPS="${VMWARE_MIN_PPS:-50}"
VMWARE_ISO="${V016_VMWARE_ISO:-$ROOT/build/danos-open-v0.16.0-rc1-vmware-vmxnet3-polling-clean.iso}"
VMWARE_DANOS_LOG="${V016_VMWARE_DANOS_LOG:-$ROOT/build/vmware-vmxnet3-test/danos-clean.serial.log}"
VMWARE_PEER_LOG="${V016_VMWARE_PEER_LOG:-$ROOT/build/vmware-vmxnet3-test/peer-clean.serial.log}"
# Exercise the console and mgrd without enabling a hardware-specific DPDK
# profile.  The I211 image binds PCI NICs for the physical runner and is not a
# suitable generic live-console smoke image.
USB_KEYBOARD_ISO="${V016_USB_KEYBOARD_ISO:-$ROOT/build/danos-open-v0.16.0-rc1-live-console.iso}"
failures=0
run_gate() {
    local name="$1"; shift
    echo "=== $name ==="
    "$@" || { echo "[FAIL] $name"; failures=$((failures + 1)); return 0; }
    echo "[PASS] $name"
}

run_gate backend-contract bash "$ROOT/danos-test/integration/run_backend_contract.sh"
run_gate pci-result-validator python3 "$ROOT/danos-test/integration/test_record_dpdk_perf_result.py"
run_gate i211-boot-result-validator python3 "$ROOT/danos-test/integration/test_record_i211_boot_result.py"
run_gate i211-serial-capture-syntax bash -n "$ROOT/danos-test/integration/capture_i211_serial.sh"
run_gate i211-serial-capture-pty python3 "$ROOT/danos-test/integration/test_capture_i211_serial.py" \
    --iso "$USB_KEYBOARD_ISO"
run_gate vmware-result-validator python3 "$ROOT/danos-test/vmware/test_record_vmxnet3_packet_result.py"
run_gate ctest ctest --test-dir "$ROOT/build" --output-on-failure
run_gate qemu-live-console-and-mgrd-recovery \
    python3 "$ROOT/danos-test/live/verify_usb_keyboard_qemu.py" \
    "$USB_KEYBOARD_ISO" --mgrd-restart
run_gate qemu-live-serial-root-shell \
    python3 "$ROOT/danos-test/live/verify_serial_shell_qemu.py" \
    "$USB_KEYBOARD_ISO" --serial-log "$ROOT/build/v016-serial-root-shell-qemu.log"
run_gate qemu-frr-vpp env QEMU_TOPOLOGY_DIR="$QEMU_TOPOLOGY_DIR" \
    bash "$ROOT/danos-test/qemu/verify_frr_vpp_topology.sh"
run_gate vmware-vmxnet3 env VMWARE_MIN_PPS="$VMWARE_MIN_PPS" \
    VMWARE_ISO="$VMWARE_ISO" VMWARE_DANOS_LOG="$VMWARE_DANOS_LOG" \
    VMWARE_PEER_LOG="$VMWARE_PEER_LOG" \
    VMWARE_RESULT_FILE="$ROOT/build/v016-vmware-vmxnet3.env" \
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
    dpdk_result_status=$(sed -n 's/^status=//p' "$ROOT/build/v016-dpdk-preflight.env" | head -1)
    dpdk_preflight_status=$(sed -n 's/^preflight_status=//p' "$ROOT/build/v016-dpdk-preflight.env" | head -1)
    if test "$dpdk_result_status" = ENVIRONMENT-OPEN && test "$dpdk_preflight_status" = PASS; then
        echo '[OPEN] pci-dpdk-preflight passed; traffic measurements are still open'
    else
        echo '[OPEN] pci-dpdk-preflight skipped; runner prerequisites are unavailable'
    fi
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
    printf 'vmware_iso=%q\n' "$VMWARE_ISO"
    printf 'vmware_result=%q\n' "$ROOT/build/v016-vmware-vmxnet3.env"
    printf 'dpdk_result=%q\n' "$ROOT/build/v016-dpdk-preflight.env"
    printf 'git_commit=%q\n' "$(git -C "$ROOT" rev-parse HEAD)"
    printf 'utc=%q\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$RESULT_FILE"
echo "[${overall}] v0.16.0-rc1 gate; result=$RESULT_FILE"
test "$overall" = PASS
