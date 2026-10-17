/* Include the implementation to exercise its private pipeline and FIFO types. */
#include "app_menu.c"
#include "nbfm_demod.h"
#include "mod_worker.h"
#include "caribou_smi/kernel/smi_stream_dev.h"
#include <stdarg.h>
#include <sys/ioctl.h>

static bool real_threads, live[1024];
static int creates, fail_create, joins, hardware_active;
static bool fail_malloc, fail_calloc, fail_demod_create;
static unsigned malloc_calls, calloc_calls, alsa_sink_opens, demod_create_calls;
static unsigned hardware_calls;
static unsigned deviation_sets, demod_thread_creates;
static bool verify_deviation_wiring, deviation_configured, fail_deviation_set;
static float last_deviation;
static audio_demod_t* last_demod_created;
static audio_demod_mode_t last_demod_mode;
audio_demod_t* __real_audio_demod_create(audio_demod_mode_t, const audio_demod_config_t*);
audio_demod_t* __wrap_audio_demod_create(audio_demod_mode_t mode, const audio_demod_config_t* c) {
    ++demod_create_calls;
    last_demod_mode=mode;
    deviation_configured=false;
    last_demod_created=fail_demod_create ? NULL : __real_audio_demod_create(mode,c);
    return last_demod_created;
}
int __real_audio_demod_set_nbfm_deviation(audio_demod_t*, float);
int __wrap_audio_demod_set_nbfm_deviation(audio_demod_t* dsp, float deviation_hz) {
    ++deviation_sets;
    if (verify_deviation_wiring) {
        assert(dsp && dsp==last_demod_created && last_demod_mode==AUDIO_DEMOD_NBFM);
        assert(!deviation_configured);
    }
    last_deviation=deviation_hz;
    int result=fail_deviation_set ? -EINVAL :
        __real_audio_demod_set_nbfm_deviation(dsp,deviation_hz);
    deviation_configured=result==0;
    return result;
}
audio_sink_t* __real_alsa_sink_open(const char*, unsigned);
audio_sink_t* __wrap_alsa_sink_open(const char* device, unsigned rate) {
    ++alsa_sink_opens;
    return __real_alsa_sink_open(device,rate);
}
static size_t fail_calloc_count;
static void* metadata_allocation;
static bool track_metadata;
static const rx_pipeline_t* tracked_cleanup_rx;
static unsigned cleanup_fifo_frees;
static pthread_barrier_t reader_ready;
void __real_free(void*);
void __wrap_free(void* p) {
    if (p == metadata_allocation) metadata_allocation = NULL;
    if (tracked_cleanup_rx && p) {
        if (p==tracked_cleanup_rx->rxq.q) cleanup_fifo_frees|=1;
        if (p==tracked_cleanup_rx->afifo.q) cleanup_fifo_frees|=2;
    }
    __real_free(p);
}
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
int __real_pthread_create(pthread_t*, const pthread_attr_t*, void*(*)(void*), void*);
int __real_pthread_cancel(pthread_t);
int __real_pthread_join(pthread_t, void**);
void *__wrap_malloc(size_t n) {
    if (verify_deviation_wiring) ++malloc_calls;
    void* p = fail_malloc ? NULL : __real_malloc(n);
    if (track_metadata) metadata_allocation = p;
    return p;
}
void *__wrap_calloc(size_t n, size_t s) {
    if (verify_deviation_wiring) ++calloc_calls;
    return (fail_calloc || (fail_calloc_count && n==fail_calloc_count)) ? NULL : __real_calloc(n,s);
}
int __wrap_pthread_create(pthread_t* t, const pthread_attr_t* a, void*(*fn)(void*), void* v) {
    if (fn==audio_demod_thread) {
        ++demod_thread_creates;
        if (verify_deviation_wiring) {
            const audio_demod_ctrl_t* ctrl=v;
            assert(ctrl->dsp==last_demod_created);
            assert(deviation_configured==(ctrl->mode==AUDIO_DEMOD_NBFM));
        }
    }
    if (real_threads) return __real_pthread_create(t,a,fn,v);
    ++creates;
    if (creates == fail_create) return EAGAIN;
    assert(creates < 1024); *t = (pthread_t)creates; live[creates] = true; return 0;
}
int __wrap_pthread_cancel(pthread_t t) {
    if (real_threads) return __real_pthread_cancel(t);
    assert(t && t < 1024 && live[t]); return 0;
}
int __wrap_pthread_join(pthread_t t, void** out) {
    if (real_threads) return __real_pthread_join(t,out);
    assert(t && t < 1024 && live[t]); live[t] = false; ++joins; return 0;
}
static double tuned_frequency;
static cariboulite_radio_state_st* tuned_radio;
static cariboulite_radio_state_st* activated_radio;
static bool fail_tune;
/* Model the modem registers separately from the driver's cached radio fields.
 * This catches a setter which returns success without programming hardware. */
