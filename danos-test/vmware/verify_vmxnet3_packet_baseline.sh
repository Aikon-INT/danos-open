#!/bin/bash
# Verify the project-local VMware VMXNET3 polling-only packet baseline.
# This is a packet-rate/reachability gate, not a line-rate DPDK benchmark.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PEER_LOG="${VMWARE_PEER_LOG:-$ROOT/build/vmware-vmxnet3-test/peer.serial.log}"
DANOS_LOG="${VMWARE_DANOS_LOG:-$ROOT/build/vmware-vmxnet3-test/danos.serial.log}"
MIN_PPS="${VMWARE_MIN_PPS:-50}"
RESULT_FILE="${VMWARE_RESULT_FILE:-}"
ISO_PATH="${VMWARE_ISO:-$ROOT/build/danos-open-v0.16.0-rc1-vmware-vmxnet3-polling.iso}"
test -s "$PEER_LOG" && test -s "$DANOS_LOG" || {
    echo "[BLOCKED] VMware serial logs missing"; exit 2;
}
test -f "$ISO_PATH" || { echo "[FAIL] VMware test ISO missing: $ISO_PATH"; exit 1; }
if test -z "$RESULT_FILE"; then
    RESULT_FILE="$ROOT/build/v016-vmware-vmxnet3.env"
fi
python3 "$ROOT/danos-test/vmware/record_vmxnet3_packet_result.py" \
    --peer-log "$PEER_LOG" --danos-log "$DANOS_LOG" --iso "$ISO_PATH" \
    --out "$RESULT_FILE" --min-pps "$MIN_PPS"
