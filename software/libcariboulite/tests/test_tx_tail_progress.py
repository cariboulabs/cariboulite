#!/usr/bin/env python3
"""Exercise actual TX progress and TX-to-RX handoff with mocked FIFO/SMI calls."""
from pathlib import Path
import re
import subprocess
import tempfile

here = Path(__file__).resolve().parent
src = here.parent / 'src'
source = (src / 'tx_pipeline.c').read_text()
# Preserve indices while masking comments/literals when finding the function body.
masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                lambda match: ' ' * len(match.group()), source, flags=re.S)
start = masked.index('static void* tx_writer_thread_func(')
opening = masked.index('{', start)
depth = 0
for end in range(opening, len(masked)):
    depth += (masked[end] == '{') - (masked[end] == '}')
    if depth == 0:
        break
else:
    raise AssertionError('TX writer function body is incomplete')

with tempfile.TemporaryDirectory(prefix='tx-tail-progress-') as directory:
    directory = Path(directory)
    # The writer now shares the production stopped-worker gate and cleanup.
    helpers = source.index('static void gate_unlock(')
    (directory / 'tx_writer_thread.inc').write_text(source[helpers:end + 1])
    binary = directory / 'test'
    subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    *['-I' + str(path) for path in
                      (src, src / 'at86rf215', src / 'rffc507x', directory)],
                    str(here / 'test_tx_tail_progress.c'), str(src / 'mod_worker.c'),
                    str(src / 'nbfm_mod.c'), str(src / 'tone_source.c'),
                    str(src / 'tx_audio_buffer.c'), str(src / 'alsa_source.c'),
                    '-Wl,--wrap=clock_nanosleep', '-Wl,--wrap=poll', '-Wl,--wrap=nanosleep',
                    '-Wl,--wrap=tone_source_set', '-lasound', '-lm', '-pthread', '-o', str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
