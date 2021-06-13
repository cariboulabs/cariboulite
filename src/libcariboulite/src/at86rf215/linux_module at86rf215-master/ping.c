#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/printk.h>
/* #include <linux/platform_device.h> */
#include <linux/regmap.h>
/* #include <linux/spi/at86rf230.h> */
#include <linux/spi/spi.h>
/* #include <net/mac802154.h> */

#include "reg.h"

#define NUM_TRX 2

/** Enumeration for RF commands and states used for trx command and state
 * registers */
typedef enum rf_cmd_status_tag {
	/** Constant RF_TRXOFF for sub-register @ref SR_CMD */
	STATUS_RF_TRXOFF =                         (0x2),

	/** Constant RF_TXPREP for sub-register @ref SR_CMD */
	STATUS_RF_TXPREP =                         (0x3),

	/** Constant RF_TX for sub-register @ref SR_CMD */
	STATUS_RF_TX =                             (0x4),

	/** Constant RF_RX for sub-register @ref SR_CMD */
	STATUS_RF_RX =                             (0x5),

	/** Constant RF_RX for sub-register @ref SR_CMD */
	STATUS_RF_TRANSITION =                      (0x6),

	/** Constant RF_RESET for sub-register @ref SR_CMD */
	STATUS_RF_RESET =                          (0x7)
} rf_cmd_status_t;

/** Enumeration for RF IRQs used for IRQS and IRQM registers */
typedef enum rf_irq_tag {
	/** Constant RF_IRQ_IQIFSF for sub-register @ref SR_IQIFSF */
	RF_IRQ_IQIFSF =                     (0x20),

	/** Constant RF_IRQ_TRXERR for sub-register @ref SR_TRXERR */
	RF_IRQ_TRXERR =                     (0x10),

	/** Constant RF_IRQ_BATLOW for sub-register @ref SR_BATLOW */
	RF_IRQ_BATLOW =                     (0x08),

	/** Constant RF_IRQ_EDC for sub-register @ref SR_EDC */
	RF_IRQ_EDC =                        (0x04),

	/** Constant RF_IRQ_TRXRDY for sub-register @ref SR_TRXRDY */
	RF_IRQ_TRXRDY =                     (0x02),

	/** Constant RF_IRQ_WAKEUP for sub-register @ref SR_WAKEUP */
	RF_IRQ_WAKEUP =                     (0x01),

	/** No interrupt is indicated by IRQ_STATUS register */
	RF_IRQ_NO_IRQ =                     (0x00),

	/** All interrupts are indicated by IRQ_STATUS register */
	RF_IRQ_ALL_IRQ =                    (0x3F)
} rf_irq_t;

/** Enumeration for BB IRQs */
typedef enum bb_irq_tag {
	/** Constant BB_IRQ_FBLI for sub-register @ref SR_FBLI */
	BB_IRQ_FBLI =                       (0x80),

	/** Constant BB_IRQ_AGCR for sub-register @ref SR_AGCR */
	BB_IRQ_AGCR =                       (0x40),

	/** Constant BB_IRQ_AGCH for sub-register @ref SR_AGCH */
	BB_IRQ_AGCH =                       (0x20),

	/** Constant BB_IRQ_TXFE for sub-register @ref SR_TXFE */
	BB_IRQ_TXFE =                       (0x10),

	/** Constant BB_IRQ_RXEM for sub-register @ref SR_RXEM */
	BB_IRQ_RXEM =                       (0x08),

	/** Constant BB_IRQ_RXAM for sub-register @ref SR_RXAM */
	BB_IRQ_RXAM =                       (0x04),

	/** Constant BB_IRQ_RXFE for sub-register @ref SR_RXFE */
	BB_IRQ_RXFE =                       (0x02),

	/** Constant BB_IRQ_RXFS for sub-register @ref SR_RXFS */
	BB_IRQ_RXFS =                       (0x01),

	/** No interrupt is indicated by IRQ_STATUS register */
	BB_IRQ_NO_IRQ =                     (0x00),

	/** All interrupts are indicated by IRQ_STATUS register */
	BB_IRQ_ALL_IRQ =                    (0xFF)
} bb_irq_t;

static bool is_configured;

enum { AT86RF215 = 0x34, AT86RF215IQ, AT86RF215M };

