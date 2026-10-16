#!/usr/bin/env python3
"""Verify the driver's actual quiet word against the unchanged FPGA collector."""
from pathlib import Path
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=(root/'driver/smi_stream_dev.c').read_text()
body=source[source.index('static void smi_tx_fill_quiet('):]
word=re.search(r'words\[n\] = (0x[0-9a-fA-F]+)U;',body).group(1)
with tempfile.TemporaryDirectory(prefix='tx-quiet-rtl-') as directory:
    executable=str(Path(directory)/'sim')
    subprocess.run(['iverilog','-g2012','-s','tb','-o',executable,
                    str(root/'driver/tests/tb_tx_quiet_framing.v'),
                    str(root/'firmware/smi_ctrl.v')],check=True)
    for raw_zero in [0,1]:
        subprocess.run(['vvp',executable,'+quiet_word='+word[2:],'+raw_zero='+str(raw_zero)],
                       check=True,timeout=10)
