#!/usr/bin/env python3
"""Qualify physical I211 VPP ping, ECMP soak, and next-hop recovery serial evidence."""

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
from record_i211_boot_result import QualificationError, result_from_log as boot_result


BDFS = ["0000:01:00.0", "0000:02:00.0"]
DESTINATIONS = ["30.30.30.2", "30.30.30.3", "30.30.30.4", "30.30.30.5"]


def _number(line: str, key: str) -> int:
    match = re.search(rf"(?:^|\s){re.escape(key)}=(\d+)(?:\s|$)", line)
    if not match:
        raise QualificationError(f"traffic result marker is missing numeric {key}")
    return int(match.group(1))


def result_from_log(text: str, *, identity: dict[str, str], iso_name: str,
                    iso_sha256: str, runner_commit: str,
                    bdfs: list[str] | None = None) -> dict[str, str]:
    bdfs = bdfs or BDFS
    boot_start = text.rfind("DANOS-INIT-ENTER")
    if boot_start < 0:
        observed_failures = re.findall(
            r"VPP-(?:DPDK-PING-[01]|ECMP-[A-Z0-9-]+)(?:-FLOW)?\s+FAIL[^\r\n]*|"
            r"VPP-ECMP-[A-Z0-9-]*-FLOW-FAIL[^\r\n]*",
            text,
        )
        if observed_failures:
            details = "; ".join(dict.fromkeys(
                re.sub(r"[^\x20-\x7e]", "", marker).strip()
                for marker in observed_failures
            ))
            return {
                "schema_version": "1", "status": "FAIL",
                "functional_status": "FAIL",
                "performance_status": "ENVIRONMENT-OPEN",
                "stage": "physical-functional", "lane": "i211-vpp-ecmp",
                "identity_status": "UNVERIFIED",
                "failure_reason": (
                    "serial log has no live-init identity marker; explicit VPP failure "
                    f"evidence was observed: {details}"
                ),
            }
        return {"status": "SKIP", "functional_status": "SKIP",
                "identity_status": "UNVERIFIED",
                "failure_reason": "serial log has no live-init boot marker or traffic failure marker"}
    boot_text = text[boot_start:]
    try:
        preflight = boot_result(
            boot_text, identity=identity, iso_name=iso_name,
            iso_sha256=iso_sha256, bdfs=bdfs, driver="uio_pci_generic",
            runner_commit=runner_commit,
        )
    except QualificationError:
        raise
    if preflight.get("preflight_status") != "PASS":
        return {"status": "SKIP", "functional_status": "SKIP",
                "failure_reason": preflight.get("failure_reason", "I211 preflight incomplete")}
    if identity.get("danos_build_dpdk_traffic_test") != "1":
        raise QualificationError("ISO does not enable the VPP traffic test")
    if identity.get("danos_build_static_neighbors") != "0":
        raise QualificationError("physical traffic ISO must use dynamic ARP, not virtual fixture neighbors")
    try:
        configured_soak = int(identity.get("danos_build_ecmp_soak_count", ""))
    except ValueError as exc:
        raise QualificationError("ISO does not embed a valid ECMP soak count") from exc
    if configured_soak < 1000:
        raise QualificationError("ISO ECMP soak count is below 1000 packets per destination")

    required = ["VPP-DPDK-PING-0 PASS", "VPP-DPDK-PING-1 PASS",
                "VPP-ECMP-SOURCE PASS", "VPP-ECMP-MULTI-FLOW PASS",
                "VPP-ECMP-NH-WITHDRAW PASS", "VPP-ECMP-PATH-FAILOVER PASS",
                "VPP-ECMP-NH-RESTORE PASS", "VPP-ECMP-PATH-RESTORE PASS"]
    required.append("VPP-DPDK-NEIGHBORS dynamic-arp")
    if f"VPP-ECMP-SOAK BEGIN packets_per_flow={configured_soak}" not in boot_text:
        required.append(f"VPP-ECMP-SOAK BEGIN packets_per_flow={configured_soak}")
    missing = [marker for marker in required if marker not in boot_text]
    fib_match = re.search(r"VPP-ECMP-FIB PASS buckets=(\d+)", boot_text)
    if not fib_match or int(fib_match.group(1)) < 2:
        missing.append("VPP-ECMP-FIB PASS buckets>=2")
    for destination in DESTINATIONS:
        marker = re.compile(
            rf"VPP-ECMP-FLOW target={re.escape(destination)} tx=3 rx=3 loss=0(?:\s|$)"
        )
        if not marker.search(boot_text):
            missing.append(f"loss-free ECMP probe for {destination}")
    soak_lines = re.findall(r"(?m)^.*VPP-ECMP-SOAK PASS flows=4 .*?$", boot_text)
    if not soak_lines:
        missing.append("VPP-ECMP-SOAK PASS")
        soak_line = ""
    else:
        soak_line = soak_lines[-1]
        try:
            soak_per_flow = _number(soak_line, "packets_per_flow")
            soak_tx = _number(soak_line, "total_tx")
            soak_rx = _number(soak_line, "total_rx")
            elapsed_ms = _number(soak_line, "elapsed_ms")
            bucket0 = _number(soak_line, "bucket0_packets")
            bucket1 = _number(soak_line, "bucket1_packets")
        except QualificationError as exc:
            missing.append(str(exc))
            soak_per_flow = soak_tx = soak_rx = elapsed_ms = bucket0 = bucket1 = 0
        if soak_per_flow < max(1000, configured_soak):
            missing.append("VPP soak packets_per_flow is below the configured minimum")
        if soak_tx != soak_per_flow * 4 or soak_rx != soak_tx:
            missing.append("VPP soak total packet counts do not match four lossless flows")
        if elapsed_ms <= 0:
            missing.append("VPP soak elapsed_ms must be positive")
        if bucket0 <= 0 or bucket1 <= 0:
            missing.append("both VPP ECMP bucket counters must be positive")
        pps_match = re.search(r"(?:^|\s)pps=(\d+(?:\.\d+)?)(?:\s|$)", soak_line)
        if not pps_match:
            missing.append("VPP soak pps is missing or malformed")
        elif elapsed_ms > 0:
            reported_pps = float(pps_match.group(1))
            measured_pps = soak_tx * 1000 / elapsed_ms
            if abs(reported_pps - measured_pps) > max(1.0, measured_pps * 0.05):
                missing.append("VPP soak pps is inconsistent with packet count and elapsed time")
        if "loss=0" not in soak_line:
            missing.append("VPP soak loss=0")
    if re.search(r"VPP-(?:DPDK-PING-[01]|ECMP-(?:MULTI-FLOW|SOAK|PATH-FAILOVER|PATH-RESTORE|NH-WITHDRAW|NH-RESTORE)) FAIL", boot_text):
        missing.append("one or more VPP traffic/failover failure markers")
    if missing:
        raise QualificationError("incomplete physical traffic evidence: " + "; ".join(missing))

    assert soak_line
    result = {
        "schema_version": "1", "status": "PASS", "functional_status": "PASS",
        "performance_status": "ENVIRONMENT-OPEN", "stage": "physical-functional",
        "lane": "i211-vpp-ecmp", "iso_name": iso_name,
        "iso_sha256": iso_sha256, "iso_build_commit": identity["iso_build_commit"],
        "iso_source_dirty": identity["iso_source_dirty"], "runner_commit": runner_commit,
        "target_bdfs": ",".join(bdfs), "pci_id": "8086:1539",
        "pci_driver": "uio_pci_generic", "direct_pings": "2x3/3",
        "ecmp_flows": "4", "ecmp_probe_packets_per_flow": "3",
        "soak_packets_per_flow": str(soak_per_flow),
        "packets_tx": str(soak_tx),
        "packets_rx": str(soak_rx),
        "loss_pct": "0", "duration_ms": str(elapsed_ms),
        "functional_pps": pps_match.group(1),
        "ecmp_bucket_0": str(bucket0), "ecmp_bucket_1": str(bucket1),
        "path_down": "PASS", "nh_withdraw": "PASS", "path_restore": "PASS",
        "nh_restore": "PASS", "host_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
    }
    return result