static const struct trx_id {
	unsigned char part_number;
	const char *description;
} trx_ids[] = {
	{ AT86RF215, "AT86RF215" },
	{ AT86RF215IQ, "AT86RF215IQ" },
	{ AT86RF215M, "AT86RF215M" },
};
#define num_of_trx_ids (sizeof(trx_ids) / sizeof(trx_ids[0]))

enum { RF_DUMMY1        = 0x0,
       RF_DUMMY2,
       RF_TRXOFF        = 0x2,  /* Transceiver off, SPI active */
       RF_TXPREP,               /* Transmit preparation */
       RF_TX,                   /* Transmit */
       RF_RX,                   /* Receive */
       RF_TRANSITION,           /* State transition in progress */
       RF_RESET,                /* Transceiver is in state RESET or SLEEP */
       RF_MAX };

static const char *trx_state_descs[RF_MAX] = {
	"DUMMY1", "DUMMY2", "TRXOFF", "TXPREP", "TX", "RX", "TRANSITION", "RESET"
};

static const struct trx_state {
	unsigned char value;
	const char *description;
} trx_states[] = {
	{ RF_TRXOFF, "TRXOFF" }, { RF_TXPREP, "TXPREP" },         { RF_TX, "TX" },
	{ RF_RX, "RX" },         { RF_TRANSITION, "TRANSITION" }, { RF_RESET, "RESET" }
};
#define num_of_trx_states (sizeof(trx_states) / sizeof(trx_states[0])

static struct at86rf215_priv {
	struct spi_device *spi;
	int cookie;
} at86rf215_priv = { .cookie = 0xbabef00d };

static struct at86rf215_priv *priv = &at86rf215_priv;

enum { CMD_NOP,
       CMD_SLEEP,
       CMD_TRXOFF,
       CMD_TXPREP,
       CMD_TX,
       CMD_RX,
       CMD_RESET = 0x7 };

/* SPI block read access data structures and functions */
static u8 trx_read_tx_buf[2];
static volatile u8 trx_read_rx_buf[2047];
static struct spi_message trx_read_msg;
static struct spi_transfer trx_read_xfers[2];


static struct {
	const char *names[4];
	u8 values[4];
} at86rf215_irqs = {
	{ "RF09_IRQS", "RF24_IRQS", "BBC0_IRQS",  "BBC1_IRQS" },
	{ 0 }
};

/** Sub-registers of Register @ref IRQS */
/** Bit Mask for Sub-Register IRQS.WAKEUP */
#define IRQS_WAKEUP_MASK                0x01
/** Bit Offset for Sub-Register IRQS.WAKEUP */
#define IRQS_WAKEUP_SHIFT               0

/** Bit Mask for Sub-Register IRQS.TRXRDY */
#define IRQS_TRXRDY_MASK                0x02
/** Bit Offset for Sub-Register IRQS.TRXRDY */
#define IRQS_TRXRDY_SHIFT               1

/** Bit Mask for Sub-Register IRQS.EDC */
#define IRQS_EDC_MASK                   0x04
/** Bit Offset for Sub-Register IRQS.EDC */
#define IRQS_EDC_SHIFT                  2

/** Bit Mask for Sub-Register IRQS.BATLOW */
#define IRQS_BATLOW_MASK                0x08
/** Bit Offset for Sub-Register IRQS.BATLOW */
#define IRQS_BATLOW_SHIFT               3

/** Bit Mask for Sub-Register IRQS.TRXERR */
#define IRQS_TRXERR_MASK                0x10
/** Bit Offset for Sub-Register IRQS.TRXERR */
#define IRQS_TRXERR_SHIFT               4

/** Bit Mask for Sub-Register IRQS.IQIFSF */
#define IRQS_IQIFSF_MASK                0x20
/** Bit Offset for Sub-Register IRQS.IQIFSF */
#define IRQS_IQIFSF_SHIFT               5

#define RF_IRQS_MAX_SHIFT		IRQS_IQIFSF_SHIFT

/** Sub-registers of Register @ref IRQS */
/** Bit Mask for Sub-Register IRQS.RXFS */
#define IRQS_RXFS_MASK                  0x01
/** Bit Offset for Sub-Register IRQS.RXFS */
#define IRQS_RXFS_SHIFT                 0

