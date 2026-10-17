#!/usr/bin/env python3
"""Compare legacy NBFM angle and guarded atan2f, without radio hardware.

The source at --baseline-ref (or --baseline-source) must contain the old
fast_atan2f_small helper. Both backends use that same channel/audio code; the
atan2f backend changes only the angle helper. This isolates the angle cost.
Input generation, warm-up, validation, and output handling are untimed.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import statistics
import subprocess
import tempfile


PRODUCTION_FLAGS = [
    "-O3", "-ffast-math", "-fno-math-errno", "-funroll-loops", "-DNDEBUG",
    "-fPIE", "-D_GNU_SOURCE", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra",
]
SOURCE_PATH = "software/libcariboulite/src/nbfm_demod_dsp.c"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-ref", default="d288d4c", help="Git revision of the legacy angle backend")
    parser.add_argument("--baseline-source", type=Path, help="Use a saved legacy source instead of Git")
    parser.add_argument("--rounds", type=int, default=7)
    parser.add_argument("--blocks", type=int, default=100, help="10 ms blocks per round and policy")
    parser.add_argument("--cpu", type=int, help="Pin benchmark to this allowed logical CPU")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--output", type=Path, help="Save the complete JSON result")
    args = parser.parse_args()
    if args.rounds < 3 or args.blocks < 100:
        parser.error("Use at least three rounds and 100 blocks per round")
    here = Path(__file__).resolve().parent
    src = here.parent / "src"
    repo = here.parents[2]
    if args.baseline_source:
        baseline = args.baseline_source.read_text()
        baseline_label = str(args.baseline_source.resolve())
    else:
        baseline = subprocess.check_output(
            ["git", "show", f"{args.baseline_ref}:{SOURCE_PATH}"], cwd=repo, text=True)
        baseline_label = subprocess.check_output(
            ["git", "rev-parse", args.baseline_ref], cwd=repo, text=True).strip()
    pattern = r"static inline float fast_atan2f_small\(float y, float x\)\n\{.*?^\}"
    replacement = """static inline float fast_atan2f_small(float y, float x)
{
    // Zero conjugate product has no phase information.
    return (x == 0.0f && y == 0.0f) ? 0.0f : atan2f(y, x);
}"""
    full, replacements = re.subn(pattern, replacement, baseline, flags=re.MULTILINE | re.DOTALL)
    if replacements != 1:
        parser.error("Baseline must contain exactly one legacy fast_atan2f_small helper")
    cc = shlex.split(args.cc)
    with tempfile.TemporaryDirectory(prefix="nbfm-discriminator-benchmark-") as directory:
        tmp = Path(directory)
        objects = []
        for name, source in (("legacy", baseline), ("atan2f", full)):
            source_path = tmp / f"{name}.c"
            source_path.write_text(source)
            object_path = tmp / f"{name}.o"
            renamed = [f"-D{symbol}={name}_{symbol}" for symbol in (
                "nb_demod_create", "nb_demod_reset", "nb_demod_set_audio",
                "nb_demod_process_with_raw")]
            subprocess.run(cc + PRODUCTION_FLAGS + ["-I" + str(src)] + renamed +
                           ["-c", str(source_path), "-o", str(object_path)], check=True)
            objects.append(str(object_path))
        binary = tmp / "benchmark"
        subprocess.run(cc + PRODUCTION_FLAGS + ["-I" + str(src),
                       str(here / "benchmark_nbfm_discriminator.c")] + objects +
                       ["-lm", "-o", str(binary)], check=True)
        previous_affinity = None
        if args.cpu is not None:
            previous_affinity = os.sched_getaffinity(0)
            if args.cpu not in previous_affinity:
                parser.error(f"CPU {args.cpu} is outside the allowed set {sorted(previous_affinity)}")
            os.sched_setaffinity(0, {args.cpu})
        try:
            measured = json.loads(subprocess.check_output(
                [str(binary), str(args.rounds), str(args.blocks)], text=True))
        finally:
            if previous_affinity is not None:
                os.sched_setaffinity(0, previous_affinity)
    for case in measured["cases"]:
        for policy in ("legacy", "atan2f"):
            summary = case[policy]
            median = statistics.median(summary["round_seconds_per_block"])
            summary["median_ms_per_10ms_block"] = median * 1000
            summary["one_core_percent"] = median / 0.01 * 100
        case["added_one_core_percentage_points"] = (
            case["atan2f"]["one_core_percent"] - case["legacy"]["one_core_percent"])
        paired_delta = statistics.median([
            full - old for full, old in zip(
                case["atan2f"]["round_seconds_per_block"],
                case["legacy"]["round_seconds_per_block"])
        ])
        case["paired_median_added_us_per_10ms_block"] = paired_delta * 1e6
        case["paired_median_added_one_core_percentage_points"] = paired_delta / 0.01 * 100
    model_path = Path("/proc/device-tree/model")
    model = model_path.read_bytes().rstrip(b"\0").decode() if model_path.exists() else platform.processor()
    frequency_path = Path(f"/sys/devices/system/cpu/cpu{args.cpu or 0}/cpufreq")
    frequency = {}
    for name in ("scaling_governor", "scaling_cur_freq", "scaling_min_freq", "scaling_max_freq"):
        path = frequency_path / name
        if path.exists():
            frequency[name] = path.read_text().strip()
    dependencies = ["fm_demod_internal.h", "nbfm_demod.h", "audio_demod.h", "fm_audio.h",
                    "fm_discriminator.h", "nbfm_channel_filter.h", "nbfm_channel_coeffs.h",
                    "nbfm_defaults.h", "math_compat.h", "iq16.h", "audio_format.h"]
    result = {
        "measured_at_utc": datetime.now(timezone.utc).isoformat(),
        "machine": model,
        "architecture": platform.machine(),
        "kernel": platform.release(),
        "compiler": subprocess.check_output(cc + ["--version"], text=True).splitlines()[0],
        "compiler_flags": PRODUCTION_FLAGS,
        "cpu_affinity": args.cpu,
        "cpu_frequency_sysfs_after_run_khz": frequency,
        "clock": "CLOCK_THREAD_CPUTIME_ID",
        "baseline": baseline_label,
        "baseline_sha256": hashlib.sha256(baseline.encode()).hexdigest(),
        "atan2f_source_sha256": hashlib.sha256(full.encode()).hexdigest(),
        "shared_header_sha256": {
            name: hashlib.sha256((src / name).read_bytes()).hexdigest() for name in dependencies
        },
        "rounds": args.rounds,
        "blocks_per_round_per_policy": args.blocks,
        "scope": "Only standalone NBFM DSP, including channel filter, limiter, angle, raw tap and audio; excludes input generation, RF transport, queues, squelch, ALSA and other threads. CPU percentages are of one core, not the whole four-core Pi.",
        **measured,
    }
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")


if __name__ == "__main__":
    main()
