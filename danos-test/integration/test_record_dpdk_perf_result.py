#!/usr/bin/env python3
"""Deterministic tests for PCI DPDK measurement qualification."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOL = Path(__file__).with_name("record_dpdk_perf_result.py")

PREFLIGHT = """status=ENVIRONMENT-OPEN
preflight_status=PASS
commit=deadbeef
iso_sha256=0123456789abcdef
target_bdf=0000:01:00.0
pci_vendor_id=0x8086
pci_device_id=0x1539
pci_driver=uio_pci_generic
pci_bound_driver=uio_pci_generic
"""

MEASUREMENT = """lane=pci-dpdk
stage=single-core-64b
commit=deadbeef
iso_sha256=0123456789abcdef
packet_size_bytes=64
flows=2
packets_tx=10000
packets_rx=10000
loss_pct=0
duration_ms=10000
pps=1000
mbps=0.512
rtt_p50_us=800
rtt_p99_us=1200
cpu_pct=22.5
ecmp_bucket_0=5000
ecmp_bucket_1=5000
restart_replay=SKIP
"""


class RecordDpdkPerfResultTest(unittest.TestCase):
    def run_case(self, measurement=MEASUREMENT, extra=()):
        with tempfile.TemporaryDirectory(prefix="dpdk-result-test-") as tmp:
            root = Path(tmp)
            preflight = root / "preflight.env"
            measured = root / "measurement.env"
            output = root / "result.env"
            preflight.write_text(PREFLIGHT)
            measured.write_text(measurement)
            command = [sys.executable, str(TOOL), "--preflight", str(preflight),
                       "--measurement", str(measured), "--out", str(output), *extra]
            proc = subprocess.run(command, text=True, capture_output=True, check=False)
            return proc, output.read_text()

    def test_valid_measurements_without_thresholds_remain_open(self):
        proc, output = self.run_case()
        self.assertEqual(proc.returncode, 2)
        self.assertIn("status=ENVIRONMENT-OPEN", output)
        self.assertIn("performance_status=ENVIRONMENT-OPEN", output)

    def test_valid_measurements_and_thresholds_pass(self):
        proc, output = self.run_case(extra=("--min-pps", "900", "--max-loss-pct", "0",
                                           "--max-cpu-pct", "50"))
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("status=PASS", output)
        self.assertIn("pci_vendor_id=0x8086", output)

    def test_threshold_failure_is_not_pass(self):
        proc, output = self.run_case(extra=("--min-pps", "1100", "--max-loss-pct", "0",
                                           "--max-cpu-pct", "50"))
        self.assertEqual(proc.returncode, 1)
        self.assertIn("status=FAIL", output)

    def test_identity_mismatch_is_rejected(self):
        proc, output = self.run_case(measurement=MEASUREMENT.replace("deadbeef", "cafebabe"))
        self.assertEqual(proc.returncode, 1)
        self.assertIn("measurement commit does not match preflight", output)
        self.assertIn("status=FAIL", output)

    def test_missing_ecmp_bucket_is_rejected(self):
        proc, output = self.run_case(measurement=MEASUREMENT.replace("ecmp_bucket_1=5000\n", ""))
        self.assertEqual(proc.returncode, 1)
        self.assertIn("missing: ecmp_bucket_1", output)

    def test_single_flow_cannot_qualify_two_ecmp_buckets(self):
        proc, output = self.run_case(measurement=MEASUREMENT.replace("flows=2\n", "flows=1\n"))
        self.assertEqual(proc.returncode, 1)
        self.assertIn("at least two 5-tuple flows", output)
        self.assertIn("status=FAIL", output)


if __name__ == "__main__":
    unittest.main(verbosity=2)
