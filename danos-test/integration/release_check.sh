#!/bin/bash
# DANOS-Open release acceptance — one command, every gate.
#
#   1. full unit/conformance suite (ctest)
#   2. external interop (gnmic I1-I9)
#   3. mgrd system smoke: boot -> gNMI -> /metrics -> crash -> recover
#
# Usage: bash danos-test/integration/release_check.sh

set -u

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
GNMIC="${GNMIC:-/tmp/gnmic-bin}"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; FAILED=1; }
FAILED=0

echo "=== DANOS-Open release acceptance ==="

# --- gate 1: full test suite ----------------------------------------------
(cd "$BUILD_DIR" && ctest -Q) && pass "gate1: ctest all green" \
    || fail "gate1: ctest"

# --- gate 2: external interop ---------------------------------------------
if bash "$PROJECT_ROOT/danos-test/integration/run_v0.4_interop.sh" > /tmp/rel-interop.log 2>&1; then
    pass "gate2: gnmic interop I1-I9"
else
    fail "gate2: gnmic interop (see /tmp/rel-interop.log)"; cat /tmp/rel-interop.log
fi

# --- gate 3: mgrd system smoke ---------------------------------------------
WAL=$(mktemp /tmp/rel-mgrd-XXXXXX.wal)
"$BUILD_DIR/danos-mgrd/danos-mgrd" --port 59360 --metrics-port 59361 \
    --wal "$WAL" --seed > /tmp/rel-mgrd.log 2>&1 &
MGRD=$!
sleep 2

if "$GNMIC" -a 127.0.0.1:59360 --insecure get --path /interfaces 2>&1 | grep -q eth0; then
    pass "gate3a: mgrd gNMI serving seeded config"
else
    fail "gate3a: mgrd gNMI"
fi

MVAL=$(curl -s http://127.0.0.1:59361/metrics | grep -c "danos_")
if [ "$MVAL" -gt 30 ]; then
    pass "gate3b: /metrics exposition ($MVAL metrics)"
else
    fail "gate3b: /metrics ($MVAL metrics)"
fi

# crash + restart: config must survive
kill -9 $MGRD 2>/dev/null; wait $MGRD 2>/dev/null
"$BUILD_DIR/danos-mgrd/danos-mgrd" --port 59362 --metrics-port 59363 \
    --wal "$WAL" > /tmp/rel-mgrd2.log 2>&1 &
MGRD=$!
sleep 2
if "$GNMIC" -a 127.0.0.1:59362 --insecure get --path /interfaces 2>&1 | grep -q eth0; then
    pass "gate3c: kill -9 + restart -> config recovered from WAL"
else
    fail "gate3c: crash recovery"
fi
grep -q "records recovered" /tmp/rel-mgrd2.log && \
    grep -o "[0-9]* records recovered" /tmp/rel-mgrd2.log | head -1 | sed 's/^/      /'

kill $MGRD 2>/dev/null; wait $MGRD 2>/dev/null
rm -f "$WAL"

echo "=== result: $([ $FAILED -eq 0 ] && echo ALL PASS || echo FAILURES) ==="
exit $FAILED
