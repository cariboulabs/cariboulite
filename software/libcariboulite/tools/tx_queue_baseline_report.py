#!/usr/bin/env python3
"""Summarize timed TX status and /proc artifacts without accessing hardware.

Rates use differences between actual monotonic timestamps. Each backend
session, TX generation, configuration, or counter reset starts a separate
segment; no counter difference spans one of those boundaries.
"""
import argparse
import json
import math
from pathlib import Path
import re


COUNTERS = {
    "queue_puts": ("queue", "puts"),
    "queue_gets": ("queue", "gets"),
    "queue_drops": ("queue", "drops"),
    "queue_put_timeouts": ("queue", "timeouts_put"),
    "queue_get_timeouts": ("queue", "timeouts_get"),
    "writer_calls": ("writer", "calls"),
    "writer_samples": ("writer", "samples"),
    "writer_completed_frames": ("writer", "written_sequence"),
    "writer_partial_writes": ("writer", "partial_writes"),
    "writer_zero_writes": ("writer", "zero_writes"),
    "writer_would_block": ("writer", "would_block"),
    "writer_errors": ("writer", "errors"),
    "capture_read_calls": ("capture", "read_calls"),
    "capture_frames": ("capture", "frames_read"),
    "capture_overruns": ("capture", "overruns"),
    "capture_recovery_attempts": ("capture", "recovery_attempts"),
    "capture_recovery_successes": ("capture", "recovery_successes"),
    "capture_recovery_failures": ("capture", "recovery_failures"),
    "capture_errors": ("capture", "read_errors"),
    "driver_completed_periods": ("driver", "completed_periods"),
    "driver_underrun_periods": ("driver", "underrun_periods"),
    "driver_accepted_bytes": ("driver", "accepted_bytes"),
    "driver_fifo_dequeued_bytes": ("driver", "fifo_dequeued_bytes"),
    **{"audio_buffer_" + key: ("audio_buffer", key) for key in (
        "captured_frames", "consumed_frames", "output_frames", "capture_calls",
        "again_calls", "read_calls", "underflows", "overflows", "overflow_frames",
        "cue_discard_frames", "resets", "clock_limit_frames", "clock_limit_faults")},
}
RATE_NAMES = {
    "queue_puts": "queue_put_frames_per_s",
    "queue_gets": "queue_get_frames_per_s",
    "writer_samples": "writer_accepted_iq_samples_per_s",
    "writer_completed_frames": "writer_completed_frames_per_s",
    "capture_frames": "capture_audio_frames_per_s",
    "driver_completed_periods": "driver_completed_periods_per_s",
    "driver_accepted_bytes": "driver_accepted_bytes_per_s",
    "driver_fifo_dequeued_bytes": "driver_fifo_dequeued_bytes_per_s",
}
OPTIONAL_SECTIONS = ("driver", "audio_buffer")


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def read_jsonl(path, warnings):
    if not path.exists():
        return []
    rows = []
    with path.open(errors="replace") as stream:
        for lineno, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
                if not isinstance(row, dict):
                    raise ValueError("record is not an object")
                rows.append(row)
            except (ValueError, TypeError) as exc:
                warnings.append(f"{path.name}:{lineno}: {exc}")
    return rows


def counter(row, path):
    block = row.get("tx", {}).get(path[0])
    if not isinstance(block, dict) or block.get("valid") is False:
        return None
    if path[0] in OPTIONAL_SECTIONS and block.get("valid") is not True:
        return None
    value = block.get(path[1])
    return value if number(value) and value >= 0 else None


def status_time(row):
    value = row.get("monotonic_s")
    return value if number(value) else None


def stable(row):
    tx = row.get("tx", {})
    control = row.get("control", {})
    return (row.get("phase") == "stable" and row.get("state") == "TX"
            and control.get("state") == "TX" and control.get("transition") is False
            and row.get("source", {}).get("kind") == "live"
            and all(tx.get(key) is True for key in ("running", "requested", "initialized"))
            and row.get("session") is not None and number(tx.get("generation"))
            and status_time(row) is not None)


def identity(row):
    config = row.get("config", {})
    return (row.get("run"), row.get("session"), row.get("owner_pid"), row.get("channel"),
            row.get("tx", {}).get("generation"), row.get("tx", {}).get("hardware_paced"),
            tuple(config.get(key) for key in (
                "tx_source", "tx_sample_rate_hz", "tx_frequency_hz", "tx_power_dbm",
                "tx_tone_hz", "tx_tone_amp", "tx_deviation_hz")))


def split_segments(rows):
    """Return independent stable intervals and explicit reasons for boundaries."""
    segments, current = [], []
    reason = "stable TX began"
    for row in rows:
        boundary = None
        if not stable(row):
            boundary = "TX state or stable phase ended"
        elif current:
            previous = current[-1]
            if identity(row) != identity(previous):
                boundary = "session, generation, owner, or TX configuration changed"
            elif (optional_block(row, "driver").get("session_generation") is not None
                  and optional_block(previous, "driver").get("session_generation") is not None
                  and optional_block(row, "driver")["session_generation"]
                  != optional_block(previous, "driver")["session_generation"]):
                boundary = "driver session generation changed"
            elif status_time(row) <= status_time(previous):
                boundary = "status timestamp did not advance"
            elif (number(row.get("sequence")) and number(previous.get("sequence"))
                  and row["sequence"] <= previous["sequence"]):
                boundary = "status sequence did not advance"
            else:
                for name, path in COUNTERS.items():
                    old, new = counter(previous, path), counter(row, path)
                    if old is not None and new is not None and new < old:
                        boundary = f"counter reset: {name}"
                        break
        if boundary and current:
            segments.append((current, reason))
            current = []
        if stable(row):
            if not current:
                reason = boundary or "stable TX began"
            current.append(row)
    if current:
        segments.append((current, reason))
    return segments


