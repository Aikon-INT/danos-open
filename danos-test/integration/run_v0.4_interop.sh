#!/bin/bash
# DANOS-Open v0.4 External Interop Verification (I1-I5)
#
# Runs the standalone gNMI server and drives it with gnmic (openconfig),
# a fully independent grpc-go client — the "external proof" that the
# hand-written C protobuf/HPACK/HTTP2/gRPC stack speaks real gNMI.
#
# Prerequisites:
#   - built tree (cmake --build build)
#   - gnmic binary:  env GOPROXY=https://goproxy.cn,direct \
#       go install github.com/openconfig/gnmic@v0.35.0
#     (or GNMIC env var pointing at the binary)
#
# Usage: bash danos-test/integration/run_v0.4_interop.sh

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
GNMIC="${GNMIC:-$HOME/go/bin/gnmic}"
[ -x /tmp/gnmic-bin ] && GNMIC=/tmp/gnmic-bin

PORT="${PORT:-59244}"
SRV_LOG=$(mktemp)
OUT=$(mktemp)
SRV_PID=""

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; FAILED=1; }
FAILED=0

cleanup() { test -n "$SRV_PID" && kill "$SRV_PID" 2>/dev/null || true; rm -f "$SRV_LOG" "$OUT"; }
trap cleanup EXIT

echo "=== DANOS-Open v0.4 external interop verification (gnmic) ==="

if [ ! -x "$BUILD_DIR/danos-test/gnmi_demo_server" ]; then
    echo "server binary missing; build first"; exit 1
fi
if [ ! -x "$GNMIC" ]; then
    echo "gnmic not found at $GNMIC; install with:"
    echo "  GOPROXY=https://goproxy.cn,direct go install github.com/openconfig/gnmic@v0.35.0"
    exit 1
fi

"$BUILD_DIR/danos-test/gnmi_demo_server" "$PORT" > "$SRV_LOG" 2>&1 &
SRV_PID=$!
sleep 1
T="-a 127.0.0.1:$PORT --insecure"

# --- I1: capabilities -----------------------------------------------------
if "$GNMIC" $T capabilities 2>&1 | grep -q "DANOS-Open DPA"; then
    pass "I1: gnmic capabilities -> DPA version + models"
else
    fail "I1: gnmic capabilities"
fi

# --- I2: get --------------------------------------------------------------
if "$GNMIC" $T get --path /interfaces 2>&1 | grep -q '"name": "eth0"'; then
    pass "I2: gnmic get /interfaces -> seeded objects visible"
else
    fail "I2: gnmic get /interfaces"
fi

# --- I3: set + verify -----------------------------------------------------
R="$("$GNMIC" $T set --update /interfaces/interface[name=ethi3]:::json_ietf:::{"mtu":7777,"admin_up":1} 2>&1)"
if echo "$R" | grep -q '"operation": "UPDATE"' &&
   "$GNMIC" $T get --path "/interfaces/interface[name=ethi3]" 2>&1 | grep -q '"mtu": 7777'; then
    pass "I3: gnmic set -> UPDATE ack + get verifies value"
else
    fail "I3: gnmic set"
fi

# --- I4: subscribe ONCE ---------------------------------------------------
if timeout 8 "$GNMIC" $T subscribe --path /interfaces --mode once > "$OUT" 2>&1 &&
   grep -q '"name": "eth0"' "$OUT"; then
    pass "I4: gnmic subscribe ONCE -> updates + sync + clean exit"
else
    fail "I4: gnmic subscribe ONCE"
fi

# --- I5: subscribe STREAM push -------------------------------------------
timeout 12 "$GNMIC" $T subscribe --path /interfaces --mode stream > "$OUT" 2>&1 &
SUB=$!
sleep 2
"$GNMIC" $T set --update /interfaces/interface[name=ethi5]:::json_ietf:::{"mtu":5555,"admin_up":1} >/dev/null 2>&1
sleep 3
if kill -0 $SUB 2>/dev/null; then kill $SUB 2>/dev/null; fi
wait $SUB 2>/dev/null
if grep -q '"name": "ethi5"' "$OUT"; then
    pass "I5: gnmic subscribe STREAM -> Set pushed to subscriber"
else
    fail "I5: gnmic subscribe STREAM push"
fi

# --- I9: TLS front-end (socat OPENSSL terminates TLS, server stays h2c) ---
if command -v socat >/dev/null && command -v openssl >/dev/null; then
    TLSDIR=$(mktemp -d)
    TLSPORT=$((PORT + 1))
    openssl req -x509 -newkey rsa:2048 -keyout "$TLSDIR/key.pem"             -out "$TLSDIR/cert.pem" -days 30 -nodes             -subj "/CN=danos-gnmi" >/dev/null 2>&1
    socat OPENSSL-LISTEN:$TLSPORT,fork,reuseaddr,cert=$TLSDIR/cert.pem,key=$TLSDIR/key.pem,verify=0           TCP:127.0.0.1:$PORT 2>/dev/null &
    SOCAT_PID=$!
    sleep 1
    if "$GNMIC" -a 127.0.0.1:$TLSPORT --skip-verify capabilities 2>&1 | grep -q "DANOS-Open DPA"; then
        pass "I9: gnmic over TLS (socat front-end) -> h2c server"
    else
        fail "I9: gnmic over TLS"
    fi
    kill $SOCAT_PID 2>/dev/null
    rm -rf "$TLSDIR"
else
    echo "[SKIP] I9: socat/openssl not installed"
fi

# --- V-group: real VPP (opt-in; requires a reachable VPP) -----------------
if [ "${1:-}" = "--with-vpp" ]; then
    VPP_SOCK="${VPP_API_SOCK:-/run/vpp/api.sock}"
    if [ ! -S "$VPP_SOCK" ]; then
        echo -e "${RED}[SKIP]${NC} V1-V5: no VPP api socket at $VPP_SOCK"
        echo "       Start VPP first (container: docker run --privileged -v /run/vpp ...)"
        echo "       or set VPP_API_SOCK. See docs/interop/v0.4_interop_dod.md"
    else
        export VPP_API_SOCK="$VPP_SOCK"
        if "$BUILD_DIR/danos-test/vpp_live_test" > "$OUT" 2>&1; then
            pass "V1/V2/V4: handshake + control_ping + set_flags against real VPP"
            grep -E "^V" "$OUT" | sed 's/^/      /'
        else
            fail "V-group: vpp_live_test (see $OUT)"
            cat "$OUT"
        fi
    fi
fi

echo "=== result: $([ $FAILED -eq 0 ] && echo ALL PASS || echo FAILURES) ==="
exit $FAILED
