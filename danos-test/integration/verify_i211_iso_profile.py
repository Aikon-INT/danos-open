#!/usr/bin/env python3
"""Fail closed unless an ISO contains the intended two-port I211 runner profile."""

import argparse
import sys
from pathlib import Path

from read_live_iso_identity import IdentityError, read_identity


EXPECTED = {
    "iso_source_dirty": "0",
    "danos_build_dpdk_ports": "0000:01:00.0 0000:02:00.0",
    "danos_build_dpdk_no_rx_interrupts": "1",
    "danos_build_dpdk_bind_driver": "uio_pci_generic",
    "danos_build_dpdk_expected_pci_id": "8086:1539",
    "danos_build_dpdk_device": "i211",
    "danos_build_dpdk_enable": "1",
    "danos_build_dpdk_autostart": "1",
    "danos_build_ping_enable": "1",
    "danos_build_dpdk_traffic_test": "0",
    "danos_build_dpdk_port_count": "2",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--expected-commit")
    args = parser.parse_args()
    try:
        identity = read_identity(args.iso)
    except (IdentityError, OSError, ValueError) as exc:
        print(f"FAIL: cannot verify I211 ISO profile: {exc}", file=sys.stderr)
        return 1

    expected = dict(EXPECTED)
    if args.expected_commit:
        expected["iso_build_commit"] = args.expected_commit.lower()
    failures = [
        f"{key}: expected {value!r}, got {identity.get(key)!r}"
        for key, value in expected.items()
        if identity.get(key) != value
    ]
    if failures:
        print("FAIL: I211 runner ISO profile mismatch", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("PASS: clean two-port I211 polling runner profile and ping plugin metadata")
    for key in ("iso_build_commit", "danos_build_iso", "danos_build_vpp_image"):
        if key in identity:
            print(f"{key}={identity[key]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
