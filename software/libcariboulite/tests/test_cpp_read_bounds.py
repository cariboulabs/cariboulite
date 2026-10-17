#!/usr/bin/env python3
"""Test actual C++ ReadSamples bodies with a mock reader and guarded storage."""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parents[1] / 'src/CaribouLiteRadioCpp.cpp').read_text()
start = source.index('int CaribouLiteRadio::ReadSamples(std::complex<float>')
end = source.index('int CaribouLiteRadio::WriteSamples(', start)
bodies = source[start:end]
harness = r'''
#include <cassert>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>
struct cariboulite_radio_state_st {};
struct cariboulite_sample_complex_int16 { short i, q; };
struct cariboulite_sample_meta { uint8_t sync; };
static size_t requested;
static int calls, response = 4;
int cariboulite_radio_read_samples(cariboulite_radio_state_st*, cariboulite_sample_complex_int16* s,
                                  cariboulite_sample_meta* m, size_t n) {
    ++calls; requested = n;
    int count = response < 0 ? response : (int)(n < (size_t)response ? n : response);
    for (int i=0;i<count;++i) { s[i] = {2048,-1024}; m[i].sync = 1; }
    return count;
}
struct CaribouLiteRadio {
    void* _radio = nullptr;
    bool _rx_is_active = true;
    cariboulite_sample_complex_int16* _read_samples;
    cariboulite_sample_meta* _read_metadata;
    size_t _read_capacity = 4;
    int ReadSamples(std::complex<float>*,size_t,uint8_t* = nullptr);
    int ReadSamples(std::complex<short>*,size_t,uint8_t* = nullptr);
};
template<class T> struct Guarded {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    void* base = mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    T* data;
    Guarded() {
        assert(base != MAP_FAILED);
        assert(mprotect((char*)base+page,page,PROT_NONE)==0);
        data = (T*)((char*)base+page)-4;
    }
    ~Guarded() { munmap(base,page*2); }
};
'''
checks = r'''
int main() {
    Guarded<cariboulite_sample_complex_int16> samples;
    Guarded<cariboulite_sample_meta> metadata;
    CaribouLiteRadio r;
    r._read_samples=samples.data; r._read_metadata=metadata.data;
    std::complex<short> ints[5]; std::complex<float> floats[5]; uint8_t meta[5];
    for (size_t n : {size_t(1),size_t(4),size_t(5),std::numeric_limits<size_t>::max()}) {
        ints[4]={7,8}; floats[4]={7,8}; memset(meta,99,sizeof(meta));
        response=4;
        int expected = n < 4 ? (int)n : 4;
        assert(r.ReadSamples(ints,n,meta)==expected && requested==(size_t)expected);
        assert(ints[0]==std::complex<short>(2048,-1024) && meta[0]==1);
        assert(ints[4]==std::complex<short>(7,8) && meta[4]==99);
        assert(r.ReadSamples(floats,n,meta)==expected);
        assert(floats[0]==std::complex<float>(0.5f,-0.25f));
        assert(floats[4]==std::complex<float>(7,8) && meta[4]==99);
    }
    response=2; assert(r.ReadSamples(floats,100)==2);
    response=-5; assert(r.ReadSamples(floats,100)==-5);
    response=0; assert(r.ReadSamples(ints,4)==0);
    int before=calls;
    assert(r.ReadSamples(ints,0)==0 && calls==before);
    r._rx_is_active=false;
    assert(r.ReadSamples(ints,4)==0 && calls==before);
    r._rx_is_active=true; r._read_capacity=0;
    assert(r.ReadSamples(ints,4)==0 && calls==before);
    puts("PASS: integer/float read bounds, metadata, short reads, errors, inactive/empty reads");
}
'''
with tempfile.TemporaryDirectory(prefix='cpp-read-bounds-') as directory:
    cpp = Path(directory)/'test.cpp'
    binary = Path(directory)/'test'
    cpp.write_text(harness+bodies+checks)
    subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=10)
