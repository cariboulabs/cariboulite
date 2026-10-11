#!/usr/bin/env python3
"""Exercise FPGA programming and reset control using simulated sessions only.

All backend launches pass --dry-run from a directory without firmware. The
actual programming sequence and faults are checked separately with C mocks.
"""
import importlib.util
from pathlib import Path
import subprocess
import sys
import time
import unittest


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    "fpga_backend_tx_helpers", Path(__file__).with_name("test_tx_control_backend.py"))
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
rx_helpers = helpers.helpers


class FpgaControlBackendTests(unittest.TestCase):
    setUpClass = helpers.TxControlBackendTests.__dict__["setUpClass"]
    setUp = helpers.TxControlBackendTests.setUp
    stop_backend = staticmethod(helpers.TxControlBackendTests.stop_backend)
    connect = helpers.TxControlBackendTests.connect
    controller = helpers.TxControlBackendTests.controller
    exchange = helpers.TxControlBackendTests.exchange
    command = helpers.TxControlBackendTests.command
    wait_state = helpers.TxControlBackendTests.wait_state
    settings = staticmethod(helpers.TxControlBackendTests.settings)
    snapshot_matching = helpers.TxControlBackendTests.snapshot_matching
    assert_simulated = helpers.TxControlBackendTests.assert_simulated

    def start_backend(self, *arguments):
        process = subprocess.Popen(
            [str(rx_helpers.BACKEND), "--dry-run", "--socket", str(self.status_path),
             "--seconds", "10", "--status-hz", "20", *arguments],
            cwd=self.temporary.name, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=rx_helpers.ENVIRONMENT)
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

    def test_running_rx_refuses_programming_and_resets_without_changing_configuration(self):
        self.start_backend()
        peer = self.controller()
        before = self.command(peer, "get")
        for command in ("fpga_program", "fpga_soft_reset", "fpga_hard_reset"):
            with self.subTest(command=command):
                denied = self.command(peer, command, "RX_RUNNING")
                self.assertEqual(self.settings(denied), self.settings(before))
        self.assertEqual(self.settings(self.command(peer, "get")), self.settings(before))

    def test_running_tx_refuses_programming_then_idle_allows_it(self):
        self.start_backend("--start-stopped")
        peer = self.controller()
        self.command(peer, "tx_start", "ACCEPTED")
        transmitting = self.wait_state(peer, "TX")
        for command in ("fpga_program", "fpga_soft_reset", "fpga_hard_reset"):
            with self.subTest(command=command):
                denied = self.command(peer, command, "TX_RUNNING")
                self.assertEqual(self.settings(denied), self.settings(transmitting))
        self.command(peer, "tx_stop", "ACCEPTED")
        before = self.wait_state(peer, "IDLE")
        programming = self.command(peer, "fpga_program", "ACCEPTED")
        self.assertEqual(programming["state"], "FPGA_PROGRAMMING")
        idle = self.wait_state(peer, "IDLE")
        self.assertEqual(idle["pipeline_generation"], before["pipeline_generation"])
        self.assertEqual(idle["tx_pipeline_generation"], before["tx_pipeline_generation"])
        self.assertFalse(idle["tx_requested"])
        self.assertFalse(idle["rx_resume"])

    def test_programming_publishes_transition_then_idle_without_hardware(self):
        self.start_backend("--start-stopped")
        peer = self.controller()
        status = self.connect(self.status_path)
        before = self.command(peer, "get")
        programming = self.command(peer, "fpga_program", "ACCEPTED")
        self.assertEqual(programming["state"], "FPGA_PROGRAMMING")
        self.assertTrue(programming["transition"])
        self.assertEqual(programming["config_generation"], before["config_generation"] + 1)
        during = self.snapshot_matching(status, lambda snapshot:
            snapshot["control"]["state"] == "FPGA_PROGRAMMING")
        self.assertTrue(during["control"]["transition"])
        self.assert_simulated(during)
        idle = self.wait_state(peer, "IDLE")
        self.assertEqual(idle["pipeline_generation"], before["pipeline_generation"])
        self.assertEqual(idle["tx_pipeline_generation"], before["tx_pipeline_generation"])
        self.assertFalse(idle["tx_requested"])
        self.assertFalse(idle["rx_resume"])
        completed = self.snapshot_matching(status, lambda snapshot:
            snapshot["control"]["state"] == "IDLE")
        self.assert_simulated(completed)
        self.assertFalse(completed["control"]["rx_requested"])

    def test_active_transition_refuses_programming(self):
        self.start_backend("--start-stopped")
        peer = self.controller()
        self.command(peer, "tx_start", "ACCEPTED")
        # Both the pending job and completed TX must refuse a reset. The
        # deterministic held-worker C fixture checks all TRANSITION guards.
        self.request_id += 1
        denied = self.exchange(peer, f"v2 {self.request_id} fpga_program".encode())
        self.assertEqual(denied["result"], "ERROR")
        self.assertIn(denied["code"], ("TRANSITION", "TX_RUNNING"))
        self.wait_state(peer, "TX")
        self.command(peer, "tx_stop", "ACCEPTED")
        self.wait_state(peer, "IDLE")

    def test_programming_survives_controller_disconnect_and_leaves_rx_stopped(self):
        backend = self.start_backend("--start-stopped")
        peer = self.controller()
        status = self.connect(self.status_path)
        before = self.command(peer, "get")
        self.command(peer, "fpga_program", "ACCEPTED")
        peer.close()
        completed = self.snapshot_matching(status, lambda snapshot:
            snapshot["control"]["state"] == "IDLE" and
            snapshot["control"]["config_generation"] > before["config_generation"] + 1)
        self.assertFalse(completed["control"]["rx_requested"])
        self.assertFalse(completed["control"]["tx_requested"])
        self.assertFalse(completed["control"]["transition"])
        self.assertEqual(completed["control"]["pipeline_generation"], before["pipeline_generation"])
        self.assertIsNone(backend.poll())
        replacement = self.controller()
        self.assertEqual(self.command(replacement, "get")["state"], "IDLE")

    def test_built_controller_one_shot_programming_completes_after_exit(self):
        self.start_backend("--start-stopped")
        status = self.connect(self.status_path)
        before = self.snapshot_matching(status)
        result = subprocess.run(
            [str(helpers.CONTROLLER), "--socket", str(self.control_path),
             "--command", "fpga program"], capture_output=True, text=True,
            timeout=3, env=rx_helpers.ENVIRONMENT)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Transition accepted", result.stdout)
        self.assertIn("SIMULATED RF24 FPGA_PROGRAMMING", result.stdout)
        completed = self.snapshot_matching(status, lambda snapshot:
            snapshot["control"]["state"] == "IDLE" and
            snapshot["control"]["config_generation"] > before["control"]["config_generation"] + 1)
        self.assert_simulated(completed)
        self.assertFalse(completed["control"]["rx_requested"])

    def test_v1_and_firmware_path_packets_remain_protocol_errors(self):
        self.start_backend("--start-stopped")
        peer = self.controller()
        before = self.command(peer, "get")
        for command in ("fpga_program", "fpga_soft_reset", "fpga_hard_reset"):
            with self.subTest(command=command):
                peer.sendall(f"v1 500 {command}".encode())
                legacy = rx_helpers.parse_reply(peer.recv(4096))
                self.assertEqual(legacy["code"], "PROTOCOL")
                self.assertEqual(legacy["state"], "IDLE")
        for packet in (b"v2 501 fpga_program firmware/top.bin", b"v2 502 fpga program",
                       b"v2 503 fpga_soft_reset extra", b"v2 504 fpga_hard_reset extra",
                       b"v2 505 fpga_soft reset", b"v2 506 fpga_hard reset"):
            denied = self.exchange(peer, packet)
            self.assertEqual(denied["code"], "PROTOCOL")
            self.assertEqual(denied["config_generation"], before["config_generation"])
            self.assertEqual(denied["state"], "IDLE")

    def test_soft_reset_preserves_configuration_without_loading_firmware(self):
        self.start_backend("--start-stopped")
        self.assertFalse((Path(self.temporary.name) / "firmware/top.bin").exists())
        peer = self.controller()
        status = self.connect(self.status_path)
        before = self.command(peer, "squelch 1 1 .18 .28")
        resetting = self.command(peer, "fpga_soft_reset", "ACCEPTED")
        self.assertEqual(resetting["state"], "FPGA_RESETTING")
        self.assertTrue(resetting["transition"])
        during = self.snapshot_matching(status, lambda s:
            s["control"]["state"] == "FPGA_RESETTING")
        self.assert_simulated(during)
        idle = self.wait_state(peer, "IDLE")
        for field in ("requested", "rate", "mode", "noise", "carrier", "open", "close",
                      "pipeline_generation", "tx_pipeline_generation",
                      "tx_requested_frequency", "tx_requested_power", "tx_rate",
                      "tx_source", "tx_tone_hz", "tx_tone_amp", "tx_deviation"):
            self.assertEqual(idle[field], before[field], field)
        self.assertFalse(idle["rx_resume"])
        self.assertFalse(idle["tx_requested"])
        self.assertEqual(idle["config_generation"], before["config_generation"] + 2)

    def test_soft_then_hard_reset_requires_programming_before_radio_mutations(self):
        self.start_backend("--start-stopped")
        self.assertFalse((Path(self.temporary.name) / "firmware/top.bin").exists())
        peer = self.controller()
        self.command(peer, "fpga_soft_reset", "ACCEPTED")
        self.wait_state(peer, "IDLE")
        self.command(peer, "fpga_hard_reset", "ACCEPTED")
        unconfigured = self.wait_state(peer, "FPGA_NEEDS_PROGRAM")
        self.assertFalse(unconfigured["rx_resume"])
        self.assertFalse(unconfigured["tx_requested"])
        commands = ("start", "frequency 430130000", "mode wbfm", "sample_rate 2000000",
                    "squelch 0 1 .18 .28", "tx_start", "tx_frequency 430130000",
                    "tx_power -11", "tx_sample_rate 2000000", "tx_source tone",
                    "tx_tone 700 .5", "fpga_soft_reset")
        for command in commands:
            with self.subTest(command=command):
                denied = self.command(peer, command, "FPGA_REQUIRED")
                self.assertEqual(self.settings(denied), self.settings(unconfigured))
        self.assertEqual(self.settings(self.command(peer, "stop")), self.settings(unconfigured))
        self.assertEqual(self.settings(self.command(peer, "tx_stop")), self.settings(unconfigured))
        self.command(peer, "fpga_hard_reset", "ACCEPTED")
        self.wait_state(peer, "FPGA_NEEDS_PROGRAM")
        self.command(peer, "fpga_program", "ACCEPTED")
        self.wait_state(peer, "IDLE")
        self.assertEqual(self.command(peer, "start")["state"], "RX")
        self.command(peer, "tx_start", "ACCEPTED")
        self.wait_state(peer, "TX")
        self.command(peer, "tx_stop", "ACCEPTED")
        self.wait_state(peer, "RX")

    def test_reset_survives_disconnect_without_restarting_rx_or_tx(self):
        backend = self.start_backend("--start-stopped")
        status = self.connect(self.status_path)
        for command, completed_state in (("fpga_soft_reset", "IDLE"),
                                         ("fpga_hard_reset", "FPGA_NEEDS_PROGRAM")):
            with self.subTest(command=command):
                peer = self.controller()
                before = self.command(peer, "get")
                self.command(peer, command, "ACCEPTED")
                peer.close()
                completed = self.snapshot_matching(status, lambda s:
                    s["control"]["state"] == completed_state and
                    s["control"]["config_generation"] > before["config_generation"] + 1)
                self.assert_simulated(completed)
                self.assertFalse(completed["control"]["rx_requested"])
                self.assertFalse(completed["control"]["tx_requested"])
                self.assertFalse(completed["control"]["transition"])
                self.assertIsNone(backend.poll())

    def test_built_controller_one_shot_reset_commands(self):
        self.start_backend("--start-stopped")
        status = self.connect(self.status_path)
        for command, completed_state in (("fpga soft reset", "IDLE"),
                                         ("fpga hard reset", "FPGA_NEEDS_PROGRAM")):
            with self.subTest(command=command):
                before = self.snapshot_matching(status)
                result = subprocess.run(
                    [str(helpers.CONTROLLER), "--socket", str(self.control_path),
                     "--command", command], capture_output=True, text=True,
                    timeout=3, env=rx_helpers.ENVIRONMENT)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("Transition accepted", result.stdout)
                self.assertIn("SIMULATED RF24 FPGA_RESETTING", result.stdout)
                completed = self.snapshot_matching(status, lambda s:
                    s["control"]["state"] == completed_state and
                    s["control"]["config_generation"] > before["control"]["config_generation"] + 1)
                self.assert_simulated(completed)
                self.assertFalse(completed["control"]["rx_requested"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