/** Bit Mask for Sub-Register IRQS.RXFE */
#define IRQS_RXFE_MASK                  0x02
/** Bit Offset for Sub-Register IRQS.RXFE */
#define IRQS_RXFE_SHIFT                 1

/** Bit Mask for Sub-Register IRQS.RXAM */
#define IRQS_RXAM_MASK                  0x04
/** Bit Offset for Sub-Register IRQS.RXAM */
#define IRQS_RXAM_SHIFT                 2

/** Bit Mask for Sub-Register IRQS.RXEM */
#define IRQS_RXEM_MASK                  0x08
/** Bit Offset for Sub-Register IRQS.RXEM */
#define IRQS_RXEM_SHIFT                 3

/** Bit Mask for Sub-Register IRQS.TXFE */
#define IRQS_TXFE_MASK                  0x10
/** Bit Offset for Sub-Register IRQS.TXFE */
#define IRQS_TXFE_SHIFT                 4

/** Bit Mask for Sub-Register IRQS.AGCH */
#define IRQS_AGCH_MASK                  0x20
/** Bit Offset for Sub-Register IRQS.AGCH */
#define IRQS_AGCH_SHIFT                 5

/** Bit Mask for Sub-Register IRQS.AGCR */
#define IRQS_AGCR_MASK                  0x40
/** Bit Offset for Sub-Register IRQS.AGCR */
#define IRQS_AGCR_SHIFT                 6

/** Bit Mask for Sub-Register IRQS.FBLI */
#define IRQS_FBLI_MASK                  0x80
/** Bit Offset for Sub-Register IRQS.FBLI */
#define IRQS_FBLI_SHIFT                 7

#define BB_IRQS_MAX_SHIFT		IRQS_FBLI_SHIFT

static void at86rf215_rf_irqs_print(const char *name, u8 val)
{
	char str[RF_IRQS_MAX_SHIFT + 2];
	str[RF_IRQS_MAX_SHIFT + 1] = '\0';

	printk("\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
	       "             _------==> IQIFSF",
	       "            / _-----==> TRXERR",
	       "           | / _----==> BATLOW",
	       "           || / _---==> EDC",
	       "           ||| / _--==> TRXRDY",
	       "           |||| / _-==> WAKEUP",
	       "           ||||| /",
	       "           ||||||"
		);
	str[RF_IRQS_MAX_SHIFT - IRQS_WAKEUP_SHIFT] =
		val & IRQS_WAKEUP_MASK ? '1' : '.';
	str[RF_IRQS_MAX_SHIFT - IRQS_TRXRDY_SHIFT] =
		val & IRQS_TRXRDY_MASK ? '1' : '.';
	str[RF_IRQS_MAX_SHIFT - IRQS_EDC_SHIFT] =
		val & IRQS_EDC_MASK ? '1' : '.';
	str[RF_IRQS_MAX_SHIFT - IRQS_BATLOW_SHIFT] =
		val & IRQS_BATLOW_MASK ? '1' : '.';
	str[RF_IRQS_MAX_SHIFT - IRQS_TRXERR_SHIFT] =
		val & IRQS_TRXERR_MASK ? '1' : '.';
	str[RF_IRQS_MAX_SHIFT - IRQS_IQIFSF_SHIFT] =
		val & IRQS_IQIFSF_MASK ? '1' : '.';
	printk("%s  %s\n", name, str);
}