static uint8_t modem_regs[0x300];
static unsigned rx_activations, stream_activations, agc_writes[2];
enum modem_fault {
    MODEM_OK, MODEM_DEACTIVATE, MODEM_STATE_READ, MODEM_STATE_MISMATCH,
    MODEM_BANDWIDTH_SET, MODEM_CUTOFF_SET, MODEM_FILTER_READ,
    MODEM_FILTER_MISMATCH, MODEM_AGC_WRITE, MODEM_AGC_READ,
    MODEM_AGC_CONTROL_MISMATCH, MODEM_AGC_TARGET_MISMATCH
};
static enum modem_fault modem_fault;
static uint8_t filter_readback_byte, filter_readback_xor=1, agc_readback_xor=1;
static unsigned modem_channel(const cariboulite_radio_state_st* r) {
    return r->type == cariboulite_channel_s1g ? 0 : 1;
}
static uint16_t modem_reg(const cariboulite_radio_state_st* r, uint16_t low) {
    return low + 0x100*modem_channel(r);
}
int __wrap_cariboulite_radio_set_frequency(cariboulite_radio_state_st* r, bool b, double* f) {
    ++hardware_calls;
    if (fail_tune) return -1;
    tuned_radio = r;
    tuned_frequency = *f;
    *f += 1; // driver returns achieved frequency: must not mutate const parameters
    return 0;
}
int __wrap_cariboulite_radio_activate_channel(cariboulite_radio_state_st* r, cariboulite_channel_dir_en d, bool a) {
    ++hardware_calls;
    if (!a && d == cariboulite_channel_dir_rx && modem_fault == MODEM_DEACTIVATE)
        return -1;
    if (a && d == cariboulite_channel_dir_rx) {
        uint16_t bw=modem_reg(r,REG_RF09_RXBWC), agc=modem_reg(r,REG_RF09_AGCC);
        assert(modem_regs[modem_reg(r,REG_RF09_STATE)]==at86rf215_radio_state_cmd_trx_off);
        assert(modem_regs[bw]==(0x10|(uint8_t)r->rx_bw));
        assert(modem_regs[bw+1]==(((uint8_t)r->rx_fcut<<5)|(uint8_t)r->rx_fs));
        if (r->rx_bw==cariboulite_radio_rx_bw_160KHz) {
            assert(r->rx_fcut==cariboulite_radio_rx_f_cut_0_25_half_fs);
            assert((modem_regs[agc]&0x73)==1 && (modem_regs[agc+1]&0xe0)==0);
            assert(r->rx_agc_on);
        } else {
            assert(r->rx_bw==cariboulite_radio_rx_bw_250KHz);
            assert(r->rx_fcut==cariboulite_radio_rx_f_cut_0_25_half_fs);
        }
        ++rx_activations;
    }
    activated_radio = r;
    modem_regs[modem_reg(r,REG_RF09_STATE)]=a ?
        (d==cariboulite_channel_dir_rx ? at86rf215_radio_state_cmd_rx : at86rf215_radio_state_cmd_tx) :
        at86rf215_radio_state_cmd_trx_off;
    r->active=a;
    hardware_active = a; return 0;
}
static smi_stream_state_en last_stream;
int __wrap_caribou_smi_set_driver_streaming_state(caribou_smi_st* s, smi_stream_state_en e) {
    ++hardware_calls;
    if(e!=smi_stream_idle) ++stream_activations;
    last_stream=e; return 0;
}
static size_t native_batch_samples = 131072;
static int fifo_multiplier = 16;
static bool fail_fifo_query;
static unsigned native_batch_reads, fifo_multiplier_reads;
size_t __wrap_caribou_smi_get_native_batch_samples(caribou_smi_st* smi) {
    ++hardware_calls;
    assert(smi);
    ++native_batch_reads;
    return native_batch_samples;
}
int __real_ioctl(int, unsigned long, ...);
int __wrap_ioctl(int fd, unsigned long request, ...) {
    ++hardware_calls;
    va_list args;
    va_start(args, request);
    void* argument = va_arg(args, void*);
    va_end(args);
    if (request != SMI_STREAM_IOC_GET_FIFO_MULT)
        return __real_ioctl(fd, request, argument);
    ++fifo_multiplier_reads;
    if (fail_fifo_query) { errno=ENOTTY; return -1; }
    assert(argument);
    *(int*)argument=fifo_multiplier;
    return 0;
}
int __wrap_caribou_fpga_set_io_ctrl_mode(caribou_fpga_st* f, uint8_t d, caribou_fpga_io_ctrl_rfm_en m) {
    ++hardware_calls;
    return 0;
}
static rx_reader_ctrl_st* capture_once;
static rx_reader_ctrl_st* scripted_reader;
static unsigned scripted_read;
static unsigned rssi_reads;
static uint16_t expected_rssi_register;
static uint8_t measured_rssi;
static int rssi_read_error;
int __wrap_at86rf215_read_buffer(at86rf215_st* dev, uint16_t reg, uint8_t* value, uint8_t length) {
    ++hardware_calls;
    (void)dev;
    if(reg==REG_RF09_RSSI || reg==REG_RF24_RSSI) {
        assert(capture_once && reg == expected_rssi_register && length == 1);
        ++rssi_reads; *value = measured_rssi;
        return rssi_read_error;
    }
    bool state=reg==REG_RF09_STATE || reg==REG_RF24_STATE;
    bool filter=reg==REG_RF09_RXBWC || reg==REG_RF24_RXBWC;
    bool agc=reg==REG_RF09_AGCC || reg==REG_RF24_AGCC;
    bool txfilter=reg==REG_RF09_TXCUTC || reg==REG_RF24_TXCUTC;
    assert((state && length==1) || ((filter || agc || txfilter) && length==2));
    if ((state && modem_fault==MODEM_STATE_READ) ||
        (filter && modem_fault==MODEM_FILTER_READ) ||
        (agc && modem_fault==MODEM_AGC_READ)) return -1;
    memcpy(value,modem_regs+reg,length);
    if(state && modem_fault==MODEM_STATE_MISMATCH) *value=at86rf215_radio_state_cmd_rx;
    if(filter && modem_fault==MODEM_FILTER_MISMATCH) value[filter_readback_byte]^=filter_readback_xor;
    if(agc) {
        value[1]|=17; /* AGC owns the live GCW; target verification must ignore it. */
        if(modem_fault==MODEM_AGC_CONTROL_MISMATCH) *value^=agc_readback_xor;
        if(modem_fault==MODEM_AGC_TARGET_MISMATCH) value[1]^=0x20;
    }
    return 0;
}
int __wrap_at86rf215_write_buffer(at86rf215_st* dev, uint16_t reg, uint8_t* value, uint8_t length) {
    ++hardware_calls;
    (void)dev;
    assert((reg==REG_RF09_AGCC || reg==REG_RF24_AGCC) && length==2);
    unsigned channel=reg==REG_RF09_AGCC ? 0 : 1;
    assert(modem_regs[REG_RF09_STATE+0x100*channel]==at86rf215_radio_state_cmd_trx_off);
    assert(value[0]==1 && value[1]==0);
    ++agc_writes[channel];
    if(modem_fault==MODEM_AGC_WRITE) return -1;
    memcpy(modem_regs+reg,value,length);
    return 0;
}
int __wrap_cariboulite_radio_read_samples(cariboulite_radio_state_st* r,
        cariboulite_sample_complex_int16* b, cariboulite_sample_meta* m, size_t n) {
    if (scripted_reader) {
        static const int results[]={4,0,-EIO,16};
        static const size_t requests[]={20,16,16,16};
        assert(scripted_read<4 && n==requests[scripted_read]);
        int result=results[scripted_read++];
        if (result>0) memset(b,0,(size_t)result*sizeof(*b));
        if (scripted_read==4) scripted_reader->active=false;
        assert(atomic_load(&scripted_reader->alive));
        return result;
    }
    if (capture_once) {
        memset(b, 0, n*sizeof(*b));
        capture_once->active = false;
        return (int)n;
    }
    pthread_barrier_wait(&reader_ready);
    for (;;) { pthread_testcancel(); usleep(1000); }
    return 0;
}
static float test_rx_rate;
static cariboulite_radio_state_st* rate_rx_radio;
int __wrap_cariboulite_radio_set_rx_sample_rate_flt(cariboulite_radio_state_st* r, float fs) {
    ++hardware_calls;
    test_rx_rate=fs; rate_rx_radio=r; return 0;
}
int __wrap_cariboulite_radio_set_rx_bandwidth(cariboulite_radio_state_st* r,
                                             cariboulite_radio_rx_bw_en bw) {
    ++hardware_calls;
    assert(modem_regs[modem_reg(r,REG_RF09_STATE)]==at86rf215_radio_state_cmd_trx_off);
    if(modem_fault==MODEM_BANDWIDTH_SET) return -1;
    /* Real setter resets RCUT to full, so policy must explicitly narrow it. */
    r->rx_bw=bw;
    r->rx_fcut=cariboulite_radio_rx_f_cut_half_fs;
    uint16_t reg=modem_reg(r,REG_RF09_RXBWC);
    modem_regs[reg]=0x10|(uint8_t)bw;
    modem_regs[reg+1]=((uint8_t)r->rx_fcut<<5)|(uint8_t)r->rx_fs;
    return 0;
}
int __wrap_cariboulite_radio_set_rx_samp_cutoff(cariboulite_radio_state_st* r,
                                              cariboulite_radio_sample_rate_en fs,
                                              cariboulite_radio_f_cut_en cutoff) {
    ++hardware_calls;
    assert(modem_regs[modem_reg(r,REG_RF09_STATE)]==at86rf215_radio_state_cmd_trx_off);
    if(modem_fault==MODEM_CUTOFF_SET) return -1;
    assert(fs==cariboulite_radio_rx_sample_rate_1000khz ||
           fs==cariboulite_radio_rx_sample_rate_2000khz ||
           fs==cariboulite_radio_rx_sample_rate_4000khz);
    test_rx_rate=fs==cariboulite_radio_rx_sample_rate_1000khz ? 1000000 :
                 fs==cariboulite_radio_rx_sample_rate_2000khz ? 2000000 : 4000000;
    rate_rx_radio=r;
    r->rx_fs=fs;
    r->rx_fcut=cutoff;
    uint16_t reg=modem_reg(r,REG_RF09_RXBWC);
    modem_regs[reg]=0x10|(uint8_t)r->rx_bw;
    modem_regs[reg+1]=((uint8_t)cutoff<<5)|(uint8_t)fs;
    return 0;
}
static float test_tx_rate = 4000000;
static cariboulite_radio_state_st* rate_tx_radio;
static int forced_tx_gap = -1;
static unsigned tx_gap_reads;
int __wrap_cariboulite_radio_set_tx_bandwidth(cariboulite_radio_state_st* r,
                                            cariboulite_radio_tx_cut_off_en bw) {
    ++hardware_calls;
    assert(!r->active && modem_regs[modem_reg(r,REG_RF09_STATE)]==at86rf215_radio_state_cmd_trx_off);
    assert(bw==cariboulite_radio_tx_cut_off_80khz);
    r->tx_bw=bw;
    modem_regs[modem_reg(r,REG_RF09_TXCUTC)]=0x80|(uint8_t)bw;
    return 0;
}
int __wrap_cariboulite_radio_set_tx_samp_cutoff(cariboulite_radio_state_st* r,
        cariboulite_radio_sample_rate_en fs, cariboulite_radio_f_cut_en cutoff) {
    ++hardware_calls;
    assert(!r->active && modem_regs[modem_reg(r,REG_RF09_STATE)]==at86rf215_radio_state_cmd_trx_off);
    assert(cutoff==cariboulite_radio_rx_f_cut_0_25_half_fs);
    test_tx_rate=fs==cariboulite_radio_rx_sample_rate_1000khz ? 1000000 :
        fs==cariboulite_radio_rx_sample_rate_2000khz ? 2000000 : 4000000;
    r->tx_fs=fs; r->tx_fcut=cutoff; rate_tx_radio=r;
    modem_regs[modem_reg(r,REG_RF09_TXDFE)]=((uint8_t)cutoff<<5)|(uint8_t)fs;
    return 0;
}
int __wrap_caribou_fpga_get_sys_ctrl_tx_sample_gap(caribou_fpga_st* f, uint8_t* gap) {
    ++tx_gap_reads;
    *gap=forced_tx_gap<0 ? 4000000/test_tx_rate-1 : forced_tx_gap;
    return 0;
}
int __wrap_cariboulite_radio_set_tx_power(cariboulite_radio_state_st* r, int power) {
    ++hardware_calls;
    return 0;
}
static const struct {
    unsigned fs;
    size_t frame_samples;
    uint8_t tx_gap;
} monitor_rates[] = {
    {1000000, 10000, 3}, {2000000, 20000, 1}, {4000000, 40000, 0}
};
static const unsigned monitor_tail_padding[] = {227, 114, 57};
static void check_clean(rx_pipeline_t* p) {
    assert(!p->inited && !p->running && !hardware_active);
    assert(!p->demod.dsp && !p->demod.sink);
    for (int i=1; i<=creates; ++i) assert(!live[i]);
    rx_pipeline_destroy(p); /* repeated cleanup is harmless */
}
static aud10_fifo_t audio;
static rf10_fifo_t rf;
static void* waiter(void* arg) {
    int which = *(int*)arg;
    aud10_frame_t af = {0}; rf10_frame_t frame = {0};
    if (which == 0) aud10_fifo_get(&audio,&af,-1);
    if (which == 1) aud10_fifo_put(&audio,&af,-1);
    if (which == 2) rf10_fifo_get(&rf,&frame,-1);
    if (which == 3) rf10_fifo_put(&rf,&frame,-1);
    return NULL;
}
static void test_rssi_capture(void) {
    sys_st sys = {0};
    sys.board_info.numeric_product_id = system_type_cariboulite_full;
    sys.radio_low.sys = sys.radio_high.sys = &sys;
    sys.radio_low.type = cariboulite_channel_s1g;
    sys.radio_high.type = cariboulite_channel_hif;
    rf10_fifo_t fifo;
    rf10_fifo_init(&fifo, 2, true);
    cariboulite_sample_complex_int16 buffer[20];
    atomic_uint flags;
    atomic_init(&flags, RX_SQUELCH_CARRIER);
    rx_reader_ctrl_st ctrl = {.active=true,.radio=&sys.radio_high,
        .rx_buffer=buffer,.rx_buffer_size=20,.rx_fifo=&fifo,.squelch_flags=&flags};
    atomic_init(&ctrl.alive,false);
    pipeline_thread_diagnostics_init(&ctrl.scheduling);
    atomic_init(&ctrl.read_calls,0); atomic_init(&ctrl.read_samples,0);
    atomic_init(&ctrl.short_reads,0); atomic_init(&ctrl.zero_reads,0);
    atomic_init(&ctrl.read_errors,0); atomic_init(&ctrl.last_error,0);
    capture_once=&ctrl;
    for (unsigned trial=0; trial<5; ++trial) {
        ctrl.active=true;
        ctrl.radio=trial==1 ? &sys.radio_low : &sys.radio_high;
        expected_rssi_register=trial==1 ? REG_RF09_RSSI : REG_RF24_RSSI;
        measured_rssi=trial==2 ? 127 : (uint8_t)(int8_t)-90;
        rssi_read_error=trial==3 ? -1 : 0;
        atomic_store(&flags, trial==4 ? 0 : RX_SQUELCH_CARRIER);
        unsigned before=rssi_reads;
        rx_reader_thread_func(&ctrl);
        assert(!atomic_load(&ctrl.alive));
        assert(atomic_load(&ctrl.read_calls)==trial+1);
        rf10_frame_t frame;
        assert(rf10_fifo_get(&fifo,&frame,0));
        assert(frame.rssi_valid == (trial<2));
        if(trial<2) assert(frame.rssi_dbm==-90);
        assert(rssi_reads==before+(trial!=4));
        assert(pthread_mutex_trylock(&g_hw_lock)==0);
        pthread_mutex_unlock(&g_hw_lock);
    }
    capture_once=NULL;
    rf10_fifo_destroy(&fifo);
}