def optional_block(row, name):
    block = row.get("tx", {}).get(name)
    return block if isinstance(block, dict) and block.get("valid") is True else {}


def observed_values(rows, section, field):
    return [optional_block(row, section).get(field) for row in rows]


def optional_telemetry(rows, section, deltas, rates):
    """Keep optional observations explicit, including gaps in counter coverage."""
    blocks = [optional_block(row, section) for row in rows]
    valid = [block for block in blocks if block]
    result = {"available": bool(valid), "observed_status_samples": len(valid),
              "complete_status_coverage": len(valid) == len(rows),
              "counter_deltas": {name.removeprefix(section + "_"): value
                                 for name, value in deltas.items() if name.startswith(section + "_")},
              "rates": {name.removeprefix(section + "_"): value
                        for name, value in rates.items() if name.startswith(section + "_")}}
    queue_field = "fifo_queued_bytes" if section == "driver" else "queued_frames"
    values = [block.get(queue_field) for block in blocks]
    observed = [value for value in values if number(value)]
    result.update({queue_field + "_start": values[0], queue_field + "_end": values[-1],
                   queue_field + "_observed_min": min(observed) if observed else None,
                   queue_field + "_observed_max": max(observed) if observed else None})
    if section == "driver":
        for key in ("session_generation", "fifo_capacity_bytes", "fifo_limit_bytes", "period_bytes",
                    "ring_bytes", "state", "flags", "bounded_fifo", "dma_running"):
            result[key + "_end"] = blocks[-1].get(key)
        result["session_generation_start"] = blocks[0].get("session_generation")
        result["meaning"] = "DMA retirement measures entry into SMI transport; accepted/dequeued byte rates do not measure RF emission"
    else:
        for key in ("capacity_frames", "target_frames", "input_per_output", "clock_valid",
                    "cue", "primed", "last_error"):
            result[key + "_end"] = blocks[-1].get(key)
        correction = observed_values(rows, section, "correction_ppm")
        observed_correction = [value for value in correction if number(value)]
        result.update({"correction_ppm_start": correction[0], "correction_ppm_end": correction[-1],
                       "correction_ppm_mean": sum(observed_correction) / len(observed_correction)
                       if observed_correction else None,
                       "correction_ppm_min": min(observed_correction) if observed_correction else None,
                       "correction_ppm_max": max(observed_correction) if observed_correction else None,
                       "correction_status_samples": len(observed_correction),
                       "correction_mean_basis": "sample mean of valid saved status observations"})
        clocks = observed_values(rows, section, "clock_valid")
        result["clock_valid_all_samples"] = (all(clocks) if all(type(value) is bool for value in clocks) else None)
    return result


def interval(rows):
    first, last = rows[0], rows[-1]
    elapsed = status_time(last) - status_time(first)
    deltas, rates = {}, {}
    for name, path in COUNTERS.items():
        if path[0] in OPTIONAL_SECTIONS and not any(path[0] in row.get("tx", {}) for row in rows):
            continue
        values = [counter(row, path) for row in rows]
        # Missing intermediate values make a hidden reset impossible to exclude.
        valid = all(value is not None for value in values)
        delta = values[-1] - values[0] if valid and len(rows) > 1 else None
        if delta is not None and (delta < 0 or any(b < a for a, b in zip(values, values[1:]))):
            delta = None
        deltas[name] = delta
        if name in RATE_NAMES:
            rates[RATE_NAMES[name]] = delta / elapsed if delta is not None and elapsed > 0 else None
    queues = [row.get("tx", {}).get("queue") or {} for row in rows]
    depths = [q.get("depth") for q in queues if number(q.get("depth"))]
    maxima = [q.get("max_depth") for q in queues if number(q.get("max_depth"))]
    minima = [q.get("min_depth") for q in queues if number(q.get("min_depth"))]
    start_depth, end_depth = queues[0].get("depth"), queues[-1].get("depth")
    capacity = queues[-1].get("capacity")
    change = end_depth - start_depth if number(start_depth) and number(end_depth) else None
    slope = change / elapsed if change is not None and elapsed > 0 else None
    saturated = next((status_time(row) - status_time(first) for row, q in zip(rows, queues)
                      if number(q.get("depth")) and number(q.get("capacity"))
                      and q["depth"] >= q["capacity"]), None)
    estimate = ((capacity - end_depth) / slope
                if number(capacity) and number(end_depth) and slope is not None
                and slope > 0 and end_depth < capacity and saturated is None else None)
    result = {
        "start_monotonic_s": status_time(first), "end_monotonic_s": status_time(last),
        "elapsed_s": elapsed, "status_samples": len(rows), "counter_deltas": deltas,
        "rates": rates,
        "queue": {
            "start_depth": start_depth, "end_depth": end_depth, "capacity": capacity,
            "observed_min_depth": min(depths) if depths else None,
            "observed_max_depth": max(depths) if depths else None,
            "reported_lifetime_min_depth": min(minima) if minima else None,
            "reported_lifetime_max_depth": max(maxima) if maxima else None,
            "depth_change": change, "growth_frames_per_s": slope,
            "first_capacity_observed_after_start_s": saturated,
            "linear_estimated_seconds_to_capacity_from_end": estimate,
            "estimate_basis": "endpoint depth slope; extrapolation, not an observed saturation time" if estimate is not None else None,
        },
        "writer_last_error": (last.get("tx", {}).get("writer") or {}).get("last_error"),
        "capture_last_error": (last.get("tx", {}).get("capture") or {}).get("last_error"),
        "fault": last.get("tx", {}).get("fault"),
    }
    for section in OPTIONAL_SECTIONS:
        if any(section in row.get("tx", {}) for row in rows):
            result[section] = optional_telemetry(rows, section, deltas, rates)
    if any("hardware_paced" in row.get("tx", {}) for row in rows):
        result["hardware_paced"] = last.get("tx", {}).get("hardware_paced")
    return result


