#!/usr/bin/env python3
"""Compile/run independent capture and clock adaptation without audio or RF."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='tx-audio-buffer-') as directory:
    for name, flags in (
        ('standard', ['-O2']),
        ('backend', ['-O3', '-ffast-math', '-fno-math-errno', '-funroll-loops']),
    ):
        binary = Path(directory) / name
        subprocess.run([
            'cc', '-D_GNU_SOURCE', '-std=c11', *flags, '-Wall', '-Wextra', '-Werror',
            '-I' + str(here.parent/'src'), str(here/'test_tx_audio_buffer.c'),
            '-pthread', '-lm', '-o', str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)