static void test_shared_rx_stats(void)
{
    rx_pipeline_stats_t stats;
    rx_pipeline_t p={0};
    memset(&stats,0xa5,sizeof(stats));
    rx_pipeline_get_stats(NULL,&stats);
    assert(!stats.playback_valid && stats.rxq.cap==0 && stats.audioq.cap==0);
    assert(!stats.reader.calls && !stats.audio_worker.created && !stats.audio_worker.alive);
    memset(&stats,0xa5,sizeof(stats));
    rx_pipeline_get_stats(&p,&stats);
    assert(!stats.playback_valid && !stats.reader.errors && !stats.demod_worker.scheduling.valid);
    rx_pipeline_get_stats(&p,NULL);
    sys_st sys={0};
    sys.board_info.numeric_product_id = system_type_cariboulite_full;
    sys.radio_high.sys=&sys; sys.radio_high.type=cariboulite_channel_hif;
    rx_params_t params={.freq_hz=430125000,.pcm_dev="null",.fs_rf=1000000,.fs_audio=48000};
    assert(rx_pipeline_init(&p,&sys,&sys.radio_high,&params)==0);
    rx_pipeline_get_stats(&p,&stats);
    assert(stats.playback_valid && stats.playback.write_calls==0);
    assert(stats.audioq.cap==24 && stats.rxq.cap==64 && !stats.reader.calls);
    assert(stats.audio_worker.created && stats.demod_worker.created);
    assert(!stats.audio_worker.alive && !stats.demod_worker.alive); // lifecycle mocks
    const audio_sink_ops_t unsupported_ops={0};
    audio_sink_t unsupported={48000,&unsupported_ops};
    audio_sink_t* sink=p.demod.sink;
    p.demod.sink=&unsupported;
    rx_pipeline_get_stats(&p,&stats);
    assert(!stats.playback_valid && stats.playback.write_calls==0);
    p.demod.sink=sink;

    p.rx_ctrl.rx_buffer=calloc(20,sizeof(*p.rx_ctrl.rx_buffer)); assert(p.rx_ctrl.rx_buffer);
    p.rx_ctrl.rx_buffer_size=20; p.rx_ctrl.active=true;
    p.rx_ctrl.squelch_flags=NULL;
    scripted_reader=&p.rx_ctrl; scripted_read=0;
    rx_reader_thread_func(&p.rx_ctrl);
    scripted_reader=NULL;
    rx_pipeline_get_stats(&p,&stats);
    assert(stats.reader.calls==4 && stats.reader.samples==20 && stats.reader.short_reads==1);
    assert(stats.reader.zero_reads==1 && stats.reader.errors==1 && stats.reader.last_error==-EIO);
    assert(!stats.reader_worker.alive && stats.reader_worker.scheduling.valid);
    assert(stats.rxq.puts==1 && stats.rxq.count==1);
    p.rx_ctrl.active=true;
    fail_malloc=true;
    rx_reader_thread_func(&p.rx_ctrl);
    fail_malloc=false;
    rx_pipeline_get_stats(&p,&stats);
    assert(!stats.reader_worker.alive && stats.reader.calls==4);
    assert(stats.reader.errors==1 && stats.reader.last_error==-ENOMEM);
    assert(stats.reader_worker.last_error==-ENOMEM);

    aud10_frame_t frame={0};
    assert(aud10_fifo_put(&p.afifo,&frame,0));
    assert(audio_sink_write(sink,frame.pcm,480).frames==480);
    rx_pipeline_get_stats(&p,&stats);
    assert(stats.audioq.puts==1 && stats.audioq.count==1);
    assert(stats.playback.write_calls==1 && stats.playback.frames_written==480);
    rx_pipeline_reset_stats(&p);
    rx_pipeline_get_stats(&p,&stats);
    assert(stats.rxq.count==1 && stats.audioq.count==1); // reset preserves queued data
    assert(!stats.rxq.puts && !stats.audioq.puts && !stats.audioq.gets && !stats.audioq.drops);
    assert(stats.audioq.min_depth==1 && stats.audioq.max_depth==1);
    assert(stats.reader.calls==4 && stats.reader.errors==1 && stats.reader.last_error==-ENOMEM);
    assert(stats.playback.write_calls==1 && stats.playback.frames_written==480);
    p.demod.pcm_gain=NAN;
    audio_demod_thread(&p.demod); // queued RF block, rejected audio configuration
    rx_pipeline_get_stats(&p,&stats);
    assert(!stats.demod_worker.alive && stats.demod_worker.last_error==-EINVAL);
    assert(stats.demod_worker.scheduling.valid);
    rx_pipeline_reset_stats(&p);
    rx_pipeline_get_stats(&p,&stats);
    assert(stats.demod_worker.last_error==-EINVAL && stats.playback.write_calls==1);
    rx_pipeline_destroy(&p); check_clean(&p);
    memset(&stats,0xa5,sizeof(stats));
    rx_pipeline_get_stats(&p,&stats);
    assert(!stats.playback_valid && !stats.rxq.cap && !stats.audioq.cap && !stats.reader.calls);
}

