#!/usr/bin/env python3
"""Tests for strict parsing of VMware two-path packet serial evidence."""

import unittest

from record_vmxnet3_packet_result import (
    ResultError,
    latest_build_identity,
    latest_danos_boot,
    latest_peer_run,
)


def peer_block(loss: str = "0", rx: int = 10, suffix: str = "") -> str:
    return "\n".join((
        "PEER-TRAFFIC-BENCH target=192.168.45.3 packets=10 elapsed_cs=10 pps=100.00",
        "PING 192.168.45.3 (192.168.45.3): 56 data bytes",
        "64 bytes from 192.168.45.3: seq=0 ttl=64 time=0.200 ms",
        f"{10} packets transmitted, {rx} packets received, {loss}% packet loss",
        "round-trip min/avg/max = 0.200/0.200/0.200 ms",
        "PEER-TRAFFIC-BENCH target=192.168.46.3 packets=10 elapsed_cs=10 pps=100.00",
        "PING 192.168.46.3 (192.168.46.3): 56 data bytes",
        "64 bytes from 192.168.46.3: seq=0 ttl=64 time=0.300 ms",
        "10 packets transmitted, 10 packets received, 0% packet loss",
        "round-trip min/avg/max = 0.300/0.300/0.300 ms",
        "PEER-TRAFFIC PASS",
        suffix,
    ))


class Vmxnet3PacketResultTest(unittest.TestCase):
    def test_latest_lossless_two_path_cycle_passes(self):
        samples = latest_peer_run(peer_block(), min_pps=50)
        self.assertEqual([s["target"] for s in samples], ["192.168.45.3", "192.168.46.3"])

    def test_packet_loss_is_rejected(self):
        with self.assertRaisesRegex(ResultError, "lossless"):
            latest_peer_run(peer_block(loss="10", rx=9), min_pps=50)

    def test_newer_unfinished_attempt_invalidates_old_pass(self):
        with self.assertRaisesRegex(ResultError, "newer peer traffic attempt"):
            latest_peer_run(peer_block(suffix="PEER-TRAFFIC-BENCH target=192.168.45.3 packets=10 elapsed_cs=10 pps=100.00"), 50)

    def test_newer_failure_invalidates_old_pass(self):
        with self.assertRaisesRegex(ResultError, "newer peer traffic failure"):
            latest_peer_run(peer_block(suffix="PEER-TRAFFIC FAIL"), 50)

    def test_latest_boot_requires_two_bucket_fib_and_both_paths(self):
        boot = """VPP API socket: ready
VPP-DPDK-INTERFACES PASS
VPP-DPDK-PING-0 PASS
VPP-DPDK-PING-1 PASS
192.168.45.3/24
192.168.46.3/24
dpo-load-balance: buckets:2
VPP-ECMP-BUCKETS bucket0_packets=9 bucket1_packets=10
VPP-ECMP-COUNTERS-AFTER
"""
        self.assertEqual(latest_danos_boot("old failed boot\n" + boot), boot)

    def test_newer_boot_failure_is_not_hidden_by_old_pass(self):
        good = "VPP API socket: ready\nVPP-DPDK-INTERFACES PASS\nVPP-DPDK-PING-0 PASS\nVPP-DPDK-PING-1 PASS\n192.168.45.3/24\n192.168.46.3/24\ndpo-load-balance: buckets:2\nVPP-ECMP-BUCKETS bucket0_packets=9 bucket1_packets=10\nVPP-ECMP-COUNTERS-AFTER\n"
        with self.assertRaisesRegex(ResultError, "latest DANOS boot"):
            latest_danos_boot(good + "VPP API socket: ready\nboot failed")

    def test_build_identity_comes_from_latest_danos_boot(self):
        old = "DANOS-BUILD commit=" + "a" * 40 + " source_dirty=0 iso=old.iso no_rx_interrupts=1\n"
        new = "DANOS-BUILD commit=" + "b" * 40 + " source_dirty=0 iso=new.iso no_rx_interrupts=1\nVPP API socket: ready\n"
        self.assertEqual(latest_build_identity(old + "VPP API socket: ready\n" + new), ("b" * 40, "0", "new.iso", "1"))


if __name__ == "__main__":
    unittest.main()