def windows(rows, seconds):
    results, start = [], 0
    for index in range(1, len(rows)):
        if status_time(rows[index]) - status_time(rows[start]) >= seconds:
            result = interval(rows[start:index + 1])
            result["partial_window"] = False
            results.append(result)
            start = index
    if start < len(rows) - 1:
        result = interval(rows[start:])
        result["partial_window"] = True
        results.append(result)
    return results


def sample_time(row):
    value = row.get("monotonic_s")
    return value if number(value) else None


def proc_intervals(samples, selector, hz):
    """Keep each PID/TID incarnation separate; percentages use one CPU core."""
    groups = {}
    errors = []
    for row in samples:
        when = sample_time(row)
        for role, item in selector(row):
            if not isinstance(item, dict) or item.get("available") is False:
                errors.append({"monotonic_s": when, "role": role, "detail": item})
                continue
            key = (role, item.get("pid", item.get("tid")), item.get("start_ticks"))
            if key[1] is None or key[2] is None or not number(item.get("ticks")):
                errors.append({"monotonic_s": when, "role": role, "detail": item,
                               "error": "PID/TID identity or CPU counter unavailable"})
                continue
            groups.setdefault(key, []).append((when, item, row.get("host", {}).get("sched_schedstats")))
    result = []
    for (role, pid, start_ticks), items in groups.items():
        measured = 0.0
        ticks = 0
        voluntary = nonvoluntary = 0
        have_voluntary = have_nonvoluntary = False
        excluded = 0
        for (ta, a, _), (tb, b, _) in zip(items, items[1:]):
            if tb <= ta or b["ticks"] < a["ticks"]:
                excluded += 1
                continue
            measured += tb - ta
            ticks += b["ticks"] - a["ticks"]
            for field, flag in (("voluntary_ctxt_switches", "voluntary"),
                                ("nonvoluntary_ctxt_switches", "nonvoluntary")):
                old, new = a.get(field), b.get(field)
                if number(old) and number(new) and new >= old:
                    if flag == "voluntary":
                        voluntary += new - old
                        have_voluntary = True
                    else:
                        nonvoluntary += new - old
                        have_nonvoluntary = True
        schedstat = {"available": False, "reason": "kernel sched_schedstats disabled or availability unverified"}
        if all(enabled is True and isinstance(item.get("schedstat"), dict)
               and item["schedstat"].get("available") is True for _, item, enabled in items):
            fields = ("runtime_ns", "runqueue_wait_ns", "timeslices")
            totals = {field: 0 for field in fields}
            valid = len(items) > 1
            for (_, a, _), (_, b, _) in zip(items, items[1:]):
                for field in fields:
                    initial, final = a["schedstat"].get(field), b["schedstat"].get(field)
                    if not number(initial) or not number(final) or final < initial:
                        valid = False
                    else:
                        totals[field] += final - initial
            if valid:
                schedstat = {"available": True, "runtime_s_delta": totals["runtime_ns"] / 1e9,
                             "runqueue_wait_s_delta": totals["runqueue_wait_ns"] / 1e9,
                             "timeslices_delta": totals["timeslices"]}
            else:
                schedstat = {"available": False, "reason": "scheduler counters missing, reset, or fewer than two samples"}
        wchan = {}
        for _, item, _ in items:
            value = item.get("wchan")
            if isinstance(value, str):
                wchan[value] = wchan.get(value, 0) + 1
        result.append({
            "role": role, "pid_or_tid": pid, "start_ticks": start_ticks,
            "samples": len(items), "measured_elapsed_s": measured,
            "cpu_ticks_delta": ticks if measured else None,
            "cpu_percent_of_one_core": 100 * ticks / hz / measured if measured and number(hz) and hz > 0 else None,
            "voluntary_context_switches_delta": voluntary if have_voluntary else None,
            "nonvoluntary_context_switches_delta": nonvoluntary if have_nonvoluntary else None,
            "excluded_counter_or_time_intervals": excluded,
            "scheduler_statistics": schedstat,
            "sampled_wchan_counts": wchan,
            "first": items[0][1], "last": items[-1][1],
        })
    return {"available": bool(result), "intervals": result, "sampling_errors": errors}


def process_selector(row):
    return list((row.get("processes") or {}).items())


def thread_selector(row):
    result = []
    for owner, process in (row.get("processes") or {}).items():
        for tid, thread in (process.get("threads") or {}).items():
            thread = dict(thread, tid=thread.get("tid", tid))
            result.append((owner + ":" + str(thread.get("name", tid)), thread))
    return result


