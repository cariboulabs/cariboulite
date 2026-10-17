#!/usr/bin/env python3
"""Exercise playback with ALSA null and scripted failures, without radio hardware."""
from pathlib import Path
import subprocess
import tempfile
here = Path(__file__).resolve().parent
src = here.parent / 'src'
app = (src / 'rx_pipeline.c').read_text()
a = app.index('int write_audio_exact(')
b = app.index('void* audio_writer_thread(', a)
helper = app[a:b].replace('int write_audio_exact(', 'int test_write_exact(')
wraps = ['snd_pcm_open', 'snd_pcm_close', 'snd_pcm_hw_params_set_channels',
         'snd_pcm_sw_params', 'snd_pcm_prepare', 'snd_pcm_writei']
with tempfile.TemporaryDirectory(prefix='audio-sink-') as directory:
    binary = Path(directory) / 'test'
    test = Path(directory) / 'test.c'
    test.write_text((here / 'test_audio_sink.c').read_text() + '\n' + helper)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-I'+str(src),
                    str(test), str(src/'alsa_sink.c'),
                    *['-Wl,--wrap='+s for s in wraps],
                    '-lasound', '-pthread', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
