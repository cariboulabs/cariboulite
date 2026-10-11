#!/usr/bin/env python3
"""Hardware-free checks for the physical RX comparison collector."""

import copy
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import compare_rx_applications as runner
from test_rx_comparison_screen import SCREEN, paint


def stat_line(name="radio (receiver)", ticks=100, start=77):
    fields = ["0"] * 50
    fields[0] = "S"
    fields[11], fields[12] = str(ticks), "50"
    fields[19] = str(start)
    fields[36], fields[37], fields[38] = "2", "70", "1"
    return "42 (" + name + ") " + " ".join(fields) + "\n"


class ComparisonRunnerTests(unittest.TestCase):
    def setUp(self):
        self.args = SimpleNamespace(frequency=430125000, sample_rate=1000000)

    def test_proc_offsets_and_optional_rollup(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            (directory / "stat").write_text(stat_line())
            (directory / "status").write_text("VmRSS:\t4096 kB\nVmHWM:\t5120 kB\n")
            (directory / "task" / "42").mkdir(parents=True)
            (directory / "task" / "42" / "stat").write_text(stat_line())
            (directory / "task" / "42" / "status").write_text("Cpus_allowed_list:\t2\nvoluntary_ctxt_switches:\t9\n")
            # A malformed rollup proves the ordinary 1 Hz path never reads it.
            (directory / "smaps_rollup").write_text("Pss: invalid kB\n")
            with patch.object(runner, "Path", return_value=directory):
                ordinary = runner.process_snapshot(42)
            self.assertEqual(ordinary["name"], "radio (receiver)")
            self.assertEqual(ordinary["ticks"], 150)
            self.assertEqual(ordinary["start_ticks"], 77)
            self.assertEqual((ordinary["last_cpu"], ordinary["rt_priority"], ordinary["policy"]), (2, 70, 1))
            self.assertNotIn("Pss_kib", ordinary)
            (directory / "smaps_rollup").write_text("Pss: 3072 kB\nPrivate_Clean: 512 kB\nPrivate_Dirty: 1536 kB\n")
            with patch.object(runner, "Path", return_value=directory):
                detailed = runner.process_snapshot(42, memory_details=True)
            self.assertEqual(detailed["Pss_kib"], 3072)
            self.assertEqual(detailed["Uss_kib"], 2048)
            (directory / "smaps_rollup").unlink()
            with patch.object(runner, "Path", return_value=directory):
                missing = runner.process_snapshot(42, memory_details=True)
            self.assertIsNone(missing["Pss_kib"])
            self.assertIsNone(missing["Uss_kib"])

    def test_cpu_delta_and_thread_identity(self):
        worker = dict(name="audio_demod", ticks=10, start_ticks=99, policy=0,
                      rt_priority=0, last_cpu=1, affinity="1",
                      voluntary_ctxt_switches=4, nonvoluntary_ctxt_switches=1)
        first = dict(pid=42, ticks=100, start_ticks=77, threads={"43":worker})
        last = copy.deepcopy(first)
        last.update(ticks=100 + runner.TICKS, VmRSS_kib=4096, VmHWM_kib=5120,
                    Pss_kib=3072, Uss_kib=2048)
        last["threads"]["43"].update(ticks=10 + runner.TICKS // 2,
                                      voluntary_ctxt_switches=14)
        last["threads"]["44"] = dict(worker, start_ticks=100)
        result = runner.process_delta(first, last, 2)
        self.assertEqual(result["cpu_percent_one_core"], 50)
        self.assertEqual((result["rss_mib"], result["pss_mib"], result["uss_mib"]), (4, 3, 2))
        self.assertEqual(len(result["threads"]), 1)
        self.assertEqual(result["threads"][0]["voluntary_ctxt_switches"], 10)
        last["start_ticks"] += 1
        with self.assertRaisesRegex(RuntimeError, "Process changed"):
            runner.process_delta(first, last, 2)

    def test_measurement_read_only_drains_raw_bytes(self):
        terminal = object.__new__(runner.TerminalProcess)
        terminal.master, terminal.eof, terminal.emulate = 7, False, False
        terminal.log, terminal.bytes, terminal.chunks = io.BytesIO(), 0, []
        terminal.recent, terminal.screen = b"old canvas", Mock()
        raw = b"\x1b[H\x1b[2Jnew frame"
        with patch.object(runner.os, "read", return_value=raw), patch.object(runner.time, "monotonic", return_value=12.5):
            terminal.read()
        terminal.screen.feed.assert_not_called()
        self.assertEqual(terminal.recent, b"old canvas")
        self.assertEqual(terminal.log.getvalue(), raw)
        self.assertEqual(terminal.chunks, [dict(monotonic_s=12.5, offset=0, length=len(raw))])

    def replay(self, chunks, start=1.5, stop=4.5, args=True):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "terminal.ansi.log"
            raw, metadata = b"", []
            for stamp, value in chunks:
                data = value.encode()
                metadata.append(dict(monotonic_s=stamp, offset=len(raw), length=len(data)))
                raw += data
            path.write_bytes(raw)
            return runner.reconstruct_terminal(path, metadata, 120, 240, start, stop,
                                               self.args if args else None)

    def test_offline_window_excludes_stale_screen_and_partial_repaint(self):
        second = SCREEN.replace("calls:1010", "calls:1011").replace("samples:10070000", "samples:10080000")
        third = second.replace("calls:1011", "calls:1012")
        partial = paint(third)
        split = partial.index("RX reads:")
        records, screens = self.replay([(1, paint(SCREEN)), (2, paint(SCREEN)),
            (3, paint(second)), (4, partial[:split]), (4.1, partial[split:]),
            (5, paint(third.replace("calls:1012", "calls:1013")))])
        self.assertEqual([row["monotonic_s"] for row in records], [3, 4.1])
        self.assertEqual([row["rx"]["reader"]["calls"] for row in records], [1011, 1012])
        self.assertTrue(all(1.5 <= row["monotonic_s"] <= 4.5 for row in screens))

    def test_offline_worker_exit_fails_comparison(self):
        broken = SCREEN.replace("calls:1010", "calls:1011").replace("reader created:1 alive:1", "reader created:1 alive:0")
        with self.assertRaisesRegex(RuntimeError, "worker exited"):
            self.replay([(1, paint(SCREEN)), (2, paint(broken))])

    def test_truncated_capture_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "terminal.ansi.log"
            path.write_bytes(b"abc")
            with self.assertRaisesRegex(RuntimeError, "Truncated terminal capture"):
                runner.reconstruct_terminal(path, [dict(monotonic_s=1, offset=0, length=4)],
                                            120, 240, 0, 2)


if __name__ == "__main__":
    unittest.main()
