#include <stdio.h>
#include "at86rf215_common.h"

//===================================================================
static void at86rf215_radio_event_handler (at86rf215_st* dev,
                                at86rf215_rf_channel_en ch,
                                at86rf215_radio_irq_st *events)
{
    if (events->wake_up_por)
    {
        printf("INT @ RADIO%s: Woke up\n", ch?"09":"24");
    }

    if (events->trx_ready)
    {
        printf("INT @ RADIO%s: Transceiver ready\n", ch?"09":"24");
    }

    if (events->energy_detection_complete)
    {
        printf("INT @ RADIO%s: Energy detection complete\n", ch?"09":"24");
    }

    if (events->battery_low)
    {
        printf("INT @ RADIO%s: Battery low\n", ch?"09":"24");
    }

    if (events->trx_error)
    {
        printf("INT @ RADIO%s: Transceiver error\n", ch?"09":"24");
    }

    if (events->IQ_if_sync_fail)
    {
        printf("INT @ RADIO%s: I/Q interface sync failed\n", ch?"09":"24");
    }
}

//===================================================================
static void at86rf215_baseband_event_handler (at86rf215_st* dev,
                                at86rf215_rf_channel_en ch,
                                at86rf215_baseband_irq_st *events)
{
    if (events->frame_rx_started)
    {
        printf("INT @ BB%s: Frame reception started\n", ch?"09":"24");
    }

    if (events->frame_rx_complete)
    {
        printf("INT @ BB%s: Frame reception complete\n", ch?"09":"24");
    }

    if (events->frame_rx_address_match)
    {
        printf("INT @ BB%s: Frame address matched\n", ch?"09":"24");
    }

    if (events->frame_rx_match_extended)
    {
        printf("INT @ BB%s: Frame extended address matched\n", ch?"09":"24");
    }

    if (events->frame_tx_complete)
    {
        printf("INT @ BB%s: Frame transmission complete\n", ch?"09":"24");
    }

    if (events->agc_hold)
    {
        printf("INT @ BB%s: AGC hold\n", ch?"09":"24");
    }

    if (events->agc_release)
    {
        printf("INT @ BB%s: AGC released\n", ch?"09":"24");
    }

    if (events->frame_buffer_level)
    {
        printf("INT @ BB%s: Frame buffer level\n", ch?"09":"24");
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