def host_summary(samples):
    if len(samples) < 2:
        return {"available": False, "reason": "fewer than two resource samples"}
    first, last = samples[0], samples[-1]
    a, b = first.get("host", {}), last.get("host", {})
    cpu = {}
    for name, initial in (a.get("cpu") or {}).items():
        final = (b.get("cpu") or {}).get(name)
        if not isinstance(initial, list) or not isinstance(final, list) or len(initial) < 8 or len(final) < 8:
            continue
        delta = [y - x for x, y in zip(initial[:8], final[:8])]
        if any(value < 0 for value in delta):
            cpu[name] = {"available": False, "reason": "CPU counter reset"}
            continue
        total = sum(delta)
        cpu[name] = {"ticks_delta_user_nice_system_idle_iowait_irq_softirq_steal": delta,
                     "busy_percent": 100 * (total - delta[3] - delta[4]) / total if total else None,
                     "softirq_percent": 100 * delta[6] / total if total else None}
    softirqs = {}
    for name, initial in (a.get("softirqs") or {}).items():
        final = (b.get("softirqs") or {}).get(name)
        if isinstance(initial, list) and isinstance(final, list) and len(initial) == len(final):
            values = [y - x for x, y in zip(initial, final)]
            softirqs[name] = values if all(value >= 0 for value in values) else None
    temps = [row.get("host", {}).get("temperature_c") for row in samples]
    temps = [value for value in temps if number(value)]
    return {"available": bool(cpu or softirqs),
            "start_monotonic_s": sample_time(first), "end_monotonic_s": sample_time(last),
            "cpu": cpu, "softirq_count_deltas_by_cpu": softirqs,
            "temperature_min_c": min(temps) if temps else None,
            "temperature_max_c": max(temps) if temps else None,
            "first": a, "last": b}


KERNEL_TIME = re.compile(r"^\s*\[\s*(\d+(?:\.\d+)?)\]")
KERNEL_MISSED = re.compile(r"init programmed write\.\s*missed:\s*(\d+)")


def kernel_summary(rows, first_time, last_time, run):
    samples = []
    for row in rows:
        line = str(row.get("line", ""))
        match = KERNEL_MISSED.search(line)
        if not match:
            continue
        stamp = KERNEL_TIME.match(line)
        # Kernel time, not reader reception time, defines attribution.
        when = float(stamp.group(1)) if stamp else row.get("kernel_monotonic_s")
        if not number(when) or not first_time <= when <= last_time:
            continue
        if row.get("run") is not None and row.get("run") != run:
            continue
        samples.append((when, int(match.group(1))))
    samples.sort()
    if len(samples) < 2:
        return {"available": False, "reason": "fewer than two timestamped TX missed-counter samples inside this stable interval",
                "samples": len(samples)}
    if any(tb <= ta or cb < ca for (ta, ca), (tb, cb) in zip(samples, samples[1:])):
        return {"available": False, "reason": "kernel counter reset or ambiguous timestamp within stable interval",
                "samples": len(samples), "counter_samples": samples}
    elapsed = samples[-1][0] - samples[0][0]
    delta = samples[-1][1] - samples[0][1]
    return {"available": True, "samples": len(samples), "counter_delta": delta,
            "first_counter": samples[0][1], "last_counter": samples[-1][1],
            "start_monotonic_s": samples[0][0], "end_monotonic_s": samples[-1][0],
            "elapsed_s": elapsed, "missed_quarters_per_s": delta / elapsed,
            "meaning": "kernel repeat/pad events in this counter-sampling interval; not a duration of lost audio"}


def resource_summary(resources, run, first_time, last_time, hz):
    selected = [row for row in resources if row.get("run") == run
                and row.get("phase") == "stable" and sample_time(row) is not None
                and first_time <= sample_time(row) <= last_time]
    return {
        "cpu_semantics": "100% equals one fully used core; a multithreaded process may exceed 100%",
        "clock_ticks_per_second": hz, "resource_samples": len(selected),
        "processes": proc_intervals(selected, process_selector, hz),
        "threads": proc_intervals(selected, thread_selector, hz), "host": host_summary(selected),
    }


def summarize_segment(rows, reason, resources, kernel, hz, window_seconds):
    first, last = rows[0], rows[-1]
    report = interval(rows)
    report.update({"boundary_reason": reason, "session": first.get("session"),
                   "generation": first.get("tx", {}).get("generation"), "run": first.get("run"),
                   "owner_pid": first.get("owner_pid"), "config": first.get("config"),
                   "windows": windows(rows, window_seconds)})
    worker_history = {}
    for row in rows:
        for role, data in (row.get("tx", {}).get("workers") or {}).items():
            history = worker_history.setdefault(role, [])
            if not history or history[-1]["status"] != data:
                history.append({"monotonic_s": status_time(row), "status": data})
    report["worker_status_changes"] = worker_history
    report["resources"] = resource_summary(resources, first.get("run"), status_time(first), status_time(last), hz)
    report["kernel_tx_missed"] = kernel_summary(kernel, status_time(first), status_time(last), first.get("run"))
    for window in report["windows"]:
        window["resources"] = resource_summary(resources, first.get("run"), window["start_monotonic_s"], window["end_monotonic_s"], hz)
        window["kernel_tx_missed"] = kernel_summary(kernel, window["start_monotonic_s"], window["end_monotonic_s"], first.get("run"))
    return report