def encode(values: dict[str, str]) -> str:
    return "".join(f"{key}={shlex.quote(str(value))}\n" for key, value in values.items())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--serial", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    try:
        identity = read_identity(args.iso)
        serial_text = args.serial.read_text(errors="replace")
        iso_sha256 = hashlib.sha256(args.iso.read_bytes()).hexdigest()
        runner_commit = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
        result = result_from_log(
            serial_text, identity=identity, iso_name=args.iso.name,
            iso_sha256=iso_sha256, runner_commit=runner_commit,
        )
    except (OSError, subprocess.SubprocessError, IdentityError, QualificationError) as exc:
        result = {"schema_version": "1", "status": "FAIL",
                  "functional_status": "FAIL", "performance_status": "ENVIRONMENT-OPEN",
                  "stage": "physical-functional", "lane": "i211-vpp-ecmp",
                  "failure_reason": str(exc)}
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(encode(result))
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(encode(result))
    if result["status"] == "PASS":
        print(f"[PASS] physical I211 VPP ping/ECMP/withdraw/restore evidence: {args.out}")
        return 0
    if result["status"] == "FAIL":
        print(f"[FAIL] physical I211 traffic failure evidence: {result.get('failure_reason', '')}",
              file=sys.stderr)
        return 1
    print(f"[SKIP] physical I211 traffic evidence unavailable: {result.get('failure_reason', '')}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
