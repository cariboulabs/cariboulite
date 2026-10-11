#!/usr/bin/env python3
"""Exercise the compiled RX backend and real monitor without radio hardware.

Build first with make -C software/libcariboulite/tools/radio_backend.
Every backend launch uses --dry-run, including invalid-argument checks.
All sockets and pseudo terminals are isolated test resources.
"""
import fcntl
import importlib.util
import os
from pathlib import Path
import pty
import select
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import termios
import time
import unittest


ROOT = Path(__file__).resolve().parents[3]
BACKEND = ROOT / "build/radio-backend/cariboulite_rx_backend"
CLIENT = ROOT / "software/libcariboulite/tools/cariboulite_monitor.py"
sys.dont_write_bytecode = True
sys.path.insert(0, str(CLIENT.parent))
import at86rf215_monitor as registers

spec = importlib.util.spec_from_file_location("radio_backend_monitor_test", CLIENT)
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)
ENVIRONMENT = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")


class RadioBackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BACKEND.is_file():
            raise RuntimeError("Build the backend first: make -C "
                               "software/libcariboulite/tools/radio_backend")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cl-rx-backend-")
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "status.sock"

    def start_backend(self, *arguments, seconds="10"):
        process = subprocess.Popen(
            [str(BACKEND), "--dry-run", "--socket", str(self.path),
             "--seconds", seconds, *arguments],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=ENVIRONMENT)
        self.addCleanup(self.stop_backend, process)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if process.poll() is not None:
                output, errors = process.communicate()
                self.fail(f"Backend exited before publishing: {output!r} {errors!r}")
            if self.path.exists():
                return process
            time.sleep(.01)
        self.fail("Backend did not create its status socket")

    @staticmethod
    def stop_backend(process):
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
        try:
            process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate(timeout=3)

    def once(self):
        result = subprocess.run(
            [sys.executable, str(CLIENT), "--socket", str(self.path),
             "--once", "--timeout", "2"],
            capture_output=True, timeout=3, env=ENVIRONMENT)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        return client.parse_snapshot(result.stdout.strip())

    def assert_transport_only(self, snapshot, channel="RF24"):
        self.assertEqual(snapshot["source"]["kind"], "simulated")
        self.assertIn("SIMULATED DATA", client.source_label(snapshot))
        self.assertEqual(snapshot["state"], "DRY_RUN")
        self.assertEqual(snapshot["channel"], channel)
        self.assertIs(snapshot["rx"]["running"], False)
        self.assertIsNone(snapshot["rx"]["queue"])
        self.assertIs(snapshot["tx"]["running"], False)
        self.assertIs(snapshot["rx"]["noise"]["valid"], False)
        self.assertIsNone(snapshot["rx"]["noise"]["rms"])
        hardware = snapshot["hardware"]
        active = hardware[channel.lower()]["registers"]
        common = hardware["common"]["registers"]
        self.assertEqual(set(common), set(registers.COMMON_REGISTERS))
        self.assertEqual(set(active), set(registers.RADIO_REGISTERS))
        self.assertTrue(all(value is None for value in common.values()))
        self.assertTrue(all(value is None for value in active.values()))
        for counter in ("read_blocks", "polls", "spi_transactions", "spi_failures"):
            self.assertEqual(hardware["register_poll"][counter], 0, counter)
        self.assertEqual(snapshot["publisher"]["spi_transactions"], 0)
        inactive = "rf09" if channel == "RF24" else "rf24"
        self.assertIs(hardware[inactive]["valid"], False)
        rows = registers.format_register_rows(hardware, channel, view="all")
        self.assertEqual(len(rows), 39)
        self.assertTrue(all("=0x--" in row for row in rows))

    def test_default_dry_run_connects_to_real_monitor_and_expires_cleanly(self):
        process = self.start_backend(seconds="1.2")
        snapshot = self.once()
        self.assert_transport_only(snapshot)
        self.assertEqual(snapshot["owner_pid"], process.pid)
        self.assertEqual(snapshot["config"]["rx_frequency_hz"], 430125000)
        self.assertEqual(snapshot["config"]["sample_rate_hz"], 1000000)
        self.assertEqual(stat.S_IMODE(self.path.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(self.path.parent.stat().st_mode), 0o700)
        _, errors = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, errors.decode())
        self.assertIn(b"SIMULATED TRANSPORT ONLY", errors)
        self.assertNotIn(b"ALSA: opened", errors)
        self.assertFalse(self.path.exists(), "Timed stop left the owned socket behind")

    def test_rf09_settings_and_escaped_playback_survive_transport(self):
        # A nonexistent, unusual device name must not be opened in dry-run.
        playback = 'nonexistent-device:"quoted"\\path\nsecond line'
        process = self.start_backend(
            "--channel", "RF09", "--frequency", "430125000",
            "--sample-rate", "4000000", "--playback", playback,
            "--status-hz", "10", "--no-noise-squelch", "--carrier-squelch",
            "--noise-open", "0.18", "--noise-close", "0.28",
            "--gain", "1234", "--deemphasis-us", "75", seconds=".8")
        snapshot = self.once()
        self.assert_transport_only(snapshot, "RF09")
        self.assertEqual(snapshot["config"]["playback"], playback)
        self.assertEqual(snapshot["config"]["sample_rate_hz"], 4000000)
        self.assertEqual(snapshot["config"]["pcm_gain"], 1234)
        self.assertAlmostEqual(snapshot["config"]["deemphasis_us"], 75, places=2)
        self.assertIs(snapshot["rx"]["noise_enabled"], False)
        self.assertIs(snapshot["rx"]["carrier_enabled"], True)
        self.assertEqual(snapshot["rx"]["noise"]["open_rms"], .18)
        self.assertEqual(snapshot["rx"]["noise"]["close_rms"], .28)
        _, errors = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, errors.decode())
        self.assertFalse(self.path.exists())

    def test_wbfm_selection_has_no_fake_reception_or_noise_measurements(self):
        process = self.start_backend("--mode", "wbfm", "--frequency", "100100000",
                                     "--sample-rate", "2000000", seconds=".8")
        snapshot = self.once()
        self.assert_transport_only(snapshot)
        self.assertEqual(snapshot["rx_mode"], "WBFM mono")
        self.assertEqual(snapshot["config"]["rx_bandwidth_hz"], 2000000)
        self.assertIs(snapshot["rx"]["noise_enabled"], False)
        _, errors = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, errors.decode())

    def test_monitor_quit_disconnect_and_reconnect_leave_backend_running(self):
        process = self.start_backend("--status-hz", "10")
        first = self.once()  # --once closes its socket after receiving.
        self.assertIsNone(process.poll())
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 57, 120, 0, 0))
        monitor = subprocess.Popen(
            [sys.executable, str(CLIENT), "--socket", str(self.path)],
            stdin=slave, stdout=slave, stderr=slave,
            env=dict(ENVIRONMENT, TERM="xterm"))
        os.close(slave)
        self.addCleanup(self.stop_backend, monitor)
        self.addCleanup(os.close, master)
        output = bytearray()
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline and b"SIMULATED DATA" not in output:
            if select.select([master], [], [], .1)[0]:
                output.extend(os.read(master, 65536))
        self.assertIn(b"SIMULATED DATA", output)
        os.write(master, b"q")
        self.assertEqual(monitor.wait(timeout=3), 0)
        self.assertIsNone(process.poll(), "Quitting monitor stopped backend")
        second = self.once()
        self.assertEqual(second["session"], first["session"])
        self.assertGreater(second["sequence"], first["sequence"])
        self.assertIsNone(process.poll(), "Reconnecting monitor stopped backend")
        process.send_signal(signal.SIGTERM)
        _, errors = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, errors.decode())
        self.assertFalse(self.path.exists())

    def test_sigterm_stops_publisher_normally_and_removes_only_owned_socket(self):
        process = self.start_backend()
        self.assert_transport_only(self.once())
        process.send_signal(signal.SIGTERM)
        _, errors = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, errors.decode())
        self.assertFalse(self.path.exists())
        self.assertTrue(self.path.parent.is_dir())

    def test_invalid_cli_fails_before_creating_status_directory(self):
        invalid = (
            ("--frequency", "nan"), ("--frequency", "inf"),
            ("--frequency", "430125000x"), ("--frequency", "0"),
            ("--sample-rate", "nan"), ("--sample-rate", "3000000"),
            ("--sample-rate", "1000001"), ("--channel", "RF12"),
            ("--channel", "RF09", "--frequency", "100100000"),
            ("--mode", "am"), ("--status-hz", "nan"),
            ("--status-hz", "0"), ("--status-hz", "21"),
            ("--seconds", "inf"), ("--seconds", "0"),
            ("--gain", "nan"), ("--gain", "-1"),
            ("--noise-open", "nan"), ("--noise-close", "9"),
            ("--noise-open", "0.30", "--noise-close", "0.20"),
            ("--noise-open", "0.3001", "--noise-close", "0.3002"),
            ("--deemphasis-us", "60"), ("--playback", ""),
            ("unexpected-positional-argument",),
        )
        for index, arguments in enumerate(invalid):
            with self.subTest(arguments=arguments):
                path = Path(self.temporary.name) / f"invalid-{index}" / "status.sock"
                result = subprocess.run(
                    [str(BACKEND), "--dry-run", "--socket", str(path), *arguments],
                    capture_output=True, timeout=3, env=ENVIRONMENT)
                self.assertEqual(result.returncode, 2, result.stderr.decode())
                self.assertIn(b"Usage:", result.stderr)
                self.assertFalse(path.parent.exists(), "Invalid CLI reached publisher setup")
                self.assertNotIn(b"driver initializing", result.stderr.lower())

    def test_existing_unknown_socket_is_preserved_and_stays_usable(self):
        server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.addCleanup(server.close)
        server.bind(str(self.path))
        server.listen(1)
        original = self.path.stat()
        result = subprocess.run(
            [str(BACKEND), "--dry-run", "--socket", str(self.path), "--seconds", ".1"],
            capture_output=True, timeout=3, env=ENVIRONMENT)
        self.assertEqual(result.returncode, 1, result.stderr.decode())
        self.assertIn(b"status socket", result.stderr)
        current = self.path.stat()
        self.assertEqual((current.st_dev, current.st_ino),
                         (original.st_dev, original.st_ino))
        peer = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.addCleanup(peer.close)
        peer.settimeout(1)
        peer.connect(str(self.path))
        accepted, _ = server.accept()
        self.addCleanup(accepted.close)
        accepted.send(b"existing publisher remains usable")
        self.assertEqual(peer.recv(100), b"existing publisher remains usable")


if __name__ == "__main__":
    unittest.main(verbosity=2)
