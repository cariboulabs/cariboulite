"""Read menu 14's displayed counters without changing the radio application.

The comparison runner gives test_app a sufficiently large pseudo-terminal and
feeds its output here. This small terminal canvas handles the ANSI operations
used by ncurses, including updates split across reads. It is not a general
terminal implementation. The original menu layout omits detailed RX counters;
those measurements remain unavailable. Historical instrumented layouts must
contain every instrumentation row, so partial counters are never combined.
"""

import codecs
import re


class IncompleteScreenError(ValueError):
    """The current screen does not contain a complete menu 14 snapshot."""


class TerminalScreen:
    """Bounded ANSI text canvas; feed bytes incrementally, then call text()."""

    def __init__(self, rows=120, columns=240):
        if rows < 1 or columns < 1:
            raise ValueError("terminal dimensions must be positive")
        self.rows, self.columns = rows, columns
        self.row = self.column = 0
        self.saved_cursor = (0, 0)
        self.scroll_top, self.scroll_bottom = 0, rows - 1
        self.clear_generation = 0
        self._last_character = None
        self._canvas = [[" "] * columns for _ in range(rows)]
        self._state, self._escape = "text", ""
        self._decoder = codecs.getincrementaldecoder("utf-8")("replace")

    def text(self):
        return "\n".join("".join(row).rstrip() for row in self._canvas).rstrip()

    def _scroll(self, direction, count=1):
        count = min(count, self.scroll_bottom - self.scroll_top + 1)
        region = self._canvas[self.scroll_top:self.scroll_bottom + 1]
        blank = [[" "] * self.columns for _ in range(count)]
        region = region[count:] + blank if direction > 0 else blank + region[:-count]
        self._canvas[self.scroll_top:self.scroll_bottom + 1] = region

    def _index(self):
        if self.row == self.scroll_bottom:
            self._scroll(1)
        else:
            self.row = min(self.rows - 1, self.row + 1)

    def _erase_line(self, mode):
        if mode == 0:
            self._canvas[self.row][self.column:] = [" "] * (self.columns - self.column)
        elif mode == 1:
            self._canvas[self.row][:self.column + 1] = [" "] * (self.column + 1)
        elif mode == 2:
            self._canvas[self.row] = [" "] * self.columns

    def _print_character(self, character):
        if self.column == self.columns:
            self.column = 0
            self._index()
        self._canvas[self.row][self.column] = character
        self.column += 1
        self._last_character = character

    def _csi(self, raw, command):
        private = raw.startswith("?")
        raw = raw.lstrip("?<=>")
        # Intermediates identify commands we do not need (cursor shape etc.).
        if any(character not in "0123456789;" for character in raw):
            return
        parameters = [int(part) if part else 0 for part in raw.split(";")]
        first = parameters[0] if parameters else 0
        count = first or 1
        if command in "Hf":
            self.row = min(self.rows - 1, max(0, count - 1))
            self.column = min(self.columns - 1, max(0, (parameters[1] or 1) - 1)) if len(parameters) > 1 else 0
        elif command == "A":
            self.row = max(0, self.row - count)
        elif command in "Be":
            self.row = min(self.rows - 1, self.row + count)
        elif command in "Ca":
            self.column = min(self.columns - 1, self.column + count)
        elif command == "D":
            self.column = max(0, self.column - count)
        elif command in "EF":
            self.row = min(self.rows - 1, self.row + count) if command == "E" else max(0, self.row - count)
            self.column = 0
        elif command in "G`":
            self.column = min(self.columns - 1, max(0, count - 1))
        elif command == "d":
            self.row = min(self.rows - 1, max(0, count - 1))
        elif command == "b" and self._last_character is not None:
            # xterm/ncurses emits REP for repeated hexadecimal zero/F digits.
            # Bound malformed output while retaining all visible repetitions.
            for _ in range(min(count, self.rows * self.columns)):
                self._print_character(self._last_character)
        elif command == "K":
            self._erase_line(first)
        elif command == "J":
            if first in (2, 3):
                self._canvas = [[" "] * self.columns for _ in range(self.rows)]
                self.clear_generation += 1
            elif first == 0:
                self._erase_line(0)
                for row in range(self.row + 1, self.rows):
                    self._canvas[row] = [" "] * self.columns
            elif first == 1:
                self._erase_line(1)
                for row in range(self.row):
                    self._canvas[row] = [" "] * self.columns
        elif command == "P":
            line = self._canvas[self.row]
            count = min(count, self.columns - self.column)
            line[self.column:] = line[self.column + count:] + [" "] * count
        elif command == "@":
            line = self._canvas[self.row]
            count = min(count, self.columns - self.column)
            line[self.column:] = ([" "] * count + line[self.column:])[:self.columns - self.column]
        elif command == "X":
            count = min(count, self.columns - self.column)
            self._canvas[self.row][self.column:self.column + count] = [" "] * count
        elif command in "LM" and self.scroll_top <= self.row <= self.scroll_bottom:
            previous_top = self.scroll_top
            self.scroll_top = self.row
            self._scroll(-1 if command == "L" else 1, count)
            self.scroll_top = previous_top
        elif command in "ST":
            self._scroll(1 if command == "S" else -1, count)
        elif command == "r" and not private:
            bottom = parameters[1] or self.rows if len(parameters) > 1 else self.rows
            if 1 <= count < bottom <= self.rows:
                self.scroll_top, self.scroll_bottom = count - 1, bottom - 1
                self.row = self.column = 0
        elif command == "s":
            self.saved_cursor = (self.row, self.column)
        elif command == "u":
            self.row, self.column = self.saved_cursor
        elif command == "h" and private and first in (47, 1047, 1049):
            # We only need the active application screen, not its predecessor.
            self._canvas = [[" "] * self.columns for _ in range(self.rows)]
            self.row = self.column = 0
            self.clear_generation += 1
        # SGR, cursor visibility, keypad and other modes have no text effect.

    def feed(self, data):
        if isinstance(data, bytes):
            data = self._decoder.decode(data)
        for character in data:
            if self._state == "escape":
                self._state = "text"
                if character == "[":
                    self._state, self._escape = "csi", ""
                elif character in "]P^_":
                    self._state = "string"
                elif character in "()*+-%#":
                    self._state = "charset"
                elif character == "7":
                    self.saved_cursor = (self.row, self.column)
                elif character == "8":
                    self.row, self.column = self.saved_cursor
                elif character in "DE":
                    self._index()
                    if character == "E":
                        self.column = 0
                elif character == "M":
                    if self.row == self.scroll_top:
                        self._scroll(-1)
                    else:
                        self.row = max(0, self.row - 1)
                continue
            if self._state == "charset":
                self._state = "text"
                continue
            if self._state in ("string", "string_escape"):
                if character == "\a" or (self._state == "string_escape" and character == "\\"):
                    self._state = "text"
                elif character == "\x1b":
                    self._state = "string_escape"
                else:
                    self._state = "string"
                continue
            if self._state == "csi":
                if "@" <= character <= "~":
                    self._csi(self._escape, character)
                    self._state = "text"
                elif character == "\x1b":
                    self._state = "escape"
                elif len(self._escape) < 128:
                    self._escape += character
                else:
                    self._state = "text"
                continue
            if character == "\x1b":
                self._state = "escape"
            elif character == "\r":
                self.column = 0
            elif character in "\n\v\f":
                self._index()
            elif character == "\b":
                self.column = max(0, self.column - 1)
            elif character == "\t":
                self.column = min(self.columns - 1, ((self.column // 8) + 1) * 8)
            elif character >= " ":
                self._print_character(character)


def _match(pattern, text, description):
    match = re.search(pattern, text, re.MULTILINE)
    if not match:
        raise IncompleteScreenError("missing or incomplete " + description)
    return match


def _integers(match, names):
    return dict(zip(names, (int(value) for value in match.groups())))


def _parse_rx_diagnostics(text):
    """Read the complete diagnostics block used by historical menu 14 builds."""
    audio = _match(r"^audio_fifo: (\d+)/(\d+) \([\d.]+%\) min:(\d+) max:(\d+) puts:(\d+) gets:(\d+) drops:(\d+)\n"
                   r"\s+timeouts put:(\d+) get:(\d+) shutdown put:(\d+) get:(\d+) \(FIFO counters reset by X\)",
                   text, "audio FIFO")
    aq = _integers(audio, ("depth", "capacity", "min_depth", "max_depth", "puts", "gets", "drops",
                           "timeouts_put", "timeouts_get", "shutdown_puts", "shutdown_gets"))
    if "ALSA playback counters: unavailable" in text:
        playback = {"valid": False}
    else:
        alsa = _match(r"^ALSA: written:(\d+) submitted:(\d+) calls:(\d+) partial:(\d+) again:(\d+) zero:(\d+)\n"
                      r"\s+underruns:(\d+) recovery:(\d+)/(\d+) failed:(\d+) write_errors:(\d+) last:(-?\d+) \(lifetime\)",
                      text, "ALSA playback counters")
        playback = _integers(alsa, ("frames_written", "frames_requested", "write_calls", "partial_writes",
                                    "would_block", "zero_writes", "underruns", "recovery_successes",
                                    "recovery_attempts", "recovery_failures", "write_errors", "last_error"))
        playback["valid"] = True
    reads = _match(r"^RX reads: calls:(\d+) samples:(\d+) short:(\d+) zero:(\d+) errors:(\d+) last:(-?\d+) \(lifetime\)",
                   text, "RX reader counters")
    reader = _integers(reads, ("calls", "samples", "short_reads", "zero_reads", "errors", "last_error"))
    workers = {}
    policies = {"SCHED_OTHER": 0, "SCHED_FIFO": 1, "SCHED_RR": 2,
                "SCHED_BATCH": 3, "SCHED_IDLE": 5, "SCHED_DEADLINE": 6}
    for name in ("reader", "demod", "audio"):
        basic = _match(r"^\s+" + name + r"\s+created:([01]) alive:([01]) last:(-?\d+) (.+)$", text, name + " worker")
        worker = {"created": bool(int(basic[1])), "alive": bool(int(basic[2])), "last_error": int(basic[3])}
        if basic[4] == "scheduling:unavailable":
            worker["scheduling"] = {"valid": False}
        else:
            schedule = _match(r"^(\w+)/(-?\d+) cpu:(\S+) req:FIFO/(-?\d+) cpu:(-?\d+) err RT:(-?\d+) "
                               r"fallback:(-?\d+) affinity:(-?\d+) mlock:(tried|skipped)/(-?\d+) query:(-?\d+)$",
                               basic[4], name + " scheduling")
            affinity = schedule[3]
            worker["scheduling"] = {
                "valid": True, "policy_name": schedule[1], "policy": policies.get(schedule[1]),
                "priority": int(schedule[2]), "affinity": affinity.removesuffix(",..."),
                "affinity_truncated": affinity.endswith(",..."),
                "requested_priority": int(schedule[4]), "requested_cpu": int(schedule[5]),
                "applied_cpu": None, "rt_error": int(schedule[6]), "fallback_error": int(schedule[7]),
                "affinity_error": int(schedule[8]), "mlock_attempted": schedule[9] == "tried",
                "mlock_error": int(schedule[10]), "query_error": int(schedule[11]),
            }
        workers[name] = worker
    return {"audio_queue": aq, "playback": playback, "reader": reader,
            "workers": workers, "instrumentation_available": True}


def parse_test_app_screen(text):
    """Return backend-shaped RX statistics from a complete menu 14 screen.

    Frequency values are the requested settings shown by test_app, rounded to
    one Hz. Register observations are included separately; they are not an
    atomic hardware snapshot. applied_cpu is unavailable from this display.
    Detailed RX diagnostics are unavailable in the original menu layout.
    """
    header = _match(r"^(HiF/RF24|S1G/RF09) \[T\] TX \[R\] RX", text, "menu 14 header")
    channel = "RF24" if header[1] == "HiF/RF24" else "RF09"
    rate = _match(r"^TX Power: (-?\d+) dBm  TX/RX (\d+) MS/s", text, "sample rate")
    frequency = _match(r"^\[F\] TX ([\d.]+) MHz  \[G\] RX ([\d.]+) MHz", text, "frequency")
    queue = _match(r"^Linux RX FIFO: (\d+) samples/frame \(10 ms\)\n"
                   r"\s+depth: (\d+)/(\d+) \([\d.]+%\), min:(\d+) max:(\d+)\n"
                   r"\s+puts:(\d+) gets:(\d+) drops:(\d+) tO_put:(\d+) tO_get:(\d+)\n"
                   r"\s+rate: puts ([\d.]+)/s, gets ([\d.]+)/s", text, "RX FIFO")
    q = dict(zip(("frame_samples", "depth", "capacity", "min_depth", "max_depth",
                  "puts", "gets", "drops", "timeouts_put", "timeouts_get"),
                 (int(queue[index]) for index in range(1, 11))))
    q["rate_puts_s"], q["rate_gets_s"] = float(queue[11]), float(queue[12])
    # A single extension marker requires the entire historical diagnostics
    # block. Missing all markers identifies the original menu layout; it does
    # not establish healthy playback or worker operation.
    extended = re.search(r"^(?:audio_fifo\b|ALSA\b|RX reads\b|\s+(?:reader|demod|audio)\s+created\b)",
                         text, re.MULTILINE)
    if extended:
        diagnostics = _parse_rx_diagnostics(text)
    else:
        diagnostics = {"audio_queue": {"valid": False}, "playback": {"valid": False},
                       "reader": {"valid": False},
                       "workers": {name: {"valid": False} for name in ("reader", "demod", "audio")},
                       "instrumentation_available": False}
    mode = _match(r"^RX mode: (NBFM|WBFM mono)  \[M\] cycle", text, "RX mode")
    squelch = _match(r"^Squelch: \[N\] noise (ON|OFF|N/A \(WBFM\))  \[C\] carrier (ON|OFF)  audio (IDLE|OPEN|MUTED)$",
                     text, "squelch status")
    noise = {"valid": False}
    if mode[1] == "NBFM":
        levels = _match(r"^Noise RMS (--|[\d.]+)  (?:detector (OPEN|CLOSED)  )?\[S\] open ([\d.]+) / close ([\d.]+)$",
                        text, "noise squelch levels")
        _match(r"^Higher noise thresholds admit noisier signals; levels can change during RX\.$",
               text, "last NBFM status row")
        noise = {"valid": levels[1] != "--", "rms": None if levels[1] == "--" else float(levels[1]),
                 "detector_open": None if levels[2] is None else levels[2] == "OPEN",
                 "open_rms": float(levels[3]), "close_rms": float(levels[4])}
    source = _match(r"^\s+RX SOURCE\s+: (\d+)\s+//", text, "FPGA RX source")
    direction = _match(r"^\s+SMI DIRECTION: (\d+)\s+//", text, "SMI direction")
    registers = {}
    for register in ("STATE", "RXDFE", "RXBWC", "AGCC", "AGCS", "RSSI"):
        observed = _match(channel + "-" + register + r"\s*:0x([0-9A-Fa-f]{2})", text, channel + " " + register)
        registers[register] = int(observed[1], 16)
    if registers["RSSI"] > 127:
        registers["RSSI_dBm"] = registers["RSSI"] - 256
    else:
        registers["RSSI_dBm"] = registers["RSSI"]
    return {
        "source": {"kind": "live", "label": "test_app menu 14 terminal"},
        "channel": channel, "rx_mode": mode[1],
        "config": {"sample_rate_hz": int(rate[2]) * 1000000,
                   "requested_frequency_hz": round(float(frequency[2]) * 1000000),
                   "tx_frequency_hz": round(float(frequency[1]) * 1000000), "tx_power_dbm": int(rate[1])},
        "rx": {"running": squelch[3] != "IDLE", "queue": q, **diagnostics,
               "noise_enabled": squelch[1] == "ON", "carrier_enabled": squelch[2] == "ON",
               "gate_open": squelch[3] == "OPEN", "noise": noise},
        "terminal_observations": {"fpga_rx_source": int(source[1]), "smi_direction": int(direction[1]),
                                  "registers": registers},
    }
