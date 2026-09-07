FROM danos-build:trixie
RUN apt-get update 2>/dev/null; apt-get install -y --no-install-recommends frr frr-pythontools iproute2 iputils-ping tcpdump python3 2>&1 | tail -3
RUN mkdir -p /var/run/frr /etc/frr && chown frr:frr /var/run/frr
ENV FRR_ENABLED_DAEMONS="zebra bgpd ospfd isisd bfdd staticd"
CMD ["/usr/lib/frr/frrinit.sh", "start", "&&", "sleep", "infinity"]
