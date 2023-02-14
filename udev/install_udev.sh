#! /bin/bash

# List all udev related files
#dpkg -L udev

cp 40-cariboulite.rules /etc/udev/rules.d/
udevadm control --reload-rules && udevadm trigger