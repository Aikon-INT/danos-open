#!/usr/bin/env python3
"""Validate the latest VMware VMXNET3 two-path packet run and emit a result."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import math
import re
import shlex
from pathlib import Path


BENCH = re.compile(r"^PEER-TRAFFIC-BENCH\s+(.*)$")
FIELD = re.compile(r"([a-z_]+)=([^\s]+)")
PING_SUMMARY = re.compile(
    r"(?P<tx>\d+) packets transmitted,\s*(?P<rx>\d+) packets received,\s*"
    r"(?P<loss>[\d.]+)% packet loss"
)
RTT = re.compile(r"time=([\d.]+) ms")


class ResultError(Exception):
    pass


def percentile(values: list[float], percent: float) -> float:
    values = sorted(values)
    return values[max(0, math.ceil(percent * len(values)) - 1)]


def latest_peer_run(text: str, min_pps: float) -> list[dict[str, object]]:
    lines = text.splitlines()
    pass_positions = [i for i, line in enumerate(lines) if line.strip() == "PEER-TRAFFIC PASS"]
    if not pass_positions:
        raise ResultError("peer log has no completed PEER-TRAFFIC PASS cycle")
    end = pass_positions[-1]
    if any(BENCH.match(line.strip()) for line in lines[end + 1:]):
        raise ResultError("a newer peer traffic attempt exists after the last PASS marker")
    if any(line.strip() == "PEER-TRAFFIC FAIL" for line in lines[end + 1:]):
        raise ResultError("a newer peer traffic failure exists after the last PASS marker")
    previous = max((i for i in pass_positions[:-1] if i < end), default=-1)
    start = previous + 1
    cycle = lines[start:end + 1]

    samples: list[dict[str, object]] = []
    i = 0
    while i < len(cycle):
        marker = BENCH.match(cycle[i].strip())
        if not marker:
            i += 1
            continue
        fields = dict(FIELD.findall(marker.group(1)))
        try:
            target = fields["target"]
            packets = int(fields["packets"])
            elapsed_cs = int(fields["elapsed_cs"])
            pps = float(fields["pps"])
        except (KeyError, ValueError) as exc:
            raise ResultError(f"malformed traffic marker: {cycle[i]}") from exc
        j = i + 1
        while j < len(cycle) and not BENCH.match(cycle[j].strip()):
            j += 1
        block = "\n".join(cycle[i + 1:j])
        summary = PING_SUMMARY.search(block)
        if not summary:
            raise ResultError(f"{target}: missing ping packet/loss summary")
        tx, rx, loss = int(summary["tx"]), int(summary["rx"]), float(summary["loss"])
        if tx != packets or rx != packets or loss != 0:
            raise ResultError(
                f"{target}: expected lossless {packets}-packet run; got tx={tx} rx={rx} loss={loss}%"
            )
        if elapsed_cs <= 0 or pps < min_pps:
            raise ResultError(f"{target}: elapsed time invalid or pps={pps} below {min_pps}")
        measured_pps = tx * 100 / elapsed_cs
        if abs(measured_pps - pps) > 0.02:
            raise ResultError(f"{target}: pps marker disagrees with packets/elapsed time")
        samples.append({
            "target": target,
            "tx": tx,
            "rx": rx,
            "loss": loss,
            "elapsed_ms": elapsed_cs * 10,
            "pps": pps,
            "rtts_us": [float(x) * 1000 for x in RTT.findall(block)],
        })
        i = j

    expected = {"192.168.45.3", "192.168.46.3"}
    if len(samples) != 2 or {str(s["target"]) for s in samples} != expected:
        raise ResultError("latest peer cycle must contain one sample for each VMXNET3 path")

    # Both samples must belong to the latest completed peer cycle; the explicit
    # PASS marker is already the delimiter used to form `cycle` above.
    return samples


def latest_danos_boot(text: str) -> str:
    marker = "VPP API socket: ready"
    start = text.rfind(marker)
    if start < 0:
        raise ResultError("DANOS serial log has no VPP-ready marker")
    boot = text[start:]
    required = (
        "VPP-DPDK-INTERFACES PASS",
        "VPP-DPDK-PING-0 PASS",
        "VPP-DPDK-PING-1 PASS",
        "VPP-ECMP-COUNTERS-AFTER",
        "192.168.45.3/24",
        "192.168.46.3/24",
    )
    missing = [item for item in required if item not in boot]
    if missing:
        raise ResultError("latest DANOS boot is missing: " + ", ".join(missing))
    fib = re.search(r"dpo-load-balance:.*?buckets:(\d+)", boot, re.S)
    if not fib or int(fib.group(1)) < 2:
        raise ResultError("latest DANOS boot has no resolved two-bucket ECMP FIB")
    return boot


def latest_build_identity(text: str) -> tuple[str, str, str, str]:
    vpp_ready = text.rfind("VPP API socket: ready")
    if vpp_ready < 0:
        return "", "unknown", "", "unknown"
    marker = text.rfind("DANOS-BUILD ", 0, vpp_ready)
    if marker < 0:
        return "", "unknown", "", "unknown"
    line = text[marker:text.find("\n", marker) if "\n" in text[marker:] else len(text)]
    match = re.search(
        r"DANOS-BUILD commit=([0-9a-f]{40,64}) source_dirty=([01]) "
        r"iso=([^\s]+) no_rx_interrupts=([01])",
        line,
    )
    if not match:
        return "", "unknown", "", "unknown"
    return match.group(1), match.group(2), match.group(3), match.group(4)


def env_text(values: dict[str, object]) -> str:
    return "".join(f"{key}={shlex.quote(str(value))}\n" for key, value in values.items())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--peer-log", required=True, type=Path)
    parser.add_argument("--danos-log", required=True, type=Path)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--min-pps", type=float, default=50.0)
    args = parser.parse_args()

    try:
        samples = latest_peer_run(args.peer_log.read_text(errors="replace"), args.min_pps)
        danos_text = args.danos_log.read_text(errors="replace")
        latest_danos_boot(danos_text)
        commit, dirty, boot_iso, no_rx_interrupts = latest_build_identity(danos_text)
        if boot_iso and boot_iso != args.iso.name:
            raise ResultError(f"DANOS booted {boot_iso}, not requested ISO {args.iso.name}")
        if no_rx_interrupts not in ("unknown", "1"):
            raise ResultError("VMware polling-only lane boot lacks no-rx-interrupts=1")
        rtts = [value for sample in samples for value in sample["rtts_us"]]
        if not rtts:
            raise ResultError("latest peer cycle contains no per-packet RTT observations")
        tx = sum(int(sample["tx"]) for sample in samples)
        rx = sum(int(sample["rx"]) for sample in samples)
        elapsed_ms = sum(int(sample["elapsed_ms"]) for sample in samples)
        pps = tx * 1000 / elapsed_ms
        # ICMP echo uses 56 data bytes: 14-byte Ethernet + 20-byte IPv4 +
        # 8-byte ICMP + 56-byte payload. This is the MAC frame without FCS.
        frame_bytes = 98
        iso_sha = hashlib.sha256(args.iso.read_bytes()).hexdigest()
        result_status = "PASS" if commit and dirty == "0" else "ENVIRONMENT-OPEN"
        if result_status == "PASS" and boot_iso != args.iso.name:
            result_status = "ENVIRONMENT-OPEN"
        if result_status == "PASS" and no_rx_interrupts != "1":
            result_status = "ENVIRONMENT-OPEN"
        values: dict[str, object] = {
            "status": result_status,
            "preflight_status": "PASS",
            "performance_status": result_status,
            "stage": "packet-baseline",
            "lane": "vmware-vmxnet3-polling",
            "commit": commit,
            "source_tree_dirty": dirty,
            "iso_sha256": iso_sha,
            "packet_size_bytes": frame_bytes,
            "flows": len(samples),
            "packets_tx": tx,
            "packets_rx": rx,
            "loss_pct": f"{(tx-rx)*100/tx:.3f}",
            "duration_ms": elapsed_ms,
            "pps": f"{pps:.2f}",
            "mbps": f"{pps*frame_bytes*8/1_000_000:.6f}",
            "rtt_p50_us": f"{percentile(rtts, .50):.3f}",
            "rtt_p99_us": f"{percentile(rtts, .99):.3f}",
            "cpu_pct": "",
            "ecmp_bucket_0": "",
            "ecmp_bucket_1": "",
            "restart_replay": "SKIP",
            "performance_scope": "low-rate ICMP packet regression; not line-rate or ECMP throughput",
            "recorded_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        }
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(env_text(values))
    except (OSError, ResultError) as exc:
        print(f"[FAIL] {exc}")
        return 1

    print(f"[PASS] fresh lossless VMware two-path packet baseline: {args.out}")
    print(f"[INFO] packets={tx}/{rx} duration_ms={elapsed_ms} pps={pps:.2f} iso_sha256={iso_sha}")
    if result_status != "PASS":
        print("[OPEN] source commit is missing or dirty; result is not release-qualified")
    print("[OPEN] line-rate/ECMP performance remain open")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
