# Description

A Linux driver for the Atmel AT86RF215 radio transceiver. The development target
is the Raspberry Pi 3.

# Compilation

A prerequisite to cross-compiling an out-of-tree kernel module for the Raspberry
Pi running a **stock kernel** is to get and cross-compile the kernel source code
for the exact version running on your board. To find out the version of the
kernel running on your board execute

```
pi@raspberrypi:~ $ uname -r
4.19.57-v7+
```

```
pi@raspberrypi:~$ vcgencmd version
Jul  9 2019 14:40:53
Copyright (c) 2012 Broadcom
version 6c3fe3f096a93de3b34252ad98cdccadeb534be2 (clean) (release) (start)
```

Clone only the relevant Git branch on your development host

```
$ git clone --branch raspberrypi-kernel_1.20190709-1 --depth 1 https://github.com/raspberrypi/linux.git

$ cd linux

$ head -5 Makefile
\# SPDX-License-Identifier: GPL-2.0
VERSION = 4
PATCHLEVEL = 19
SUBLEVEL = 57
EXTRAVERSION =
```

Cross-compile the kernel source code for your specific board model as instructed
in the official documentation. For instance, for a Raspberry Pi 3 Model B+
execute

```
$ KERNEL=kernel7

$ make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- bcm2709_defconfig

$ make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage modules dtbs
```

# Loading

Make sure to load the IEEE 802.15.4 "soft MAC"

```
pi@raspberrypi:~ $ sudo modprobe mac802154
```

before loading the driver module

```
pi@raspberrypi:~ $ sudo insmod /path/to/at86rf215.ko
```

This should result in the enumeration of a new network interface

```
pi@raspberrypi:~ $ ip link
...
8: wpan0: <BROADCAST,NOARP> mtu 123 qdisc noop state DOWN mode DEFAULT group default qlen 300
    link/ieee802.15.4 5a:52:9d:09:b9:40:86:73 brd ff:ff:ff:ff:ff:ff:ff:ff
...
```

To unload the driver execute

```
pi@raspberrypi:~ $ sudo rmmod at86rf215
```
