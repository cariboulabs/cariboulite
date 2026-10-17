#!/usr/bin/env python3
"""Compile the actual SPI implementation with mocked GPIO and SPI I/O."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
src=root/'software/libcariboulite/src'
with tempfile.TemporaryDirectory() as d:
    binary=str(Path(d)/'test')
    subprocess.run(['cc','-D_GNU_SOURCE','-I'+str(src),'-pthread',
        str(Path(__file__).with_suffix('.c')),str(src/'io_utils/io_utils_spi.c'),
        '-Wl,--wrap=io_utils_set_gpio_mode','-Wl,--wrap=spi_free','-Wl,--wrap=spi_exchange',
        '-L'+str(root/'build'),'-Wl,-rpath,'+str(root/'build'),'-lcariboulite','-o',binary],check=True)
    subprocess.run([binary],check=True,timeout=10)
