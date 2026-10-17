#!/usr/bin/env python3
"""Drive test_app menu 14 on a PTY and record decoded RX IQ before DSP."""
import argparse
from datetime import datetime
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import pty
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import termios
import time
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[3]
ZONE = ZoneInfo("Europe/Brussels")
ANSI = re.compile(rb"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\)|[()][A-Z0-9]|[=>])")
RADIO_NAMES = {"hif": "HiF/RF24", "s1g": "S1G/RF09"}


def monitor_radio(data):
    matches = re.findall(rb"(S1G/RF09|HiF/RF24) \[T\] TX", ANSI.sub(b"", data))
    if not matches:
        raise RuntimeError("Cannot identify the menu 14 radio channel")
    return "hif" if matches[-1] == b"HiF/RF24" else "s1g"


def timestamp(value):
    result = datetime.fromisoformat(value)
    return result.replace(tzinfo=ZONE) if result.tzinfo is None else result


class TerminalApp:
    def __init__(self, app, hook, iq, log, radio):
        self.log = log
        self.recent = b""
        self.eof = False
        self.check_capture_errors = True
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 120, 160, 0, 0))
        env = os.environ.copy()
        env.update(TERM="xterm", LD_PRELOAD=str(hook), CARIBOULITE_RX_IQ_FILE=str(iq),
                   CARIBOULITE_RX_IQ_CHANNEL=radio)
        try:
            self.proc = subprocess.Popen([str(app)], cwd=ROOT, env=env,
                                         stdin=slave, stdout=slave, stderr=slave,
                                         start_new_session=True)
        finally:
            os.close(slave)

    def pump(self, seconds=0.2):
        if self.eof:
            return
        ready, _, _ = select.select([self.master], [], [], max(0, seconds))
        if not ready:
            return
        try:
            data = os.read(self.master, 65536)
        except OSError as exc:
            if exc.errno != errno.EIO:
                raise
            data = b""
        if not data:
            self.eof = True
            return
        if not self.log.closed:
            self.log.write(data)
            self.log.flush()
        else:
            with Path(self.log.name).open("ab", buffering=0) as tail:
                tail.write(data)
        self.recent = (self.recent + data)[-1048576:]
        if self.check_capture_errors and b"IQ_CAPTURE_ERROR" in self.recent:
            raise RuntimeError("IQ capture hook reported an error; inspect application.log")

    def send(self, keys):
        self.recent = b""
        os.write(self.master, keys.encode())

    def expect(self, text, seconds=20):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if text.encode() in ANSI.sub(b"", self.recent):
                return
            self.pump(min(0.2, max(0, deadline - time.monotonic())))
            if self.proc.poll() is not None or self.eof:
                raise RuntimeError(f"test_app exited while waiting for {text!r}")
        raise TimeoutError(f"test_app did not display {text!r} within {seconds}s")

    def wait_until(self, target, event):
        last_report = 0.0
        while True:
            remaining = target.timestamp() - time.time()
            if remaining <= 0:
                return
            if self.proc.poll() is not None or self.eof:
                raise RuntimeError("test_app exited before the scheduled action")
            if time.monotonic() - last_report >= 30:
                event("waiting", action_at=target.isoformat(), seconds_remaining=round(remaining, 1))
                last_report = time.monotonic()
            self.pump(min(0.2, remaining))

    def close(self):
        self.check_capture_errors = False
        if self.proc.poll() is None:
            # Q stops either stream and returns to the main menu.
            at_main_menu = b"Choice:" in ANSI.sub(b"", self.recent)
            self.send("99\n" if at_main_menu else "\x1bQ")
            try:
                if not at_main_menu:
                    self.expect("Choice:", 12)
                    self.send("99\n")
                deadline = time.monotonic() + 12
                while self.proc.poll() is None and time.monotonic() < deadline:
                    self.pump(0.2)
            except (RuntimeError, TimeoutError, OSError, ValueError):
                pass
        if self.proc.poll() is None:
            os.killpg(self.proc.pid, signal.SIGTERM)
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.proc.pid, signal.SIGKILL)
                self.proc.wait()
                raise RuntimeError("Forced test_app termination; verify the radio is idle")
        # Drain destructor output including IQ_CAPTURE_COMPLETE.
        while not self.eof:
            self.pump(0.1)
        os.close(self.master)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--start", required=True, help="ISO local date/time; default zone Europe/Brussels")
    parser.add_argument("--stop", required=True, help="ISO local date/time, later than --start")
    parser.add_argument("--frequency-mhz", type=float, default=430.125)
    parser.add_argument("--radio", choices=tuple(RADIO_NAMES), default="hif",
                        help="Expected menu 14 channel; this does not change the app channel")
    parser.add_argument("--sample-rate", type=int, choices=(1000000, 2000000, 4000000), default=1000000)
    parser.add_argument("--app", type=Path, default=ROOT / "build/cariboulite_test_app")
    parser.add_argument("--output", type=Path, help="New output directory; existing directories are refused")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        start, stop = timestamp(args.start), timestamp(args.stop)
    except ValueError as exc:
        parser.error(str(exc))
    if stop <= start:
        parser.error("--stop must be later than --start")
    if args.radio == "hif":
        if not 1 <= args.frequency_mhz < 6000:
            parser.error("HiF frequency must be at least 1 MHz and below 6000 MHz")
    elif not (377 <= args.frequency_mhz <= 530 or 779 <= args.frequency_mhz <= 1020):
        parser.error("RF09 frequency must be in 377–530 or 779–1020 MHz")
    output = (args.output or ROOT / "build/iq-captures" /
              start.strftime("%Y%m%dT%H%M%S%z")).resolve()
    iq = output / "rx_iq.cs16"
    expected_bytes = round((stop - start).total_seconds() * args.sample_rate * 4)
    plan = {"scheduled_start": start.isoformat(), "scheduled_stop": stop.isoformat(),
            "timezone": "Europe/Brussels", "frequency_hz": round(args.frequency_mhz * 1000000),
            "radio": args.radio, "channel": RADIO_NAMES[args.radio],
            "sample_rate": args.sample_rate, "iq_file": str(iq),
            "nominal_bytes": expected_bytes,
            "format": "little-endian interleaved signed int16 I,Q; native 13-bit amplitudes",
            "capture_point": "cariboulite_radio_read_samples, before software decimation and squelch"}
    print(json.dumps(plan, indent=2), flush=True)
    if args.dry_run:
        return 0
    if start.timestamp() <= time.time():
        parser.error("The start time has already passed; specify a future window")
    args.app = args.app.resolve()
    if not args.app.is_file() or not os.access(args.app, os.X_OK):
        parser.error("Build cariboulite_test_app first")
    output.mkdir(parents=True, exist_ok=False)
    metadata = dict(plan, status="preparing", events=[])
    metadata["revision"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    metadata["app_sha256"] = hashlib.sha256(args.app.read_bytes()).hexdigest()

    def event(kind, **fields):
        row = {"event": kind, "local_time": datetime.now(ZONE).isoformat(), **fields}
        metadata["events"].append(row)
        (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
        print(json.dumps(row), flush=True)

    hook = ROOT / "build/menu14_iq_capture.so"
    capture_source = Path(__file__).with_name("menu14_iq_capture.c")
    terminal = None
    result = 1
    try:
        subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared",
                        "-I" + str(ROOT / "software/libcariboulite/src"), str(capture_source),
                        "-o", str(hook), "-ldl", "-pthread"], check=True)
        metadata["hook_sha256"] = hashlib.sha256(hook.read_bytes()).hexdigest()
        if shutil.disk_usage(output).free < expected_bytes + 64 * 1024 * 1024:
            raise RuntimeError("Insufficient free disk space for the scheduled recording")
        with (output / "application.log").open("xb", buffering=0) as log:
            terminal = TerminalApp(args.app, hook, iq, log, args.radio)
            event("app_started", pid=terminal.proc.pid)
            terminal.expect("Choice:", 45)
            terminal.send("14\n")
            terminal.expect("[G] RX", 30)
            observed_radio = monitor_radio(terminal.recent)
            metadata["observed_radio"] = observed_radio
            if observed_radio != args.radio:
                raise RuntimeError(f"Menu 14 uses {RADIO_NAMES[observed_radio]}; "
                                   f"expected {RADIO_NAMES[args.radio]}")
            terminal.send("G")
            terminal.expect("RX MHz [")
            terminal.send(f"{args.frequency_mhz:.6f}\n")
            terminal.expect("Frequency saved")
            terminal.send(str(args.sample_rate // 1000000))
            terminal.expect("TX/RX rate selected", 30)
            # The prompt renders the saved frequency in full despite incremental ncurses updates.
            terminal.send("G")
            terminal.expect(f"RX MHz [{args.frequency_mhz:.6f}]")
            terminal.send("\x1b")
            terminal.expect("Unchanged:")
            if start.timestamp() <= time.time():
                raise RuntimeError("Setup missed the scheduled start; RX was not started")
            metadata["status"] = "armed"
            event("configured", frequency_mhz=args.frequency_mhz, sample_rate=args.sample_rate,
                  radio=observed_radio, channel=RADIO_NAMES[observed_radio])
            terminal.wait_until(start, event)
            terminal.send("R")
            event("rx_start_key_sent")
            terminal.expect("RX running at the saved RX frequency.", min(20, (stop - start).total_seconds()))
            metadata["status"] = "recording"
            event("rx_started")
            terminal.wait_until(stop, event)
            terminal.send("R")
            event("rx_stop_key_sent")
            # Q provides graceful stop/cleanup even if the stop status update is fragmented.
            terminal.pump(0.5)
            terminal.close()
            metadata["app_returncode"] = terminal.proc.returncode
            terminal = None
        data = (output / "application.log").read_bytes()
        if b"IQ_CAPTURE_ERROR" in data or not re.search(
                rb"IQ_CAPTURE_COMPLETE samples=\d+ bytes=\d+ failed=0", data):
            raise RuntimeError("Capture did not finish cleanly; inspect application.log")
        captured = re.findall(rb"IQ_CAPTURE_CHANNEL radio=(hif|s1g) channel=(HiF/RF24|S1G/RF09)", data)
        expected_channel = (args.radio.encode(), RADIO_NAMES[args.radio].encode())
        if captured != [expected_channel]:
            raise RuntimeError("Captured radio channel does not match the requested menu channel")
        metadata.update(captured_radio=captured[0][0].decode(), captured_channel=captured[0][1].decode())
        size = iq.stat().st_size
        if not size or size % 4:
            raise RuntimeError("IQ file is empty or contains an incomplete I/Q pair")
        if metadata["app_returncode"] != 0:
            raise RuntimeError("test_app exited with an error")
        metadata.update(status="complete", bytes=size, iq_pairs=size // 4,
                        recorded_sample_seconds=size / (4 * args.sample_rate),
                        smi_read_timeout_messages=data.count(b"SMI reading operation returned timeout"),
                        continuity_verified=False)
        event("complete", bytes=size, iq_pairs=size // 4)
        result = 0
    except (OSError, RuntimeError, TimeoutError, subprocess.SubprocessError, KeyboardInterrupt) as exc:
        metadata.update(status="failed", error=str(exc) or "Interrupted")
        if terminal is not None:
            try:
                terminal.close()
            except (OSError, RuntimeError) as cleanup:
                metadata["cleanup_error"] = str(cleanup)
        if iq.exists():
            metadata["partial_bytes"] = iq.stat().st_size
        event("failed", error=metadata["error"])
    return result


if __name__ == "__main__":
    sys.exit(main())
