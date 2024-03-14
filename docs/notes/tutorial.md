
# general info

tested on raspberry pi zero 2W, running raspbian bookworm, headless (no GUI)


# how to use the modified cariboulite

steps:
- install the OS
- install dependencies
- install cariboulite
- install cariboulite matteoserva
- compile driver
- compile software

- disable wifi powersave
- load driver

- run tx test
- run transmit samples
- run gnuradio transmit


## install latest raspbian bookworm, 64 bit

raspberry pi imager to install rasbpian bookworm (latest), 64 bit. With or without gui.
The raspberry imager allows you to enable ssh and configure wifi before installing, so there is no need for a monitor.

## install dependencies

apt-get install git socat screen
apt-get install libsoapysdr-dev gnuradio soapyremote-server 
apt-get install buffer
apt-get install gir1.2-gtk-3.0


## install original software

 git clone https://github.com/cariboulabs/cariboulite.git cariboulite_original
 cd cariboulite_original
 ...

## install my branch in cariboulite

git clone https://github.com/matteoserva/cariboulite.git cariboulite_matteo
cd cariboulite_matteo
git checkout transmission_fix

## compile kernel driver

cd cariboulite_matteo/driver
mkdir build
cd build
cmake .. && make

## compile software

cd cariboulite_matteo/software/libcariboulite
mkdir build
cd build
cmake .. && make

## disable wifi powersave

sudo /sbin/iwconfig wlan0 power off


## load fpga

(ignore this)
printf "3\n2\n99\n" | ./cariboulite_test_app 

## launch transmission test

./tx_test -c 1 -f 433125000 -r 4000000 -w 0 - 

## transmit from gnuradio

socat tcp-listen:9999,fork - |  ./tx_test -c 1 -f 433125000 -r 400000 -w 0 -g 0 -e 1 -
 follow the example workflow in gnuradio_tx.png

## planned features

- Soapy TX
  - fix numeric range in exchanged data
  - wrong data arriving at the transmitter

- Raspberry pi 5 support
  The original developers are trying to use the camera port as a high speed device.
  My approach is to use two RP1 dual spi ports. In future this can be replaced with a quad-spi but
  right now the required pins are not routed by the caribou lite.


