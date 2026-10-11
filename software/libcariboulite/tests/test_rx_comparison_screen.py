#!/usr/bin/env python3
"""Hardware-free checks for reconstructing menu 14 comparison snapshots."""

from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from rx_comparison_screen import IncompleteScreenError, TerminalScreen, parse_test_app_screen


SCREEN = """HiF/RF24 [T] TX [R] RX [M] RX mode [L] loopback [1/2/4] MS/s [Q] quit [X] stats
TX Power: 13 dBm  TX/RX 1 MS/s
[F] TX 430.100000 MHz  [G] RX 430.125000 MHz
AT86RF215 Modem Status Registers:
    RF09-RSSI  :0x00  RF24-RSSI  :0xA4
    RF09-RXDFE :0x00  RF24-RXDFE :0x04
    RF09-RXBWC :0x00  RF24-RXBWC :0x10
    RF09-AGCC  :0x00  RF24-AGCC  :0x01
    RF09-AGCS  :0x00  RF24-AGCS  :0x15
    RF09-STATE :0x00  RF24-STATE :0x05
    RX SOURCE    : 1    // 0=RF09 1=RF24; unused in TX
    SMI DIRECTION: 1    // 0=TX   1=RX
Linux TX FIFO: 10000 samples/frame (10 ms)
    depth: 0/64 (0%), min:0 max:0
    puts:0 gets:0 drops:0 tO_put:0 tO_get:0
    rate: puts 0.0/s, gets 0.0/s  (expect ~100 fps @ 10ms)
Linux RX FIFO: 10000 samples/frame (10 ms)
    depth: 1/64 (2%), min:0 max:3
    puts:1007 gets:1006 drops:2 tO_put:4 tO_get:5
    rate: puts 100.1/s, gets 99.9/s  (expect ~100 fps @ 10ms)
audio_fifo: 12/24 (50%) min:0 max:17 puts:1006 gets:994 drops:3
    timeouts put:3 get:6 shutdown put:0 get:0 (FIFO counters reset by X)
ALSA: written:477120 submitted:477121 calls:996 partial:1 again:1 zero:2
    underruns:7 recovery:6/8 failed:2 write_errors:9 last:-32 (lifetime)
RX reads: calls:1010 samples:10070000 short:1 zero:2 errors:3 last:-5 (lifetime)
    reader created:1 alive:1 last:0 SCHED_OTHER/0 cpu:2 req:FIFO/70 cpu:2 err RT:1 fallback:0 affinity:0 mlock:tried/12 query:0
    demod  created:1 alive:1 last:0 SCHED_OTHER/0 cpu:1 req:FIFO/55 cpu:1 err RT:1 fallback:0 affinity:0 mlock:tried/12 query:0
    audio  created:1 alive:1 last:-5 SCHED_OTHER/0 cpu:0 req:FIFO/46 cpu:0 err RT:1 fallback:0 affinity:0 mlock:tried/12 query:0
RX mode: NBFM  [M] cycle (TX/RX/loopback stopped); TX mode: NBFM
Squelch: [N] noise ON  [C] carrier OFF  audio MUTED
Noise RMS 0.512  detector CLOSED  [S] open 0.200 / close 0.300
Higher noise thresholds admit noisier signals; levels can change during RX.

TX/RX rate selected. [T] starts TX; [R] starts RX.
"""


def paint(text):
    return "\x1b[H\x1b[2J" + text.replace("\n", "\r\n")


