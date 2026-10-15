#!/usr/bin/env python3
"""Integration checks for baseline lifecycle and interruption; never live TX."""
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "software/libcariboulite/tools/tx_queue_baseline.py"
BACKEND = ROOT / "build/radio-backend/cariboulite_radio_backend"


@unittest.skipUnless(BACKEND.is_file(), "existing backend build required")
class BaselineLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="tx-baseline-test-", dir="/tmp")
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name) / "artifacts"

    def command(self, *options):
        return [sys.executable, str(SCRIPT), "--dry-run", "--output", str(self.output), *options]

    def metadata(self):
        return json.loads((self.output / "metadata.json").read_text())

    def assert_clean(self, metadata):
        self.assertEqual(metadata["backend_exit_code"], 0)
        self.assertEqual(metadata["controller_exit_code"], 0)
        self.assertEqual(metadata["cleanup_errors"], [])
        for path in metadata["sockets"].values():
            self.assertFalse(Path(path).exists(), path)
        events = [json.loads(line) for line in (self.output / "events.jsonl").read_text().splitlines()]
        self.assertTrue(any(row["event"] == "cleanup_idle_confirmed" for row in events))

    def test_all_cases_change_generation_and_preserve_unavailable_metrics(self):
        result = subprocess.run(self.command("--duration", "0.6"), cwd=ROOT,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        metadata = self.metadata()
        self.assertEqual(metadata["result"], "complete")
        self.assertEqual(metadata["options"]["frequency"], 430100000)
        self.assertIn("--dry-run", metadata["commands"][0])
        self.assertIn("--seconds", metadata["commands"][0])
        self.assert_clean(metadata)
        snapshots = [json.loads(line) for line in (self.output / "status.jsonl").read_text().splitlines()]
        generations = set()
        for source in ("mic", "tone"):
            for rate in (1000000, 4000000):
                label = f"{source}-{rate // 1000000}M"
                rows = [row for row in snapshots if row["run"] == label and row["phase"] == "stable"]
                self.assertGreaterEqual(len(rows), 2, label)
                for row in rows:
                    self.assertEqual(row["source"]["kind"], "simulated")
                    self.assertEqual(row["state"], "DRY_RUN")
                    self.assertEqual(row["control"]["state"], "TX")
                    self.assertIs(row["tx"]["requested"], True)
                    self.assertIs(row["tx"]["running"], False)
                    self.assertIsNone(row["tx"]["queue"])
                    self.assertIsNone(row["tx"]["writer"])
                    self.assertEqual(row["tx"]["capture"], {"valid": False})
                    self.assertEqual(row["config"]["tx_source"], source)
                    self.assertEqual(row["config"]["tx_sample_rate_hz"], rate)
                generations.add(rows[-1]["tx"]["generation"])
        self.assertEqual(len(generations), 4)
        self.assertTrue((self.output / "README.md").is_file())
        summary = json.loads((self.output / "summary.json").read_text())
        self.assertTrue(all(not run["stable_segments"] for run in summary["runs"]))

    def test_sigterm_unkeys_then_exits_with_interrupted_artifacts(self):
        process = subprocess.Popen(self.command("--duration", "30", "--sources", "tone", "--rates", "1000000"),
                                   cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(lambda: process.kill() if process.poll() is None else None)
        line = process.stdout.readline()
        self.assertIn("SIMULATED lifecycle", line)
        process.send_signal(signal.SIGTERM)
        stdout, stderr = process.communicate(timeout=20)
        self.assertEqual(process.returncode, 130, line + stdout + stderr)
        metadata = self.metadata()
        self.assertEqual(metadata["result"], "interrupted")
        self.assertIn("SIGTERM", metadata["error"])
        self.assert_clean(metadata)

    def test_wrong_host_model_prevents_any_backend_launch(self):
        result = subprocess.run(self.command("--duration", "0.1", "--expect-model", "Impossible test model"),
                                cwd=ROOT, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 1)
        metadata = self.metadata()
        self.assertEqual(metadata["result"], "failed")
        self.assertIn("Expected host model", metadata["error"])
        self.assertEqual(metadata["commands"], [])
        self.assertNotIn("backend_exit_code", metadata)

    def test_requested_status_cadence_reaches_backend_and_cleans_up(self):
        alternate = Path(self.temporary.name) / "selected-backend"
        shutil.copy2(BACKEND, alternate)
        controller = BACKEND.with_name("cariboulite_radio_controller")
        result = subprocess.run(self.command("--duration", "0.2", "--sources", "tone",
                                            "--rates", "1000000", "--status-hz", "20",
                                            "--backend", str(alternate), "--controller", str(controller)),
                                cwd=ROOT, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        metadata = self.metadata()
        command = metadata["commands"][0]
        self.assertEqual(command[0], str(alternate))
        self.assertEqual(metadata["binaries"]["backend"]["path"], str(alternate))
        self.assertEqual(metadata["commands"][1][0], str(controller))
        self.assertEqual(float(command[command.index("--status-hz") + 1]), 20)
        self.assert_clean(metadata)


if __name__ == "__main__":
    unittest.main()
