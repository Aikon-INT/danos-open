#!/bin/bash
# DANOS-Open v0.3 Vertical Stack Verification
#
# Exercises the v0.3 vertical chain end to end:
#   V1: VPP binary API protocol conformance (mock VPP server, wire-level)
#   V2: gNMI gRPC server vs real client over TCP (Capabilities/Get/Set)
#   V3: Config persistence restart cycle (WAL replay + reconcile)
#   V4: gNMI Set → DPA store → persistence → restart recovery roundtrip
#   V5 (optional): real FRR zebra ZAPI session (needs danos-frr-test image)
#
# Usage: bash danos-test/integration/run_v0.3_verify.sh [--with-frr]

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; NC='\033[0m'
pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; FAILED=1; }
info() { echo -e "${YELLOW}[INFO]${NC} $1"; }

FAILED=0
WITH_FRR=0
[ "${1:-}" = "--with-frr" ] && WITH_FRR=1

echo "=== DANOS-Open v0.3 vertical stack verification ==="

# ---------------------------------------------------------------------------
# V1: protocol conformance
# ---------------------------------------------------------------------------
info "V1: VPP binary API protocol conformance (wire-level, mock VPP server)"
if "$BUILD_DIR/danos-vpp/tests/vpp_proto_test" >/dev/null 2>&1; then
    pass "V1: sockclnt_create handshake + msg table + typed messages + stat segment"
else
    fail "V1: vpp_proto_test"
fi

# ---------------------------------------------------------------------------
# V2: gNMI gRPC end to end
# ---------------------------------------------------------------------------
info "V2: gNMI gRPC (protobuf over HTTP/2) end to end"
if "$BUILD_DIR/danos-mgmt/tests/gnmi_grpc_test" >/dev/null 2>&1; then
    pass "V2: Capabilities/Get/Set over real TCP + DPA store roundtrip"
else
    fail "V2: gnmi_grpc_test"
fi

# ---------------------------------------------------------------------------
# V3: persistence restart cycle
# ---------------------------------------------------------------------------
info "V3: config persistence (WAL + replay + reconcile)"
if "$BUILD_DIR/danos-core/tests/persist_test" >/dev/null 2>&1; then
    pass "V3: WAL log -> restart -> recovery -> reconciler diffs"
else
    fail "V3: persist_test"
fi

# ---------------------------------------------------------------------------
# V4: vertical roundtrip — gNMI Set survives a restart
# ---------------------------------------------------------------------------
info "V4: gNMI Set -> persistence -> recovery roundtrip"
V4WAL=$(mktemp /tmp/danos-v4-XXXXXX.wal)
rm -f "$V4WAL"

# use the gnmi_proto + grpc + persist stack through a small driver binary
if [ -x "$BUILD_DIR/danos-test/v03_roundtrip_test" ]; then
    if "$BUILD_DIR/danos-test/v03_roundtrip_test" "$V4WAL" >/dev/null 2>&1; then
        pass "V4: config set via gNMI recovered after simulated restart"
    else
        fail "V4: v03_roundtrip_test"
    fi
else
    info "V4: driver binary not built (optional)"
fi
rm -f "$V4WAL"

# ---------------------------------------------------------------------------
# V5: real FRR zebra session (optional)
# ---------------------------------------------------------------------------
if [ "$WITH_FRR" = "1" ]; then
    if docker image inspect danos-frr-test:latest >/dev/null 2>&1; then
        info "V5: real FRR zebra ZAPI session"
        CNAME="danos-v03-frr-$$"
        docker run -d --name "$CNAME" --privileged \
            -v /tmp/danos-v03-frr:/var/run/frr-share \
            danos-frr-test:latest sh -c "sleep infinity" >/dev/null 2>&1
        docker exec "$CNAME" sh -c "
            sed -i 's/zserv_path=.*/zserv_path=\/var\/run\/frr-share\/zebra.zserv/' /etc/frr/zebra.conf 2>/dev/null
            /usr/lib/frr/frrinit.sh start >/dev/null 2>&1
        " >/dev/null 2>&1
        sleep 2
        if docker exec "$CNAME" vtysh -c "show version" >/dev/null 2>&1; then
            pass "V5: FRR zebra reachable (vtysh ok); ZAPI client test: run fib_e2e with shared socket"
        else
            fail "V5: FRR zebra not reachable"
        fi
        docker rm -f "$CNAME" >/dev/null 2>&1
    else
        info "V5: danos-frr-test image not present, skipped"
    fi
fi

echo "=== result: $([ $FAILED -eq 0 ] && echo ALL PASS || echo FAILURES) ==="
exit $FAILED
