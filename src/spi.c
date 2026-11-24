#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>

#include "spi.h"

static struct ad7683_controller controller;

static int ad7683_spi_transfer_one(struct spi_controller *ctlr,
				   struct spi_device *spi,
				   struct spi_transfer *tfr)
{
	u8 *rx = tfr->rx_buf;

	if (tfr->tx_buf || !rx || tfr->len != 2)
		return -EINVAL;

	rx[0] = 0x12;
	rx[1] = 0x34;

	return 0;
}

static int ad7683_spi_controller_init(void)
{
	struct spi_controller *ctlr;
	struct device *parent;
	int ret;

	parent = root_device_register("ad7683_spi_controller");
	if (IS_ERR(parent))
		return PTR_ERR(parent);

	ctlr = spi_alloc_host(parent, 0);
	if (!ctlr) {
		ret = -ENOMEM;
		goto err_parent;
	}

	ctlr->bus_num = -1;
	ctlr->mode_bits = SPI_MODE_0;
	ctlr->num_chipselect = 1;
	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->max_speed_hz = 1000000;
	ctlr->transfer_one = ad7683_spi_transfer_one;

	ret = spi_register_controller(ctlr);
	if (ret)
		goto err_ctlr;

	controller.ctlr = ctlr;
	controller.parent = parent;
	return 0;

err_ctlr:
	spi_controller_put(ctlr);
err_parent:
	root_device_unregister(parent);
	return ret;
}

static void ad7683_spi_controller_exit(void)
{
	spi_unregister_controller(controller.ctlr);
	root_device_unregister(controller.parent);
}

static int ad7683_probe(struct spi_device *spi)
{
	return 0;
}

static void ad7683_remove(struct spi_device *spi)
{
}

static int __init ad7683_spi_init(void)
{
	int ret = ad7683_spi_controller_init();

	if (ret < 0) {
		pr_err("ad7683_spi: init error: %d\n", ret);
		return ret;
	}

	pr_info("ad7683_spi loaded successfully\n");
	return 0;
}

static void __exit ad7683_spi_exit(void)
{
	ad7683_spi_controller_exit();
	pr_info("ad7683_spi unloaded\n");
}

module_init(ad7683_spi_init);
module_exit(ad7683_spi_exit);

static const struct spi_device_id ad7683_id[] = {
	{ "ad7683", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7683_id);

static struct spi_driver ad7683_driver __maybe_unused = {
	.driver = {
		.name = "ad7683",
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
	},
	.probe		= ad7683_probe,
	.remove		= ad7683_remove,
	.id_table	= ad7683_id,
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Timofey Zaika");
MODULE_DESCRIPTION("Virtual AD7683 SPI controller stand");
MODULE_VERSION("1.0");
