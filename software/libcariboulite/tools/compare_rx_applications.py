#!/usr/bin/env python3
"""Compare RX-only menu 14 with the backend and interactive passive monitor.

Uses real radio hardware sequentially, never starts TX, and records no IQ.
The existing audio bridges remain in place. Run only after stopping radio apps.
The original menu layout exposes IQ FIFO counters; detailed audio and worker
measurements are unavailable there, and remain fully checked for the backend.
"""
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
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import time

sys.dont_write_bytecode = True
from rx_comparison_screen import TerminalScreen, IncompleteScreenError, parse_test_app_screen
from cariboulite_radio_monitor import parse_snapshot

ROOT = Path(__file__).resolve().parents[3]
TICKS = os.sysconf("SC_CLK_TCK")
ANSI = re.compile(rb"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\)|[()][A-Z0-9]|[=>])")


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def process_stat(path):
    value = path.read_text()
    fields = value[value.rfind(")") + 2:].split()
    return dict(name=value[value.find("(") + 1:value.rfind(")")],
                ticks=int(fields[11]) + int(fields[12]), start_ticks=int(fields[19]),
                last_cpu=int(fields[36]), rt_priority=int(fields[37]), policy=int(fields[38]))


def process_snapshot(pid, memory_details=False):
    directory = Path(f"/proc/{pid}")
    result = process_stat(directory / "stat")
    result.update(pid=pid, threads={})
    for task in (directory / "task").iterdir():
        try:
            row = process_stat(task / "stat")
            for line in (task / "status").read_text().splitlines():
                key, _, value = line.partition(":")
                if key == "Cpus_allowed_list": row["affinity"] = value.strip()
                if key in ("voluntary_ctxt_switches", "nonvoluntary_ctxt_switches"):
                    row[key] = int(value)
            result["threads"][task.name] = row
        except FileNotFoundError:
            continue
    for line in (directory / "status").read_text().splitlines():
        key, _, value = line.partition(":")
        if key in ("VmRSS", "VmHWM"): result[key + "_kib"] = int(value.split()[0])
    if memory_details:
        result.update(Pss_kib=None, Uss_kib=None)
        try:
            memory = {}
            for line in (directory / "smaps_rollup").read_text().splitlines():
                key, _, value = line.partition(":")
                if key in ("Pss", "Private_Clean", "Private_Dirty"):
                    memory[key] = int(value.split()[0])
            result["Pss_kib"] = memory.get("Pss")
            if "Private_Clean" in memory and "Private_Dirty" in memory:
                result["Uss_kib"] = memory["Private_Clean"] + memory["Private_Dirty"]
        except (OSError, ValueError): pass
    return result


def host_snapshot():
    values = list(map(int, Path("/proc/stat").read_text().splitlines()[0].split()[1:9]))
    result = dict(monotonic_s=time.monotonic(), cpu_total=sum(values), cpu_idle=values[3] + values[4])
    for key, path in (("temperature_c", "/sys/class/thermal/thermal_zone0/temp"),
                      ("cpu_frequency_mhz", "/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq")):
        try: result[key] = int(Path(path).read_text()) / 1000
        except (OSError, ValueError): result[key] = None
    return result


def throttling():
    try:
        result = subprocess.run(["vcgencmd", "get_throttled"], capture_output=True, text=True, timeout=3)
        return result.stdout.strip() if result.returncode == 0 else "unavailable"
    except (OSError, subprocess.TimeoutExpired): return "unavailable"


