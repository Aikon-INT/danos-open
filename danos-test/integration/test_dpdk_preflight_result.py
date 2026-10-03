#!/usr/bin/env python3
"""Verify missing PCI prerequisites produce a versioned structured SKIP."""

import os
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "danos-test/integration/run_vpp_dpdk_lane.sh"


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="dpdk-preflight-result-") as tmp:
        result_path = Path(tmp) / "preflight.env"
        env = os.environ.copy()
        for key in ("DPDK_PCI_BDF", "DPDK_ISO", "DPDK_PCI_DRIVER"):
            env.pop(key, None)
        env["DPDK_RESULT_FILE"] = str(result_path)
        proc = subprocess.run(
            ["bash", str(RUNNER)], cwd=ROOT, env=env,
            capture_output=True, text=True, timeout=10, check=False,
        )
        if proc.returncode != 2:
            raise AssertionError(
                f"preflight returned {proc.returncode}, expected SKIP (2):\n"
                f"{proc.stdout}{proc.stderr}"
            )
        result = result_path.read_text()
        expected = (
            "schema_version=1\n",
            "status=SKIP\n",
            "preflight_status=SKIP\n",
            "performance_status=ENVIRONMENT-OPEN\n",
            "lane=pci-dpdk\n",
            "target_bdf=''\n",
        )
        for field in expected:
            if field not in result:
                raise AssertionError(f"preflight result missing {field!r}:\n{result}")
    print("PASS: missing PCI prerequisites emit versioned SKIP at DPDK_RESULT_FILE")


if __name__ == "__main__":
    main()
