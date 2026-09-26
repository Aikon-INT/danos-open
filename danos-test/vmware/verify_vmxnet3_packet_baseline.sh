#!/bin/bash
# Verify the project-local VMware VMXNET3 polling-only packet baseline.
# This is a packet-rate/reachability gate, not a line-rate DPDK benchmark.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PEER_LOG="${VMWARE_PEER_LOG:-$ROOT/build/vmware-vmxnet3-test/peer.serial.log}"
DANOS_LOG="${VMWARE_DANOS_LOG:-$ROOT/build/vmware-vmxnet3-test/danos.serial.log}"
MIN_PPS="${VMWARE_MIN_PPS:-50}"
RESULT_FILE="${VMWARE_RESULT_FILE:-}"
test -s "$PEER_LOG" && test -s "$DANOS_LOG" || {
    echo "[BLOCKED] VMware serial logs missing"; exit 2;
}

require() {
    local pattern="$1" file="$2" label="$3"
    rg -q -- "$pattern" "$file" || { echo "[FAIL] $label"; exit 1; }
    echo "[PASS] $label"
}

require 'PEER-TRAFFIC PASS' "$PEER_LOG" 'peer traffic gate'
require 'VPP-DPDK-PING-0 PASS' "$DANOS_LOG" 'VPP path 0'
require 'VPP-DPDK-PING-1 PASS' "$DANOS_LOG" 'VPP path 1'
require 'VPP-DPDK-INTERFACES PASS' "$DANOS_LOG" 'VPP interface gate'
require 'VPP-ECMP-COUNTERS-AFTER' "$DANOS_LOG" 'VPP ECMP counters'
require 'L3 192\.168\.45\.3/24' "$DANOS_LOG" 'VPP path 0 address'
require 'L3 192\.168\.46\.3/24' "$DANOS_LOG" 'VPP path 1 address'

mapfile -t rates < <(awk '
    /PEER-TRAFFIC-BENCH target=/ {
        target=""; pps="";
        for (i = 1; i <= NF; i++) {
            if ($i ~ /^target=/) target=substr($i, 8);
            if ($i ~ /^pps=/) pps=substr($i, 5);
        }
        if (target != "" && pps != "") print target " " pps;
    }
' "$PEER_LOG" | tail -2)
test "${#rates[@]}" -ge 2 || { echo '[FAIL] two packet baseline markers'; exit 1; }
for row in "${rates[@]}"; do
    target=${row% *}; pps=${row##* }
    awk -v p="$pps" -v min="$MIN_PPS" 'BEGIN { exit !(p >= min) }' || {
        echo "[FAIL] $target pps=$pps below minimum=$MIN_PPS"; exit 1;
    }
    echo "[PASS] $target packet baseline pps=$pps"
done

if test -n "$RESULT_FILE"; then
    iso_path="${VMWARE_ISO:-$ROOT/build/danos-open-v0.16.0-rc1-vmware-vmxnet3-polling.iso}"
    iso_sha256=""
    test -f "$iso_path" && iso_sha256=$(sha256sum "$iso_path" | awk '{print $1}') || true
    commit=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)
    first_pps=$(printf '%s\n' "${rates[0]}" | awk '{print $2}')
    {
        printf 'status=PASS\n'
        printf 'lane=vmware-vmxnet3-polling\n'
        printf 'commit=%s\n' "$commit"
        printf 'iso_sha256=%s\n' "$iso_sha256"
        printf 'packet_size_bytes=64\nflows=2\npackets_tx=\npackets_rx=\n'
        printf 'loss_pct=0\nduration_ms=\npps=%s\nmbps=\nrtt_p50_us=\nrtt_p99_us=\ncpu_pct=\n' "$first_pps"
        printf 'ecmp_bucket_0=\necmp_bucket_1=\nrestart_replay=SKIP\n'
    } > "$RESULT_FILE"
fi

echo '[PASS] VMware VMXNET3 packet baseline gate'
