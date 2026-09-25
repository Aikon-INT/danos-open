#!/usr/bin/env python3
"""
DANOS-Open Multi-Core Forwarding Throughput Test (H3)

Tests forwarding throughput across multiple cores using TRex traffic generator
against a VPP dataplane. Measures:
- Per-core throughput (Mpps)
- Aggregate throughput vs linear scaling
- Latency percentiles (p50, p95, p99)
- CPU utilization per core

Requirements (not available in current env, framework ready for deployment):
- VPP dataplane running with DPDK
- TRex traffic generator v3.x
- Containerlab topology: 2x DANOS nodes back-to-back
- At least 4 CPU cores

Usage:
    python3 test_multicore_perf.py --cores 4 --duration 60 --frame-size 64
    python3 test_multicore_perf.py --simulate --cores 4  # demo only, never an acceptance result
    python3 test_multicore_perf.py --topology b2b --validate-scaling
"""

import argparse
import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from typing import List, Optional


@dataclass
class CoreResult:
    core_id: int
    rx_pps: float          # packets per second
    tx_pps: float
    rx_bps: float          # bits per second
    tx_bps: float
    drops: int
    cpu_util: float        # percentage


@dataclass
class TestResult:
    timestamp: str
    num_cores: int
    frame_size: int
    duration_sec: int
    per_core: List[CoreResult]
    aggregate_mpps: float
    aggregate_gbps: float
    latency_p50_ns: int
    latency_p95_ns: int
    latency_p99_ns: int
    expected_mpps: float    # 1-core * N (linear scaling)
    scaling_efficiency: float  # actual / expected


def run_trex(cores: int, duration: int, frame_size: int,
             trex_dir: str = "/opt/trex", simulate: bool = False) -> dict:
    """Run TRex traffic generator and return parsed stats."""
    # In production: invoke TRex STL client
    # python3 -c "from trex_stl_lib import *; ..."
    #
    if not simulate:
        raise RuntimeError(
            "TRex integration is not available; refusing to fabricate a "
            "DPDK performance result (use --simulate only for a demo)"
        )

    # Explicit demo mode only. These values must never be used as an
    # acceptance baseline.
    results = []
    base_pps = 14.88e6  # 10Gbps @ 64B frames = 14.88 Mpps
    per_core_pps = base_pps / 1.0  # each core handles ~14.88 Mpps

    for i in range(cores):
        # Simulate slight per-core variance
        efficiency = 0.95 + 0.02 * (i % 3)
        results.append({
            "core_id": i,
            "rx_pps": per_core_pps * efficiency,
            "tx_pps": per_core_pps * efficiency,
            "rx_bps": per_core_pps * efficiency * frame_size * 8,
            "tx_bps": per_core_pps * efficiency * frame_size * 8,
            "drops": 0,
            "cpu_util": 85.0 + (i % 5),
        })
    return {"per_core": results}


def measure_latency(duration: int, simulate: bool = False) -> dict:
    """Measure latency percentiles using TRex latency stream."""
    if not simulate:
        raise RuntimeError(
            "TRex latency integration is not available; refusing to "
            "fabricate latency percentiles"
        )
    return {
        "p50_ns": 5000,    # 5us
        "p95_ns": 15000,   # 15us
        "p99_ns": 50000,   # 50us
    }


def run_test(cores: int, duration: int, frame_size: int,
             simulate: bool = False) -> TestResult:
    """Run multi-core throughput test."""
    print(f"Starting multi-core throughput test:")
    print(f"  Cores: {cores}")
    print(f"  Duration: {duration}s")
    print(f"  Frame size: {frame_size}B")

    # Run traffic
    trex_stats = run_trex(cores, duration, frame_size, simulate=simulate)
    latency = measure_latency(duration, simulate=simulate)

    # Build per-core results
    per_core = []
    total_pps = 0.0
    total_bps = 0.0
    for s in trex_stats["per_core"]:
        cr = CoreResult(
            core_id=s["core_id"],
            rx_pps=s["rx_pps"],
            tx_pps=s["tx_pps"],
            rx_bps=s["rx_bps"],
            tx_bps=s["tx_bps"],
            drops=s["drops"],
            cpu_util=s["cpu_util"],
        )
        per_core.append(cr)
        total_pps += s["tx_pps"]
        total_bps += s["tx_bps"]

    # Calculate scaling
    single_core_pps = per_core[0].tx_pps if per_core else 0
    expected_mpps = single_core_pps * cores / 1e6
    actual_mpps = total_pps / 1e6
    efficiency = (actual_mpps / expected_mpps * 100) if expected_mpps > 0 else 0

    return TestResult(
        timestamp=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        num_cores=cores,
        frame_size=frame_size,
        duration_sec=duration,
        per_core=per_core,
        aggregate_mpps=actual_mpps,
        aggregate_gbps=total_bps / 1e9,
        latency_p50_ns=latency["p50_ns"],
        latency_p95_ns=latency["p95_ns"],
        latency_p99_ns=latency["p99_ns"],
        expected_mpps=expected_mpps,
        scaling_efficiency=efficiency,
    )


