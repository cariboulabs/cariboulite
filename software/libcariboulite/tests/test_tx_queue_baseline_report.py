#!/usr/bin/env python3
"""Measurement semantics and discontinuity guards; never access RF hardware."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools/tx_queue_baseline_report.py"
spec = importlib.util.spec_from_file_location("tx_queue_baseline_report", SCRIPT)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


def snapshot(when, puts, gets, samples, *, generation=3, session="backend-42", capture=0, overruns=0):
    """Representative public monitor packet; inputs are independent counters."""
    return {
        "run": "mic-1M", "phase": "stable", "state": "TX", "channel": "RF24",
        "source": {"kind": "live"}, "session": session, "owner_pid": 42,
        "control": {"state": "TX", "transition": False},
        "monotonic_s": when, "sequence": int(when * 100),
        "config": {"tx_source": "mic", "tx_sample_rate_hz": 1000000,
                   "tx_frequency_hz": 430100000, "tx_actual_frequency_hz": 430100001.3,
                   "tx_power_dbm": -12, "tx_actual_power_dbm": -12},
        "tx": {
            "running": True, "requested": True, "initialized": True, "generation": generation,
            "queue": {"puts": puts, "gets": gets, "depth": puts - gets, "capacity": 64,
                      "min_depth": 0, "max_depth": max(puts - gets, 4),
                      "drops": 0, "timeouts_put": 0, "timeouts_get": 0},
            "writer": {"calls": gets, "samples": samples, "written_sequence": gets,
                       "partial_writes": 0, "zero_writes": 0, "would_block": 0,
                       "errors": 0, "last_error": 0},
            "capture": {"valid": True, "read_calls": capture // 480, "frames_read": capture,
                        "overruns": overruns, "recovery_attempts": overruns,
                        "recovery_successes": overruns, "recovery_failures": 0,
                        "read_errors": 0, "last_error": 0},
            "workers": {"producer": {"created": True, "alive": True, "last_error": 0,
                                     "scheduling": {"valid": True, "policy_name": "SCHED_OTHER",
                                                    "priority": 0, "affinity": "0", "rt_error": 1}}},
            "fault": None,
        },
    }


def hardware_status(row, *, accepted=1000, dequeued=900, completed=10, underruns=0,
                    driver_generation=8, queued_bytes=32768, audio_queued=1900,
                    correction=0, underflows=0, overflows=0, cue_discards=0,
                    clock_faults=0):
    row["tx"].update(hardware_paced=True, driver={
        "valid": True, "abi_version": 1, "session_generation": driver_generation,
        "state": 3, "flags": 3, "bounded_fifo": True, "dma_running": True,
        "fifo_queued_bytes": queued_bytes, "fifo_capacity_bytes": 4194304,
        "fifo_limit_bytes": 65536, "period_bytes": 65536, "ring_bytes": 262144,
        "accepted_bytes": accepted, "fifo_dequeued_bytes": dequeued,
        "completed_periods": completed, "underrun_periods": underruns,
    }, audio_buffer={
        "valid": True, "created": True, "alive": True, "running": True,
        "clock_valid": True, "cue": False, "primed": True, "last_error": 0,
        "queued_frames": audio_queued, "capacity_frames": 16384, "target_frames": 1920,
        "input_per_output": 1 + correction / 1e6, "correction_ppm": correction,
        "captured_frames": 12345, "consumed_frames": 12000, "output_frames": 11999,
        "capture_calls": 100, "again_calls": 40, "read_calls": 20,
        "underflows": underflows, "overflows": overflows, "overflow_frames": overflows * 480,
        "cue_discard_frames": cue_discards, "resets": 1,
        "clock_limit_frames": clock_faults * 480, "clock_limit_faults": clock_faults,
    })
    row["tx"]["workers"]["capture"] = {
        "created": True, "alive": True, "last_error": 0,
        "scheduling": {"valid": True, "policy_name": "SCHED_OTHER", "priority": 0,
                       "affinity": "2", "rt_error": 1}}
    return row


class ReportTest(unittest.TestCase):
    def test_actual_elapsed_and_accepted_iq_are_independent_of_nominal_frames(self):
        # A 30-second target contains 30.4 seconds of observed data. An API
        # partially accepts one frame; multiplying gets by nominal frame size
        # would falsely claim 1 MS/s and mask that discrepancy.
        rows = [snapshot(100, 10, 10, 99000, capture=4800),
                snapshot(115.2, 1535, 1530, 15292500, capture=730000),
                snapshot(130.4, 3060, 3050, 30487000, capture=1459200),
                snapshot(142.1, 4240, 4220, 42177000, capture=2018400)]
        intervals = report.windows(rows, 30)
        self.assertEqual(len(intervals), 2)
        first = intervals[0]
        self.assertAlmostEqual(first["elapsed_s"], 30.4)
        self.assertAlmostEqual(first["rates"]["queue_get_frames_per_s"], 100)
        self.assertAlmostEqual(first["rates"]["queue_put_frames_per_s"], 3050 / 30.4)
        self.assertAlmostEqual(first["rates"]["writer_accepted_iq_samples_per_s"], 30388000 / 30.4)
        self.assertNotEqual(first["rates"]["writer_accepted_iq_samples_per_s"], 1000000)
        self.assertFalse(first["partial_window"])
        self.assertTrue(intervals[1]["partial_window"])
        self.assertAlmostEqual(report.interval(rows)["queue"]["growth_frames_per_s"], 20 / 42.1)

    def test_generations_sessions_configuration_and_counter_resets_never_cross(self):
        rows = [snapshot(100, 100, 99, 990000, generation=3),
                snapshot(110, 1100, 1098, 10980000, generation=3),
                snapshot(120, 3, 2, 20000, generation=4),
                snapshot(130, 1003, 1002, 10020000, generation=4),
                snapshot(140, 2, 1, 10000, generation=4, session="backend-43"),
                snapshot(150, 1002, 1001, 10010000, generation=4, session="backend-43"),
                snapshot(160, 2002, 2001, 20010000, generation=4, session="backend-43"),
                snapshot(170, 3002, 3001, 30010000, generation=4, session="backend-43")]
        rows[6]["config"]["tx_sample_rate_hz"] = 4000000
        rows[7]["config"]["tx_sample_rate_hz"] = 4000000
        rows += [snapshot(180, 1, 1, 10000, generation=4, session="backend-43"),
                 snapshot(190, 1001, 1001, 10010000, generation=4, session="backend-43")]
        rows[-2]["config"]["tx_sample_rate_hz"] = 4000000
        rows[-1]["config"]["tx_sample_rate_hz"] = 4000000
        segments = report.split_segments(rows)
        self.assertEqual(len(segments), 5)
        self.assertTrue(segments[-1][1].startswith("counter reset"))
        self.assertTrue(all(report.interval(segment)["elapsed_s"] == 10 for segment, _ in segments))
        self.assertTrue(all(report.interval(segment)["counter_deltas"]["writer_samples"] > 0
                            for segment, _ in segments))

    def test_incomplete_control_transition_is_not_stable_tx(self):
        a = snapshot(10, 1, 1, 10000)
        b = snapshot(20, 1001, 1001, 10010000)
        c = snapshot(30, 2001, 2001, 20010000)
        b["control"]["transition"] = True
        c["control"]["state"] = "STOPPING_TX"
        segments = report.split_segments([a, b, c])
        self.assertEqual(len(segments), 1)
        self.assertEqual(len(segments[0][0]), 1)

    def test_hidden_counter_gap_is_unavailable_and_capture_reset_splits(self):
        a = snapshot(10, 1, 1, 10000, capture=480, overruns=3)
        b = snapshot(20, 1001, 1001, 10010000, capture=480480, overruns=3)
        c = snapshot(30, 2001, 2001, 20010000, capture=960480, overruns=1)
        self.assertEqual(len(report.split_segments([a, b, c])), 2)
        b["tx"]["writer"] = None
        result = report.interval([a, b, c])
        self.assertIsNone(result["counter_deltas"]["writer_samples"])
        self.assertIsNone(result["rates"]["writer_accepted_iq_samples_per_s"])

    def test_kernel_samples_require_kernel_time_and_stable_interval(self):
        kernel = [
            {"monotonic_s": 110, "line": "[   50.1] smi: init programmed write. missed:9999, sema 0"},
            {"monotonic_s": 120, "line": "smi: init programmed write. missed:8888, sema 0"},
            {"line": "[  111.0] smi: init programmed write. missed:400, sema 0"},
            {"line": "[  129.0] smi: init programmed write. missed:450, sema 0"},
            {"run": "other", "line": "[  130.0] smi: init programmed write. missed:500, sema 0"},
            {"line": "[  141.0] smi: init programmed write. missed:800, sema 0"},
        ]
        measured = report.kernel_summary(kernel, 100, 140, "mic-1M")
        self.assertTrue(measured["available"])
        self.assertEqual(measured["counter_delta"], 50)
        self.assertEqual(measured["elapsed_s"], 18)
        # Counter reset inside one purported stable interval is ambiguous.
        kernel += [{"line": "[  135.0] smi: init programmed write. missed:3, sema 0"}]
        self.assertFalse(report.kernel_summary(kernel, 100, 140, "mic-1M")["available"])

    def test_cpu_is_one_core_percentage_and_pid_reuse_never_subtracted(self):
        def process(ticks, start, voluntary):
            return {"available": True, "pid": 42, "start_ticks": start, "ticks": ticks,
                    "voluntary_ctxt_switches": voluntary, "nonvoluntary_ctxt_switches": 4,
                    "policy": 0, "affinity": "0-3", "threads": {}}
        samples = [
            {"monotonic_s": 10, "processes": {"backend": process(100, 9, 5)}},
            {"monotonic_s": 20, "processes": {"backend": process(1600, 9, 15)}},
            {"monotonic_s": 30, "processes": {"backend": process(10, 29, 1)}},
            {"monotonic_s": 40, "processes": {"backend": process(510, 29, 9)}},
        ]
        result = report.proc_intervals(samples, report.process_selector, 100)
        self.assertEqual(len(result["intervals"]), 2)
        self.assertEqual(result["intervals"][0]["cpu_percent_of_one_core"], 150)
        self.assertEqual(result["intervals"][1]["cpu_percent_of_one_core"], 50)
        self.assertEqual(result["intervals"][0]["voluntary_context_switches_delta"], 10)

    def test_empty_and_dry_run_artifacts_do_not_fabricate_counters(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            metadata = {"options": {"dry_run": True, "frequency": 430100000},
                        "runlist": [{"run": "mic-1M"}], "result": "complete"}
            (path / "metadata.json").write_text(json.dumps(metadata))
            fake = snapshot(10, 100, 90, 900000)
            fake["source"]["kind"] = "dry-run"
            fake["tx"]["running"] = False
            (path / "status.jsonl").write_text(json.dumps(fake) + "\n")
            summary = report.generate_report(path, make_plot=False)
            self.assertEqual(summary["runs"][0]["stable_segments"], [])
            text = (path / "README.md").read_text()
            self.assertIn("rates, CPU measurements and counter deltas are unavailable", text)
            self.assertNotIn("900000", text)
            self.assertEqual(json.loads((path / "summary.json").read_text())["status_records"], 1)
            empty = path / "empty"
            self.assertEqual(report.generate_report(empty, make_plot=False)["runs"], [])

    def test_disabled_scheduler_stats_are_unavailable_even_if_zero_is_reported(self):
        samples = [{"monotonic_s": t, "host": {"sched_schedstats": False},
                    "processes": {"backend": {"available": True, "pid": 42, "start_ticks": 1,
                                               "ticks": t * 50, "schedstat": {
                                                   "available": True, "runtime_ns": t * 500000000,
                                                   "runqueue_wait_ns": 0, "timeslices": 0}}}}
                   for t in (10, 20)]
        result = report.proc_intervals(samples, report.process_selector, 100)
        self.assertEqual(result["intervals"][0]["cpu_percent_of_one_core"], 50)
        scheduler = result["intervals"][0]["scheduler_statistics"]
        self.assertFalse(scheduler["available"])
        self.assertNotIn("runqueue_wait_s_delta", scheduler)

    def test_metadata_frequency_and_setup_records_do_not_create_a_pseudo_run(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / "metadata.json").write_text(json.dumps({"options": {"frequency": 145000000},
                                                           "runlist": [{"run": "mic-1M"}]}))
            setup = snapshot(10, 1, 1, 10000)
            setup.update(run=None, phase="setup")
            (path / "status.jsonl").write_text(json.dumps(setup))
            summary = report.generate_report(path, make_plot=False)
            self.assertEqual(summary["requested_frequency_hz"], 145000000)
            self.assertEqual([run["run"] for run in summary["runs"]], ["mic-1M"])

    def test_live_report_preserves_errors_and_deduplicates_kernel_snapshots(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / "metadata.json").write_text(json.dumps({"clock_ticks_per_second": 100,
                                                           "runlist": [{"run": "mic-1M"}]}))
            rows = [snapshot(100, 5, 4, 40000), snapshot(140, 4020, 4000, 40000000)]
            (path / "status.jsonl").write_text("\n".join(map(json.dumps, rows)))
            (path / "kernel.jsonl").write_text("")
            kernel = "[ 111.0] smi: init programmed write. missed:42, sema 0\n[ 130.0] smi: init programmed write. missed:43, sema 0\n"
            (path / "dmesg-mic-1M.log").write_text(kernel)
            (path / "dmesg-end.log").write_text(kernel)
            summary = report.generate_report(path, make_plot=False)
            segment = summary["runs"][0]["stable_segments"][0]
            self.assertEqual(segment["kernel_tx_missed"]["samples"], 2)
            self.assertEqual(segment["kernel_tx_missed"]["counter_delta"], 1)
            scheduling = segment["worker_status_changes"]["producer"][0]["status"]["scheduling"]
            self.assertEqual(scheduling["rt_error"], 1)
            self.assertEqual(scheduling["affinity"], "0")
            self.assertFalse(segment["resources"]["host"]["available"])

    def test_driver_rates_and_audio_clock_use_independent_observed_counters(self):
        rows = [
            hardware_status(snapshot(100, 5, 4, 40000), accepted=100000, dequeued=90000,
                            completed=99, underruns=7, queued_bytes=30000, audio_queued=1800,
                            correction=0, underflows=1, overflows=4, cue_discards=480, clock_faults=3),
            hardware_status(snapshot(105.125, 517, 515, 5150000), accepted=20600000, dequeued=20590000,
                            completed=412, underruns=8, queued_bytes=40000, audio_queued=2200,
                            correction=4200, underflows=2, overflows=4, cue_discards=960, clock_faults=3),
            hardware_status(snapshot(110.25, 1030, 1028, 10280000), accepted=41100000, dequeued=41090000,
                            completed=725, underruns=10, queued_bytes=20000, audio_queued=1920,
                            correction=3900, underflows=3, overflows=5, cue_discards=1440, clock_faults=4),
        ]
        result = report.interval(rows)
        driver, audio = result["driver"], result["audio_buffer"]
        self.assertTrue(result["hardware_paced"])
        self.assertTrue(driver["complete_status_coverage"])
        self.assertAlmostEqual(driver["rates"]["accepted_bytes_per_s"], 4000000)
        self.assertAlmostEqual(driver["rates"]["fifo_dequeued_bytes_per_s"], 4000000)
        self.assertAlmostEqual(driver["rates"]["completed_periods_per_s"], 626 / 10.25)
        self.assertEqual(driver["counter_deltas"]["underrun_periods"], 3)
        self.assertEqual(driver["fifo_queued_bytes_observed_min"], 20000)
        self.assertEqual(driver["fifo_queued_bytes_observed_max"], 40000)
        self.assertEqual(audio["correction_ppm_mean"], 2700)
        self.assertEqual(audio["correction_ppm_end"], 3900)
        self.assertEqual(audio["correction_ppm_min"], 0)
        self.assertEqual(audio["correction_ppm_max"], 4200)
        self.assertEqual(audio["queued_frames_observed_min"], 1800)
        self.assertEqual(audio["queued_frames_observed_max"], 2200)
        self.assertEqual(audio["counter_deltas"]["underflows"], 2)
        self.assertEqual(audio["counter_deltas"]["overflows"], 1)
        self.assertEqual(audio["counter_deltas"]["overflow_frames"], 480)
        self.assertEqual(audio["counter_deltas"]["cue_discard_frames"], 960)
        self.assertEqual(audio["counter_deltas"]["clock_limit_faults"], 1)
        self.assertTrue(audio["clock_valid_all_samples"])
        self.assertIn("do not measure RF emission", driver["meaning"])

    def test_driver_session_and_hardware_pacing_changes_split_even_without_counter_reset(self):
        rows = [hardware_status(snapshot(t, t * 100, t * 100, t * 1000000),
                                accepted=t * 4000000, dequeued=t * 4000000,
                                completed=t * 60, driver_generation=8 if t < 30 else 9)
                for t in (10, 20, 30, 40)]
        segments = report.split_segments(rows)
        self.assertEqual(len(segments), 2)
        self.assertEqual(segments[-1][1], "driver session generation changed")
        self.assertEqual([report.interval(row)["elapsed_s"] for row, _ in segments], [10, 10])
        rows[-1]["tx"]["hardware_paced"] = False
        self.assertEqual(len(report.split_segments(rows)), 3)

    def test_driver_and_audio_counter_resets_start_new_intervals(self):
        for section, key, expected in (("driver", "underrun_periods", "driver_underrun_periods"),
                                       ("audio_buffer", "overflows", "audio_buffer_overflows")):
            with self.subTest(section=section):
                rows = [hardware_status(snapshot(t, t * 100, t * 100, t * 1000000))
                        for t in (10, 20, 30, 40)]
                for row, count in zip(rows, (4, 8, 1, 2)):
                    row["tx"][section][key] = count
                segments = report.split_segments(rows)
                self.assertEqual(len(segments), 2)
                self.assertEqual(segments[-1][1], "counter reset: " + expected)
                self.assertEqual(report.interval(segments[-1][0])[section]["counter_deltas"][key], 1)

    def test_optional_telemetry_gaps_cannot_claim_rates_or_zero_faults(self):
        rows = [hardware_status(snapshot(t, t * 100, t * 100, t * 1000000),
                                accepted=t * 4000000, dequeued=t * 4000000, correction=t)
                for t in (10, 20, 30)]
        rows[1]["tx"]["driver"]["valid"] = False
        rows[1]["tx"]["audio_buffer"].update(valid=False, correction_ppm=99999)
        rows[2]["tx"]["audio_buffer"]["clock_valid"] = False
        result = report.interval(rows)
        self.assertFalse(result["driver"]["complete_status_coverage"])
        self.assertIsNone(result["driver"]["rates"]["accepted_bytes_per_s"])
        self.assertIsNone(result["driver"]["counter_deltas"]["underrun_periods"])
        audio = result["audio_buffer"]
        self.assertEqual(audio["correction_ppm_mean"], 20)
        self.assertEqual(audio["correction_ppm_max"], 30)
        self.assertEqual(audio["correction_status_samples"], 2)
        self.assertIsNone(audio["counter_deltas"]["overflows"])
        self.assertIsNone(audio["clock_valid_all_samples"])
        self.assertFalse(audio["clock_valid_end"])
        legacy = report.interval([snapshot(10, 1, 1, 10000), snapshot(20, 1001, 1001, 10010000)])
        self.assertNotIn("driver", legacy)
        self.assertNotIn("audio_buffer", legacy)
        self.assertNotIn("driver_accepted_bytes", legacy["counter_deltas"])

    def test_hardware_report_tables_preserve_capture_worker_and_measurement_semantics(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            rows = [hardware_status(snapshot(t, t * 100, t * 100, t * 1000000),
                                    accepted=t * 4000000, dequeued=t * 4000000,
                                    completed=t * 60, correction=t * 100)
                    for t in (100, 110, 120)]
            (path / "metadata.json").write_text(json.dumps({"runlist": [{"run": "mic-1M"}]}))
            (path / "status.jsonl").write_text("\n".join(map(json.dumps, rows)))
            summary = report.generate_report(path, window_seconds=10, make_plot=False)
            segment = summary["runs"][0]["stable_segments"][0]
            text = (path / "README.md").read_text()
            self.assertEqual(len(segment["windows"]), 2)
            self.assertEqual(segment["driver"]["rates"]["accepted_bytes_per_s"], 4000000)
            for expected in ("TX DMA transport", "Accepted bytes/s | 4000000.00",
                             "Completed DMA periods/s | 60.00", "TX audio clock matching",
                             "Correction mean / end, ppm | 11000.00 / 12000.00",
                             "do not measure RF emission", "Clock-limit faults delta"):
                self.assertIn(expected, text)
            history = segment["worker_status_changes"]["capture"]
            self.assertEqual(history[0]["status"]["scheduling"]["affinity"], "2")

    def test_terminal_clock_fault_is_preserved_outside_the_stable_interval(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            first = hardware_status(snapshot(10, 1, 1, 10000))
            last = hardware_status(snapshot(20, 1001, 1001, 10010000))
            fault = hardware_status(snapshot(21, 1002, 1002, 10020000), clock_faults=1)
            fault.update(phase="cleanup", state="FAULT")
            fault["control"].update(state="FAULT", transition=False)
            fault["tx"].update(running=False, requested=False, fault="TX_WORKER")
            fault["tx"]["workers"]["producer"]["last_error"] = -34
            fault["tx"]["workers"]["capture"]["last_error"] = -34
            fault["tx"]["audio_buffer"].update(last_error=-34, clock_valid=False)
            # Destruction can remove observations. The last valid counter and
            # recorded nonzero errors must survive that final unavailable row.
            closed = json.loads(json.dumps(fault))
            closed.update(monotonic_s=22, sequence=2200)
            closed["tx"]["audio_buffer"] = {"valid": False}
            closed["tx"]["workers"]["producer"]["last_error"] = 0
            closed["tx"]["workers"]["capture"]["last_error"] = 0
            (path / "metadata.json").write_text(json.dumps({"result": "failed"}))
            (path / "status.jsonl").write_text("\n".join(map(json.dumps, (first, last, fault, closed))))
            summary = report.generate_report(path, make_plot=False)
            run = summary["runs"][0]
            self.assertEqual(run["stable_segments"][0]["audio_buffer"]["counter_deltas"]["clock_limit_faults"], 0)
            terminal = run["terminal"]
            self.assertTrue(terminal["aborted"])
            self.assertEqual(terminal["clock_limit_faults"]["final_total"], 1)
            self.assertEqual(terminal["clock_limit_faults"]["delta_since_first_stable"], 1)
            self.assertEqual(terminal["clock_limit_faults"]["final_monotonic_s"], 21)
            self.assertEqual(terminal["recorded_errors"]["producer_last_error"], -34)
            self.assertEqual(terminal["recorded_errors"]["capture_worker_last_error"], -34)
            self.assertEqual(terminal["recorded_errors"]["capture_last_error"], 0)
            text = (path / "README.md").read_text()
            for expected in ("Run aborted", "TX_WORKER", "producer error `-34`",
                             "capture worker error `-34`", "**1 / 1**",
                             "exclude the terminal fault transition"):
                self.assertIn(expected, text)

    def test_terminal_clock_fault_delta_does_not_cross_generation_or_missing_data(self):
        first = hardware_status(snapshot(10, 1, 1, 10000))
        fault = hardware_status(snapshot(20, 1001, 1001, 10010000, generation=4), clock_faults=1)
        fault.update(state="FAULT", phase="cleanup")
        fault["tx"]["fault"] = "TX_WORKER"
        terminal = report.terminal_status([first, fault])
        self.assertTrue(terminal["aborted"])
        self.assertEqual(terminal["clock_limit_faults"]["final_total"], 1)
        self.assertIsNone(terminal["clock_limit_faults"]["delta_since_first_stable"])
        fault["tx"]["generation"] = 3
        missing = json.loads(json.dumps(first))
        missing.update(monotonic_s=15, sequence=1500)
        missing["tx"]["audio_buffer"]["valid"] = False
        self.assertIsNone(report.terminal_status([first, missing, fault])["clock_limit_faults"]["delta_since_first_stable"])

    def test_retained_previous_generation_fault_does_not_abort_a_clean_run(self):
        old = hardware_status(snapshot(9, 1, 1, 10000, generation=2), clock_faults=1)
        old.update(phase="warmup", state="FAULT")
        old["tx"]["fault"] = "TX_WORKER"
        current = hardware_status(snapshot(10, 1, 1, 10000))
        terminal = report.terminal_status([old, current])
        self.assertFalse(terminal["aborted"])
        self.assertEqual(terminal["clock_limit_faults"]["final_total"], 0)


if __name__ == "__main__":
    unittest.main()
