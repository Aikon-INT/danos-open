#!/bin/bash
# Assert the real QEMU FRR -> DANOS -> VPP route lifecycle evidence.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="${QEMU_TOPOLOGY_DIR:-$ROOT/build/qemu-frr-vpp-topology-16}"
DANOS_LOG="$T/danos.serial.log"
FRR_LOG="$T/frr-1.serial.log"
test -s "$DANOS_LOG" && test -s "$FRR_LOG" || { echo "[BLOCKED] topology logs missing: $T"; exit 2; }

require() {
    local pattern="$1" file="$2" label="$3"
    rg -q -- "$pattern" "$file" || { echo "[FAIL] $label"; exit 1; }
    echo "[PASS] $label"
}

require 'VPP API socket: ready' "$DANOS_LOG" 'VPP API socket'
require 'VPP stats socket: ready' "$DANOS_LOG" 'VPP stats socket'
require 'VPP-DPDK-PING-0 PASS' "$DANOS_LOG" 'packet reachability path 0'
require 'VPP-DPDK-PING-1 PASS' "$DANOS_LOG" 'packet reachability path 1'
require 'VPP-ECMP-MULTI-FLOW PASS' "$DANOS_LOG" 'ECMP multi-destination traffic'
require 'VPP-ECMP-COUNTERS-BEFORE' "$DANOS_LOG" 'ECMP counters before traffic'
require 'VPP-ECMP-COUNTERS-AFTER' "$DANOS_LOG" 'ECMP counters after traffic'
require 'VPP-RESTART-TEST PASS' "$DANOS_LOG" 'VPP process restart and route replay'
require 'command=31' "$DANOS_LOG" 'ZAPI route add'
require 'command=32' "$DANOS_LOG" 'ZAPI route withdraw'
require 'vpp route add' "$DANOS_LOG" 'VPP route programming'
require 'add-rc=0' "$FRR_LOG" 'FRR route add'
require 'withdraw-rc=0' "$FRR_LOG" 'FRR route withdraw'
require 'restore-rc=0' "$FRR_LOG" 'FRR route restore'
require 'cycle-done' "$FRR_LOG" 'FRR cycle completion'
if rg -q 'programming failed|zapi peer EOF|Syntax error' "$DANOS_LOG" "$FRR_LOG"; then
    echo '[FAIL] runtime error marker present'
    exit 1
fi
echo '[PASS] FRR -> DPA -> VPP IPv4 add/withdraw/restore gate'
