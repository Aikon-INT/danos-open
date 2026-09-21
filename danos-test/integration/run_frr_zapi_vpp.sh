#!/bin/bash
# Live FRR zebra ZAPI -> DANOS FIB/DPA -> VPP acceptance harness.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BRIDGE="${BRIDGE:-$ROOT/build/danos-test/fib_live_bridge}"
ZEBRA_SOCK="${ZEBRA_SOCK:-/var/run/frr/zserv.api}"
VPP_SOCK="${VPP_SOCK:-/run/vpp/api.sock}"
MESSAGES="${MESSAGES:-1}"
VPP_IFINDEX_MAP="${DANOS_VPP_IFINDEX_MAP:-}"
RECONNECT=0
usage() { echo "usage: $0 [--zebra-sock PATH] [--vpp-sock PATH] [--messages N] [--reconnect]"; }
while [ "$#" -gt 0 ]; do
    case "$1" in
        --zebra-sock) ZEBRA_SOCK="$2"; shift 2 ;;
        --vpp-sock) VPP_SOCK="$2"; shift 2 ;;
        --messages) MESSAGES="$2"; shift 2 ;;
        --reconnect) RECONNECT=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done
test -x "$BRIDGE" || { echo "[BLOCKED] bridge binary missing: $BRIDGE"; exit 2; }
test -S "$ZEBRA_SOCK" || { echo "[BLOCKED] zebra socket missing: $ZEBRA_SOCK"; exit 2; }
test -S "$VPP_SOCK" || { echo "[BLOCKED] VPP API socket missing: $VPP_SOCK"; exit 2; }
args=(--zebra-sock "$ZEBRA_SOCK" --vpp-sock "$VPP_SOCK" --messages "$MESSAGES")
[ "$RECONNECT" -eq 1 ] && args+=(--reconnect)
echo "[INFO] running FRR ZAPI -> DPA -> VPP bridge"
if [ -n "$VPP_IFINDEX_MAP" ]; then
    echo "[INFO] using FRR->VPP ifindex map: $VPP_IFINDEX_MAP"
    export DANOS_VPP_IFINDEX_MAP="$VPP_IFINDEX_MAP"
fi
"$BRIDGE" "${args[@]}"
echo "[PASS] FRR ZAPI -> DPA -> VPP bridge processed $MESSAGES message(s)"