static void at86rf215_bb_irqs_print(const char *name, u8 val)
{
	char str[BB_IRQS_MAX_SHIFT + 2];
	str[BB_IRQS_MAX_SHIFT + 1] = '\0';

	printk("\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
	       "             _--------==> FBLI",
	       "            / _-------==> AGCR",
	       "           | / _------==> AGCH",
	       "           || / _-----==> TXFE",
	       "           ||| / _----==> RXEM",
	       "           |||| / _---==> RXAM",
	       "           ||||| / _--==> RXFE",
	       "           |||||| / _-==> RXFS",
	       "           ||||||| /",
	       "           ||||||||");
	str[BB_IRQS_MAX_SHIFT - IRQS_RXFS_SHIFT] =
		val & IRQS_RXFS_MASK ? '1' : '.';
	str[BB_IRQS_MAX_SHIFT - IRQS_RXFE_SHIFT] =
		val & IRQS_RXFE_MASK ? '1' : '.';
	str[BB_IRQS_MAX_SHIFT - IRQS_RXAM_SHIFT] =
		val & IRQS_RXAM_MASK ? '1' : '.';
	str[BB_IRQS_MAX_SHIFT - IRQS_RXEM_SHIFT] =
		val & IRQS_RXEM_MASK ? '1' : '.';
	str[BB_IRQS_MAX_SHIFT - IRQS_TXFE_SHIFT] =
		val & IRQS_TXFE_MASK ? '1' : '.';
	str[BB_IRQS_MAX_SHIFT - IRQS_AGCH_SHIFT] =
		val & IRQS_AGCH_MASK ? '1' : '.';
	str[BB_IRQS_MAX_SHIFT - IRQS_AGCR_SHIFT] =
		val & IRQS_AGCR_MASK ? '1' : '.';
	str[BB_IRQS_MAX_SHIFT - IRQS_FBLI_SHIFT] =
		val & IRQS_FBLI_MASK ? '1' : '.';
	printk("%s  %s\n", name, str);
}

static void at86rf215_irqs_read_complete(void *context)
{
	struct at86rf215_priv *priv = context;
	int i;

	(void)priv;

	for (i = 0; i < NUM_TRX; i++) {
		/* if (at86rf215_irqs.values[i]) */
			at86rf215_rf_irqs_print(at86rf215_irqs.names[i],
						at86rf215_irqs.values[i]);

		/* if (at86rf215_irqs.values[i + 2]) */
			at86rf215_bb_irqs_print(at86rf215_irqs.names[i + 2],
						at86rf215_irqs.values[i + 2]);
	}
}

static void at86rf215_trx_read(struct at86rf215_priv *priv, u16 addr,
			       u8 *data, size_t len,
			       void (*complete)(void *context))
{
        /* The first transfer carries the two command bytes */
	trx_read_xfers[0].len = 2;
	trx_read_tx_buf[0] = addr >> 8;
	trx_read_tx_buf[1] = addr;
	trx_read_xfers[0].tx_buf = &trx_read_tx_buf[0];
	trx_read_xfers[0].rx_buf = NULL;

        /*
         * The second transfer reads payload. According to the SPI API
         * documentation, the chip select normally remains active until after
         * the last transfer in a message.
         */
	trx_read_xfers[1].len = len;
	trx_read_xfers[1].tx_buf = NULL;
	trx_read_xfers[1].rx_buf = data;

	spi_message_init_with_transfers(&trx_read_msg, trx_read_xfers,
					sizeof(trx_read_xfers) / sizeof(trx_read_xfers[0]));
	trx_read_msg.complete = complete;
	trx_read_msg.context = priv;

	spi_async(priv->spi, &trx_read_msg);
}

/* SPI block write access data structures and functions */
static u8 trx_write_tx_buf[2047 + 2];
static struct spi_message trx_write_msg;
static struct spi_transfer trx_write_xfers[2];

static void at86rf215_trx_write_complete(void *context)
{
	struct at86rf215_priv *priv = context;

	printk("%s in_interrupt: %s  cookie: 0x%x\n", __func__,
	       in_interrupt() ? "yes" : "no", priv->cookie);
}

static void at86rf215_trx_write(struct at86rf215_priv *priv, u16 addr, u8 *data,
				size_t len)
{
        /* The first transfer carries the two command bytes */
	trx_write_xfers[0].len = 2;
	trx_write_tx_buf[0] = (addr >> 8) | 0x8000;
	trx_write_tx_buf[1] = addr;
	trx_write_xfers[0].tx_buf = &trx_write_tx_buf[0];
	trx_write_xfers[0].rx_buf = NULL;

        /*
         * The second transfer writes payload. According to the SPI API
         * documentation, the chip select normally remains active until after
         * the last transfer in a message.
         */
	trx_write_xfers[1].len = len;
	trx_write_xfers[1].tx_buf = data;
	trx_write_xfers[1].rx_buf = NULL;

	spi_message_init_with_transfers(&trx_write_msg, trx_write_xfers,
					sizeof(trx_write_xfers) / sizeof(trx_write_xfers[0]));
	trx_write_msg.complete = at86rf215_trx_write_complete;
	trx_write_msg.context = priv;

	spi_async(priv->spi, &trx_write_msg);
}

