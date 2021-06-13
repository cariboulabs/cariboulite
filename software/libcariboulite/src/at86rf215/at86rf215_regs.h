#ifndef _AT86RF215_REGS_H_
#define _AT86RF215_REGS_H_

/*!
 * ============================================================================
 * AT86RF215 Internal registers Address
 * ============================================================================
 */

/* Common settings */
#define REG_RF_RST                          0x0005

/* Interrupt registers */
#define REG_RF_CFG                          0x0006
#define REG_RF09_IRQS                       0x0000
#define REG_RF24_IRQS                       0x0001
#define REG_BBC0_IRQS                       0x0002
#define REG_BBC1_IRQS                       0x0003

#define REG_RF09_IRQM                       0x0100
#define REG_RF24_IRQM                       0x0200
#define REG_BBC0_IRQM                       0x0300
#define REG_BBC1_IRQM                       0x0400

/* RF interrupt register values */
#define RF_IRQM_WAKEUP                 		0x00
#define RF_IRQM_TRXRDY                 		0x01
#define RF_IRQM_EDC                    		0x02
#define RF_IRQM_BATLOW                 		0x03
#define RF_IRQM_TRXERR                 		0x04
#define RF_IRQM_IQIFSF               		0x05

/* BB interrupt register values */
#define BB_INTR_RXFS                        0x01
#define BB_INTR_RXFE                        0x02
#define BB_INTR_RXAM                        0x04
#define BB_INTR_RXEM                        0x08
#define BB_INTR_TXFE                        0x10
#define BB_INTR_AGCH                        0x20
#define BB_INTR_AGCR                        0x40
#define BB_INTR_FBLI                        0x80

#define REG_RF_IQIFC0                       0x000A
#define REG_RF_IQIFC1                       0x000B
#define REG_RF_IQIFC2                       0x000C

/* Baseband registers */
#define REG_BBC0_TXFLL                      0x0306
#define REG_BBC0_TXFLH                      0x0307
#define REG_BBC0_FBTXS                      0x2800
#define REG_BBC0_FBTXE                      0x2FFE
#define REG_BBC0_FSKPHRTX                   0x036A
#define REG_BBC0_PC                         0x0301
#define REG_BBC0_FSKDM                      0x0372

#define REG_BBC1_TXFLL                      0x0406
#define REG_BBC1_TXFLH                      0x0407
#define REG_BBC1_FBTXS                      0x3800
#define REG_BBC1_FBTXE                      0x3FFE
#define REG_BBC1_FSKPHRTX                   0x046A
#define REG_BBC1_TXDFE                      0x0113
#define REG_BBC1_PC                         0x0401
#define REG_BBC1_FSKDM                      0x0372

/* Common RF */
#define REG_RF09_PLLCF                      0x0122
#define REG_RF_CLKO							0x0007
#define RF_CLKO_OFF					        0x00
typedef enum
{
    at86rf215_drive_current_2ma = 0,
    at86rf215_drive_current_4ma = 1,
    at86rf215_drive_current_6ma = 2,
    at86rf215_drive_current_8ma = 3,
} at86rf215_drive_current_en;

typedef enum
{
    at86rf215_clock_out_freq_off = 0,
    at86rf215_clock_out_freq_26mhz = 1,
    at86rf215_clock_out_freq_32mhz = 2,
    at86rf215_clock_out_freq_16mhz = 3,
    at86rf215_clock_out_freq_8mhz = 4,
    at86rf215_clock_out_freq_4mhz = 5,
    at86rf215_clock_out_freq_2mhz = 6,
    at86rf215_clock_out_freq_1mhz = 7,
} at86rf215_clock_out_freq_en;

/* bandwidth */
#define REG_RF09_RXBWC                      0x0109
#define REG_RF24_RXBWC                      0x0209

/* Front end FE Pads */
#define REG_RF09_PADFE			            0x0116
#define REG_RF24_PADFE			            0x0216

/* different bandwidth values */
#define RF_BW160KHZ_IF250KHZ                0x00
#define RF_BW200KHZ_IF250KHZ                0x01
#define RF_BW250KHZ_IF250KHZ                0x02
#define RF_BW320KHZ_IF500KHZ                0x03
#define RF_BW400KHZ_IF500KHZ                0x04
#define RF_BW500KHZ_IF500KHZ                0x05
#define RF_BW630KHZ_IF1000KHZ               0x06
#define RF_BW800KHZ_IF1000KHZ               0x07
#define RF_BW1000KHZ_IF1000KHZ              0x08
#define RF_BW1250KHZ_IF2000KHZ              0x09
#define RF_BW1600KHZ_IF2000KHZ              0x0A
#define RF_BW2000KHZ_IF2000KHZ              0x0B

/* different Skew values */
#define RF_IQ_SKEW_pos_2		            0x00
#define RF_IQ_SKEW_pos_1		            0x01
#define RF_IQ_SKEW_zero			            0x02
#define RF_IQ_SKEW_neg_1		            0x03

/* CMV values */
#define RF_IQ_LVDS_CMV150		            0x00
#define RF_IQ_LVDS_CMV200		            0x01
#define RF_IQ_LVDS_CMV250		            0x02
#define RF_IQ_LVDS_CMV300		            0x03

/* IQ drive current */
#define RF_IQ_DRV_Current_1mA	            0x00
#define RF_IQ_DRV_Current_2mA	            0x01
#define RF_IQ_DRV_Current_3mA	            0x02
#define RF_IQ_DRV_Current_4mA	            0x03

