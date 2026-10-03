#!/usr/bin/env python3
"""Tests for strict I211 live VPP functional evidence parsing."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from record_i211_traffic_result import QualificationError, result_from_log


BDF0 = "0000:01:00.0"
BDF1 = "0000:02:00.0"
IDENTITY = {
    "iso_build_commit": "0123456789abcdef", "iso_source_dirty": "0",
    "danos_build_dpdk_traffic_test": "1", "danos_build_ecmp_soak_count": "1000",
    "danos_build_static_neighbors": "0",
}


def serial_log() -> str:
    lines = [
        "DANOS-INIT-ENTER",
        f"PCI-NIC {BDF0} vendor=0x8086 device=0x1539",
        f"PCI-NIC {BDF1} vendor=0x8086 device=0x1539",
        "DANOS-BUILD commit=0123456789abcdef source_dirty=0 iso=traffic.iso no_rx_interrupts=1 utc=2026-10-04T00:00:00Z",
        f"DPDK-PCI-BIND PASS bdf={BDF0} pci_id=8086:1539 driver=uio_pci_generic",
        f"DPDK-PCI-BIND PASS bdf={BDF1} pci_id=8086:1539 driver=uio_pci_generic",
        "VPP API socket: ready", "VPP stats socket: ready", "VPP-DPDK-INTERFACES PASS",
        "VPP-DPDK-PING-0 PASS", "VPP-DPDK-PING-1 PASS",
        "VPP-ECMP-FIB PASS buckets=2", "VPP-ECMP-SOURCE PASS address=30.30.30.1 interface=loop0",
    ]
    lines.extend(f"VPP-ECMP-FLOW target={dst} tx=3 rx=3 loss=0" for dst in (
        "30.30.30.2", "30.30.30.3", "30.30.30.4", "30.30.30.5"))
    lines.extend([
        "VPP-ECMP-MULTI-FLOW PASS",
        "VPP-DPDK-NEIGHBORS dynamic-arp",
        "VPP-ECMP-SOAK BEGIN packets_per_flow=1000 interval=0.01",
        "VPP-ECMP-SOAK PASS flows=4 packets_per_flow=1000 total_tx=4000 total_rx=4000 loss=0 elapsed_ms=48000 pps=83.33 bucket0_packets=3000 bucket1_packets=1000",
        "VPP-ECMP-NH-WITHDRAW PASS nexthop=10.20.0.2 interface=GigabitEthernet0/3/0",
        "VPP-ECMP-PATH-FAILOVER PASS interface=GigabitEthernet0/3/0 probes=20",
        "VPP-ECMP-NH-RESTORE PASS nexthop=10.20.0.2 interface=GigabitEthernet0/3/0",
        "VPP-ECMP-PATH-RESTORE PASS interface=GigabitEthernet0/3/0 buckets=2 probes=20",
    ])
    return "\n".join(lines) + "\n"


class RecordI211TrafficResultTest(unittest.TestCase):
    def qualify(self, text: str, identity=None):
        return result_from_log(
            text, identity=identity or IDENTITY, iso_name="traffic.iso",
            iso_sha256="a" * 64, runner_commit="fedcba",
        )

    def test_complete_physical_traffic_and_recovery_evidence_passes(self):
        result = self.qualify(serial_log())
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["packets_tx"], "4000")
        self.assertEqual(result["ecmp_bucket_0"], "3000")
        self.assertEqual(result["ecmp_bucket_1"], "1000")
        self.assertEqual(result["performance_status"], "ENVIRONMENT-OPEN")

    def test_incomplete_path_restore_is_rejected(self):
        with self.assertRaisesRegex(QualificationError, "PATH-RESTORE"):
            self.qualify(serial_log().replace(
                "VPP-ECMP-PATH-RESTORE PASS", "VPP-ECMP-PATH-RESTORE FAIL"))

    def test_zero_bucket_counter_is_rejected(self):
        with self.assertRaisesRegex(QualificationError, "both VPP ECMP bucket"):
            self.qualify(serial_log().replace("bucket1_packets=1000", "bucket1_packets=0"))

    def test_non_traffic_image_cannot_qualify(self):
        identity = dict(IDENTITY, danos_build_dpdk_traffic_test="0")
        with self.assertRaisesRegex(QualificationError, "does not enable"):
            self.qualify(serial_log(), identity)

    def test_inconsistent_pps_is_rejected(self):
        with self.assertRaisesRegex(QualificationError, "inconsistent"):
            self.qualify(serial_log().replace("pps=83.33", "pps=800"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
