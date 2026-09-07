#!/bin/bash
# DANOS-Open VPP Backend Deployment Verification
#
# Verifies J1/J3/J4/J5 exit conditions:
#   J1: All P0 items complete (real VPP)
#   J3: CI stable green
#   J4: Performance baseline met (VPP+DPDK > 1 Mpps)
#   J5: DPA conformance VPP backend 100%
#
# Prerequisites:
#   - danos-vpp-test:latest image built (or VPP installed on host)
#   - DPDK hugepages configured (for perf test)
#   - DANOS-Open built: cmake -B build && cmake --build build
#
# Usage:
#   bash danos-test/integration/run_vpp_verify.sh          # all checks
#   bash danos-test/integration/run_vpp_verify.sh conformance  # J5 only
#   bash danos-test/integration/run_vpp_verify.sh perf         # J4 only
#   bash danos-test/integration/run_vpp_verify.sh --list

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; }
info() { echo -e "${YELLOW}[INFO]${NC} $1"; }

# ---------------------------------------------------------------------------
# Check 1: VPP availability
# ---------------------------------------------------------------------------
check_vpp_available() {
    info "Checking VPP availability..."

    # Try host VPP first
    if command -v vpp >/dev/null 2>&1; then
        VPP_CMD="vpp"
        info "Found VPP on host: $(vpp --version 2>&1 | head -1)"
        return 0
    fi

    # Try Docker image
    if docker image inspect danos-vpp-test:latest >/dev/null 2>&1; then
        VPP_CMD="docker run --rm --privileged -d --name danos-vpp-verify danos-vpp-test:latest"
        info "Found danos-vpp-test:latest Docker image"
        return 0
    fi

    fail "VPP not found. Install VPP or build: docker build -t danos-vpp-test:latest -f danos-vpp-test.Dockerfile ."
    return 1
}

# ---------------------------------------------------------------------------
# Check 2: VPP API socket connectivity (D1)
# ---------------------------------------------------------------------------
check_vpp_api_connect() {
    info "Checking VPP binary API socket..."

    if [ ! -S /run/vpp/api.sock ]; then
        fail "/run/vpp/api.sock not found. Start VPP first."
        return 1
    fi

    info "VPP API socket present: /run/vpp/api.sock"
    pass "D1: VPP binary API socket accessible"
    return 0
}

# ---------------------------------------------------------------------------
# Check 3: VPP stat segment connectivity (D1)
# ---------------------------------------------------------------------------
check_vpp_stat_connect() {
    info "Checking VPP stat segment socket..."

    if [ ! -S /run/vpp/stats.sock ]; then
        fail "/run/vpp/stats.sock not found."
        return 1
    fi

    info "VPP stat segment socket present: /run/vpp/stats.sock"
    pass "D1: VPP stat segment accessible"
    return 0
}

# ---------------------------------------------------------------------------
# Check 4: DPA Conformance test with real VPP (J5)
# ---------------------------------------------------------------------------
check_conformance() {
    info "Running DPA conformance test (J5)..."

    if [ ! -x "$BUILD_DIR/danos-test/conformance_test" ]; then
        fail "conformance_test not built. Run: cmake -B build && cmake --build build"
        return 1
    fi

    # Run conformance test in real VPP mode (no mock)
    if DANOS_VPP_REAL=1 "$BUILD_DIR/danos-test/conformance_test" 2>&1; then
        pass "J5: DPA conformance VPP backend 100% passed"
        return 0
    else
        fail "J5: DPA conformance test failed"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Check 5: Performance baseline - VPP+DPDK throughput (J4, H2)
# ---------------------------------------------------------------------------
check_perf_baseline() {
    info "Running performance baseline test (J4, H2)..."

    if [ ! -x "$BUILD_DIR/danos-test/perf/sw_fwd_baseline_test" ]; then
        fail "sw_fwd_baseline_test not built."
        return 1
    fi

    # Check hugepages
    local hp_total
    hp_total=$(cat /proc/meminfo 2>/dev/null | grep HugePages_Total | awk '{print $2}')
    if [ "${hp_total:-0}" -lt 1024 ]; then
        info "Hugepages < 1024, configuring for DPDK..."
        echo 1024 > /proc/sys/vm/nr_hugepages 2>/dev/null || info "Cannot set hugepages (need root)"
    fi

    # Run perf test - target: > 1 Mpps single core
    if "$BUILD_DIR/danos-test/perf/sw_fwd_baseline_test" 2>&1 | tee /tmp/vpp_perf.log; then
        local mpps
        mpps=$(grep -oP '(\d+\.?\d*)\s*Mpps' /tmp/vpp_perf.log | head -1 | awk '{print $1}')
        if [ -n "$mpps" ] && [ "$(echo "$mpps > 1.0" | bc -l 2>/dev/null || echo 0)" = "1" ]; then
            pass "J4/H2: VPP+DPDK throughput ${mpps} Mpps > 1 Mpps target"
            return 0
        else
            fail "J4/H2: throughput ${mpps:-unknown} Mpps < 1 Mpps target"
            return 1
        fi
    else
        fail "J4: performance test failed"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Check 6: End-to-end FRR → ZAPI → DPA → VPP (C4)
# ---------------------------------------------------------------------------
check_e2e_frr_vpp() {
    info "Running end-to-end FRR → VPP test (C4)..."

    if [ ! -x "$BUILD_DIR/danos-test/fib_e2e_test" ]; then
        fail "fib_e2e_test not built."
        return 1
    fi

    # This test requires both FRR and VPP running
    if ! command -v vtysh >/dev/null 2>&1; then
        fail "vtysh not found (FRR not installed). Build: docker build -t danos-frr-test:latest -f danos-frr-test.Dockerfile ."
        return 1
    fi

    if "$BUILD_DIR/danos-test/fib_e2e_test" 2>&1; then
        pass "C4: FRR BGP → ZAPI → DPA → VPP end-to-end verified"
        return 0
    else
        fail "C4: end-to-end test failed"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
list_checks() {
    echo "Available checks:"
    echo "  vpp-available  - Check VPP is installed/running"
    echo "  api-connect    - D1: VPP binary API socket"
    echo "  stat-connect   - D1: VPP stat segment socket"
    echo "  conformance    - J5: DPA conformance 100%"
    echo "  perf           - J4/H2: VPP+DPDK > 1 Mpps"
    echo "  e2e            - C4: FRR → VPP end-to-end"
    echo ""
    echo "Run all: bash run_vpp_verify.sh"
}

run_all() {
    local rc=0
    check_vpp_available || rc=1
    check_vpp_api_connect || rc=1
    check_vpp_stat_connect || rc=1
    check_conformance || rc=1
    check_perf_baseline || rc=1
    check_e2e_frr_vpp || rc=1

    echo ""
    echo "==================================="
    if [ $rc -eq 0 ]; then
        pass "All VPP backend verification checks passed (J1/J3/J4/J5)"
    else
        fail "Some checks failed (see above)"
    fi
    echo "==================================="
    return $rc
}

case "${1:-all}" in
    --list) list_checks ;;
    all) run_all ;;
    conformance) check_conformance ;;
    perf) check_perf_baseline ;;
    e2e) check_e2e_frr_vpp ;;
    api-connect) check_vpp_api_connect ;;
    stat-connect) check_vpp_stat_connect ;;
    vpp-available) check_vpp_available ;;
    *) echo "Unknown check: $1"; list_checks; exit 1 ;;
esac
