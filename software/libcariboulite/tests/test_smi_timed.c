// Actual SMI implementation with scripted syscalls and a monotonic fake clock.
#include "caribou_smi/caribou_smi.h"
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static int64_t now;
static int calls, polls, mode, prefix, interrupts, rx_offset;
static unsigned char output[128];
static size_t output_len;
int __wrap_clock_gettime(clockid_t id, struct timespec *ts) {
    (void)id; ts->tv_sec=now/1000000; ts->tv_nsec=(now%1000000)*1000; return 0;
}
int __wrap_ppoll(struct pollfd *fds, nfds_t n, const struct timespec *wait, const sigset_t *mask) {
    (void)n; (void)mask; ++polls;
    int64_t us=wait->tv_sec*1000000+wait->tv_nsec/1000;
    assert(us>0);
    if (interrupts) { --interrupts; now+=us<100?us:100; errno=EINTR; return -1; }
    if (mode==4) { fds->revents=POLLERR; return 1; }
    now+=us; return 0;
}
ssize_t __wrap_write(int fd, const void *buf, size_t n) {
    (void)fd; ++calls;
    if (mode==1) { errno=EIO; return -1; }
    if (mode==2 || prefix) {
        size_t take=prefix?(size_t)prefix:n; prefix=0;
        if(take>n)take=n;
        memcpy(output+output_len,buf,take); output_len+=take; return take;
    }
    errno=EAGAIN; return -1;
}
ssize_t __wrap_read(int fd, void *buf, size_t n) {
    (void)fd; ++calls;
    if (mode==1) { errno=EIO; return -1; }
    if (mode==2 || mode==3 || mode==5) {
        if(n>24)n=24;
        uint32_t word=mode==5?0x84037048:(mode==2?0x80004000:0);
        memset(buf,0xff,n);
        for(size_t i=rx_offset;i+4<=n;i+=4)memcpy((char*)buf+i,&word,4);
        return n;
    }
    return 0;
}
int main(void) {
    uint8_t scratch[128];
    caribou_smi_st d={0}; d.filedesc=42; d.native_batch_len=sizeof(scratch);
    d.read_temp_buffer=scratch; d.write_temp_buffer=scratch;
    caribou_smi_sample_complex_int16 samples[32]={{0}};
    for(int tx=0;tx<2;tx++) {
        const long budgets[] = {0, 37, 1000};
        for(unsigned budget=0;budget<3;budget++) {
            long us=budgets[budget];
            now=0;calls=polls=0;mode=0;interrupts=3;
            int r=tx?caribou_smi_write_timed(&d,0,samples,32,us):caribou_smi_read_timed(&d,0,samples,NULL,32,us);
            assert(r==0 && now==us);
            assert(us || (calls==1 && polls==0));
        }
        for(mode=1;mode<=4;mode++) {
            now=0;interrupts=0;
            int r=tx?caribou_smi_write_timed(&d,0,samples,32,1000):caribou_smi_read_timed(&d,0,samples,NULL,32,1000);
            if(mode==1 || mode==4) assert(r==-1);
            if(mode==2) assert(r==(tx?32:6));
            if(mode==3 && !tx) assert(r==-3);
        }
    }
    // Modem EXTLB returns the TX control bit at bit 16 unchanged.
    mode=5; interrupts=0;
    assert(caribou_smi_read_timed(&d,0,samples,NULL,32,1000)==-3);
    assert(caribou_smi_read_loopback_timed(&d,caribou_smi_channel_900,
                                         samples,32,1000)==6);
    for(int k=0;k<6;++k) assert(samples[k].i==513 && samples[k].q==-2012);
    assert(caribou_smi_read_loopback_timed(&d,caribou_smi_channel_2400,
                                         samples,32,1000)==6);
    for(int k=0;k<6;++k) assert(samples[k].i==-2012 && samples[k].q==513);
    for(rx_offset=1;rx_offset<=3;++rx_offset) {
        samples[5].i=1234;
        assert(caribou_smi_read_loopback_timed(&d,caribou_smi_channel_2400,
                                             samples,32,1000)==5);
        for(int k=0;k<5;++k) assert(samples[k].i==-2012 && samples[k].q==513);
        assert(samples[5].i==1234); // No invented/interpolated tail sample.
    }
    rx_offset=0;
    assert(caribou_smi_read_loopback_timed(&d,0,samples,1,1000)==1);
    assert(caribou_smi_read_loopback_timed(&d,0,samples,0,1000)==0);
    mode=3;assert(caribou_smi_read_loopback_timed(&d,0,samples,32,1000)==-3);
    mode=1;assert(caribou_smi_read_loopback_timed(&d,0,samples,32,1000)==-1);
    mode=0;now=0;interrupts=3;
    assert(caribou_smi_read_loopback_timed(&d,0,samples,32,1000)==0 && now==1000);
    // Every possible partial byte prefix: retry must reproduce the exact wire data.
    for(int bytes=1;bytes<8;bytes++) {
        unsigned char expected[16];
        samples[0].i=123; samples[1].q=456;
        mode=2;output_len=0;d.write_partial_bytes=0;
        assert(caribou_smi_write_timed(&d,0,samples,2,0)==2);
        memcpy(expected,output,8);
        mode=0;prefix=bytes;output_len=0;now=0;
        int r=caribou_smi_write_timed(&d,0,samples,2,1000);
        assert(r==bytes/4 && d.write_partial_bytes==(size_t)bytes%4);
        mode=2;
        assert(caribou_smi_write_timed(&d,0,samples+r,2-r,0)==2-r);
        assert(output_len==8 && memcmp(expected,output,8)==0);
    }
    puts("PASS: SMI zero/short deadlines, EINTR budget, partial RX/TX, byte-prefix retries, I/O/poll errors, RX corruption");
}
