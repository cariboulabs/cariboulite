#!/usr/bin/env python3
"""Exercise real FPGA gap API with zero-success SPI and injected failures."""
from pathlib import Path
import subprocess
import tempfile
s = (Path(__file__).resolve().parents[1] / 'src/caribou_fpga/caribou_fpga.c').read_text()
def function(name):
    a = s.index(name)
    a = s.rfind('\n', 0, a) + 1
    b = s.index('\n}', a) + 2
    return s[a:b]
types = s[s.index('#pragma pack(1)'):s.index('#pragma pack()')+len('#pragma pack()')]
preamble = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define ZF_LOGE(...) ((void)0)
#define CARIBOU_FPGA_CHECK_DEV(...) ((void)0)
#define IOC_SYS_CTRL_TX_SAMPLE_GAP 6
#define IOC_SYS_CTRL_DEBUG_MODES 5
#define io_utils_spi_read_write 0
typedef struct { void *io_spi; int io_spi_handle; } caribou_fpga_st;
static uint8_t regval, debugval;
static int calls, fail_call;
static int io_utils_spi_transmit(void *d,int h,const uint8_t *tx,uint8_t *rx,size_t len,int dir) {
    assert(len==2); assert((tx[0]&127)==6 || tx[0]==0x85);
    if(++calls==fail_call)return -1;
    if(tx[0]==0x85) { debugval=tx[1]; return 0; }
    if(tx[0]&128)regval=tx[1];
    rx[1]=regval;
    return 0; /* Actual io_utils contract. */
}
'''
main = r'''
int main(void) {
 caribou_fpga_st d={0}; uint8_t gap=255;
 regval=0xa0;
 assert(caribou_fpga_set_sys_ctrl_tx_sample_gap(&d,1)==0);
 assert(regval==0xa1 && calls==2);
 assert(caribou_fpga_get_sys_ctrl_tx_sample_gap(&d,&gap)==0 && gap==1);
 calls=0; assert(caribou_fpga_set_sys_ctrl_tx_sample_gap(&d,1)==0 && calls==1);
 calls=0; fail_call=1;
 assert(caribou_fpga_set_sys_ctrl_tx_sample_gap(&d,0)==-1 && calls==1 && regval==0xa1);
 calls=0; fail_call=2;
 assert(caribou_fpga_set_sys_ctrl_tx_sample_gap(&d,0)==-1 && regval==0xa1);
 calls=0; fail_call=1;
 assert(caribou_fpga_get_sys_ctrl_tx_sample_gap(&d,&gap)==-1);
 calls=0; fail_call=0;
 assert(caribou_fpga_set_sys_ctrl_tx_sample_gap(&d,0)==0 && regval==0xa0);
 calls=0;fail_call=0;
 assert(caribou_fpga_set_debug_loopback(&d,true)==0 && debugval==8);
 assert(caribou_fpga_set_debug_loopback(&d,false)==0 && debugval==0);
 calls=0;fail_call=1;
 assert(caribou_fpga_set_debug_loopback(&d,true)==-1 && debugval==0);
 calls=0;fail_call=0;
 assert(caribou_fpga_set_debug_loopback(&d,true)==0 && debugval==8);
 calls=0;fail_call=1;
 assert(caribou_fpga_set_debug_loopback(&d,false)==-1 && debugval==8);
 return 0;
}
'''
code = preamble + types + '\n' + '\n'.join(function(n) for n in [
 'static int caribou_fpga_spi_transfer (',
 'int caribou_fpga_set_sys_ctrl_tx_sample_gap (',
 'int caribou_fpga_get_sys_ctrl_tx_sample_gap (',
 'int caribou_fpga_set_debug_loopback(']) + main
with tempfile.TemporaryDirectory() as d:
    p = Path(d); (p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS FPGA SPI success, gap read/write, preserved sync bits, debug-loopback writes, and failure propagation')
