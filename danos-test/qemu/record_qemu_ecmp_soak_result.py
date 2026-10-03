#!/usr/bin/env python3
"""Convert identity-bound QEMU ECMP soak logs to the v0.16 result schema."""

import argparse
import datetime as dt
import hashlib
import re
import shlex
import subprocess
import sys
from pathlib import Path


class ResultError(Exception):
    pass


def read_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator or not key or key in values:
            raise ResultError(f"{path}:{number}: malformed or duplicate key")
        values[key] = value
    return values


def write_env(path: Path, values: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(f"{k}={shlex.quote(str(v))}\n" for k, v in values.items()))


def validate(topology: Path, required_count: int) -> dict[str, object]:
    manifest_path = topology / "run-manifest.env"
    serial_path = topology / "danos.serial.log"
    manifest = read_env(manifest_path)
    serial = serial_path.read_text(errors="replace")
    if manifest.get("qemu_dataplane_model") != "e1000":
        raise ResultError("QEMU soak result adapter requires the e1000 dataplane lane")
    iso_value = manifest.get("danos_iso", "")
    if not iso_value or not re.fullmatch(r"[0-9a-f]{64}", manifest.get("danos_iso_sha256", "")):
        raise ResultError("topology manifest is missing ISO path or SHA256")
    iso = Path(iso_value)
    if not iso.is_absolute():
        iso = topology.parent.parent / iso
    if not iso.is_file():
        raise ResultError(f"manifest ISO does not exist: {iso}")
    digest = hashlib.sha256(iso.read_bytes()).hexdigest()
    if digest != manifest["danos_iso_sha256"]:
        raise ResultError("ISO digest does not match topology manifest")

    build_markers = list(re.finditer(
        r"DANOS-BUILD commit=([^\s]+) source_dirty=([01]) iso=([^\s]+)", serial
    ))
    if not build_markers:
        raise ResultError("serial log has no DANOS-BUILD identity marker")

    begins = list(re.finditer(r"VPP-ECMP-SOAK BEGIN packets_per_flow=(\d+)", serial))
    if not begins:
        raise ResultError("serial log has no ECMP soak begin marker")
    begin = begins[-1]
    if build_markers[-1].start() > begin.start():
        raise ResultError("latest ECMP soak does not belong to the latest DANOS boot")
    commit, dirty, iso_name = build_markers[-1].groups()
    if dirty != "0" or commit != manifest.get("git_commit"):
        raise ResultError("boot identity is dirty or differs from topology manifest")
    if iso_name != iso.name:
        raise ResultError("boot ISO name differs from topology manifest")
    end_match = re.search(r"VPP-ECMP-SOAK (?:PASS|FAIL)[^\r\n]*", serial[begin.start():])
    if not end_match:
        raise ResultError("latest ECMP soak has no terminal marker")
    block_end = begin.start() + end_match.end()
    block = serial[begin.start():block_end]
    if re.search(r"VPP-ECMP-SOAK-(?:FLOW-FAIL|FAIL)", block):
        raise ResultError("latest ECMP soak contains a failure marker")
    summary = re.search(
        r"VPP-ECMP-SOAK PASS flows=(\d+) packets_per_flow=(\d+) "
        r"total_tx=(\d+) total_rx=(\d+) loss=([\d.]+) elapsed_ms=(\d+) "
        r"pps=([\d.]+) bucket0_packets=(\d+) bucket1_packets=(\d+)",
        block,
    )
    if not summary:
        raise ResultError("latest ECMP soak did not pass or has malformed summary")
    flows, per_flow, tx, rx = map(int, summary.group(1, 2, 3, 4))
    loss = float(summary.group(5))
    duration = int(summary.group(6))
    pps = float(summary.group(7))
    bucket0, bucket1 = map(int, summary.group(8, 9))
    if int(begin.group(1)) != required_count or flows != 4 or per_flow != required_count:
        raise ResultError("soak flow/count does not match the required QEMU acceptance")
    if tx != flows * per_flow or rx != tx or loss != 0 or duration <= 0 or pps <= 0:
        raise ResultError("QEMU soak packet/loss/timing summary is invalid")
    if abs(pps - tx * 1000 / duration) > 0.02:
        raise ResultError("QEMU soak PPS disagrees with packet count and duration")
    if bucket0 <= 0 or bucket1 <= 0:
        raise ResultError("both ECMP bucket deltas must be positive")
    for target in ("30.30.30.2", "30.30.30.3", "30.30.30.4", "30.30.30.5"):
        if not re.search(
            rf"VPP-ECMP-SOAK-FLOW target={re.escape(target)} "
            rf"tx={per_flow} rx={per_flow} loss=0(?:\D|$)", block
        ):
            raise ResultError(f"missing lossless soak flow for {target}")
    if "VPP-RESTART-TEST PASS" not in serial[block_end:]:
        raise ResultError("serial log has no post-soak VPP restart/replay PASS marker")

    runner_commit = ""
    try:
        runner_commit = subprocess.run(
            ["git", "-C", str(Path(__file__).resolve().parents[2]), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
            timeout=5,
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    return {
        "schema_version": 1,
        "status": "PASS",
        "preflight_status": "PASS",
        "performance_status": "ENVIRONMENT-OPEN",
        "stage": "long-stability",
        "lane": "qemu-e1000",
        "commit": commit,
        "iso_build_commit": commit,
        "iso_source_dirty": dirty,
        "runner_commit": runner_commit,
        "iso_sha256": digest,
        "packet_size_bytes": "",
        "flows": flows,
        "packets_tx": tx,
        "packets_rx": rx,
        "loss_pct": f"{loss:.3f}",
        "duration_ms": duration,
        "pps": f"{pps:.2f}",
        "mbps": "",
        "rtt_p50_us": "",
        "rtt_p99_us": "",
        "cpu_pct": "",
        "ecmp_bucket_0": bucket0,
        "ecmp_bucket_1": bucket1,
        "restart_replay": "PASS",
        "performance_scope": "QEMU functional ECMP soak; not PCI performance or line rate",
        "recorded_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--topology", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--required-count", type=int, default=1000)
    args = parser.parse_args()
    try:
        if args.required_count < 1000:
            raise ResultError("QEMU release result requires at least 1000 packets per flow")
        result = validate(args.topology, args.required_count)
    except (OSError, ResultError) as exc:
        write_env(args.out, {
            "schema_version": 1,
            "status": "FAIL",
            "preflight_status": "FAIL",
            "performance_status": "FAIL",
            "stage": "long-stability",
            "lane": "qemu-e1000",
            "failure_reason": str(exc),
        })
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    write_env(args.out, result)
    print(f"[PASS] QEMU functional ECMP soak recorded: {args.out}")
    print(f"[INFO] packets={result['packets_tx']}/{result['packets_rx']} "
          f"pps={result['pps']} buckets={result['ecmp_bucket_0']}/{result['ecmp_bucket_1']}; "
          "PCI performance remains ENVIRONMENT-OPEN")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
