#!/usr/bin/env python3
"""Exercise traffic-mode UART capture and result recording through a PTY."""

import argparse
import os
import pty
import subprocess
import tempfile
import time
from pathlib import Path

from read_live_iso_identity import read_identity


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    args = parser.parse_args()
    iso = args.iso.resolve()
    identity = read_identity(iso)
    commit = identity["iso_build_commit"]
    boot = f"""DANOS-INIT-ENTER
PCI-NIC 0000:01:00.0 vendor=0x8086 device=0x1539
PCI-NIC 0000:02:00.0 vendor=0x8086 device=0x1539
DANOS-BUILD commit={commit} source_dirty=0 iso={iso.name} no_rx_interrupts=1 utc=2026-10-04T00:00:00Z
DPDK-PCI-BIND PASS bdf=0000:01:00.0 pci_id=8086:1539 driver=uio_pci_generic
DPDK-PCI-BIND PASS bdf=0000:02:00.0 pci_id=8086:1539 driver=uio_pci_generic
VPP API socket: ready
VPP stats socket: ready
VPP-DPDK-INTERFACES PASS
VPP-DPDK-NEIGHBORS dynamic-arp
VPP-DPDK-PING-0 PASS
VPP-DPDK-PING-1 PASS
VPP-ECMP-FIB PASS buckets=2
VPP-ECMP-SOURCE PASS address=30.30.30.1 interface=loop0
VPP-ECMP-FLOW target=30.30.30.2 tx=3 rx=3 loss=0
VPP-ECMP-FLOW target=30.30.30.3 tx=3 rx=3 loss=0
VPP-ECMP-FLOW target=30.30.30.4 tx=3 rx=3 loss=0
VPP-ECMP-FLOW target=30.30.30.5 tx=3 rx=3 loss=0
VPP-ECMP-MULTI-FLOW PASS
VPP-ECMP-SOAK BEGIN packets_per_flow=1000 interval=0.01
VPP-ECMP-SOAK PASS flows=4 packets_per_flow=1000 total_tx=4000 total_rx=4000 loss=0 elapsed_ms=48000 pps=83.33 bucket0_packets=3000 bucket1_packets=1000
VPP-ECMP-NH-WITHDRAW PASS nexthop=10.20.0.2 interface=GigabitEthernet0/3/0
VPP-ECMP-PATH-FAILOVER PASS interface=GigabitEthernet0/3/0 probes=20
VPP-ECMP-NH-RESTORE PASS nexthop=10.20.0.2 interface=GigabitEthernet0/3/0
VPP-ECMP-PATH-RESTORE PASS interface=GigabitEthernet0/3/0 buckets=2 probes=20
"""

    with tempfile.TemporaryDirectory(prefix="i211-traffic-capture-test-") as temp:
        tempdir = Path(temp)
        logfile = tempdir / "captured.serial.log"
        resultfile = Path(str(logfile) + ".traffic.env")
        master, slave = pty.openpty()
        root = Path(__file__).resolve().parents[2]
        process = subprocess.Popen(
            ["bash", str(root / "danos-test/integration/capture_i211_serial.sh"),
             os.ttyname(slave), str(iso), str(logfile), "2", "--traffic"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        try:
            time.sleep(0.35)
            os.write(master, boot.encode())
            stdout, stderr = process.communicate(timeout=25)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)
            os.close(slave)
        if process.returncode != 0:
            raise RuntimeError(f"traffic capture failed ({process.returncode})\n{stdout}\n{stderr}")
        captured = logfile.read_text(errors="replace")
        result = resultfile.read_text()
        if "VPP-ECMP-SOAK PASS" not in captured or f"status=PASS" not in result:
            raise RuntimeError("PTY capture did not persist/qualify physical traffic markers")
        if "performance_status=ENVIRONMENT-OPEN" not in result:
            raise RuntimeError("functional ICMP qualification was incorrectly labeled as line-rate performance")
        if f"iso_sha256=" not in result or f"iso_build_commit={commit}" not in result:
            raise RuntimeError("traffic result does not bind back to the exact ISO identity")
    print("PASS: traffic UART PTY capture produced ISO-bound functional PASS and performance OPEN")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
