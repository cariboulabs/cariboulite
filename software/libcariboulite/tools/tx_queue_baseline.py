#!/usr/bin/env python3
"""Timed dummy-load TX measurements using existing binaries, without rebuilding.

Owns one backend and a persistent C controller. Real TX is the default; --dry-run
exercises the same command lifecycle without hardware or invented RF counters.
All sockets are private and every process stopped by this tool was started by it.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import select
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
from cariboulite_radio_monitor import MAX_PACKET_BYTES, parse_snapshot

ROOT = Path(__file__).resolve().parents[3]
BACKEND = ROOT / "build/radio-backend/cariboulite_radio_backend"
CONTROLLER = BACKEND.with_name("cariboulite_radio_controller")
TICKS = os.sysconf("SC_CLK_TCK")


def unavailable(error):
    return {"available": False, "error": str(error)}


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def read_text(path):
    try:
        return Path(path).read_text()
    except OSError as error:
        return unavailable(error)


def numeric_file(path, divisor=1):
    try:
        return float(Path(path).read_text()) / divisor
    except (OSError, ValueError) as error:
        return unavailable(error)


def key_numbers(path):
    try:
        return {key: int(value.strip()) for key, value in
                (line.split(":", 1) for line in Path(path).read_text().splitlines())}
    except (OSError, ValueError) as error:
        return unavailable(error)


def process_stat(directory):
    try:
        value = (directory / "stat").read_text()
        fields = value[value.rfind(")") + 2:].split()
        result = dict(available=True, name=value[value.find("(") + 1:value.rfind(")")],
                      ticks=int(fields[11]) + int(fields[12]), utime_ticks=int(fields[11]),
                      stime_ticks=int(fields[12]), start_ticks=int(fields[19]),
                      last_cpu=int(fields[36]), rt_priority=int(fields[37]),
                      policy=int(fields[38]))
    except (OSError, ValueError, IndexError) as error:
        return unavailable(error)
    status = read_text(directory / "status")
    if isinstance(status, str):
        for line in status.splitlines():
            key, _, value = line.partition(":")
            if key == "Cpus_allowed_list":
                result["affinity"] = value.strip()
            elif key in ("voluntary_ctxt_switches", "nonvoluntary_ctxt_switches"):
                result[key] = int(value.strip())
            elif key in ("VmRSS", "VmHWM"):
                result[{"VmRSS": "rss_kib", "VmHWM": "hwm_kib"}[key]] = int(value.split()[0])
    else:
        result["status"] = status
    result["io"] = key_numbers(directory / "io")
    schedstat = read_text(directory / "schedstat")
    if isinstance(schedstat, str):
        try:
            runtime, waiting, slices = map(int, schedstat.split())
            result["schedstat"] = dict(available=True, runtime_ns=runtime,
                                       runqueue_wait_ns=waiting, timeslices=slices)
        except ValueError as error:
            result["schedstat"] = unavailable(error)
    else:
        result["schedstat"] = schedstat
    wchan = read_text(directory / "wchan")
    result["wchan"] = wchan.strip() if isinstance(wchan, str) else wchan
    return result


def process_snapshot(pid):
    directory = Path(f"/proc/{pid}")
    result = process_stat(directory)
    result.update(pid=pid, threads={})
    try:
        for task in sorted((directory / "task").iterdir(), key=lambda p: int(p.name)):
            result["threads"][task.name] = process_stat(task)
    except OSError as error:
        result["threads_error"] = unavailable(error)
    return result


def host_snapshot():
    result = {}
    stat = read_text("/proc/stat")
    if isinstance(stat, str):
        result["cpu"] = {row[0]: list(map(int, row[1:])) for row in
                         (line.split() for line in stat.splitlines()) if row[0].startswith("cpu")}
        result["stat_other"] = {row[0]: list(map(int, row[1:])) for row in
                                (line.split() for line in stat.splitlines()) if not row[0].startswith("cpu")}
    else:
        result["cpu"] = stat
    for key in ("softirqs", "interrupts"):
        value = read_text(f"/proc/{key}")
        if isinstance(value, str) and value.strip():
            lines = value.splitlines()
            values = {}
            cpu_count = len(lines[0].split())
            for line in lines[1:]:
                label, separator, text = line.partition(":")
                if not separator:
                    continue
                tokens = text.split()
                try:
                    values[label.strip()] = list(map(int, tokens[:cpu_count]))
                except ValueError:
                    continue
            result[key] = values
            if key == "interrupts":
                result["interrupts_raw"] = value
        else:
            result[key] = value if isinstance(value, dict) else unavailable("empty /proc file")
    result["temperature_c"] = numeric_file("/sys/class/thermal/thermal_zone0/temp", 1000)
    result["cpu_frequency_mhz"] = numeric_file(
        "/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq", 1000)
    enabled = numeric_file("/proc/sys/kernel/sched_schedstats")
    result["sched_schedstats"] = bool(enabled) if isinstance(enabled, (int, float)) else enabled
    result["alsa_pcm"] = {str(path): read_text(path) for pattern in ("status", "hw_params", "sw_params")
                          for path in sorted(Path("/proc/asound").glob(f"card*/pcm*/sub*/{pattern}"))}
    if shutil.which("vcgencmd"):
        try:
            output = subprocess.run(["vcgencmd", "get_throttled"], capture_output=True,
                                    text=True, timeout=.5)
            result["throttling"] = ({"available": True, "value": output.stdout.strip()}
                                    if output.returncode == 0 else unavailable(output.stderr.strip()))
        except (OSError, subprocess.TimeoutExpired) as error:
            result["throttling"] = unavailable(error)
    else:
        result["throttling"] = unavailable("vcgencmd not installed")
    return result


def assert_radio_idle():
    for directory in Path("/proc").iterdir():
        if not directory.name.isdigit():
            continue
        try:
            arguments = (directory / "cmdline").read_bytes().split(b"\0")
        except OSError:
            continue
        if arguments and Path(os.fsdecode(arguments[0])).name in (
                "cariboulite_test_app", "cariboulite_radio_backend", "cariboulite_rx_backend"):
            raise RuntimeError(f"Radio owner PID {directory.name} already running; left untouched")


def fingerprint(path):
    try:
        stat = path.stat()
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1048576), b""):
                digest.update(block)
        return dict(path=str(path), sha256=digest.hexdigest(), size=stat.st_size,
                    mtime_ns=stat.st_mtime_ns)
    except OSError as error:
        return dict(path=str(path), **unavailable(error))


def host_model():
    value = read_text("/proc/device-tree/model")
    return value.rstrip("\x00\n") if isinstance(value, str) else value


def audio_bridges():
    result = {}
    for directory in Path("/proc").iterdir():
        if not directory.name.isdigit():
            continue
        try:
            args = (directory / "cmdline").read_bytes().split(b"\0")
        except OSError:
            continue
        if args and Path(os.fsdecode(args[0])).name in ("arecord", "aplay"):
            result[directory.name] = [os.fsdecode(value) for value in args if value]
    return result


class Baseline:
    def __init__(self, args):
        self.args = args
        self.output = args.output.resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.streams = {key: (self.output / f"{key}.jsonl").open("x", buffering=1)
                        for key in ("status", "resources", "events", "kernel")}
        self.backend = self.controller = self.status_socket = self.kernel_observer = None
        self.backend_log = self.controller_log = None
        self.socket_directory = None
        self.run, self.phase = None, "setup"
        self.last_snapshot, self.controller_buffer = None, b""
        self.controller_replies = []
        self.controller_problem = None
        self.kernel_buffer = b""
        self.bridge_commands = audio_bridges()
        self.next_resource_at = time.monotonic()
        self.started_at = time.monotonic()
        self.sequence = -1
        self.session = None
        self.runlist = [dict(run=f"{source}-{rate // 1000000}M", source=source,
                             rate_hz=rate, duration_s=args.duration)
                        for source in args.sources for rate in args.rates]
        self.metadata = dict(schema_version=1, started_utc=datetime.now(timezone.utc).isoformat(),
                             options={key: str(value) if isinstance(value, Path) else value
                                      for key, value in vars(args).items()},
                             root=str(ROOT), clock_ticks_per_second=TICKS,
                             harness_pid=os.getpid(), runlist=self.runlist,
                             system=dict(uname=list(os.uname()), euid=os.geteuid(), model=host_model(),
                                         smi_driver_srcversion=read_text("/sys/module/smi_stream_dev/srcversion"),
                                         proc_version=read_text("/proc/version"),
                                         process_limits=read_text("/proc/self/limits"),
                                         sched_schedstats=numeric_file("/proc/sys/kernel/sched_schedstats"),
                                         alsa_cards=read_text("/proc/asound/cards"),
                                         alsa_pcm=read_text("/proc/asound/pcm"),
                                         audio_bridges=self.bridge_commands),
                             unmeasured=["writer service time per frame", "transport poll wait per write",
                                         "producer modulation time per frame", "RF on-air gap duration"],
                             binaries={name: fingerprint(path) for name, path in
                                       (("backend", args.backend), ("controller", args.controller),
                                        ("libcariboulite", ROOT / "build/libcariboulite.so"),
                                        ("firmware", ROOT / "firmware/top.bin"))},
                             runtime_sources={str(path.relative_to(ROOT)): fingerprint(path) for path in
                                              [ROOT / "software/libcariboulite/tools/radio_backend/main.c",
                                               ROOT / "driver/smi_stream_dev.c", ROOT / "driver/smi_stream_dev.h", *[
                                                  ROOT / "software/libcariboulite/src" / name for name in
                                                  ("mod_worker.c", "nbfm_mod.c", "tx_pipeline.c", "tx_pipeline.h",
                                                   "tx_audio_buffer.c", "tx_audio_buffer.h", "pipeline_runtime.c",
                                                   "pipeline_transport.c", "alsa_source.c", "tone_source.c",
                                                   "cariboulite_radio.c", "caribou_smi/caribou_smi.c")]]}, commands=[])
        write_json(self.output / "metadata.json", self.metadata)

    def append(self, key, value):
        self.streams[key].write(json.dumps(value, allow_nan=False) + "\n")

    def event(self, event, **details):
        self.append("events", dict(event=event, monotonic_s=time.monotonic(),
                                   run=self.run, phase=self.phase, **details))

    def kernel_snapshot(self, name):
        try:
            output = subprocess.run(["dmesg", "--color=never"], capture_output=True,
                                    text=True, timeout=3)
            if output.returncode:
                self.event("kernel_unavailable", stage=name, error=output.stderr.strip())
                return
            (self.output / f"dmesg-{name}.log").write_text(output.stdout)
            self.event("kernel_snapshot", stage=name, file=f"dmesg-{name}.log")
        except (OSError, subprocess.TimeoutExpired) as error:
            self.event("kernel_unavailable", stage=name, error=str(error))

    def sample_resources(self):
        started = time.monotonic()
        processes = {"harness": process_snapshot(os.getpid())}
        for name in ("backend", "controller"):
            process = getattr(self, name)
            if process is not None:
                processes[name] = process_snapshot(process.pid)
        for pid in self.bridge_commands:
            processes[f"audio_bridge_{pid}"] = process_snapshot(int(pid))
        self.append("resources", dict(monotonic_s=started, run=self.run, phase=self.phase,
                                      processes=processes, host=host_snapshot(),
                                      collection_duration_s=time.monotonic() - started))
        self.next_resource_at = started + 1

    def kernel_read(self):
        data = os.read(self.kernel_observer.stdout.fileno(), 65536)
        if not data:
            code = self.kernel_observer.poll()
            self.event("kernel_observer_closed", returncode=code,
                       error=self.kernel_buffer.decode(errors="replace"))
            self.kernel_observer.stdout.close()
            return
        self.kernel_buffer += data
        while b"\n" in self.kernel_buffer:
            line, self.kernel_buffer = self.kernel_buffer.split(b"\n", 1)
            if line.startswith(b"dmesg:"):
                self.event("kernel_observer_unavailable", error=line.decode(errors="replace"))
                continue
            self.append("kernel", dict(monotonic_s=time.monotonic(), run=self.run, phase=self.phase,
                                       line=line.decode(errors="replace")))

    def controller_read(self):
        data = os.read(self.controller.stdout.fileno(), 65536)
        if not data:
            raise RuntimeError("Owning controller disconnected unexpectedly")
        self.controller_log.write(data)
        self.controller_buffer += data
        while b"\n" in self.controller_buffer:
            line, self.controller_buffer = self.controller_buffer.split(b"\n", 1)
            text = line.decode(errors="replace")
            if any(marker in text for marker in ("Command rejected:", "Cannot connect",
                                                "Reply unavailable", "Request not sent",
                                                "Backend connection closed", "Invalid command")):
                self.controller_problem = text
            # The compact controller ends its TX settings with the audio
            # source. Retain the older final-line marker for staged binaries.
            if (("requested=" in text and "transition=" in text) or
                    text.split() in (["Audio", "source", "Microphone"],
                                     ["Audio", "source", "Tone"])):
                self.controller_replies.append(text)

    def status_read(self):
        packet, _, flags, _ = self.status_socket.recvmsg(MAX_PACKET_BYTES)
        if not packet:
            raise RuntimeError("Passive status connection closed unexpectedly")
        record = parse_snapshot(packet, truncated=bool(flags & socket.MSG_TRUNC))
        if record.get("owner_pid") != self.backend.pid:
            raise RuntimeError("Status sender is not this harness's backend")
        if self.session is not None and record["session"] != self.session:
            raise RuntimeError("Backend session changed during measurement")
        if record["sequence"] <= self.sequence:
            raise RuntimeError("Status sequence did not increase")
        self.session, self.sequence = record["session"], record["sequence"]
        record.update(run=self.run, phase=self.phase, received_monotonic_s=time.monotonic())
        self.append("status", record)
        self.last_snapshot = record
        tx = record.get("tx", {})
        if tx.get("fault") is not None or record.get("control", {}).get("state") == "FAULT":
            raise RuntimeError(f"Backend TX fault: {tx.get('fault')}; state={record.get('control')}")
        if self.phase == "stable":
            if record.get("control", {}).get("state") != "TX" or tx.get("requested") is not True:
                raise RuntimeError("TX left stable state during timed measurement")
            if not self.args.dry_run:
                if record.get("state") != "TX" or tx.get("running") is not True:
                    raise RuntimeError("Live TX stopped during timed measurement")
                required_workers = ["producer", "writer"]
                if tx.get("audio_buffer", {}).get("valid") is True:
                    required_workers.append("capture")
                for name in required_workers:
                    worker = tx.get("workers", {}).get(name, {})
                    if worker.get("alive") is not True or worker.get("created") is not True:
                        raise RuntimeError(f"TX {name} worker exited")

    def pump(self, seconds=.1, check_processes=True):
        if check_processes:
            for name in ("backend", "controller"):
                process = getattr(self, name)
                if process is not None and process.poll() is not None:
                    raise RuntimeError(f"{name} exited with status {process.returncode}")
        readers = []
        if self.status_socket is not None:
            readers.append(self.status_socket)
        if self.controller is not None and self.controller.stdout is not None:
            readers.append(self.controller.stdout)
        if self.kernel_observer is not None and not self.kernel_observer.stdout.closed:
            readers.append(self.kernel_observer.stdout)
        for reader in select.select(readers, [], [], seconds)[0]:
            if reader is self.status_socket:
                self.status_read()
            elif self.kernel_observer is not None and reader is self.kernel_observer.stdout:
                self.kernel_read()
            else:
                self.controller_read()
        if time.monotonic() >= self.next_resource_at:
            self.sample_resources()
        if self.controller_problem:
            raise RuntimeError(self.controller_problem)
        if self.phase == "stable" and (self.last_snapshot is None or
                time.monotonic() - self.last_snapshot["received_monotonic_s"] > 3):
            raise RuntimeError("Backend status has been stale for more than three seconds")

    def command(self, text):
        if self.controller.poll() is not None:
            raise RuntimeError("Controller is no longer connected")
        self.controller_replies.clear()
        self.event("command", command=text)
        self.controller.stdin.write((text + "\n").encode())
        self.controller.stdin.flush()
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            self.pump()
            if self.controller_replies:
                self.event("command_reply", command=text, final_line=self.controller_replies[-1])
                return
        raise TimeoutError(f"Controller did not acknowledge {text!r}")

    def wait_state(self, wanted, after=0):
        deadline = time.monotonic() + self.args.transition_timeout
        while time.monotonic() < deadline:
            self.pump()
            record = self.last_snapshot
            if record is None or record["received_monotonic_s"] < after:
                continue
            control = record.get("control", {})
            if control.get("state") == wanted and control.get("transition") is False:
                tx = record.get("tx", {})
                if tx.get("requested") is (wanted == "TX"):
                    return record
        raise TimeoutError(f"Backend did not complete transition to {wanted}")

    def launch(self):
        model = self.metadata["system"]["model"]
        if self.args.expect_model and (not isinstance(model, str) or self.args.expect_model not in model):
            raise RuntimeError(f"Expected host model {self.args.expect_model!r}; observed {model!r}")
        if not self.args.dry_run:
            assert_radio_idle()
        for executable in (self.args.backend, self.args.controller):
            if not os.access(executable, os.X_OK):
                raise RuntimeError(f"Build first: existing executable unavailable: {executable}")
        self.socket_directory = Path(tempfile.mkdtemp(prefix="cariboulite-tx-baseline-", dir="/tmp"))
        status_path = self.socket_directory / "status.sock"
        control_path = self.socket_directory / "control.sock"
        backup_seconds = min(86400, 180 + len(self.runlist) *
                             (self.args.duration + 2 * self.args.transition_timeout + 30))
        backend_command = [str(self.args.backend), "--channel", self.args.channel,
                           "--rx-frequency", str(self.args.frequency), "--tx-frequency", str(self.args.frequency),
                           "--tx-power", str(self.args.power), "--rx-sample-rate", "1000000",
                           "--tx-sample-rate", str(self.args.rates[0]), "--tx-source", self.args.sources[0],
                           "--tx-microphone", self.args.microphone, "--rx-playback", self.args.playback,
                           "--socket", str(status_path), "--control-socket", str(control_path),
                           "--status-hz", str(self.args.status_hz), "--seconds", str(backup_seconds)]
        if self.args.dry_run:
            backend_command.append("--dry-run")
        controller_command = [str(self.args.controller), "--socket", str(control_path), "--timeout", "10"]
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        self.metadata["environment"] = {key: environment.get(key) for key in
                                        ("LD_PRELOAD", "LD_LIBRARY_PATH", "PATH")}
        # An inherited IQ capture hook would change the baseline and could save RF data.
        if environment.get("LD_PRELOAD"):
            raise RuntimeError("Unset LD_PRELOAD before collecting a baseline")
        self.metadata["commands"] = [backend_command, controller_command]
        self.metadata["sockets"] = {"status": str(status_path), "control": str(control_path)}
        write_json(self.output / "metadata.json", self.metadata)
        self.backend_log = (self.output / "backend.log").open("xb", buffering=0)
        self.backend = subprocess.Popen(backend_command, cwd=ROOT, env=environment,
                                        stdin=subprocess.DEVNULL, stdout=self.backend_log,
                                        stderr=subprocess.STDOUT, start_new_session=True)
        self.event("backend_started", pid=self.backend.pid, backup_seconds=backup_seconds)
        self.kernel_snapshot("start")
        try:
            self.kernel_observer = subprocess.Popen(["dmesg", "--follow-new", "--color=never"],
                                                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                                    stderr=subprocess.STDOUT, start_new_session=True)
            self.event("kernel_observer_started", pid=self.kernel_observer.pid)
        except OSError as error:
            self.event("kernel_observer_unavailable", error=str(error))
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            if self.backend.poll() is not None:
                raise RuntimeError(f"Backend exited during initialization: {self.backend.returncode}")
            if status_path.exists() and control_path.exists():
                self.status_socket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
                self.status_socket.settimeout(3)
                self.status_socket.connect(str(status_path))
                self.status_socket.setblocking(False)
                break
            self.pump(.1)
        else:
            raise TimeoutError("Backend did not publish private sockets within 120 seconds")
        self.controller_log = (self.output / "controller.log").open("xb", buffering=0)
        self.controller = subprocess.Popen(controller_command, cwd=ROOT, env=environment,
                                           stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, start_new_session=True)
        self.event("controller_started", pid=self.controller.pid)
        deadline = time.monotonic() + 12
        while not self.controller_replies and time.monotonic() < deadline:
            self.pump()
        if not self.controller_replies:
            raise TimeoutError("C controller did not finish initial status exchange")
        self.wait_state("IDLE")
        if self.args.program_fpga:
            self.command("fpga program")
            self.wait_state("IDLE", after=time.monotonic())
        self.command(f"tx frequency {self.args.frequency / 1e6:.9f}")
        self.command(f"tx power {self.args.power}")

    def measure(self):
        for configuration in self.runlist:
            self.run, self.phase = configuration["run"], "setup"
            self.event("run_configure", source=configuration["source"],
                       rate_hz=configuration["rate_hz"], duration_s=configuration["duration_s"])
            self.command(f"tx source {configuration['source']}")
            self.command(f"tx rate {configuration['rate_hz'] // 1000000}")
            self.phase = "starting"
            self.command("tx start")
            record = self.wait_state("TX", after=time.monotonic())
            config = record.get("config", {})
            if (record.get("channel") != self.args.channel or
                    config.get("tx_sample_rate_hz") != configuration["rate_hz"] or
                    config.get("tx_source") != configuration["source"] or
                    config.get("tx_power_dbm") != self.args.power or
                    abs(config.get("tx_frequency_hz", 0) - self.args.frequency) > 1):
                raise RuntimeError(f"Accepted TX settings differ from requested test: {config}")
            self.phase = "stable"
            started = time.monotonic()
            generation = record["tx"]["generation"]
            self.event("stable_start", tx_generation=generation, duration_s=self.args.duration)
            print(f"{self.run}: {'SIMULATED lifecycle' if self.args.dry_run else 'TX'} "
                  f"{self.args.frequency / 1e6:.6f} MHz for {self.args.duration:g}s", flush=True)
            self.sample_resources()
            deadline = started + self.args.duration
            next_progress_at = started + 20
            while time.monotonic() < deadline:
                self.pump(min(.1, max(0, deadline - time.monotonic())))
                if self.last_snapshot["tx"]["generation"] != generation:
                    raise RuntimeError("TX generation changed inside a timed run")
                if time.monotonic() >= next_progress_at:
                    queue = self.last_snapshot.get("tx", {}).get("queue") or {}
                    capture = self.last_snapshot.get("tx", {}).get("capture") or {}
                    print(f"{self.run}: {time.monotonic() - started:.0f}/{self.args.duration:g}s "
                          f"queue={queue.get('depth', 'unavailable')}/{queue.get('capacity', 'unavailable')} "
                          f"capture overruns={capture.get('overruns', 'unavailable')}", flush=True)
                    next_progress_at += 20
            self.sample_resources()
            self.event("stable_end", measured_wall_s=time.monotonic() - started)
            self.phase = "stopping"
            self.command("tx stop")
            record = self.wait_state("IDLE", after=time.monotonic())
            if record["tx"].get("running") is not False or record["tx"].get("requested") is not False:
                raise RuntimeError("Stop transition completed without TX being stopped")
            self.phase = "idle"
            self.event("run_finished")
            self.kernel_snapshot(self.run)
        self.run = None

    def cleanup(self):
        self.phase = "cleanup"
        errors = []
        # Stop via the still-connected owner first; loss of that owner also unkeys TX.
        if self.controller is not None and self.controller.poll() is None:
            self.controller_problem = None
            try:
                self.command("tx stop")
                self.wait_state("IDLE", after=time.monotonic())
                self.event("cleanup_idle_confirmed")
            except (Exception, KeyboardInterrupt) as error:
                errors.append(f"Idle confirmation: {error}")
            try:
                self.controller.stdin.write(b"q\n")
                self.controller.stdin.flush()
                self.controller.stdin.close()
                self.controller.wait(timeout=4)
            except (OSError, subprocess.TimeoutExpired):
                self.controller.terminate()
                try:
                    self.controller.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    self.controller.kill()
                    self.controller.wait(timeout=3)
                    errors.append("Owning controller required forced termination")
        if self.backend is not None:
            if self.backend.poll() is None:
                self.backend.send_signal(signal.SIGINT)
                try:
                    self.backend.wait(timeout=min(60, self.args.transition_timeout + 10))
                except subprocess.TimeoutExpired:
                    self.backend.terminate()
                    try:
                        self.backend.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        self.backend.kill()
                        self.backend.wait(timeout=3)
                        errors.append("Backend required forced termination; radio idle could not be confirmed")
            self.metadata["backend_exit_code"] = self.backend.returncode
            if self.backend.returncode != 0:
                errors.append(f"Backend exit code {self.backend.returncode}")
        if self.controller is not None:
            self.metadata["controller_exit_code"] = self.controller.poll()
        if self.kernel_observer is not None:
            if self.kernel_observer.poll() is None:
                self.kernel_observer.terminate()
                try:
                    self.kernel_observer.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.kernel_observer.kill()
                    self.kernel_observer.wait(timeout=2)
            if not self.kernel_observer.stdout.closed:
                remainder = self.kernel_observer.stdout.read()
                for line in (self.kernel_buffer + remainder).splitlines():
                    if line.startswith(b"dmesg:"):
                        self.event("kernel_observer_unavailable", error=line.decode(errors="replace"))
                        continue
                    self.append("kernel", dict(monotonic_s=time.monotonic(), run=self.run, phase=self.phase,
                                               line=line.decode(errors="replace")))
                self.kernel_observer.stdout.close()
        self.kernel_snapshot("end")
        self.event("cleanup_finished", errors=errors)
        self.metadata["cleanup_errors"] = errors
        if self.status_socket is not None:
            self.status_socket.close()
        for stream in (self.backend_log, self.controller_log):
            if stream is not None:
                stream.close()
        # Remove only our sockets in our private directory, never a shared socket.
        if self.socket_directory is not None:
            for name in ("status.sock", "control.sock"):
                path = self.socket_directory / name
                if path.exists() and path.is_socket():
                    path.unlink()
            try:
                self.socket_directory.rmdir()
            except OSError as error:
                self.metadata["socket_cleanup_error"] = str(error)
        self.metadata["ended_utc"] = datetime.now(timezone.utc).isoformat()
        self.metadata["total_wall_s"] = time.monotonic() - self.started_at
        write_json(self.output / "metadata.json", self.metadata)
        for stream in self.streams.values():
            stream.close()
        return errors


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", type=Path, default=BACKEND,
                        help="existing backend executable (default build/radio-backend)")
    parser.add_argument("--controller", type=Path, default=CONTROLLER,
                        help="existing controller executable (default build/radio-backend)")
    parser.add_argument("--duration", type=float, default=180, help="seconds of stable TX per run (default 180)")
    parser.add_argument("--sources", nargs="+", choices=("mic", "tone"), default=["mic", "tone"])
    parser.add_argument("--rates", nargs="+", type=int, choices=(1000000, 2000000, 4000000),
                        default=[1000000, 4000000], help="RF samples/s")
    parser.add_argument("--output", type=Path, default=ROOT / "build/radio-backend" /
                        datetime.now(timezone.utc).strftime("tx-baseline-%Y%m%dT%H%M%SZ"))
    parser.add_argument("--channel", choices=("RF24", "RF09"), default="RF24")
    parser.add_argument("--frequency", type=int, default=430100000, help="TX frequency Hz (default 430100000)")
    parser.add_argument("--power", type=int, default=-12, help="requested dBm (default -12)")
    parser.add_argument("--microphone", default="plughw:Loopback,1,1")
    parser.add_argument("--playback", default="plughw:Loopback,0,0")
    parser.add_argument("--status-hz", type=float, default=5,
                        help="backend status/register refresh Hz, 0.5..20 (default 5)")
    parser.add_argument("--transition-timeout", type=float, default=60)
    parser.add_argument("--expect-model", help="require this substring in /proc/device-tree/model before launch")
    parser.add_argument("--program-fpga", action="store_true", help="program existing firmware/top.bin before trials")
    parser.add_argument("--dry-run", action="store_true", help="simulated lifecycle only; no hardware/audio/RF counts")
    args = parser.parse_args(argv)
    args.backend = args.backend.expanduser().resolve()
    args.controller = args.controller.expanduser().resolve()
    if not math.isfinite(args.duration) or not .1 <= args.duration <= 7200:
        parser.error("duration must be finite and between 0.1 and 7200 seconds")
    if not math.isfinite(args.transition_timeout) or not 1 <= args.transition_timeout <= 120:
        parser.error("transition-timeout must be finite and between 1 and 120 seconds")
    if not math.isfinite(args.status_hz) or not .5 <= args.status_hz <= 20:
        parser.error("status-hz must be finite and between 0.5 and 20 Hz")
    if not 1000000 <= args.frequency <= 6000000000:
        parser.error("frequency must be between 1000000 and 6000000000 Hz")
    if not (-12 if args.channel == "RF24" else -14) <= args.power <= 14:
        parser.error("power is outside channel limits")
    if len(args.sources) != len(set(args.sources)) or len(args.rates) != len(set(args.rates)):
        parser.error("sources and rates must not contain duplicates")
    return args


def main(argv=None):
    args = arguments(argv)
    baseline = Baseline(args)
    result = 0
    def request_stop(number, frame):
        raise KeyboardInterrupt(f"Received {signal.Signals(number).name}")
    for number in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(number, request_stop)
    try:
        baseline.launch()
        baseline.measure()
        baseline.metadata["result"] = "complete"
    except (Exception, KeyboardInterrupt) as error:
        baseline.metadata["result"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        baseline.metadata["error"] = str(error) or "KeyboardInterrupt"
        baseline.event("aborted", error=str(error) or "KeyboardInterrupt")
        print(f"Baseline stopped: {error or 'KeyboardInterrupt'}", file=sys.stderr, flush=True)
        result = 130 if isinstance(error, KeyboardInterrupt) else 1
    finally:
        # Cleanup remains bounded if a second interrupt arrives while unkeying.
        for number in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            signal.signal(number, signal.SIG_IGN)
        if baseline.cleanup():
            result = result or 1
    try:
        from tx_queue_baseline_report import generate_report
        generate_report(baseline.output)
    except Exception as error:
        baseline.metadata["analysis_error"] = str(error)
        write_json(baseline.output / "metadata.json", baseline.metadata)
        print(f"Report unavailable: {error}; raw artifacts retained", file=sys.stderr)
    print(f"Artifacts: {baseline.output}", flush=True)
    return result


if __name__ == "__main__":
    sys.exit(main())
