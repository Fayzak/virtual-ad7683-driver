#ifndef SPIH
#define SPIH

#include <linux/device.h>
#include <linux/spi/spi.h>

// SPI Protocol reference - ad7476.c
struct ad7683_protocol {
	int probe_result;
};

// SPI Controller reference - spi-bcm2835.c
struct ad7683_controller {
	struct spi_controller *ctlr;
	struct device *parent;
};

#endif
