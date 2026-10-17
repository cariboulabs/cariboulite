#!/usr/bin/env python3
"""Verify NBFM resampling against independent absolute output sample times."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
src = here.parent / 'src'
with tempfile.TemporaryDirectory(prefix='nbfm-interpolation-') as directory:
    for policy, flags in [('strict', []),
                          ('production', ['-ffast-math', '-fno-math-errno', '-funroll-loops'])]:
        print('NBFM interpolation compiler policy:', policy, flush=True)
        binary = Path(directory) / policy
        subprocess.run(['cc', '-std=c11', '-O3', *flags, '-Wall', '-Wextra', '-Werror',
                        '-I'+str(src), str(here/'test_nbfm_interpolation.c'),
                        str(src/'nbfm_demod.c'), str(src/'nbfm_demod_dsp.c'),
                        str(src/'wbfm_demod.c'), '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=120)