def fmt(value, precision=2):
    return f"{value:.{precision}f}" if number(value) else "unavailable"


def count_fmt(value):
    return str(value) if number(value) else "unavailable"


def bool_fmt(value):
    return "yes" if value is True else "no" if value is False else "unavailable"


def terminal_status(rows):
    """Retain faults during shutdown as well as the final valid clock counter."""
    if not rows:
        return {"available": False, "aborted": False, "recorded_faults": [],
                "clock_limit_faults": {"available": False, "final_total": None,
                                       "delta_since_first_stable": None}}

    def details(row):
        tx = row.get("tx") or {}
        workers = tx.get("workers") or {}
        return {"monotonic_s": status_time(row), "phase": row.get("phase"),
                "state": row.get("state"), "control_state": (row.get("control") or {}).get("state"),
                "tx_generation": tx.get("generation"), "tx_fault": tx.get("fault"),
                "producer_last_error": (workers.get("producer") or {}).get("last_error"),
                "writer_last_error": (workers.get("writer") or {}).get("last_error"),
                "capture_worker_last_error": (workers.get("capture") or {}).get("last_error"),
                "capture_last_error": (tx.get("capture") or {}).get("last_error"),
                "audio_buffer_last_error": optional_block(row, "audio_buffer").get("last_error")}

    first_stable = next((row for row in rows if stable(row)), None)
    first_stable_index = rows.index(first_stable) if first_stable else len(rows)
    faults = []
    for index, row in enumerate(rows):
        value = details(row)
        # A retained previous pipeline may be visible while configuring the
        # next run; do not attribute its old fault to a new stable generation.
        if first_stable and index < first_stable_index and (row.get("session"), value["tx_generation"]) != (
                first_stable.get("session"), first_stable.get("tx", {}).get("generation")):
            continue
        if value["state"] == "FAULT" or value["control_state"] == "FAULT" or value["tx_fault"]:
            signature = {key: item for key, item in value.items() if key != "monotonic_s"}
            if not faults or signature != {key: item for key, item in faults[-1].items() if key != "monotonic_s"}:
                faults.append(value)

    path = COUNTERS["audio_buffer_clock_limit_faults"]
    final_audio = next((row for row in reversed(rows) if counter(row, path) is not None), None)
    final_total = counter(final_audio, path) if final_audio else None
    baseline_total = counter(first_stable, path) if first_stable else None
    delta = None
    if first_stable and final_audio and baseline_total is not None:
        start, end = rows.index(first_stable), rows.index(final_audio)
        endpoints = rows[start:end+1] if end >= start else []
        same_owner = all((row.get("session"), row.get("tx", {}).get("generation")) == (
            first_stable.get("session"), first_stable.get("tx", {}).get("generation")) for row in endpoints)
        if same_owner:
            values = [counter(row, path) for row in endpoints]
            if values and all(value is not None for value in values) and all(
                    b >= a for a, b in zip(values, values[1:])):
                delta = final_total - baseline_total
    error_fields = ("producer_last_error", "writer_last_error", "capture_worker_last_error",
                    "capture_last_error", "audio_buffer_last_error")
    recorded_errors = {}
    for key in error_fields:
        observed = [fault[key] for fault in reversed(faults) if number(fault[key])]
        recorded_errors[key] = next((value for value in observed if value != 0), observed[0] if observed else None)
    return {"available": True, "aborted": bool(faults), "last_record": details(rows[-1]),
            "recorded_faults": faults,
            "recorded_errors": recorded_errors,
            "clock_limit_faults": {"available": final_total is not None,
                                   "first_stable_total": baseline_total,
                                   "final_total": final_total, "delta_since_first_stable": delta,
                                   "final_monotonic_s": status_time(final_audio) if final_audio else None,
                                   "scope": "first stable snapshot through final valid audio status, including shutdown; generation changes, resets or missing counters invalidate the delta"}}


