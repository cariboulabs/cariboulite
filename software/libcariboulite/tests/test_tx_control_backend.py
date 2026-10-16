#!/usr/bin/env python3
"""Exercise half-duplex TX control using only simulated backend sessions.

Build first with make -C software/libcariboulite/tools/radio_backend.
Every backend launch includes --dry-run; these tests never key real hardware.
"""
import importlib.util
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time
import unittest


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    "tx_backend_rx_helpers", Path(__file__).with_name("test_rx_control_backend.py"))
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
CONTROLLER = helpers.BACKEND.with_name("cariboulite_radio_controller")


def parse_reply(packet):
    fields = packet.decode("ascii").split()
    if len(fields) != 30 or fields[0] != "v2":
        raise AssertionError(f"Invalid TX command reply: {packet!r}")
    reply = helpers.parse_reply(" ".join(["v1", *fields[1:17]]).encode())
    reply.update({
        "tx_pipeline_generation": int(fields[17]),
        "tx_requested_frequency": float(fields[18]),
        "tx_actual_frequency": float(fields[19]),
        "tx_rate": int(fields[20]),
        "tx_requested_power": int(fields[21]),
        "tx_actual_power": int(fields[22]),
        "tx_source": "tone" if fields[23] == "1" else "mic",
        "tx_tone_hz": float(fields[24]), "tx_tone_amp": float(fields[25]),
        "tx_deviation": float(fields[26]), "tx_requested": fields[27] == "1",
        "rx_resume": fields[28] == "1", "transition": fields[29] == "1",
    })
    for index in (23, 27, 28, 29):
        if fields[index] not in ("0", "1"):
            raise AssertionError(f"Invalid TX boolean field: {packet!r}")
    return reply


