#ifndef SPIH
#define SPIH

#include <linux/spi/spi.h>

// SPI Protocol reference - ad7476.c
struct ad7683_protocol {

};

// SPI Controller reference - spi-bcm2835.c
struct ad7683_controller {
    struct spi_controller *ctlr;
};

#endif