class TerminalProcess:
    def __init__(self, command, path, rows, columns):
        self.command = command
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
        self.screen = TerminalScreen(rows=rows, columns=columns)
        self.log = path.open("xb", buffering=0)
        self.recent, self.bytes, self.eof = b"", 0, False
        self.emulate = True
        self.chunks = []
        environment = dict(os.environ, TERM="xterm", PYTHONDONTWRITEBYTECODE="1")
        # Comparison must not inherit a previous raw-IQ capture hook.
        environment.pop("LD_PRELOAD", None)
        try:
            self.process = subprocess.Popen(command, cwd=ROOT, env=environment,
                stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
        except BaseException:
            os.close(self.master); self.log.close(); raise
        finally: os.close(slave)

    def read(self):
        if self.eof: return
        try: data = os.read(self.master, 65536)
        except OSError as error:
            if error.errno != errno.EIO: raise
            data = b""
        if not data: self.eof = True; return
        read_at = time.monotonic()
        self.log.write(data)
        self.chunks.append(dict(monotonic_s=read_at, offset=self.bytes, length=len(data)))
        self.bytes += len(data)
        if self.emulate:
            self.recent = (self.recent + data)[-131072:]
            self.screen.feed(data)

    def pump(self, seconds=.1):
        if not self.eof and select.select([self.master], [], [], seconds)[0]: self.read()

    def send(self, value):
        self.recent = b""
        os.write(self.master, value.encode())

    def expect(self, value, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if value.encode() in ANSI.sub(b"", self.recent): return
            if self.process.poll() is not None: raise RuntimeError(f"Application exited waiting for {value!r}")
            self.pump(.1)
        raise TimeoutError(f"Application did not display {value!r}")

    def wait_exit(self, timeout=12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and (self.process.poll() is None or not self.eof):
            self.pump(.1)
        if self.process.poll() is None: raise TimeoutError("Application did not stop gracefully")
        if self.process.returncode: raise RuntimeError(f"Application exit status {self.process.returncode}")

    def dispose(self):
        if self.process.poll() is None:
            if Path(self.command[0]).name == "cariboulite_test_app":
                try:
                    if "Choice:" in self.screen.text(): self.send("99\n")
                    else:
                        self.send("\x1bQ"); self.expect("Choice:", 12); self.send("99\n")
                    self.wait_exit()
                except (OSError, RuntimeError, TimeoutError): pass
        if self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try: self.wait_exit(5)
            except (RuntimeError, TimeoutError):
                if self.process.poll() is None:
                    os.killpg(self.process.pid, signal.SIGKILL)
                    self.process.wait(timeout=3)
                    raise RuntimeError("Forced termination; verify radio idle before another trial")
        os.close(self.master); self.log.close()


def assert_radio_idle():
    for directory in Path("/proc").iterdir():
        if not directory.name.isdigit(): continue
        try: arguments = (directory / "cmdline").read_bytes().split(b"\0")
        except (OSError, PermissionError): continue
        if arguments and Path(os.fsdecode(arguments[0])).name in (
                "cariboulite_test_app", "cariboulite_radio_backend", "cariboulite_rx_backend"):
            raise RuntimeError(f"Radio owner PID {directory.name} already running; it was left untouched")


def check_settings(record, args, health=True):
    config, rx = record["config"], record["rx"]
    observations = record.get("terminal_observations")
    baseline_menu = observations is not None and rx.get("instrumentation_available") is False
    if record["channel"] != "RF24" or config["sample_rate_hz"] != args.sample_rate:
        raise RuntimeError("Unexpected radio channel/sample rate")
    frequency = config.get("rx_frequency_hz", config.get("requested_frequency_hz"))
    if frequency is None or abs(frequency - args.frequency) > 10 or "NBFM" not in record["rx_mode"]:
        raise RuntimeError("Unexpected RX frequency/mode")
    if not rx["running"] or not rx["noise_enabled"] or rx["carrier_enabled"]:
        raise RuntimeError("Unexpected RX or squelch state")
    if not baseline_menu and not rx["playback"]["valid"]:
        raise RuntimeError("Playback instrumentation unavailable")
    noise = rx["noise"]
    if abs(noise["open_rms"]-.20) > .001 or abs(noise["close_rms"]-.30) > .001:
        raise RuntimeError("Unexpected noise squelch thresholds")
    registers = observations["registers"] if observations else record["hardware"]["rf24"]["registers"]
    expected_rx_dfe = {1000000:4, 2000000:2, 4000000:1}[args.sample_rate]
    if (registers["STATE"] & 7 != 5 or registers["RXBWC"] & 0x3f != 0x10
        or registers["RXDFE"] & 0xef != expected_rx_dfe or registers["AGCC"] & 0x73 != 1
        or registers["AGCS"] & 0xe0 != 0):
        raise RuntimeError("Unexpected modem RX state/filter/AGC")
    if observations and (observations["fpga_rx_source"] != 1 or observations["smi_direction"] != 1):
        raise RuntimeError("Unexpected FPGA RX source/direction")
    if health:
        if record.get("state", "RX") != "RX": raise RuntimeError("Backend reported a receive fault")
        if not baseline_menu and any(not w["created"] or not w["alive"] for w in rx["workers"].values()):
            raise RuntimeError("A receive worker exited during comparison")


def process_delta(first, last, seconds):
    if first["start_ticks"] != last["start_ticks"]: raise RuntimeError("Process changed during measurement")
    result = dict(pid=last["pid"], cpu_percent_one_core=(last["ticks"]-first["ticks"])/TICKS/seconds*100,
        rss_mib=last.get("VmRSS_kib", 0)/1024, peak_rss_mib=last.get("VmHWM_kib", 0)/1024,
        pss_mib=last["Pss_kib"]/1024 if last.get("Pss_kib") is not None else None,
        uss_mib=last["Uss_kib"]/1024 if last.get("Uss_kib") is not None else None,
        threads=[])
    for tid, end in last["threads"].items():
        start = first["threads"].get(tid)
        if not start or start["start_ticks"] != end["start_ticks"]: continue
        row = dict(tid=int(tid), name=end["name"], cpu_percent_one_core=(end["ticks"]-start["ticks"])/TICKS/seconds*100,
            policy=end["policy"], rt_priority=end["rt_priority"], affinity=end.get("affinity"), last_cpu=end["last_cpu"])
        for key in ("voluntary_ctxt_switches", "nonvoluntary_ctxt_switches"):
            row[key] = end.get(key, 0)-start.get(key, 0)
        result["threads"].append(row)
    return result


def summarize_records(records):
    first, last = records[0], records[-1]
    seconds = last["monotonic_s"]-first["monotonic_s"]
    result = dict(snapshots=len(records), snapshot_interval_s=seconds, settings=last["config"],
                  workers=last["rx"]["workers"], gate_open_snapshots=sum(bool(r["rx"]["gate_open"]) for r in records))
    result["workers"] = {name: ({"valid": False} if any(
        r["rx"]["workers"][name].get("valid") is False for r in records) else worker)
        for name, worker in last["rx"]["workers"].items()}
    for section, keys in (
        ("queue", ("puts", "gets", "drops", "timeouts_put", "timeouts_get")),
        ("audio_queue", ("puts", "gets", "drops", "timeouts_put", "timeouts_get", "shutdown_puts", "shutdown_gets")),
        ("playback", ("write_calls", "frames_requested", "frames_written", "partial_writes", "would_block", "zero_writes", "underruns", "recovery_attempts", "recovery_successes", "recovery_failures", "write_errors")),
        ("reader", ("calls", "samples", "short_reads", "zero_reads", "errors"))):
        begin, end = first["rx"][section], last["rx"][section]
        if any(r["rx"][section].get("valid") is False for r in records):
            result[section] = {"valid": False}
            continue
        delta = {key:end[key]-begin[key] for key in keys}
        if any(value < 0 for value in delta.values()): raise RuntimeError("Counters reset during measurement")
        result[section] = dict(delta=delta, first=begin, last=end)
        if "depth" in end:
            depths = [r["rx"][section]["depth"] for r in records]
            result[section].update(observed_min=min(depths), observed_max=max(depths),
                puts_per_s=delta["puts"]/seconds, gets_per_s=delta["gets"]/seconds)
    result["states"] = sorted({r.get("state", "RX") for r in records})
    return result


def reconstruct_terminal(path, chunks, rows, columns, start, stop, args=None):
    """Decode already-drained logs after CPU sampling and radio cleanup.

    Pre-window chunks establish the canvas and last progress count, but cannot
    become measured snapshots. Each measured snapshot must contain a fresh
    reader count, or IQ FIFO put count for the original layout, and uses its
    PTY read timestamp. Menu 14 still describes
    adjacent instants, rather than an atomic pipeline transaction.
    """
    screen = TerminalScreen(rows=rows, columns=columns)
    records, screenshots = [], []
    last_progress = None
    next_screen = start
    with path.open("rb") as log:
        for chunk in chunks:
            stamp = chunk["monotonic_s"]
            if stamp > stop: break
            log.seek(chunk["offset"])
            data = log.read(chunk["length"])
            if len(data) != chunk["length"]:
                raise RuntimeError("Truncated terminal capture")
            screen.feed(data)
            text = screen.text()
            if start <= stamp and stamp >= next_screen:
                screenshots.append(dict(monotonic_s=stamp, text=text))
                next_screen = stamp + 1
            if args is None: continue
            try: record = parse_test_app_screen(text)
            except IncompleteScreenError: continue
            rx = record["rx"]
            progress = (("queue.puts", rx["queue"]["puts"])
                        if rx["instrumentation_available"] is False
                        else ("reader.calls", rx["reader"]["calls"]))
            fresh = last_progress is None or progress != last_progress
            last_progress = progress
            if stamp < start or not fresh: continue
            check_settings(record, args)
            record["monotonic_s"] = stamp
            records.append(record)
    return records, screenshots


def trial(kind, index, args, output):
    assert_radio_idle()
    directory = output / f"trial-{index:02d}-{kind}"
    directory.mkdir()
    owner = monitor = connection = None
    terms = []
    report = dict(kind=kind, status="preparing", started_local=datetime.now().astimezone().isoformat())
    records, host_rows, process_rows, screen_rows = [], [], [], []
    try:
        with tempfile.TemporaryDirectory(prefix="cl-rx-compare-") as socket_dir:
            if kind == "menu14":
                owner = TerminalProcess([str(ROOT / "build/cariboulite_test_app")], directory/"owner.ansi.log", args.rows, args.columns)
                terms.append(owner)
                owner.expect("Choice:", 45); owner.send("14\n"); owner.expect("[G] RX")
                owner.send("G"); owner.expect("RX MHz ["); owner.send(f"{args.frequency/1e6:.6f}\n"); owner.expect("Frequency saved")
                owner.send(str(args.sample_rate//1000000)); owner.expect("TX/RX rate selected")
                owner.send("R"); owner.expect("RX running at the saved RX frequency.")
            else:
                socket_path = str(Path(socket_dir)/"status.sock")
                owner = TerminalProcess([str(ROOT/"build/radio-backend/cariboulite_radio_backend"),
                    "--channel", "RF24", "--rx-start", "--rx-frequency", str(args.frequency), "--rx-sample-rate", str(args.sample_rate),
                    "--rx-mode", "nbfm", "--status-hz", "5", "--socket", socket_path], directory/"owner.ansi.log", args.rows, args.columns)
                terms.append(owner)
                owner.expect("RX started", 45)
                monitor = TerminalProcess([str(ROOT/"build/radio-backend/cariboulite_radio_monitor"), "--socket", socket_path],
                    directory/"monitor.ansi.log", args.rows, args.columns)
                terms.append(monitor)
                connection = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
                connection.connect(socket_path); connection.setblocking(False)

            phase_started = time.monotonic()
            last_record = None
            measured = False
            before = cpu_before = elapsed = None
            before_bytes = {}
            last_parsed_bytes = -1
            next_sample = phase_started
            last_progress = 0
            while True:
                now = time.monotonic()
                if measured and now-phase_started >= args.duration: break
                if not measured and now-phase_started >= args.warmup:
                    if last_record is None: raise RuntimeError("No complete receiver snapshot during warmup")
                    check_settings(last_record, args)
                    # Drain and timestamp raw PTY chunks during CPU sampling;
                    # terminal emulation and regex decoding happen offline.
                    for term in terms: term.emulate = False
                    before = {"owner":process_snapshot(owner.process.pid, memory_details=True)}
                    if monitor: before["monitor"] = process_snapshot(monitor.process.pid, memory_details=True)
                    before["profiler"] = process_snapshot(os.getpid(), memory_details=True)
                    cpu_before = host_snapshot()
                    before_bytes = {term.process.pid:term.bytes for term in terms}
                    phase_started = next_sample = time.monotonic()
                    measured = True
                    print(f"trial {index} {kind}: measuring {args.duration:g}s", flush=True)
                fds = [term.master for term in terms if not term.eof]
                if connection: fds.append(connection)
                ready, _, _ = select.select(fds, [], [], .05)
                for term in terms:
                    if term.master in ready: term.read()
                    if term.process.poll() is not None: raise RuntimeError("Application exited during comparison")
                if connection and connection in ready:
                    for _ in range(64):
                        try: packet, _, flags, _ = connection.recvmsg(16384)
                        except BlockingIOError: break
                        if not packet: raise RuntimeError("Backend disconnected during comparison")
                        record = parse_snapshot(packet, bool(flags & socket.MSG_TRUNC))
                        if record["owner_pid"] != owner.process.pid or record["source"]["kind"] != "live":
                            raise RuntimeError("Wrong status source")
                        check_settings(record, args, health=measured)
                        last_record = record
                        if measured and record["monotonic_s"] >= cpu_before["monotonic_s"]:
                            records.append(record)
                now = time.monotonic()
                if kind == "menu14" and not measured and owner.bytes != last_parsed_bytes:
                    last_parsed_bytes = owner.bytes
                    try: record = parse_test_app_screen(owner.screen.text())
                    except IncompleteScreenError: record = None
                    if record:
                        check_settings(record, args)
                        record["monotonic_s"] = now
                        last_record = record
                if measured and now >= next_sample:
                    host_rows.append(host_snapshot())
                    row = {"monotonic_s":now, "owner":process_snapshot(owner.process.pid)}
                    if monitor: row["monitor"] = process_snapshot(monitor.process.pid)
                    process_rows.append(row)
                    next_sample = now+1
                if now-last_progress >= 20:
                    print(f"trial {index} {kind}: {'measurement' if measured else 'warmup'} {now-phase_started:.1f}s", flush=True)
                    last_progress = now
            after = {name:process_snapshot(data["pid"], memory_details=True) for name, data in before.items()}
            cpu_after = host_snapshot()
            elapsed = cpu_after["monotonic_s"]-cpu_before["monotonic_s"]
            for term in terms: term.emulate = True
            report.update(status="cpu_measured", duration_s=elapsed, warmup_s=args.warmup,
                measurement_start_monotonic_s=cpu_before["monotonic_s"],
                measurement_stop_monotonic_s=cpu_after["monotonic_s"],
                processes={name:process_delta(data, after[name], elapsed) for name,data in before.items()},
                host_samples=host_rows,
                host_busy_percent=100*(1-(cpu_after["cpu_idle"]-cpu_before["cpu_idle"])/(cpu_after["cpu_total"]-cpu_before["cpu_total"])),
                terminal_bytes={str(term.process.pid):term.bytes-before_bytes[term.process.pid] for term in terms},
                throttling=throttling())
            report["combined_cpu_percent_one_core"] = sum(data["cpu_percent_one_core"] for name,data in report["processes"].items() if name != "profiler")
            report["combined_cpu_percent_machine_capacity"] = report["combined_cpu_percent_one_core"]/os.cpu_count()
            applications = [data for name, data in report["processes"].items() if name != "profiler"]
            for key in ("pss_mib", "uss_mib"):
                report["combined_" + key] = sum(data[key] for data in applications) if all(data[key] is not None for data in applications) else None
            if kind == "menu14":
                owner.send("R"); owner.expect("audio IDLE"); owner.send("Q"); owner.expect("Choice:"); owner.send("99\n")
            else:
                monitor.send("q"); monitor.wait_exit()
                owner.process.send_signal(signal.SIGINT)
            owner.wait_exit()
            # Both applications have exited before any expensive screen replay.
            for term in terms:
                decoded, screens = reconstruct_terminal(Path(term.log.name), term.chunks,
                    args.rows, args.columns, cpu_before["monotonic_s"], cpu_after["monotonic_s"],
                    args if kind == "menu14" and term is owner else None)
                if kind == "menu14" and term is owner: records = decoded
                screen_rows.extend(dict(terminal="owner" if term is owner else "monitor", **row)
                                   for row in screens)
            records = [r for r in records if cpu_before["monotonic_s"] <= r["monotonic_s"] <= cpu_after["monotonic_s"]]
            if len(records) < args.duration*3: raise RuntimeError("Too few complete steady-state snapshots")
            report["receiver"] = summarize_records(records)
            report["status"] = "complete"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        if connection: connection.close()
        cleanup_error = None
        for term in reversed(terms):
            term.emulate = True
            try: term.dispose()
            except BaseException as error: cleanup_error = error
        if cleanup_error:
            report.update(status="failed", cleanup_error=repr(cleanup_error))
        write_json(directory/"report.json", report)
        for name, rows in (("snapshots.jsonl", records), ("process-samples.jsonl", process_rows), ("screens.jsonl", screen_rows)):
            (directory/name).write_text("".join(json.dumps(row)+"\n" for row in rows))
        for term in terms:
            role = "owner" if term is owner else "monitor"
            (directory/f"{role}.chunks.jsonl").write_text("".join(json.dumps(row)+"\n" for row in term.chunks))
        if cleanup_error: raise cleanup_error
    print(f"trial {index} {kind}: complete, {report['combined_cpu_percent_one_core']:.2f}% of one core", flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--warmup", type=float, default=30)
    parser.add_argument("--duration", type=float, default=60)
    parser.add_argument("--order", default="menu14,backend,backend,menu14")
    parser.add_argument("--frequency", type=int, default=430125000)
    parser.add_argument("--sample-rate", type=int, choices=(1000000,2000000,4000000), default=1000000)
    parser.add_argument("--rows", type=int, default=120)
    parser.add_argument("--columns", type=int, default=240)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    order = args.order.split(",")
    if any(kind not in ("menu14", "backend") for kind in order): parser.error("Order must list menu14 and/or backend")
    if not 1 <= args.warmup <= 600 or not 2 <= args.duration <= 600: parser.error("Invalid measurement duration")
    if args.rows < 100 or args.columns < 180: parser.error("PTY must fit all menu14 instrumentation")
    plan = dict(order=order, warmup_s=args.warmup, measurement_s=args.duration,
        settings=dict(channel="RF24", frequency_hz=args.frequency, sample_rate_hz=args.sample_rate,
            mode="NBFM", playback="plughw:Loopback,0,0", audio_rate_hz=48000, deemphasis_us=50,
            pcm_gain=8000, noise_enabled=True, carrier_enabled=False, noise_open=.20, noise_close=.30),
        terminal=dict(rows=args.rows, columns=args.columns), logical_cpus=os.cpu_count(),
        extra_backend_status_client=True, IQ_capture=False,
        terminal_decoding="offline after CPU snapshots and application exit",
        menu14_diagnostics="IQ FIFO counters; audio/playback/reader/worker measurements unavailable in the original layout",
        note="Live RF is uncontrolled. CPU excludes the measured profiler process; both PTYs are continuously drained and timestamped without terminal emulation during measurement.")
    print(json.dumps(plan, indent=2), flush=True)
    if args.dry_run: return 0
    assert_radio_idle()
    args.output.mkdir(parents=True, exist_ok=False)
    plan["revision"] = subprocess.check_output(["git","rev-parse","HEAD"], cwd=ROOT, text=True).strip()
    plan["throttling_before"] = throttling()
    plan["binaries"] = {str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest() for path in
        (ROOT/"build/cariboulite_test_app",ROOT/"build/radio-backend/cariboulite_radio_backend")}
    write_json(args.output/"plan.json", plan)
    reports = []
    for index, kind in enumerate(order, 1):
        reports.append(trial(kind, index, args, args.output))
        write_json(args.output/"report.json", dict(plan=plan, trials=reports))
    return 0


if __name__ == "__main__":
    try: raise SystemExit(main())
    except KeyboardInterrupt: raise SystemExit(130)
