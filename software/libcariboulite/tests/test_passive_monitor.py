#!/usr/bin/env python3
"""Test the standalone passive client using an isolated Python status publisher.

No test_app code or radio hardware is accessed. Integration tests use temporary
Unix-domain sockets, and the interactive test uses a private pseudo terminal.
"""
import importlib.util
import io
import json
import os
from pathlib import Path
import select
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from contextlib import redirect_stdout
from unittest import mock


ROOT = Path(__file__).resolve().parents[3]
CLIENT = ROOT / "software/libcariboulite/tools/cariboulite_radio_monitor.py"
DEMO = CLIENT.with_name("monitor_demo_source.py")
MAX_PACKET = 16384
sys.dont_write_bytecode = True
sys.path.insert(0, str(CLIENT.parent))
import at86rf215_monitor as registers

spec = importlib.util.spec_from_file_location("cariboulite_radio_monitor_test", CLIENT)
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


def snapshot(sequence=1, **extra):
    value = {"version": 1, "session": "test-session", "sequence": sequence,
             "monotonic_s": time.monotonic(), "owner_pid": os.getpid(),
             "source": {"kind": "simulation", "label": "isolated test fixture"}}
    value.update(extra)
    return value


def encoded(value):
    return json.dumps(value, separators=(",", ":")).encode()


def rx_instrumentation():
    """Independent nonzero fixture: reveal misleading zero/error displays."""
    scheduling = {"valid": True, "requested_priority": 80, "requested_cpu": 2,
                  "applied_cpu": 2, "policy": 0, "policy_name": "SCHED_OTHER",
                  "priority": 0, "affinity": "2", "rt_error": 1,
                  "fallback_error": 0, "affinity_error": 0, "mlock_error": 12,
                  "query_error": 0, "mlock_attempted": True}
    return {
        "audio_queue": {"capacity": 24, "depth": 12, "puts": 100, "gets": 88,
                        "drops": 3, "timeouts_put": 3, "timeouts_get": 2,
                        "min_depth": 0, "max_depth": 17,
                        "shutdown_puts": 1, "shutdown_gets": 2},
        "playback": {"valid": True, "write_calls": 91, "frames_requested": 48000,
                     "frames_written": 47000, "partial_writes": 4, "would_block": 5,
                     "zero_writes": 6, "underruns": 7, "recovery_attempts": 8,
                     "recovery_successes": 6, "recovery_failures": 2,
                     "write_errors": 9, "last_error": -32},
        "reader": {"calls": 110, "samples": 1000000, "short_reads": 3,
                   "zero_reads": 2, "errors": 4, "last_error": -5},
        "workers": {name: {"alive": True, "created": True, "last_error": -5,
                           "scheduling": dict(scheduling)}
                    for name in ("reader", "demod", "audio")},
    }


def tx_instrumentation():
    scheduling = {"valid": True, "requested_priority": 60, "requested_cpu": 0,
                  "applied_cpu": 0, "policy_name": "SCHED_OTHER", "priority": 0,
                  "affinity": "0", "rt_error": 1, "fallback_error": 0,
                  "affinity_error": 0, "mlock_error": 12, "query_error": 0,
                  "mlock_attempted": True}
    return {
        "running": True, "requested": True, "initialized": True, "generation": 5,
        "writer": {"calls": 151, "samples": 1234567, "partial_writes": 7,
                   "zero_writes": 3, "would_block": 4, "errors": 2,
                   "last_error": -11, "written_sequence": 123},
        "capture": {"valid": True, "read_calls": 42, "frames_read": 20160,
                    "overruns": 4, "recovery_attempts": 7, "recovery_successes": 6,
                    "recovery_failures": 1, "read_errors": 2, "last_error": -32},
        "workers": {name: {"alive": True, "created": True, "last_error": -5,
                           "scheduling": dict(scheduling)} for name in ("producer", "writer")},
        "fault": None,
    }


def hardware_tx_instrumentation():
    tx = tx_instrumentation()
    tx.update(hardware_paced=True,
              smi_timing={"valid": True, "write_setup": 1, "write_strobe": 4,
                          "write_hold": 2, "write_pace": 1},
              driver={"valid": True, "fifo_queued_bytes": 32768,
                      "fifo_limit_bytes": 65536, "completed_periods": 777,
                      "underrun_periods": 3, "dma_running": True, "bounded_fifo": True},
              audio_buffer={"valid": True, "queued_frames": 1900, "capacity_frames": 16384,
                            "target_frames": 1920, "correction_ppm": 4033.5, "clock_valid": True,
                            "underflows": 2, "overflows": 4, "cue_discard_frames": 960,
                            "last_error": -5})
    tx["workers"]["capture"] = {"created": True, "alive": True, "last_error": -5,
                                  "scheduling": dict(tx["workers"]["producer"]["scheduling"],
                                                     requested_cpu=2, applied_cpu=2, affinity="2")}
    return tx


class FixturePublisher:
    """An isolated socket fixture; it never accepts or executes commands."""

    def __init__(self, path, latest=None):
        self.path = path
        self.latest = latest
        self.received = []
        self.peers = []
        self.lock = threading.Lock()
        self.connected = threading.Event()
        self.stopping = threading.Event()
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.server.bind(str(path))
        self.server.listen(4)
        self.server.setblocking(False)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stopping.is_set():
            with self.lock:
                readable_sockets = [self.server, *self.peers]
            readable, _, _ = select.select(readable_sockets, [], [], .02)
            with self.lock:
                for peer in readable:
                    if peer is self.server:
                        accepted, _ = self.server.accept()
                        accepted.setblocking(False)
                        self.peers.append(accepted)
                        if self.latest is not None:
                            accepted.send(self.latest)
                        self.connected.set()
                    else:
                        try:
                            message = peer.recv(MAX_PACKET)
                        except ConnectionResetError:
                            message = b""
                        if message:
                            self.received.append(message)
                        else:
                            self.peers.remove(peer)
                            peer.close()

    def publish(self, value):
        payload = encoded(value) if isinstance(value, dict) else value
        with self.lock:
            self.latest = payload
            for peer in self.peers:
                peer.send(payload)

    def stop(self):
        self.stopping.set()
        self.thread.join(timeout=1)
        if self.thread.is_alive():
            raise RuntimeError("fixture publisher failed to stop")
        with self.lock:
            for peer in self.peers:
                peer.close()
            self.peers.clear()
        self.server.close()
        self.path.unlink(missing_ok=True)


