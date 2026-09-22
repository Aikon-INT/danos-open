#!/bin/bash
# Dynamic FRR protocol -> ZAPI -> DANOS FIB/DPA -> VPP gate.
# The FRR topology must be provisioned by the caller; this gate never mocks
# zebra or fabricates a route event.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BRIDGE="${BRIDGE:-$ROOT/build/danos-test/fib_live_bridge}"
ZEBRA_SOCK="${ZEBRA_SOCK:-/var/run/frr/zserv.api}"
VPP_SOCK="${VPP_SOCK:-/run/vpp/api.sock}"
PROTOCOL="${PROTOCOL:-bgp}"
MESSAGES="${MESSAGES:-0}"
TIMEOUT="${TIMEOUT:-60}"
LOG="${LOG:-$(mktemp /tmp/danos-frr-dynamic.XXXXXX.log)}"

case "$PROTOCOL" in
  bgp) ZAPI_PROTOCOL=2 ;;
  ospf) ZAPI_PROTOCOL=3 ;;
  *) echo "usage: $0 [--protocol bgp|ospf] [--messages N]"; exit 2 ;;
esac
while [ "$#" -gt 0 ]; do
  case "$1" in
    --protocol) PROTOCOL="$2"; shift 2 ;;
    --messages) MESSAGES="$2"; shift 2 ;;
  --timeout) TIMEOUT="$2"; shift 2 ;;
  --help|-h) echo "usage: $0 [--protocol bgp|ospf] [--messages N] [--timeout SEC]"; exit 0 ;;
    *) echo "unknown option: $1"; exit 2 ;;
  esac
done
case "$PROTOCOL" in bgp) ZAPI_PROTOCOL=2 ;; ospf) ZAPI_PROTOCOL=3 ;; esac

test -x "$BRIDGE" || { echo "[BLOCKED] bridge missing: $BRIDGE"; exit 2; }
test -S "$ZEBRA_SOCK" || { echo "[BLOCKED] FRR zebra socket missing: $ZEBRA_SOCK"; exit 2; }
test -S "$VPP_SOCK" || { echo "[BLOCKED] VPP API socket missing: $VPP_SOCK"; exit 2; }

echo "[INFO] protocol=$PROTOCOL zapi_protocol=$ZAPI_PROTOCOL"
echo "[INFO] waiting up to ${TIMEOUT}s for externally provisioned FRR route add/withdraw events"
set +e
timeout "$TIMEOUT" env DANOS_ZAPI_PROTOCOL="$ZAPI_PROTOCOL" DANOS_ZAPI_DEBUG=1 \
  "$BRIDGE" --zebra-sock "$ZEBRA_SOCK" --vpp-sock "$VPP_SOCK" \
  --messages "$MESSAGES" --reconnect 2>&1 | tee "$LOG"
rc=${PIPESTATUS[0]}
set -e
if [ "$rc" -ne 0 ]; then
  if [ "$rc" -eq 124 ]; then
    echo "[BLOCKED] no route event before timeout; log=$LOG"
    exit 2
  fi
  echo "[FAIL] bridge exited rc=$rc; log=$LOG"
  exit "$rc"
fi
grep -Eq 'zapi command=(9|10|31|32)' "$LOG" || {
  echo "[BLOCKED] no dynamic route ZAPI add/withdraw observed; log=$LOG"; exit 2;
}
grep -q 'programming sweep: withdrawn=' "$LOG" || {
  echo "[BLOCKED] no route withdrawal sweep observed; log=$LOG"; exit 2;
}
echo "[PASS] $PROTOCOL -> ZAPI -> DPA -> VPP dynamic route evidence"
