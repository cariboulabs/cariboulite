#!/usr/bin/env python3
"""Real TX workers/queues; scripted radio and microphone, no RF hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
src = root / 'software/libcariboulite/src'
wraps = [
    'alsa_source_open', 'alsa_source_reset', 'alsa_source_get_stats',
    'cariboulite_radio_set_frequency', 'cariboulite_radio_set_tx_power',
    'cariboulite_radio_set_tx_bandwidth', 'cariboulite_radio_set_tx_samp_cutoff',
    'caribou_fpga_get_sys_ctrl_tx_sample_gap', 'caribou_fpga_set_io_ctrl_mode',
    'cariboulite_radio_activate_channel', 'caribou_smi_set_driver_streaming_state',
    'caribou_smi_get_native_batch_samples', 'caribou_smi_write_samples', 'ioctl',
    'tone_source_set',
    'at86rf215_read_buffer',
]
with tempfile.TemporaryDirectory(prefix='tx-worker-parking-') as directory:
    binary = Path(directory) / 'test'
    subprocess.run([
        'cc', '-D_GNU_SOURCE', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        *['-I' + str(path) for path in (src, src/'at86rf215', src/'rffc507x')],
        str(Path(__file__).with_suffix('.c')),
        *[str(src/name) for name in (
            'radio_profile.c', 'tx_pipeline.c', 'mod_worker.c', 'nbfm_mod.c', 'tone_source.c',
            'pipeline_transport.c', 'pipeline_runtime.c', 'alsa_source.c',
            'tx_audio_buffer.c')],
        *['-Wl,--wrap=' + name for name in wraps],
        '-L' + str(root/'build'), '-Wl,-rpath,' + str(root/'build'),
        '-lcariboulite', '-lasound', '-lm', '-pthread', '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, timeout=50)