static audio_sink_result_t fail_audio_write(audio_sink_t* sink,const int16_t* samples,size_t frames)
{
    (void)sink; (void)samples; (void)frames;
    return (audio_sink_result_t){0,AUDIO_SINK_ERROR,-EIO};
}
static void test_audio_worker_exits(void)
{
    const audio_sink_ops_t fail_ops={.write=fail_audio_write};
    audio_sink_t sink={48000,&fail_ops};
    aud10_fifo_t fifo;
    aud10_fifo_init(&fifo,1);
    audio_writer_ctrl_t ctrl={.active=true,.sink=&sink,.fifo=&fifo};
    atomic_init(&ctrl.alive,false);
    atomic_init(&ctrl.last_error,0);
    pipeline_thread_diagnostics_init(&ctrl.scheduling);
    aud10_frame_t frame={0};
    assert(aud10_fifo_put(&fifo,&frame,0));
    audio_writer_thread(&ctrl); // fatal sink error
    assert(!atomic_load(&ctrl.alive) && atomic_load(&ctrl.scheduling.ready));
    assert(atomic_load(&ctrl.last_error)==-EIO);
    aud10_fifo_stop(&fifo);
    audio_writer_thread(&ctrl); // normal stopped-queue return
    assert(!atomic_load(&ctrl.alive));
    assert(atomic_load(&ctrl.last_error)==-EIO); // retained on subsequent normal exit
    aud10_fifo_destroy(&fifo);
    aud10_fifo_init(&fifo,1);
    pipeline_thread_diagnostics_reset(&ctrl.scheduling);
    pthread_t thread; void* result;
    assert(pthread_create(&thread,NULL,audio_writer_thread,&ctrl)==0);
    for(unsigned retry=0; retry<1000 && !atomic_load(&ctrl.alive); ++retry) usleep(1000);
    assert(atomic_load(&ctrl.alive));
    assert(pthread_cancel(thread)==0);
    assert(pthread_join(thread,&result)==0 && result==PTHREAD_CANCELED);
    assert(!atomic_load(&ctrl.alive));
    assert(pthread_mutex_trylock(&fifo.m)==0); pthread_mutex_unlock(&fifo.m);
    aud10_fifo_destroy(&fifo);
}
// Lifecycle mocks do not run DSP threads; acknowledge cue consumption so a
// successful TX start exercises real tuning/activation without generating RF.
static void* consume_injection(void* arg) {
    tx_pipeline_t* tx=arg;
    for (;;) {
        pthread_mutex_lock(&g_tx_injection_lock);
        if (tx->tx_ctrl.inj.frames_left>0) {
            // Emulate enqueue followed by complete writer acceptance, before
            // signalling completion of the injected sequence.
            tx->tx_ctrl.inj.last_sequence += tx->tx_ctrl.inj.frames_left;
            atomic_store(&tx->tx_ctrl.written_sequence,
                         tx->tx_ctrl.inj.last_sequence);
            tx->tx_ctrl.inj.frames_left=0;
        }
        pthread_mutex_unlock(&g_tx_injection_lock);
        usleep(1000);
    }
    return NULL;
}
static void stale_nbfm_profile(cariboulite_radio_state_st* radio) {
    uint16_t filter=modem_reg(radio,REG_RF09_RXBWC);
    uint16_t agc=modem_reg(radio,REG_RF09_AGCC);
    modem_regs[filter]=0x20|cariboulite_radio_rx_bw_2000KHz; /* IFS off, IFI on */
    modem_regs[filter+1]=(cariboulite_radio_rx_f_cut_half_fs<<5)|cariboulite_radio_rx_sample_rate_4000khz;
    modem_regs[agc]=0x62; /* manual gain, frozen, unfiltered, 32-sample average */
    modem_regs[agc+1]=0xf7; /* stale -42 dB target and maximum manual gain */
    radio->rx_bw=cariboulite_radio_rx_bw_2000KHz;
    radio->rx_fcut=cariboulite_radio_rx_f_cut_half_fs;
    radio->rx_fs=cariboulite_radio_rx_sample_rate_4000khz;
    radio->rx_agc_on=false;
    radio->rx_gain_value_db=69;
}
static void assert_modem_profile(const rx_pipeline_t* p) {
    const cariboulite_radio_state_st* radio=p->radio;
    bool nbfm=p->demod.mode==AUDIO_DEMOD_NBFM;
    cariboulite_radio_rx_bw_en bw=nbfm ? cariboulite_radio_rx_bw_160KHz : cariboulite_radio_rx_bw_250KHz;
    cariboulite_radio_f_cut_en cutoff=cariboulite_radio_rx_f_cut_0_25_half_fs;
    cariboulite_radio_sample_rate_en rate=p->demod.fs_rf==1000000 ? cariboulite_radio_rx_sample_rate_1000khz :
        p->demod.fs_rf==2000000 ? cariboulite_radio_rx_sample_rate_2000khz : cariboulite_radio_rx_sample_rate_4000khz;
    uint16_t filter=modem_reg(radio,REG_RF09_RXBWC), agc=modem_reg(radio,REG_RF09_AGCC);
    assert(radio->rx_bw==bw && radio->rx_fcut==cutoff && radio->rx_fs==rate);
    assert(modem_regs[filter]==(0x10|(uint8_t)bw));
    assert(modem_regs[filter+1]==(((uint8_t)cutoff<<5)|(uint8_t)rate));
    assert(test_rx_rate==p->demod.fs_rf && rate_rx_radio==radio);
    if(nbfm) {
        assert(modem_regs[agc]==1 && modem_regs[agc+1]==0);
        assert(radio->rx_agc_on && radio->rx_gain_value_db==51);
    }
}
static void test_modem_policy(void) {
    sys_st sys={0};
    sys.board_info.numeric_product_id = system_type_cariboulite_full;
    sys.radio_low.sys=sys.radio_high.sys=&sys;
    sys.radio_low.type=cariboulite_channel_s1g;
    sys.radio_high.type=cariboulite_channel_hif;
    for(unsigned channel=0;channel<2;++channel) {
        cariboulite_radio_state_st* radio=channel ? &sys.radio_high : &sys.radio_low;
        const uint16_t other_filter=REG_RF09_RXBWC+0x100*!channel;
        /* RXBWC, RXDFE, AGCC and AGCS are contiguous. Keep a distinct profile
         * in the inactive bank so cross-channel configuration is observable. */
        const uint8_t other_registers[]={0x26,0x61,0x62,0xf7};
        memcpy(modem_regs+other_filter,other_registers,sizeof(other_registers));
        for(unsigned rate=0;rate<sizeof(monitor_rates)/sizeof(*monitor_rates);++rate) {
            rx_params_t par={.mode=AUDIO_DEMOD_NBFM,.freq_hz=430125000,.pcm_dev="null",
                .fs_rf=monitor_rates[rate].fs,.fs_audio=48000};
            rx_pipeline_t rx={0}; tx_pipeline_t tx={0}; monitor_loopback_t loopback={0};
            assert(rx_pipeline_init(&rx,&sys,radio,&par)==0);
            assert_modem_profile(&rx); /* Idle initialization uses the shared profile too. */
            unsigned other_agc=agc_writes[!channel];
            for(unsigned restart=0;restart<2;++restart) {
                stale_nbfm_profile(radio);
                unsigned before=agc_writes[channel];
                assert(rx_pipeline_start(&rx)==0);
                assert_modem_profile(&rx);
                assert(!memcmp(modem_regs+other_filter,other_registers,sizeof(other_registers)));
                assert(agc_writes[channel]==before+1 && agc_writes[!channel]==other_agc);
                assert(last_stream==(channel ? smi_stream_rx_channel_1 : smi_stream_rx_channel_0));
                rx_pipeline_stop(&rx);
            }
            assert(monitor_cycle_rx_mode(&tx,&rx,&loopback,&sys,&par)==0 && par.mode==AUDIO_DEMOD_WBFM);
            /* WBFM restores its 250 kHz profile without changing gain policy. */
            stale_nbfm_profile(radio);
            unsigned before=agc_writes[channel];
            uint16_t agc=modem_reg(radio,REG_RF09_AGCC);
            uint8_t saved_agc=modem_regs[agc], saved_gain=modem_regs[agc+1];
            assert(rx_pipeline_start(&rx)==0);
            assert_modem_profile(&rx);
            assert(!memcmp(modem_regs+other_filter,other_registers,sizeof(other_registers)));
            assert(agc_writes[channel]==before && modem_regs[agc]==saved_agc &&
                   modem_regs[agc+1]==saved_gain && !radio->rx_agc_on);
            rx_pipeline_stop(&rx);
            assert(monitor_cycle_rx_mode(&tx,&rx,&loopback,&sys,&par)==0 && par.mode==AUDIO_DEMOD_NBFM);
            assert(agc_writes[channel]==before+1);
            assert_modem_profile(&rx);
            assert(rx_pipeline_start(&rx)==0);
            assert_modem_profile(&rx);
            assert(!memcmp(modem_regs+other_filter,other_registers,sizeof(other_registers)));
            assert(agc_writes[channel]==before+2);
            rx_pipeline_destroy(&rx); check_clean(&rx);
        }
        const audio_demod_mode_t modes[]={AUDIO_DEMOD_NBFM,AUDIO_DEMOD_WBFM};
        for(unsigned mode=0;mode<sizeof(modes)/sizeof(*modes);++mode) {
            const bool nbfm=modes[mode]==AUDIO_DEMOD_NBFM;
            rx_params_t par={.mode=modes[mode],.freq_hz=430125000,.pcm_dev="null",
                .fs_rf=1000000,.fs_audio=48000};
            rx_pipeline_t rx={0};
            const enum modem_fault last_fault=nbfm ? MODEM_AGC_TARGET_MISMATCH : MODEM_FILTER_MISMATCH;
            for(enum modem_fault fault=MODEM_DEACTIVATE;fault<=last_fault;++fault) {
                stale_nbfm_profile(radio);
                unsigned before_activations=rx_activations, before_streams=stream_activations;
                filter_readback_byte=0;
                filter_readback_xor=agc_readback_xor=1;
                modem_fault=fault;
                assert(rx_pipeline_init(&rx,&sys,radio,&par)<0);
                assert(rx_activations==before_activations && stream_activations==before_streams);
                assert(!radio->rx_agc_on);
                assert(!memcmp(modem_regs+other_filter,other_registers,sizeof(other_registers)));
                check_clean(&rx);
                modem_fault=MODEM_OK;
            }
            assert(rx_pipeline_init(&rx,&sys,radio,&par)==0);
            assert_modem_profile(&rx);
            for(enum modem_fault fault=MODEM_DEACTIVATE;fault<=last_fault;++fault) {
                unsigned variants=fault==MODEM_FILTER_MISMATCH || fault==MODEM_AGC_CONTROL_MISMATCH ? 5 : 1;
                for(unsigned variant=0;variant<variants;++variant) {
                    static const uint8_t filter_xors[]={1,0x10,0x20,0x20,1};
                    static const uint8_t agc_xors[]={1,2,0x10,0x20,0x40};
                    filter_readback_byte=variant>=3;
                    filter_readback_xor=filter_xors[variant];
                    agc_readback_xor=agc_xors[variant];
                    stale_nbfm_profile(radio);
                    uint16_t agc=modem_reg(radio,REG_RF09_AGCC);
                    uint8_t saved_agc=modem_regs[agc], saved_gain=modem_regs[agc+1];
                    unsigned before_activations=rx_activations, before_streams=stream_activations;
                    int before_creates=creates;
                    modem_fault=fault;
                    assert(rx_pipeline_start(&rx)==-4);
                    assert(!rx.running && !rx.rx_thread_created && !rx.rx_ctrl.active && !hardware_active);
                    assert(rx_activations==before_activations && stream_activations==before_streams && creates==before_creates);
                    assert(!memcmp(modem_regs+other_filter,other_registers,sizeof(other_registers)));
                    assert(!radio->rx_agc_on); /* Only verified readback may update this cache. */
                    assert(pthread_mutex_trylock(&g_hw_lock)==0);
                    pthread_mutex_unlock(&g_hw_lock);
                    modem_fault=MODEM_OK;
                    assert(rx_pipeline_start(&rx)==0);
                    assert_modem_profile(&rx);
                    assert(!memcmp(modem_regs+other_filter,other_registers,sizeof(other_registers)));
                    if(!nbfm) assert(modem_regs[agc]==saved_agc && modem_regs[agc+1]==saved_gain && !radio->rx_agc_on);
                    rx_pipeline_stop(&rx);
                }
            }
            rx_pipeline_destroy(&rx); check_clean(&rx);
        }
    }
    puts("PASS: RF09/RF24 NBFM/WBFM filter restoration at 1/2/4 MS/s, isolated registers, mode transitions, failed configuration blocks RX and retries cleanly");
}
static void test_rx_deviation_wiring(void) {
    sys_st sys={0};
    sys.board_info.numeric_product_id = system_type_cariboulite_full;
    sys.radio_high.sys=&sys;
    sys.radio_high.type=cariboulite_channel_hif;
    rx_params_t par={.freq_hz=430125000,.pcm_dev="null",.fs_rf=1000000,.fs_audio=48000};
    rx_pipeline_t rx={0};
    verify_deviation_wiring=true;

    const float references[]={0,1,2500,5000,24000};
    for (unsigned rate=0; rate<sizeof(monitor_rates)/sizeof(*monitor_rates); ++rate) {
        par.fs_rf=monitor_rates[rate].fs;
        for (unsigned i=0; i<sizeof(references)/sizeof(*references); ++i) {
            par.nbfm_deviation_hz=references[i];
            unsigned before_sets=deviation_sets, before_threads=demod_thread_creates;
            assert(rx_pipeline_init(&rx,&sys,&sys.radio_high,&par)==0);
            assert(deviation_sets==before_sets+1 && demod_thread_creates==before_threads+1);
            assert(last_deviation==(references[i] ? references[i] : 2500));
            assert(rx.demod.dsp==last_demod_created && deviation_configured);
            assert(par.nbfm_deviation_hz==references[i]); // legacy zero stays an omitted setting
            rx_pipeline_destroy(&rx); check_clean(&rx);
        }

        // A saved NBFM reference must not be applied to the WBFM demodulator.
        par.mode=AUDIO_DEMOD_WBFM;
        par.nbfm_deviation_hz=5000;
        unsigned before_sets=deviation_sets, before_threads=demod_thread_creates;
        assert(rx_pipeline_init(&rx,&sys,&sys.radio_high,&par)==0);
        assert(deviation_sets==before_sets && demod_thread_creates==before_threads+1);
        assert(!deviation_configured && rx.demod.mode==AUDIO_DEMOD_WBFM);
        rx_pipeline_destroy(&rx); check_clean(&rx);
        par.mode=AUDIO_DEMOD_NBFM;
    }

    const float invalid[]={-1,.5f,24001,NAN,INFINITY,-INFINITY};
    for (unsigned mode=0; mode<2; ++mode) {
        par.mode=mode ? AUDIO_DEMOD_WBFM : AUDIO_DEMOD_NBFM;
        for (unsigned i=0; i<sizeof(invalid)/sizeof(*invalid); ++i) {
            par.nbfm_deviation_hz=invalid[i];
            int before_creates=creates;
            unsigned before_malloc=malloc_calls, before_calloc=calloc_calls;
            unsigned before_alsa=alsa_sink_opens, before_demod=demod_create_calls;
            unsigned before_sets=deviation_sets;
            assert(rx_pipeline_init(&rx,&sys,&sys.radio_high,&par)<0);
            assert(creates==before_creates && malloc_calls==before_malloc && calloc_calls==before_calloc);
            assert(alsa_sink_opens==before_alsa && demod_create_calls==before_demod && deviation_sets==before_sets);
            assert(!rx.rxq.q && !rx.afifo.q);
            check_clean(&rx);
        }
    }

    // Setter failure joins the already-created writer and releases all resources;
    // it must never start the demodulator, and a later initialization can succeed.
    par.mode=AUDIO_DEMOD_NBFM;
    par.nbfm_deviation_hz=5000;
    fail_deviation_set=true;
    tracked_cleanup_rx=&rx;
    cleanup_fifo_frees=0;
    int before_creates=creates, before_joins=joins;
    unsigned before_sets=deviation_sets, before_threads=demod_thread_creates;
    assert(rx_pipeline_init(&rx,&sys,&sys.radio_high,&par)<0);
    assert(creates==before_creates+1 && joins==before_joins+1);
    assert(deviation_sets==before_sets+1 && demod_thread_creates==before_threads);
    assert(cleanup_fifo_frees==3);
    check_clean(&rx);
    tracked_cleanup_rx=NULL;
    fail_deviation_set=false;
    assert(rx_pipeline_init(&rx,&sys,&sys.radio_high,&par)==0);
    assert(last_deviation==5000 && deviation_configured);
    rx_pipeline_destroy(&rx); check_clean(&rx);
    verify_deviation_wiring=false;
    puts("PASS: RX deviation defaults/ranges and NBFM-only configuration before demod startup; validation and setter failures clean up safely");
}