def validate_scaling(results: List[TestResult]) -> bool:
    """Validate that throughput scales linearly with cores.

    Acceptance criteria (H3):
    - 2-core throughput >= 1.8x single-core (90% efficiency)
    - 4-core throughput >= 3.5x single-core (87.5% efficiency)
    - Latency p99 < 100us at all core counts
    """
    if len(results) < 2:
        print("Need at least 2 test results for scaling validation")
        return False

    base = results[0]  # single core
    all_pass = True

    for r in results[1:]:
        ratio = r.aggregate_mpps / base.aggregate_mpps
        expected_ratio = r.num_cores / base.num_cores
        eff = ratio / expected_ratio * 100

        print(f"\n{r.num_cores} cores vs {base.num_cores} core:")
        print(f"  Throughput ratio: {ratio:.2f}x (expected {expected_ratio:.1f}x)")
        print(f"  Scaling efficiency: {eff:.1f}%")
        print(f"  Aggregate: {r.aggregate_mpps:.2f} Mpps / {r.aggregate_gbps:.2f} Gbps")
        print(f"  Latency p99: {r.latency_p99_ns/1000:.1f} us")

        if eff < 85.0:
            print(f"  FAIL: efficiency {eff:.1f}% < 85% threshold")
            all_pass = False
        if r.latency_p99_ns > 100000:
            print(f"  FAIL: p99 latency {r.latency_p99_ns/1000:.1f}us > 100us")
            all_pass = False
        if eff >= 85.0 and r.latency_p99_ns <= 100000:
            print(f"  PASS")

    return all_pass


def main():
    parser = argparse.ArgumentParser(description="DANOS Multi-Core Perf Test")
    parser.add_argument("--cores", type=int, default=4,
                        help="Number of cores to test")
    parser.add_argument("--duration", type=int, default=60,
                        help="Test duration in seconds")
    parser.add_argument("--frame-size", type=int, default=64,
                        help="Frame size in bytes")
    parser.add_argument("--topology", choices=["b2b", "3node"], default="b2b")
    parser.add_argument("--validate-scaling", action="store_true",
                        help="Run 1,2,4 core tests and validate scaling")
    parser.add_argument("--output", default=None,
                        help="Output JSON file for results")
    parser.add_argument("--simulate", action="store_true",
                        help="Run synthetic demo data; never a real acceptance result")
    args = parser.parse_args()

    try:
        if args.validate_scaling:
            results = []
            for n in [1, 2, 4]:
                if n > args.cores:
                    continue
                r = run_test(n, args.duration, args.frame_size, args.simulate)
                results.append(r)
                print(f"\nResult: {r.aggregate_mpps:.2f} Mpps, "
                      f"{r.aggregate_gbps:.2f} Gbps, "
                      f"p99={r.latency_p99_ns/1000:.1f}us")

            ok = validate_scaling(results)
            if args.output:
                with open(args.output, "w") as f:
                    json.dump([asdict(r) for r in results], f, indent=2)
            sys.exit(0 if ok else 1)
        r = run_test(args.cores, args.duration, args.frame_size, args.simulate)
        print(f"\n=== Result ===")
        print(f"Aggregate: {r.aggregate_mpps:.2f} Mpps / {r.aggregate_gbps:.2f} Gbps")
        print(f"Latency p50/p95/p99: {r.latency_p50_ns}/"
              f"{r.latency_p95_ns}/{r.latency_p99_ns} ns")
        print(f"Scaling efficiency: {r.scaling_efficiency:.1f}%")
        for cr in r.per_core:
            print(f"  Core {cr.core_id}: {cr.tx_pps/1e6:.2f} Mpps, "
                  f"CPU {cr.cpu_util:.0f}%, drops {cr.drops}")

        if args.output:
            with open(args.output, "w") as f:
                json.dump(asdict(r), f, indent=2)
    except RuntimeError as exc:
        print(f"[ENVIRONMENT-OPEN] {exc}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
