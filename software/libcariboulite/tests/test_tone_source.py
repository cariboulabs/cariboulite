#!/usr/bin/env python3
"""Compare the new tone source with the previous app formulas, without hardware."""
from pathlib import Path
import subprocess
import tempfile
here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='tone-source-') as directory:
    binary = Path(directory) / 'test'
    subprocess.run(['cc', '-Wall', '-Wextra', '-I'+str(here.parent/'src'),
                    str(here/'test_tone_source.c'), str(here.parent/'src/tone_source.c'),
                    '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