static void test_noise_level_controls(void) {
    float opening=0, closing=0;
    assert(monitor_parse_noise_levels("0.120 0.180",&opening,&closing));
    assert(fabsf(opening-.12f)<1e-6f && fabsf(closing-.18f)<1e-6f);
    assert(monitor_parse_noise_levels(" .1404\t.2104 ",&opening,&closing));
    assert(fabsf(opening-.140f)<1e-6f && fabsf(closing-.210f)<1e-6f);
    const char* invalid[]={"", "0.12", "0.12,0.18", "0 0.18", "-0.1 0.2",
        "0.18 0.12", "0.12 0.12", "0.1201 0.1202", "0.1 8.001",
        "nan 0.2", "0.1 inf", "1e999 0.2", "0.12 0.18 extra", "0.12 0.18 0.3"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        assert(!monitor_parse_noise_levels(invalid[i],&opening,&closing));
        assert(fabsf(opening-.140f)<1e-6f && fabsf(closing-.210f)<1e-6f);
    }
    sys_st sys={0};
    sys.board_info.numeric_product_id = system_type_cariboulite_full;
    sys.radio_high.sys=&sys;
    sys.radio_high.type=cariboulite_channel_hif;
    rx_params_t par={.freq_hz=430125000,.pcm_dev="null",.fs_rf=1000000,.fs_audio=48000};
    rx_pipeline_t rx={0};
    rx_noise_squelch_status_t status;
    // Invalid initial levels fail before allocating FIFOs or creating threads.
    par.noise_squelch_open_rms=.2f; par.noise_squelch_close_rms=.1f;
    int before=creates;
    assert(rx_pipeline_init(&rx,&sys,&sys.radio_high,&par)<0 && creates==before);
    par.noise_squelch_open_rms=par.noise_squelch_close_rms=0;
    assert(rx_pipeline_init(&rx,&sys,&sys.radio_high,&par)==0);
    assert(rx_pipeline_get_noise_squelch_status(&rx,&status)==0 && !status.valid);
    assert(fabsf(status.open_rms-.20f)<1e-6f && fabsf(status.close_rms-.30f)<1e-6f);
    assert(rx_pipeline_start(&rx)==0);
    audio_demod_t* dsp=rx.demod.dsp;
    rx.demod.reset=false;
    before=creates;
    assert(monitor_apply_noise_levels(&rx,&par,.1404f,.2104f)==0);
    assert(fabsf(par.noise_squelch_open_rms-.140f)<1e-6f &&
           fabsf(par.noise_squelch_close_rms-.210f)<1e-6f);
    assert(rx.running && !rx.demod.reset && rx.demod.dsp==dsp && creates==before);
    assert(monitor_apply_noise_levels(&rx,&par,.3f,.2f)<0);
    assert(rx_pipeline_get_noise_squelch_status(&rx,&status)==0 && !status.valid);
    assert(fabsf(status.open_rms-.140f)<1e-6f && fabsf(status.close_rms-.210f)<1e-6f);
    // Publishing measurements remains independent of the effective gate.
    atomic_store(&rx.demod.noise_squelch_rms_milli,52);
    atomic_store(&rx.demod.noise_squelch_detector_open,1);
    atomic_store(&rx.demod.noise_squelch_valid,1);
    rx_pipeline_set_squelch(&rx,false,true);
    assert(rx_pipeline_get_noise_squelch_status(&rx,&status)==0 && status.valid);
    assert(fabsf(status.rms-.052f)<1e-6f && status.detector_open);
    assert(!rx_pipeline_squelch_open(&rx));
    rx_pipeline_stop(&rx);
    assert(rx_pipeline_get_noise_squelch_status(&rx,&status)==0 && !status.valid);
    rx_pipeline_destroy(&rx); check_clean(&rx);
    for(unsigned rate=0;rate<sizeof(monitor_rates)/sizeof(*monitor_rates);++rate) {
        par.fs_rf=monitor_rates[rate].fs;
        assert(rx_pipeline_init(&rx,&sys,&sys.radio_high,&par)==0);
        assert(rx_pipeline_get_noise_squelch_status(&rx,&status)==0);
        assert(fabsf(status.open_rms-.140f)<1e-6f && fabsf(status.close_rms-.210f)<1e-6f);
        tx_pipeline_t tx={0}; monitor_loopback_t loopback={0};
        assert(monitor_cycle_rx_mode(&tx,&rx,&loopback,&sys,&par)==0);
        assert(par.mode==AUDIO_DEMOD_WBFM);
        assert(rx_pipeline_start(&rx)==0);
        atomic_store(&rx.demod.noise_squelch_valid,1); // WBFM still cannot expose noise RMS.
        assert(rx_pipeline_get_noise_squelch_status(&rx,&status)==0 && !status.valid);
        rx_pipeline_stop(&rx);
        assert(monitor_cycle_rx_mode(&tx,&rx,&loopback,&sys,&par)==0);
        assert(par.mode==AUDIO_DEMOD_NBFM);
        assert(rx_pipeline_get_noise_squelch_status(&rx,&status)==0);
        assert(fabsf(status.open_rms-.140f)<1e-6f && fabsf(status.close_rms-.210f)<1e-6f);
        rx_pipeline_destroy(&rx); check_clean(&rx);
    }
    assert(rx_pipeline_set_noise_squelch_levels(&rx,.12f,.18f)<0);
    assert(rx_pipeline_get_noise_squelch_status(&rx,&status)<0);
    puts("PASS: noise-level input validation, live atomic settings, bypass telemetry, and rate/mode persistence");
}

static void assert_invalid_rx_config(sys_st* sys, cariboulite_radio_state_st* radio,
                                     const rx_params_t* params)
{
    rx_pipeline_t pipeline={0};
    unsigned before_malloc=malloc_calls, before_calloc=calloc_calls;
    unsigned before_hardware=hardware_calls, before_alsa=alsa_sink_opens;
    unsigned before_demod=demod_create_calls;
    int before_creates=creates;
    assert(rx_pipeline_init(&pipeline,sys,radio,params)<0);
    assert(malloc_calls==before_malloc && calloc_calls==before_calloc);
    assert(hardware_calls==before_hardware && alsa_sink_opens==before_alsa);
    assert(demod_create_calls==before_demod && creates==before_creates);
    check_clean(&pipeline);
}

static void assert_invalid_tx_config(sys_st* sys, cariboulite_radio_state_st* radio,
                                     const tx_params_t* params)
{
    tx_pipeline_t pipeline={0};
    unsigned before_malloc=malloc_calls, before_calloc=calloc_calls;
    unsigned before_hardware=hardware_calls;
    int before_creates=creates;
    assert(tx_pipeline_init(&pipeline,sys,radio,params)<0);
    assert(malloc_calls==before_malloc && calloc_calls==before_calloc);
    assert(hardware_calls==before_hardware && creates==before_creates);
    assert(!pipeline.inited && !pipeline.running && !pipeline.tx_ctrl.gate_initialized);
    assert(!pipeline.txq.q && !pipeline.tx_ctrl.mic && !pipeline.tx_ctrl.fm);
}

