#!/usr/bin/env python3
"""Verify bidirectional ttyS0 root-shell input on a live ISO under QEMU."""

from __future__ import annotations

import argparse
import os
import re
import select
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--serial-log", type=Path)
    parser.add_argument("--shell-command", default="true",
                        help="single-line shell command to run after ttyS0 is ready")
    parser.add_argument("--expect-output",
                        help="require this command-output substring before passing")
    args = parser.parse_args()
    if not args.shell_command.strip() or "\n" in args.shell_command or "\r" in args.shell_command:
        parser.error("--shell-command must be a non-empty single line")
    if args.expect_output and "\n" in args.expect_output:
        parser.error("--expect-output must be a single line")
    iso = args.iso.resolve()
    if not iso.is_file():
        parser.error(f"ISO not found: {iso}")

    with tempfile.TemporaryDirectory(prefix="danos-serial-shell-") as temp:
        socket_path = str(Path(temp) / "serial.sock")
        accel = (["-accel", "kvm", "-cpu", "host"]
                 if Path("/dev/kvm").exists() and os.access("/dev/kvm", os.R_OK | os.W_OK)
                 else ["-accel", "tcg", "-cpu", "max"])
        command = [
            args.qemu, "-machine", "q35", *accel, "-m", "2048", "-smp", "2",
            "-cdrom", str(iso), "-boot", "order=d", "-display", "none",
            "-netdev", "user,id=mgmt", "-device", "virtio-net-pci,netdev=mgmt",
            "-serial", f"unix:{socket_path},server=on,wait=off", "-monitor", "none",
            "-no-reboot",
        ]
        try:
            process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.PIPE, text=True)
        except FileNotFoundError:
            print(f"[SKIP] QEMU executable not found: {args.qemu}", file=sys.stderr)
            return 2

        conn: socket.socket | None = None
        log = bytearray()
        deadline = time.monotonic() + args.timeout
        command_sent = False
        shell_ready_at: float | None = None
        try:
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    stderr = process.stderr.read() if process.stderr else ""
                    raise RuntimeError(f"QEMU exited early ({process.returncode}): {stderr[-2000:]}")
                if conn is None and Path(socket_path).exists():
                    try:
                        conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                        conn.settimeout(1)
                        conn.connect(socket_path)
                        conn.setblocking(False)
                    except (FileNotFoundError, ConnectionRefusedError, OSError):
                        if conn:
                            conn.close()
                        conn = None
                if conn is None:
                    time.sleep(0.1)
                    continue

                readable, _, _ = select.select([conn], [], [], 0.2)
                if readable:
                    try:
                        chunk = conn.recv(65536)
                    except BlockingIOError:
                        chunk = b""
                    if not chunk:
                        raise RuntimeError("QEMU serial socket closed before shell marker")
                    log.extend(chunk)
                text = log.decode("utf-8", errors="replace")
                if "SERIAL-SHELL-READY ttyS0" in text and not command_sent:
                    # Wait for the background setsid/cttyhack shell to finish
                    # acquiring ttyS0. Send a harmless newline first; BusyBox
                    # ash emits a prompt only after it is ready for input.
                    if shell_ready_at is None:
                        shell_ready_at = time.monotonic()
                        conn.sendall(b"\n")
                    if re.search(rb"(?m)(?:^|\r?\n)[^\r\n]*# ?$", bytes(log)):
                        command = f"{args.shell_command}; echo SERIAL_COMMAND_RESULT_7F3A\n"
                        conn.sendall(command.encode())
                        command_sent = True
                    elif time.monotonic() - shell_ready_at > 8:
                        # Prompt can be suppressed by BusyBox configuration;
                        # still test the shell with a bounded delayed command.
                        command = f"{args.shell_command}; echo SERIAL_COMMAND_RESULT_7F3A\n"
                        conn.sendall(command.encode())
                        command_sent = True
                # The tty echoes the input command. Require the unique result
                # token twice: once in that input echo and once in ash output.
                expected_seen = not args.expect_output or args.expect_output in text
                # Terminal echo may wrap a long command at the column limit,
                # splitting the marker across CR/LF. Count on a newline-free
                # view while still requiring the distinct shell result.
                marker_count = text.replace("\r", "").replace("\n", "").count(
                    "SERIAL_COMMAND_RESULT_7F3A"
                )
                if (command_sent
                        and marker_count >= 2
                        and expected_seen):
                    if args.serial_log:
                        args.serial_log.parent.mkdir(parents=True, exist_ok=True)
                        args.serial_log.write_bytes(log)
                    print("[PASS] QEMU ttyS0 root shell executed the command and emitted its result")
                    return 0

            if args.serial_log:
                args.serial_log.parent.mkdir(parents=True, exist_ok=True)
                args.serial_log.write_bytes(log)
            raise TimeoutError(
                "ttyS0 shell command/result or expected output not observed; serial tail:\n"
                + log.decode("utf-8", errors="replace")[-4000:]
            )
        finally:
            if conn:
                conn.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, TimeoutError, OSError) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1)
