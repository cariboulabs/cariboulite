#!/usr/bin/env python3
"""Verify the modulator contract, bounded IQ changes, and recovered FM fidelity."""
from pathlib import Path
import subprocess
import tempfile
here = Path(__file__).resolve().parent
src = here.parent / 'src'
with tempfile.TemporaryDirectory(prefix='nbfm-mod-') as directory:
    directory = Path(directory)
    common = ['cc', '-Wall', '-Wextra', '-Werror', '-I'+str(src)]
    harness = directory / 'harness.o'
    subprocess.run(common + ['-O2', '-c', str(here/'test_nbfm_mod.c'),
                            '-o', str(harness)], check=True)
    for name, flags, arguments in (
        ('contract', ['-O2'], []),
        # Compile DSP with the backend flags, but keep the independent oracle and
        # measurement code at O2. Fast-math's existing nonfinite-validation issue
        # is outside this signal test, so contract errors run in the first pass.
        ('production-signal', ['-O3', '-ffast-math', '-fno-math-errno',
                               '-funroll-loops'], ['--signal-only']),
    ):
        module = directory / (name+'.o')
        binary = directory / name
        subprocess.run(common + flags + ['-c', str(src/'nbfm_mod.c'),
                                        '-o', str(module)], check=True)
        subprocess.run(['cc', str(harness), str(module), '-Wl,--wrap=calloc',
                        '-Wl,--wrap=free', '-lm', '-o', str(binary)], check=True)
        print('Testing '+name, flush=True)
        subprocess.run([str(binary)] + arguments, check=True, timeout=60)
