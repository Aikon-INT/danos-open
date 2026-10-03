#!/usr/bin/env bash
# Capture one I211 live-ISO boot over USB-UART and qualify preflight or traffic evidence.
set -euo pipefail

usage() {
    echo "Usage: $0 /dev/ttyUSB0 <i211-runner.iso> <new-serial-log> [capture-seconds] [--traffic]" >&2
}

if test "$#" -lt 3 || test "$#" -gt 5; then
    usage
    exit 2
fi

serial_device="$1"
iso="$2"
serial_log="$3"
capture_seconds="${4:-180}"
mode="preflight"
if test "$#" -eq 5; then
    test "$5" = --traffic || { usage; exit 2; }
    mode=traffic
elif test "$#" -eq 4 && test "$4" = --traffic; then
    mode=traffic
    capture_seconds=180
fi
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if test "$mode" = traffic; then
    recorder="$root/danos-test/integration/record_i211_traffic_result.py"
    result_file="${serial_log}.traffic.env"
else
    recorder="$root/danos-test/integration/record_i211_boot_result.py"
    result_file="${serial_log}.preflight.env"
fi

test -c "$serial_device" || { echo "ERROR: not a character device: $serial_device" >&2; exit 2; }
test -r "$serial_device" && test -w "$serial_device" || {
    echo "ERROR: serial device is not readable/writable for current user: $serial_device" >&2
    exit 2
}
test -r "$iso" || { echo "ERROR: ISO is not readable: $iso" >&2; exit 2; }
test ! -e "$serial_log" || { echo "ERROR: refusing to overwrite existing log: $serial_log" >&2; exit 2; }
test ! -e "$result_file" || {
    echo "ERROR: refusing to overwrite existing qualification result: $result_file" >&2
    exit 2
}
[[ "$capture_seconds" =~ ^[1-9][0-9]*$ ]] || {
    echo "ERROR: capture-seconds must be a positive integer" >&2; exit 2;
}
command -v stty >/dev/null && command -v timeout >/dev/null || {
    echo 'ERROR: GNU stty and timeout are required' >&2; exit 2;
}
if test "$mode" = traffic; then
    python3 "$root/danos-test/integration/verify_i211_iso_profile.py" \
        "$iso" --traffic-test --minimum-soak-count 1000 || {
        echo 'ERROR: refusing UART traffic capture with an invalid traffic ISO' >&2
        exit 2
    }
fi

mkdir -p "$(dirname "$serial_log")"
stty -F "$serial_device" 115200 cs8 -cstopb -parenb -ixon -ixoff raw -echo
echo "[INFO] capturing $serial_device at 115200 8N1 for up to ${capture_seconds}s" >&2
echo '[INFO] start/power on the I211 machine now; capture ends automatically' >&2

set +e
timeout --foreground --signal=INT "${capture_seconds}s" cat "$serial_device" \
    | tee "$serial_log"
pipeline_status=("${PIPESTATUS[@]}")
set -e
capture_rc="${pipeline_status[0]}"
tee_rc="${pipeline_status[1]}"
if test "$tee_rc" -ne 0; then
    echo "ERROR: failed to persist serial capture (tee status $tee_rc)" >&2
    exit "$tee_rc"
fi
if test "$capture_rc" -ne 0 && test "$capture_rc" -ne 124 && test "$capture_rc" -ne 130; then
    echo "ERROR: serial capture failed with status $capture_rc" >&2
    exit "$capture_rc"
fi

if test "$mode" = traffic; then
    set +e
    python3 "$recorder" --iso "$iso" --serial "$serial_log" --out "$result_file"
    recorder_rc=$?
    set -e
else
    set +e
    python3 "$recorder" \
        --iso "$iso" --serial "$serial_log" \
        --bdf 0000:01:00.0 --bdf 0000:02:00.0 \
        --driver uio_pci_generic --out "$result_file"
    recorder_rc=$?
    set -e
fi
echo "[INFO] serial_log=$serial_log"
echo "[INFO] qualification_result=$result_file"
exit "$recorder_rc"
