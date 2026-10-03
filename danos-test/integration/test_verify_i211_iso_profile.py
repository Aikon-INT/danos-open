#!/usr/bin/env python3
"""Unit tests for the fail-closed I211 runner ISO profile validator."""

import importlib.util
import unittest
from pathlib import Path
from unittest.mock import patch


MODULE = Path(__file__).with_name("verify_i211_iso_profile.py")
SPEC = importlib.util.spec_from_file_location("verify_i211_iso_profile", MODULE)
assert SPEC and SPEC.loader
PROFILE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROFILE)


class VerifyI211ISOProfileTest(unittest.TestCase):
    def test_accepts_exact_runner_profile(self):
        identity = dict(PROFILE.EXPECTED, iso_build_commit="abcdef0")
        with patch.object(PROFILE, "read_identity", return_value=identity):
            with patch("sys.argv", [str(MODULE), "runner.iso"]):
                self.assertEqual(PROFILE.main(), 0)

    def test_rejects_non_i211_driver_or_missing_ping_plugin(self):
        identity = dict(PROFILE.EXPECTED, iso_build_commit="abcdef0")
        identity["danos_build_dpdk_expected_pci_id"] = "10ec:8168"
        identity["danos_build_ping_enable"] = "0"
        with patch.object(PROFILE, "read_identity", return_value=identity):
            with patch("sys.argv", [str(MODULE), "runner.iso"]):
                self.assertEqual(PROFILE.main(), 1)

    def test_can_bind_image_to_expected_commit(self):
        identity = dict(PROFILE.EXPECTED, iso_build_commit="abcdef0")
        with patch.object(PROFILE, "read_identity", return_value=identity):
            with patch("sys.argv", [str(MODULE), "runner.iso", "--expected-commit", "1234567"]):
                self.assertEqual(PROFILE.main(), 1)


if __name__ == "__main__":
    unittest.main()
