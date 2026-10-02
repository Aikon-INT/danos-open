#!/usr/bin/env python3
"""Unit tests for serial-to-I211 preflight evidence qualification."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from record_i211_boot_result import QualificationError, result_from_log


BDF0 = "0000:01:00.0"
BDF1 = "0000:02:00.0"
IDENTITY = {"iso_build_commit": "0123456789abcdef", "iso_source_dirty": "0"}


def serial_log() -> str:
    return f"""DANOS-BUILD commit=0123456789abcdef source_dirty=0 iso=runner.iso no_rx_interrupts=1 utc=2026-10-02T00:00:00Z
PCI-NIC {BDF0} vendor=0x8086 device=0x1539
PCI-NIC {BDF1} vendor=0x8086 device=0x1539
DPDK-PCI-BIND PASS bdf={BDF0} pci_id=8086:1539 driver=uio_pci_generic
DPDK-PCI-BIND PASS bdf={BDF1} pci_id=8086:1539 driver=uio_pci_generic
VPP API socket: ready
VPP stats socket: ready
VPP-DPDK-INTERFACES PASS
"""


class RecordI211BootResultTest(unittest.TestCase):
    def qualify(self, text: str):
        return result_from_log(
            text, identity=IDENTITY, iso_name="runner.iso", iso_sha256="a" * 64,
            bdfs=[BDF0, BDF1], driver="uio_pci_generic", runner_commit="fedcba",
        )

    def test_complete_clean_two_port_boot_is_preflight_only(self):
        result = self.qualify(serial_log())
        self.assertEqual(result["preflight_status"], "PASS")
        self.assertEqual(result["performance_status"], "ENVIRONMENT-OPEN")
        self.assertEqual(result["target_bdfs"], f"{BDF0},{BDF1}")
        self.assertEqual(result["iso_sha256"], "a" * 64)
        self.assertEqual(result["pps"], "")

    def test_qemu_or_missing_hardware_is_skip_not_pass(self):
        text = serial_log().replace(
            f"DPDK-PCI-BIND PASS bdf={BDF1} pci_id=8086:1539 driver=uio_pci_generic",
            f"DPDK-PCI-BIND FAIL device-absent={BDF1}",
        )
        result = self.qualify(text)
        self.assertEqual(result["preflight_status"], "SKIP")
        self.assertEqual(result["status"], "SKIP")

    def test_iso_identity_mismatch_is_rejected(self):
        with self.assertRaisesRegex(QualificationError, "identity"):
            self.qualify(serial_log().replace("0123456789abcdef", "fedcba9876543210"))

    def test_wrong_pci_or_driver_is_rejected(self):
        with self.assertRaisesRegex(QualificationError, "I211"):
            self.qualify(serial_log().replace("pci_id=8086:1539", "pci_id=10ec:8168", 1))

    def test_missing_vpp_interface_pass_is_rejected(self):
        with self.assertRaisesRegex(QualificationError, "INTERFACES PASS"):
            self.qualify(serial_log().replace("VPP-DPDK-INTERFACES PASS\n", ""))


if __name__ == "__main__":
    unittest.main(verbosity=2)
