#ifndef __AT86RF215_H__
#define __AT86RF215_H__

#include "at86rf215_common.h"

int at86rf215_init(at86rf215_st* dev,
					io_utils_spi_st* io_spi);
int at86rf215_close(at86rf215_st* dev);
void at86rf215_reset(at86rf215_st* dev);

void at86rf215_get_versions(at86rf215_st* dev, uint8_t *pn, uint8_t *vn);
void at86rf215_set_clock_output(at86rf215_st* dev,
                                at86rf215_drive_current_en drv_level,
                                at86rf215_clock_out_freq_en clock_val);
void at86rf215_setup_rf_irq(at86rf215_st* dev,  uint8_t active_low,
                                                uint8_t show_masked_irq,
                                                at86rf215_drive_current_en drive);
void at86rf215_set_xo_trim(at86rf215_st* dev, uint8_t fast_start, float cap_trim);
void at86rf215_get_iq_if_cfg(at86rf215_st* dev, at86rf215_iq_interface_config_st* cfg, int verbose);
void at86rf215_setup_iq_if(at86rf215_st* dev, at86rf215_iq_interface_config_st* cfg);
void at86rf215_setup_iq_radio_transmit (at86rf215_st* dev, at86rf215_rf_channel_en radio);
void at86rf215_setup_iq_radio_receive (at86rf215_st* dev, at86rf215_rf_channel_en radio);
void at86rf215_setup_iq_radio_continues_tx (at86rf215_st* dev, at86rf215_rf_channel_en radio);
void at86rf215_setup_iq_radio_dac_value_override (at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                    uint32_t freq_hz,
                                                    uint8_t tx_power );
int at86rf215_setup_channel ( at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                    uint32_t freq_hz);
#endif // __AT86RF215_H__