class ComparisonScreenTests(unittest.TestCase):
    def test_parse_nonzero_counters_and_settings(self):
        value = parse_test_app_screen(SCREEN)
        self.assertEqual(value["channel"], "RF24")
        self.assertEqual(value["config"]["sample_rate_hz"], 1000000)
        self.assertEqual(value["config"]["requested_frequency_hz"], 430125000)
        rx = value["rx"]
        self.assertTrue(rx["running"])
        self.assertFalse(rx["gate_open"])
        self.assertEqual(rx["queue"]["puts"], 1007)
        self.assertEqual(rx["queue"]["drops"], 2)
        self.assertEqual(rx["audio_queue"]["drops"], 3)
        self.assertEqual(rx["audio_queue"]["timeouts_get"], 6)
        self.assertEqual(rx["playback"]["underruns"], 7)
        self.assertEqual(rx["playback"]["recovery_successes"], 6)
        self.assertEqual(rx["playback"]["recovery_attempts"], 8)
        self.assertEqual(rx["playback"]["last_error"], -32)
        self.assertEqual(rx["reader"]["samples"], 10070000)
        self.assertEqual(rx["reader"]["errors"], 3)
        schedule = rx["workers"]["reader"]["scheduling"]
        self.assertEqual(schedule["policy"], 0)
        self.assertEqual(schedule["policy_name"], "SCHED_OTHER")
        self.assertEqual(schedule["priority"], 0)
        self.assertEqual(schedule["affinity"], "2")
        self.assertEqual(schedule["requested_priority"], 70)
        self.assertEqual(schedule["rt_error"], 1)
        self.assertEqual(schedule["mlock_error"], 12)
        self.assertTrue(schedule["mlock_attempted"])
        self.assertIsNone(schedule["applied_cpu"])
        self.assertEqual(value["terminal_observations"]["registers"]["RSSI_dBm"], -92)

    def test_fragmented_full_repaint(self):
        screen = TerminalScreen()
        raw = paint(SCREEN).encode()
        for byte in raw:
            screen.feed(bytes([byte]))
        self.assertEqual(parse_test_app_screen(screen.text()), parse_test_app_screen(SCREEN))
        self.assertEqual(screen.clear_generation, 1)

    def test_delta_repaint_and_erasure(self):
        screen = TerminalScreen()
        screen.feed(paint(SCREEN))
        line = SCREEN.splitlines().index("    depth: 1/64 (2%), min:0 max:3") + 1
        screen.feed(f"\x1b[{line};1H\x1b[2K    depth: 0/64 (0%), min:0 max:4")
        alsa_line = next(index for index, row in enumerate(SCREEN.splitlines(), 1) if row.startswith("ALSA:"))
        screen.feed(f"\x1b[{alsa_line};1HALSA: written:99 submitted:99 calls:1 partial:0 again:0 zero:0\x1b[K")
        value = parse_test_app_screen(screen.text())
        self.assertEqual(value["rx"]["queue"]["depth"], 0)
        self.assertEqual(value["rx"]["queue"]["max_depth"], 4)
        self.assertEqual(value["rx"]["playback"]["frames_written"], 99)

    def test_cleared_incomplete_frame_rejected(self):
        screen = TerminalScreen()
        screen.feed(paint(SCREEN))
        screen.feed(paint(SCREEN.split("RX reads:")[0]))
        with self.assertRaises(IncompleteScreenError):
            parse_test_app_screen(screen.text())

    def test_truncated_row_rejected(self):
        with self.assertRaises(IncompleteScreenError):
            parse_test_app_screen(SCREEN.replace("mlock:tried/12 query:0", "mlock:tried/12", 1))

    def test_unavailable_is_distinct_from_zero(self):
        value = SCREEN.replace("ALSA: written:477120 submitted:477121 calls:996 partial:1 again:1 zero:2\n"
                               "    underruns:7 recovery:6/8 failed:2 write_errors:9 last:-32 (lifetime)",
                               "ALSA playback counters: unavailable")
        value = value.replace("SCHED_OTHER/0 cpu:2 req:FIFO/70 cpu:2 err RT:1 fallback:0 affinity:0 mlock:tried/12 query:0",
                              "scheduling:unavailable")
        rx = parse_test_app_screen(value)["rx"]
        self.assertEqual(rx["playback"], {"valid": False})
        self.assertEqual(rx["workers"]["reader"]["scheduling"], {"valid": False})

    def test_wbfm_and_idle(self):
        value = SCREEN.replace("RX mode: NBFM", "RX mode: WBFM mono")
        value = value.replace("noise ON  [C] carrier OFF  audio MUTED", "noise N/A (WBFM)  [C] carrier OFF  audio IDLE")
        rx = parse_test_app_screen(value)["rx"]
        self.assertFalse(rx["noise_enabled"])
        self.assertFalse(rx["running"])
        self.assertEqual(rx["noise"], {"valid": False})

    def test_cursor_scroll_and_control_strings(self):
        screen = TerminalScreen(rows=4, columns=20)
        screen.feed("\x1b]0;ignored title\x07first\r\nsecond\r\nthird\r\nfourth\r\nfifth")
        self.assertEqual(screen.text(), "second\nthird\nfourth\nfifth")
        screen.feed("\x1b[2;1H\x1b7\x1b[2Knew\x1b8\x1b[2C!")
        self.assertEqual(screen.text().splitlines()[1], "ne!")
        screen.feed("\x1b[2;1H\x1b[1P\x1b[1@")
        self.assertEqual(screen.text().splitlines()[1], " e!")

    def test_fragmented_unicode_and_ignored_attributes(self):
        screen = TerminalScreen(rows=2, columns=30)
        raw = "\x1b[31mµA\x1b[0m\x1b(B".encode()
        for byte in raw:
            screen.feed(bytes([byte]))
        self.assertEqual(screen.text(), "µA")

    def test_fragmented_repeat_previous_printable_character(self):
        screen = TerminalScreen(rows=4, columns=40)
        raw = b"TX_I:0x0\x1b[7b  TX_Q:0xF\x1b[6bC\r\nabc\x1b[b\x1b[2b"
        for byte in raw:
            screen.feed(bytes([byte]))
        self.assertEqual(screen.text(), "TX_I:0x00000000  TX_Q:0xFFFFFFFC\nabcccc")
        screen.feed(b"\x1b[2;1H9\x1b[3b\x1b[2K\x1b[1;1H\x1b[b")
        self.assertTrue(screen.text().startswith("9X_I:"))

    def test_repeat_previous_character_preserves_counter_values(self):
        screen = TerminalScreen()
        raw = paint(SCREEN).replace("samples:10070000", "samples:10070\x1b[3b")
        screen.feed(raw)
        self.assertEqual(parse_test_app_screen(screen.text())["rx"]["reader"]["samples"], 10070000)


if __name__ == "__main__":
    unittest.main()
