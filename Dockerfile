# DANOS-Open Build & Runtime Image (G4)
#
# Multi-stage build:
#   stage 1: build danos-open from source
#   stage 2: runtime image with FRR + VPP + danos-open

###############################################################################
# Stage 1: Build
###############################################################################
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    gcc-13 \
    g++-13 \
    libc6-dev \
    python3 \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build/src

# Copy source
COPY . /build/src/

# Build
RUN cmake -B /build/out -DCMAKE_BUILD_TYPE=Release -DDANOS_BUILD_TESTS=ON \
    && cmake --build /build/out -j$(nproc) \
    && ctest --test-dir /build/out --output-on-failure

###############################################################################
# Stage 2: Runtime
###############################################################################
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libc6 \
    libgcc-s1 \
    # FRR (from PPA in production)
    iproute2 \
    iputils-ping \
    python3 \
    && rm -rf /var/lib/apt/lists/*

# Copy built libraries and binaries
COPY --from=builder /build/out/danos-dpa/libdanos-dpa.a /usr/lib/danos/
COPY --from=builder /build/out/danos-core/libdanos-core.a /usr/lib/danos/
COPY --from=builder /build/out/danos-fib/libdanos-fib.a /usr/lib/danos/
COPY --from=builder /build/out/danos-vpp/libdanos-vpp.a /usr/lib/danos/
COPY --from=builder /build/out/danos-mgmt/libdanos-mgmt.a /usr/lib/danos/

# Copy test binaries
COPY --from=builder /build/out/danos-test/conformance_test /usr/bin/danos-conformance-test
COPY --from=builder /build/out/danos-core/tests/core_test /usr/bin/danos-core-test
COPY --from=builder /build/out/danos-dpa/tests/dpa_test_errors /usr/bin/danos-dpa-test-errors
COPY --from=builder /build/out/danos-dpa/tests/dpa_test_version /usr/bin/danos-dpa-test-version
COPY --from=builder /build/out/danos-dpa/tests/dpa_test_capability /usr/bin/danos-dpa-test-capability
COPY --from=builder /build/out/danos-fib/tests/fib_test_parse /usr/bin/danos-fib-test-parse
COPY --from=builder /build/out/danos-fib/tests/fib_test_mapper /usr/bin/danos-fib-test-mapper
COPY --from=builder /build/out/danos-vpp/tests/vpp_mapper_test /usr/bin/danos-vpp-test
COPY --from=builder /build/out/danos-mgmt/tests/cli_test /usr/bin/danos-cli-test
COPY --from=builder /build/out/danos-mgmt/tests/gnmi_test /usr/bin/danos-gnmi-test

# Copy YANG models
COPY danos-models/ /etc/danos/models/

# Copy configuration
COPY danos-build/configs/ /etc/danos/

# Health check: run conformance test
HEALTHCHECK --interval=30s --timeout=10s --retries=3 \
    CMD /usr/bin/danos-conformance-test || exit 1

# Default entrypoint: run all tests
ENTRYPOINT ["/bin/bash", "-c"]
CMD ["echo 'DANOS-Open v0.1' && echo 'Run tests with: danos-conformance-test' && /bin/bash"]
