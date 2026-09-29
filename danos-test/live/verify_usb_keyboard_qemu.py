#!/usr/bin/env python3
"""Boot a DANOS live ISO and prove QEMU USB keyboard input reaches tty1."""

import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


KEYS = [
    "e", "c", "h", "o", "spc", "u", "s", "b", "k", "e", "y", "p",
    "a", "s", "s", "shift-dot", "slash", "d", "e", "v", "slash", "t",
    "t", "y", "shift-s", "0", "ret",
]


def monitor_send(monitor_path: str, command: str) -> None:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as conn:
        conn.settimeout(5)
        conn.connect(monitor_path)
        try:
            conn.recv(4096)  # HMP greeting and prompt
        except socket.timeout:
            pass
        conn.sendall((command + "\n").encode())


def wait_for_log(log_path: Path, process: subprocess.Popen, wanted: str,
                 timeout: float) -> str:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        if log_path.exists():
            last = log_path.read_text(errors="replace")
            if wanted in last:
                return last
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited early with status {process.returncode}")
        time.sleep(0.25)
    raise TimeoutError(f"timed out waiting for {wanted!r}; serial tail:\n{last[-3000:]}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path,
                        help="bootable DANOS live ISO to verify")
    parser.add_argument("--serial-log", type=Path,
                        help="serial log path (default: build/i211-usb-keyboard-qemu.serial.log)")
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    parser.add_argument("--boot-timeout", type=float, default=90)
    # Allow the live init script to finish opening the controlling tty and
    # leave its shell prompt ready before injecting a keyboard sequence.
    parser.add_argument("--input-settle", type=float, default=10)
    parser.add_argument("--input-timeout", type=float, default=15)
    args = parser.parse_args()

    iso = args.iso.resolve()
    if not iso.is_file():
        parser.error(f"ISO not found: {iso}")
    serial_log = args.serial_log or (Path.cwd() / "build/i211-usb-keyboard-qemu.serial.log")
    serial_log = serial_log.resolve()
    serial_log.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="danos-kbd-") as temp_dir:
        monitor_path = str(Path(temp_dir) / "monitor.sock")
        accel = (["-accel", "kvm", "-cpu", "host"]
                 if Path("/dev/kvm").exists() and os.access("/dev/kvm", os.R_OK | os.W_OK)
                 else ["-accel", "tcg", "-cpu", "max"])
        command = [
            args.qemu, "-machine", "q35", *accel, "-m", "2048", "-smp", "2",
            "-cdrom", str(iso), "-boot", "order=d", "-display", "none",
            "-serial", f"file:{serial_log}",
            "-monitor", f"unix:{monitor_path},server=on,wait=off",
            "-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,bus=xhci.0",
            "-no-reboot",
        ]
        try:
            process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.PIPE, text=True)
        except FileNotFoundError:
            print(f"[SKIP] QEMU executable not found: {args.qemu}", file=sys.stderr)
            return 2

        try:
            wait_for_log(serial_log, process, "Run /init as init process",
                         args.boot_timeout)
            log = wait_for_log(serial_log, process, "hid-generic", args.boot_timeout)
            if "USB HID v1.11 Keyboard" not in log and "USB Keyboard" not in log:
                raise RuntimeError("USB HID keyboard did not enumerate in serial log")
            print("[PASS] QEMU xHCI enumerated a USB HID keyboard")
            wait_for_log(serial_log, process, "USB-KEYBOARD detected", args.boot_timeout)
            wait_for_log(serial_log, process, "LIVE-SHELL-READY", args.boot_timeout)
            time.sleep(args.input_settle)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as conn:
                conn.settimeout(5)
                conn.connect(monitor_path)
                try:
                    conn.recv(4096)
                except socket.timeout:
                    pass
                for key in KEYS:
                    conn.sendall((f"sendkey {key}\n").encode())
                    time.sleep(0.15)
            wait_for_log(serial_log, process, "usbkeypass", args.input_timeout)
            print("[PASS] USB keyboard command reached tty1 shell; serial marker usbkeypass observed")
            print(f"[INFO] serial_log={serial_log}")
            return 0
        except (OSError, RuntimeError, TimeoutError) as exc:
            print(f"[FAIL] {exc}", file=sys.stderr)
            if process.poll() is not None and process.stderr:
                stderr = process.stderr.read()
                if stderr:
                    print(stderr[-2000:], file=sys.stderr)
            return 1
        finally:
            if process.poll() is None:
                try:
                    monitor_send(monitor_path, "quit")
                    process.wait(timeout=5)
                except (OSError, subprocess.TimeoutExpired):
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
