# DANOS-Open VPP Test Image
# Provides VPP 24.x + DPDK 23.x for conformance/perf testing
#
# Build:
#   docker build -t danos-vpp-test:latest -f danos-vpp-test.Dockerfile .
#
# Run (privileged for DPDK hugepages):
#   docker run --rm --privileged -v /dev/hugepages:/dev/hugepages danos-vpp-test:latest

FROM debian:trixie

# Install VPP + DPDK from Debian repos (trixie has VPP 24.x packages)
RUN apt-get update && apt-get install -y --no-install-recommends \
        vpp \
        vpp-plugin-core \
        vpp-plugin-dpdk \
        vpp-plugin-linux-cp \
        vpp-dev \
        libvppinfra \
        dpdk \
        dpdk-dev \
        iproute2 \
        iputils-ping \
        tcpdump \
        python3 \
        hugepages \
    && rm -rf /var/lib/apt/lists/*

# VPP startup config
RUN mkdir -p /etc/vpp /run/vpp
COPY <<'EOF' /etc/vpp/startup.conf
unix {
  nodaemon
  log /var/log/vpp/vpp.log
  cli-listen /run/vpp/cli.sock
  api-socket /run/vpp/api.sock
}
statseg {
  socket /run/vpp/stats.sock
}
plugins {
  plugin dpdk_plugin.so { enable }
}
EOF

# DPDK hugepage setup
RUN mkdir -p /var/log/vpp

ENV VPP_STARTUP_ARGS="--conf /etc/vpp/startup.conf"

CMD ["vpp", "--conf", "/etc/vpp/startup.conf"]
