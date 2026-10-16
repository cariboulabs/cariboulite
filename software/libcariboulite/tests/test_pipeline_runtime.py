#!/usr/bin/env python3
"""Check shared scheduling diagnostics with deterministic failures, without RT privileges."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
src = here.parent / "src"
wraps = ["pthread_setschedparam", "pthread_setaffinity_np", "pthread_getschedparam",
         "pthread_getaffinity_np", "mlockall"]
with tempfile.TemporaryDirectory(prefix="pipeline-runtime-") as directory:
    binary = Path(directory) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I" + str(src),
                    str(here / "test_pipeline_runtime.c"), str(src / "pipeline_runtime.c"),
                    *["-Wl,--wrap=" + symbol for symbol in wraps],
                    "-pthread", "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    assert "actual=SCHED_OTHER/0" in result.stderr
    assert "rt_error=1" in result.stderr and "mlock=skipped" in result.stderr
    print(result.stdout, end="")
