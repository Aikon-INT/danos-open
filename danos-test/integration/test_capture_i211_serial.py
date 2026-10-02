#!/usr/bin/env python3
"""Exercise the serial capture wrapper with a temporary PTY, never real hardware."""

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
    with tempfile.TemporaryDirectory(prefix="i211-serial-capture-test-") as temp:
        tempdir = Path(temp)
        logfile = tempdir / "captured.serial.log"
        resultfile = Path(str(logfile) + ".preflight.env")
        master, slave = pty.openpty()
        slave_name = os.ttyname(slave)
        root = Path(__file__).resolve().parents[2]
        command = [
            "bash", str(root / "danos-test/integration/capture_i211_serial.sh"),
            slave_name, str(iso), str(logfile), "2",
        ]
        process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        try:
            time.sleep(0.35)
            commit = identity["iso_build_commit"]
            boot = f"""DANOS-INIT-ENTER
PCI-NIC 0000:01:00.0 vendor=0x8086 device=0x1539
PCI-NIC 0000:02:00.0 vendor=0x8086 device=0x1539
DANOS-BUILD commit={commit} source_dirty=0 iso={iso.name} no_rx_interrupts=1 utc=2026-10-02T00:00:00Z
DPDK-PCI-BIND PASS bdf=0000:01:00.0 pci_id=8086:1539 driver=uio_pci_generic
DPDK-PCI-BIND PASS bdf=0000:02:00.0 pci_id=8086:1539 driver=uio_pci_generic
VPP API socket: ready
VPP stats socket: ready
VPP-DPDK-INTERFACES PASS
"""
            os.write(master, boot.encode())
            stdout, stderr = process.communicate(timeout=25)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)
            os.close(slave)

        if process.returncode != 2:
            raise RuntimeError(
                f"capture wrapper returned {process.returncode}, expected OPEN(2)\n"
                f"stdout:\n{stdout}\nstderr:\n{stderr}"
            )
        captured = logfile.read_text(errors="replace")
        result = resultfile.read_text()
        if "DANOS-INIT-ENTER" not in captured or "DPDK-PCI-BIND PASS" not in captured:
            raise RuntimeError("serial PTY bytes were not persisted to the capture log")
        if "preflight_status=PASS" not in result or "performance_status=ENVIRONMENT-OPEN" not in result:
            raise RuntimeError("captured boot did not produce preflight PASS/performance OPEN")
        if f"iso_sha256=" not in result or f"commit={commit}" not in result:
            raise RuntimeError("captured result is missing the ISO-bound identity")
        print("PASS: PTY capture saved serial evidence and emitted identity-bound preflight OPEN")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
