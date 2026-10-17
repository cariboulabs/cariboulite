#ifndef MONITOR_LOOPBACK_H
#define MONITOR_LOOPBACK_H

// AT86RF215 datasheet 4.5.7: EXTLB is for interface tests, never normal RF TX.
// Menu 14 owns the hardware and has stopped both pipelines before these calls.
typedef struct {
    bool armed;                 // Stay interlocked until cleanup succeeds.
    bool active;
    uint8_t saved_iq[2];
    caribou_smi_sample_complex_int16 samples[16];
    int count;
    unsigned long long total;
    unsigned long timeouts;
} monitor_loopback_t;

static bool monitor_loopback_blocks_control(const monitor_loopback_t* lb, int key)
{
    return lb->armed && (key == 't' || key == 'T' || key == 'r' || key == 'R' ||
                         key == '1' || key == '2' || key == '4' ||
                         key == 'f' || key == 'F' ||
                         key == 'g' || key == 'G');
}

static int monitor_lb_write_checked(sys_st* sys, uint16_t reg, uint8_t value,
                                    uint8_t mask)
{
    uint8_t actual = 0;
    if (at86rf215_write_byte(&sys->modem, reg, value) != 0 ||
        at86rf215_read_buffer(&sys->modem, reg, &actual, 1) != 0)
        return -1;
    return (actual & mask) == (value & mask) ? 0 : -1;
}

static int monitor_lb_radios_off(sys_st* sys)
{
    int failed = 0;
    failed |= at86rf215_write_byte(&sys->modem, REG_RF09_CMD, 2) != 0;
    failed |= at86rf215_write_byte(&sys->modem, REG_RF24_CMD, 2) != 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
        uint8_t low = 0, high = 0;
        if (at86rf215_read_buffer(&sys->modem, REG_RF09_STATE, &low, 1) != 0 ||
            at86rf215_read_buffer(&sys->modem, REG_RF24_STATE, &high, 1) != 0)
            return -1;
        if ((low & 7) == 2 && (high & 7) == 2) return failed ? -1 : 0;
        usleep(1000);
    }
    return -1;
}

static int monitor_loopback_stop(sys_st* sys, monitor_loopback_t* lb)
{
    if (!lb->armed) return 0;
    lb->active = false;
    lb->count = 0;
    int failed = 0;
    failed |= caribou_smi_set_driver_streaming_state(&sys->smi, smi_stream_idle) != 0;
    failed |= caribou_fpga_set_debug_loopback(&sys->fpga, false) != 0;
    // Clear EXTLB and embedded TX control before restoring any normal settings.
    failed |= monitor_lb_write_checked(sys, REG_RF_IQIFC0,
                                      lb->saved_iq[0] & ~0x81, 0xbf) != 0;
    int off = monitor_lb_radios_off(sys);
    failed |= off != 0;
    failed |= caribou_fpga_set_io_ctrl_mode(&sys->fpga, 0,
                              caribou_fpga_io_ctrl_rfm_low_power) != 0;
    if (!failed) {
        failed |= monitor_lb_write_checked(sys, REG_RF_IQIFC1,
                                          lb->saved_iq[1], 0x73) != 0;
        // Never restore an externally enabled loopback bit.
        if (!failed)
            failed |= monitor_lb_write_checked(sys, REG_RF_IQIFC0,
                                              lb->saved_iq[0] & ~0x80, 0xbf) != 0;
    }
    if (!failed) lb->armed = false;
    fprintf(stderr, "[monitor loopback] cleanup %s\n",
            failed ? "failed; controls remain locked" : "complete; radios off");
    return failed ? -1 : 0;
}

static int monitor_loopback_start(sys_st* sys, monitor_loopback_t* lb)
{
    if (lb->armed) return -1;
    if (at86rf215_read_buffer(&sys->modem, REG_RF_IQIFC0, lb->saved_iq, 2) != 0)
        return -1;
    lb->armed = true;
    lb->active = false;
    lb->count = 0;
    lb->total = 0;
    lb->timeouts = 0;
    // EEC=0 prevents the test word's embedded control bits starting RF TX.
    if (caribou_smi_set_driver_streaming_state(&sys->smi, smi_stream_idle) != 0 ||
        monitor_lb_write_checked(sys, REG_RF_IQIFC0,
                                 lb->saved_iq[0] & ~0x81, 0xbf) != 0 ||
        monitor_lb_radios_off(sys) != 0 ||
        caribou_fpga_set_debug_loopback(&sys->fpga, false) != 0 ||
        caribou_fpga_set_io_ctrl_mode(&sys->fpga, 0,
                                    caribou_fpga_io_ctrl_rfm_low_power) != 0 ||
        monitor_lb_write_checked(sys, REG_RF_IQIFC1,
                                 (lb->saved_iq[1] & ~0x70) | 0x10, 0x73) != 0 ||
        caribou_fpga_set_smi_channel(&sys->fpga, caribou_fpga_smi_channel_1) != 0 ||
        caribou_fpga_set_smi_ctrl_data_direction(&sys->fpga, 1) != 0 ||
        // RX supplies the interface clock; no TX or TX_PREP command is issued.
        at86rf215_write_byte(&sys->modem, REG_RF24_CMD, 5) != 0)
        goto fail;
    bool receiving = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        uint8_t low = 0, high = 0;
        if (at86rf215_read_buffer(&sys->modem, REG_RF09_STATE, &low, 1) != 0 ||
            at86rf215_read_buffer(&sys->modem, REG_RF24_STATE, &high, 1) != 0)
            goto fail;
        if ((low & 7) == 2 && (high & 7) == 5) { receiving = true; break; }
        usleep(1000);
    }
    if (!receiving ||
        monitor_lb_write_checked(sys, REG_RF_IQIFC0,
                                 (lb->saved_iq[0] & ~0x01) | 0x80, 0xbf) != 0 ||
        caribou_fpga_set_debug_loopback(&sys->fpga, true) != 0 ||
        caribou_smi_set_driver_streaming_state(&sys->smi, smi_stream_rx_channel_1) != 0)
        goto fail;
    lb->active = true;
    fprintf(stderr, "[monitor loopback] enabled; EEC off, RF09 off, RF24 RX, SMI RX24\n");
    return 0;
fail:
    fprintf(stderr, "[monitor loopback] setup failed; cleaning up\n");
    monitor_loopback_stop(sys, lb);
    return -1;
}

static int monitor_loopback_read(sys_st* sys, monitor_loopback_t* lb)
{
    lb->count = 0; // Never display stale samples as a fresh capture.
    if (!lb->active) return 0;
    caribou_smi_sample_complex_int16 batch[4096];
    int n = caribou_smi_read_loopback_timed(&sys->smi, caribou_smi_channel_2400,
                                 batch, 4096, 10000);
    if (n < 0) {
        fprintf(stderr, "[monitor loopback] sample read failed: %d\n", n);
        return -1;
    }
    if (n == 0) { ++lb->timeouts; return 0; }
    lb->count = n < 16 ? n : 16;
    memcpy(lb->samples, batch, lb->count * sizeof(batch[0]));
    lb->total += n;
    return 0;
}
#endif
