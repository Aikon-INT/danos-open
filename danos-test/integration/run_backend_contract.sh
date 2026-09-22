#!/bin/bash
# v0.16 backend contract gate for the in-process Linux/mock and VPP adapters.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

require_test() {
    local name="$1" binary="$2"
    test -x "$binary" || { echo "[BLOCKED] missing $name: $binary"; exit 2; }
}

require_test conformance "$BUILD_DIR/danos-test/conformance_test"
require_test programming_pipeline "$BUILD_DIR/danos-test/programming_pipeline_test"
require_test route_composite "$BUILD_DIR/danos-test/route_composite_test"

echo "[1/3] capability and transaction conformance"
"$BUILD_DIR/danos-test/conformance_test"
echo "[2/3] idempotency, failure accounting, tombstone sweep, replay"
"$BUILD_DIR/danos-test/programming_pipeline_test"
echo "[3/3] composite route/NH/NHGroup lifecycle"
"$BUILD_DIR/danos-test/route_composite_test"
echo "[PASS] v0.16 backend contract gate"
