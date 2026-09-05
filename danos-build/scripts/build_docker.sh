#!/bin/bash
# DANOS-Open Docker Image Build Script (G4)
# Usage: ./build_docker.sh [tag]

set -e

TAG="${1:-danos-open:latest}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== Building DANOS-Open Docker image: $TAG ==="
echo "  context: $PROJECT_DIR"

cd "$PROJECT_DIR"

# Build image
docker build -t "$TAG" .

# Verify: run tests inside container
echo ""
echo "=== Running tests inside container ==="
docker run --rm "$TAG" -c '
    echo "--- Conformance ---"
    /usr/bin/danos-conformance_test 2>&1 | tail -3
    echo "--- Core ---"
    /usr/bin/danos-core-test 2>&1 | tail -3
    echo "--- DPA Errors ---"
    /usr/bin/danos-dpa-test-errors 2>&1 | tail -3
    echo "--- DPA Version ---"
    /usr/bin/danos-dpa-test-version 2>&1 | tail -3
    echo "--- DPA Capability ---"
    /usr/bin/danos-dpa-test-capability 2>&1 | tail -3
    echo "--- FIB Parse ---"
    /usr/bin/danos-fib-test-parse 2>&1 | tail -3
    echo "--- FIB Mapper ---"
    /usr/bin/danos-fib-test-mapper 2>&1 | tail -3
    echo "--- VPP Mapper ---"
    /usr/bin/danos-vpp-test 2>&1 | tail -3
    echo "--- CLI ---"
    /usr/bin/danos-cli-test 2>&1 | tail -3
    echo "--- gNMI ---"
    /usr/bin/danos-gnmi-test 2>&1 | tail -3
    echo "=== All tests complete ==="
'

echo ""
echo "=== Image built: $TAG ==="
docker images "$TAG"
