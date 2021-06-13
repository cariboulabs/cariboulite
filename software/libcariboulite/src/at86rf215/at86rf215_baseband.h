#ifndef __AT86RF215_BASEBAND_H__
#define __AT86RF215_BASEBAND_H__

struct at86rf215_BBC_regs
{
    uint16_t RG_IRQS;
    uint16_t RG_FBRXS;
    uint16_t RG_FBRXE;
    uint16_t RG_FBTXS;
    uint16_t RG_FBTXE;
    uint16_t RG_IRQM;
    uint16_t RG_PC;
    uint16_t RG_PS;
    uint16_t RG_RXFLL;
    uint16_t RG_RXFLH;
    uint16_t RG_TXFLL;
    uint16_t RG_TXFLH;
    uint16_t RG_FBLL;
    uint16_t RG_FBLH;
    uint16_t RG_FBLIL;
    uint16_t RG_FBLIH;
    uint16_t RG_OFDMPHRTX;
    uint16_t RG_OFDMPHRRX;
    uint16_t RG_OFDMC;
    uint16_t RG_OFDMSW;
    uint16_t RG_OQPSKC0;
    uint16_t RG_OQPSKC1;
    uint16_t RG_OQPSKC2;
    uint16_t RG_OQPSKC3;
    uint16_t RG_OQPSKPHRTX;
    uint16_t RG_OQPSKPHRRX;
    uint16_t RG_AFC0;
    uint16_t RG_AFC1;
    uint16_t RG_AFFTM;
    uint16_t RG_AFFVM;
    uint16_t RG_AFS;
    uint16_t RG_MACEA0;
    uint16_t RG_MACEA1;
    uint16_t RG_MACEA2;
    uint16_t RG_MACEA3;
    uint16_t RG_MACEA4;
    uint16_t RG_MACEA5;
    uint16_t RG_MACEA6;
    uint16_t RG_MACEA7;
    uint16_t RG_MACPID0F0;
    uint16_t RG_MACPID1F0;
    uint16_t RG_MACSHA0F0;
    uint16_t RG_MACSHA1F0;
    uint16_t RG_MACPID0F1;
    uint16_t RG_MACPID1F1;
    uint16_t RG_MACSHA0F1;
    uint16_t RG_MACSHA1F1;
    uint16_t RG_MACPID0F2;
    uint16_t RG_MACPID1F2;
    uint16_t RG_MACSHA0F2;
    uint16_t RG_MACSHA1F2;
    uint16_t RG_MACPID0F3;
    uint16_t RG_MACPID1F3;
    uint16_t RG_MACSHA0F3;
    uint16_t RG_MACSHA1F3;
    uint16_t RG_AMCS;
    uint16_t RG_AMEDT;
    uint16_t RG_AMAACKPD;
    uint16_t RG_AMAACKTL;
    uint16_t RG_AMAACKTH;
    uint16_t RG_FSKC0;
    uint16_t RG_FSKC1;
    uint16_t RG_FSKC2;
    uint16_t RG_FSKC3;
    uint16_t RG_FSKC4;
    uint16_t RG_FSKPLL;
    uint16_t RG_FSKSFD0L;
    uint16_t RG_FSKSFD0H;
    uint16_t RG_FSKSFD1L;
    uint16_t RG_FSKSFD1H;
    uint16_t RG_FSKPHRTX;
    uint16_t RG_FSKPHRRX;
    uint16_t RG_FSKRPC;
    uint16_t RG_FSKRPCONT;
    uint16_t RG_FSKRPCOFFT;
    uint16_t RG_FSKRRXFLL;
    uint16_t RG_FSKRRXFLH;
    uint16_t RG_FSKDM;
    uint16_t RG_FSKPE0;
    uint16_t RG_FSKPE1;
    uint16_t RG_FSKPE2;
    uint16_t RG_PMUC;
    uint16_t RG_PMUVAL;
    uint16_t RG_PMUQF;
    uint16_t RG_PMUI;
    uint16_t RG_PMUQ;
    uint16_t RG_CNTC;
    uint16_t RG_CNT0;
    uint16_t RG_CNT1;
    uint16_t RG_CNT2;
    uint16_t RG_CNT3;
};

/** @} */

