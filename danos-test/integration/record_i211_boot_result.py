#!/usr/bin/env python3
"""Bind a captured live-ISO serial boot to its image and I211 PCI preflight."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import re
import shlex
import subprocess
import sys
from pathlib import Path

from read_live_iso_identity import IdentityError, read_identity


BUILD = re.compile(
    r"DANOS-BUILD commit=(\S+) source_dirty=(\S+) iso=(\S+) "
    r"no_rx_interrupts=(\S+)"
)
PCI_NIC = re.compile(
    r"PCI-NIC (?P<bdf>[\da-fA-F:.]+) vendor=(?P<vendor>0x[\da-fA-F]+) "
    r"device=(?P<device>0x[\da-fA-F]+)"
)
BIND_PASS = re.compile(
    r"DPDK-PCI-BIND PASS bdf=(\S+) pci_id=([\da-fA-F]{4}:[\da-fA-F]{4}) "
    r"driver=(\S+)"
)


class QualificationError(Exception):
    pass


def result_from_log(text: str, *, identity: dict[str, str], iso_name: str,
                    iso_sha256: str, bdfs: list[str], driver: str,
                    runner_commit: str) -> dict[str, str]:
    builds = list(BUILD.finditer(text))
    if not builds:
        return {"status": "SKIP", "preflight_status": "SKIP",
                "failure_reason": "serial log has no DANOS-BUILD identity marker"}
    boot = builds[-1]
    commit, dirty, boot_iso, polling = boot.groups()
    if (commit.lower() != identity["iso_build_commit"] or dirty != "0"
            or identity["iso_source_dirty"] != "0" or boot_iso != iso_name
            or polling != "1"):
        raise QualificationError("serial boot identity does not match the clean polling ISO")

    inventory = {m["bdf"]: (m["vendor"].lower(), m["device"].lower())
                 for m in PCI_NIC.finditer(text)}
    passes = {bdf: (pci_id.lower(), bound_driver)
              for bdf, pci_id, bound_driver in BIND_PASS.findall(text)}
    failures = re.findall(r"DPDK-PCI-BIND FAIL ([^\r\n]+)", text)
    relevant_failures = [failure for failure in failures
                         if any(bdf in failure for bdf in bdfs)]
    if any("device-absent=" in failure for failure in relevant_failures):
        return {"status": "SKIP", "preflight_status": "SKIP",
                "failure_reason": "requested I211 PCI function is absent in the captured boot",
                "bind_failure_kind": "device-absent"}
    if relevant_failures:
        raise QualificationError("serial log contains DPDK bind failure: " + relevant_failures[0])
    missing = [bdf for bdf in bdfs if bdf not in inventory or bdf not in passes]
    if missing:
        reason = "requested BDF not present or no complete binding marker: " + ",".join(missing)
        return {"status": "SKIP", "preflight_status": "SKIP", "failure_reason": reason,
                "bind_failure_kind": "incomplete-serial-evidence"}

    for bdf in bdfs:
        vendor, device = inventory[bdf]
        if (vendor, device) != ("0x8086", "0x1539"):
            raise QualificationError(f"{bdf} inventory PCI ID is {vendor}:{device}, not I211")
        pci_id, bound_driver = passes[bdf]
        if pci_id != "8086:1539" or bound_driver != driver:
            raise QualificationError(f"{bdf} binding marker does not match requested I211/driver")

    if "VPP API socket: ready" not in text or "VPP stats socket: ready" not in text:
        return {"status": "SKIP", "preflight_status": "SKIP",
                "failure_reason": "serial evidence lacks VPP API/stats readiness"}
    if "VPP-DPDK-INTERFACES PASS" not in text:
        raise QualificationError("serial evidence lacks VPP-DPDK-INTERFACES PASS")

    return {
        "status": "ENVIRONMENT-OPEN", "preflight_status": "PASS",
        "performance_status": "ENVIRONMENT-OPEN", "stage": "preflight",
        "lane": "pci-dpdk", "commit": commit.lower(),
        "iso_build_commit": identity["iso_build_commit"], "iso_source_dirty": "0",
        "runner_commit": runner_commit, "iso_sha256": iso_sha256,
        "packet_size_bytes": "64", "flows": "", "packets_tx": "", "packets_rx": "",
        "loss_pct": "", "duration_ms": "", "pps": "", "mbps": "",
        "rtt_p50_us": "", "rtt_p99_us": "", "cpu_pct": "",
        "ecmp_bucket_0": "", "ecmp_bucket_1": "", "restart_replay": "SKIP",
        "target_bdf": bdfs[0], "target_bdfs": ",".join(bdfs),
        "pci_vendor_id": "0x8086", "pci_device_id": "0x1539",
        "pci_driver": driver, "pci_bound_driver": driver,
        "host_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
    }


def encode(values: dict[str, str]) -> str:
    return "".join(f"{key}={shlex.quote(str(value))}\n" for key, value in values.items())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--serial", type=Path, required=True)
    parser.add_argument("--bdf", action="append", required=True,
                        help="expected I211 PCI BDF; repeat once per dataplane port")
    parser.add_argument("--driver", default="uio_pci_generic")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    try:
        identity = read_identity(args.iso)
        serial_text = args.serial.read_text(errors="replace")
        iso_hash = hashlib.sha256(args.iso.read_bytes()).hexdigest()
        runner_commit = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
        values = result_from_log(
            serial_text, identity=identity, iso_name=args.iso.name,
            iso_sha256=iso_hash, bdfs=args.bdf, driver=args.driver,
            runner_commit=runner_commit,
        )
    except (OSError, subprocess.SubprocessError, IdentityError, QualificationError) as exc:
        values = {"status": "FAIL", "preflight_status": "FAIL",
                  "performance_status": "ENVIRONMENT-OPEN", "stage": "preflight",
                  "lane": "pci-dpdk", "failure_reason": str(exc)}
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(encode(values))
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(encode(values))
    if values["preflight_status"] == "PASS":
        print(f"[OPEN] I211 boot/preflight evidence accepted; traffic performance remains open: {args.out}")
        return 2
    print(f"[SKIP] I211 boot preflight not established: {values.get('failure_reason', '')}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
