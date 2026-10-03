#!/bin/bash
# Unified v0.16.0-rc1 gate. DPDK hardware absence is recorded as OPEN/SKIP.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GIT_COMMIT="$(git -C "$ROOT" rev-parse HEAD)"
RESULT_FILE="${V016_GATE_RESULT_FILE:-$ROOT/build/v016-release-gate.env}"
DPDK_RESULT_FILE="${DPDK_RESULT_FILE:-$ROOT/build/v016-dpdk-preflight.env}"
QEMU_TOPOLOGY_DIR="${QEMU_TOPOLOGY_DIR:-$ROOT/build/qemu-frr-vpp-topology-140-soak-serialized}"
QEMU_ECMP_SOAK_REQUIRED="${QEMU_ECMP_SOAK_REQUIRED:-1}"
QEMU_ECMP_SOAK_COUNT="${QEMU_ECMP_SOAK_COUNT:-1000}"
QEMU_RESULT_FILE="${V016_QEMU_RESULT_FILE:-$ROOT/build/v016-qemu-ecmp-soak-${GIT_COMMIT:0:7}.env}"
if test "$QEMU_ECMP_SOAK_REQUIRED" != 1; then
    echo '[FAIL] release gate requires QEMU_ECMP_SOAK_REQUIRED=1' >&2
    exit 2
