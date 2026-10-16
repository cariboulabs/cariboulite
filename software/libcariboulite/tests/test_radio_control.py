#!/usr/bin/env python3
"""Exercise the C radio controller against isolated mock seqpacket peers.

These tests never launch a hardware backend or open a radio/audio device.
"""
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import threading
import time
import unittest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "software/libcariboulite/tools/radio_backend"


def reply(packet, *, result="OK", code="OK", state="RX", noise=1,
          carrier=0, opening=.2, closing=.3, identifier=None, actual=430125000,
          version=1, tx_requested=0, transition=0, tx_actual_power=-11,
          simulated=True, mode="nbfm", tx_actual=430225000, tx_source=1):
    tokens = packet.decode().split()
    identifier = tokens[1] if identifier is None else identifier
    backend = "simulated" if simulated else "live"
    base = (f"v{version} {identifier} {result} {code} 4 2 {backend} {state} RF24 "
            f"430125000 {actual} 1000000 {mode} {noise} {carrier} "
            f"{opening} {closing}")
    if version == 2:
        base += (f" 3 430225000 {tx_actual} 2000000 -12 {tx_actual_power} {tx_source} 1000 .5 2500 "
                 f"{tx_requested} 1 {transition}")
    return (base + "\n").encode()


class MockOwner:
    def __init__(self, path, handler, admission=None):
        self.path = path
        self.handler = handler
        self.admission = admission
        self.packets = []
        self.errors = []
        self.finished = threading.Event()
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.listener.bind(str(path))
        self.listener.listen(4)
        self.listener.settimeout(.1)
        self.worker = threading.Thread(target=self.serve, daemon=True)
        self.worker.start()

    def serve(self):
        connection_number = 0
        while not self.finished.is_set():
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            connection_number += 1
            with connection:
                connection.settimeout(.1)
                if self.admission is not None:
                    connection.sendall(self.admission)
                    connection.shutdown(socket.SHUT_WR)
                    # Keep BUSY observable even when the startup GET crosses
                    # the admission reply. Closing with unread Unix data can
                    # discard the reply and produce ECONNRESET on Linux.
                    try:
                        packet = connection.recv(4096)
                        if packet:
                            self.packets.append(packet.decode().split())
                    except (OSError, socket.timeout):
                        pass
                    continue
                while not self.finished.is_set():
                    try:
                        packet = connection.recv(4096)
                    except socket.timeout:
                        continue
                    except OSError:
                        break
                    if not packet:
                        break
                    self.packets.append(packet.decode().split())
                    try:
                        response = self.handler(connection_number, packet)
                        if response == b"":
                            break
                        if response is not None:
                            connection.sendall(response)
                    except Exception as error:
                        self.errors.append(error)
                        break

    def close(self):
        self.finished.set()
        self.listener.close()
        self.worker.join(timeout=1)


class RadioControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="cl-control-build-")
        cls.binary = Path(cls.build.name) / "cariboulite_radio_controller"
        subprocess.run([
            "cc", "-D_GNU_SOURCE", "-D_POSIX_C_SOURCE=200809L", "-std=c11",
            "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(SOURCE),
            str(SOURCE / "radio_controller.c"), str(SOURCE / "command_protocol.c"),
            "-lm", "-o", str(cls.binary),
        ], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cl-control-test-")
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "control.sock"

    def owner(self, handler=lambda number, packet: reply(packet), *, admission=None):
        owner = MockOwner(self.path, handler, admission=admission)
        self.addCleanup(owner.close)
        return owner

    def once(self, command, *, timeout=".2", socket_path=None):
        return subprocess.run([
            str(self.binary), "--socket", str(socket_path or self.path),
            "--timeout", timeout, "--command", command,
        ], capture_output=True, text=True, timeout=3)

    def wait_for_packets(self, owner, count):
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline and len(owner.packets) < count:
            time.sleep(.01)
        self.assertGreaterEqual(len(owner.packets), count, owner.errors)

    def assert_field(self, output, label, value):
        self.assertRegex(output,
                         rf"(?m)^\s*{re.escape(label)}\s+{re.escape(value)}$")

    def assert_status(self, output, state, *, simulated=True):
        labels = {
            "RX": "RX", "IDLE": "IDLE", "TX": "TX",
            "TX_STARTING": "TX", "TX_STOPPING": "TX", "RX_STARTING": "RX",
            "FPGA_PROGRAMMING": "IDLE", "FPGA_RESETTING": "IDLE",
            "FPGA_NEEDS_PROGRAM": "ERROR: FPGA needs programming",
            "FAULT": "ERROR: Backend fault; check backend log",
        }
        backend = "SIMULATED" if simulated else "LIVE"
        self.assertRegex(output,
                         rf"(?m)^\s*{backend} \| RF24 \| {re.escape(labels[state])}$")

    def assert_compact_sections(self, output, *, transmit=True):
        self.assertRegex(output, r"(?m)^\s*Receive$")
        self.assertEqual(len(re.findall(r"(?m)^\s*Transmit$", output)), int(transmit))
        fields = re.findall(r"(?m)^    (\S(?:.*?\S)?) {2,}\S", output)
        expected = ["Frequency", "Mode", "Sample rate", "Noise squelch", "Carrier squelch"]
        if transmit:
            expected += ["Frequency", "Power", "Mode", "Sample rate", "Audio source"]
        self.assertEqual(fields, expected)
        for omitted in ("requested", "Last tuned", "Tuned to", "Noise thresholds",
                        "estimate", "amplitude", "deviation", "After transmit",
                        "config=", "pipeline=", "Simulation only", "Transmit -"):
            self.assertNotIn(omitted, output)

    def test_status_only_queries_and_clearly_labels_simulation(self):
        owner = self.owner()
        result = self.once("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.wait_for_packets(owner, 1)
        self.assertEqual(owner.packets, [["v2", "1", "get"]])
        self.assert_status(result.stdout, "RX")
        self.assert_compact_sections(result.stdout, transmit=False)
        self.assert_field(result.stdout, "Frequency", "430.125000 MHz")
        self.assert_field(result.stdout, "Mode", "NBFM")
        self.assert_field(result.stdout, "Sample rate", "1 MS/s")
        self.assert_field(result.stdout, "Noise squelch", "On")
        self.assert_field(result.stdout, "Carrier squelch", "Off")
        self.assertNotIn("Transmit", result.stdout)  # v1 has no transmit snapshot.

    def test_live_wbfm_hides_inapplicable_noise_and_microphone_tone_settings(self):
        self.owner(lambda number, packet: reply(packet, version=2, simulated=False,
                                                mode="wbfm", tx_source=0))
        result = self.once("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_status(result.stdout, "RX", simulated=False)
        self.assert_compact_sections(result.stdout)
        self.assertNotIn("SIMULATED", result.stdout)
        self.assert_field(result.stdout, "Mode", "WBFM (mono)")
        self.assert_field(result.stdout, "Noise squelch", "Not Applicable")
        self.assert_field(result.stdout, "Audio source", "Microphone")
        self.assertNotRegex(result.stdout, r"(?m)^\s*Tone\s")

    def test_stopped_snapshot_shows_only_saved_frequency_and_power_settings(self):
        self.owner(lambda number, packet: reply(packet, version=2, state="IDLE", noise=0,
                                                actual=430124998, tx_actual=430224999))
        result = self.once("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_status(result.stdout, "IDLE")
        self.assert_compact_sections(result.stdout)
        receive, transmit = result.stdout.split("  Transmit\n", 1)
        self.assert_field(receive, "Frequency", "430.125000 MHz")
        self.assert_field(receive, "Noise squelch", "Off")
        self.assert_field(transmit, "Frequency", "430.225000 MHz")
        self.assert_field(transmit, "Power", "-12 dBm")
        self.assert_field(transmit, "Audio source", "Tone")
        self.assertNotIn("430.124998", result.stdout)
        self.assertNotIn("430.224999", result.stdout)
        self.assertNotIn("-11 dBm", result.stdout)
        self.assertNotIn("1000.0 Hz", result.stdout)

    def test_radio_states_stay_compact_and_only_flagged_transitions_show_refresh_hint(self):
        for state, transition in (("TX", 0), ("TX_STARTING", 0), ("TX_STARTING", 1), ("TX_STOPPING", 1),
                                  ("RX_STARTING", 1), ("FAULT", 0),
                                  ("FPGA_PROGRAMMING", 1), ("FPGA_RESETTING", 1),
                                  ("FPGA_NEEDS_PROGRAM", 0)):
            with self.subTest(state=state, transition=transition):
                owner = self.owner(lambda number, packet: reply(packet, version=2, state=state,
                                                                transition=transition))
                result = self.once("status")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assert_status(result.stdout, state)
                self.assert_compact_sections(result.stdout)
                if transition:
                    self.assertIn("Change in progress; type status to refresh.", result.stdout)
                else:
                    self.assertNotIn("Change in progress", result.stdout)
                owner.close()
                self.path.unlink()

    def test_frequency_mode_rate_and_stop_send_exact_units(self):
        for command, expected in (
            ("rx frequency 430.125", ["frequency", "430125000"]),
            ("rx freq 430.125", ["frequency", "430125000"]),
            ("frequency 430.125", ["frequency", "430125000"]),
            ("freq 430.125", ["frequency", "430125000"]),
            ("rx mode nbfm", ["mode", "nbfm"]),
            ("rx mode wbfm", ["mode", "wbfm"]),
            ("mode wbfm", ["mode", "wbfm"]),
            ("rx rate 1", ["sample_rate", "1000000"]),
            ("rx rate 2", ["sample_rate", "2000000"]),
            ("rx rate 4", ["sample_rate", "4000000"]),
            ("rx sample-rate 2", ["sample_rate", "2000000"]),
            ("rate 2", ["sample_rate", "2000000"]),
            ("sample-rate 2", ["sample_rate", "2000000"]),
            ("rx stop", ["stop"]),
            ("stop", ["stop"]),
            ("rx start", ["start"]),
            ("start", ["start"]),
        ):
            with self.subTest(command=command):
                owner = self.owner()
                result = self.once(command)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.wait_for_packets(owner, 2)
                self.assertEqual(owner.packets[0], ["v2", "1", "get"])
                self.assertEqual(owner.packets[1][2:], expected)
                owner.close()
                self.path.unlink()

    def test_partial_squelch_edits_refresh_and_preserve_other_fields(self):
        for prefix in ("", "squelch ", "rx squelch "):
            for edit, expected in (
                ("noise on", (1, 1, .18, .28)),
                ("noise off", (0, 1, .18, .28)),
                ("carrier on", (0, 1, .18, .28)),
                ("carrier off", (0, 0, .18, .28)),
                ("levels .21 .31", (0, 1, .21, .31)),
            ):
                command = prefix + edit
                with self.subTest(command=command):
                    gets = 0

                    def handler(number, packet):
                        nonlocal gets
                        if packet.decode().split()[2] == "get":
                            gets += 1
                        # Force the edit to use the second, refreshed GET,
                        # rather than the initial controller snapshot.
                        if gets == 1:
                            return reply(packet, noise=1, carrier=0, opening=.4, closing=.5)
                        return reply(packet, noise=0, carrier=1, opening=.18, closing=.28)

                    owner = self.owner(handler)
                    result = self.once(command)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.wait_for_packets(owner, 3)
                    self.assertEqual([packet[2] for packet in owner.packets],
                                     ["get", "get", "squelch"])
                    self.assertEqual(owner.packets[-1][3:5], [str(value) for value in expected[:2]])
                    self.assertAlmostEqual(float(owner.packets[-1][5]), expected[2], places=6)
                    self.assertAlmostEqual(float(owner.packets[-1][6]), expected[3], places=6)
                    owner.close()
                    self.path.unlink()

    def test_invalid_values_are_rejected_before_any_connection(self):
        for command in ("frequency nan", "frequency inf", "frequency -1",
                        "frequency 430.125junk", "rate 1.5", "rate 3",
                        "frequency 0x1", "rate 0x1", "levels 0x1 2",
                        "levels .3 .2", "levels .2 .2", "levels 0 .3",
                        "levels .2 8.1", "levels nan .3", "noise maybe",
                        "start extra", "tx frequency nan", "tx power 1.5",
                        "tx power 15", "tx power -15", "tx rate 3", "tx source file",
                        "tx tone 0 .5", "tx tone 3001 .5", "tx tone 1000 0",
                        "tx tone 1000 1.1", "tx tone 1000 1e-50", "tx tone 1e-50 .5",
                        "tx tone 0x1 .5", "tx sample-rate 3", "rx", "rx frequency",
                        "rx frequency nan", "rx frequency 430.125 extra", "rx mode am",
                        "rx rate 3", "rx sample-rate 1.5", "rx start extra", "squelch",
                        "squelch noise", "squelch noise maybe", "squelch noise on extra",
                        "squelch carrier true", "squelch levels .3 .2", "squelch levels .2",
                        "rx squelch noise maybe", "rx squelch carrier off extra",
                        "rx squelch levels .2 .3 extra", "rx squelch levels nan .3",
                        "fpga", "fpga reset", "fpga Program", "fpga program top.bin",
                        "fpga program extra words", "rx fpga program", "tx fpga program",
                        "squelch fpga program", "rx squelch fpga program", "fpga tx start",
                        "fpga soft", "fpga hard", "fpga Soft reset",
                        "fpga soft reset extra", "fpga hard reset top.bin", "fpga hard reset extra",
                        "rx fpga soft reset", "tx fpga hard reset", "squelch fpga hard reset",
                        "rx squelch fpga soft reset", "fpga soft program", ""):
            with self.subTest(command=command):
                result = self.once(command)
                self.assertEqual(result.returncode, 2)
                self.assertIn("Invalid command", result.stderr)
                self.assertNotIn("Cannot connect", result.stderr)

    def test_busy_admission_rejection_has_clear_message(self):
        owner = self.owner(lambda number, packet:
                           reply(packet, result="ERROR", code="BUSY", identifier="0"))
        result = self.once("start")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Another controller is already connected", result.stderr)
        self.assertNotIn("Protocol error", result.stderr)
        self.assertNotIn("uncertain", result.stderr)
        self.assertNotIn("SIMULATED", result.stdout)
        self.assertEqual([packet[2] for packet in owner.packets], ["get"])

    def test_fault_is_reported_as_error_and_unavailable_actual_frequency_is_hidden(self):
        self.owner(lambda number, packet: reply(packet, state="FAULT", actual=0))
        result = self.once("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_status(result.stdout, "FAULT")
        self.assert_compact_sections(result.stdout, transmit=False)
        self.assert_field(result.stdout, "Frequency", "430.125000 MHz")
        self.assertNotIn("unavailable", result.stdout)
        self.assertNotIn("0.000000 MHz", result.stdout)

    def test_unsolicited_busy_before_request_is_reported(self):
        self.owner(admission=reply(b"v1 1 get", result="ERROR", code="BUSY", identifier="0"))
        result = self.once("start")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Another controller is already connected", result.stderr)
        self.assertNotIn("Protocol error", result.stderr)
        self.assertNotIn("uncertain", result.stderr)

    def test_timeout_does_not_retry_uncertain_mutation(self):
        def handler(number, packet):
            return reply(packet) if packet.decode().split()[2] == "get" else None
        owner = self.owner(handler)
        started = time.monotonic()
        result = self.once("start", timeout=".1")
        self.assertEqual(result.returncode, 1)
        self.assertLess(time.monotonic() - started, .8)
        self.assertIn("outcome is uncertain", result.stderr)
        self.assertIn("was not retried", result.stderr)
        self.assertEqual([packet[2] for packet in owner.packets], ["get", "start"])

    def test_mismatched_reply_id_cannot_confirm_mutation(self):
        def handler(number, packet):
            return reply(packet, identifier="999") if packet.decode().split()[2] == "stop" else reply(packet)
        owner = self.owner(handler)
        result = self.once("stop")
        self.assertEqual(result.returncode, 1)
        self.assertIn("outcome is uncertain", result.stderr)
        self.assertEqual([packet[2] for packet in owner.packets], ["get", "stop"])

    def test_rejection_displays_confirmed_unchanged_settings(self):
        def handler(number, packet):
            if packet.decode().split()[2] == "frequency":
                return reply(packet, result="ERROR", code="RX_RUNNING")
            return reply(packet)
        owner = self.owner(handler)
        result = self.once("frequency 435")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Command rejected: RX_RUNNING", result.stderr)
        self.assert_field(result.stdout, "Frequency", "430.125000 MHz")
        self.assertIn("ERROR: Stop receive before changing settings", result.stdout)
        self.assertNotIn("uncertain", result.stderr)
        self.assertEqual(len(owner.packets), 2)

    def test_interactive_quit_and_eof_do_not_stop_receiver(self):
        for input_text in ("q\n", ""):
            with self.subTest(input_text=input_text):
                owner = self.owner()
                result = subprocess.run([
                    str(self.binary), "--socket", str(self.path), "--timeout", ".2",
                ], input=input_text, capture_output=True, text=True, timeout=3)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.wait_for_packets(owner, 1)
                self.assertEqual(owner.packets, [["v2", "1", "get"]])
                owner.close()
                self.path.unlink()

    def test_interactive_reconnect_gets_current_squelch_before_edit(self):
        def handler(number, packet):
            if number == 1:
                return b""
            return reply(packet, noise=0, carrier=1, opening=.21, closing=.31)
        owner = self.owner(handler)
        process = subprocess.Popen([
            str(self.binary), "--socket", str(self.path), "--timeout", ".2",
        ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(lambda: process.kill() if process.poll() is None else None)
        self.wait_for_packets(owner, 1)
        output, errors = process.communicate("levels .18 .28\nq\n", timeout=3)
        self.assertEqual(process.returncode, 0, errors)
        self.assertIn("SIMULATED", output)
        self.assertEqual([packet[2] for packet in owner.packets], ["get", "get", "squelch"])
        self.assertEqual(owner.packets[-1][3:5], ["0", "1"])
        self.assertAlmostEqual(float(owner.packets[-1][5]), .18, places=6)
        self.assertAlmostEqual(float(owner.packets[-1][6]), .28, places=6)

    def test_oversized_response_is_rejected(self):
        owner = self.owner(lambda number, packet: b"x" * 2048)
        result = self.once("status")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Reply unavailable", result.stderr)
        self.assertEqual(len(owner.packets), 1)

    def test_help_and_quit_need_no_backend(self):
        for command in ("help", "quit"):
            result = self.once(command)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("Cannot connect", result.stderr)

    def test_help_displays_symmetric_radio_commands_and_application_name(self):
        result = self.once("help")
        self.assertEqual(result.returncode, 0, result.stderr)
        for command in ("rx start", "rx stop", "rx frequency", "rx mode", "rx rate",
                        "squelch noise", "squelch carrier", "squelch levels",
                        "tx start", "tx stop", "tx frequency", "tx rate", "fpga program",
                        "fpga soft reset", "fpga hard reset"):
            with self.subTest(command=command):
                self.assertIn(command, result.stdout)
        self.assertIn("firmware/top.bin", result.stdout)
        self.assertIn("requires stopped RX/TX", result.stdout)
        self.assertIn("continues after disconnect", result.stdout)
        self.assertIn("keep the loaded firmware", result.stdout)
        self.assertIn("hardware reset", result.stdout)
        self.assertIn("run fpga program afterwards", result.stdout)
        result = subprocess.run([str(self.binary), "--help"], capture_output=True,
                                text=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("cariboulite_radio_controller", result.stdout)
        self.assertNotIn("cariboulite_radio_control ", result.stdout)

    def test_one_shot_tx_start_requires_interactive_session(self):
        result = self.once("tx start")
        self.assertEqual(result.returncode, 2)
        self.assertIn("requires an interactive controller session", result.stderr)
        self.assertNotIn("Cannot connect", result.stderr)

    def test_one_shot_fpga_program_sends_once_and_accepts_async_transition(self):
        def handler(number, packet):
            if packet.decode().split()[2] == "fpga_program":
                return reply(packet, version=2, state="FPGA_PROGRAMMING", code="ACCEPTED",
                             transition=1)
            return reply(packet, version=2, state="IDLE")

        owner = self.owner(handler)
        result = self.once("fpga program")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(owner.packets, [["v2", "1", "get"], ["v2", "2", "fpga_program"]])
        self.assertIn("Transition accepted", result.stdout)
        self.assert_status(result.stdout, "FPGA_PROGRAMMING")
        self.assertNotIn("uncertain", result.stderr)

    def test_fpga_program_rejection_displays_busy_radio_state(self):
        for code, state, message in (
            ("RX_RUNNING", "RX", "Stop receive before changing settings"),
            ("TX_RUNNING", "TX", "Stop transmit before changing settings"),
            ("TRANSITION", "FPGA_PROGRAMMING", "A change is already in progress"),
            ("HARDWARE", "IDLE", "Hardware operation failed; check backend log"),
            ("FIRMWARE_FILE", "IDLE", "FPGA firmware file unavailable"),
        ):
            with self.subTest(code=code):
                def handler(number, packet):
                    if packet.decode().split()[2] == "fpga_program":
                        return reply(packet, version=2, state=state, result="ERROR", code=code)
                    return reply(packet, version=2, state=state)

                owner = self.owner(handler)
                result = self.once("fpga program")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(f"Command rejected: {code}", result.stderr)
                self.assertIn(f"SIMULATED | RF24 | ERROR: {message}", result.stdout)
                self.assert_compact_sections(result.stdout)
                self.assertEqual([packet[2] for packet in owner.packets], ["get", "fpga_program"])
                owner.close()
                self.path.unlink()

    def test_uncertain_fpga_program_is_not_retried(self):
        def handler(number, packet):
            return reply(packet, version=2, state="IDLE") if packet.decode().split()[2] == "get" else None

        owner = self.owner(handler)
        result = self.once("fpga program", timeout=".1")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("outcome is uncertain", result.stderr)
        self.assertIn("was not retried", result.stderr)
        self.assertEqual([packet[2] for packet in owner.packets], ["get", "fpga_program"])

    def test_interactive_fpga_program_completion_requires_explicit_status(self):
        programmed = False

        def handler(number, packet):
            nonlocal programmed
            if packet.decode().split()[2] == "fpga_program":
                programmed = True
                return reply(packet, version=2, state="FPGA_PROGRAMMING", code="ACCEPTED",
                             transition=1)
            return reply(packet, version=2, state="IDLE" if programmed else "FAULT")

        owner = self.owner(handler)
        result = subprocess.run([str(self.binary), "--socket", str(self.path), "--timeout", ".2"],
                                input="fpga program\nstatus\nq\n", capture_output=True,
                                text=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([packet[2] for packet in owner.packets], ["get", "fpga_program", "get"])
        self.assert_status(result.stdout, "FPGA_PROGRAMMING")
        self.assert_status(result.stdout, "IDLE")

    def test_one_shot_fpga_resets_use_exact_packets_and_accept_transition(self):
        for command, wire in (("fpga soft reset", "fpga_soft_reset"),
                              ("fpga hard reset", "fpga_hard_reset")):
            with self.subTest(command=command):
                def handler(number, packet):
                    if packet.decode().split()[2] == wire:
                        return reply(packet, version=2, state="FPGA_RESETTING", code="ACCEPTED",
                                     transition=1)
                    return reply(packet, version=2, state="IDLE")

                owner = self.owner(handler)
                result = self.once(command)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(owner.packets, [["v2", "1", "get"], ["v2", "2", wire]])
                self.assertIn("Transition accepted", result.stdout)
                self.assert_status(result.stdout, "FPGA_RESETTING")
                owner.close()
                self.path.unlink()

    def test_uncertain_fpga_resets_are_never_retried(self):
        for command, wire in (("fpga soft reset", "fpga_soft_reset"),
                              ("fpga hard reset", "fpga_hard_reset")):
            with self.subTest(command=command):
                owner = self.owner(lambda number, packet:
                    reply(packet, version=2, state="IDLE")
                    if packet.decode().split()[2] == "get" else None)
                result = self.once(command, timeout=".1")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("outcome is uncertain", result.stderr)
                self.assertIn("was not retried", result.stderr)
                self.assertEqual([packet[2] for packet in owner.packets], ["get", wire])
                owner.close()
                self.path.unlink()

    def test_reset_completion_states_are_displayed_only_on_explicit_status(self):
        for command, wire, completed in (("fpga soft reset", "fpga_soft_reset", "IDLE"),
                                          ("fpga hard reset", "fpga_hard_reset", "FPGA_NEEDS_PROGRAM")):
            with self.subTest(command=command):
                reset = False

                def handler(number, packet):
                    nonlocal reset
                    if packet.decode().split()[2] == wire:
                        reset = True
                        return reply(packet, version=2, state="FPGA_RESETTING", code="ACCEPTED",
                                     transition=1)
                    return reply(packet, version=2, state=completed if reset else "IDLE")

                owner = self.owner(handler)
                result = subprocess.run([str(self.binary), "--socket", str(self.path), "--timeout", ".2"],
                                        input=f"{command}\nstatus\nq\n", capture_output=True,
                                        text=True, timeout=3)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual([packet[2] for packet in owner.packets], ["get", wire, "get"])
                self.assert_status(result.stdout, "FPGA_RESETTING")
                self.assert_status(result.stdout, completed)
                owner.close()
                self.path.unlink()

    def test_post_hard_reset_rejection_explains_programming_required(self):
        def handler(number, packet):
            if packet.decode().split()[2] == "start":
                return reply(packet, version=2, state="FPGA_NEEDS_PROGRAM", result="ERROR",
                             code="FPGA_REQUIRED")
            return reply(packet, version=2, state="FPGA_NEEDS_PROGRAM")

        owner = self.owner(handler)
        result = self.once("rx start")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("Command rejected: FPGA_REQUIRED", result.stderr)
        self.assertIn("Run fpga program", result.stderr)
        self.assert_status(result.stdout, "FPGA_NEEDS_PROGRAM")
        self.assertEqual([packet[2] for packet in owner.packets], ["get", "start"])

    def test_tx_configuration_commands_use_exact_v2_fields(self):
        for command, expected in (
            ("tx frequency 430.225", ["tx_frequency", "430225000"]),
            ("tx power -12", ["tx_power", "-12"]),
            ("tx rate 2", ["tx_sample_rate", "2000000"]),
            ("tx sample-rate 2", ["tx_sample_rate", "2000000"]),
            ("tx source tone", ["tx_source", "tone"]),
            ("tx source mic", ["tx_source", "mic"]),
            ("tx tone 1000 .5", ["tx_tone", "1000", "0.5"]),
            ("tx stop", ["tx_stop"]),
        ):
            with self.subTest(command=command):
                owner = self.owner(lambda number, packet: reply(packet, version=2))
                result = self.once(command)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(owner.packets, [["v2", "1", "get"], ["v2", "2", *expected]])
                self.assert_field(result.stdout, "Frequency", "430.225000 MHz")
                self.assert_field(result.stdout, "Power", "-12 dBm")
                self.assert_compact_sections(result.stdout)
                owner.close()
                self.path.unlink()

    def test_unavailable_tx_power_and_frequency_do_not_add_diagnostic_fields(self):
        for state in ("RX", "TX"):
            with self.subTest(state=state):
                owner = self.owner(lambda number, packet: reply(packet, version=2, state=state,
                                                                  tx_actual=0,
                                                                  tx_actual_power=-2147483648))
                result = self.once("status")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assert_field(result.stdout, "Power", "-12 dBm")
                self.assert_field(result.stdout, "Frequency", "430.225000 MHz")
                self.assert_compact_sections(result.stdout)
                self.assertNotIn("unavailable", result.stdout)
                self.assertNotIn("-2147483648", result.stdout)
                self.assertNotIn("0.000000 MHz", result.stdout)
                owner.close()
                self.path.unlink()

    def test_interactive_tx_acknowledges_transition_without_auto_poll(self):
        def handler(number, packet):
            command = packet.decode().split()[2]
            if command == "tx_start":
                return reply(packet, version=2, state="TX_STARTING", code="ACCEPTED",
                             tx_requested=1, transition=1)
            if command == "tx_stop":
                return reply(packet, version=2, state="TX_STOPPING", code="ACCEPTED",
                             transition=1)
            return reply(packet, version=2)
        owner = self.owner(handler)
        result = subprocess.run([str(self.binary), "--socket", str(self.path), "--timeout", ".2"],
                                input="tx start\nstatus\ntx stop\nq\n", capture_output=True,
                                text=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([packet[2] for packet in owner.packets], ["get", "tx_start", "get", "tx_stop"])
        self.assertEqual(result.stdout.count("Transition accepted"), 2)
        self.assert_status(result.stdout, "TX_STARTING")
        self.assert_status(result.stdout, "TX_STOPPING")
        self.assertIn("q disconnects and unkeys", result.stdout)
        self.assertIn("status", result.stdout)

    def test_uncertain_tx_start_is_not_retried(self):
        def handler(number, packet):
            return reply(packet, version=2) if packet.decode().split()[2] == "get" else None
        owner = self.owner(handler)
        result = subprocess.run([str(self.binary), "--socket", str(self.path), "--timeout", ".1"],
                                input="tx start\nq\n", capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("outcome is uncertain", result.stderr)
        self.assertIn("was not retried", result.stderr)
        self.assertEqual([packet[2] for packet in owner.packets], ["get", "tx_start"])


if __name__ == "__main__":
    unittest.main()
