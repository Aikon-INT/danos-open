#!/usr/bin/env python3
"""Check unsafe/incomplete I211 traffic profiles fail before starting Docker builds."""

import os
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BUILDER = ROOT / "danos-test/live/build_i211_traffic_runner_iso.sh"


def rejected(overrides: dict[str, str], expected: str) -> None:
    env = os.environ.copy()
    env.update(overrides)
    with tempfile.TemporaryDirectory(prefix="i211-builder-config-") as temp:
        result = subprocess.run(
            ["bash", str(BUILDER), str(Path(temp) / "must-not-build.iso")],
            env=env, capture_output=True, text=True, timeout=5, check=False,
        )
    output = result.stdout + result.stderr
    assert result.returncode == 2, f"builder returned {result.returncode}, expected 2:\n{output}"
    assert expected in output, f"missing {expected!r}:\n{output}"


def main() -> None:
    rejected({"VPP_ECMP_SOAK_COUNT": "999"}, "at least 1000 probes")
    rejected({"VPP_STATIC_NEIGHBORS": "1"}, "must use dynamic ARP")
    rejected({"VPP_DPDK_TRAFFIC_TEST": "0"}, "must remain enabled")
    print("PASS: invalid I211 traffic profiles are rejected before Docker/build side effects")


if __name__ == "__main__":
    main()
