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
TRAFFIC_MEMBERS = {
    "vpp-dpdk-traffic-test.enabled",
    "usr/lib/x86_64-linux-gnu/vpp_plugins/dpdk_plugin.so",
    "usr/lib/x86_64-linux-gnu/vpp_plugins/ping_plugin.so",
    "etc/danos/vpp-traffic.env", "etc/vpp/startup.conf", "init",
}
TRAFFIC_CONTENTS = {
    "etc/danos/vpp-traffic.env": "VPP_STATIC_NEIGHBORS=0\nVPP_ECMP_SOAK_COUNT=1000\n",
    "etc/vpp/startup.conf": "plugin dpdk_plugin.so { enable }\nplugin ping_plugin.so { enable }\n",
    "init": "VPP-DPDK-NEIGHBORS dynamic-arp",
}


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

    def test_traffic_mode_requires_traffic_and_soak_profile(self):
        identity = dict(PROFILE.EXPECTED, iso_build_commit="abcdef0",
                        danos_build_ecmp_soak_count="1000",
                        danos_build_static_neighbors="0")
        identity["danos_build_dpdk_traffic_test"] = "1"
        with patch.object(PROFILE, "read_identity", return_value=identity), patch.object(
            PROFILE, "read_initramfs_members", return_value=(TRAFFIC_MEMBERS, TRAFFIC_CONTENTS)
        ):
            with patch("sys.argv", [str(MODULE), "runner.iso", "--traffic-test"]):
                self.assertEqual(PROFILE.main(), 0)
        identity["danos_build_ecmp_soak_count"] = "999"
        with patch.object(PROFILE, "read_identity", return_value=identity), patch.object(
            PROFILE, "read_initramfs_members", return_value=(TRAFFIC_MEMBERS, TRAFFIC_CONTENTS)
        ):
            with patch("sys.argv", [str(MODULE), "runner.iso", "--traffic-test"]):
                self.assertEqual(PROFILE.main(), 1)
        identity["danos_build_ecmp_soak_count"] = "1000"
        identity["danos_build_static_neighbors"] = "0"
        payload = dict(TRAFFIC_CONTENTS)
        payload["etc/vpp/startup.conf"] = "plugin dpdk_plugin.so { enable }"
        with patch.object(PROFILE, "read_identity", return_value=identity), patch.object(
            PROFILE, "read_initramfs_members", return_value=(TRAFFIC_MEMBERS, payload)
        ):
            with patch("sys.argv", [str(MODULE), "runner.iso", "--traffic-test"]):
                self.assertEqual(PROFILE.main(), 1)
        identity["danos_build_static_neighbors"] = "0"
        with patch.object(PROFILE, "read_identity", return_value=identity), patch.object(
            PROFILE, "read_initramfs_members", return_value=(TRAFFIC_MEMBERS - {
                "usr/lib/x86_64-linux-gnu/vpp_plugins/ping_plugin.so"
            }, TRAFFIC_CONTENTS)
        ):
            with patch("sys.argv", [str(MODULE), "runner.iso", "--traffic-test"]):
                self.assertEqual(PROFILE.main(), 1)
        identity["danos_build_static_neighbors"] = "1"
        with patch.object(PROFILE, "read_identity", return_value=identity), patch.object(
            PROFILE, "read_initramfs_members", return_value=(TRAFFIC_MEMBERS, TRAFFIC_CONTENTS)
        ):
            with patch("sys.argv", [str(MODULE), "runner.iso", "--traffic-test"]):
                self.assertEqual(PROFILE.main(), 1)


if __name__ == "__main__":
    unittest.main()
