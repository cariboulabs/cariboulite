#!/usr/bin/env python3
"""Build actual Soapy/SMI sources with mock radio/syscalls. No hardware access."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[3]
src=root/'software/libcariboulite/src'
tests=Path(__file__).parent
includes=['-I'+str(src),'-I'+str(src/'iir')]
libs=['-L'+str(root/'build'),'-Wl,-rpath,'+str(root/'build'),'-lcariboulite','-lSoapySDR','-pthread']
with tempfile.TemporaryDirectory(prefix='soapy-stream-') as directory:
    exe=str(Path(directory)/'soapy')
    wraps=['cariboulite_radio_'+s for s in ['get_native_mtu_size_samples','activate_channel','set_cw_outputs','read_samples_timed','write_samples_timed']]
    sources=[src/'soapy_api'/s for s in ['Cariboulite.cpp','CaribouliteSensors.cpp','CaribouliteStream.cpp','CaribouliteStreamFunctions.cpp']]
    subprocess.run(['c++','-std=c++11',*includes,str(tests/'test_soapy_stream.cpp'),*map(str,sources),*[f'-Wl,--wrap={s}' for s in wraps],str(root/'build/src/iir/libiir.so'),'-Wl,-rpath,'+str(root/'build/src/iir'),*libs,'-o',exe],check=True)
    subprocess.run([exe],check=True,timeout=10)
    exe=str(Path(directory)/'smi')
    subprocess.run(['cc','-D_GNU_SOURCE',*includes,str(tests/'test_smi_timed.c'),str(src/'caribou_smi/caribou_smi.c'),*[f'-Wl,--wrap={s}' for s in ['read','write','ppoll','clock_gettime']],*libs,'-o',exe],check=True)
    subprocess.run([exe],check=True,timeout=10)