fi
if ! [[ "$QEMU_ECMP_SOAK_COUNT" =~ ^[0-9]+$ ]] ||
   (( 10#$QEMU_ECMP_SOAK_COUNT < 1000 )); then
    echo '[FAIL] release gate requires at least 1000 packets per ECMP flow' >&2
    exit 2
fi
VMWARE_MIN_PPS="${VMWARE_MIN_PPS:-50}"
VMWARE_ISO="${V016_VMWARE_ISO:-$ROOT/build/danos-open-v0.16.0-rc1-vmware-vmxnet3-polling-clean.iso}"
VMWARE_DANOS_LOG="${V016_VMWARE_DANOS_LOG:-$ROOT/build/vmware-vmxnet3-test/danos-clean.serial.log}"
VMWARE_PEER_LOG="${V016_VMWARE_PEER_LOG:-$ROOT/build/vmware-vmxnet3-test/peer-clean.serial.log}"
# Exercise the console and mgrd without enabling a hardware-specific DPDK
# profile.  The I211 image binds PCI NICs for the physical runner and is not a
# suitable generic live-console smoke image.
USB_KEYBOARD_ISO="${V016_USB_KEYBOARD_ISO:-$ROOT/build/danos-open-v0.16.0-rc1-i211-dpdk-polling-runner-r13.iso}"
I211_ISO_COMMIT="${V016_I211_ISO_COMMIT:-88f4bed22f2490e8aaeb081d015243636f538cb6}"
I211_TRAFFIC_ISO="${V016_I211_TRAFFIC_ISO:-$ROOT/build/danos-open-v0.16.0-rc1-i211-dpdk-traffic-runner-r1.iso}"
I211_TRAFFIC_ISO_COMMIT="${V016_I211_TRAFFIC_ISO_COMMIT:-72dcd8b04ed22db12388a4302ea38921f005932d}"
failures=0
run_gate() {
    local name="$1"; shift
    echo "=== $name ==="
    "$@" || { echo "[FAIL] $name"; failures=$((failures + 1)); return 0; }
    echo "[PASS] $name"
}

run_gate backend-contract bash "$ROOT/danos-test/integration/run_backend_contract.sh"
run_gate release-gate-soak-config python3 \
    "$ROOT/danos-test/integration/test_v016_release_gate_config.py"
run_gate qemu-soak-result-validator python3 \
    "$ROOT/danos-test/qemu/test_record_qemu_ecmp_soak_result.py"
run_gate pci-result-validator python3 "$ROOT/danos-test/integration/test_record_dpdk_perf_result.py"
run_gate dpdk-preflight-result-schema python3 \
    "$ROOT/danos-test/integration/test_dpdk_preflight_result.py"
run_gate i211-boot-result-validator python3 "$ROOT/danos-test/integration/test_record_i211_boot_result.py"
run_gate i211-traffic-result-validator python3 \
    "$ROOT/danos-test/integration/test_record_i211_traffic_result.py"
run_gate i211-iso-profile-validator python3 \
    "$ROOT/danos-test/integration/test_verify_i211_iso_profile.py"
run_gate i211-traffic-runner-config python3 \
    "$ROOT/danos-test/integration/test_i211_traffic_runner_builder.py"
run_gate i211-traffic-runner-builder-syntax bash -n \
    "$ROOT/danos-test/live/build_i211_traffic_runner_iso.sh"
run_gate i211-traffic-iso-profile python3 \
    "$ROOT/danos-test/integration/verify_i211_iso_profile.py" "$I211_TRAFFIC_ISO" \
    --traffic-test --minimum-soak-count 1000 --expected-commit "$I211_TRAFFIC_ISO_COMMIT"
run_gate i211-traffic-serial-capture-pty python3 \
    "$ROOT/danos-test/integration/test_capture_i211_traffic_serial.py" --iso "$I211_TRAFFIC_ISO"
run_gate i211-runner-iso-profile python3 \
    "$ROOT/danos-test/integration/verify_i211_iso_profile.py" "$USB_KEYBOARD_ISO" \
    --expected-commit "$I211_ISO_COMMIT"
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
run_gate qemu-i211-vpp-ping-plugin-runtime \
    python3 "$ROOT/danos-test/live/verify_serial_shell_qemu.py" \
    "$USB_KEYBOARD_ISO" \
    --shell-command "if /usr/bin/vppctl -s /run/vpp/cli.sock show plugins | grep -q ping_plugin.so; then printf 'RUNTIME_PLUGIN_OK'; else printf 'RUNTIME_PLUGIN_MISSING'; fi" \
    --expect-output RUNTIME_PLUGIN_OK \
    --serial-log "$ROOT/build/v016-i211-runtime-plugin-qemu.log"
qemu_gate_failures_before=$failures
run_gate qemu-frr-vpp env QEMU_TOPOLOGY_DIR="$QEMU_TOPOLOGY_DIR" \
    QEMU_ECMP_SOAK_REQUIRED="$QEMU_ECMP_SOAK_REQUIRED" \
    QEMU_ECMP_SOAK_COUNT="$QEMU_ECMP_SOAK_COUNT" \
    bash "$ROOT/danos-test/qemu/verify_frr_vpp_topology.sh"
if test "$failures" -eq "$qemu_gate_failures_before"; then
    qemu_result_failures_before=$failures
    run_gate qemu-ecmp-soak-result python3 \
        "$ROOT/danos-test/qemu/record_qemu_ecmp_soak_result.py" \
        --topology "$QEMU_TOPOLOGY_DIR" --out "$QEMU_RESULT_FILE" \
        --required-count "$QEMU_ECMP_SOAK_COUNT"
    if test "$failures" -eq "$qemu_result_failures_before"; then
        qemu_result_status=PASS
    else
        qemu_result_status=FAIL
    fi
else
    qemu_result_status=SKIP
fi
if test "$failures" -eq "$qemu_gate_failures_before"; then
    qemu_gate_status=PASS
else
    qemu_gate_status=FAIL
fi
run_gate vmware-vmxnet3 env VMWARE_MIN_PPS="$VMWARE_MIN_PPS" \
    VMWARE_ISO="$VMWARE_ISO" VMWARE_DANOS_LOG="$VMWARE_DANOS_LOG" \
    VMWARE_PEER_LOG="$VMWARE_PEER_LOG" \
    VMWARE_RESULT_FILE="$ROOT/build/v016-vmware-vmxnet3.env" \
    bash "$ROOT/danos-test/vmware/verify_vmxnet3_packet_baseline.sh"

echo '=== pci-dpdk-preflight ==='
set +e
DPDK_RESULT_FILE="$DPDK_RESULT_FILE" \
    bash "$ROOT/danos-test/integration/run_vpp_dpdk_lane.sh"
dpdk_rc=$?
set -e
if test "$dpdk_rc" -eq 0; then
    dpdk_status=PASS
    echo '[PASS] pci-dpdk-preflight'
elif test "$dpdk_rc" -eq 2; then
    dpdk_status=ENVIRONMENT-OPEN
    dpdk_result_status=$(sed -n 's/^status=//p' "$DPDK_RESULT_FILE" | head -1)
    dpdk_preflight_status=$(sed -n 's/^preflight_status=//p' "$DPDK_RESULT_FILE" | head -1)
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
    printf 'qemu_gate_status=%s\nqemu_ecmp_soak_required=%q\nqemu_ecmp_soak_count=%q\n' \
        "$qemu_gate_status" "$QEMU_ECMP_SOAK_REQUIRED" "$QEMU_ECMP_SOAK_COUNT"
    printf 'qemu_result_status=%s\n' "$qemu_result_status"
    printf 'qemu_topology=%q\n' "$QEMU_TOPOLOGY_DIR"
    printf 'qemu_result=%q\n' "$QEMU_RESULT_FILE"
    printf 'qemu_iso=%q\n' "$(sed -n 's/^danos_iso=//p' "$QEMU_TOPOLOGY_DIR/run-manifest.env" | head -1)"
    printf 'qemu_iso_sha256=%q\n' "$(sed -n 's/^danos_iso_sha256=//p' "$QEMU_TOPOLOGY_DIR/run-manifest.env" | head -1)"
    printf 'vmware_min_pps=%q\n' "$VMWARE_MIN_PPS"
    printf 'vmware_iso=%q\n' "$VMWARE_ISO"
    printf 'vmware_result=%q\n' "$ROOT/build/v016-vmware-vmxnet3.env"
    printf 'i211_iso=%q\n' "$USB_KEYBOARD_ISO"
    printf 'i211_iso_sha256=%q\n' "$(sha256sum "$USB_KEYBOARD_ISO" | awk '{print $1}')"
    printf 'i211_iso_commit=%q\n' "$I211_ISO_COMMIT"
    printf 'i211_traffic_iso=%q\n' "$I211_TRAFFIC_ISO"
    printf 'i211_traffic_iso_sha256=%q\n' "$(sha256sum "$I211_TRAFFIC_ISO" | awk '{print $1}')"
    printf 'i211_traffic_iso_commit=%q\n' "$I211_TRAFFIC_ISO_COMMIT"
    printf 'dpdk_result=%q\n' "$DPDK_RESULT_FILE"
    printf 'git_commit=%q\n' "$GIT_COMMIT"
    printf 'utc=%q\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$RESULT_FILE"
echo "[${overall}] v0.16.0-rc1 gate; result=$RESULT_FILE"
test "$overall" = PASS
