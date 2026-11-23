PWD := $(shell pwd)
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

DRV_NAME := virtual-ad7683

.PHONY: build run remove install uninstall clean format check build-spi run-spi remove-spi

build:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

run:
	insmod $(PWD)/$(DRV_NAME).ko

remove:
	rmmod $(DRV_NAME)

install:
	cp $(DRV_NAME).ko /lib/modules/$(shell uname -r)
	cp $(DRV_NAME).conf /etc/modprobe.d/
	depmod -a
	modprobe $(DRV_NAME)

uninstall:
	modprobe -r $(DRV_NAME)
	rm /lib/modules/$(shell uname -r)/$(DRV_NAME).ko
	rm /etc/modprobe.d/$(DRV_NAME).conf
	depmod -a

build-spi:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) SPI_STAND=1 modules

run-spi:
	insmod $(PWD)/spi.ko

remove-spi:
	rmmod spi

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

format:
	find . -name '*.c' -o -name '*.h' | xargs clang-format -i --style=file
