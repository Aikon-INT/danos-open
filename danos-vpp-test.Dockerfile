# Debian trixie VPP validation image built from VPP source.
FROM debian:13
ENV DEBIAN_FRONTEND=noninteractive
ARG VPP_REF=master
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl wget git build-essential debhelper-compat dh-python python3-all python3-setuptools chrpath libtool libtool-bin libibverbs-dev python3 python3-pip python3-venv libnuma-dev libpcap-dev libssl-dev libelf-dev libmnl-dev libnl-3-dev libnl-route-3-dev libxdp-dev libbpf-dev iproute2 iputils-ping tcpdump nasm pkg-config autoconf automake bison flex clang llvm ninja-build cmake && rm -rf /var/lib/apt/lists/*
WORKDIR /opt
RUN git clone --depth 1 --branch ${VPP_REF} https://github.com/FDio/vpp.git
WORKDIR /opt/vpp
RUN make install-dep && make pkg-deb
RUN apt-get update && apt-get install -y --no-install-recommends ./build-root/*.deb && rm -rf /var/lib/apt/lists/*
RUN mkdir -p /etc/vpp /run/vpp /var/log/vpp
COPY <<'EOF' /etc/vpp/startup.conf
unix {
  nodaemon
  log /var/log/vpp/vpp.log
  cli-listen /run/vpp/cli.sock
  api-segment { prefix /vpp }
}
statseg { socket-name /run/vpp/stats.sock }
plugins { plugin dpdk_plugin.so { disable } }
EOF
CMD ["vpp", "-c", "/etc/vpp/startup.conf"]
