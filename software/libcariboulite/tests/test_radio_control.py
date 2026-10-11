#!/usr/bin/env python3
"""Exercise the C radio controller against isolated mock seqpacket peers.

These tests never launch a hardware backend or open a radio/audio device.
"""
from pathlib import Path
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
          version=1, tx_requested=0, transition=0, tx_actual_power=-11):
    tokens = packet.decode().split()
    identifier = tokens[1] if identifier is None else identifier
    base = (f"v{version} {identifier} {result} {code} 4 2 simulated {state} RF24 "
            f"430125000 {actual} 1000000 nbfm {noise} {carrier} "
            f"{opening} {closing}")
    if version == 2:
        base += (f" 3 430225000 430225000 2000000 -12 {tx_actual_power} 1 1000 .5 2500 "
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

    def test_status_only_queries_and_clearly_labels_simulation(self):
        owner = self.owner()
        result = self.once("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.wait_for_packets(owner, 1)
        self.assertEqual(owner.packets, [["v2", "1", "get"]])
        self.assertIn("SIMULATED RF24 RX", result.stdout)
        self.assertIn("requested 430.125000 MHz, actual 430.125000 MHz", result.stdout)
        self.assertIn("noise on, carrier off, open 0.200, close 0.300", result.stdout)

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
                        "rx squelch levels .2 .3 extra", "rx squelch levels nan .3", ""):
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

    def test_unavailable_actual_frequency_is_not_shown_as_zero_mhz(self):
        self.owner(lambda number, packet: reply(packet, state="FAULT", actual=0))
        result = self.once("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("SIMULATED RF24 FAULT", result.stdout)
        self.assertIn("actual unavailable", result.stdout)
        self.assertNotIn("actual 0.000000 MHz", result.stdout)

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
        self.assertIn("actual 430.125000 MHz", result.stdout)
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
                        "tx start", "tx stop", "tx frequency", "tx rate"):
            with self.subTest(command=command):
                self.assertIn(command, result.stdout)
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
                self.assertIn("TX: requested 430.225000 MHz", result.stdout)
                self.assertIn("power requested -12 dBm, estimate -11 dBm", result.stdout)
                self.assertIn("deviation +/-2500 Hz", result.stdout)
                owner.close()
                self.path.unlink()

    def test_unavailable_tx_power_is_not_shown_as_integer_sentinel(self):
        self.owner(lambda number, packet: reply(packet, version=2, tx_actual_power=-2147483648))
        result = self.once("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("power requested -12 dBm, estimate unavailable", result.stdout)
        self.assertNotIn("-2147483648", result.stdout)

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
        self.assertIn("SIMULATED RF24 TX_STARTING", result.stdout)
        self.assertIn("SIMULATED RF24 TX_STOPPING", result.stdout)
        self.assertIn("q disconnects and unkeys", result.stdout)

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