def hardware_markdown(segment):
    lines = []
    driver = segment.get("driver")
    audio = segment.get("audio_buffer")
    if driver is not None:
        rates, deltas = driver["rates"], driver["counter_deltas"]
        lines += ["", "#### TX DMA transport", "",
                  f"Hardware paced: {bool_fmt(segment.get('hardware_paced'))}. "
                  f"Driver status is valid in {driver['observed_status_samples']} of "
                  f"{segment['status_samples']} snapshots. A missing observation makes counter deltas "
                  "and rates unavailable for this interval.", "",
                  "| Driver measurement | Value |", "| --- | --- |",
                  f"| Accepted bytes/s | {fmt(rates.get('accepted_bytes_per_s'))} |",
                  f"| FIFO dequeued bytes/s | {fmt(rates.get('fifo_dequeued_bytes_per_s'))} |",
                  f"| Completed DMA periods/s | {fmt(rates.get('completed_periods_per_s'))} |",
                  f"| Underrun periods delta | {count_fmt(deltas.get('underrun_periods'))} |",
                  f"| FIFO queued bytes start / end | {count_fmt(driver['fifo_queued_bytes_start'])} / {count_fmt(driver['fifo_queued_bytes_end'])} |",
                  f"| Observed FIFO queued bytes min / max | {count_fmt(driver['fifo_queued_bytes_observed_min'])} / {count_fmt(driver['fifo_queued_bytes_observed_max'])} |",
                  f"| FIFO limit / capacity, bytes at end | {count_fmt(driver['fifo_limit_bytes_end'])} / {count_fmt(driver['fifo_capacity_bytes_end'])} |",
                  f"| DMA period / ring, bytes at end | {count_fmt(driver['period_bytes_end'])} / {count_fmt(driver['ring_bytes_end'])} |", "",
                  driver["meaning"] + ".", "",
                  "| Window elapsed, s | Accepted / dequeued bytes/s | Completed DMA periods/s | Underrun periods delta |",
                  "| --- | --- | --- | --- |"]
        for window in segment["windows"]:
            d = window.get("driver", {})
            r, c = d.get("rates", {}), d.get("counter_deltas", {})
            lines.append(f"| {fmt(window['elapsed_s'])} "
                         f"| {fmt(r.get('accepted_bytes_per_s'))} / {fmt(r.get('fifo_dequeued_bytes_per_s'))} "
                         f"| {fmt(r.get('completed_periods_per_s'))} | {count_fmt(c.get('underrun_periods'))} |")
    if audio is not None:
        deltas = audio["counter_deltas"]
        lines += ["", "#### TX audio clock matching", "",
                  f"Audio buffer status is valid in {audio['observed_status_samples']} of "
                  f"{segment['status_samples']} snapshots. Correction mean uses "
                  f"{audio['correction_status_samples']} valid saved observations; "
                  "queue extrema also describe observed snapshots.", "",
                  "| Audio buffer measurement | Value |", "| --- | --- |",
                  f"| Queued audio frames start / end | {count_fmt(audio['queued_frames_start'])} / {count_fmt(audio['queued_frames_end'])} |",
                  f"| Observed queued audio frames min / max | {count_fmt(audio['queued_frames_observed_min'])} / {count_fmt(audio['queued_frames_observed_max'])} |",
                  f"| Target / capacity, audio frames at end | {count_fmt(audio['target_frames_end'])} / {count_fmt(audio['capacity_frames_end'])} |",
                  f"| Correction mean / end, ppm | {fmt(audio['correction_ppm_mean'])} / {fmt(audio['correction_ppm_end'])} |",
                  f"| Observed correction min / max, ppm | {fmt(audio['correction_ppm_min'])} / {fmt(audio['correction_ppm_max'])} |",
                  f"| Clock valid at end / all samples | {bool_fmt(audio['clock_valid_end'])} / {bool_fmt(audio['clock_valid_all_samples'])} |",
                  f"| Underflows / overflows delta | {count_fmt(deltas.get('underflows'))} / {count_fmt(deltas.get('overflows'))} |",
                  f"| Overflow / cue-discard audio frames delta | {count_fmt(deltas.get('overflow_frames'))} / {count_fmt(deltas.get('cue_discard_frames'))} |",
                  f"| Clock-limit audio frames / faults delta | {count_fmt(deltas.get('clock_limit_frames'))} / {count_fmt(deltas.get('clock_limit_faults'))} |",
                  f"| Audio buffer last error at end | {count_fmt(audio['last_error_end'])} |", "",
                  "| Window elapsed, s | Observed audio queue min / max, frames | Correction mean / end, ppm | Underflows / overflows delta | Clock-limit faults delta |",
                  "| --- | --- | --- | --- | --- |"]
        for window in segment["windows"]:
            a = window.get("audio_buffer", {})
            c = a.get("counter_deltas", {})
            lines.append(f"| {fmt(window['elapsed_s'])} "
                         f"| {count_fmt(a.get('queued_frames_observed_min'))} / {count_fmt(a.get('queued_frames_observed_max'))} "
                         f"| {fmt(a.get('correction_ppm_mean'))} / {fmt(a.get('correction_ppm_end'))} "
                         f"| {count_fmt(c.get('underflows'))} / {count_fmt(c.get('overflows'))} "
                         f"| {count_fmt(c.get('clock_limit_faults'))} |")
    return lines