static void test_config_validation_before_side_effects(void)
{
    sys_st sys={0};
    sys.board_info.numeric_product_id=system_type_cariboulite_full;
    sys.radio_low=(cariboulite_radio_state_st){.sys=&sys,.type=cariboulite_channel_s1g};
    sys.radio_high=(cariboulite_radio_state_st){.sys=&sys,.type=cariboulite_channel_hif};
    const rx_params_t rx_defaults={.freq_hz=430125000,.pcm_dev="null",
        .fs_rf=1000000,.fs_audio=48000,.pcm_gain=8000,.deemph_tau_s=50e-6f};
    const tx_params_t tx_defaults={.freq_hz=430125000,.rf_fs=1000000,
        .tx_power_dbm=-3,.tone_mode=true,.tone_hz=600,.tone_amp=.4f,
        .f_dev_hz=2500,.out_scale=4000};
    verify_deviation_wiring=true;
    for (unsigned channel=0;channel<2;++channel) {
        cariboulite_radio_state_st* radio=channel ? &sys.radio_high : &sys.radio_low;
        rx_pipeline_t rx_control={.sys=&sys,.radio=radio,.running=true};
        tx_pipeline_t tx_control={.sys=&sys,.radio=radio};
        rx_control.demod.pcm_gain=rx_defaults.pcm_gain;
        rx_control.demod.deemph_tau=rx_defaults.deemph_tau_s;
        rx_control.demod.prime_blocks_10ms=19;
        const double bad_frequency[]={0,-1,NAN,INFINITY,-INFINITY,6000000000.0};
        for (unsigned i=0;i<sizeof(bad_frequency)/sizeof(*bad_frequency);++i) {
            rx_params_t rx=rx_defaults; rx.freq_hz=bad_frequency[i];
            tx_params_t tx=tx_defaults; tx.freq_hz=bad_frequency[i];
            assert_invalid_rx_config(&sys,radio,&rx);
            assert_invalid_tx_config(&sys,radio,&tx);
            unsigned before_hardware=hardware_calls;
            assert(rx_pipeline_set_freq(&rx_control,bad_frequency[i])<0);
            assert(tx_pipeline_set_freq_power(&tx_control,bad_frequency[i],-3)<0);
            assert(hardware_calls==before_hardware);
            assert(!rx_control.demod.reset && rx_control.demod.prime_blocks_10ms==19);
        }
        const float bad_live_values[]={-1,NAN,INFINITY,-INFINITY};
        unsigned before_hardware=hardware_calls;
        for (unsigned i=0;i<sizeof(bad_live_values)/sizeof(*bad_live_values);++i) {
            assert(rx_pipeline_set_pcm_gain(&rx_control,bad_live_values[i])<0);
            assert(rx_control.demod.pcm_gain==rx_defaults.pcm_gain);
            assert(rx_pipeline_set_deemph(&rx_control,bad_live_values[i])<0);
            assert(rx_control.demod.deemph_tau==rx_defaults.deemph_tau_s);
        }
        assert(rx_pipeline_set_pcm_gain(&rx_control,0)==0 && rx_control.demod.pcm_gain==0);
        assert(rx_pipeline_set_deemph(&rx_control,0)==0 && rx_control.demod.deemph_tau==0);
        assert(hardware_calls==before_hardware);
        const int bad_power[]={channel ? -13 : -15,15};
        for (unsigned i=0;i<sizeof(bad_power)/sizeof(*bad_power);++i) {
            assert(tx_pipeline_set_freq_power(&tx_control,430125000,bad_power[i])<0);
            assert(hardware_calls==before_hardware);
        }
        sys.board_info.numeric_product_id=-1;
        assert_invalid_rx_config(&sys,radio,&rx_defaults);
        assert_invalid_tx_config(&sys,radio,&tx_defaults);
        assert(rx_pipeline_set_freq(&rx_control,430125000)<0);
        assert(tx_pipeline_set_freq_power(&tx_control,430125000,-3)<0);
        assert(hardware_calls==before_hardware);
        sys.board_info.numeric_product_id=system_type_cariboulite_full;
        for (unsigned field=0;field<3;++field) {
            const float invalid[]={-1,NAN,INFINITY,-INFINITY};
            for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
                rx_params_t rx=rx_defaults;
                if (field==0) rx.fs_rf=invalid[i];
                if (field==1) rx.pcm_gain=invalid[i];
                if (field==2) rx.deemph_tau_s=invalid[i];
                assert_invalid_rx_config(&sys,radio,&rx);
            }
        }
        rx_params_t rx=rx_defaults; rx.mode=(audio_demod_mode_t)99;
        assert_invalid_rx_config(&sys,radio,&rx);
        rx=rx_defaults; rx.fs_rf=3000000;
        assert_invalid_rx_config(&sys,radio,&rx);
        rx=rx_defaults; rx.fs_audio=44100;
        assert_invalid_rx_config(&sys,radio,&rx);
        for (unsigned field=0;field<4;++field) {
            const float invalid[]={-1,NAN,INFINITY,-INFINITY};
            for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
                tx_params_t tx=tx_defaults;
                if (field==0) tx.tone_hz=invalid[i];
                if (field==1) tx.tone_amp=invalid[i];
                if (field==2) tx.f_dev_hz=invalid[i];
                if (field==3) tx.out_scale=invalid[i];
                assert_invalid_tx_config(&sys,radio,&tx);
            }
        }
        tx_params_t tx=tx_defaults; tx.rf_fs=3000000;
        assert_invalid_tx_config(&sys,radio,&tx);
        tx=tx_defaults; tx.tx_power_dbm=channel ? -13 : -15;
        assert_invalid_tx_config(&sys,radio,&tx);
        tx=tx_defaults; tx.tx_power_dbm=15;
        assert_invalid_tx_config(&sys,radio,&tx);
        tx=tx_defaults; tx.tone_hz=24000;
        assert_invalid_tx_config(&sys,radio,&tx);
        tx=tx_defaults; tx.tone_amp=1.01f;
        assert_invalid_tx_config(&sys,radio,&tx);
        tx=tx_defaults; tx.f_dev_hz=24001;
        assert_invalid_tx_config(&sys,radio,&tx);
        tx=tx_defaults; tx.out_scale=32768;
        assert_invalid_tx_config(&sys,radio,&tx);
        tx=tx_defaults; tx.pacing_mode=(tx_pacing_mode_t)99;
        assert_invalid_tx_config(&sys,radio,&tx);
    }
    verify_deviation_wiring=false;

    /* Failed tuning must unwind initialized resources and never enable RX;
     * it used to be ignored by RX initialization. A later retry succeeds. */
    rx_pipeline_t pipeline={0};
    unsigned before_activations=rx_activations;
    fail_tune=true;
    assert(rx_pipeline_init(&pipeline,&sys,&sys.radio_high,&rx_defaults)<0);
    assert(rx_activations==before_activations && !hardware_active);
    check_clean(&pipeline);
    fail_tune=false;
    assert(rx_pipeline_init(&pipeline,&sys,&sys.radio_high,&rx_defaults)==0);
    rx_pipeline_destroy(&pipeline);
    check_clean(&pipeline);
    puts("PASS: invalid RX/TX settings and retunes rejected before side effects; live setters preserve valid settings; tuning failures unwind and retry");
}

