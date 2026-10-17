#!/usr/bin/env python3
"""Hardware-free mono WBFM signal/streaming tests and local CPU timing."""
from pathlib import Path
import subprocess
import tempfile
here = Path(__file__).resolve().parent
src = here.parent / 'src'
with tempfile.TemporaryDirectory(prefix='wbfm-demod-') as directory:
    binary = Path(directory) / 'test'
    subprocess.run(['cc', '-O3', '-Wall', '-Wextra', '-I'+str(src),
                    str(here/'test_wbfm_demod.c'), str(src/'nbfm_demod.c'), str(src/'nbfm_demod_dsp.c'), str(src/'wbfm_demod.c'),
                    '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=60)
