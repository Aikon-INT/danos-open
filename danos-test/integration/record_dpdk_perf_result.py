#!/usr/bin/env python3
"""Validate and combine a PCI DPDK measurement with its runner preflight."""

import argparse
import datetime as dt
import shlex
import sys
from decimal import Decimal, InvalidOperation
from pathlib import Path


METRICS = (
    "flows", "packets_tx", "packets_rx", "loss_pct", "duration_ms", "pps",
    "mbps", "rtt_p50_us", "rtt_p99_us", "cpu_pct", "ecmp_bucket_0",
    "ecmp_bucket_1",
)


class ResultError(Exception):
    pass


def read_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        if not raw or raw.startswith("#"):
            continue
        key, separator, value = raw.partition("=")
        if not separator or not key or key in values:
            raise ResultError(f"{path}:{number}: malformed or duplicate key")
        values[key] = value
    return values


def decimal(values: dict[str, str], key: str) -> Decimal:
    try:
        number = Decimal(values[key])
    except (KeyError, InvalidOperation):
        raise ResultError(f"missing or invalid numeric field: {key}") from None
    if not number.is_finite():
        raise ResultError(f"non-finite numeric field: {key}")
    return number


def encode(values: dict[str, str]) -> str:
    return "".join(f"{key}={shlex.quote(str(value))}\n" for key, value in values.items())


def validate(preflight: dict[str, str], measured: dict[str, str], args) -> dict[str, str]:
    if preflight.get("preflight_status") != "PASS":
        raise ResultError("PCI preflight_status is not PASS")
    for key in ("target_bdf", "pci_vendor_id", "pci_device_id", "pci_bound_driver", "iso_sha256", "commit"):
        if not preflight.get(key):
            raise ResultError(f"preflight is missing identity field: {key}")
    if preflight["pci_bound_driver"] != preflight.get("pci_driver"):
        raise ResultError("preflight target binding does not match requested PCI driver")
    if measured.get("lane", "pci-dpdk") != "pci-dpdk":
        raise ResultError("measurement lane must be pci-dpdk")
    if measured.get("stage", "single-core-64b") != "single-core-64b":
        raise ResultError("first qualification stage must be single-core-64b")
    for identity in ("commit", "iso_sha256"):
        if measured.get(identity) != preflight[identity]:
            raise ResultError(f"measurement {identity} does not match preflight")
    if decimal(measured, "packet_size_bytes") != 64:
        raise ResultError("first qualification requires 64-byte packets")
    missing = [key for key in METRICS if not measured.get(key)]
    if missing:
        raise ResultError("measurement is missing: " + ",".join(missing))

    flows = decimal(measured, "flows")
    tx = decimal(measured, "packets_tx")
    rx = decimal(measured, "packets_rx")
    loss = decimal(measured, "loss_pct")
    duration = decimal(measured, "duration_ms")
    pps = decimal(measured, "pps")
    mbps = decimal(measured, "mbps")
    p50 = decimal(measured, "rtt_p50_us")
    p99 = decimal(measured, "rtt_p99_us")
    cpu = decimal(measured, "cpu_pct")
    bucket0 = decimal(measured, "ecmp_bucket_0")
    bucket1 = decimal(measured, "ecmp_bucket_1")
    if flows < 2:
        raise ResultError("single-core ECMP qualification requires at least two 5-tuple flows")
    if tx < 1 or duration < 1 or pps <= 0 or mbps <= 0:
        raise ResultError("packets_tx, duration_ms, pps and mbps must be positive")
    if rx < 0 or rx > tx:
        raise ResultError("packets_rx must be between zero and packets_tx")
    if not 0 <= loss <= 100 or abs(loss - (tx - rx) * 100 / tx) > Decimal("0.2"):
        raise ResultError("loss_pct is outside range or disagrees with packet counters")
    if p50 < 0 or p99 < p50:
        raise ResultError("latency percentiles are invalid")
    if not 0 <= cpu <= 100:
        raise ResultError("cpu_pct must be in [0,100]")
    if bucket0 < 1 or bucket1 < 1:
        raise ResultError("both ECMP buckets must carry traffic")
    if any(value != value.to_integral_value() for value in (flows, tx, rx, duration, bucket0, bucket1)):
        raise ResultError("flow, packet, duration, and bucket counters must be integers")

    thresholds = (args.min_pps, args.max_loss_pct, args.max_cpu_pct)
    if any(value is not None for value in thresholds) and any(value is None for value in thresholds):
        raise ResultError("provide all three acceptance thresholds or none")
    performance_status = "ENVIRONMENT-OPEN"
    if all(value is not None for value in thresholds):
        if pps < Decimal(str(args.min_pps)) or loss > Decimal(str(args.max_loss_pct)) or cpu > Decimal(str(args.max_cpu_pct)):
            performance_status = "FAIL"
        else:
            performance_status = "PASS"

    result = {
        "status": performance_status,
        "preflight_status": "PASS",
        "performance_status": performance_status,
        "stage": "single-core-64b",
        "lane": "pci-dpdk",
        "commit": preflight["commit"],
        "iso_sha256": preflight["iso_sha256"],
        "packet_size_bytes": "64",
    }
    result.update({key: measured[key] for key in METRICS})
    result.update({
        "restart_replay": measured.get("restart_replay", "SKIP"),
        "target_bdf": preflight["target_bdf"],
        "pci_vendor_id": preflight["pci_vendor_id"],
        "pci_device_id": preflight["pci_device_id"],
        "pci_driver": preflight["pci_driver"],
        "pci_bound_driver": preflight["pci_bound_driver"],
        "min_pps": "" if args.min_pps is None else str(args.min_pps),
        "max_loss_pct": "" if args.max_loss_pct is None else str(args.max_loss_pct),
        "max_cpu_pct": "" if args.max_cpu_pct is None else str(args.max_cpu_pct),
        "recorded_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
    })
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preflight", required=True, type=Path)
    parser.add_argument("--measurement", required=True, type=Path,
                        help="key=value data exported by the real traffic generator")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--min-pps", type=Decimal)
    parser.add_argument("--max-loss-pct", type=Decimal)
    parser.add_argument("--max-cpu-pct", type=Decimal)
    args = parser.parse_args()
    try:
        result = validate(read_env(args.preflight), read_env(args.measurement), args)
    except (OSError, ResultError) as exc:
        result = {
            "status": "FAIL", "preflight_status": "FAIL",
            "performance_status": "FAIL", "stage": "single-core-64b",
            "lane": "pci-dpdk", "failure_reason": str(exc),
        }
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(encode(result))
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(encode(result))
    if result["status"] == "PASS":
        print(f"[PASS] PCI DPDK 64-byte result accepted: {args.out}")
        return 0
    if result["status"] == "FAIL":
        print(f"[FAIL] PCI DPDK thresholds were not met: {args.out}", file=sys.stderr)
        return 1
    print(f"[OPEN] metrics validated; explicit acceptance thresholds are still required: {args.out}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
