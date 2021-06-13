# A prerequisite to cross-compiling an out-of-tree kernel module for the
# Raspberry Pi running a **stock kernel** is to get and cross-compile the kernel
# source code for the version **matching exactly** the one running on your
# board. Figure out the version of the kernel running on your board
#
# pi@raspberrypi:~ $ uname -r
# 4.19.57-v7+
#
# pi@raspberrypi:~$ vcgencmd version
# Jul  9 2019 14:40:53
# Copyright (c) 2012 Broadcom
# version 6c3fe3f096a93de3b34252ad98cdccadeb534be2 (clean) (release) (start)
#
# Clone only the relevant Git branch on your development host
#
# $ git clone --branch raspberrypi-kernel_1.20190709-1 --depth 1 https://github.com/raspberrypi/linux.git
#
# $ cd linux
#
# $ head -5 Makefile
# # SPDX-License-Identifier: GPL-2.0
# VERSION = 4
# PATCHLEVEL = 19
# SUBLEVEL = 57
# EXTRAVERSION =
#
# Cross-compile the kernel source code for your specific board model as
# instructed in the official documentation. For instance, for a Raspberry Pi 3
# Model B+
#
# $ KERNEL=kernel7
#
# $ make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- bcm2709_defconfig
#
# $ make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage modules dtbs
#

CFLAGS_MODULE += -Werror
CFLAGS_MODULE += -DDEBUG

ARCH := arm
KERNELDIR := /home/godzilla/making/linux
CROSS_COMPILE := arm-linux-gnueabihf-

PWD       := $(shell pwd)

obj-m	:= at86rf215.o ping.o

default:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(PWD) modules
clean:
	rm -rf .*.cmd .tmp_versions *~ *.ko *.mod.c *.o modules.order Module.symvers
