#!/usr/bin/env python3
"""Regression tests for identity-bound QEMU ECMP result recording."""

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from record_qemu_ecmp_soak_result import ResultError, validate  # noqa: E402


class RecordQemuEcmpSoakResultTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.topology = self.root / "topology"
        self.topology.mkdir()
        self.iso = self.root / "danos.iso"
        self.iso.write_bytes(b"clean test ISO fixture")
        self.digest = hashlib.sha256(self.iso.read_bytes()).hexdigest()
        (self.topology / "run-manifest.env").write_text(
            f"git_commit={self.commit}\n"
            f"danos_iso={self.iso}\n"
            f"danos_iso_sha256={self.digest}\n"
            "qemu_dataplane_model=e1000\n"
        )
        (self.topology / "danos.serial.log").write_text(self.serial())

    commit = "a" * 40

    def serial(self, dirty="0", bucket1=1000, restart=True):
        flows = "".join(
            f"VPP-ECMP-SOAK-FLOW target=30.30.30.{i} tx=1000 rx=1000 loss=0\n"
            for i in range(2, 6)
        )
        return (
            f"DANOS-BUILD commit={self.commit} source_dirty={dirty} "
            f"iso={self.iso.name} no_rx_interrupts=1\n"
            "VPP-ECMP-SOAK BEGIN packets_per_flow=1000 interval=0.01\n"
            f"{flows}"
            "VPP-ECMP-SOAK PASS flows=4 packets_per_flow=1000 total_tx=4000 "
            f"total_rx=4000 loss=0 elapsed_ms=48730 pps=82.08 "
            f"bucket0_packets=3000 bucket1_packets={bucket1}\n"
            + ("VPP-RESTART-TEST PASS\n" if restart else "")
        )

    def test_valid_qemu_soak_is_functional_not_pci_performance(self):
        result = validate(self.topology, 1000)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["performance_status"], "ENVIRONMENT-OPEN")
        self.assertEqual(result["lane"], "qemu-e1000")
        self.assertEqual(result["packets_tx"], 4000)
        self.assertEqual(result["ecmp_bucket_0"], 3000)
        self.assertEqual(result["ecmp_bucket_1"], 1000)
        self.assertEqual(result["mbps"], "")
        self.assertEqual(result["cpu_pct"], "")

    def test_dirty_iso_is_rejected(self):
        (self.topology / "danos.serial.log").write_text(self.serial(dirty="1"))
        with self.assertRaisesRegex(ResultError, "dirty"):
            validate(self.topology, 1000)

    def test_manifest_digest_is_verified(self):
        manifest = self.topology / "run-manifest.env"
        manifest.write_text(manifest.read_text().replace(self.digest, "0" * 64))
        with self.assertRaisesRegex(ResultError, "digest"):
            validate(self.topology, 1000)

    def test_vmnetx3_topology_is_not_labeled_as_e1000(self):
        manifest = self.topology / "run-manifest.env"
        manifest.write_text(manifest.read_text().replace("qemu_dataplane_model=e1000", "qemu_dataplane_model=vmxnet3"))
        with self.assertRaisesRegex(ResultError, "requires the e1000"):
            validate(self.topology, 1000)

    def test_each_expected_flow_is_required(self):
        log = self.topology / "danos.serial.log"
        log.write_text(log.read_text().replace(
            "VPP-ECMP-SOAK-FLOW target=30.30.30.5 tx=1000 rx=1000 loss=0\n", ""
        ))
        with self.assertRaisesRegex(ResultError, "30.30.30.5"):
            validate(self.topology, 1000)

    def test_both_bucket_deltas_must_be_positive(self):
        (self.topology / "danos.serial.log").write_text(self.serial(bucket1=0))
        with self.assertRaisesRegex(ResultError, "bucket deltas"):
            validate(self.topology, 1000)

    def test_restart_replay_marker_is_required(self):
        (self.topology / "danos.serial.log").write_text(self.serial(restart=False))
        with self.assertRaisesRegex(ResultError, "restart/replay"):
            validate(self.topology, 1000)

    def test_old_restart_pass_before_soak_is_not_accepted(self):
        log = self.topology / "danos.serial.log"
        log.write_text("VPP-RESTART-TEST PASS\n" + self.serial(restart=False))
        with self.assertRaisesRegex(ResultError, "post-soak VPP restart/replay"):
            validate(self.topology, 1000)

    def test_old_soak_is_not_accepted_after_a_newer_boot(self):
        log = self.topology / "danos.serial.log"
        log.write_text(self.serial() + self.serial().split("VPP-ECMP-SOAK BEGIN")[0])
        with self.assertRaisesRegex(ResultError, "does not belong to the latest DANOS boot"):
            validate(self.topology, 1000)

    def test_soak_failure_marker_is_rejected(self):
        log = self.topology / "danos.serial.log"
        log.write_text(log.read_text().replace(
            "VPP-ECMP-SOAK PASS", "VPP-ECMP-SOAK-FLOW-FAIL target=30.30.30.5\n"
            "VPP-ECMP-SOAK PASS"
        ))
        with self.assertRaisesRegex(ResultError, "failure marker"):
            validate(self.topology, 1000)

    def test_soak_must_meet_requested_count(self):
        with self.assertRaisesRegex(ResultError, "flow/count"):
            validate(self.topology, 2000)

    def test_pps_must_match_packets_and_elapsed_time(self):
        log = self.topology / "danos.serial.log"
        log.write_text(log.read_text().replace("pps=82.08", "pps=999.00"))
        with self.assertRaisesRegex(ResultError, "PPS disagrees"):
            validate(self.topology, 1000)


if __name__ == "__main__":
    unittest.main(verbosity=2)
