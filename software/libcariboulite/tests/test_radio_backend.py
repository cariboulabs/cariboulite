#!/usr/bin/env python3
"""Exercise the compiled radio backend and real monitor without radio hardware.

Build first with make -C software/libcariboulite/tools/radio_backend.
Every backend launch uses --dry-run, including invalid-argument checks.
All sockets and pseudo terminals are isolated test resources.
"""
import fcntl
import importlib.util
import json
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
BACKEND = ROOT / "build/radio-backend/cariboulite_radio_backend"
CLIENT = ROOT / "software/libcariboulite/tools/cariboulite_radio_monitor.py"
MONITOR = BACKEND.with_name("cariboulite_radio_monitor")
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
        self.assertIsNone(snapshot["rx"]["audio_queue"])
        self.assertIsNone(snapshot["rx"]["reader"])
        self.assertEqual(snapshot["rx"]["playback"], {"valid": False})
        for worker in ("reader", "demod", "audio"):
            self.assertEqual(snapshot["rx"]["workers"][worker], {
                "created": False, "alive": False, "last_error": 0,
                "scheduling": {"valid": False}})
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

    def test_named_monitor_launcher_reads_dry_run_snapshot(self):
        process = self.start_backend()
        self.assertTrue(MONITOR.is_file(), "Build the named monitor launcher first")
        self.assertTrue(os.access(MONITOR, os.X_OK), "Monitor launcher is not executable")
        result = subprocess.run(
            [str(MONITOR), "--socket", str(self.path), "--once", "--timeout", "2"],
            capture_output=True, timeout=3, env=ENVIRONMENT)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        snapshot = client.parse_snapshot(result.stdout.strip())
        self.assert_transport_only(snapshot)
        self.assertEqual(snapshot["owner_pid"], process.pid)

    def test_named_application_help_identifies_backend_and_monitor(self):
        for application in (BACKEND, MONITOR):
            with self.subTest(application=application.name):
                arguments = [str(application)]
                if application == BACKEND:
                    arguments.append("--dry-run")
                result = subprocess.run([*arguments, "--help"], capture_output=True,
                                        timeout=3, env=ENVIRONMENT)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                self.assertIn(application.name, result.stdout.decode())

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
        self.assertEqual(snapshot["config"]["rx_bandwidth_hz"], 250000)
        self.assertIs(snapshot["rx"]["noise_enabled"], False)
        _, errors = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, errors.decode())

    def test_unavailable_audio_and_worker_counters_remain_explicit_in_monitor(self):
        self.start_backend()
        snapshot = self.once()
        self.assert_transport_only(snapshot)
        display = "\n".join(client.format_snapshot(snapshot))
        self.assertIn("RX audio FIFO: unavailable/unavailable", display)
        self.assertIn("Playback: unavailable", display)
        self.assertIn("RX reader: calls=unavailable", display)
        for name in ("reader", "demod", "audio"):
            self.assertIn(f"{name} worker: alive=off created=off", display)
        self.assertNotIn("sched=SCHED_OTHER/0", display,
                         "Dry-run guessed a policy for workers it did not create")

    def test_maximum_escaped_device_name_fits_packet_limit_with_instrumentation(self):
        playback = "\x01" * 255
        self.start_backend("--playback", playback)
        peer = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.addCleanup(peer.close)
        peer.settimeout(2)
        peer.connect(str(self.path))
        packet, _, flags, _ = peer.recvmsg(8192)
        self.assertFalse(flags & socket.MSG_TRUNC, "Instrumented packet exceeds protocol limit")
        self.assertLessEqual(len(packet), 8192)
        snapshot = client.parse_snapshot(packet)
        self.assertEqual(snapshot["config"]["playback"], playback)
        self.assert_transport_only(snapshot)

    def test_live_serializer_preserves_counters_errors_and_actual_scheduling(self):
        # Compile the actual serializer with cached-data fixtures. The unused
        # radio-owning entry point is discarded; these stubs only answer status.
        src = ROOT / "software/libcariboulite/src"
        backend = CLIENT.parent / "radio_backend"
        fixture = Path(self.temporary.name) / "serializer.c"
        fixture.write_text(r'''
#define _GNU_SOURCE
#define main unused_backend_main
#include "main.c"
#undef main
#include <assert.h>
#include <sched.h>

bool rx_pipeline_running(const rx_pipeline_t *p) { (void)p; return true; }
bool rx_pipeline_squelch_open(const rx_pipeline_t *p) { (void)p; return true; }
int rx_pipeline_get_noise_squelch_status(const rx_pipeline_t *p,
                                       rx_noise_squelch_status_t *out) {
    (void)p;
    *out = (rx_noise_squelch_status_t){.valid=true, .detector_open=true,
        .rms=.12f, .open_rms=.20f, .close_rms=.30f};
    return 0;
}
int main(void) {
    options_t options;
    char *arguments[] = {"fixture", NULL};
    assert(!parse_options(1, arguments, &options));
    rx_pipeline_t pipeline = {.inited=true};
    rx_pipeline_stats_t stats = {
        .rxq = {.cap=64, .count=2, .puts=101, .gets=99, .drops=3,
                .timeouts_put=4, .timeouts_get=5, .min_depth=0, .max_depth=7},
        .audioq = {.cap=24, .count=12, .puts=21, .gets=9, .drops=2,
                   .timeouts_put=3, .timeouts_get=4, .min_depth=1, .max_depth=17,
                   .shutdown_puts=5, .shutdown_gets=6},
        .playback_valid=true,
        .playback = {.write_calls=1, .frames_requested=2, .frames_written=3,
            .partial_writes=4, .would_block=5, .zero_writes=6, .underruns=7,
            .recovery_attempts=8, .recovery_successes=9, .recovery_failures=10,
            .write_errors=11, .last_error=-EIO},
        .reader = {.calls=201, .samples=202, .short_reads=203,
                   .zero_reads=204, .errors=205, .last_error=-ENODEV},
        .reader_worker = {.created=true, .alive=true, .last_error=-ENODEV,
            .scheduling={.valid=true, .requested_priority=46,
                .requested_cpu=0, .applied_cpu=0, .policy=SCHED_OTHER, .priority=0,
                .affinity="0", .rt_error=EPERM, .fallback_error=0,
                .affinity_error=0, .mlock_attempted=true, .mlock_error=ENOMEM}},
        .demod_worker = {.created=true, .alive=false, .last_error=-ENOMEM},
        .audio_worker = {.created=true, .alive=true, .last_error=-EIO}
    };
    stats.demod_worker.scheduling = stats.reader_worker.scheduling;
    stats.demod_worker.scheduling.requested_priority = 55;
    stats.demod_worker.scheduling.requested_cpu = 1;
    stats.demod_worker.scheduling.applied_cpu = -1;
    stats.demod_worker.scheduling.affinity_error = EINVAL;
    stats.demod_worker.scheduling.mlock_attempted = false;
    stats.demod_worker.scheduling.mlock_error = 0;
    strcpy(stats.demod_worker.scheduling.affinity, "0,1,2,3");
    modem_snapshot modem;
    modem_snapshot_init(&modem, at86rf215_rf_channel_2400mhz);
    status_publisher_t publisher = {0};
    char packet[STATUS_PUBLISHER_MAX_PACKET_BYTES];
    int size = make_snapshot(packet, sizeof(packet), &options, "fixture", 1,
        "RX", &pipeline, &stats, &modem, &publisher, NULL, NULL);
    assert(size > 0); puts(packet);

    // Largest supported escaped device name and counters exercise bounds on
    // the full live schema rather than the smaller unavailable dry-run data.
    char device[256]; memset(device, 1, 255); device[255] = 0;
    options.playback = device;
    memset(&stats.rxq, 255, sizeof(stats.rxq));
    memset(&stats.audioq, 255, sizeof(stats.audioq));
    memset(&stats.playback, 255, sizeof(stats.playback));
    memset(&stats.reader, 255, sizeof(stats.reader));
    stats.audio_worker.scheduling = stats.demod_worker.scheduling;
    stats.audio_worker.scheduling.requested_priority = 70;
    size = make_snapshot(packet, sizeof(packet), &options, "fixture", UINT64_MAX,
        "RX", &pipeline, &stats, &modem, &publisher, NULL, NULL);
    assert(size > 0); puts(packet);
    // RX resumes while TX workers remain allocated and parked. Both sets of
    // lifetime counters must still fit the monitor transport together.
    tx_pipeline_stats_t txstats = {.capture_valid=true};
    memset(&txstats.txq, 255, sizeof(txstats.txq));
    memset(&txstats.writer, 255, sizeof(txstats.writer));
    memset(&txstats.capture, 255, sizeof(txstats.capture));
    txstats.producer_worker = (tx_worker_status_t){.created=true, .alive=true,
        .scheduling=stats.demod_worker.scheduling};
    txstats.writer_worker = txstats.producer_worker;
    options.tx_initialized = true;
    options.tx_pipeline_generation = UINT64_MAX;
    size = make_snapshot(packet, sizeof(packet), &options, "fixture", UINT64_MAX,
        "RX", &pipeline, &stats, &modem, &publisher, NULL, &txstats);
    assert(size > 0); puts(packet);
    return 0;
}
''')
        binary = Path(self.temporary.name) / "serializer"
        compilation = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-Wno-missing-braces",
                        "-ffunction-sections", "-fdata-sections",
                        "-I" + str(src), "-I" + str(backend), str(fixture),
                        str(backend / "modem_snapshot.c"), str(src / "pipeline_runtime.c"),
                        str(src / "noise_squelch.c"),
                        "-Wl,--gc-sections", "-pthread", "-lm", "-o", str(binary)],
                       capture_output=True)
        self.assertEqual(compilation.returncode, 0, compilation.stderr.decode())
        result = subprocess.run([str(binary)], check=True, capture_output=True, timeout=3)
        packets = result.stdout.splitlines()
        self.assertEqual(len(packets), 3)
        snapshot = client.parse_snapshot(packets[0])
        self.assertEqual(snapshot["source"]["kind"], "live")
        rx = snapshot["rx"]
        self.assertEqual(rx["queue"]["puts"], 101)
        self.assertEqual(rx["audio_queue"], {
            "capacity": 24, "depth": 12, "puts": 21, "gets": 9, "drops": 2,
            "timeouts_put": 3, "timeouts_get": 4, "min_depth": 1, "max_depth": 17,
            "shutdown_puts": 5, "shutdown_gets": 6})
        playback_fields = ("write_calls", "frames_requested", "frames_written",
                           "partial_writes", "would_block", "zero_writes", "underruns",
                           "recovery_attempts", "recovery_successes", "recovery_failures",
                           "write_errors")
        self.assertIs(rx["playback"]["valid"], True)
        self.assertEqual([rx["playback"][name] for name in playback_fields], list(range(1, 12)))
        self.assertEqual(rx["playback"]["last_error"], -5)
        self.assertEqual(rx["reader"], {"calls": 201, "samples": 202,
                                       "short_reads": 203, "zero_reads": 204,
                                       "errors": 205, "last_error": -19})
        workers = rx["workers"]
        self.assertEqual(workers["reader"]["last_error"], -19)
        self.assertIs(workers["demod"]["alive"], False)
        self.assertEqual(workers["demod"]["last_error"], -12)
        scheduling = workers["reader"]["scheduling"]
        self.assertEqual(scheduling["policy_name"], "SCHED_OTHER")
        self.assertEqual(scheduling["priority"], 0)
        self.assertEqual(scheduling["requested_priority"], 46)
        self.assertEqual(scheduling["affinity"], "0")
        self.assertEqual(scheduling["rt_error"], 1)
        self.assertEqual(scheduling["mlock_error"], 12)
        self.assertIs(scheduling["mlock_attempted"], True)
        scheduling = workers["demod"]["scheduling"]
        self.assertEqual(scheduling["requested_priority"], 55)
        self.assertEqual(scheduling["affinity_error"], 22)
        self.assertEqual(scheduling["applied_cpu"], -1)
        self.assertIs(scheduling["mlock_attempted"], False)
        self.assertEqual(workers["audio"]["scheduling"], {"valid": False})
        display = "\n".join(client.format_snapshot(snapshot))
        self.assertIn("underruns=7", display)
        self.assertIn("recovery(attempt/ok/fail)=8/9/10", display)
        self.assertIn("sched=SCHED_OTHER/0", display)
        for packet in packets:
            self.assertLessEqual(len(packet), 8192)
            json.loads(packet)
        extreme = client.parse_snapshot(packets[1])
        self.assertEqual(extreme["config"]["playback"], "\x01" * 255)
        self.assertEqual(extreme["rx"]["playback"]["underruns"], 2 ** 64 - 1)
        self.assertEqual(extreme["sequence"], 2 ** 64 - 1)
        retained = client.parse_snapshot(packets[2])
        self.assertIs(retained["tx"]["initialized"], True)
        self.assertIs(retained["tx"]["running"], False)
        self.assertEqual(retained["tx"]["writer"]["samples"], 2 ** 64 - 1)
        self.assertEqual(retained["tx"]["capture"]["overruns"], 2 ** 64 - 1)
        self.assertEqual(retained["tx"]["generation"], 2 ** 64 - 1)

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
