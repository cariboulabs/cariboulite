#!/usr/bin/env python3
"""Read telemetry from a status publisher without controlling radio hardware.

The local SOCK_SEQPACKET connection only receives version 1 JSON snapshots.
Closing this program has no effect on the receiver or transmitter.
This standalone client does not add a publisher to test_app.
"""

import argparse
import curses
import json
import math
import os
import re
import socket
import sys
import time

from at86rf215_monitor import decode_agc, format_register_rows


MAX_PACKET_BYTES = 16384
MAX_DRAIN_PACKETS = 64
RECONNECT_SECONDS = 0.5
STALE_SECONDS = 1.0


def default_socket_path():
    return f"/tmp/cariboulite-monitor-{os.geteuid()}/status.sock"


def _number(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


def _json_float(value):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError("nonfinite JSON number")
    return number


def _reject_json_constant(value):
    raise ValueError(f"invalid JSON constant {value}")


def parse_snapshot(packet, truncated=False):
    """Validate a bounded protocol packet; do not accept incompatible versions."""
    if truncated or len(packet) > MAX_PACKET_BYTES:
        raise ValueError(f"snapshot exceeds the {MAX_PACKET_BYTES} byte packet limit")
    try:
        snapshot = json.loads(packet, parse_float=_json_float,
                              parse_constant=_reject_json_constant)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise ValueError("malformed JSON snapshot") from exc
    if not isinstance(snapshot, dict):
        raise ValueError("snapshot must be a JSON object")
    if type(snapshot.get("version")) is not int or snapshot["version"] != 1:
        raise ValueError("unsupported snapshot protocol version")
    if not isinstance(snapshot.get("session"), str) or not snapshot["session"]:
        raise ValueError("snapshot has no session identifier")
    if type(snapshot.get("sequence")) is not int or snapshot["sequence"] < 0:
        raise ValueError("snapshot has an invalid sequence number")
    if not _number(snapshot.get("monotonic_s")):
        raise ValueError("snapshot has an invalid monotonic timestamp")
    for key in ("realtime_s", "hardware_monotonic_s"):
        if key in snapshot and not _number(snapshot[key]):
            raise ValueError(f"snapshot has an invalid {key} timestamp")
    # These sections can be omitted by older version 1 producers, but should
    # never make the display fail by changing type unexpectedly.
    for key in ("config", "rx", "tx", "hardware", "loopback", "publisher"):
        if key in snapshot and not isinstance(snapshot[key], dict):
            raise ValueError(f"snapshot {key} section must be an object")
    return snapshot



def _mapping(value):
    return value if isinstance(value, dict) else {}


def _label(value, default="unknown"):
    if value is None:
        return default
    # Avoid terminal control characters even if another local process sends a
    # malformed label. This affects display only, not the JSON output.
    return "".join(c for c in str(value) if c.isprintable())[:160]


def _on(value):
    return "on" if value is True else "off" if value is False else "unknown"


def _formatted(value, pattern, default="unknown"):
    return format(value, pattern) if _number(value) else default


def _frequency(value):
    return f"{value / 1e6:.6f} MHz" if _number(value) else "unknown"


def _bandwidth(value):
    return f"{value / 1e3:g} kHz" if _number(value) else "unknown"


def _age(snapshot, key, now):
    stamp = snapshot.get(key)
    return max(0.0, now - stamp) if _number(stamp) and stamp > 0 else None


def _age_label(age):
    if age is None:
        return "unavailable"
    return f"{age:.1f}s" + (" STALE" if age > STALE_SECONDS else "")


def source_label(snapshot):
    """Make simulated data conspicuous, and avoid assuming a live source."""
    source = snapshot.get("source")
    if isinstance(source, dict):
        kind = _label(source.get("kind"), "unknown")
        label = _label(source.get("label"), "")
        source = f"{kind}: {label}" if label else kind
    elif isinstance(source, str):
        source = _label(source)
    else:
        source = "unknown (publisher did not identify its data source)"
    words = re.findall(r"[a-z]+", source.lower())
    if any(word.startswith("simulat") or word in ("synthetic", "demo", "fixture", "mock")
           for word in words):
        return f"SIMULATED DATA - {source}"
    return source


def _integer(value, nonnegative=False):
    if type(value) is int and (not nonnegative or value >= 0):
        return str(value)
    return "unavailable"


def _counter(section, key):
    return _integer(section.get(key), nonnegative=True)


def _format_worker(name, value):
    worker = _mapping(value)
    scheduling = _mapping(worker.get("scheduling"))
    valid = scheduling.get("valid") is True
    policy = _label(scheduling.get("policy_name"), "unavailable") if valid else "unavailable"
    priority = _integer(scheduling.get("priority")) if valid else "unavailable"
    affinity = _label(scheduling.get("affinity"), "unavailable") if valid else "unavailable"
    errors = "/".join(_integer(scheduling.get(key)) for key in
                      ("rt_error", "fallback_error", "affinity_error", "mlock_error", "query_error"))
    return (
        f"{name} worker: alive={_on(worker.get('alive'))}"
        f" created={_on(worker.get('created'))}"
        f" last_error={_integer(worker.get('last_error'))}"
        f" sched={policy}/{priority}"
        f" req={_integer(scheduling.get('requested_priority'))}"
        f" cpu(req/applied)={_integer(scheduling.get('requested_cpu'))}/"
        f"{_integer(scheduling.get('applied_cpu'))} affinity={affinity}"
        f" errors(rt/fb/cpu/lock/query)={errors}"
        f" mlock={_on(scheduling.get('mlock_attempted'))}"
    )


def _format_rx_instrumentation(rx):
    """Optional version 1 extensions; absent counters never imply zero errors."""
    lines = []
    if "audio_queue" in rx:
        queue = _mapping(rx["audio_queue"])
        lines += [
            f"RX audio FIFO: {_counter(queue, 'depth')}/{_counter(queue, 'capacity')}"
            f"  min/max={_counter(queue, 'min_depth')}/{_counter(queue, 'max_depth')}"
            f"  puts/gets={_counter(queue, 'puts')}/{_counter(queue, 'gets')}",
            f"  drops={_counter(queue, 'drops')}"
            f"  put/get timeouts={_counter(queue, 'timeouts_put')}/{_counter(queue, 'timeouts_get')}"
            f"  shutdown put/get={_counter(queue, 'shutdown_puts')}/{_counter(queue, 'shutdown_gets')}",
        ]
    if "playback" in rx:
        playback = _mapping(rx["playback"])
        if playback.get("valid") is True:
            lines += [
                f"Playback: calls={_counter(playback, 'write_calls')}"
                f"  frames(req/written)={_counter(playback, 'frames_requested')}/"
                f"{_counter(playback, 'frames_written')}"
                f"  partial={_counter(playback, 'partial_writes')}"
                f"  would_block={_counter(playback, 'would_block')}"
                f"  zero={_counter(playback, 'zero_writes')}",
                f"  underruns={_counter(playback, 'underruns')}"
                f"  recovery(attempt/ok/fail)={_counter(playback, 'recovery_attempts')}/"
                f"{_counter(playback, 'recovery_successes')}/{_counter(playback, 'recovery_failures')}"
                f"  errors={_counter(playback, 'write_errors')}"
                f"  last_error={_integer(playback.get('last_error'))}",
            ]
        else:
            lines += ["Playback: unavailable", "  ALSA counters unavailable"]
    if "reader" in rx:
        reader = _mapping(rx["reader"])
        lines.append(
            f"RX reader: calls={_counter(reader, 'calls')}"
            f"  samples={_counter(reader, 'samples')}"
            f"  short/zero reads={_counter(reader, 'short_reads')}/{_counter(reader, 'zero_reads')}"
            f"  errors={_counter(reader, 'errors')}"
            f"  last_error={_integer(reader.get('last_error'))}"
        )
    if "workers" in rx:
        workers = _mapping(rx["workers"])
        for name in ("reader", "demod", "audio"):
            lines.append(_format_worker(name, workers.get(name)))
    return lines


def _format_tx_instrumentation(tx, control):
    """Keep eight rows, including optional hardware pacing instrumentation."""
    writer = _mapping(tx.get("writer"))
    capture = _mapping(tx.get("capture"))
    workers = _mapping(tx.get("workers"))
    extended = "driver" in tx or "audio_buffer" in tx or "smi_timing" in tx or "capture" in workers
    generation = tx.get("generation", control.get("tx_pipeline_generation"))
    lines = [
        f"TX pipeline: running={_on(tx.get('running'))}"
        f" requested={_on(tx.get('requested', control.get('tx_requested')))}"
        f" initialized={_on(tx.get('initialized'))} generation={_integer(generation, True)}"
        f" resume RX={_on(control.get('rx_resume'))} transition={_on(control.get('transition'))}",
    ]
    if extended:
        lines[0] += f" hardware paced={_on(tx.get('hardware_paced'))}"
        driver = _mapping(tx.get("driver"))
        audio = _mapping(tx.get("audio_buffer"))
        if driver.get("valid") is True:
            lines.append(
                f"TX driver: FIFO={_counter(driver, 'fifo_queued_bytes')}/"
                f"{_counter(driver, 'fifo_limit_bytes')} bytes"
                f" completed={_counter(driver, 'completed_periods')}"
                f" underruns={_counter(driver, 'underrun_periods')}"
                f" DMA={_on(driver.get('dma_running'))} bounded={_on(driver.get('bounded_fifo'))}")
        else:
            lines.append("TX driver: unavailable")
        if "smi_timing" in tx:
            timing = _mapping(tx.get("smi_timing"))
            if timing.get("valid") is True:
                values = "/".join(_integer(timing.get(key), True) for key in
                                  ("write_setup", "write_strobe", "write_hold", "write_pace"))
                lines[-1] += f" SMI write(set/strobe/hold/pace)={values}"
            else:
                lines[-1] += " SMI write=unavailable"
        if audio.get("valid") is True:
            lines.append(
                f"TX audio: {_counter(audio, 'queued_frames')}/{_counter(audio, 'capacity_frames')} frames"
                f" target={_counter(audio, 'target_frames')}"
                f" correction={_formatted(audio.get('correction_ppm'), '+.1f', 'unavailable')} ppm"
                f" clock={_on(audio.get('clock_valid'))}"
                f" under/overflow={_counter(audio, 'underflows')}/{_counter(audio, 'overflows')}"
                f" cue drops={_counter(audio, 'cue_discard_frames')}"
                f" error={_integer(audio.get('last_error'))}")
        else:
            lines.append("TX audio: unavailable")
    writer_lines = [
        f"TX writer: calls={_counter(writer, 'calls')} samples={_counter(writer, 'samples')}"
        f" written_sequence={_counter(writer, 'written_sequence')}",
        f"partial/zero writes={_counter(writer, 'partial_writes')}/{_counter(writer, 'zero_writes')}"
        f" would_block={_counter(writer, 'would_block')} errors={_counter(writer, 'errors')}"
        f" last_error={_integer(writer.get('last_error'))}",
    ]
    lines.extend([" ".join(writer_lines)] if extended else
                 [writer_lines[0], "  " + writer_lines[1]])
    if capture.get("valid") is True:
        capture_lines = [
            f"TX capture: calls={_counter(capture, 'read_calls')}"
            f" frames={_counter(capture, 'frames_read')} overruns={_counter(capture, 'overruns')}",
            f"recovery(attempt/ok/fail)={_counter(capture, 'recovery_attempts')}/"
            f"{_counter(capture, 'recovery_successes')}/{_counter(capture, 'recovery_failures')}"
            f" errors={_counter(capture, 'read_errors')} last_error={_integer(capture.get('last_error'))}",
        ]
        lines.extend([" ".join(capture_lines)] if extended else
                     [capture_lines[0], "  " + capture_lines[1]])
    else:
        lines.extend(["TX capture: unavailable (ALSA capture counters unavailable)"] if extended else
                     ["TX capture: unavailable", "  ALSA capture counters unavailable"])
    for name in (("producer", "writer", "capture") if extended else ("producer", "writer")):
        lines.append(_format_worker(name, workers.get(name)))
    fault = tx.get("fault")
    fault_label = "none" if "fault" in tx and fault is None else (
        _label(fault) if isinstance(fault, str) and fault else "unavailable")
    if extended:
        lines[0] += f" TX fault: {fault_label}"
    else:
        lines.append(f"TX fault: {fault_label}")
    return lines


def format_snapshot(snapshot, now=None, rates=None, register_view="auto"):
    """Return readable cached telemetry, tolerating unavailable fields."""
    now = time.monotonic() if now is None else now
    config = _mapping(snapshot.get("config"))
    rx = _mapping(snapshot.get("rx"))
    tx = _mapping(snapshot.get("tx"))
    noise = _mapping(rx.get("noise"))
    hardware = _mapping(snapshot.get("hardware"))
    loopback = _mapping(snapshot.get("loopback"))
    control = _mapping(snapshot.get("control"))
    state = control.get("state") or snapshot.get("state")
    tx_active = state in ("TX_STARTING", "TX", "TX_STOPPING")
    effective_register_view = ("tx" if tx_active else "rx") if register_view == "auto" else register_view
    tx_settings = (f"  TX rate={_formatted(config.get('tx_sample_rate_hz'), ',.0f')} S/s"
                   f" source={_label(config.get('tx_source'))}"
                   f" dev=+/-{_formatted(config.get('tx_deviation_hz'), 'g')} Hz") if (
                       "tx_sample_rate_hz" in config) else ""
    tx_actual = config.get("tx_actual_frequency_hz")
    tx_frequency = _frequency(config.get("tx_frequency_hz"))
    if "tx_actual_frequency_hz" in config:
        tx_frequency += " actual=" + (_frequency(tx_actual) if _number(tx_actual) and tx_actual > 0 else "unavailable")
    tx_power = _formatted(config.get("tx_power_dbm"), "g")
    if "tx_actual_power_dbm" in config:
        tx_power = f"power(req/est)={tx_power}/{_formatted(config.get('tx_actual_power_dbm'), 'g', 'unavailable')} dBm"
    else:
        tx_power = f"power={tx_power} dBm"
    tx_fault = tx.get("fault")
    inactive_tx_fault = f"  TX fault={_label(tx_fault)}" if (
        not tx_active and isinstance(tx_fault, str) and tx_fault) else ""
    generations = (f"  config={_label(control.get('config_generation'))}"
                   f"  pipeline={_label(control.get('pipeline_generation'))}") if control else ""
    rates = rates or {}
    noise_valid = noise.get("valid") is True
    lines = [
        f"Data source: {source_label(snapshot)}",
        f"{_label(snapshot.get('channel'))}  state={_label(state)}{inactive_tx_fault}"
        f"  mode={_label(snapshot.get('rx_mode'))}"
        f"  sample rate={_formatted(config.get('sample_rate_hz'), ',.0f')} S/s",
        f"RX {_frequency(config.get('rx_frequency_hz'))}"
        f"  TX {tx_frequency}  TX {tx_power}",
        f"RX bandwidth={_bandwidth(config.get('rx_bandwidth_hz'))}"
        f"  TX bandwidth={_bandwidth(config.get('tx_bandwidth_hz'))}{tx_settings}",
        f"Session={_label(snapshot.get('session'))}"
        f"  PID={_label(snapshot.get('owner_pid'))}"
        f"  sequence={_label(snapshot.get('sequence'))}{generations}",
        f"Snapshot age={_age_label(_age(snapshot, 'monotonic_s', now))}"
        f"  hardware age={_age_label(_age(snapshot, 'hardware_monotonic_s', now))}",
        "",
    ]
    for name, pipeline in (("RX", rx), ("TX", tx)):
        queue = _mapping(pipeline.get("queue"))
        lines.append(
            f"{name} application RF queue: {_label(queue.get('depth'))}/"
            f"{_label(queue.get('capacity'))}  running={_on(pipeline.get('running'))}"
            f"  min/max={_label(queue.get('min_depth'))}/{_label(queue.get('max_depth'))}"
        )
        lines.append(
            f"  puts={_label(queue.get('puts'))}  gets={_label(queue.get('gets'))}"
            f"  drops={_label(queue.get('drops'))}"
            f"  put/get timeouts={_label(queue.get('timeouts_put'))}/"
            f"{_label(queue.get('timeouts_get'))}"
        )
        rate = rates.get(name.lower())
        if rate:
            lines.append(f"  put/get rate={rate[0]:.1f}/{rate[1]:.1f} frames/s")
    lines.extend(_format_tx_instrumentation(tx, control) if tx_active else _format_rx_instrumentation(rx))
    lines += [
        "",
        f"Squelch: noise={_on(rx.get('noise_enabled'))}"
        f"  carrier={_on(rx.get('carrier_enabled'))}"
        f"  audio gate={_on(rx.get('gate_open'))}",
        f"Noise detector: valid={_on(noise.get('valid'))}"
        f"  open={_on(noise.get('detector_open') if noise_valid else None)}"
        f"  RMS={_formatted(noise.get('rms'), '.4f') if noise_valid else 'unavailable'}"
        f"  open/close={_formatted(noise.get('open_rms'), '.4f')}/"
        f"{_formatted(noise.get('close_rms'), '.4f')}",
        "",
    ]
    lines.append("AT86RF215 Modem Status Registers:")
    lines.extend(format_register_rows(hardware, snapshot.get("channel"), view=effective_register_view))
    fpga = _mapping(hardware.get("fpga"))
    if fpga.get("valid") is True:
        fields = []
        for key in ("raw_status", "io_control", "debug"):
            value = fpga.get(key)
            if type(value) is int:
                fields.append(f"{key}=0x{value:02X}")
        lines.append("FPGA: " + " ".join(fields))
    lines.append(f"Loopback: armed={_on(loopback.get('armed'))}"
                 f" active={_on(loopback.get('active'))}"
                 f" samples={_label(loopback.get('samples'))}"
                 f" timeouts={_label(loopback.get('timeouts'))}")
    lines += ["", "Queue timeouts are wait events; drops are application queue drops."]
    return lines


def queue_rates(previous, current):
    """Only calculate rates within a session, allowing queue counter resets."""
    if not previous or previous.get("session") != current.get("session"):
        return {}
    elapsed = current["monotonic_s"] - previous["monotonic_s"]
    if not _number(elapsed) or elapsed <= 0:
        return {}
    result = {}
    for name in ("rx", "tx"):
        generation_key = "pipeline_generation" if name == "rx" else "tx_pipeline_generation"
        old_generation = _mapping(previous.get("control")).get(generation_key)
        new_generation = _mapping(current.get("control")).get(generation_key)
        if old_generation != new_generation:
            continue
        if name == "tx" and (_mapping(previous.get(name)).get("generation") !=
                             _mapping(current.get(name)).get("generation")):
            continue
        old = _mapping(_mapping(previous.get(name)).get("queue"))
        new = _mapping(_mapping(current.get(name)).get("queue"))
        values = [old.get("puts"), old.get("gets"), new.get("puts"), new.get("gets")]
        if all(type(value) is int and value >= 0 for value in values):
            old_puts, old_gets, new_puts, new_gets = values
            if new_puts >= old_puts and new_gets >= old_gets:
                try:
                    rates = ((new_puts - old_puts) / elapsed,
                             (new_gets - old_gets) / elapsed)
                except OverflowError:
                    continue
                if all(_number(rate) for rate in rates):
                    result[name] = rates
    return result


class SnapshotConnection:
    """A receive-only client with bounded waits and reconnection."""

    def __init__(self, path):
        self.path = path
        self.sock = None
        self.retry_at = 0.0
        self.status = "waiting for status publisher"
        self.latest = None
        self.rates = {}

    def close(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def _disconnect(self, reason):
        self.close()
        self.retry_at = time.monotonic() + RECONNECT_SECONDS
        self.status = reason

    def poll(self, timeout=0.15):
        timeout = min(max(timeout, 0.0), 0.2)
        if self.sock is None:
            if time.monotonic() < self.retry_at:
                time.sleep(max(0.0, min(timeout, self.retry_at - time.monotonic())))
                return None
            candidate = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            candidate.settimeout(max(timeout, 0.001))
            try:
                candidate.connect(self.path)
            except OSError as exc:
                candidate.close()
                self.retry_at = time.monotonic() + RECONNECT_SECONDS
                self.status = f"waiting for status publisher ({exc.strerror or exc})"
                return None
            self.sock = candidate
            self.status = "connected"
        self.sock.settimeout(max(timeout, 0.001))
        try:
            packet, _, flags, _ = self.sock.recvmsg(MAX_PACKET_BYTES)
        except socket.timeout:
            return None
        except OSError as exc:
            self._disconnect(f"disconnected ({exc.strerror or exc})")
            return None
        if not packet:
            self._disconnect("disconnected; waiting for status publisher")
            return None
        # A slow terminal should catch up to the newest queued snapshot, rather
        # than slowly replaying old telemetry. Bound both packet size and work
        # per display tick so a busy or malformed publisher cannot monopolize
        # the client. No messages are sent to request another snapshot.
        candidate = self.latest
        newest = None
        self.sock.setblocking(False)
        for index in range(MAX_DRAIN_PACKETS):
            if index:
                try:
                    packet, _, flags, _ = self.sock.recvmsg(MAX_PACKET_BYTES)
                except BlockingIOError:
                    break
                except OSError as exc:
                    self._disconnect(f"disconnected ({exc.strerror or exc})")
                    break
                if not packet:
                    self._disconnect("disconnected; waiting for status publisher")
                    break
            try:
                snapshot = parse_snapshot(packet, bool(flags & socket.MSG_TRUNC))
                if candidate and snapshot["session"] == candidate["session"]:
                    if snapshot["sequence"] < candidate["sequence"]:
                        raise ValueError("snapshot sequence moved backwards within a session")
                    if snapshot["sequence"] == candidate["sequence"]:
                        continue  # A reconnect may replay the last published record.
                    if snapshot["monotonic_s"] < candidate["monotonic_s"]:
                        raise ValueError("snapshot timestamp moved backwards within a session")
            except ValueError as exc:
                self.status = f"snapshot rejected: {exc}"
                continue
            self.status = "connected"
            candidate = newest = snapshot
        if newest is not None:
            self.rates = queue_rates(self.latest, newest)
            self.latest = newest
        return newest


def run_interactive(screen, connection, register_view="auto"):
    try:
        curses.curs_set(0)
    except curses.error:
        pass
    screen.nodelay(True)
    screen.keypad(True)
    scroll = 0
    horizontal = 0
    while True:
        connection.poll()
        height, width = screen.getmaxyx()
        screen.erase()
        body = format_snapshot(connection.latest, rates=connection.rates,
                               register_view=register_view) if connection.latest else [
            "Waiting for a status publisher in another terminal.",
            "This standalone client does not change or control test_app.",
            f"Socket: {connection.path}",
        ]
        available = max(1, height - 3)
        scroll = min(scroll, max(0, len(body) - available))
        widest = max((len(_label(line, "")) for line in body), default=0)
        horizontal = min(horizontal, max(0, widest - max(1, width - 1)))
        lines = ["CaribouLite radio monitor (passive) | q: quit | arrows: scroll",
                 connection.status, ""] + body[scroll:scroll + available]
        for row, line in enumerate(lines[:height]):
            if width <= 1:
                continue
            try:
                rendered = _label(line, "")
                if row >= 3:
                    rendered = rendered[horizontal:]
                screen.addnstr(row, 0, rendered, width - 1)
            except curses.error:
                pass  # A resize may happen between getmaxyx and addnstr.
        screen.refresh()
        key = screen.getch()
        if key in (ord("q"), ord("Q")):
            return 0
        if key == curses.KEY_DOWN:
            scroll += 1
        elif key == curses.KEY_UP:
            scroll = max(0, scroll - 1)
        elif key == curses.KEY_NPAGE:
            scroll += available
        elif key == curses.KEY_PPAGE:
            scroll = max(0, scroll - available)
        elif key == curses.KEY_RIGHT:
            horizontal += 8
        elif key == curses.KEY_LEFT:
            horizontal = max(0, horizontal - 8)


def run_stream(connection, once=False, timeout=5.0, plain=False, register_view="auto"):
    deadline = time.monotonic() + timeout if once else None
    previous_status = None
    while deadline is None or time.monotonic() < deadline:
        remaining = deadline - time.monotonic() if deadline else 0.15
        snapshot = connection.poll(min(0.15, max(0, remaining)))
        if snapshot is not None:
            if plain:
                print("\n".join(format_snapshot(snapshot, rates=connection.rates,
                                               register_view=register_view)), flush=True)
                print(flush=True)
            else:
                print(json.dumps(snapshot, separators=(",", ":"), allow_nan=False), flush=True)
            if once:
                return 0
        if connection.status != previous_status:
            print(connection.status, file=sys.stderr, flush=True)
            previous_status = connection.status
    print(f"No valid snapshot received within {timeout:g} seconds from {connection.path}",
          file=sys.stderr)
    return 1


def positive_seconds(value):
    try:
        seconds = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("timeout must be a positive number") from exc
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("timeout must be a positive finite number")
    return seconds


def main(argv=None):
    parser = argparse.ArgumentParser(prog="cariboulite_radio_monitor", description=__doc__)
    parser.add_argument("--socket", default=default_socket_path(), metavar="PATH",
                        help="local status socket (default: %(default)s)")
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--once", action="store_true", help="print one JSON snapshot and exit")
    output.add_argument("--json", action="store_true", help="stream JSON snapshots; reconnect automatically")
    output.add_argument("--plain", action="store_true", help="stream readable snapshots without curses")
    parser.add_argument("--timeout", type=positive_seconds, default=5.0, metavar="SECONDS",
                        help="maximum wait with --once (default: %(default)s)")
    parser.add_argument("--registers", choices=("auto", "rx", "tx", "all"), default="auto",
                        help="register display: follow RX/TX state, compact RX or TX, or full I/Q inventory (default: auto)")
    args = parser.parse_args(argv)
    if not (args.once or args.json or args.plain) and not (sys.stdin.isatty() and sys.stdout.isatty()):
        parser.error("interactive mode needs a terminal; use --once, --json, or --plain")
    connection = SnapshotConnection(args.socket)
    try:
        if args.once or args.json or args.plain:
            return run_stream(connection, args.once, args.timeout, args.plain, args.registers)
        return curses.wrapper(run_interactive, connection, args.registers)
    except KeyboardInterrupt:
        return 0
    except BrokenPipeError:
        return 0
    finally:
        connection.close()


if __name__ == "__main__":
    sys.exit(main())