/*
 * TODO: confirm that it's preferable to allocate context on the heap. Make sure to free allocated
 * memory!
 */
static irqreturn_t at86rf215_irq_handler(int irq, void *data)
{
	struct at86rf215_priv *priv = data;

	printk("%s in_interrupt: %s\n", __func__,
	       in_interrupt() ? "yes" : "no");

	disable_irq_nosync(irq);

	/* TODO: disable all IRQs on the transceiver by writing BBCx_IRQM and RFx_IRQM */

	/* Read all IRQ status registers. This will clear the interrupt line. */
	at86rf215_trx_read(priv, RG_RF09_IRQS, &at86rf215_irqs.values[0],
			   sizeof(at86rf215_irqs.values),
			   at86rf215_irqs_read_complete);

	return IRQ_HANDLED;
}

static struct timer_list ping_timer;

/* RSTN pin number */
static int rstn_pin_num;

static struct regmap *regmap;

/* Registers whose value can be written */
static bool at86rf215_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RG_RF09_IRQM:
	case RG_RF09_CMD:
	case RG_RF09_CS:
	case RG_RF09_CCF0L:
	case RG_RF09_CCF0H:
	case RG_RF09_CNL:
	case RG_RF09_CNM:
	case RG_RF09_RXBWC:
	case RG_RF09_RXDFE:
	case RG_RF09_EDD:
	case RG_RF09_RNDV:
	case RG_RF09_TXCUTC:
	case RG_RF09_TXDFE:
	case RG_RF09_PAC:
	case RG_BBC0_IRQM:
	case RG_BBC0_PC:
	case RG_BBC0_OFDMPHRTX:
		return true;

	default:
		return false;
	}
}

/* Registers whose value can be read */
static bool at86rf215_readable_reg(struct device *dev, unsigned int reg)
{
	bool ret;

	ret = at86rf215_writeable_reg(dev, reg);
	if (ret) {
		return ret;
	}

	switch (reg) {
	case RG_RF09_IRQS:
	case RG_RF24_IRQS:
	case RG_RF_PN:
	case RG_RF_VN:
	case RG_RF09_STATE:
	case RG_RF09_RSSI:
		return true;

	default:
		return false;
	}
}

/* Registers whose value shouldn't be cached */
static bool at86rf215_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RG_RF09_IRQS:
	case RG_RF24_IRQS:
	case RG_RF_PN:  /* TODO: remove */
	case RG_RF_VN:  /* TODO: remove */
	case RG_RF09_STATE:
	case RG_RF09_RSSI:
		return true;

	default:
		return false;
	}
}

/* Registers whose value should not be read outside a call from the driver */
/* The IRQ status registers are cleared automatically by reading the
 * corresponding register via SPI. */
static bool at86rf215_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	/* case RG_RF09_IRQS: */
	case RG_BBC0_IRQS:
		return true;

	default:
		return false;
	}
}

/* AT86RF215 register map configuration */
static const struct regmap_config at86rf215_regmap_config = {
	.reg_bits        = 16,
	.val_bits        = 8,
	.writeable_reg   = at86rf215_writeable_reg,
	.readable_reg    = at86rf215_readable_reg,
	.volatile_reg    = at86rf215_volatile_reg,
	.precious_reg    = at86rf215_precious_reg,
	.read_flag_mask  = 0x0,         /* [0|0|x|x|x|x|x|x|x|x|x|x|x|x|x|x] */
	.write_flag_mask = 0x80,        /* [1|0|x|x|x|x|x|x|x|x|x|x|x|x|x|x] */
	.cache_type      = REGCACHE_RBTREE,
	.max_register    = 0x3FFE,
};

/* Retrieve RSTN pin number from device tree */
static int at86rf215_get_platform_data(struct spi_device *spi,
				       int *rstn_pin_num)
{
	if (!spi->dev.of_node) {
		dev_err(&spi->dev, "no device tree node found!\n");
		return -ENOENT;
	}

	*rstn_pin_num = of_get_named_gpio(spi->dev.of_node, "reset-gpio", 0);

	return 0;
}

