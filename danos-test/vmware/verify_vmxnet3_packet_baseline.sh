#!/bin/bash
# Verify the project-local VMware VMXNET3 polling-only packet baseline.
# This is a packet-rate/reachability gate, not a line-rate DPDK benchmark.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PEER_LOG="${VMWARE_PEER_LOG:-$ROOT/build/vmware-vmxnet3-test/peer.serial.log}"
DANOS_LOG="${VMWARE_DANOS_LOG:-$ROOT/build/vmware-vmxnet3-test/danos.serial.log}"
MIN_PPS="${VMWARE_MIN_PPS:-50}"
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

echo '[PASS] VMware VMXNET3 packet baseline gate'
