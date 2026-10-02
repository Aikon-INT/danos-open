#!/usr/bin/env bash
# Capture one I211 live-ISO boot over a USB-UART and qualify its preflight log.
set -euo pipefail

usage() {
    echo "Usage: $0 /dev/ttyUSB0 <i211-runner.iso> <new-serial-log> [capture-seconds]" >&2
}

if test "$#" -lt 3 || test "$#" -gt 4; then
    usage
    exit 2
fi

serial_device="$1"
iso="$2"
serial_log="$3"
capture_seconds="${4:-180}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
recorder="$root/danos-test/integration/record_i211_boot_result.py"

test -c "$serial_device" || { echo "ERROR: not a character device: $serial_device" >&2; exit 2; }
test -r "$serial_device" && test -w "$serial_device" || {
    echo "ERROR: serial device is not readable/writable for current user: $serial_device" >&2
    exit 2
}
test -r "$iso" || { echo "ERROR: ISO is not readable: $iso" >&2; exit 2; }
test ! -e "$serial_log" || { echo "ERROR: refusing to overwrite existing log: $serial_log" >&2; exit 2; }
test ! -e "${serial_log}.preflight.env" || {
    echo "ERROR: refusing to overwrite existing preflight: ${serial_log}.preflight.env" >&2
    exit 2
}
[[ "$capture_seconds" =~ ^[1-9][0-9]*$ ]] || {
    echo "ERROR: capture-seconds must be a positive integer" >&2; exit 2;
}
command -v stty >/dev/null && command -v timeout >/dev/null || {
    echo 'ERROR: GNU stty and timeout are required' >&2; exit 2;
}

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

result_file="${serial_log}.preflight.env"
set +e
python3 "$recorder" \
    --iso "$iso" --serial "$serial_log" \
    --bdf 0000:01:00.0 --bdf 0000:02:00.0 \
    --driver uio_pci_generic --out "$result_file"
recorder_rc=$?
set -e
echo "[INFO] serial_log=$serial_log"
echo "[INFO] preflight_result=$result_file"
exit "$recorder_rc"
