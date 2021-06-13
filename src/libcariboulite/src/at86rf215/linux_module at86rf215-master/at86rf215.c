#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/printk.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spi/at86rf230.h>
#include <linux/spi/spi.h>
#include <net/mac802154.h>

struct at86rf215_private {
	struct regmap *regmap;
	struct ieee802154_hw *hw;
};

static const struct regmap_config at86rf215_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.read_flag_mask = 0x0,		/* [0|0|A|A|A|A|A|A|A|A|A|A|A|A|A|A] */
	.write_flag_mask = 0x80,	/* [1|0|A|A|A|A|A|A|A|A|A|A|A|A|A|A] */
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0x3FFE,
};

static struct at86rf215_private *priv;

static int at86rf215_start(struct ieee802154_hw *hw)
{
	return 0;
}

static void at86rf215_stop(struct ieee802154_hw *hw)
{
}

static int at86rf215_xmit_async(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	return 0;
}

static int at86rf215_ed(struct ieee802154_hw *hw, u8 *level)
{
	return 0;
}

static int at86rf215_set_channel(struct ieee802154_hw *hw, u8 page, u8 channel)
{
	return 0;
}

static const struct ieee802154_ops at86rf215_ieee802154_ops = {
	.start = at86rf215_start,
	.stop = at86rf215_stop,
	.xmit_async = at86rf215_xmit_async,
	.ed = at86rf215_ed,
	.set_channel = at86rf215_set_channel
};

/* Retrieve RSTN pin number from device tree */
static int at86rf215_get_platform_data(struct spi_device *spi, int *rstn_pin_num)
{
	if (!spi->dev.of_node) {
		dev_err(&spi->dev, "no device tree node found!\n");
		return -ENOENT;
	}

	*rstn_pin_num = of_get_named_gpio(spi->dev.of_node, "reset-gpio", 0);


	return 0;
}

static int at86rf215_probe(struct spi_device *spi)
{
#if 1
	int rstn_pin_num = 0;
#define RG_RF_PN                        (0x0D)
#define RG_RF_VN                        (0x0E)
	u8 cmd1[3] = {0, RG_RF_PN, 0xff};
	/* u8 cmd2[3] = {0, RG_RF_VN, 0xff}; */
	u8 rx_buf[3] = {0};
	struct spi_message msg;
	struct spi_transfer xfer = {
		.len = 3,
		.tx_buf = &cmd1[0],
		.rx_buf = &rx_buf[0]
	};

	/* Retrieve RSTN pin number */ 
	if (at86rf215_get_platform_data(spi, &rstn_pin_num) < 0 ||
		!gpio_is_valid(rstn_pin_num)) {
		return -1;
	}

	dev_dbg(&spi->dev, "RSTN pin number is %d\n", rstn_pin_num);

	/* Configure as output, active low */
	if (devm_gpio_request_one(&spi->dev, rstn_pin_num,
				  GPIOF_OUT_INIT_HIGH, "reset") < 0) {
		
	}

	/*
	 * Reset transceiver. The pulse width at pin RSTN must last at least
	 * 625ns
	 */
	/* 
	 * udelay(1);
	 * gpio_set_value_cansleep(rstn_pin_num, 0);
	 * udelay(1);
	 * gpio_set_value_cansleep(rstn_pin_num, 1);
	 */

	/* 
	 * spi_write(spi, cmd1, 3);
	 * usleep_range(500, 1000);
	 * spi_write(spi, cmd2, 3);
	 */

	spi_message_init(&msg);

	/* Read transceiver Part Number */
	xfer.tx_buf = &cmd1[0];
	spi_message_add_tail(&xfer, &msg);
	spi_sync(spi, &msg);

	print_hex_dump_bytes("", DUMP_PREFIX_NONE, rx_buf, 3);

	/* FIXME: the added code below leads to kernel crash */
	/* Read transceiver Version Number */
	/*
	 * xfer.tx_buf = &cmd2[0];
	 * spi_message_add_tail(&xfer, &msg);
	 * spi_sync(spi, &msg);
	 *
	 * print_hex_dump_bytes("", DUMP_PREFIX_NONE, rx_buf, 3);
	 */
#else
	int ret = 0;
	struct ieee802154_hw *hw;

	if (priv != NULL)
	{
		return -1;
	}

	hw = ieee802154_alloc_hw(sizeof(priv), &at86rf215_ieee802154_ops);
	if (!hw)
		return -ENOMEM;

	priv = hw->priv;
	priv->hw = hw;

	ieee802154_random_extended_addr(&hw->phy->perm_extended_addr);
	
	ret = ieee802154_register_hw(hw);
	if (ret)
		ieee802154_free_hw(hw);
#endif

	return 0;
}

static int at86rf215_remove(struct spi_device *spi)
{
#if 1
	priv = NULL;
#else
	ieee802154_unregister_hw(priv->hw);
	ieee802154_free_hw(priv->hw);

	priv = NULL;
#endif
	return 0;
}

static const struct of_device_id at86rf215_of_match[] = {
	{ .compatible = "atmel,at86rf233", },
	{ },
};
MODULE_DEVICE_TABLE(of, at86rf215_of_match);

static const struct spi_device_id at86rf215_device_id[] = {
	{ .name = "at86rf233", },
	{ },
};
MODULE_DEVICE_TABLE(spi, at86rf215_device_id);

static struct spi_driver at86rf215_driver = {
	.id_table = at86rf215_device_id,
	.driver = {
		.of_match_table = of_match_ptr(at86rf215_of_match),
		.name = "at86rf233"
	},
	.probe = at86rf215_probe,
	.remove = at86rf215_remove
};

module_spi_driver(at86rf215_driver);

MODULE_DESCRIPTION("Atmel AT86RF215 radio transceiver driver");
MODULE_LICENSE("GPL v2");
