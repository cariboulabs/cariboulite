#!/usr/bin/env python3
"""Hardware-free detector and real RX worker gating checks."""
from pathlib import Path
import subprocess
import tempfile
here = Path(__file__).resolve().parent
src = here.parent / 'src'
with tempfile.TemporaryDirectory(prefix='squelch-') as directory:
    modules = ['noise_squelch.c', 'carrier_squelch.c', 'nbfm_mod.c',
               'nbfm_demod.c', 'nbfm_demod_dsp.c', 'wbfm_demod.c', 'demod_worker.c']
    for label, flags in [('strict', ['-O2']), ('production',
            ['-O3', '-ffast-math', '-fno-math-errno', '-funroll-loops'])]:
        binary = Path(directory) / label
        subprocess.run(['cc', '-std=c11', *flags, '-Wall', '-Wextra', '-Werror',
                        '-I'+str(src), str(here/'test_squelch.c'),
                        *[str(src/m) for m in modules], '-lm', '-pthread',
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=90)
