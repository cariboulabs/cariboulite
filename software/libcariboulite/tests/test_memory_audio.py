#!/usr/bin/env python3
"""Build the adapters and runnable example with only libc/libm; no ALSA or radio."""
from pathlib import Path
import subprocess
import tempfile
here = Path(__file__).resolve().parent
src = here.parent / 'src'
modules = ['memory_audio.c', 'tone_source.c', 'nbfm_mod.c', 'nbfm_demod.c', 'nbfm_demod_dsp.c', 'wbfm_demod.c']
with tempfile.TemporaryDirectory(prefix='memory-audio-') as directory:
    for name, entry, extra in [
        ('test', here/'test_memory_audio.c', ['-Wl,--wrap=calloc', '-Wl,--wrap=free']),
        ('demo', here.parent/'tools/nbfm_memory_demo.c', []),
    ]:
        binary = Path(directory) / name
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(src), str(entry), *[str(src/m) for m in modules],
                        *extra, '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)