def markdown_report(summary):
    metadata = summary.get("metadata", {})
    lines = ["# Timed TX baseline", "",
             "No runtime, scheduling, or driver settings are changed by this report.", "",
             "Rates are measured from counter differences and actual monotonic elapsed time. "
             "Writer samples mean IQ accepted by the API; they do not measure RF timing. "
             "A generation, session, owner, configuration change or decreasing counter starts a separate interval.", "",
             f"Requested test frequency: {summary['requested_frequency_hz']} Hz ({summary['requested_frequency_hz'] / 1e6:g} MHz). "
             "The run tables below show the backend's requested and actual configuration.", "",
             f"Harness result: `{json.dumps(metadata.get('result'), sort_keys=True)}`", "",
             f"Rate window target: {summary['window_seconds']:g} seconds. "
             "Window boundaries use received status samples, so durations can differ slightly; the final window can be shorter.", ""]
    if not summary["runs"]:
        lines += ["No run records or planned run list were available.", ""]
    for run in summary["runs"]:
        lines += [f"## {run['run']}", ""]
        terminal = run.get("terminal", {})
        if terminal.get("aborted"):
            fault = terminal["recorded_faults"][-1]
            clock = terminal["clock_limit_faults"]
            errors = terminal["recorded_errors"]
            lines += [f"**Run aborted:** recorded TX fault `{fault['tx_fault']}`, "
                      f"producer error `{errors['producer_last_error']}`, "
                      f"capture worker error `{errors['capture_worker_last_error']}`, "
                      f"ALSA capture error `{errors['capture_last_error']}`. "
                      f"Final clock-limit faults total / delta since first stable snapshot: "
                      f"**{count_fmt(clock['final_total'])} / {count_fmt(clock['delta_since_first_stable'])}**. "
                      f"Fault observed in `{fault['state']}` during `{fault['phase']}`. "
                      "Stable-interval tables below exclude the terminal fault transition.", ""]
        if not run["stable_segments"]:
            lines += ["No measurable live stable TX interval. All rates, CPU measurements and counter deltas are unavailable. "
                      "Dry-run or setup records establish command sequencing only.", ""]
            continue
        for index, segment in enumerate(run["stable_segments"], 1):
            config = segment.get("config") or {}
            lines += [f"### Independent interval {index}", "",
                      f"Session `{segment['session']}`, TX generation `{segment['generation']}`; "
                      f"{fmt(segment['elapsed_s'])} s, {segment['status_samples']} status samples. "
                      f"Boundary: {segment['boundary_reason']}.", "",
                      f"Source `{config.get('tx_source')}`, requested IQ rate `{config.get('tx_sample_rate_hz')}` samples/s; "
                      f"frequency requested `{config.get('tx_frequency_hz')}`, actual `{config.get('tx_actual_frequency_hz')}` Hz; "
                      f"power requested `{config.get('tx_power_dbm')}`, actual `{config.get('tx_actual_power_dbm')}` dBm.", "",
                      "| Measurement | Value |", "| --- | --- |"]
            for label, value in segment["rates"].items():
                lines.append(f"| {label.replace('_', ' ')} | {fmt(value)} |")
            q = segment["queue"]
            lines += [f"| Queue depth start / end / capacity | {q['start_depth']} / {q['end_depth']} / {q['capacity']} |",
                      f"| Observed depth min / max | {q['observed_min_depth']} / {q['observed_max_depth']} |",
                      f"| Reported lifetime depth min / max | {q['reported_lifetime_min_depth']} / {q['reported_lifetime_max_depth']} |",
                      f"| Queue growth, frames/s | {fmt(q['growth_frames_per_s'], 4)} |",
                      f"| First observed capacity, seconds after interval start | {fmt(q['first_capacity_observed_after_start_s'])} |",
                      f"| Linear estimated seconds to capacity from interval end | {fmt(q['linear_estimated_seconds_to_capacity_from_end'])} |", "",
                      "Queue extrema reported by the backend include the pipeline lifetime; observed extrema use only these saved snapshots. "
                      "An extrapolated time to capacity is an estimate based on endpoint growth, not an observed saturation event.", "",
                      "| Counter | Measured delta |", "| --- | --- |"]
            for label, value in segment["counter_deltas"].items():
                lines.append(f"| {label.replace('_', ' ')} | {value if value is not None else 'unavailable'} |")
            lines += ["", "| Window elapsed, s | Put / get, frames/s | Accepted IQ samples/s | Capture frames/s | Capture overruns |",
                      "| --- | --- | --- | --- | --- |"]
            for window in segment["windows"]:
                r = window["rates"]
                lines.append(f"| {fmt(window['elapsed_s'])}{' (partial)' if window['partial_window'] else ''} "
                             f"| {fmt(r['queue_put_frames_per_s'])} / {fmt(r['queue_get_frames_per_s'])} "
                             f"| {fmt(r['writer_accepted_iq_samples_per_s'], 0)} "
                             f"| {fmt(r['capture_audio_frames_per_s'])} | {window['counter_deltas']['capture_overruns']} |")
            lines += hardware_markdown(segment)
            lines += ["", "CPU is expressed per core: 100% means one fully used core; process totals may exceed 100%.", "",
                      "| Process / thread | CPU, % of one core | Sampled elapsed, s | Voluntary / involuntary context switches |",
                      "| --- | --- | --- | --- |"]
            for group in ("processes", "threads"):
                for item in segment["resources"][group]["intervals"]:
                    lines.append(f"| {item['role']} ({item['pid_or_tid']}) "
                                 f"| {fmt(item['cpu_percent_of_one_core'])} | {fmt(item['measured_elapsed_s'])} "
                                 f"| {item['voluntary_context_switches_delta']} / {item['nonvoluntary_context_switches_delta']} |")
            lines += ["", "Complete scheduling policies, affinities, retained errors, /proc endpoints, host CPU/softirq deltas "
                      "and resource sampling errors are preserved in `summary.json` and the raw artifacts.", ""]
            for role, history in segment["worker_status_changes"].items():
                for entry in history:
                    lines.append(f"- `{role}` status at {fmt(entry['monotonic_s'], 3)} s: "
                                 f"`{json.dumps(entry['status'], sort_keys=True)}`")
            kernel = segment["kernel_tx_missed"]
            if kernel["available"]:
                lines += ["", f"Kernel TX missed counter increased by **{kernel['counter_delta']}** across its own "
                          f"{fmt(kernel['elapsed_s'])} s sampled interval. These are repeat/pad events, not measured lost-audio duration.", ""]
            else:
                lines += ["", f"Kernel TX missed delta unavailable: {kernel['reason']}.", ""]
    if summary["warnings"]:
        lines += ["## Artifact read warnings", ""] + [f"- {warning}" for warning in summary["warnings"]] + [""]
    lines += ["Raw artifacts and `metadata.json` remain the source of provenance. "
              "The analysis cannot establish audible quality or actual RF output continuity.", ""]
    return "\n".join(lines)


