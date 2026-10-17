#!/usr/bin/env python3
"""Exercise the actual monitor controller with mocked register/SMI services."""
from pathlib import Path
import subprocess
import tempfile
src = Path(__file__).resolve().parents[1] / 'src'
with tempfile.TemporaryDirectory(prefix='monitor-loopback-') as tmp:
    exe = str(Path(tmp) / 'test')
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-parameter',
                    '-I'+str(src), '-I'+str(src/'at86rf215'), '-I'+str(src/'rffc507x'),
                    str(Path(__file__).with_suffix('.c')), '-o', exe], check=True)
    subprocess.run([exe], check=True, timeout=15)
