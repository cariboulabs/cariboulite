#!/usr/bin/env python3
"""Verify full-quadrant NBFM angle accuracy and public receiver recovery."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
src = here.parent / 'src'
with tempfile.TemporaryDirectory(prefix='fm-discriminator-') as directory:
    for policy, flags in [('strict', []),
                          ('production', ['-ffast-math', '-fno-math-errno', '-funroll-loops'])]:
        print('FM discriminator compiler policy:', policy, flush=True)
        binary = Path(directory) / policy
        subprocess.run(['cc', '-std=c11', '-O3', *flags, '-Wall', '-Wextra', '-Werror',
                        '-I'+str(src), str(here/'test_fm_discriminator.c'),
                        str(src/'nbfm_demod.c'), str(src/'nbfm_demod_dsp.c'),
                        str(src/'wbfm_demod.c'), '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=60)
