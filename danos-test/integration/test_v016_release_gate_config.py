#!/usr/bin/env python3
"""Ensure release-gate overrides cannot disable or shrink the ECMP soak."""

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GATE = ROOT / "danos-test/integration/run_v016_release_gate.sh"


def check_rejected(name: str, value: str, expected: str) -> None:
    env = os.environ.copy()
    env[name] = value
    result = subprocess.run(
        ["bash", str(GATE)],
        cwd=ROOT,
        env=env,
        capture_output=True,
        text=True,
        timeout=5,
        check=False,
    )
    output = result.stdout + result.stderr
    assert result.returncode == 2, (
        f"{name}={value!r} returned {result.returncode}, expected 2:\n{output}"
    )
    assert expected in output, f"{name}={value!r} missing {expected!r}:\n{output}"


def main() -> None:
    check_rejected(
        "QEMU_ECMP_SOAK_REQUIRED", "0", "requires QEMU_ECMP_SOAK_REQUIRED=1"
    )
    for value in ("0", "999", "nope"):
        check_rejected(
            "QEMU_ECMP_SOAK_COUNT",
            value,
            "requires at least 1000 packets per ECMP flow",
        )
    print("PASS: release gate rejects disabled, undersized, and invalid ECMP soak overrides")


if __name__ == "__main__":
    main()
