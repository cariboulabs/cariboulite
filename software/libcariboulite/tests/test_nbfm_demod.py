#!/usr/bin/env python3
"""Check NBFM worker framing, deterministic PCM and streaming equivalence.

The frozen worker remains a queue-framing oracle; its boxcar PCM intentionally
differs from the production complex channel filter.
"""
from pathlib import Path
import subprocess
import tempfile
here = Path(__file__).resolve().parent
src = here.parent / 'src'
with tempfile.TemporaryDirectory(prefix='nbfm-demod-') as directory:
    binary = Path(directory) / 'test'
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-I'+str(src),
                    str(here/'test_nbfm_demod.c'), str(src/'nbfm_demod.c'), str(src/'nbfm_demod_dsp.c'), str(src/'wbfm_demod.c'),
                    str(src/'demod_worker.c'), str(src/'noise_squelch.c'), str(src/'carrier_squelch.c'), '-Wl,--wrap=clock_gettime', '-lm', '-pthread', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=60)