static void at86rf215_configure(void)
{
	int ret;

        /* Radio IRQ mask configuration */
	ret = regmap_write(regmap, RG_RF09_IRQM, 0x1F);
	if (ret < 0) {
		pr_err("can't write radio IRQ mask\n");
		return;
	}

        /* Baseband IRQ mask configuration */
	ret = regmap_write(regmap, RG_BBC0_IRQM, 0x1F);
	if (ret < 0) {
		pr_err("can't write baseband IRQ mask\n");
		return;
	}

        /* Channel configuration */
	ret = regmap_write(regmap, RG_RF09_CS, 0x30);
	if (ret < 0) {
		pr_err("can't write channel spacing\n");
		return;
	}

	ret = regmap_write(regmap, RG_RF09_CCF0L, 0x20);
	if (ret < 0) {
		pr_err("can't write channel center freq0 low byte\n");
		return;
	}

	ret = regmap_write(regmap, RG_RF09_CCF0H, 0x8D);
	if (ret < 0) {
		pr_err("can't write channel center freq0 high byte\n");
		return;
	}

	ret = regmap_write(regmap, RG_RF09_CNL, 0x3);
	if (ret < 0) {
		pr_err("can't write channel number\n");
		return;
	}

	ret = regmap_write(regmap, RG_RF09_CNM, 0);
	if (ret < 0) {
		pr_err("can't trigger channel setting update\n");
		return;
	}

        /*
         * ret = regmap_write(regmap, , );
         * if (ret < 0) {
         *      pr_err("can't write transceiver's \n");
         *      return;
         * }
         */

        /* Frontend configuration */

        /* TXCUTC.LPFCUT is set to 0xB */
	ret = regmap_write(regmap, RG_RF09_TXCUTC, 0xB);
	if (ret < 0) {
		pr_err("can't write TX filter cut-off freq\n");
		return;
	}

        /* TXDFE.SR is set to 3, TXDFE.RCUT is set to 4 */
	ret = regmap_write(regmap, RG_RF09_TXDFE, 0x83);
	if (ret < 0) {
		pr_err("can't write TX sample rate\n");
		return;
	}

        /* PAC.TXPWR is set to 0x1C */
	ret = regmap_write(regmap, RG_RF09_PAC, 0x7C);
	if (ret < 0) {
		pr_err("can't write TX output power\n");
		return;
	}

        /* Energy Measurement and AGC Configuration */
        /* The reset values for the AGC configuration are already suitable for
         * ODFM Option 1 */
	ret = regmap_write(regmap, RG_RF09_EDD, 0x7A);
	if (ret < 0) {
		pr_err("can't write RX energy detection averaging duration\n");
		return;
	}

        /* Modulation configuration */

        /* PC.PT is set to 2 (MR-OFDM) */
	ret = regmap_write(regmap, RG_BBC0_PC, 0x56);
	if (ret < 0) {
		pr_err("can't write PHY type\n");
		return;
	}

        /* OFDMPHRTX.MCS is set to 3 (800kb/s) */
	ret = regmap_write(regmap, RG_BBC0_OFDMPHRTX, 0x3);
	if (ret < 0) {
		pr_err("can't write bit rate\n");
		return;
	}

        /* Initiate transceiver state change */
	ret = regmap_write(regmap, RG_RF09_CMD, RF_TXPREP);
	if (ret < 0) {
		pr_err("can't write command\n");
		return;
	}

	is_configured = true;
}

static void ping_process(struct timer_list *t)
{
	int ret, i;
	unsigned int val;
	const char *description = NULL;

	(void)ret;
	(void)i;
	(void)val;
	(void)description;
	(void)trx_state_descs;

	if (!is_configured) {
		pr_debug("configuring transceiver...");
		at86rf215_configure();
	} else {
                /* pr_debug("transceiver is already configured"); */
	}

#if 0
	ret = regmap_read(regmap, RG_RF_PN, &val);
	if (ret < 0) {
		pr_err("can't read transceiver part number\n");
		return;
	}

	for (i = 0; i < num_of_trx_ids; i++) {
		if (trx_ids[i].part_number == val) {
			description = trx_ids[i].description;
			break;
		}
	}

	if (description == NULL) {
		pr_err("unknown transceiver part number 0x%x\n", val);
		return;
	}

	ret = regmap_read(regmap, RG_RF_VN, &val);
	if (ret < 0) {
		pr_err("can't read transceiver version number\n");
		return;
	}
	pr_debug("detected %sv%d transceiver\n", description, val);

	ret = regmap_read(regmap, RG_RF09_STATE, &val);
	if (ret < 0) {
		pr_err("can't read transceiver's current state\n");
		return;
	}
	pr_debug("transceiver's current state is %s\n", trx_state_descs[val]);

        /* Initiate transceiver state change */
	ret = regmap_write(regmap, RG_RF09_CMD, RF_TXPREP);
	if (ret < 0) {
		pr_err("can't write command\n");
		return;
	}
#endif

	mod_timer(&ping_timer, jiffies + msecs_to_jiffies(1000));
}


