#!/usr/bin/env python3
"""Build and test actual audio FIFO accounting with no hardware or ALSA."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
src = here.parent / 'src'
with tempfile.TemporaryDirectory(prefix='audio-fifo-stats-') as directory:
    binary = Path(directory) / 'test'
    subprocess.run([
        'cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O2',
        '-Wall', '-Wextra', '-Werror', '-I' + str(src),
        str(here / 'test_audio_fifo_stats.c'), str(src / 'pipeline_transport.c'),
        '-Wl,--wrap=pthread_cond_wait', '-Wl,--wrap=pthread_cond_timedwait',
        '-pthread', '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, timeout=20)
