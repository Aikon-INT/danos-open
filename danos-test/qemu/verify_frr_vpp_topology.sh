#!/bin/bash
# Assert the real QEMU FRR -> DANOS -> VPP route lifecycle evidence.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="${QEMU_TOPOLOGY_DIR:-$ROOT/build/qemu-frr-vpp-topology-16}"
DANOS_LOG="$T/danos.serial.log"
FRR_LOG="$T/frr-1.serial.log"
test -s "$DANOS_LOG" && test -s "$FRR_LOG" || { echo "[BLOCKED] topology logs missing: $T"; exit 2; }
test -s "$T/run-manifest.env" || { echo "[FAIL] topology run manifest missing"; exit 1; }
rg -q '^danos_iso_sha256=[0-9a-f]{64}$' "$T/run-manifest.env" || {
    echo "[FAIL] topology run manifest has no ISO digest"; exit 1;
}
echo '[PASS] reproducible QEMU run manifest'

require() {
    local pattern="$1" file="$2" label="$3"
    rg -q -- "$pattern" "$file" || { echo "[FAIL] $label"; exit 1; }
    echo "[PASS] $label"
}

if test "${QEMU_ECMP_SOAK_REQUIRED:-0}" = 1; then
    soak_count="${QEMU_ECMP_SOAK_COUNT:-1000}"
    require "VPP-ECMP-SOAK PASS flows=4 packets_per_flow=${soak_count} total_tx=$((soak_count * 4)) total_rx=$((soak_count * 4)) loss=0 elapsed_ms=[1-9][0-9]* pps=[1-9][0-9]*\\.[0-9][0-9] bucket0_packets=[1-9][0-9]* bucket1_packets=[1-9][0-9]*" \
        "$DANOS_LOG" 'four-flow lossless ECMP soak with timing, rate and both bucket deltas'
    for destination in 30.30.30.2 30.30.30.3 30.30.30.4 30.30.30.5; do
        require "VPP-ECMP-SOAK-FLOW target=${destination} tx=${soak_count} rx=${soak_count} loss=0" \
            "$DANOS_LOG" "ECMP soak flow ${destination} loss-free"
    done
    if rg -q 'VPP-ECMP-SOAK-(FLOW-FAIL|FAIL)' "$DANOS_LOG"; then
        echo '[FAIL] ECMP soak failure marker present'
        exit 1
    fi
fi

require 'VPP API socket: ready' "$DANOS_LOG" 'VPP API socket'
require 'VPP stats socket: ready' "$DANOS_LOG" 'VPP stats socket'
require 'VPP-DPDK-PING-0 PASS' "$DANOS_LOG" 'packet reachability path 0'
require 'VPP-DPDK-PING-1 PASS' "$DANOS_LOG" 'packet reachability path 1'
require 'VPP-TRAFFIC-PEER-READY PASS endpoint=10\.0\.3\.2:[0-9]+' "$DANOS_LOG" 'FRR dataplane peer readiness'
require 'VPP-ECMP-FIB PASS buckets=([2-9]|[1-9][0-9]+)' "$DANOS_LOG" 'resolved ECMP forwarding buckets'
require 'VPP-ECMP-SOURCE PASS address=30\.30\.30\.1 interface=loop[0-9]+' "$DANOS_LOG" 'symmetric ECMP probe source'
require 'VPP-ECMP-MULTI-FLOW PASS' "$DANOS_LOG" 'ECMP multi-destination traffic'
for destination in 30.30.30.2 30.30.30.3 30.30.30.4 30.30.30.5; do
    require "VPP-ECMP-FLOW target=${destination} tx=3 rx=3 loss=0" "$DANOS_LOG" \
        "ECMP flow ${destination} received all probes"
done
require 'VPP-ECMP-BUCKETS bucket0_packets=[1-9][0-9]* bucket1_packets=[1-9][0-9]*' \
    "$DANOS_LOG" 'traffic observed on both ECMP output interfaces'