class TxControlBackendTests(unittest.TestCase):
    # Reuse resource management, not the RX-only test methods themselves.
    setUpClass = helpers.RxControlBackendTests.__dict__["setUpClass"]
    setUp = helpers.RxControlBackendTests.setUp
    stop_backend = staticmethod(helpers.RxControlBackendTests.stop_backend)
    start_backend = helpers.RxControlBackendTests.start_backend
    connect = helpers.RxControlBackendTests.connect
    controller = helpers.RxControlBackendTests.controller
    snapshot_matching = helpers.RxControlBackendTests.snapshot_matching

    def exchange(self, peer, packet):
        peer.sendall(packet)
        response, _, flags, _ = peer.recvmsg(4096)
        self.assertFalse(flags & socket.MSG_TRUNC)
        reply = parse_reply(response)
        self.assertEqual(reply["source"], "simulated")
        return reply

    def command(self, peer, command, code="OK"):
        self.request_id += 1
        reply = self.exchange(peer, f"v2 {self.request_id} {command}".encode())
        self.assertEqual(reply["id"], self.request_id)
        self.assertEqual(reply["code"], code, reply)
        self.assertEqual(reply["result"], "OK" if code in ("OK", "ACCEPTED") else "ERROR")
        return reply

    @staticmethod
    def settings(reply):
        return {key: value for key, value in reply.items()
                if key not in ("id", "result", "code")}

    def wait_state(self, peer, wanted):
        deadline = time.monotonic() + 3
        latest = None
        while time.monotonic() < deadline:
            latest = self.command(peer, "get")
            if latest["state"] == wanted and not latest["transition"]:
                return latest
            time.sleep(.015)
        self.fail(f"Backend did not complete transition to {wanted}: {latest!r}")

    def assert_simulated(self, snapshot, requested=False):
        self.assertEqual(snapshot["source"]["kind"], "simulated")
        self.assertEqual(snapshot["state"], "DRY_RUN")
        self.assertIs(snapshot["rx"]["running"], False)
        self.assertIs(snapshot["tx"]["running"], False)
        self.assertIs(snapshot["tx"]["requested"], requested)
        self.assertIsNone(snapshot["tx"]["queue"])
        self.assertIsNone(snapshot["tx"]["writer"])
        self.assertEqual(snapshot["tx"]["capture"], {"valid": False})
        self.assertIsNone(snapshot["tx"]["hardware_paced"])
        self.assertEqual(snapshot["tx"]["driver"], {"valid": False})
        self.assertEqual(snapshot["tx"]["audio_buffer"], {"valid": False})
        self.assertEqual(snapshot["tx"]["smi_timing"], {"valid": False})
        self.assertEqual(snapshot["hardware"]["register_poll"]["polls"], 0)
        for worker in ("producer", "writer", "capture"):
            self.assertIs(snapshot["tx"]["workers"][worker]["created"], False)
            self.assertIs(snapshot["tx"]["workers"][worker]["alive"], False)

    def test_v2_defaults_and_legacy_v1_reply_remain_available(self):
        self.start_backend()
        peer = self.controller()
        reply = self.command(peer, "get")
        self.assertEqual(reply["state"], "RX")
        self.assertFalse(reply["tx_requested"])
        self.assertFalse(reply["transition"])
        self.assertEqual(reply["tx_requested_frequency"], 430125000)
        self.assertEqual(reply["tx_rate"], 1000000)
        self.assertEqual(reply["tx_requested_power"], -12)
        self.assertEqual(reply["tx_source"], "mic")
        self.assertAlmostEqual(reply["tx_tone_hz"], 600)
        self.assertAlmostEqual(reply["tx_tone_amp"], .4)
        self.assertEqual(reply["tx_deviation"], 2500)
        peer.sendall(b"v1 999 get")
        legacy = helpers.parse_reply(peer.recv(4096))
        self.assertEqual(legacy["id"], 999)
        self.assertEqual(legacy["state"], "RX")
        snapshot = self.snapshot_matching(self.connect(self.status_path))
        self.assert_simulated(snapshot)

    def test_saved_tx_settings_can_change_during_rx_without_restarting_it(self):
        self.start_backend()
        peer = self.controller()
        initial = self.command(peer, "get")
        edits = (
            ("tx_frequency 430130000", "tx_requested_frequency", 430130000),
            ("tx_power 3", "tx_requested_power", 3),
            ("tx_sample_rate 2000000", "tx_rate", 2000000),
            ("tx_source tone", "tx_source", "tone"),
        )
        latest = initial
        for command, field, expected in edits:
            with self.subTest(command=command):
                before = latest
                latest = self.command(peer, command)
                self.assertEqual(latest[field], expected)
                self.assertEqual(latest["state"], "RX")
                self.assertEqual(latest["pipeline_generation"], initial["pipeline_generation"])
                self.assertEqual(latest["tx_pipeline_generation"], initial["tx_pipeline_generation"])
                self.assertEqual(latest["config_generation"], before["config_generation"] + 1)
                self.assertEqual(self.settings(self.command(peer, command)), self.settings(latest))
        latest = self.command(peer, "tx_tone 700 .5")
        self.assertAlmostEqual(latest["tx_tone_hz"], 700)
        self.assertAlmostEqual(latest["tx_tone_amp"], .5)
        self.assertEqual(latest["tx_deviation"], 2500)

    def test_invalid_tx_settings_preserve_configuration(self):
        self.start_backend()
        peer = self.controller()
        before = self.command(peer, "get")
        invalid = (
            "tx_frequency 999999", "tx_frequency 6000000000", "tx_frequency nan",
            "tx_power -13", "tx_power 15", "tx_power 1.5",
            "tx_sample_rate 3000000", "tx_sample_rate 1000001",
            "tx_source file", "tx_tone 0 .4", "tx_tone 3001 .4",
            "tx_tone 600 0", "tx_tone 600 1.1", "tx_tone nan .4",
            "tx_tone 600 inf", "tx_tone 600 .4 extra",
        )
        for command in invalid:
            with self.subTest(command=command):
                self.request_id += 1
                denied = self.exchange(peer, f"v2 {self.request_id} {command}".encode())
                self.assertEqual(denied["result"], "ERROR")
                self.assertIn(denied["code"], ("INVALID", "FREQ_RANGE", "PROTOCOL"))
                self.assertEqual(self.settings(denied), self.settings(before))
                self.assertEqual(self.settings(self.command(peer, "get")), self.settings(before))

    def test_tx_from_rx_restores_saved_receiver_with_fresh_pipeline(self):
        self.start_backend()
        peer = self.controller()
        self.command(peer, "squelch 1 1 .18 .28")
        before = self.command(peer, "get")
        starting = self.command(peer, "tx_start", "ACCEPTED")
        self.assertEqual(starting["state"], "TX_STARTING")
        self.assertTrue(starting["transition"])
        transmitting = self.wait_state(peer, "TX")
        self.assertTrue(transmitting["tx_requested"])
        self.assertTrue(transmitting["rx_resume"])
        self.assertEqual(transmitting["tx_pipeline_generation"], before["tx_pipeline_generation"] + 1)
        for command in ("frequency 430130000", "mode wbfm", "sample_rate 2000000"):
            with self.subTest(command=command):
                denied = self.command(peer, command, "TX_RUNNING")
                self.assertEqual(self.settings(denied), self.settings(transmitting))
        for command in ("tx_frequency 430130000", "tx_power 3", "tx_source tone",
                        "tx_sample_rate 2000000", "tx_tone 700 .5"):
            with self.subTest(command=command):
                denied = self.command(peer, command, "TX_RUNNING")
                self.assertEqual(self.settings(denied), self.settings(transmitting))
        repeated = self.command(peer, "tx_start")
        self.assertEqual(self.settings(repeated), self.settings(transmitting))
        stopping = self.command(peer, "tx_stop", "ACCEPTED")
        self.assertEqual(stopping["state"], "TX_STOPPING")
        self.assertTrue(stopping["transition"])
        resumed = self.wait_state(peer, "RX")
        self.assertFalse(resumed["tx_requested"])
        self.assertEqual(resumed["pipeline_generation"], before["pipeline_generation"] + 1)
        for field in ("requested", "rate", "mode", "noise", "carrier", "open", "close"):
            self.assertEqual(resumed[field], before[field])
        self.assertEqual(self.settings(self.command(peer, "tx_stop")), self.settings(resumed))

    def test_tx_from_idle_returns_to_idle_and_transitions_reject_edits(self):
        self.start_backend(start=False)
        peer = self.controller()
        before = self.command(peer, "get")
        self.assertEqual(before["state"], "IDLE")
        self.assertEqual(before["pipeline_generation"], 0)
        self.assertFalse(before["rx_resume"])
        starting = self.command(peer, "tx_start", "ACCEPTED")
        self.assertTrue(starting["transition"])
        # If the asynchronous job finishes between replies, the stable TX
        # rejection is equally valid; the requested edit must never apply.
        self.request_id += 1
        denied = self.exchange(peer, f"v2 {self.request_id} tx_power 3".encode())
        self.assertEqual(denied["result"], "ERROR")
        self.assertIn(denied["code"], ("TRANSITION", "TX_RUNNING"))
        self.assertEqual(denied["tx_requested_power"], before["tx_requested_power"])
        transmitting = self.wait_state(peer, "TX")
        self.assertFalse(transmitting["rx_resume"])
        self.command(peer, "tx_stop", "ACCEPTED")
        idle = self.wait_state(peer, "IDLE")
        self.assertFalse(idle["tx_requested"])
        self.assertFalse(idle["rx_resume"])
        self.assertEqual(idle["pipeline_generation"], before["pipeline_generation"])
        self.assertEqual(idle["tx_pipeline_generation"], transmitting["tx_pipeline_generation"])

    def test_rf09_power_range_and_supported_tx_rate_endpoints(self):
        self.start_backend("--channel", "RF09", start=False)
        peer = self.controller()
        for power in (-14, 14):
            with self.subTest(power=power):
                self.assertEqual(self.command(peer, f"tx_power {power}")["tx_requested_power"], power)
        previous = self.command(peer, "get")
        for power in (-15, 15):
            with self.subTest(power=power):
                denied = self.command(peer, f"tx_power {power}", "INVALID")
                self.assertEqual(self.settings(denied), self.settings(previous))
        for rate in (1000000, 2000000, 4000000):
            with self.subTest(rate=rate):
                self.assertEqual(self.command(peer, f"tx_sample_rate {rate}")["tx_rate"], rate)
        tone = self.command(peer, "tx_tone 3000 1")
        self.assertEqual(tone["tx_tone_hz"], 3000)
        self.assertEqual(tone["tx_tone_amp"], 1)

    def test_every_tx_session_uses_fresh_generation_with_saved_settings(self):
        self.start_backend(start=False)
        peer = self.controller()
        self.command(peer, "tx_start", "ACCEPTED")
        first = self.wait_state(peer, "TX")
        self.command(peer, "tx_stop", "ACCEPTED")
        self.wait_state(peer, "IDLE")
        self.command(peer, "tx_start", "ACCEPTED")
        second = self.wait_state(peer, "TX")
        self.assertEqual(second["tx_pipeline_generation"], first["tx_pipeline_generation"] + 1)
        for field in ("tx_requested_frequency", "tx_rate", "tx_requested_power", "tx_source",
                      "tx_tone_hz", "tx_tone_amp", "tx_deviation"):
            self.assertEqual(second[field], first[field])
        self.command(peer, "tx_stop", "ACCEPTED")
        self.wait_state(peer, "IDLE")
        self.command(peer, "tx_tone 700 .5")
        self.command(peer, "tx_start", "ACCEPTED")
        recreated = self.wait_state(peer, "TX")
        self.assertEqual(recreated["tx_pipeline_generation"], second["tx_pipeline_generation"] + 1)
        self.assertAlmostEqual(recreated["tx_tone_hz"], 700)
        self.assertAlmostEqual(recreated["tx_tone_amp"], .5)
        self.command(peer, "tx_stop", "ACCEPTED")
        self.wait_state(peer, "IDLE")

    def test_controller_disconnect_stops_tx_and_resumes_previous_rx(self):
        process = self.start_backend()
        peer = self.controller()
        before = self.command(peer, "get")
        self.command(peer, "tx_start", "ACCEPTED")
        self.wait_state(peer, "TX")
        peer.close()
        # Allow the backend's disconnected-controller cleanup to complete.
        time.sleep(.35)
        reconnected = self.controller()
        resumed = self.wait_state(reconnected, "RX")
        self.assertFalse(resumed["tx_requested"])
        self.assertEqual(resumed["pipeline_generation"], before["pipeline_generation"] + 1)
        self.assertIsNone(process.poll(), "Controller disconnect terminated the radio owner")

    def test_controller_disconnect_during_opening_transition_resumes_rx(self):
        process = self.start_backend("--status-hz", "20")
        peer = self.controller()
        status = self.connect(self.status_path)
        before = self.command(peer, "get")
        starting = self.command(peer, "tx_start", "ACCEPTED")
        self.assertEqual(starting["state"], "TX_STARTING")
        peer.close()
        resumed = self.snapshot_matching(status, lambda snapshot:
            snapshot["control"]["state"] == "RX" and
            snapshot["control"]["pipeline_generation"] == before["pipeline_generation"] + 1)
        self.assertFalse(resumed["control"]["tx_requested"])
        self.assertFalse(resumed["control"]["transition"])
        self.assertEqual(resumed["control"]["tx_pipeline_generation"], 1)
        self.assertIsNone(process.poll())
        replacement = self.controller()
        self.assertEqual(self.command(replacement, "get")["state"], "RX")

    def test_stop_queued_during_opening_transition_completes_rx_handoff(self):
        self.start_backend("--status-hz", "20")
        peer = self.controller()
        before = self.command(peer, "get")
        starting = self.command(peer, "tx_start", "ACCEPTED")
        self.assertEqual(starting["state"], "TX_STARTING")
        stopped = self.command(peer, "tx_stop", "ACCEPTED")
        # The stop must remain accepted if the opening transition completes
        # between socket exchanges; either state still represents a pending stop.
        self.assertIn(stopped["state"], ("TX_STARTING", "TX_STOPPING"))
        self.assertTrue(stopped["transition"])
        resumed = self.wait_state(peer, "RX")
        self.assertFalse(resumed["tx_requested"])
        self.assertFalse(resumed["rx_resume"])
        self.assertEqual(resumed["pipeline_generation"], before["pipeline_generation"] + 1)
        self.assertEqual(resumed["tx_pipeline_generation"], 1)

    def test_built_interactive_controller_starts_stops_and_releases_owned_tx(self):
        self.assertTrue(CONTROLLER.is_file(), "Build the C controller before running this test")
        backend = self.start_backend("--status-hz", "20")
        status = self.connect(self.status_path)
        before = self.snapshot_matching(status)
        client = subprocess.Popen(
            [str(CONTROLLER), "--socket", str(self.control_path)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=helpers.ENVIRONMENT)
        self.addCleanup(self.stop_backend, client)

        def send(command):
            self.assertIsNone(client.poll(), "Interactive controller exited before the command")
            client.stdin.write((command + "\n").encode())
            client.stdin.flush()

        def state(wanted, generation=None):
            return self.snapshot_matching(status, lambda snapshot:
                snapshot["control"]["state"] == wanted and
                not snapshot["control"]["transition"] and
                (generation is None or snapshot["control"]["pipeline_generation"] == generation))

        send("tx source tone")
        send("tx tone 700 .5")
        configured = self.snapshot_matching(status, lambda snapshot:
            snapshot["config"]["tx_source"] == "tone" and
            snapshot["config"]["tx_tone_hz"] == 700 and
            snapshot["config"]["tx_tone_amp"] == .5)
        self.assertEqual(configured["control"]["state"], "RX")
        send("tx start")
        first_tx = state("TX")
        self.assertTrue(first_tx["control"]["tx_requested"])
        send("status")
        send("tx stop")
        generation = before["control"]["pipeline_generation"] + 1
        resumed = state("RX", generation)
        self.assertFalse(resumed["control"]["tx_requested"])
        send("tx start")
        second_tx = state("TX")
        self.assertEqual(second_tx["control"]["tx_pipeline_generation"],
                         first_tx["control"]["tx_pipeline_generation"] + 1)
        send("status")
        # EOF follows the same real client disconnect path as leaving its terminal.
        client.stdin.close()
        client.stdin = None
        output, errors = client.communicate(timeout=3)
        self.assertEqual(client.returncode, 0, errors.decode())
        self.assertEqual(errors, b"")
        text = output.decode()
        self.assertIn("SIMULATED | RF24 | TX", text)
        self.assertIn("Transition accepted", text)
        self.assertRegex(text, r"Audio source\s+Tone")
        self.assertNotIn("700.0 Hz", text)
        self.assertNotIn("amplitude", text)
        released = state("RX", generation + 1)
        self.assertFalse(released["control"]["tx_requested"])
        self.assertFalse(released["control"]["rx_resume"])
        self.assertIsNone(backend.poll(), "Client EOF terminated the backend")
        replacement = self.controller()
        self.assertEqual(self.command(replacement, "get")["state"], "RX")

    def test_passive_monitor_survives_half_duplex_transition_with_truthful_simulation(self):
        self.start_backend("--status-hz", "20")
        peer = self.controller()
        status = self.connect(self.status_path)
        first = self.snapshot_matching(status)
        self.command(peer, "tx_start", "ACCEPTED")
        transmitting = self.wait_state(peer, "TX")
        tx_status = self.snapshot_matching(status, lambda snapshot:
            snapshot.get("control", {}).get("state") == "TX")
        self.assert_simulated(tx_status, requested=True)
        self.assertEqual(tx_status["session"], first["session"])
        self.assertEqual(tx_status["control"]["tx_pipeline_generation"],
                         transmitting["tx_pipeline_generation"])
        self.command(peer, "tx_stop", "ACCEPTED")
        resumed = self.wait_state(peer, "RX")
        rx_status = self.snapshot_matching(status, lambda snapshot:
            snapshot.get("control", {}).get("state") == "RX" and
            snapshot.get("control", {}).get("pipeline_generation") == resumed["pipeline_generation"])
        self.assert_simulated(rx_status)
        self.assertGreater(rx_status["sequence"], tx_status["sequence"])
        rendered = "\n".join(helpers.monitor.format_snapshot(tx_status))
        self.assertIn("SIMULATED DATA", rendered)

    def test_sigterm_during_tx_clears_intent_and_cleans_owned_sockets(self):
        process = self.start_backend()
        peer = self.controller()
        status = self.connect(self.status_path)
        self.snapshot_matching(status)
        self.command(peer, "tx_start", "ACCEPTED")
        self.wait_state(peer, "TX")
        process.send_signal(signal.SIGTERM)
        final = self.snapshot_matching(status, lambda snapshot: snapshot["state"] == "STOPPED")
        self.assertFalse(final["tx"]["requested"])
        self.assertFalse(final["control"]["tx_requested"])
        self.assertFalse(final["rx"]["running"])
        self.assertFalse(final["tx"]["running"])
        _, errors = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, errors.decode())
        self.assertFalse(self.status_path.exists())
        self.assertFalse(self.control_path.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