class IQRegisterDisplayTests(unittest.TestCase):
    """Check decodes against the datasheet, independently of the demo bytes."""

    COMMON = ("RST", "CFG", "CLKO", "BMDVC", "XOC", "IQIFC0", "IQIFC1",
              "IQIFC2", "PN", "VN")
    RADIO = ("IRQS", "IRQM", "AUXS", "STATE", "CMD", "CS", "CCF0L", "CCF0H",
             "CNL", "CNM", "RXBWC", "RXDFE", "AGCC", "AGCS", "RSSI", "EDC",
             "EDD", "EDV", "RNDV", "TXCUTC", "TXDFE", "PAC", "PADFE", "PLL",
             "PLLCF", "TXCI", "TXCQ", "TXDACI", "TXDACQ")
    RX_COMMON = ("CFG", "CLKO", "RST", "IQIFC0", "IQIFC1", "IQIFC2")
    RX_RADIO = ("RSSI", "RXDFE", "RXBWC", "AGCC", "AGCS", "PLL",
                "PLLCF", "IRQM", "IRQS", "STATE")
    TX_COMMON = RX_COMMON
    TX_RADIO = ("TXDFE", "TXCUTC", "PAC", "PADFE", "AUXS", "TXCI", "TXCQ",
                "TXDACI", "TXDACQ", "PLL", "PLLCF", "IRQM", "IRQS", "STATE")

    @staticmethod
    def hardware(common=None, rf09=None, rf24=None):
        return {name: {"valid": True, "registers": values or {}}
                for name, values in (("common", common), ("rf09", rf09), ("rf24", rf24))}

    def row(self, rows, bank, name):
        matches = [row for row in rows if row.startswith(f"{bank} {name}=")]
        self.assertEqual(len(matches), 1, (bank, name, rows))
        self.assertNotIn("\n", matches[0])
        return matches[0]

    def test_register_inventory_covers_iq_radio_and_shared_controls(self):
        self.assertEqual(tuple(registers.COMMON_REGISTERS), self.COMMON)
        self.assertEqual(tuple(registers.RADIO_REGISTERS), self.RADIO)
        hardware = self.hardware(common={name: 0 for name in self.COMMON},
                                 rf24={name: 0 for name in self.RADIO})
        rows = registers.format_register_rows(hardware, "RF24", view="all")
        for bank, names in (("RF", self.COMMON), ("RF24", self.RADIO)):
            for name in names:
                with self.subTest(bank=bank, name=name):
                    self.assertIn("=0x00", self.row(rows, bank, name))
        self.assertEqual(len(rows), 39)
        self.assertFalse(any("BBC" in row for row in rows))

    def test_register_addresses_preserve_shared_irq_and_radio_map_gaps(self):
        expected = {"RF_RST": 0x0005, "RF_IQIFC0": 0x000A,
                    "RF_IQIFC1": 0x000B, "RF_IQIFC2": 0x000C,
                    "RF_PN": 0x000D, "RF_VN": 0x000E,
                    "RF09_IRQS": 0x0000, "RF24_IRQS": 0x0001,
                    "RF09_IRQM": 0x0100, "RF24_IRQM": 0x0200,
                    "RF09_AGCC": 0x010B, "RF24_AGCC": 0x020B,
                    "RF09_AGCS": 0x010C, "RF24_AGCS": 0x020C,
                    "RF09_PADFE": 0x0116, "RF24_PADFE": 0x0216,
                    "RF09_PLL": 0x0121, "RF24_PLL": 0x0221,
                    "RF09_PLLCF": 0x0122, "RF24_PLLCF": 0x0222,
                    "RF09_TXDACQ": 0x0128, "RF24_TXDACQ": 0x0228}
        for name, address in expected.items():
            with self.subTest(name=name):
                self.assertEqual(registers.REGISTER_ADDRESSES[name], address)
        self.assertEqual(len(registers.REGISTER_ADDRESSES), 10 + 2 * 29)

    def test_requested_agc_example_is_two_independent_register_rows(self):
        rows = registers.format_register_rows(
            self.hardware(rf24={"AGCC": 0x01, "AGCS": 0x15, "RXBWC": 0x10}), "RF24")
        self.assertEqual(self.row(rows, "RF24", "AGCC"),
                         "RF24 AGCC=0x01 AGCI=0(FILT) AVGS=0(8) RST=0 FRZS=0 FRZC=0 EN=1")
        self.assertEqual(self.row(rows, "RF24", "AGCS"),
                         "RF24 AGCS=0x15 TGT=0(-21dBFS) GCW=21/21(MAX)")

    def test_default_rx_inventory_follows_requested_register_order(self):
        hardware = self.hardware(common={name: 0 for name in self.COMMON},
                                 rf09={name: 0 for name in self.RADIO},
                                 rf24={name: 0 for name in self.RADIO})
        for channel in ("RF09", "RF24"):
            with self.subTest(channel=channel):
                rows = registers.format_register_rows(hardware, channel)
                explicit = registers.format_register_rows(hardware, channel, view="rx")
                self.assertEqual(rows, explicit)
                names = tuple(row.split(" ")[1].split("=")[0] for row in rows)
                self.assertEqual(names, self.RX_COMMON + self.RX_RADIO)
                self.assertEqual(len(rows), 16)
                self.assertTrue(all(row.startswith("RF ") for row in rows[:6]))
                self.assertTrue(all(row.startswith(f"{channel} ") for row in rows[6:]))
                self.assertFalse(any(" PAC=" in row or " TXDFE=" in row or " TXDAC" in row for row in rows))

    def test_rx_bit_fields_follow_descending_datasheet_bit_order(self):
        hardware = self.hardware(
            common={"CFG": 0x0D, "CLKO": 0x1A, "RST": 0,
                    "IQIFC0": 0xAB, "IQIFC1": 0xD3, "IQIFC2": 0x80},
            rf24={"AGCC": 0x6B, "AGCS": 0xB6, "RXBWC": 0x26,
                  "RXDFE": 0x64, "PLL": 0x12,
                  "IRQM": 0x29, "IRQS": 0x12})
        expected = {
            ("RF", "CFG"): "IRQMM=1 IRQP=1 DRV=1(4mA)",
            ("RF", "CLKO"): "DRV=3(8mA) OS=2(32MHz)",
            ("RF", "IQIFC0"): "EXTLB=1 SF=0 DRV=2(3mA) CMV=2(250mV) CMV1V2=1(1.2V;CMV ignored) EEC=1",
            ("RF", "IQIFC1"): "FAILSF=1 CHPM=5(BBRF24) SKEWDRV=3(4.906ns)",
            ("RF24", "AGCC"): "AGCI=1(UNFILT) AVGS=2(32) RST=1 FRZS=0 FRZC=1 EN=1",
            ("RF24", "AGCS"): "TGT=5(-36dBFS) GCW=22/22(MAX)",
            ("RF24", "RXDFE"): "RCUT=3(0.75*Fs/2) SR=4(1000kHz)",
            ("RF24", "RXBWC"): "IFI=1 IFS=0 BW=6(630kHz,IFbase=1000kHz)",
            ("RF24", "PLL"): "LBW=1(-15%)(RF09-only) LS=1(LOCKED)",
            ("RF24", "IRQM"): "IQIFSF=1 TRXERR=0 BATLOW=1 EDC=0 TRXRDY=0 WAKEUP=1",
            ("RF24", "IRQS"): "IQIFSF=0 TRXERR=1 BATLOW=0 EDC=0 TRXRDY=1 WAKEUP=0 (cached events)",
        }
        for view in ("rx", "all"):
            rows = registers.format_register_rows(hardware, "RF24", view=view)
            for (bank, name), fields in expected.items():
                with self.subTest(view=view, bank=bank, name=name):
                    self.assertEqual(self.row(rows, bank, name).split(" ", 2)[2], fields)

    def test_compact_tx_inventory_selects_only_the_requested_radio(self):
        self.assertEqual(tuple(registers.TX_COMMON_REGISTERS), self.TX_COMMON)
        self.assertEqual(tuple(registers.TX_RADIO_REGISTERS), self.TX_RADIO)
        hardware = self.hardware(common={name: 0 for name in self.COMMON},
                                 rf09={name: 0x01 for name in self.RADIO},
                                 rf24={name: 0x04 for name in self.RADIO})
        for channel, selected, suppressed, byte in (
                ("RF24", "RF24", "RF09", 0x04),
                ("HiF (RF24)", "RF24", "RF09", 0x04),
                ("RF09", "RF09", "RF24", 0x01),
                ("S1G (RF09)", "RF09", "RF24", 0x01)):
            with self.subTest(channel=channel):
                rows = registers.format_register_rows(hardware, channel, view="tx")
                names = tuple(row.split(" ")[1].split("=")[0] for row in rows)
                self.assertEqual(names, self.TX_COMMON + self.TX_RADIO)
                self.assertEqual(len(rows), 20)
                self.assertTrue(all(row.startswith("RF ") for row in rows[:6]))
                self.assertTrue(all(row.startswith(f"{selected} ") for row in rows[6:]))
                self.assertTrue(all(f"=0x{byte:02X}" in row for row in rows[6:]))
                self.assertFalse(any(row.startswith(f"{suppressed} ") for row in rows))
                self.assertFalse(any(" AGCC=" in row or " RXDFE=" in row for row in rows))

    def test_tx_byte_decodes_follow_descending_datasheet_bit_order(self):
        # Independent examples from the TX frontend, PA, calibration, PLL and
        # status register tables. Reserved bits intentionally vary so they
        # cannot leak into the fields or change their datasheet order.
        hardware = self.hardware(rf09={
            "TXDFE": 0x74, "TXCUTC": 0x85, "PAC": 0x66, "PADFE": 0xDF,
            "AUXS": 0xDD, "TXCI": 0xAF, "TXCQ": 0xE5,
            "TXDACI": 0xC9, "TXDACQ": 0x56, "PLL": 0x22,
            "PLLCF": 0xD5, "IRQM": 0x29, "IRQS": 0x12, "STATE": 0xFC})
        expected = {
            "TXDFE": "RCUT=3(0.75*Fs/2) DM=1 SR=4(1000kHz)",
            "TXCUTC": "PARAMP=2(16us) LPFCUT=5(250kHz)",
            "PAC": "PACUR=3(FULL) TXPWR=6",
            "PADFE": "PADFE=3(TX/RX+LNA_BYPASS)",
            "AUXS": "EXTLNABYP=1 AGCMAP=2(LNA12dB) AVEXT=1 AVEN=1 AVS=1 PAVC=1(2.2V)",
            "TXCI": "DCOI=47", "TXCQ": "DCOQ=37",
            "TXDACI": "ENTXDACID=1 TXDACID=73",
            "TXDACQ": "ENTXDACQD=0 TXDACQD=86",
            "PLL": "LBW=2(+15%) LS=1(LOCKED)", "PLLCF": "CF=21",
            "IRQM": "IQIFSF=1 TRXERR=0 BATLOW=1 EDC=0 TRXRDY=0 WAKEUP=1",
            "IRQS": "IQIFSF=0 TRXERR=1 BATLOW=0 EDC=0 TRXRDY=1 WAKEUP=0 (cached events)",
            "STATE": "STATE=4(TX)",
        }
        self.assertEqual(set(expected), set(self.TX_RADIO))
        for view in ("tx", "all"):
            rows = registers.format_register_rows(hardware, "RF09", view=view)
            for name, fields in expected.items():
                with self.subTest(view=view, name=name):
                    self.assertEqual(self.row(rows, "RF09", name).split(" ", 2)[2], fields)

    def test_tx_null_bytes_and_invalid_banks_remain_unavailable(self):
        hardware = self.hardware(rf24={name: 0x04 for name in self.TX_RADIO})
        for name in self.TX_RADIO:
            with self.subTest(name=name):
                hardware["rf24"]["registers"][name] = None
                # Explicit null in the current register map must suppress a
                # retained legacy flat byte rather than invent fresh data.
                hardware["rf24"][name.lower()] = 0x04
                rows = registers.format_register_rows(hardware, "RF24", view="tx")
                self.assertEqual(self.row(rows, "RF24", name),
                                 f"RF24 {name}=0x-- unavailable")
                hardware["rf24"]["registers"][name] = 0x04
        for valid in (False, None, "true", 1):
            with self.subTest(valid=valid):
                hardware["rf24"]["valid"] = valid
                rows = registers.format_register_rows(hardware, "RF24", view="tx")
                for name in self.TX_RADIO:
                    self.assertEqual(self.row(rows, "RF24", name),
                                     f"RF24 {name}=0x-- unavailable")

    def test_auto_register_view_follows_effective_control_state_and_rx_fallback(self):
        hardware = self.hardware(common={name: 0 for name in self.COMMON},
                                 rf24={name: 0 for name in self.RADIO})
        cases = [
            ({"state": state}, "RX", 5, "tx")
            for state in ("TX_STARTING", "TX", "TX_STOPPING")
        ] + [
            ({}, state, 5, "tx")
            for state in ("TX_STARTING", "TX", "TX_STOPPING")
        ] + [
            ({"state": state}, "TX", 4, "rx")
            for state in ("RX_STARTING", "RX", "RX_STOPPING", "IDLE", "FAULT")
        ] + [({}, None, 4, "rx"), ({"state": None}, "TX", 5, "tx"),
             ({"state": ""}, "TX", 5, "tx"), ({}, "DRY_RUN", 4, "rx")]
        for control, state, modem_state, view in cases:
            with self.subTest(control=control, state=state, modem_state=modem_state):
                hardware["rf24"]["registers"]["STATE"] = modem_state
                value = snapshot(channel="RF24", hardware=hardware,
                                 control=control, state=state)
                default = client.format_snapshot(value)
                self.assertEqual(default, client.format_snapshot(value, register_view="auto"))
                text = "\n".join(default)
                self.assertEqual("RF24 TXDFE=" in text, view == "tx")
                self.assertEqual("RF24 AGCC=" in text, view == "rx")
                self.assertIn(f"RF24 STATE=0x{modem_state:02X}", text)

    def test_fixed_register_views_override_effective_state(self):
        hardware = self.hardware(common={name: 0 for name in self.COMMON},
                                 rf24={name: 0 for name in self.RADIO})
        for state in ("TX_STARTING", "TX", "TX_STOPPING", "RX", "IDLE"):
            for view in ("rx", "tx", "all"):
                with self.subTest(state=state, view=view):
                    text = "\n".join(client.format_snapshot(
                        snapshot(channel="RF24", hardware=hardware,
                                 control={"state": state}), register_view=view))
                    self.assertEqual("RF24 TXDFE=" in text, view in ("tx", "all"))
                    self.assertEqual("RF24 AGCC=" in text, view in ("rx", "all"))
                    self.assertEqual("RF PN=" in text, view == "all")

    def test_only_explicit_active_radio_is_shown_and_shared_rows_remain(self):
        hardware = self.hardware(common={"IQIFC0": 0x14},
                                 rf09={"AGCC": 0x01}, rf24={"AGCC": 0xFD})
        for channel, selected, suppressed, byte in (
                ("RF24", "RF24", "RF09", 0xFD),
                ("HiF (RF24)", "RF24", "RF09", 0xFD),
                ("RF09", "RF09", "RF24", 0x01),
                ("S1G (RF09)", "RF09", "RF24", 0x01)):
            with self.subTest(channel=channel):
                rows = registers.format_register_rows(hardware, channel)
                self.assertIn(f"=0x{byte:02X}", self.row(rows, selected, "AGCC"))
                self.assertIn("IQIFC0=0x14", self.row(rows, "RF", "IQIFC0"))
                self.assertFalse(any(row.startswith(f"{suppressed} ") for row in rows))
                rendered = "\n".join(client.format_snapshot(snapshot(channel=channel,
                                                                     hardware=hardware)))
                self.assertIn(f"{selected} AGCC=0x{byte:02X}", rendered)
                self.assertNotIn(f"{suppressed} AGCC", rendered)

    def test_unknown_channel_cannot_silently_select_a_radio(self):
        hardware = self.hardware(common={"IQIFC0": 0x14},
                                 rf09={"AGCC": 1}, rf24={"AGCC": 1})
        for channel in (None, "unknown", "RF25", "RF09 and RF24", [], {}):
            for view in ("rx", "tx", "all"):
                with self.subTest(channel=channel, view=view):
                    rows = registers.format_register_rows(hardware, channel, view=view)
                    self.assertIn("IQIFC0=0x14", self.row(rows, "RF", "IQIFC0"))
                    self.assertFalse(any(row.startswith(("RF09 ", "RF24 ")) for row in rows))

    def test_agcc_masks_are_independent_of_reserved_bit_seven(self):
        for value in range(128):
            with self.subTest(value=value):
                plain = self.row(registers.format_register_rows(
                    self.hardware(rf24={"AGCC": value}), "RF24"), "RF24", "AGCC")
                reserved = self.row(registers.format_register_rows(
                    self.hardware(rf24={"AGCC": value | 0x80}), "RF24"), "RF24", "AGCC")
                # Only the raw byte changes. The reserved high bit is outside
                # AGCI, AVGS and RST, which previously suffered oversized masks.
                self.assertEqual(plain.split(" ")[2:], reserved.split(" ")[2:])
                self.assertIn(f"EN={value & 1}", plain)
                self.assertIn(f"FRZC={(value >> 1) & 1}", plain)
                self.assertIn(f"FRZS={(value >> 2) & 1}", plain)
                self.assertIn(f"RST={(value >> 3) & 1}", plain)
                average = (value >> 4) & 3
                self.assertIn(f"AVGS={average}({8 << average})", plain)
                self.assertIn(f"AGCI={(value >> 6) & 1}(", plain)

    def test_target_is_relative_to_full_scale_and_gain_max_uses_bw_code(self):
        # Datasheet section 6.2.3/Table 6-11: gain maximum follows analog
        # bandwidth, independently of the IF inversion and shift flags.
        for code, target in enumerate((-21, -24, -27, -30, -33, -36, -39, -42)):
            row = self.row(registers.format_register_rows(
                self.hardware(rf24={"AGCS": (code << 5) | 21, "RXBWC": 0x10}),
                "RF24"), "RF24", "AGCS")
            self.assertIn(f"TGT={code}({target}dBFS)", row)
            self.assertIn("GCW=21/21(MAX)", row)
        maxima = (21,) * 6 + (22,) * 3 + (23,) * 3 + (None,) * 4
        for bandwidth, maximum in enumerate(maxima):
            with self.subTest(bandwidth=bandwidth):
                row = self.row(registers.format_register_rows(
                    self.hardware(rf24={"AGCS": 21, "RXBWC": 0x30 | bandwidth}),
                    "RF24"), "RF24", "AGCS")
                self.assertIn(f"GCW=21/{maximum if maximum is not None else 'unknown'}", row)
                self.assertEqual("(MAX)" in row, maximum == 21)

    def test_register_missing_values_do_not_invalidate_other_registers(self):
        hardware = self.hardware(rf24={"AGCS": 0xF5})
        rows = registers.format_register_rows(hardware, "RF24")
        self.assertEqual(self.row(rows, "RF24", "AGCC"), "RF24 AGCC=0x-- unavailable")
        agcs = self.row(rows, "RF24", "AGCS")
        self.assertIn("AGCS=0xF5 TGT=7(-42dBFS)", agcs)
        self.assertIn("GCW=21/unknown", agcs)
        self.assertNotIn("(MAX)", agcs)
        self.assertEqual(self.row(rows, "RF24", "RSSI"), "RF24 RSSI=0x-- unavailable")
        for value in (None, True, False, -1, 256, "1", 1.0, [], {}):
            with self.subTest(value=value):
                hardware["rf24"]["registers"]["AGCC"] = value
                rows = registers.format_register_rows(hardware, "RF24")
                self.assertEqual(self.row(rows, "RF24", "AGCC"),
                                 "RF24 AGCC=0x-- unavailable")
                self.assertIn("AGCS=0xF5", self.row(rows, "RF24", "AGCS"))

    def test_invalid_register_bank_cannot_display_old_bytes_as_valid(self):
        for valid in (False, None, "true", 1):
            with self.subTest(valid=valid):
                hardware = self.hardware(rf24={"AGCC": 1, "AGCS": 21, "RXBWC": 0x10})
                hardware["rf24"]["valid"] = valid
                rows = registers.format_register_rows(hardware, "RF24")
                self.assertEqual(self.row(rows, "RF24", "AGCC"),
                                 "RF24 AGCC=0x-- unavailable")
                self.assertEqual(self.row(rows, "RF24", "AGCS"),
                                 "RF24 AGCS=0x-- unavailable")
        hardware = self.hardware(rf24={"AGCC": 1})
        del hardware["rf24"]["valid"]
        self.assertIn("AGCC=0x01", self.row(
            registers.format_register_rows(hardware, "RF24"), "RF24", "AGCC"))

    def test_legacy_flat_bytes_still_decode_without_inactive_radio(self):
        hardware = {"rf09": {"valid": True, "agcc": 0xFD, "agcs": 0xF5,
                             "rxbwc": 0x10, "rxdfe": 4, "rssi_dbm": -100},
                    "rf24": {"valid": True, "agcc": 1, "agcs": 21}}
        rows = registers.format_register_rows(hardware, "RF09")
        self.assertIn("AGCC=0xFD", self.row(rows, "RF09", "AGCC"))
        self.assertIn("AGCS=0xF5 TGT=7(-42dBFS)", self.row(rows, "RF09", "AGCS"))
        self.assertIn("RSSI=0x9C", self.row(rows, "RF09", "RSSI"))
        self.assertFalse(any(row.startswith("RF24 ") for row in rows))

    def test_iq_interface_and_clock_fields_use_the_datasheet_bits(self):
        # Fixed examples from sections 4.4.1 and 4.5.8. These intentionally
        # differ from the demo to catch mistaken shared decoder/demo mappings.
        hardware = self.hardware(common={"IQIFC0": 0xAB, "IQIFC1": 0xD3,
                                         "IQIFC2": 0x80, "CLKO": 0x1A})
        rows = registers.format_register_rows(hardware, "RF24")
        iq0 = self.row(rows, "RF", "IQIFC0")
        for field in ("EXTLB=1", "SF=0", "DRV=2(3mA)", "CMV=2(250mV)",
                      "CMV1V2=1", "EEC=1"):
            self.assertIn(field, iq0)
        iq1 = self.row(rows, "RF", "IQIFC1")
        for field in ("FAILSF=1", "CHPM=5(", "SKEWDRV=3(4.906ns)"):
            self.assertIn(field, iq1)
        self.assertIn("SYNC=1", self.row(rows, "RF", "IQIFC2"))
        clock = self.row(rows, "RF", "CLKO")
        self.assertIn("DRV=3(8mA)", clock)
        self.assertIn("OS=2(32MHz)", clock)
        hardware["common"]["registers"].update(IQIFC0=0x14, IQIFC1=0x12)
        rows = registers.format_register_rows(hardware, "RF24")
        iq0 = self.row(rows, "RF", "IQIFC0")
        for field in ("EXTLB=0", "SF=0", "DRV=1(2mA)", "CMV=1(200mV)",
                      "CMV1V2=0", "EEC=0"):
            self.assertIn(field, iq0)
        iq1 = self.row(rows, "RF", "IQIFC1")
        self.assertIn("FAILSF=0", iq1)
        self.assertIn("CHPM=1(RF)", iq1)
        self.assertIn("SKEWDRV=2(3.906ns)", iq1)

    def test_reserved_chip_mode_is_reported_without_guessing_a_mode(self):
        for value in (0x20, 0x30, 0x60, 0x70):
            row = self.row(registers.format_register_rows(
                self.hardware(common={"IQIFC1": value}), "RF24"), "RF", "IQIFC1")
            self.assertIn(f"CHPM={value >> 4}(RESERVED)", row)

    def test_frontend_bandwidth_rate_and_signed_signal_level_examples(self):
        hardware = self.hardware(rf24={"RXBWC": 0x10, "RXDFE": 0x04,
                                       "TXDFE": 0x94, "RSSI": 0xBA, "EDV": 0x7F})
        rows = registers.format_register_rows(hardware, "RF24", view="all")
        bandwidth = self.row(rows, "RF24", "RXBWC")
        for field in ("IFI=0", "IFS=1", "BW=0(160kHz"):
            self.assertIn(field, bandwidth)
        rxdfe = self.row(rows, "RF24", "RXDFE")
        for field in ("RCUT=0(0.25*Fs/2)", "SR=4(1000kHz)"):
            self.assertIn(field, rxdfe)
        self.assertNotIn("DM=", rxdfe)
        txdfe = self.row(rows, "RF24", "TXDFE")
        for field in ("RCUT=4(1.0*Fs/2)", "DM=1", "SR=4(1000kHz)"):
            self.assertIn(field, txdfe)
        self.assertIn("RSSI=-70dBm", self.row(rows, "RF24", "RSSI"))
        self.assertIn("INVALID", self.row(rows, "RF24", "EDV"))
        self.assertNotIn("127dBm", self.row(rows, "RF24", "EDV"))
        hardware["rf24"]["registers"].update(RXDFE=0xE7, RSSI=0x80)
        rows = registers.format_register_rows(hardware, "RF24")
        rxdfe = self.row(rows, "RF24", "RXDFE")
        self.assertIn("RCUT=7(RESERVED)", rxdfe)
        self.assertIn("SR=7(UNDEFINED->4000kHz)", rxdfe)
        self.assertIn("INVALID", self.row(rows, "RF24", "RSSI"))

    def test_register_sections_fit_in_one_protocol_packet(self):
        value = snapshot(channel="RF24", hardware=self.hardware(
            common={name: 255 for name in self.COMMON},
            rf09={name: 255 for name in self.RADIO},
            rf24={name: 255 for name in self.RADIO}))
        packet = encoded(value)
        self.assertLess(len(packet), MAX_PACKET)
        self.assertEqual(client.parse_snapshot(packet), value)
        rows = registers.format_register_rows(value["hardware"], value["channel"], view="all")
        self.assertEqual(len(rows), 39)

    def test_small_terminal_can_page_to_the_last_register(self):
        class CachedConnection:
            def __init__(self, value):
                self.latest = value
                self.rates = {}
                self.status = "connected"

            def poll(self):
                return None

        class Screen:
            def __init__(self):
                self.frames = []
                self.keys = iter([client.curses.KEY_NPAGE] * 5 + [ord("q")])

            def nodelay(self, enabled):
                pass

            def keypad(self, enabled):
                pass

            def getmaxyx(self):
                return 18, 80

            def erase(self):
                self.lines = []

            def addnstr(self, row, column, line, limit):
                self.lines.append(line[:limit])

            def refresh(self):
                self.frames.append(tuple(self.lines))

            def getch(self):
                return next(self.keys)

        value = snapshot(channel="RF24", hardware=self.hardware(
            common={name: 0 for name in self.COMMON},
            rf09={name: 0 for name in self.RADIO},
            rf24={name: 0 for name in self.RADIO}))
        screen = Screen()
        self.assertEqual(client.run_interactive(screen, CachedConnection(value),
                                                register_view="all"), 0)
        initial = "\n".join(screen.frames[0])
        paged = "\n".join(line for frame in screen.frames[1:] for line in frame)
        self.assertNotIn("RF24 TXDACQ=", initial)
        self.assertIn("RF24 TXDACQ=0x00", paged)
        self.assertNotIn("RF09 AGCC", paged)
        self.assertTrue(all(frame[0].startswith("CaribouLite radio monitor (passive)")
                            for frame in screen.frames))

    def test_default_rx_view_fits_57_rows_with_both_rate_panels_and_footer(self):
        class CachedConnection:
            def __init__(self, value):
                self.latest = value
                self.rates = {"rx": (100, 100), "tx": (100, 100)}
                self.status = "connected"

            def poll(self):
                return None

        class Screen:
            def __init__(self):
                self.frames = []

            def nodelay(self, enabled):
                pass

            def keypad(self, enabled):
                pass

            def getmaxyx(self):
                return 57, 160

            def erase(self):
                self.lines = {}

            def addnstr(self, row, column, line, limit):
                # Enforce terminal bounds, rather than accepting off-screen
                # rows or text which a real curses window would reject.
                assert 0 <= row < 57 and column == 0
                self.lines[row] = line[:limit]

            def refresh(self):
                self.frames.append(dict(self.lines))

            def getch(self):
                return ord("q")

        hardware = self.hardware(common={name: 0 for name in self.COMMON},
                                 rf09={name: 0 for name in self.RADIO},
                                 rf24={name: 0 for name in self.RADIO})
        hardware["rf24"]["registers"].update(STATE=5)
        hardware["fpga"] = {"valid": True, "raw_status": 7,
                            "io_control": 0, "debug": 1}
        queue = {"capacity": 64, "depth": 3, "min_depth": 0,
                 "max_depth": 20, "puts": 100, "gets": 97, "drops": 0,
                 "timeouts_put": 0, "timeouts_get": 1}
        value = snapshot(channel="RF24", hardware=hardware,
                         rx={"running": True, "queue": queue, **rx_instrumentation()},
                         tx={"running": False, "queue": queue})
        connection = CachedConnection(value)
        body = client.format_snapshot(value, rates=connection.rates)
        self.assertLessEqual(len(body) + 3, 57)
        screen = Screen()
        self.assertEqual(client.run_interactive(screen, connection), 0)
        self.assertEqual(len(screen.frames), 1)
        frame = screen.frames[0]
        displayed = "\n".join(frame[row] for row in sorted(frame))
        for line in ("RX application RF queue:", "TX application RF queue:",
                     "RX audio FIFO: 12/24", "RX playback: calls=91", "underruns=7",
                     "RX reader: calls=110", "RX reader worker: alive=on",
                     "RX demod worker: alive=on", "RX audio worker: alive=on",
                     "errors(rt/fb/cpu/lock/query)=1/0/0/12/0",
                     "RF24 STATE=0x05 STATE=5(RX)", "FPGA:", "Loopback:",
                     "Queue timeouts are wait events; drops are application queue drops."):
            with self.subTest(line=line):
                self.assertIn(line, displayed)
        self.assertEqual(displayed.count("put/get rate=100.0/100.0 frames/s"), 2)
        self.assertNotIn("RF09 ", displayed)
        self.assertNotIn(" TXDFE=", displayed)
        self.assertNotIn(" TXDAC", displayed)
        self.assertEqual(frame[len(body) + 2], body[-1])

    def test_default_tx_view_fits_57_rows_and_keeps_registers_and_footer_visible(self):
        class Connection:
            status = "connected"
            rates = {"rx": (100, 100), "tx": (200, 200)}

            def __init__(self, value):
                self.latest = value

            def poll(self):
                return None

        class Screen:
            def __init__(self):
                self.lines = {}

            def nodelay(self, enabled): pass
            def keypad(self, enabled): pass
            def getmaxyx(self): return 57, 160
            def erase(self): self.lines = {}
            def refresh(self): pass
            def getch(self): return ord("q")

            def addnstr(self, row, column, line, limit):
                assert 0 <= row < 57 and column == 0
                self.lines[row] = line[:limit]

        hardware = self.hardware(common={name: 0 for name in self.COMMON},
                                 rf24={name: 0 for name in self.RADIO})
        hardware["rf24"]["registers"]["STATE"] = 6
        hardware["fpga"] = {"valid": True, "raw_status": 7, "io_control": 1, "debug": 2}
        queue = {"capacity": 64, "depth": 3, "min_depth": 0, "max_depth": 20,
                 "puts": 100, "gets": 97, "drops": 0, "timeouts_put": 0, "timeouts_get": 1}
        tx = hardware_tx_instrumentation()
        tx["queue"] = queue
        for state in ("TX_STARTING", "TX", "TX_STOPPING"):
            with self.subTest(state=state):
                value = snapshot(channel="RF24", state="DRY_RUN", hardware=hardware,
                                 rx={"running": False, "queue": queue, **rx_instrumentation()},
                                 tx=tx, control={"state": state, "tx_pipeline_generation": 5,
                                                 "tx_requested": True, "transition": True,
                                                 "rx_resume": True},
                                 config={"tx_sample_rate_hz": 2000000, "tx_source": "mic",
                                         "tx_deviation_hz": 2500})
                connection = Connection(value)
                body = client.format_snapshot(value, rates=connection.rates)
                self.assertLessEqual(len(body) + 3, 57)
                screen = Screen()
                self.assertEqual(client.run_interactive(screen, connection), 0)
                text = "\n".join(screen.lines.values())
                for expected in (f"state={state}", "TX writer: calls=151", "overruns=4",
                                 "TX producer worker: alive=on", "TX writer worker: alive=on",
                                 "TX driver: FIFO=32768/65536 bytes completed=777 underruns=3",
                                 "SMI write(set/strobe/hold/pace)=1/4/2/1",
                                 "TX audio: 1900/16384 frames target=1920 correction=+4033.5 ppm",
                                 "TX capture worker: alive=on", "hardware paced=on",
                                 "TX fault: none", "RF24 STATE=0x06", "FPGA:", "Loopback:",
                                 "TX sample rate=2,000,000 S/s TX source=mic TX deviation=+/-2500 Hz",
                                 "Queue timeouts are wait events; drops are application queue drops."):
                    self.assertIn(expected, text)
                for name in self.TX_COMMON:
                    self.assertIn(f"RF {name}=", text)
                for name in self.TX_RADIO:
                    self.assertIn(f"RF24 {name}=", text)
                self.assertEqual(sum(line.startswith("RF24 ") and "=0x" in line
                                     for line in screen.lines.values()), 14)
                self.assertNotIn("RF09 ", text)
                self.assertNotIn("RF24 AGCC=", text)
                self.assertNotIn("RF24 RXDFE=", text)
                self.assertNotIn("RX audio FIFO:", text)
                self.assertEqual(screen.lines[len(body) + 2], body[-1])

    def test_rx_instrumentation_reports_fifo_playback_reader_and_actual_scheduling(self):
        lines = client.format_snapshot(snapshot(rx=rx_instrumentation()))
        text = "\n".join(lines)
        for expected in (
                "RX audio FIFO: 12/24  min/max=0/17  puts/gets=100/88",
                "drops=3  put/get timeouts=3/2  shutdown put/get=1/2",
                "RX playback: calls=91  frames(req/written)=48000/47000",
                "partial=4  would_block=5  zero=6", "underruns=7",
                "recovery(attempt/ok/fail)=8/6/2  errors=9  last_error=-32",
                "RX reader: calls=110  samples=1000000  short/zero reads=3/2",
                "errors=4  last_error=-5", "created=on last_err=-5",
                "sched=SCHED_OTHER/0 req=80",
                "cpu(req/applied)=2/2 affinity=2",
                "errors(rt/fb/cpu/lock/query)=1/0/0/12/0 mlock=on"):
            with self.subTest(expected=expected):
                self.assertIn(expected, text)
        self.assertEqual(sum(" worker:" in line for line in lines), 3)
        self.assertTrue(all(len(line) <= 159 for line in lines if " worker:" in line))

    def test_tx_details_replace_rx_instrumentation_in_all_transmit_states(self):
        for state in ("TX_STARTING", "TX", "TX_STOPPING"):
            with self.subTest(state=state):
                value = snapshot(state="DRY_RUN", channel="RF24", rx=rx_instrumentation(),
                                 tx=tx_instrumentation(),
                                 control={"state": state, "tx_pipeline_generation": 5,
                                          "tx_requested": True, "rx_resume": True, "transition": True},
                                 config={"tx_frequency_hz": 430225000,
                                         "tx_actual_frequency_hz": None, "tx_power_dbm": -12,
                                         "tx_actual_power_dbm": None, "tx_sample_rate_hz": 2000000,
                                         "tx_source": "mic", "tx_deviation_hz": 2500})
                lines = client.format_snapshot(value)
                text = "\n".join(lines)
                for expected in (f"state={state}", "TX pipeline: running=on requested=on",
                                 "initialized=on generation=5 resume RX=on transition=on",
                                 "TX writer: calls=151 samples=1234567 written_sequence=123",
                                 "partial/zero writes=7/3 would_block=4 errors=2 last_error=-11",
                                 "TX capture: calls=42 frames=20160 overruns=4",
                                 "recovery(attempt/ok/fail)=7/6/1 errors=2 last_error=-32",
                                 "TX producer worker: alive=on created=on last_err=-5",
                                 "TX writer worker: alive=on created=on last_err=-5",
                                 "sched=SCHED_OTHER/0 req=60", "affinity=0",
                                 "errors(rt/fb/cpu/lock/query)=1/0/0/12/0 mlock=on",
                                 "TX fault: none", "TX sample rate=2,000,000 S/s TX source=mic TX deviation=+/-2500 Hz",
                                 "TX frequency=430.225000 MHz  TX actual frequency=unavailable", "power(req/est)=-12/unavailable dBm"):
                    self.assertIn(expected, text)
                for absent in ("RX audio FIFO:", "RX playback:", "RX reader:", "RX reader worker:"):
                    self.assertNotIn(absent, text)
                self.assertEqual(len(client._format_tx_instrumentation(value["tx"], value["control"])), 8)
                self.assertTrue(all(len(line) <= 159 for line in lines if " worker:" in line))

    def test_hardware_tx_metrics_remain_unavailable_when_invalid(self):
        tx = hardware_tx_instrumentation()
        tx["hardware_paced"] = None
        tx["driver"]["valid"] = False
        tx["audio_buffer"]["valid"] = False
        tx["smi_timing"]["valid"] = False
        tx["workers"]["capture"] = None
        lines = client._format_tx_instrumentation(tx, {"state": "TX"})
        self.assertEqual(len(lines), 8)
        text = "\n".join(lines)
        for expected in ("hardware paced=unknown", "TX driver: unavailable", "TX audio: unavailable",
                         "TX capture worker: alive=unknown created=unknown", "SMI write=unavailable"):
            self.assertIn(expected, text)
        for absent in ("FIFO=32768", "completed=0", "underruns=0", "correction=+0.0"):
            self.assertNotIn(absent, text)

    def test_old_tx_publishers_do_not_invent_smi_timing(self):
        text = "\n".join(client._format_tx_instrumentation(tx_instrumentation(), {}))
        self.assertNotIn("SMI write", text)

    def test_unavailable_tx_statistics_do_not_imply_healthy_zeroes(self):
        tx = tx_instrumentation()
        tx.update(running=None, writer=None, capture={"valid": False, "overruns": 4},
                  fault="writer failure\x1b[2J\n")
        tx["workers"]["producer"] = None
        tx["workers"]["writer"]["scheduling"]["valid"] = False
        text = "\n".join(client.format_snapshot(snapshot(control={"state": "TX"}, tx=tx)))
        for expected in ("running=unknown", "TX writer: calls=unavailable samples=unavailable",
                         "partial/zero writes=unavailable/unavailable", "errors=unavailable",
                         "TX capture: unavailable", "ALSA capture counters unavailable",
                         "TX producer worker: alive=unknown created=unknown",
                         "sched=unavailable/unavailable", "TX fault: writer failure"):
            self.assertIn(expected, text)
        for absent in ("overruns=0", "overruns=4", "calls=0", "errors=0", "\x1b"):
            self.assertNotIn(absent, text)

    def test_rx_details_return_after_tx_and_retained_tx_fault_is_visible(self):
        tx = tx_instrumentation()
        tx["fault"] = "capture failed"
        for state in ("RX_STARTING", "RX", "IDLE", "FAULT"):
            text = "\n".join(client.format_snapshot(snapshot(control={"state": state},
                                                            rx=rx_instrumentation(), tx=tx)))
            self.assertIn("RX audio FIFO:", text)
            self.assertNotIn("TX writer:", text)
            self.assertIn("TX fault=capture failed", text)

    def test_optional_rx_instrumentation_does_not_invent_data_for_older_publishers(self):
        text = "\n".join(client.format_snapshot(snapshot(rx={"running": True})))
        for label in ("RX audio FIFO:", "RX playback:", "RX reader:", " worker:"):
            with self.subTest(label=label):
                self.assertNotIn(label, text)

    def test_unavailable_rx_instrumentation_cannot_report_healthy_zeroes(self):
        new_rx = rx_instrumentation()
        new_rx["audio_queue"] = {"depth": True, "capacity": -1, "drops": "0"}
        new_rx["playback"]["valid"] = False
        new_rx["reader"] = []
        worker = new_rx["workers"]["reader"]
        worker["alive"] = False
        del worker["last_error"]
        worker["scheduling"]["valid"] = False
        worker["scheduling"]["query_error"] = 3
        new_rx["workers"]["demod"] = None
        new_rx["workers"]["audio"]["scheduling"] = []
        text = "\n".join(client.format_snapshot(snapshot(rx=new_rx)))
        for expected in ("RX audio FIFO: unavailable/unavailable", "drops=unavailable",
                         "RX playback: unavailable", "ALSA counters unavailable",
                         "RX reader: calls=unavailable", "short/zero reads=unavailable/unavailable",
                         "RX reader worker: alive=off created=on last_err=unavailable",
                         "sched=unavailable/unavailable",
                         "req=80", "errors(rt/fb/cpu/lock/query)=1/0/0/12/3",
                         "RX demod worker: alive=unknown created=unknown",
                         "affinity=unavailable"):
            with self.subTest(expected=expected):
                self.assertIn(expected, text)
        self.assertNotIn("underruns=0", text)
        self.assertNotIn("underruns=7", text)
        self.assertNotIn("calls=0", text)
        self.assertNotIn("drops=0", text)

    def test_plain_stream_respects_auto_rx_tx_and_all_register_views(self):
        hardware = self.hardware(
            common={name: 0 for name in self.COMMON},
            rf24={name: 0 for name in self.RADIO})
        for state in ("RX", "TX_STARTING", "TX", "TX_STOPPING"):
            value = snapshot(channel="RF24", hardware=hardware, control={"state": state})
            for view in (None, "auto", "rx", "tx", "all"):
                with self.subTest(state=state, view=view):
                    connection = mock.Mock(rates={}, status="connected")
                    connection.poll.return_value = value
                    output = io.StringIO()
                    options = {} if view is None else {"register_view": view}
                    with redirect_stdout(output):
                        self.assertEqual(client.run_stream(connection, once=True, plain=True,
                                                           **options), 0)
                    displayed = output.getvalue()
                    effective = ("rx" if state == "RX" else "tx") if view in (None, "auto") else view
                    self.assertIn("RF24 STATE=", displayed)
                    self.assertEqual("RF24 TXDFE=" in displayed, effective in ("tx", "all"))
                    self.assertEqual("RF24 TXDACQ=" in displayed, effective in ("tx", "all"))
                    self.assertEqual("RF24 AGCC=" in displayed, effective in ("rx", "all"))

    def test_cli_passes_register_view_to_stream_and_interactive_display(self):
        for selected in (None, "auto", "rx", "tx", "all"):
            view = "auto" if selected is None else selected
            options = [] if selected is None else ["--registers", selected]
            with self.subTest(view=view, mode="plain"):
                with mock.patch.object(client, "SnapshotConnection") as connection_type, \
                        mock.patch.object(client, "run_stream", return_value=0) as stream:
                    self.assertEqual(client.main(["--plain", *options]), 0)
                    stream.assert_called_once_with(connection_type.return_value,
                                                   False, 5.0, True, view)
                    connection_type.return_value.close.assert_called_once_with()
            with self.subTest(view=view, mode="interactive"):
                with mock.patch.object(client, "SnapshotConnection") as connection_type, \
                        mock.patch.object(client.sys.stdin, "isatty", return_value=True), \
                        mock.patch.object(client.sys.stdout, "isatty", return_value=True), \
                        mock.patch.object(client.curses, "wrapper", return_value=0) as wrapped:
                    self.assertEqual(client.main(options), 0)
                    wrapped.assert_called_once_with(client.run_interactive,
                                                    connection_type.return_value, view)
                    connection_type.return_value.close.assert_called_once_with()

    def test_cli_rejects_unknown_register_view_before_connecting(self):
        with mock.patch.object(client, "SnapshotConnection") as connection_type, \
                mock.patch.object(client.sys, "stderr", new_callable=io.StringIO):
            with self.assertRaises(SystemExit) as error:
                client.main(["--registers", "invalid"])
            self.assertEqual(error.exception.code, 2)
            connection_type.assert_not_called()


class PassiveClientTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="monitor-client-test-")
        self.path = Path(self.directory.name) / "status.sock"
        self.publishers = []
        self.connections = []

    def tearDown(self):
        for connection in self.connections:
            connection.close()
        for publisher in self.publishers:
            publisher.stop()
        self.directory.cleanup()

    def start(self, latest=None):
        publisher = FixturePublisher(self.path, encoded(latest) if latest else None)
        self.publishers.append(publisher)
        return publisher

    def connection(self):
        connection = client.SnapshotConnection(str(self.path))
        self.connections.append(connection)
        return connection

    def wait_for_snapshot(self, connection, timeout=2):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = connection.poll(.02)
            if value is not None:
                return value
        self.fail(f"Client did not receive a valid snapshot: {connection.status}")

    def test_valid_minimal_packet_and_full_sections(self):
        value = snapshot(config={"sample_rate_hz": 1000000},
                         rx={"queue": {"puts": 1}}, tx={}, hardware={}, loopback={})
        self.assertEqual(client.parse_snapshot(encoded(value)), value)
        self.assertEqual(client.parse_snapshot(encoded(snapshot()))["version"], 1)

    def test_extended_v1_packet_above_old_ceiling_is_accepted(self):
        value = snapshot(tx=hardware_tx_instrumentation(), padding="p" * 9000)
        packet = encoded(value)
        self.assertGreater(len(packet), 8192)
        self.assertLessEqual(len(packet), MAX_PACKET)
        self.assertEqual(client.parse_snapshot(packet), value)

    def test_rejects_malformed_and_incompatible_packets(self):
        malformed = [b"\xff", b"[]", b"{}", b"broken JSON", b"x" * (MAX_PACKET + 1)]
        for change in ({"version": 2}, {"version": True}, {"session": None},
                       {"sequence": -1}, {"sequence": True},
                       {"monotonic_s": float("nan")}, {"config": []},
                       {"hardware_monotonic_s": float("inf")}):
            malformed.append(encoded(snapshot(**change)))
        for payload in malformed:
            with self.subTest(payload=payload[:80]):
                with self.assertRaises(ValueError):
                    client.parse_snapshot(payload)
        with self.assertRaises(ValueError):
            client.parse_snapshot(encoded(snapshot()), truncated=True)

    def test_rejects_nonfinite_numbers_in_nested_json(self):
        for number in ("NaN", "Infinity", "-Infinity", "1e999"):
            value = encoded(snapshot())[:-1] + b',"config":{"value":' + number.encode() + b"}}"
            with self.subTest(number=number):
                with self.assertRaises(ValueError):
                    client.parse_snapshot(value)

    def test_queue_rates_handle_counter_resets_and_new_sessions(self):
        old = snapshot(monotonic_s=10, rx={"queue": {"puts": 100, "gets": 90}})
        new = snapshot(2, monotonic_s=12, rx={"queue": {"puts": 300, "gets": 280}})
        self.assertEqual(client.queue_rates(old, new), {"rx": (100, 95)})
        reset = snapshot(3, monotonic_s=14, rx={"queue": {"puts": 1, "gets": 0}})
        self.assertEqual(client.queue_rates(new, reset), {})
        changed = dict(new, session="new-session")
        self.assertEqual(client.queue_rates(old, changed), {})
        self.assertEqual(client.queue_rates(old, dict(new, monotonic_s=10)), {})

    def test_extreme_counters_and_timestamps_cannot_crash_rate_display(self):
        old = snapshot(monotonic_s=1, rx={"queue": {"puts": 0, "gets": 0}})
        extreme = snapshot(2, monotonic_s=2,
                           rx={"queue": {"puts": 10**400, "gets": 10**400}})
        parsed = client.parse_snapshot(encoded(extreme))
        self.assertEqual(client.queue_rates(old, parsed), {})
        near = snapshot(monotonic_s=0, rx={"queue": {"puts": 0, "gets": 0}})
        overflow = snapshot(2, monotonic_s=1e-308,
                            rx={"queue": {"puts": 100, "gets": 100}})
        self.assertEqual(client.queue_rates(near, overflow), {})
        self.assertEqual(client.queue_rates(dict(old, monotonic_s=-1e308),
                                           dict(extreme, monotonic_s=1e308)), {})

    def test_pipeline_generation_prevents_rates_across_receiver_restart(self):
        old = snapshot(monotonic_s=10, control={"pipeline_generation": 1},
                       rx={"queue": {"puts": 10, "gets": 9}})
        new = snapshot(2, monotonic_s=12, control={"config_generation": 4,
                       "pipeline_generation": 2},
                       rx={"queue": {"puts": 210, "gets": 209}})
        self.assertEqual(client.queue_rates(old, new), {})
        self.assertEqual(client.queue_rates(dict(old, control={"pipeline_generation": 2}), new),
                         {"rx": (100, 100)})
        self.assertIn("config=4  RX pipeline=2", "\n".join(client.format_snapshot(new)))

    def test_tx_generation_prevents_rates_across_transmitter_restart(self):
        old = snapshot(monotonic_s=10, control={"tx_pipeline_generation": 1},
                       tx={"generation": 1, "queue": {"puts": 10, "gets": 9}})
        new = snapshot(2, monotonic_s=12, control={"tx_pipeline_generation": 2},
                       tx={"generation": 2, "queue": {"puts": 210, "gets": 209}})
        self.assertEqual(client.queue_rates(old, new), {})
        same = dict(old, control={"tx_pipeline_generation": 2},
                    tx={"generation": 2, "queue": old["tx"]["queue"]})
        self.assertEqual(client.queue_rates(same, new), {"tx": (100, 100)})
        # Either exported generation changing suppresses rates, even if an
        # incomplete publisher temporarily leaves the other field unchanged.
        self.assertEqual(client.queue_rates(dict(same, control={"tx_pipeline_generation": 1}), new), {})
        self.assertEqual(client.queue_rates(dict(same, tx=old["tx"]), new), {})
        self.assertIn("TX pipeline=2", "\n".join(client.format_snapshot(new)))
        reset = snapshot(3, monotonic_s=14, control={"tx_pipeline_generation": 2},
                         tx={"generation": 2, "queue": {"puts": 1, "gets": 0}})
        self.assertEqual(client.queue_rates(new, reset), {})

    def test_formatting_marks_stale_cache_and_safe_labels(self):
        value = snapshot(monotonic_s=10, hardware_monotonic_s=8, rx_mode="NBFM",
                         channel="HiF\x1b[2J\n", config={"sample_rate_hz": 1000000,
                         "rx_frequency_hz": 430125000, "tx_frequency_hz": 430225000})
        lines = client.format_snapshot(value, now=12)
        text = "\n".join(lines)
        self.assertIn("STALE", text)
        self.assertIn("Snapshot age=2.0s", text)
        self.assertIn("hardware age=4.0s", text)
        self.assertIn("RX mode=NBFM  RX sample rate=1,000,000 S/s", text)
        self.assertIn("RX frequency=430.125000 MHz  TX frequency=430.225000 MHz", text)
        self.assertIn("RX squelch:", text)
        self.assertIn("RX noise detector:", text)
        self.assertNotIn("\x1b", text)
        self.assertIn("simulated", text.lower())

    def test_tx_analog_filter_cutoff_uses_published_hz_value(self):
        value = snapshot(config={"rx_bandwidth_hz": 160000,
                                 "tx_bandwidth_hz": 1000000})
        text = "\n".join(client.format_snapshot(value))
        self.assertIn("RX bandwidth=160 kHz  TX bandwidth=1000 kHz", text)
        self.assertNotIn("TX analog filter cutoff=", text)

    def test_unavailable_or_older_tx_cutoff_stays_unknown(self):
        for config in ({}, {"tx_bandwidth_hz": None}):
            with self.subTest(config=config):
                value = snapshot(channel="RF24", config=config,
                                 hardware={"rf24": {"valid": True,
                                                    "registers": {"TXCUTC": 11}}})
                text = "\n".join(client.format_snapshot(value, register_view="tx"))
                self.assertIn("TX bandwidth=unknown", text)
                # Raw register rows remain useful with older publishers, but
                # the summary must use the supplied config measurement.
                self.assertIn("LPFCUT=11(1000kHz)", text)
                self.assertNotIn("TX analog filter cutoff=", text)

    def test_agc_bit_fields_and_target(self):
        decoded = client.decode_agc(0xFD, 0xF5, 0x10)
        self.assertTrue(decoded["enabled"])
        self.assertEqual(decoded["agci"], 1)
        self.assertEqual(decoded["average_samples"], 64)
        self.assertEqual(decoded["reset"], 1)
        self.assertEqual(decoded["freeze_status"], 1)
        self.assertEqual(decoded["freeze_control"], 0)
        self.assertEqual(decoded["target_dbfs"], -42)
        self.assertEqual(decoded["gain_word"], 21)
        self.assertEqual(decoded["gain_max"], 21)
        self.assertEqual(client.decode_agc(1, 21, 6)["gain_max"], 22)
        self.assertEqual(client.decode_agc(1, 21, 9)["gain_max"], 23)
        self.assertIsNone(client.decode_agc(1, 21, 12)["gain_max"])

    def test_invalid_measurements_display_unavailable(self):
        value = snapshot(
            channel="HiF (RF24)",
            rx={"noise": {"valid": False, "rms": 0, "detector_open": True}},
            hardware={"rf09": {"valid": "true", "rssi_dbm": -100},
                      "rf24": {"valid": True, "rssi_dbm": 127, "agcc": 1,
                               "agcs": 21, "rxbwc": 12, "rxdfe": 4},
                      "fpga": {"valid": "true", "raw_status": 7}})
        text = "\n".join(client.format_snapshot(value))
        self.assertNotIn("RF09", text)
        self.assertIn("RF24 RSSI=0x-- unavailable", text)
        self.assertIn("RMS=unavailable", text)
        self.assertIn("open=unknown", text)
        self.assertIn("GCW=21/unknown", text)
        self.assertNotIn("FPGA:", text)
        self.assertNotIn("127 dBm", text)

    def test_client_attaches_to_latest_snapshot_and_sends_no_commands(self):
        publisher = self.start(snapshot(17))
        connection = self.connection()
        self.assertEqual(self.wait_for_snapshot(connection)["sequence"], 17)
        publisher.publish(snapshot(18))
        self.assertEqual(self.wait_for_snapshot(connection)["sequence"], 18)
        time.sleep(.03)
        self.assertEqual(publisher.received, [])
        connection.close()
        self.assertFalse(publisher.stopping.is_set())

    def test_client_started_first_waits_then_attaches(self):
        connection = self.connection()
        started = time.monotonic()
        self.assertIsNone(connection.poll(.01))
        self.assertLess(time.monotonic() - started, .3)
        self.start(snapshot(42))
        self.assertEqual(self.wait_for_snapshot(connection)["sequence"], 42)

    def test_idle_connection_poll_is_bounded(self):
        self.start()
        connection = self.connection()
        started = time.monotonic()
        self.assertIsNone(connection.poll(.02))
        self.assertLess(time.monotonic() - started, .3)

    def test_queued_snapshots_are_drained_to_newest(self):
        publisher = self.start(snapshot(1))
        connection = self.connection()
        self.wait_for_snapshot(connection)
        for sequence in range(2, 22):
            publisher.publish(snapshot(sequence))
        self.assertEqual(self.wait_for_snapshot(connection)["sequence"], 21)
        self.assertEqual(connection.latest["sequence"], 21)

    def test_duplicate_and_older_records_cannot_replace_newer_status(self):
        publisher = self.start(snapshot(10, monotonic_s=20))
        connection = self.connection()
        self.wait_for_snapshot(connection)
        publisher.publish(snapshot(10, monotonic_s=20))
        self.assertIsNone(connection.poll(.02))
        publisher.publish(snapshot(9, monotonic_s=21))
        self.assertIsNone(connection.poll(.02))
        self.assertEqual(connection.latest["sequence"], 10)
        publisher.publish(snapshot(11, monotonic_s=19))
        self.assertIsNone(connection.poll(.02))
        self.assertEqual(connection.latest["sequence"], 10)
        publisher.publish(snapshot(12, monotonic_s=22))
        self.assertEqual(self.wait_for_snapshot(connection)["sequence"], 12)

    def test_oversized_snapshot_is_rejected_then_valid_stream_recovers(self):
        publisher = self.start()
        connection = self.connection()
        connection.poll(.02)
        self.assertTrue(publisher.connected.wait(1))
        publisher.publish(encoded(snapshot(1, padding="p" * MAX_PACKET)))
        self.assertIsNone(connection.poll(.02))
        self.assertIn("rejected", connection.status)
        publisher.publish(snapshot(2))
        self.assertEqual(self.wait_for_snapshot(connection)["sequence"], 2)

    def test_disconnect_and_reconnect_to_new_session_clears_rates(self):
        publisher = self.start(snapshot(10, rx={"queue": {"puts": 100, "gets": 100}}))
        connection = self.connection()
        old = self.wait_for_snapshot(connection)
        publisher.publish(snapshot(11, monotonic_s=old["monotonic_s"] + 1,
                                   rx={"queue": {"puts": 200, "gets": 200}}))
        self.wait_for_snapshot(connection)
        self.assertEqual(connection.rates["rx"], (100, 100))
        publisher.stop()
        self.publishers.remove(publisher)
        connection.poll(.02)
        self.assertIn("disconnected", connection.status)
        self.start(snapshot(1, session="new-session", rx={"queue": {"puts": 0, "gets": 0}}))
        self.assertEqual(self.wait_for_snapshot(connection)["session"], "new-session")
        self.assertEqual(connection.rates, {})

    def test_subprocess_once_reads_json(self):
        self.start(snapshot(37))
        result = subprocess.run(["python3", str(CLIENT), "--socket", str(self.path),
                                 "--once", "--timeout", "2"], capture_output=True,
                                text=True, timeout=4)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["sequence"], 37)

    def test_subprocess_once_started_before_publisher(self):
        with subprocess.Popen(["python3", str(CLIENT), "--socket", str(self.path),
                               "--once", "--timeout", "2"], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True) as observer:
            time.sleep(.08)
            self.start(snapshot(42))
            stdout, stderr = observer.communicate(timeout=4)
        self.assertEqual(observer.returncode, 0, stderr)
        self.assertEqual(json.loads(stdout)["sequence"], 42)

    def test_subprocess_once_timeout_and_invalid_cli(self):
        result = subprocess.run(["python3", str(CLIENT), "--socket", str(self.path),
                                 "--once", "--timeout", ".1"], capture_output=True,
                                text=True, timeout=3)
        self.assertEqual(result.returncode, 1)
        self.assertTrue(result.stderr)
        for timeout in ("0", "NaN", "Infinity"):
            result = subprocess.run(["python3", str(CLIENT), "--once", "--timeout", timeout],
                                    capture_output=True, text=True, timeout=3)
            self.assertEqual(result.returncode, 2)

    def test_actual_demo_source_is_simulated_private_and_independent(self):
        source = subprocess.Popen(["python3", str(DEMO), "--socket", str(self.path),
                                   "--seconds", "2"], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.PIPE, text=True,
                                  env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
        try:
            connection = self.connection()
            first = self.wait_for_snapshot(connection)
            self.assertIn("SIMULATED", first["source"])
            self.assertIn("SIMULATED DATA", client.format_snapshot(first)[0])
            self.assertEqual(first["config"]["sample_rate_hz"], 1000000)
            for bank, names in (("common", IQRegisterDisplayTests.COMMON),
                                ("rf24", IQRegisterDisplayTests.RADIO)):
                self.assertIs(first["hardware"][bank]["valid"], True)
                values = first["hardware"][bank]["registers"]
                self.assertEqual(set(values), set(names))
                self.assertTrue(all(type(value) is int and 0 <= value <= 255
                                    for value in values.values()))
            self.assertLess(len(encoded(first)), MAX_PACKET)
            display = "\n".join(client.format_snapshot(first))
            self.assertIn("RF24 AGCC=0x01 AGCI=0(FILT) AVGS=0(8) RST=0 FRZS=0 FRZC=0 EN=1",
                          display)
            self.assertNotIn("RF09 AGCC", display)
            self.assertEqual(self.path.parent.stat().st_mode & 0o777, 0o700)
            self.assertEqual(self.path.stat().st_mode & 0o777, 0o600)
            second = self.wait_for_snapshot(connection)
            third = self.wait_for_snapshot(connection)
            self.assertGreater(second["sequence"], first["sequence"])
            self.assertGreater(third["sequence"], second["sequence"])
            # About 5 Hz: generous tolerance permits a busy development machine.
            interval = ((third["monotonic_s"] - first["monotonic_s"])
                        / (third["sequence"] - first["sequence"]))
            self.assertGreater(interval, .1)
            self.assertLess(interval, .6)
            self.assertEqual(third["rx"]["queue"]["puts"], third["sequence"] * 20)
            connection.close()
            self.assertIsNone(source.poll(), "Closing the monitor stopped its source")
            self.assertEqual(source.wait(timeout=4), 0)
            self.assertFalse(self.path.exists(), "Demo did not remove its owned socket")
            self.assertIn("no radio connection", source.stderr.read())
        finally:
            if source.poll() is None:
                source.send_signal(signal.SIGINT)
                source.wait(timeout=3)
            source.stderr.close()

    def test_demo_refuses_existing_file_without_replacing_it(self):
        self.path.write_text("preserve this file")
        result = subprocess.run(["python3", str(DEMO), "--socket", str(self.path),
                                 "--seconds", ".1"], capture_output=True,
                                text=True, timeout=3,
                                env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("already exists", result.stderr)
        self.assertEqual(self.path.read_text(), "preserve this file")

    def test_interactive_terminal_quit_is_local(self):
        import fcntl
        import pty
        import struct
        import termios

        publisher = self.start(snapshot())
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 36, 120, 0, 0))
        environment = dict(os.environ, TERM="xterm")
        process = subprocess.Popen(["python3", str(CLIENT), "--socket", str(self.path)],
                                   stdin=slave, stdout=slave, stderr=slave, env=environment)
        os.close(slave)
        output = bytearray()
        try:
            self.assertTrue(publisher.connected.wait(2), "Interactive client did not attach")
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline and b"radio monitor (passive)" not in output.lower():
                if select.select([master], [], [], .1)[0]:
                    output.extend(os.read(master, 65536))
            self.assertIn(b"radio monitor (passive)", output.lower())
            os.write(master, b"q")
            self.assertEqual(process.wait(timeout=3), 0)
            self.assertEqual(publisher.received, [])
            self.assertFalse(publisher.stopping.is_set())
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)


if __name__ == "__main__":
    unittest.main(verbosity=2)
