#!/usr/bin/env python3
"""Exercise the receiver command socket without touching radio hardware.

Build first with make -C software/libcariboulite/tools/radio_backend.
All executable launches use --dry-run. Commands use isolated Unix seqpacket
sockets; the existing passive monitor parses the backend's real snapshots.
"""
import importlib.util
import os
from pathlib import Path
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[3]
TOOLS = ROOT / "software/libcariboulite/tools"
BACKEND = ROOT / "build/radio-backend/cariboulite_rx_backend"
MONITOR = TOOLS / "cariboulite_monitor.py"
sys.dont_write_bytecode = True
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("rx_control_monitor_test", MONITOR)
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)
ENVIRONMENT = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")


def parse_reply(packet):
    fields = packet.decode("ascii").split()
    if len(fields) != 17 or fields[0] != "v1":
        raise AssertionError(f"Invalid command reply: {packet!r}")
    if fields[2] not in ("OK", "ERROR"):
        raise AssertionError(f"Invalid result: {packet!r}")
    if fields[6] not in ("live", "simulated"):
        raise AssertionError(f"Invalid source: {packet!r}")
    if fields[13] not in ("0", "1") or fields[14] not in ("0", "1"):
        raise AssertionError(f"Invalid squelch flags: {packet!r}")
    return {
        "id": int(fields[1]), "result": fields[2], "code": fields[3],
        "config_generation": int(fields[4]), "pipeline_generation": int(fields[5]),
        "source": fields[6], "state": fields[7], "channel": fields[8],
        "requested": float(fields[9]), "actual": float(fields[10]),
        "rate": int(fields[11]), "mode": fields[12],
        "noise": fields[13] == "1", "carrier": fields[14] == "1",
        "open": float(fields[15]), "close": float(fields[16]),
    }


class RxControlBackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BACKEND.is_file():
            raise RuntimeError("Build the backend first: make -C "
                               "software/libcariboulite/tools/radio_backend")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cl-rx-control-")
        self.addCleanup(self.temporary.cleanup)
        self.status_path = Path(self.temporary.name) / "status.sock"
        self.control_path = self.status_path.with_name("control.sock")
        self.request_id = 0

    @staticmethod
    def stop_backend(process):
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
        try:
            process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate(timeout=3)

    def start_backend(self, *arguments, seconds="10", control_path=None):
        if control_path is not None:
            self.control_path = Path(control_path)
            arguments = (*arguments, "--control-socket", str(control_path))
        process = subprocess.Popen(
            [str(BACKEND), "--dry-run", "--socket", str(self.status_path),
             "--seconds", seconds, *arguments],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=ENVIRONMENT)
        self.addCleanup(self.stop_backend, process)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if process.poll() is not None:
                output, errors = process.communicate()
                self.fail(f"Backend exited before serving: {output!r} {errors!r}")
            if self.status_path.exists() and self.control_path.exists():
                return process
            time.sleep(.01)
        self.fail("Backend did not create both sockets")

    def connect(self, path):
        peer = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.addCleanup(peer.close)
        peer.settimeout(2)
        peer.connect(str(path))
        return peer

    def controller(self):
        return self.connect(self.control_path)

    def exchange(self, peer, packet):
        peer.sendall(packet)
        reply, _, flags, _ = peer.recvmsg(4096)
        self.assertFalse(flags & socket.MSG_TRUNC)
        parsed = parse_reply(reply)
        self.assertEqual(parsed["source"], "simulated")
        return parsed

    def command(self, peer, command, code="OK"):
        self.request_id += 1
        response = self.exchange(peer, f"v1 {self.request_id} {command}".encode())
        self.assertEqual(response["id"], self.request_id)
        self.assertEqual(response["code"], code, response)
        self.assertEqual(response["result"], "OK" if code == "OK" else "ERROR")
        return response

    @staticmethod
    def configuration(reply):
        return {key: value for key, value in reply.items()
                if key not in ("id", "result", "code")}

    def snapshot_matching(self, peer, predicate=lambda snapshot: True):
        deadline = time.monotonic() + 2
        last = None
        while time.monotonic() < deadline:
            packet, _, flags, _ = peer.recvmsg(monitor.MAX_PACKET_BYTES)
            last = monitor.parse_snapshot(packet, truncated=bool(flags & socket.MSG_TRUNC))
            if predicate(last):
                return last
        self.fail(f"Status did not reflect the acknowledged configuration: {last!r}")

    def assert_truthful_simulation(self, snapshot, reply):
        self.assertEqual(snapshot["state"], "DRY_RUN")
        self.assertEqual(snapshot["source"]["kind"], "simulated")
        self.assertIs(snapshot["rx"]["running"], False)
        self.assertIs(snapshot["tx"]["running"], False)
        self.assertEqual(snapshot["control"], {
            "config_generation": reply["config_generation"],
            "pipeline_generation": reply["pipeline_generation"],
            "rx_requested": reply["state"] == "RX", "state": reply["state"],
        })
        self.assertIsNone(snapshot["rx"]["queue"])
        self.assertIsNone(snapshot["rx"]["audio_queue"])
        self.assertEqual(snapshot["hardware"]["register_poll"]["polls"], 0)

    def test_default_intent_permissions_and_timed_cleanup(self):
        process = self.start_backend(seconds="1")
        reply = self.command(self.controller(), "get")
        self.assertEqual(reply["state"], "RX")
        self.assertEqual(reply["channel"], "RF24")
        self.assertEqual(reply["requested"], 430125000)
        self.assertEqual(reply["actual"], 430125000)
        self.assertEqual(reply["rate"], 1000000)
        self.assertEqual(reply["mode"], "nbfm")
        for path in (self.status_path, self.control_path):
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        status = self.snapshot_matching(self.connect(self.status_path))
        self.assert_truthful_simulation(status, reply)
        _, errors = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, errors.decode())
        self.assertFalse(self.status_path.exists())
        self.assertFalse(self.control_path.exists())
        self.assertNotIn(b"ALSA: opened", errors)

    def test_start_stop_are_idempotent_and_identify_new_pipeline_lifetimes(self):
        self.start_backend("--start-stopped")
        peer = self.controller()
        initial = self.command(peer, "get")
        self.assertEqual(initial["state"], "IDLE")
        self.assertEqual(self.configuration(self.command(peer, "stop")),
                         self.configuration(initial))
        first = self.command(peer, "start")
        self.assertEqual(first["state"], "RX")
        self.assertEqual(first["config_generation"], initial["config_generation"] + 1)
        self.assertEqual(first["pipeline_generation"], initial["pipeline_generation"] + 1)
        self.assertEqual(self.configuration(self.command(peer, "start")),
                         self.configuration(first))
        stopped = self.command(peer, "stop")
        self.assertEqual(stopped["state"], "IDLE")
        self.assertEqual(stopped["config_generation"], first["config_generation"] + 1)
        self.assertEqual(stopped["pipeline_generation"], first["pipeline_generation"])
        second = self.command(peer, "start")
        self.assertEqual(second["pipeline_generation"], first["pipeline_generation"] + 1)

    def test_frequency_mode_and_rate_changes_require_stopped_rx(self):
        self.start_backend()
        peer = self.controller()
        before = self.command(peer, "get")
        for command in ("frequency 430130000", "mode wbfm", "sample_rate 2000000"):
            with self.subTest(command=command):
                error = self.command(peer, command, code="RX_RUNNING")
                self.assertEqual(self.configuration(error), self.configuration(before))
        stopped = self.command(peer, "stop")
        tuned = self.command(peer, "frequency 430130000")
        self.assertEqual(tuned["requested"], 430130000)
        self.assertEqual(tuned["actual"], 430130000)
        self.assertEqual(tuned["config_generation"], stopped["config_generation"] + 1)
        self.assertEqual(tuned["pipeline_generation"], stopped["pipeline_generation"])
        self.assertEqual(self.configuration(self.command(peer, "frequency 430130000")),
                         self.configuration(tuned))
        changed_mode = self.command(peer, "mode wbfm")
        self.assertEqual(changed_mode["mode"], "wbfm")
        self.assertEqual(changed_mode["config_generation"], tuned["config_generation"] + 1)
        self.assertFalse(changed_mode["noise"])
        changed_rate = self.command(peer, "sample_rate 2000000")
        self.assertEqual(changed_rate["rate"], 2000000)
        self.assertEqual(changed_rate["config_generation"], changed_mode["config_generation"] + 1)
        self.assertEqual(changed_rate["pipeline_generation"], stopped["pipeline_generation"])
        running = self.command(peer, "start")
        self.assertEqual(running["pipeline_generation"], stopped["pipeline_generation"] + 1)
        self.assertEqual(running["mode"], "wbfm")
        self.assertEqual(running["rate"], 2000000)

    def test_live_squelch_updates_are_atomic_and_invalid_levels_preserve_settings(self):
        self.start_backend()
        peer = self.controller()
        initial = self.command(peer, "get")
        updated = self.command(peer, "squelch 1 1 .18 .28")
        self.assertEqual(updated["state"], "RX")
        self.assertTrue(updated["noise"])
        self.assertTrue(updated["carrier"])
        self.assertAlmostEqual(updated["open"], .18)
        self.assertAlmostEqual(updated["close"], .28)
        self.assertEqual(updated["config_generation"], initial["config_generation"] + 1)
        self.assertEqual(updated["pipeline_generation"], initial["pipeline_generation"])
        self.assertEqual(self.configuration(self.command(peer, "squelch 1 1 .18 .28")),
                         self.configuration(updated))
        for command in ("squelch 1 0 .30 .20", "squelch 1 0 .20 .20",
                        "squelch 1 0 0 .30", "squelch 1 0 .20 9",
                        "squelch 1 0 .3001 .3002"):
            with self.subTest(command=command):
                error = self.command(peer, command, code="INVALID")
                self.assertEqual(self.configuration(error), self.configuration(updated))

    def test_wbfm_disables_noise_detection_and_restores_nbfm_preference(self):
        self.start_backend("--start-stopped")
        peer = self.controller()
        nbfm = self.command(peer, "squelch 1 1 .18 .28")
        wbfm = self.command(peer, "mode wbfm")
        self.assertFalse(wbfm["noise"])
        self.assertTrue(wbfm["carrier"])
        denied = self.command(peer, "squelch 1 0 .20 .30", code="WBFM_NOISE")
        self.assertEqual(self.configuration(denied), self.configuration(wbfm))
        restored = self.command(peer, "mode nbfm")
        self.assertEqual(restored["noise"], nbfm["noise"])
        self.assertEqual(restored["carrier"], nbfm["carrier"])
        self.assertAlmostEqual(restored["open"], nbfm["open"])
        self.assertAlmostEqual(restored["close"], nbfm["close"])

    def test_all_supported_rates_and_rf24_frequency_boundaries(self):
        self.start_backend("--start-stopped")
        peer = self.controller()
        for rate in (1000000, 2000000, 4000000):
            with self.subTest(rate=rate):
                self.assertEqual(self.command(peer, f"sample_rate {rate}")["rate"], rate)
        for frequency in (1000000, 100100000, 5999999999):
            with self.subTest(frequency=frequency):
                reply = self.command(peer, f"frequency {frequency}")
                self.assertEqual(reply["requested"], frequency)
                self.assertEqual(reply["actual"], frequency)
        previous = self.command(peer, "get")
        for frequency in (999999, 6000000000):
            with self.subTest(frequency=frequency):
                denied = self.command(peer, f"frequency {frequency}", code="FREQ_RANGE")
                self.assertEqual(self.configuration(denied), self.configuration(previous))

    def test_rf09_band_edges_and_gap_are_validated_before_mutation(self):
        self.start_backend("--channel", "RF09", "--start-stopped")
        peer = self.controller()
        for frequency in (377000000, 530000000, 779000000, 1020000000):
            with self.subTest(frequency=frequency):
                reply = self.command(peer, f"frequency {frequency}")
                self.assertEqual(reply["channel"], "RF09")
                self.assertEqual(reply["actual"], frequency)
        previous = self.command(peer, "get")
        for frequency in (376999999, 530000001, 778999999, 1020000001):
            with self.subTest(frequency=frequency):
                denied = self.command(peer, f"frequency {frequency}", code="FREQ_RANGE")
                self.assertEqual(self.configuration(denied), self.configuration(previous))

    def test_malformed_packets_are_rejected_without_configuration_changes(self):
        self.start_backend("--start-stopped")
        peer = self.controller()
        before = self.command(peer, "get")
        packets = (
            b"v2 40 start", b"v1 40 get surplus", b"v1 40", b"v1 40 transmit",
            b"v1 -1 get", b"v1 not-an-id start", b"v1 18446744073709551616 get",
            b"v1 40 start\x00stop", b"v1 40 frequency nan", b"v1 40 frequency inf",
            b"v1 40 frequency 430125000x", b"v1 40 mode am",
            b"v1 40 sample_rate 3000000", b"v1 40 sample_rate 1000001",
            b"v1 40 squelch 2 0 .20 .30", b"v1 40 squelch 1 0 nan .30",
            b"v1 40 squelch 1 0 .20 inf", b"v1 40 squelch 1 0 .20 .30 extra",
            b"v1 40 get " + b"x" * 8192,
        )
        for packet in packets:
            with self.subTest(packet=packet[:100]):
                denied = self.exchange(peer, packet)
                self.assertEqual(denied["result"], "ERROR", denied)
                self.assertIn(denied["code"], ("PROTOCOL", "INVALID"))
                self.assertEqual(self.configuration(denied), self.configuration(before))
                self.assertEqual(self.configuration(self.command(peer, "get")),
                                 self.configuration(before))

    def test_one_controller_disconnect_and_reconnect_leave_intent_intact(self):
        process = self.start_backend()
        owner = self.controller()
        before = self.command(owner, "squelch 0 1 .18 .28")
        rejected = self.controller()
        # The server rejects the extra connection before accepting its commands.
        denied = parse_reply(rejected.recv(4096))
        self.assertEqual(denied["result"], "ERROR")
        self.assertEqual(denied["code"], "BUSY")
        rejected.close()
        self.assertEqual(self.configuration(self.command(owner, "get")),
                         self.configuration(before))
        owner.close()
        time.sleep(.05)
        replacement = self.controller()
        after = self.command(replacement, "get")
        self.assertEqual(self.configuration(after), self.configuration(before))
        self.assertIsNone(process.poll(), "Closing the controller stopped the backend")

    def test_connected_monitor_tracks_control_changes_without_becoming_controller(self):
        process = self.start_backend("--start-stopped", "--status-hz", "10")
        status = self.connect(self.status_path)
        peer = self.controller()
        initial = self.command(peer, "get")
        first = self.snapshot_matching(status)
        self.assert_truthful_simulation(first, initial)
        tuned = self.command(peer, "frequency 430130000")
        self.command(peer, "sample_rate 2000000")
        self.command(peer, "squelch 0 1 .18 .28")
        running = self.command(peer, "start")
        latest = self.snapshot_matching(status, lambda s:
            s.get("control", {}).get("config_generation") == running["config_generation"])
        self.assert_truthful_simulation(latest, running)
        self.assertEqual(latest["session"], first["session"])
        self.assertGreater(latest["sequence"], first["sequence"])
        self.assertEqual(latest["config"]["rx_frequency_hz"], tuned["actual"])
        self.assertEqual(latest["config"]["sample_rate_hz"], running["rate"])
        self.assertIs(latest["rx"]["noise_enabled"], False)
        self.assertIs(latest["rx"]["carrier_enabled"], True)
        status.close()
        peer.close()
        result = subprocess.run(
            [sys.executable, str(MONITOR), "--socket", str(self.status_path),
             "--once", "--timeout", "2"],
            capture_output=True, timeout=3, env=ENVIRONMENT)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assert_truthful_simulation(monitor.parse_snapshot(result.stdout.strip()), running)
        self.assertIsNone(process.poll(), "Closing the passive monitor stopped RX intent")

    def test_custom_control_path_and_sigterm_remove_both_owned_sockets(self):
        path = Path(self.temporary.name) / "commands.sock"
        process = self.start_backend("--start-stopped", control_path=path)
        self.assertEqual(self.command(self.controller(), "get")["state"], "IDLE")
        process.send_signal(signal.SIGTERM)
        _, errors = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, errors.decode())
        self.assertFalse(self.status_path.exists())
        self.assertFalse(path.exists())

    def test_live_handler_faults_preserve_settings_and_release_failed_pipeline(self):
        # Compile the actual owner handler with mocked hardware/pipeline calls.
        # The original backend main and status serializer are linker-discarded.
        # This checks live error paths; --dry-run cannot exercise those failures.
        src = ROOT / "software/libcariboulite/src"
        backend = TOOLS / "radio_backend"
        fixture = Path(__file__).with_name("test_rx_control_failures.c")
        binary = Path(self.temporary.name) / "control-failures"
        compilation = subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-Wno-missing-braces", "-ffunction-sections",
             "-fdata-sections", "-I" + str(src), "-I" + str(backend), str(fixture),
             str(src / "noise_squelch.c"), "-Wl,--gc-sections", "-pthread", "-lm",
             "-o", str(binary)], capture_output=True, timeout=15)
        self.assertEqual(compilation.returncode, 0, compilation.stderr.decode())
        result = subprocess.run([str(binary)], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertIn(b"RX control fault injection passed", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
