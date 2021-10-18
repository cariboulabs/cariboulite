#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "AT86RF215_Events"

#include <stdio.h>
#include "zf_log/zf_log.h"
#include "at86rf215_common.h"

//===================================================================
static void at86rf215_radio_event_handler (at86rf215_st* dev,
                                at86rf215_rf_channel_en ch,
                                at86rf215_radio_irq_st *events)
{
    char channel_st[3];
    if (ch == at86rf215_rf_channel_900mhz) strcpy(channel_st, "09");
    else strcpy(channel_st, "24");

    if (events->wake_up_por)
    {
        ZF_LOGD("INT @ RADIO%s: Woke up", channel_st);
    }

    if (events->trx_ready)
    {
        ZF_LOGD("INT @ RADIO%s: Transceiver ready", channel_st);
    }

    if (events->energy_detection_complete)
    {
        ZF_LOGD("INT @ RADIO%s: Energy detection complete", channel_st);
    }

    if (events->battery_low)
    {
        ZF_LOGD("INT @ RADIO%s: Battery low", channel_st);
    }

    if (events->trx_error)
    {
        ZF_LOGD("INT @ RADIO%s: Transceiver error", channel_st);
    }

    if (events->IQ_if_sync_fail)
    {
        ZF_LOGD("INT @ RADIO%s: I/Q interface sync failed", channel_st);
    }
}

//===================================================================
static void at86rf215_baseband_event_handler (at86rf215_st* dev,
                                at86rf215_rf_channel_en ch,
                                at86rf215_baseband_irq_st *events)
{
    char channel_st[3];
    if (ch == at86rf215_rf_channel_900mhz) strcpy(channel_st, "09");
    else strcpy(channel_st, "24");


    if (events->frame_rx_started)
    {
        ZF_LOGD("INT @ BB%s: Frame reception started", channel_st);
    }

    if (events->frame_rx_complete)
    {
        ZF_LOGD("INT @ BB%s: Frame reception complete", channel_st);
    }

    if (events->frame_rx_address_match)
    {
        ZF_LOGD("INT @ BB%s: Frame address matched", channel_st);
    }

    if (events->frame_rx_match_extended)
    {
        ZF_LOGD("INT @ BB%s: Frame extended address matched", channel_st);
    }

    if (events->frame_tx_complete)
    {
        ZF_LOGD("INT @ BB%s: Frame transmission complete", channel_st);
    }

    if (events->agc_hold)
    {
        ZF_LOGD("INT @ BB%s: AGC hold", channel_st);
    }

    if (events->agc_release)
    {
        ZF_LOGD("INT @ BB%s: AGC released", channel_st);
    }

    if (events->frame_buffer_level)
    {
        ZF_LOGD("INT @ BB%s: Frame buffer level", channel_st);
    }
}

//===================================================================
void at86rf215_interrupt_handler (int event, int level, uint32_t tick, void *data)
{
    int i;
    at86rf215_st* dev = (at86rf215_st*)data;
    at86rf215_irq_st irq = {0};

    if (level == 0) return;
    // first read the irqs
    at86rf215_get_irqs(dev, &irq, 0);
    uint8_t *tmp = (uint8_t *)&irq;

    if (tmp[0] != 0) at86rf215_radio_event_handler (dev, at86rf215_rf_channel_900mhz, &irq.radio09);
    if (tmp[1] != 0) at86rf215_radio_event_handler (dev, at86rf215_rf_channel_2400mhz, &irq.radio24);
    if (tmp[2] != 0) at86rf215_baseband_event_handler (dev, at86rf215_rf_channel_900mhz, &irq.bb0);
    if (tmp[3] != 0) at86rf215_baseband_event_handler (dev, at86rf215_rf_channel_2400mhz, &irq.bb1);

    /*for (i=0; i<e; i++)
    {
        printf("u=%d t=%"PRIu64" c=%d g=%d l=%d f=%d (%d of %d)\n",
        dev, evt[i].report.timestamp, evt[i].report.chip,
        evt[i].report.gpio, evt[i].report.level,
        evt[i].report.flags, i+1, e);
    }*/
}
