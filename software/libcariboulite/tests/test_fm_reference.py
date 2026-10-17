#!/usr/bin/env python3
"""Check exact WBFM frozen PCM and filtered NBFM streaming/API invariants."""
from pathlib import Path
import subprocess
import tempfile
here = Path(__file__).resolve().parent
src = here.parent / 'src'
with tempfile.TemporaryDirectory(prefix='fm-reference-') as directory:
    for policy, flags in [('strict', []),
                          ('production', ['-ffast-math', '-fno-math-errno', '-funroll-loops'])]:
        print('FM reference compiler policy:', policy, flush=True)
        binary = Path(directory) / policy
        subprocess.run(['cc', '-O3', *flags, '-Wall', '-Wextra', '-I'+str(src),
                        str(here/'test_fm_reference.c'), str(src/'nbfm_demod.c'), str(src/'nbfm_demod_dsp.c'), str(src/'wbfm_demod.c'),
                        str(here/'fixtures/legacy_wbfm_demod/oracle.c'),
                        '-Wl,--wrap=calloc', '-Wl,--wrap=free', '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=60)