/** BPSK, rate ½, 4 x frequency repetition */
#define BB_MCS_BPSK_REP4                0
/** BPSK, rate ½, 2 x frequency repetition */
#define BB_MCS_BPSK_REP2                1
/** QPSK, rate ½, 2 x frequency repetition */
#define BB_MCS_QPSK_REP2                2
/** QPSK, rate ½ */
#define BB_MCS_QPSK_1BY2                3
/** QPSK, rate ¾ */
#define BB_MCS_QPSK_3BY4                4
/** 16-QAM, rate ½ */
#define BB_MCS_16QAM_1BY2               5
/** 16-QAM, rate ¾ */
#define BB_MCS_16QAM_3BY4               6

/** receive only MR-O-QPSK */
#define RXM_MR_OQPSK                    0x0
/** receive only legacy O-QPSK */
#define RXM_LEGACY_OQPSK                0x1
/** receive both legacy & MR-O-QPSK */
#define RXM_BOTH_OQPSK                  0x2
/** receive nothing */
#define RXM_DISABLE                     0x3

/** Modulation Order 2-FSK */
#define FSK_MORD_2SFK                   (0 << FSKC0_MORD_SHIFT)
/** Modulation Order 4-FSK */
#define FSK_MORD_4SFK                   (1 << FSKC0_MORD_SHIFT)

/**
 * FSK modulation index
 * @{
 */
#define FSK_MIDX_3_BY_8                 (0 << FSKC0_MIDX_SHIFT)
#define FSK_MIDX_4_BY_8                 (1 << FSKC0_MIDX_SHIFT)
#define FSK_MIDX_6_BY_8                 (2 << FSKC0_MIDX_SHIFT)
#define FSK_MIDX_8_BY_8                 (3 << FSKC0_MIDX_SHIFT)
#define FSK_MIDX_10_BY_8                (4 << FSKC0_MIDX_SHIFT)
#define FSK_MIDX_12_BY_8                (5 << FSKC0_MIDX_SHIFT)
#define FSK_MIDX_14_BY_8                (6 << FSKC0_MIDX_SHIFT)
#define FSK_MIDX_16_BY_8                (7 << FSKC0_MIDX_SHIFT)
/** @} */

/**
 * FSK modulation index scale
 * @{
 */
#define FSK_MIDXS_SCALE_7_BY_8          (0 << FSKC0_MIDXS_SHIFT)
#define FSK_MIDXS_SCALE_8_BY_8          (1 << FSKC0_MIDXS_SHIFT)
#define FSK_MIDXS_SCALE_9_BY_8          (2 << FSKC0_MIDXS_SHIFT)
#define FSK_MIDXS_SCALE_10_BY_8         (3 << FSKC0_MIDXS_SHIFT)
/** @} */

/**
 * FSK bandwidth time product
 * @{
 */
#define FSK_BT_05                       (0 << FSKC0_BT_SHIFT)
#define FSK_BT_10                       (1 << FSKC0_BT_SHIFT)
#define FSK_BT_15                       (2 << FSKC0_BT_SHIFT)
#define FSK_BT_20                       (3 << FSKC0_BT_SHIFT)
/** @} */

/**
 * FSK symbol rate (kHz)
 * @{
 */
#define FSK_SRATE_50K                   0x0
#define FSK_SRATE_100K                  0x1
#define FSK_SRATE_150K                  0x2
#define FSK_SRATE_200K                  0x3
#define FSK_SRATE_300K                  0x4
#define FSK_SRATE_400K                  0x5
/** @} */

/**
 * FSK channel spacing (kHz)
 * @{
 */
#define FSK_CHANNEL_SPACING_200K        0x0
#define FSK_CHANNEL_SPACING_400K        0x1
/** @} */

/** Lower values increase the SFD detector sensitivity.
   Higher values increase the SFD selectivity.
   The default value 8 is recommended for simultaneous sensing
   of the SFD pairs according to IEEE 802.15.4g. */
#define FSKC3_SFDT(n) (((n) << FSKC3_SFDT_SHIFT) & FSKC3_SFDT_MASK)

/** Lower values increase the preamble detector sensitivity. */
#define FSKC3_PDT(n)  (((n) << FSKC3_PDT_SHIFT) & FSKC3_PDT_MASK)

/** @} */


typedef struct
{

} at86rf215_bb_phy_control_st;

void at86rf215_bb_set_phy_control (at86rf215_st *dev, at86rf215_rf_channel_en ch, at86rf215_bb_phy_control_st* pc);
void at86rf215_bb_get_phy_control (at86rf215_st *dev, at86rf215_rf_channel_en ch, at86rf215_bb_phy_control_st* pc);


#endif // __AT86RF215_BASEBAND_H__