static int at86rf215_probe(struct spi_device *spi)
{
	unsigned int val;
	unsigned long irqflags = 0;

	(void)val;
	(void)at86rf215_trx_write;

	if (!spi->irq) {
		dev_err(&spi->dev, "no IRQ number\n");
		return -1;
	}

	dev_dbg(&spi->dev, "IRQ number is %d\n", spi->irq);

        /* Retrieve RSTN pin number */
	if (at86rf215_get_platform_data(spi, &rstn_pin_num) < 0 ||
	    !gpio_is_valid(rstn_pin_num)) {
		return -1;
	}

	dev_dbg(&spi->dev, "RSTN pin number is %d\n", rstn_pin_num);

        /* Configure as output, active low */
	if (devm_gpio_request_one(&spi->dev, rstn_pin_num, GPIOF_OUT_INIT_HIGH,
				  "rstn") < 0) {
		return -1;
	}

        /* TODO: reset transceiver by asserting RTSN low */

        /* Initialize register map */
	regmap = devm_regmap_init_spi(spi, &at86rf215_regmap_config);
	if (IS_ERR(regmap)) {
		int ret = PTR_ERR(regmap);
		dev_err(&spi->dev, "can't alloc regmap 0x%x\n", ret);
		devm_gpio_free(&spi->dev, rstn_pin_num);
		return -1;
	}

	priv->spi = spi;

	timer_setup(&ping_timer, ping_process, 0);

        /* Read IRQ status regs to reset line */
	regmap_read(regmap, RG_RF09_IRQS, &val);
	regmap_read(regmap, RG_RF24_IRQS, &val);

	ping_process(NULL);

	irqflags = irq_get_trigger_type(spi->irq);
	dev_dbg(&spi->dev, "irqflags 0x%lx\n", irqflags);
	if (!irqflags) {
		irqflags = IRQF_TRIGGER_HIGH;
	}

	if (devm_request_irq(&spi->dev, spi->irq, at86rf215_irq_handler,
			     IRQF_SHARED | irqflags,
			     dev_name(&spi->dev), &at86rf215_priv) < 0) {
		dev_err(&spi->dev, "can't allocate IRQ\n");
		devm_gpio_free(&spi->dev, rstn_pin_num);
		return -1;
	}

        /* disable_irq(spi->irq); */

	return 0;
}

static int at86rf215_remove(struct spi_device *spi)
{
	if (del_timer(&ping_timer)) {
		pr_debug("deactivated active timer\n");
	}

	devm_free_irq(&spi->dev, spi->irq, &at86rf215_priv);
	devm_gpio_free(&spi->dev, rstn_pin_num);

	return 0;
}

static const struct of_device_id at86rf215_of_match[] = {
	{
		.compatible = "atmel,at86rf233",
	},
	{},
};
MODULE_DEVICE_TABLE(of, at86rf215_of_match);

static const struct spi_device_id at86rf215_device_id[] = {
	{
		.name = "at86rf233",
	},
	{},
};
MODULE_DEVICE_TABLE(spi, at86rf215_device_id);

static struct spi_driver at86rf215_driver = {
	.id_table = at86rf215_device_id,
	.driver   = { .of_match_table = of_match_ptr(at86rf215_of_match),
		      .name           = "at86rf233" },
	.probe    = at86rf215_probe,
	.remove   = at86rf215_remove
};

module_spi_driver(at86rf215_driver);

MODULE_DESCRIPTION("Atmel AT86RF215 radio transceiver driver");
MODULE_LICENSE("GPL v2");