/* different IFS values */
#define RX_IFS_Deactive				        0x00
#define RX_IFS_Active				        0x01

/* sampling rate */
#define REG_RF09_RXDFE                      0x010A
#define REG_RF24_RXDFE                      0x020A

/* different sampling rate values */
#define RF_SR4000                           0x01
#define RF_SR2000                           0x02
#define RF_SR1333                           0x03
#define RF_SR1000                           0x04
#define RF_SR800                            0x05
#define RF_SR666                            0x06
#define RF_SR500                            0x08
#define RF_SR400                            0x0A

/* RX filter relative cut-off frequency */
#define RF_CUT_1_4                          0x00
#define RF_CUT_3_8                          0x01
#define RF_CUT_1_2                          0x02
#define RF_CUT_3_4                          0x03
#define RF_CUT_4_4                          0x04

/* AGC */
#define REG_RF09_AGCC                       0x010B
#define REG_RF09_AGCS                       0x010C
#define REG_RF24_AGCC                       0x020B
#define REG_RF24_AGCS                       0x020C

/* AGC values */

/* AGC average sampling */
#define RF_AGC_AVGS_8		                0x00
#define RF_AGC_AVGS_16		                0x01
#define RF_AGC_AVGS_32		                0x02
#define RF_AGC_AVGS_64		                0x03

/* Chip mode */
#define RF_MODE_BBRF                        0x00
#define RF_MODE_RF                          0x01
#define RF_MODE_BBRF09                      0x04
#define RF_MODE_BBRF24                      0x05

/* Transceiver operation commands */
#define RF_CMD_NOP                          0x00
#define RF_CMD_SLEEP                        0x01
#define RF_CMD_TRXOFF                       0x02
#define RF_CMD_TXPREP                       0x03
#define RF_CMD_TX                           0x04
#define RF_CMD_RX                           0x05
#define RF_CMD_RESET                        0x07

/* Transceiver Baseband PHY type, combined BBEN and PT part of PHY Control register */
#define BB_PHY_PHYOFF                       0x00
#define BB_PHY_FSK                          0x05
#define BB_PHY_OFDM                         0x06
#define BB_PHY_OQPSK                        0x07

/* Transceiver interface mode */
#define RF_MODE_BBRF                        0x00
#define RF_MODE_RF                          0x01
#define RF_MODE_BBRF09                      0x04
#define RF_MODE_BBRF24                      0x05

/* Oscillator settings */
#define REG_RF_XOC				            0x0009

/* Command */

/* RF09 Radio */
#define REG_RF09_AUXS                      	0x0101
#define REG_RF09_STATE                      0x0102
#define REG_RF09_CMD                        0x0103
#define REG_RF09_PAC                        0x0114
#define REG_RF09_TXDFE                      0x0113

/* RF24 Radio */
#define REG_RF24_AUXS                      	0x0201
#define REG_RF24_CMD                        0x0203
#define REG_RF24_STATE                      0x0202
#define REG_RF24_PAC                        0x0214
#define REG_RF24_TXDFE                      0x0213

/* power levels */
#define RF_TXPWR_00							0x00		//-21.4dBm
#define RF_TXPWR_01							0x01		//-20.4dBm
#define RF_TXPWR_10							0x10		//
#define RF_TXPWR_MAX						0x1F		//

/* power amplifier current reduction */
#define RF_PAC_3dB_Reduction				0x00
#define RF_PAC_2dB_Reduction				0x01
#define RF_PAC_1dB_Reduction				0x02
#define RF_PAC_0dB_Reduction				0x03

/* power amplifier DC voltage */
#define RF_PA_VC_2_0						0x00
#define RF_PA_VC_2_2						0x01
#define RF_PA_VC_2_4						0x02

/* frequency registers */
/* 900MHz */
#define REG_RF09_CS                         0x0104
#define REG_RF09_CCF0L                      0x0105
#define REG_RF09_CCF0H                      0x0106
#define REG_RF09_CNL                        0x0107
#define REG_RF09_CNM                        0x0108
/* 2400MHz */
#define REG_RF24_CS                         0x0204
#define REG_RF24_CCF0L                      0x0205
#define REG_RF24_CCF0H                      0x0206
#define REG_RF24_CNL                        0x0207
#define REG_RF24_CNM                       	0x0208

/* Calibration registers */
/* RF09 Interface */
#define REG_RF09_TXCI						0x0125
#define REG_RF09_TXCQ						0x0126

/* RF24 Interface */
#define REG_RF24_TXCI						0x0225
#define REG_RF24_TXCQ						0x0226

/* RF24 Radio */
#define REG_RF24_CMD                        0x0203

/* I/O settings */

/* Version */
#define REG_RF_VN                           0x000E
#define REG_RF_PN							0x000D

typedef enum
{
    at86rf215_pn_at86rf215 = 0x34,
    at86rf215_pn_at86rf215iq = 0x35,
    at86rf215_pn_at86rf215m = 0x36,
} at86rf215_pn_en;

/* Additional settings */

/*!
 * ============================================================================
 * Timing Definitions
 * Timings are define in nano seconds
 * Page 189 datasheet
 * ============================================================================
 */
#define T_TRXOFF_TXPREP_ns			200
#define T_TXPREP_TRXOFF_ns			200
#define	T_TXPREP_TX_ns				200
#define T_TXPREP_RX_ns				200
#define T_TX_TXPREP_TXFE_ns			200
#define T_TX_TXPREP_CMD_us			33			//assuming PA ramp 3
#define T_RX_TXPREP_ns				200
#define T_TX_Start_Delay_us			4


#endif
