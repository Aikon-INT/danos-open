#!/usr/bin/env python3
"""Read the embedded source identity from a DANOS live ISO initramfs."""

import argparse
import gzip
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


class IdentityError(Exception):
    pass


def read_identity(iso: Path) -> dict[str, str]:
    if not iso.is_file():
        raise IdentityError(f"ISO is not a readable file: {iso}")
    for command in ("xorriso", "gzip", "cpio"):
        if not shutil.which(command):
            raise IdentityError(f"required command unavailable: {command}")

    with tempfile.TemporaryDirectory(prefix="danos-iso-identity-") as work:
        archive = Path(work) / "initramfs.cpio.gz"
        extract = subprocess.run(
            ["xorriso", "-osirrox", "on", "-indev", str(iso),
             "-extract", "/initramfs.cpio.gz", str(archive)],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            text=True, timeout=90, check=False,
        )
        if extract.returncode or not archive.is_file():
            raise IdentityError("cannot extract /initramfs.cpio.gz from ISO")

        decompressor = subprocess.Popen(
            ["gzip", "-dc", str(archive)], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            listing = subprocess.run(
                ["cpio", "-i", "--to-stdout", "etc/danos/build-info.env"],
                stdin=decompressor.stdout, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, timeout=90, check=False,
            )
            if decompressor.stdout:
                decompressor.stdout.close()
            gzip_rc = decompressor.wait(timeout=30)
        except Exception:
            decompressor.kill()
            decompressor.wait()
            raise
        if listing.returncode or gzip_rc or not listing.stdout:
            raise IdentityError("ISO has no readable etc/danos/build-info.env")

    values: dict[str, str] = {}
    for raw in listing.stdout.decode("utf-8", errors="strict").splitlines():
        key, sep, value = raw.partition("=")
        if sep:
            try:
                parsed = shlex.split(value, posix=True)
            except ValueError as exc:
                raise IdentityError(f"malformed shell-escaped build metadata for {key}") from exc
            values[key] = parsed[0] if len(parsed) == 1 else " ".join(parsed)
    commit = values.get("DANOS_BUILD_COMMIT", "")
    dirty = values.get("DANOS_BUILD_SOURCE_DIRTY", "")
    if not re.fullmatch(r"[0-9a-fA-F]{7,40}", commit):
        raise IdentityError("ISO build commit is missing or malformed")
    if dirty not in ("0", "1"):
        raise IdentityError("ISO source dirty marker is missing or malformed")
    result = {"iso_build_commit": commit.lower(), "iso_source_dirty": dirty}
    for key in (
        "DANOS_BUILD_ISO", "DANOS_BUILD_VPP_IMAGE", "DANOS_BUILD_DPDK_PORTS",
        "DANOS_BUILD_DPDK_NO_RX_INTERRUPTS", "DANOS_BUILD_DPDK_BIND_DRIVER",
        "DANOS_BUILD_DPDK_EXPECTED_PCI_ID", "DANOS_BUILD_DPDK_DEVICE",
        "DANOS_BUILD_DPDK_ENABLE", "DANOS_BUILD_DPDK_AUTOSTART",
        "DANOS_BUILD_PING_ENABLE", "DANOS_BUILD_DPDK_TRAFFIC_TEST",
        "DANOS_BUILD_DPDK_PORT_COUNT", "DANOS_BUILD_ECMP_SOAK_COUNT",
        "DANOS_BUILD_ECMP_SOAK_INTERVAL", "DANOS_BUILD_TRAFFIC_READY_ENDPOINT",
        "DANOS_BUILD_PEER_MAC1", "DANOS_BUILD_PEER_MAC2", "DANOS_BUILD_PEER_IP1",
        "DANOS_BUILD_PEER_IP2", "DANOS_BUILD_IF1_ADDR", "DANOS_BUILD_IF2_ADDR",
        "DANOS_BUILD_STATIC_NEIGHBORS",
    ):
        if key in values:
            result[key.lower()] = values[key]
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    args = parser.parse_args()
    try:
        values = read_identity(args.iso)
    except (IdentityError, OSError, subprocess.SubprocessError, UnicodeError) as exc:
        print(f"[SKIP] {exc}", file=sys.stderr)
        return 2
    for key, value in values.items():
        print(f"{key}={shlex.quote(value)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
