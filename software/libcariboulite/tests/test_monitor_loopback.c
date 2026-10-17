#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cariboulite_setup.h"
#include "at86rf215.h"
#include "monitor_loopback.h"

static uint8_t regs[0x300];
static int calls, fail_at, persistent, capture_result = 32;
static bool pattern, stuck, mismatch;
static smi_stream_state_en stream;
static int fault(void) { ++calls; return persistent || calls == fail_at; }
int at86rf215_read_buffer(at86rf215_st* d, uint16_t a, uint8_t* b, uint8_t n) {
    if (fault()) return -1;
    memcpy(b, regs+a, n); if(mismatch && a==REG_RF_IQIFC0) b[0]^=0x10; return 0;
}
int at86rf215_write_byte(at86rf215_st* d, uint16_t a, uint8_t v) {
    // The controller must NEVER request TX or TX_PREP, even during recovery.
    if (a==REG_RF09_CMD || a==REG_RF24_CMD) assert(v==2 || v==5);
    if (fault()) return -1;
    if (a==REG_RF_IQIFC0 && (v&0x80)) {
        assert(!(v&1));
        assert(regs[REG_RF09_STATE]==2 && regs[REG_RF24_STATE]==5);
    }
    regs[a]=v;
    if (a==REG_RF09_CMD && !stuck) regs[REG_RF09_STATE]=v;
    if (a==REG_RF24_CMD && !stuck) regs[REG_RF24_STATE]=v;
    return 0;
}
int caribou_smi_set_driver_streaming_state(caribou_smi_st* d, smi_stream_state_en s) {
    assert(s==smi_stream_idle || s==smi_stream_rx_channel_1);
    if (fault()) return -1;
    if (s!=smi_stream_idle) assert(pattern && (regs[REG_RF_IQIFC0]&0x81)==0x80);
    stream=s; return 0;
}
int caribou_fpga_set_debug_loopback(caribou_fpga_st* d, bool b) {
    if (fault()) return -1;
    if(b) assert((regs[REG_RF_IQIFC0]&0x81)==0x80);
    pattern=b; return 0;
}
int caribou_fpga_set_io_ctrl_mode(caribou_fpga_st* d, uint8_t b, caribou_fpga_io_ctrl_rfm_en m) {
    assert(m==caribou_fpga_io_ctrl_rfm_low_power); return fault() ? -1 : 0;
}
int caribou_fpga_set_smi_channel(caribou_fpga_st* d, caribou_fpga_smi_channel_en c) {
    assert(c==caribou_fpga_smi_channel_1); return fault() ? -1 : 0;
}
int caribou_fpga_set_smi_ctrl_data_direction(caribou_fpga_st* d, uint8_t v) {
    assert(v==1); return fault() ? -1 : 0;
}
int caribou_smi_read_loopback_timed(caribou_smi_st* d, caribou_smi_channel_en ch,
                          caribou_smi_sample_complex_int16* b,
                          size_t count, long timeout) {
    assert(ch==caribou_smi_channel_2400 && count==4096 && timeout==10000);
    for(int i=0;i<capture_result;++i) {b[i].i=-2012;b[i].q=513;}
    return capture_result;
}
static void setup(void) {
    memset(regs,0,sizeof(regs)); regs[REG_RF_IQIFC0]=0x15;regs[REG_RF_IQIFC1]=0x12;
    regs[REG_RF09_STATE]=2;regs[REG_RF24_STATE]=2;
    calls=fail_at=persistent=0;pattern=stuck=mismatch=false;stream=smi_stream_idle;
}
static void clean(monitor_loopback_t* lb) {
    assert(!lb->armed && !lb->active && !pattern && stream==smi_stream_idle);
    assert(!(regs[REG_RF_IQIFC0]&0x80));
    assert(regs[REG_RF09_STATE]==2 && regs[REG_RF24_STATE]==2);
}
int main(void) {
    sys_st sys={0};monitor_loopback_t lb={0};setup();
    assert(monitor_loopback_start(&sys,&lb)==0);
    int start_calls=calls;
    assert(lb.armed && lb.active);
    const char* blocked="tTrR124fFgG";
    for(const char* k=blocked;*k;++k) assert(monitor_loopback_blocks_control(&lb,*k));
    assert(!monitor_loopback_blocks_control(&lb,'l'));
    assert(!monitor_loopback_blocks_control(&lb,'q'));
    assert(monitor_loopback_start(&sys,&lb)!=0); // duplicate start does not overwrite saved config
    assert(monitor_loopback_read(&sys,&lb)==0 && lb.count==16 && lb.total==32);
    assert(lb.samples[0].i==-2012 && lb.samples[0].q==513);
    capture_result=3;assert(monitor_loopback_read(&sys,&lb)==0 && lb.count==3);
    capture_result=0;assert(monitor_loopback_read(&sys,&lb)==0 && lb.count==0 && lb.timeouts==1);
    capture_result=-1;assert(monitor_loopback_read(&sys,&lb)==-1 && lb.count==0);
    calls=0;assert(monitor_loopback_stop(&sys,&lb)==0);int stop_calls=calls;
    clean(&lb);assert(regs[REG_RF_IQIFC0]==0x15 && regs[REG_RF_IQIFC1]==0x12);
    assert(monitor_loopback_stop(&sys,&lb)==0);
    for(int n=1;n<=start_calls;++n) {
        setup();memset(&lb,0,sizeof(lb));fail_at=n;
        assert(monitor_loopback_start(&sys,&lb)!=0);
        if(lb.armed) {fail_at=0;assert(monitor_loopback_stop(&sys,&lb)==0);}
        clean(&lb);
    }
    for(int n=1;n<=stop_calls;++n) {
        setup();memset(&lb,0,sizeof(lb));assert(monitor_loopback_start(&sys,&lb)==0);
        calls=0;fail_at=n;
        assert(monitor_loopback_stop(&sys,&lb)!=0 && lb.armed && !lb.active);
        fail_at=0;assert(monitor_loopback_stop(&sys,&lb)==0);clean(&lb);
    }
    setup();memset(&lb,0,sizeof(lb));assert(monitor_loopback_start(&sys,&lb)==0);
    persistent=1;assert(monitor_loopback_stop(&sys,&lb)!=0 && lb.armed);
    assert(monitor_loopback_start(&sys,&lb)!=0);
    persistent=0;assert(monitor_loopback_stop(&sys,&lb)==0);clean(&lb);
    setup();memset(&lb,0,sizeof(lb));stuck=true;
    assert(monitor_loopback_start(&sys,&lb)!=0 && !lb.active && !pattern);
    stuck=false;if(lb.armed) assert(monitor_loopback_stop(&sys,&lb)==0);clean(&lb);
    setup();memset(&lb,0,sizeof(lb));mismatch=true;
    assert(monitor_loopback_start(&sys,&lb)!=0 && !lb.active && !pattern);
    mismatch=false;if(lb.armed) assert(monitor_loopback_stop(&sys,&lb)==0);clean(&lb);
    for(const char* k=blocked;*k;++k) assert(!monitor_loopback_blocks_control(&lb,*k));
    puts("PASS: loopback sequencing, no TX commands, register restoration, fresh samples, every start/stop I/O failure and cleanup retry");
}
