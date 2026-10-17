#!/usr/bin/env python3
"""Demodulate generated IQ to verify pitch/deviation at all selectable TX rates."""
import ctypes as C
import math
from pathlib import Path
import subprocess
import tempfile
src=Path(__file__).resolve().parents[1]/'src'
class Config(C.Structure):
    _fields_=[('audio',C.c_double),('rf',C.c_double),('dev',C.c_double),('tau',C.c_double),('scale',C.c_float),('linear',C.c_int)]
class IQ(C.Structure):
    _pack_=1
    _fields_=[('i',C.c_int16),('q',C.c_int16)]
with tempfile.TemporaryDirectory() as d:
    libpath=Path(d)/'mod.so'
    subprocess.run(['cc','-shared','-fPIC','-O2',str(src/'nbfm_mod.c'),'-lm','-o',str(libpath)],check=True)
    lib=C.CDLL(str(libpath))
    lib.nbfm_create.argtypes=[C.POINTER(Config)];lib.nbfm_create.restype=C.c_void_p
    lib.nbfm_destroy.argtypes=[C.c_void_p]
    lib.nbfm_push_audio.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.c_size_t];lib.nbfm_push_audio.restype=C.c_size_t
    lib.nbfm_pull_iq.argtypes=[C.c_void_p,C.POINTER(IQ),C.c_size_t];lib.nbfm_pull_iq.restype=C.c_size_t
    lib.nbfm_buffered_audio.argtypes=[C.c_void_p];lib.nbfm_buffered_audio.restype=C.c_size_t
    for fs in (4000000,2000000,1000000):
        m=lib.nbfm_create(C.byref(Config(48000,fs,2500,0,12000,1)))
        assert m,fs
        audio=(C.c_float*480)(*[0.4*math.sin(2*math.pi*600*j/48000) for j in range(480)])
        iq=(IQ*(fs//100))(); crossings=[]; previous=None; last=0; index=0; peak_deviation=0
        try:
            for frame in range(12):
                assert lib.nbfm_push_audio(m,audio,480)==480
                assert lib.nbfm_pull_iq(m,iq,len(iq))==len(iq)
                assert lib.nbfm_buffered_audio(m)==0
                # Average phase differences over 100us to suppress quantization.
                acc=0; count=0
                for sample in iq:
                    cur=complex(sample.i,sample.q)
                    if previous is not None:
                        z=cur*previous.conjugate();acc+=math.atan2(z.imag,z.real);count+=1
                    previous=cur;index+=1
                    if count==fs//10000:
                        if index>fs//50:
                            peak_deviation=max(peak_deviation,abs(acc)*fs/(2*math.pi*count))
                        if index>fs//50 and last<0<=acc:crossings.append(index)
                        last=acc;acc=0;count=0
            hz=fs*(len(crossings)-1)/(crossings[-1]-crossings[0])
            assert abs(hz-600)<2,(fs,hz)
            assert 970<peak_deviation<1010,(fs,peak_deviation)
            print(f'PASS {fs} samples/s: demodulated tone {hz:.2f} Hz, deviation {peak_deviation:.1f} Hz; no audio backlog')
        finally:lib.nbfm_destroy(m)