def write_plot(directory, runs, frequency_hz):
    if not any(run["stable_segments"] for run in runs):
        return {"available": False, "reason": "no live stable TX data"}
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return {"available": False, "reason": "optional matplotlib is not installed"}
    figure, axes = plt.subplots(3, 1, figsize=(10, 9), constrained_layout=True)
    for run in runs:
        for index, segment in enumerate(run["stable_segments"], 1):
            label = run["run"] + (f" interval {index}" if len(run["stable_segments"]) > 1 else "")
            points = segment["plot_samples"]
            x = [point["elapsed_s"] for point in points]
            axes[0].plot(x, [point["queue_depth"] for point in points], label=label)
            axes[2].plot(x, [point["capture_overruns"] for point in points], label=label)
            wx, wy = [], []
            for window in segment["windows"]:
                wx.append((window["start_monotonic_s"] + window["end_monotonic_s"]) / 2 - segment["start_monotonic_s"])
                value = window["rates"]["writer_accepted_iq_samples_per_s"]
                wy.append(value / 1e6 if value is not None else float("nan"))
            axes[1].plot(wx, wy, "o-", label=label)
    for axis, ylabel in zip(axes, ("Application queue depth (frames)", "Accepted IQ rate (million samples/s)", "Capture overrun count delta")):
        axis.set_ylabel(ylabel)
        axis.set_xlabel("Seconds since first stable TX snapshot")
        axis.grid(True, alpha=0.3)
        axis.legend(fontsize="small")
    figure.suptitle(f"TX baseline at {frequency_hz / 1e6:g} MHz — independent stable intervals")
    path = directory / "tx-baseline.svg"
    figure.savefig(path, format="svg")
    plt.close(figure)
    return {"available": True, "path": path.name}


def generate_report(directory, window_seconds=30.0, make_plot=True):
    directory = Path(directory)
    if not number(window_seconds) or window_seconds <= 0:
        raise ValueError("window_seconds must be positive")
    warnings = []
    try:
        metadata = json.loads((directory / "metadata.json").read_text())
        if not isinstance(metadata, dict):
            raise ValueError("metadata is not an object")
    except (OSError, ValueError) as exc:
        metadata = {}
        warnings.append(f"metadata.json: {exc}")
    status = read_jsonl(directory / "status.jsonl", warnings)
    resources = read_jsonl(directory / "resources.jsonl", warnings)
    kernel = read_jsonl(directory / "kernel.jsonl", warnings)
    if not kernel:
        seen = set()
        paths = [directory / "kernel.log", directory / "dmesg.log"] + sorted(directory.glob("dmesg-*.log"))
        for path in paths:
            if path.exists():
                for line in path.read_text(errors="replace").splitlines():
                    # Whole-ring dmesg snapshots overlap. Keep a line only once.
                    if line not in seen:
                        seen.add(line)
                        kernel.append({"line": line})
    planned = [item["run"] for item in metadata.get("runlist", []) if isinstance(item, dict) and "run" in item]
    names = list(dict.fromkeys(planned + [row["run"] for row in status if row.get("run") is not None]))
    if any(stable(row) and row.get("run") is None for row in status):
        names.append("unlabelled")
    runs = [{"run": name, "stable_segments": [],
             "terminal": terminal_status([row for row in status if (row.get("run") or "unlabelled") == name])}
            for name in names]
    by_name = {run["run"]: run for run in runs}
    for rows, reason in split_segments(status):
        name = rows[0].get("run") or "unlabelled"
        segment = summarize_segment(rows, reason, resources, kernel,
                                    metadata.get("clock_ticks_per_second"), window_seconds)
        initial_overruns = counter(rows[0], COUNTERS["capture_overruns"])
        segment["plot_samples"] = [{
            "elapsed_s": status_time(row) - status_time(rows[0]),
            "queue_depth": (row.get("tx", {}).get("queue") or {}).get("depth"),
            "capture_overruns": (counter(row, COUNTERS["capture_overruns"]) - initial_overruns
                                if counter(row, COUNTERS["capture_overruns"]) is not None and initial_overruns is not None else None),
        } for row in rows]
        by_name[name]["stable_segments"].append(segment)
    frequency_hz = metadata.get("options", {}).get("frequency", 430100000)
    if not number(frequency_hz):
        frequency_hz = 430100000
    summary = {"schema_version": 1, "requested_frequency_hz": frequency_hz,
               "window_seconds": window_seconds, "metadata": metadata,
               "status_records": len(status), "resource_records": len(resources),
               "runs": runs, "warnings": warnings,
               "interpretation": "counter deltas are isolated to each live stable TX session/generation/configuration; missing metrics remain null"}
    summary["plot"] = write_plot(directory, runs, frequency_hz) if make_plot else {"available": False, "reason": "plot disabled"}
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "summary.json").write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n")
    markdown = markdown_report(summary)
    if summary["plot"].get("available"):
        markdown += "\n![TX queue, accepted IQ rate and capture overruns](tx-baseline.svg)\n"
    (directory / "README.md").write_text(markdown)
    return summary


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="timed baseline artifact directory")
    parser.add_argument("--window-seconds", type=float, default=30.0)
    parser.add_argument("--no-plot", action="store_true", help="skip optional matplotlib SVG")
    args = parser.parse_args(argv)
    try:
        result = generate_report(args.directory, args.window_seconds, not args.no_plot)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(f"Report: {args.directory / 'README.md'}; {sum(len(run['stable_segments']) for run in result['runs'])} independent stable TX intervals")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