int main(void) {
    test_config_validation_before_side_effects();
    test_rssi_capture();
    test_shared_rx_stats();
    test_modem_policy();
    test_rx_deviation_wiring();
    test_noise_level_controls();
    sys_st sys = {0};
    sys.board_info.numeric_product_id = system_type_cariboulite_full;
    sys.radio_low.sys = sys.radio_high.sys = &sys;
    sys.radio_low.type = cariboulite_channel_s1g;
    sys.radio_high.type = cariboulite_channel_hif;
    sys.board_info.numeric_product_id = system_type_cariboulite_full;
    double parsed=123;
    assert(monitor_parse_frequency("430.125",&sys.radio_high,&parsed) && parsed==430125000);
    assert(monitor_parse_frequency(" 145.500 ",&sys.radio_high,&parsed) && parsed==145500000);
    const char* invalid[]={"", "nan", "inf", "-1", "0", "6000", "430foo", "1e999", "430 100"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        assert(!monitor_parse_frequency(invalid[i],&sys.radio_high,&parsed));
        assert(parsed==145500000);
    }
    sys.board_info.numeric_product_id = system_type_cariboulite_ism;
    assert(!monitor_parse_frequency("430",&sys.radio_high,&parsed) && parsed==145500000);
    assert(monitor_parse_frequency("2385",&sys.radio_high,&parsed) && parsed==2385000000);
    assert(monitor_parse_frequency("2495",&sys.radio_high,&parsed) && parsed==2495000000);
    assert(!monitor_parse_frequency("2495.001",&sys.radio_high,&parsed) && parsed==2495000000);
    // The S1G connector has the same two native bands on both board variants.
    for(unsigned board=0;board<2;++board) {
        sys.board_info.numeric_product_id = board ? system_type_cariboulite_full : system_type_cariboulite_ism;
        assert(monitor_parse_frequency("377",&sys.radio_low,&parsed) && parsed==CARIBOULITE_S1G_MIN1);
        assert(monitor_parse_frequency("530",&sys.radio_low,&parsed) && parsed==CARIBOULITE_S1G_MAX1);
        assert(monitor_parse_frequency("779",&sys.radio_low,&parsed) && parsed==CARIBOULITE_S1G_MIN2);
        assert(monitor_parse_frequency("1020",&sys.radio_low,&parsed) && parsed==CARIBOULITE_S1G_MAX2);
        assert(monitor_parse_frequency(" 430.125 \t",&sys.radio_low,&parsed) && parsed==430125000);
        const char* invalid_s1g[]={"376.999", "530.001", "600", "778.999", "1020.001", "2385"};
        for(unsigned i=0;i<sizeof(invalid_s1g)/sizeof(*invalid_s1g);++i) {
            assert(!monitor_parse_frequency(invalid_s1g[i],&sys.radio_low,&parsed));
            assert(parsed==430125000);
        }
        for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
            assert(!monitor_parse_frequency(invalid[i],&sys.radio_low,&parsed));
            assert(parsed==430125000);
        }
    }
    cariboulite_radio_state_st radio = {.sys=&sys, .type=cariboulite_channel_s1g};
    rx_pipeline_t p;
    rx_params_t par = {.freq_hz=430100000,.pcm_dev="null", .fs_rf=4000000, .fs_audio=48000};
    assert(rx_pipeline_init(&p,&sys,&radio,&par) == 0);
    assert(atomic_load(&p.demod.squelch_flags)==RX_SQUELCH_NOISE);
    rx_pipeline_set_squelch(&p,false,true);
    assert(atomic_load(&p.demod.squelch_flags)==RX_SQUELCH_CARRIER);
    rx_pipeline_set_squelch(&p,false,false);
    assert(atomic_load(&p.demod.squelch_flags)==0);
    rx_pipeline_destroy(&p); check_clean(&p); /* no reader ever created */
    assert(rx_pipeline_init(&p,&sys,&radio,&par) == 0);
    rx_pipeline_set_squelch(&p,false,true);
    for (int i=0; i<20; ++i) {
        assert(rx_pipeline_start(&p) == 0);
        rx_pipeline_stop(&p); rx_pipeline_stop(&p);
        assert(atomic_load(&p.demod.squelch_flags)==RX_SQUELCH_CARRIER);
    }
    rx_pipeline_destroy(&p); check_clean(&p);
    assert(rx_pipeline_init(&p,&sys,&radio,&par) == 0);
    assert(rx_pipeline_start(&p) == 0);
    rx_pipeline_destroy(&p); check_clean(&p); /* destroy while running */
    for (int failure=1; failure<=2; ++failure) {
        fail_create = creates + failure;
        assert(rx_pipeline_init(&p,&sys,&radio,&par) < 0);
        check_clean(&p); fail_create = 0;
    }
    fail_calloc = true;
    assert(rx_pipeline_init(&p,&sys,&radio,&par) < 0);
    fail_calloc = false; check_clean(&p);
    assert(rx_pipeline_init(&p,&sys,&radio,&par) == 0);
    fail_malloc = true;
    assert(rx_pipeline_start(&p) < 0 && !hardware_active);
    fail_malloc = false;
    fail_create = creates + 1;
    assert(rx_pipeline_start(&p) < 0 && !hardware_active);
    fail_create = 0;
    assert(rx_pipeline_start(&p) == 0); /* recover after failed start */
    rx_pipeline_destroy(&p); check_clean(&p);
    for(unsigned rate=0;rate<sizeof(monitor_rates)/sizeof(*monitor_rates);++rate) {
        par.fs_rf=monitor_rates[rate].fs;
        assert(rx_pipeline_init(&p,&sys,&radio,&par)==0);
        assert(rx_pipeline_start(&p)==0);
        assert(p.rx_ctrl.rx_buffer_size==monitor_rates[rate].frame_samples);
        assert(test_rx_rate==monitor_rates[rate].fs && rate_rx_radio==&radio);
        rx_pipeline_destroy(&p);check_clean(&p);
    }
    par.fs_rf=4000000;
    assert(rx_pipeline_init(&p,&sys,&sys.radio_high,&par)==0);
    assert(rx_pipeline_start(&p)==0);
    assert(last_stream==smi_stream_rx_channel_1);
    rx_pipeline_destroy(&p);check_clean(&p);
    assert(rx_pipeline_init(&p,&sys,&sys.radio_low,&par)==0);
    assert(rx_pipeline_start(&p)==0);
    assert(last_stream==smi_stream_rx_channel_0);
    rx_pipeline_destroy(&p);check_clean(&p);
    par.pcm_dev = "cariboulite_test_missing_device";
    assert(rx_pipeline_init(&p,&sys,&radio,&par) < 0); check_clean(&p);

    tx_pipeline_t tx;
    tx_params_t tp = {.freq_hz=430100000,.tone_mode=true, .f_dev_hz=2500, .out_scale=4000};
    for (int test=0;test<7;++test) {
        if(test==1) fail_create=creates+1;
        if(test==2) fail_create=creates+2;
        if(test==3) fail_calloc=true;
        if(test==4) fail_calloc_count=480;
        if(test==5) fail_calloc_count=40000;
        if(test==6) tp.mic_dev="cariboulite_test_missing_device";
        int ret=tx_pipeline_init(&tx,&sys,&radio,&tp);
        assert(test==0 ? ret==0 : ret<0);
        tx_pipeline_destroy(&tx); tx_pipeline_destroy(&tx);
        assert(!tx.inited && !tx.running);
        assert(!tx.tx_ctrl.mic && !tx.tx_ctrl.fm && !tx.tx_ctrl.a48k && !tx.tx_ctrl.iq_rf);
        for(int i=1;i<=creates;++i) assert(!live[i]);
        fail_create=0;fail_calloc=false;fail_calloc_count=0;
    }
    // Shared tuner must be restored on every direction start, including after
    // both initializers have tuned it, and tuning failures must block activation.
    for(unsigned rate=0;rate<sizeof(monitor_rates)/sizeof(*monitor_rates);++rate) {
        unsigned fs=monitor_rates[rate].fs;
        forced_tx_gap=monitor_rates[rate].tx_gap;
        unsigned before_gap_reads=tx_gap_reads;
        tx_params_t split_tx={.tone_mode=true,.f_dev_hz=2500,.out_scale=4000,
            .freq_hz=430125000,.rf_fs=fs};
        rx_params_t split_rx={.pcm_dev="null",.freq_hz=868500000,.fs_rf=fs,.fs_audio=48000};
        assert(monitor_init_pipelines(&tx,&p,&sys,&sys.radio_high,&split_tx,&split_rx));
        assert(tx.radio==&sys.radio_high && p.radio==&sys.radio_high);
        assert(tx.tx_ctrl.radio==&sys.radio_high && tx.dsp_ctrl.tx==&tx.tx_ctrl);
        assert(p.rx_ctrl.radio==&sys.radio_high);
        assert(tx_pipeline_frame_samples(&tx)==monitor_rates[rate].frame_samples);
        assert(tx.tx_ctrl.tail_padding_frames==monitor_tail_padding[rate]);
        assert(rx_pipeline_frame_samples(&p)==monitor_rates[rate].frame_samples);
        assert(test_tx_rate==fs && rate_tx_radio==&sys.radio_high);
        assert(tx_gap_reads==before_gap_reads+1);
        assert(split_tx.freq_hz==430125000 && split_rx.freq_hz==868500000);
        assert(tuned_radio==&sys.radio_high && tuned_frequency==split_rx.freq_hz);
        pthread_t consumer;
        assert(__real_pthread_create(&consumer,NULL,consume_injection,&tx)==0);
        assert(monitor_start_tx(&tx,&p,&split_tx)==0);
        assert(tx.running && !p.running && tuned_frequency==split_tx.freq_hz);
        assert(tuned_radio==&sys.radio_high && activated_radio==&sys.radio_high);
        assert(last_stream==smi_stream_tx_channel);
        assert(monitor_start_rx(&tx,&p,&split_rx)==0);
        assert(p.running && !tx.running && tuned_frequency==split_rx.freq_hz);
        assert(tuned_radio==&sys.radio_high && activated_radio==&sys.radio_high);
        assert(last_stream==smi_stream_rx_channel_1 && p.rx_ctrl.radio==&sys.radio_high);
        assert(test_rx_rate==fs && rate_rx_radio==&sys.radio_high);
        fail_tune=true;
        assert(monitor_start_tx(&tx,&p,&split_tx)!=0);
        assert(!tx.running && !p.running && !hardware_active);
        assert(monitor_start_rx(&tx,&p,&split_rx)!=0);
        assert(!p.running && !hardware_active);
        fail_tune=false;
        assert(monitor_start_rx(&tx,&p,&split_rx)==0);
        assert(tuned_frequency==split_rx.freq_hz);
        assert(last_stream==smi_stream_rx_channel_1);
        assert(__real_pthread_cancel(consumer)==0);
        assert(__real_pthread_join(consumer,NULL)==0);
        rx_pipeline_destroy(&p); tx_pipeline_destroy(&tx);
        check_clean(&p);
    }
    forced_tx_gap=-1;
    tp.mic_dev=NULL;
    assert(tx_pipeline_init(&tx,&sys,&radio,&tp)==0);
    tx_pipeline_destroy(&tx);

    for(unsigned rate=0;rate<sizeof(monitor_rates)/sizeof(*monitor_rates);++rate) {
        unsigned fs=monitor_rates[rate].fs;
        forced_tx_gap=monitor_rates[rate].tx_gap;
        tp.rf_fs=fs;
        assert(tx_pipeline_init(&tx,&sys,&radio,&tp)==0);
        assert(tx.tx_ctrl.frame_samples==monitor_rates[rate].frame_samples);
        assert(tx.tx_ctrl.tail_padding_frames==monitor_tail_padding[rate]);
        assert(test_tx_rate==fs);
        float audio[480]={0};
        nbfm_push_audio(tx.tx_ctrl.fm,audio,480);
        assert(nbfm_pull_iq(tx.tx_ctrl.fm,tx.tx_ctrl.iq_rf,fs/100)==fs/100);
        tx_pipeline_destroy(&tx);
    }
    // Tail padding accounts for the kernel FIFO and cyclic DMA, and falls back
    // conservatively when old drivers cannot report valid buffer information.
    forced_tx_gap=-1;
    const struct {
        size_t native;
        int multiplier;
        bool query_error;
        unsigned rate;
        unsigned expected_frames;
    } padding_cases[] = {
        {131072, 2, false, 1000000, 43},
        {131072, 2, false, 2000000, 25},
        {131072, 6, false, 1000000, 122},
        {131072, 6, false, 2000000, 61},
        {131072, 6, false, 4000000, 31},
        {131072, 32, false, 1000000, 436},
        {131072, 32, false, 2000000, 218},
        {131072, 32, false, 4000000, 109},
        {131072, 16, true, 1000000, 436},
        {131072, 0, false, 1000000, 436},
        {131072, 1, false, 1000000, 436},
        {131072, 33, false, 1000000, 436},
        {131072, -1, false, 1000000, 436},
        {0, 16, false, 1000000, 227},
        {65536, 16, false, 1000000, 114},
        {4096, 2, false, 1000000, 25}
    };
    for(unsigned i=0;i<sizeof(padding_cases)/sizeof(*padding_cases);++i) {
        native_batch_samples=padding_cases[i].native;
        fifo_multiplier=padding_cases[i].multiplier;
        fail_fifo_query=padding_cases[i].query_error;
        tp.rf_fs=padding_cases[i].rate;
        unsigned before_native=native_batch_reads, before_fifo=fifo_multiplier_reads;
        assert(tx_pipeline_init(&tx,&sys,&radio,&tp)==0);
        assert(tx.tx_ctrl.tail_padding_frames==padding_cases[i].expected_frames);
        assert(native_batch_reads==before_native+1 && fifo_multiplier_reads==before_fifo+1);
        tx_pipeline_destroy(&tx);
        assert(!tx.inited && !tx.running && !hardware_active);
        for(int j=1;j<=creates;++j) assert(!live[j]);
    }
    native_batch_samples=131072;
    fifo_multiplier=16;
    fail_fifo_query=false;
    // At 1 MS/s, stale gaps from the 2/4 MS/s settings must block TX startup.
    tp.rf_fs=1000000;
    for(forced_tx_gap=0;forced_tx_gap<=1;++forced_tx_gap) {
        int before_creates=creates;
        unsigned before_gap_reads=tx_gap_reads;
        assert(tx_pipeline_init(&tx,&sys,&radio,&tp)<0);
        assert(tx_gap_reads==before_gap_reads+1 && creates==before_creates);
        assert(!tx.inited && !tx.running && !hardware_active);
        assert(!tx.tx_ctrl.fm && !tx.tx_ctrl.a48k && !tx.tx_ctrl.iq_rf);
        tx_pipeline_destroy(&tx);
    }
    forced_tx_gap=-1;
    tp.rf_fs=0;

    par.pcm_dev="null";
    fail_demod_create=true;
    assert(rx_pipeline_init(&p,&sys,&radio,&par)<0); check_clean(&p);
    fail_demod_create=false;
    for(int failure=1;failure<=4;++failure) {
        rx_pipeline_t rx={0};
        fail_create=creates+failure;
        assert(!monitor_init_pipelines(&tx,&rx,&sys,&sys.radio_high,&tp,&par));
        assert(!tx.inited && !rx.inited);
        for(int i=1;i<=creates;++i) assert(!live[i]);
        fail_create=0;
    }
    assert(monitor_init_pipelines(&tx,&p,&sys,&sys.radio_high,&tp,&par));
    monitor_loopback_t loopback = {0};
    nbfm_demod_t* original = p.demod.dsp;
    int original_creates = creates;
    for (int busy=0; busy<4; ++busy) {
        tx.running = busy==0; p.running = busy==1;
        loopback.armed = busy==2; loopback.active = busy==3;
        assert(monitor_cycle_rx_mode(&tx,&p,&loopback,&sys,&par)==-EBUSY);
        assert(par.mode==FM_MODE_NBFM && p.demod.dsp==original && creates==original_creates);
    }
    tx.running = p.running = loopback.armed = loopback.active = false;
    for (unsigned rate=0; rate<sizeof(monitor_rates)/sizeof(*monitor_rates); ++rate) {
        par.fs_rf = monitor_rates[rate].fs;
        rx_params_t saved=par;
        assert(monitor_cycle_rx_mode(&tx,&p,&loopback,&sys,&par)==0);
        assert(par.mode==FM_MODE_WBFM && p.demod.mode==FM_MODE_WBFM && !p.running);
        assert(p.radio==&sys.radio_high && p.rx_ctrl.radio==&sys.radio_high);
        assert(tx.radio==&sys.radio_high && tx.tx_ctrl.radio==&sys.radio_high);
        assert(par.freq_hz==saved.freq_hz && par.pcm_dev==saved.pcm_dev &&
               par.pcm_gain==saved.pcm_gain && par.deemph_tau_s==saved.deemph_tau_s &&
               par.fs_rf==saved.fs_rf && par.fs_audio==saved.fs_audio &&
               par.noise_squelch_disabled==saved.noise_squelch_disabled &&
               par.carrier_squelch_enabled==saved.carrier_squelch_enabled);
        assert(p.demod.pcm_gain==saved.pcm_gain && p.demod.deemph_tau==saved.deemph_tau_s);
        assert(!audio_demod_capabilities(p.demod.dsp));
        rx_pipeline_set_squelch(&p,true,true);
        assert(atomic_load(&p.demod.squelch_flags)==RX_SQUELCH_CARRIER);
        assert(monitor_start_rx(&tx,&p,&par)==0);
        assert(last_stream==smi_stream_rx_channel_1 && activated_radio==&sys.radio_high);
        assert(p.rx_ctrl.rx_buffer_size==monitor_rates[rate].frame_samples);
        assert(test_rx_rate==monitor_rates[rate].fs && rate_rx_radio==&sys.radio_high);
        rx_pipeline_stop(&p);
        assert(monitor_cycle_rx_mode(&tx,&p,&loopback,&sys,&par)==0);
        assert(par.mode==FM_MODE_NBFM && p.demod.mode==FM_MODE_NBFM && !p.running);
        assert(p.radio==&sys.radio_high && p.rx_ctrl.radio==&sys.radio_high);
        assert(atomic_load(&p.demod.squelch_flags)==RX_SQUELCH_NOISE);
        assert(audio_demod_capabilities(p.demod.dsp)==AUDIO_DEMOD_CAP_NOISE_SQUELCH);
        assert(par.freq_hz==saved.freq_hz && par.pcm_gain==saved.pcm_gain &&
               par.deemph_tau_s==saved.deemph_tau_s && par.fs_rf==saved.fs_rf);
        assert(monitor_start_rx(&tx,&p,&par)==0);
        assert(last_stream==smi_stream_rx_channel_1 && activated_radio==&sys.radio_high);
        assert(p.rx_ctrl.rx_buffer_size==monitor_rates[rate].frame_samples);
        assert(test_rx_rate==monitor_rates[rate].fs && rate_rx_radio==&sys.radio_high);
        rx_pipeline_stop(&p);
    }
    fail_calloc = true;
    assert(monitor_cycle_rx_mode(&tx,&p,&loopback,&sys,&par)!=0);
    assert(!p.inited && par.mode==FM_MODE_NBFM);
    fail_calloc = false;
    rx_pipeline_destroy(&p);tx_pipeline_destroy(&tx);

    /* Empty reads/full writes must honor the requested monotonic deadline. */
    for (int which=0; which<4; ++which) {
        aud10_fifo_init(&audio,1); rf10_fifo_init(&rf,1,false);
        aud10_frame_t af = {0}; rf10_frame_t frame = {0};
        if (which==1) audio.count=1;
        if (which==3) rf.count=1;
        struct timespec begin,end;
        clock_gettime(CLOCK_MONOTONIC,&begin);
        bool ok = which==0 ? aud10_fifo_get(&audio,&af,100) :
                  which==1 ? aud10_fifo_put(&audio,&af,100) :
                  which==2 ? rf10_fifo_get(&rf,&frame,100) :
                             rf10_fifo_put(&rf,&frame,100);
        clock_gettime(CLOCK_MONOTONIC,&end);
        double elapsed = (end.tv_sec-begin.tv_sec)*1000.0 +
                         (end.tv_nsec-begin.tv_nsec)/1000000.0;
        assert(!ok && elapsed>=90 && elapsed<2000);
        printf("FIFO timed wait %d: %.1f ms\n",which,elapsed);
        aud10_fifo_destroy(&audio); rf10_fifo_destroy(&rf);
    }

    real_threads = true;
    test_audio_worker_exits();
    // Actual waiting DSP/writer threads must join before DSP/sink destruction.
    assert(rx_pipeline_init(&p,&sys,&radio,&par)==0);
    for (unsigned retry=0; retry<1000 && (!atomic_load(&p.demod.alive) || !atomic_load(&p.aw.alive)); ++retry)
        usleep(1000);
    rx_pipeline_stats_t active_stats;
    rx_pipeline_get_stats(&p,&active_stats);
    assert(active_stats.demod_worker.alive && active_stats.audio_worker.alive);
    assert(active_stats.demod_worker.scheduling.valid && active_stats.audio_worker.scheduling.valid);
    rx_pipeline_destroy(&p); check_clean(&p);
    assert(!atomic_load(&p.demod.alive) && !atomic_load(&p.aw.alive));

    for (int which=0; which<4; ++which) {
        aud10_fifo_init(&audio,1); rf10_fifo_init(&rf,1,false);
        if (which==1) audio.count=1;
        if (which==3) rf.count=1;
        pthread_t t; void* result;
        assert(pthread_create(&t,NULL,waiter,&which)==0);
        assert(pthread_cancel(t)==0);
        assert(pthread_join(t,&result)==0 && result==PTHREAD_CANCELED);
        assert(pthread_mutex_trylock(&audio.m)==0); pthread_mutex_unlock(&audio.m);
        assert(pthread_mutex_trylock(&rf.m)==0); pthread_mutex_unlock(&rf.m);
        aud10_fifo_destroy(&audio); rf10_fifo_destroy(&rf);
    }
    cariboulite_sample_complex_int16 samples[40000];
    radio.sys = &sys;
    rx_reader_ctrl_st reader = {.active=true, .radio=&radio, .rx_buffer=samples,.rx_buffer_size=40000};
    atomic_init(&reader.alive,false);
    pipeline_thread_diagnostics_init(&reader.scheduling);
    atomic_init(&reader.read_calls,0); atomic_init(&reader.read_samples,0);
    atomic_init(&reader.short_reads,0); atomic_init(&reader.zero_reads,0);
    atomic_init(&reader.read_errors,0); atomic_init(&reader.last_error,0);
    pthread_t thread;
    assert(pthread_barrier_init(&reader_ready,NULL,2)==0);
    track_metadata = true;
    assert(pthread_create(&thread,NULL,rx_reader_thread_func,&reader)==0);
    pthread_barrier_wait(&reader_ready);
    assert(metadata_allocation != NULL);
    assert(atomic_load(&reader.alive) && atomic_load(&reader.scheduling.ready));
    assert(pthread_cancel(thread)==0);
    assert(pthread_join(thread,NULL)==0);
    assert(metadata_allocation == NULL);
    assert(!atomic_load(&reader.alive));
    track_metadata = false;
    pthread_barrier_destroy(&reader_ready);
    puts("PASS: S1G/HiF routing/rates/modes, modem policy, TX/RX lifecycle, shared FIFO/playback/reader stats, worker errors and cancellation");
}
