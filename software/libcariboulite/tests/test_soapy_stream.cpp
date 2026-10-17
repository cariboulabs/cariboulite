// Actual Soapy adapter with a simulated radio; no device is opened.
#include "soapy_api/Cariboulite.hpp"
#include <SoapySDR/Errors.h>
#include <cassert>
#include <chrono>
#include <cstdio>

sys_st SoapyCaribouliteSession::sys = {};
SoapyCaribouliteSession::SoapyCaribouliteSession() {}
SoapyCaribouliteSession::~SoapyCaribouliteSession() {}
static int transfers, result, activation_result, activations;
static long timeout_seen;
extern "C" size_t __wrap_cariboulite_radio_get_native_mtu_size_samples(cariboulite_radio_state_st*) { return 64; }
extern "C" int __wrap_cariboulite_radio_set_cw_outputs(cariboulite_radio_state_st*, bool, bool) { return 0; }
extern "C" int __wrap_cariboulite_radio_activate_channel(cariboulite_radio_state_st*, cariboulite_channel_dir_en, bool) {
    ++activations; return activation_result;
}
extern "C" int __wrap_cariboulite_radio_read_samples_timed(cariboulite_radio_state_st*, cariboulite_sample_complex_int16*, cariboulite_sample_meta*, size_t, long us) {
    ++transfers; timeout_seen=us; return result;
}
extern "C" int __wrap_cariboulite_radio_write_samples_timed(cariboulite_radio_state_st*, cariboulite_sample_complex_int16*, size_t, long us) {
    ++transfers; timeout_seen=us; return result;
}
int main() {
    Cariboulite dev({{"device_id","test"},{"label","mock"},{"channel","S1G"}});
    cariboulite_sample_complex_int16 data[64] = {};
    void *rx[] = {data}; const void *tx[] = {data};
    for (int direction : {SOAPY_SDR_RX, SOAPY_SDR_TX}) {
        auto *s=dev.setupStream(direction, SOAPY_SDR_CS16, {}, {});
        int flags=0; long long time=0;
        auto transfer=[&](long us) {
            return direction==SOAPY_SDR_RX ? dev.readStream(s,rx,64,flags,time,us)
                                          : dev.writeStream(s,tx,64,flags,0,us);
        };
        int before=transfers;
        assert(transfer(0)==SOAPY_SDR_TIMEOUT);
        auto start=std::chrono::steady_clock::now();
        assert(transfer(20000)==SOAPY_SDR_TIMEOUT);
        auto elapsed=std::chrono::steady_clock::now()-start;
        assert(elapsed>=std::chrono::milliseconds(19));
        assert(elapsed<std::chrono::seconds(1));
        assert(transfers==before);
        activation_result=-1;
        assert(dev.activateStream(s)==SOAPY_SDR_STREAM_ERROR);
        assert(transfer(0)==SOAPY_SDR_TIMEOUT && transfers==before);
        activation_result=0;
        int calls=activations;
        assert(dev.activateStream(s,SOAPY_SDR_HAS_TIME)==SOAPY_SDR_NOT_SUPPORTED);
        assert(dev.activateStream(s,0,0,32)==SOAPY_SDR_NOT_SUPPORTED);
        assert(activations==calls);
        assert(dev.activateStream(s)==0);
        for (long us : {0L, 37L, 20000L}) {
            result=7;
            assert(transfer(us)==7 && timeout_seen==us);
            result=0; assert(transfer(us)==SOAPY_SDR_TIMEOUT);
            result=-1; assert(transfer(us)==SOAPY_SDR_STREAM_ERROR);
            result=-2; assert(transfer(us)==SOAPY_SDR_STREAM_ERROR);
        }
        // Format conversion must preserve counts/errors and the latency budget.
        for (const char *fmt : {SOAPY_SDR_CS8, SOAPY_SDR_CF32, SOAPY_SDR_CF64}) {
            double storage[128] = {};
            void *converted_rx[] = {storage}; const void *converted_tx[] = {storage};
            assert(s->setFormat(fmt)==0);
            result=5;
            int r=direction==SOAPY_SDR_RX ? dev.readStream(s,converted_rx,64,flags,time,123)
                : dev.writeStream(s,converted_tx,64,flags,0,123);
            assert(r==5 && timeout_seen==123);
        }
        assert(s->setFormat(SOAPY_SDR_CS16)==0);
        if (direction==SOAPY_SDR_RX) {
            flags=SOAPY_SDR_HAS_TIME; time=123;
            result=-3; assert(transfer(0)==SOAPY_SDR_CORRUPTION);
            assert(flags==0 && time==0);
            assert(dev.writeStream(s,tx,64,flags,0,0)==SOAPY_SDR_NOT_SUPPORTED);
        } else {
            flags=SOAPY_SDR_END_BURST;
            assert(transfer(0)==SOAPY_SDR_NOT_SUPPORTED); flags=0;
            assert(dev.readStream(s,rx,64,flags,time,0)==SOAPY_SDR_NOT_SUPPORTED);
        }
        activation_result=-1;
        assert(dev.deactivateStream(s)==SOAPY_SDR_STREAM_ERROR);
        before=transfers; assert(transfer(0)==SOAPY_SDR_TIMEOUT && transfers==before);
        activation_result=0;
        assert(dev.activateStream(s)==0);
        assert(dev.deactivateStream(s)==0);
        before=transfers; assert(transfer(0)==SOAPY_SDR_TIMEOUT && transfers==before);
        assert(dev.activateStream(s)==0);
        dev.closeStream(s);
        before=transfers; assert(transfer(0)==SOAPY_SDR_TIMEOUT && transfers==before);
    }
    puts("PASS: Soapy inactive waits, activation failures, deadlines forwarded, partial counts, errors, flags, close/restart");
}
