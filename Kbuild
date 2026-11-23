ifeq ($(SPI_STAND),1)
obj-m := spi.o
spi-y := src/spi.o
else
obj-m := virtual-ad7683.o
virtual-ad7683-objs := src/main.o src/params.o src/device.o src/client.o
endif

ccflags-y := -I$(src)/inc