require 'VPP-ECMP-NH-WITHDRAW PASS nexthop=10\.20\.0\.2 interface=GigabitEthernet0/3/0' \
    "$DANOS_LOG" 'second ECMP next hop withdrawn from VPP FIB'
require 'VPP-ECMP-PATH-DOWN-BUCKETS buckets=1' "$DANOS_LOG" \
    'FIB converged to the surviving ECMP bucket'
for destination in 30.30.30.2 30.30.30.3 30.30.30.4 30.30.30.5; do
    require "VPP-ECMP-PATH-DOWN-FLOW target=${destination} tx=5 rx=5 loss=0" "$DANOS_LOG" \
        "surviving ECMP path forwards ${destination} during next-hop withdrawal"
done
require 'VPP-ECMP-PATH-UP-FLOW target=30\.30\.30\.[2345] tx=5 rx=5 loss=0' \
    "$DANOS_LOG" 'restored ECMP paths forward sampled destinations'
require 'VPP-ECMP-NH-RESTORE PASS nexthop=10\.20\.0\.2 interface=GigabitEthernet0/3/0' \
    "$DANOS_LOG" 'second ECMP next hop restored into VPP FIB'
require 'VPP-ECMP-PATH-RESTORE PASS interface=GigabitEthernet0/3/0 buckets=([2-9]|[1-9][0-9]+) probes=20' \
    "$DANOS_LOG" 'restored path rejoins ECMP and sampled destinations recover'
if rg -q "unknown input .*ping|VPP-DPDK-PING-[01] FAIL|VPP-TRAFFIC-PEER-READY FAIL|VPP-ECMP-FIB FAIL|VPP-ECMP-SOURCE FAIL|VPP-ECMP-FLOW FAIL|VPP-ECMP-MULTI-FLOW FAIL|VPP-ECMP-(NH-(WITHDRAW|RESTORE)|PATH-(DOWN|UP))-FAIL|VPP-ECMP-PATH-(DOWN|UP)-FLOW-FAIL|VPP-ECMP-PATH-(FAILOVER|RESTORE) FAIL" "$DANOS_LOG"; then
    echo '[FAIL] VPP ping/ECMP command failed despite any PASS markers'
    exit 1
fi
require 'VPP-RESTART-TEST PASS' "$DANOS_LOG" 'VPP process restart and route replay'
require 'frr route event type=10 add=1' "$DANOS_LOG" 'BGP route add into DANOS'
require 'frr route event type=10 add=0' "$DANOS_LOG" 'BGP route withdraw from DANOS'
require 'FRR-PEER-ROUTE-READY PASS' "$FRR_LOG" 'FRR BGP peer route learned before dataplane traffic'
require 'Full' "$FRR_LOG" 'FRR OSPF adjacency'
require 'command=31' "$DANOS_LOG" 'ZAPI route add'
require 'command=32' "$DANOS_LOG" 'ZAPI route withdraw'
require 'vpp route add' "$DANOS_LOG" 'VPP route programming'
require 'add-rc=0' "$FRR_LOG" 'FRR route add'
require 'withdraw-rc=0' "$FRR_LOG" 'FRR route withdraw'
require 'restore-rc=0' "$FRR_LOG" 'FRR route restore'
require 'cycle-done' "$FRR_LOG" 'FRR cycle completion'
require 'FRR-RESTART-TEST PASS' "$FRR_LOG" 'FRR daemon restart and zserv recovery'
if rg -q 'programming failed|Syntax error' "$DANOS_LOG" "$FRR_LOG"; then
    echo '[FAIL] runtime error marker present'
    exit 1
fi
if rg -q 'zapi peer EOF' "$DANOS_LOG" "$FRR_LOG" && \
   ! rg -q 'FRR-RESTART-TEST PASS' "$FRR_LOG"; then
    echo '[FAIL] unexpected zserv EOF without FRR restart recovery'
    exit 1
fi
echo '[PASS] FRR -> DPA -> VPP IPv4 add/withdraw/restore gate'
