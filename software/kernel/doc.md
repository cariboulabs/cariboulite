# SMI Kernel Module
**Based on a doc. written By Michael Bishop - @CleverCa22 @ "https://github.com/librerpi/rpi-open-firmware"**

- "brcm,bcm2835-smi-dev" - creates a character device, smi_handle must point to the main smi instance
- "brcm,bcm2835-smi" - the main smi instance

In the default mode, there are separate read and write strobes, called SOE and SWE. when the **bus is idle, the rpi will drive the data pins low**.

During the setup SETUP period the strobe is high, and the address pins are presented. Then, if its a read operation, the data pins will switch to input mode at the start of SETUP. If its a write operation, the data will be presented on the data pins.

Then there is the STROBE phase, when either SOE or SWE goes low. For a read (SOE), the rpi will sample the data pins at the end of STROBE.

The HOLD phase just to let the lines settle after after strobe has been released. In the default mode, it does 16bit transfers.

If the full packet is an even multiple of 32bits (4 bytes), then the RPI can use dma to burst the whole thing, with just setup + strobe + hold clocks per 16bit (or 8-bits) transfer. If there is a stray 16bits at the end, the dma will do a dense burst with dma, but then do the stray 16bit seperately a while later

# SMI Clock
and from arch/arm/boot/dts/bcm270x.dtsi
```
Code: Select all

                smi: smi@7e600000 {
...
                        assigned-clocks = <&clocks BCM2835_CLOCK_SMI>;
                        assigned-clock-rates = <125000000>;
```
Here we can see that the DTS sets the default clock of 125MHz to the SMI operation.

## Register SMI_CS (0x00) - Control + Status
| Bit |  Name | Description
|-----|-------|-----------|
|0 |      ENABLE||
|1 |      DONE  |    returns 1 when done|
|2 |      ACTIVE |   returns 1 when doing a transfer
|3 |      START  |   write 1 to start transfer
|4 |      CLEAR  |   write 1 to clear fifo
|5 |      WRITE  |   direction, 1=write, 0=read
|6:7 |    PAD   |    for write, drop the first $PAD bytes in the fifo, for read, drop $PAD bytes from hw, before filling fifo
|8   |   TEEN   |   tear effect mode enabled, transfers will wait for TE trigger
|9    |  INTD   |   interrupt when done
|10   |   INTT  |    interrupt on tx
|11   |   INTR  |    interrupt on rx
|12   |   PVMODE |   enable pixelvalve mode
|13   |   SETERR |   write 1 to clear, turns 1 to signal a change to timing while ACTIVE
|14   |   PXLDAT |   enable pixel transfer modes
|15   |   EDREQ  |   signals an external DREQ level
|24   |   TBD| pixel ready?
|25   |   TBD| axi fifo error, set to 1 to clear, sets itself if you read fifo when empty, or write fifo when full
|26   |   TBD| tx fifo needs writing (under 1/4ths full)
|27   |   TBD| rx fifo needs reading (either 3/4ths full, or DONE and not empty)
|28   |   TBD| tx fifo has room
|29   |   TBD| rx fifo contains data
|30   |   TBD| tx fifo empty
|31  |    TBD| rx fifo full


## Register SMI_L (0x04) - length / count
TBD

## Register SMI_A (0x08) - address
| bits | Name | Description|
|--|--|--|
|  0:5| TBD|     address|
  8:9|TBD|      device address|

## Register SMI_D   0x0c  data


SMI_DSR0  0x10  device0 read settings
SMI_DSW0  0x14  device0 write settings

SMI_DSR1  0x18  device1 read
SMI_DSW1  0x1c  device1 write

SMI_DSR2  0x20
SMI_DSW2  0x24
SMI_DSR3  0x28
SMI_DSW3  0x2c

settings:
0:6     strobe    0-127 clock cycles to assert strobe
7       dreq            use external dma request on sd16 to pace reads from device, sd17 to pace writes
8:14    pace            clock cycles to wait between CS deassertion and start of next xfer
15      paceall         if 1, use the PACE value, even if the next device is different
16:21   hold      0-63  clock cycles between strobe going inactive and cs/addr going inactive
22      read:fsetup 1: setup time only on first xfer after addr change, write: wswap(swap pixel data bits)
23      read:mode68 0:(oe+we) 1:(enable+dir), write: wformat(0=rgb565, 1=32bit rgba8888)
24:29   setup, clock cycles between chipselect/address, and read/write strobe
30:31   width, 00=8bit, 01==16bit, 10=18bit, 11=9bit

SMI_DC    0x30  dma control registers
  24    dma passthru
  28    dma enable
SMI_DCS   0x34  direct control/status register
  0     ENABLE
  1     START
  2     DONE
  3     WRITE
SMI_DA    0x38  direct address register
  0:5   addr
  8:9   device
SMI_DD    0x3c  direct data registers
SMI_FD    0x40  FIFO debug register
  0:5   FCNT    current fifo count
  8:13  FLVL    high tide mark of FIFO count during most recent xfer


notes from experiments done with /dev/smi
the dma on the rpi can only send/recv 32bit chunks to the SMI (see drivers/char/broadcom/bcm2835_smi_dev.c odd_bytes)
any extra has to be sent as a second transaction? with an variable delay after the main burst
the main burst can operate at max speed with very dense packing

a burst involves a repeating setup, strobe, hold, setup, strobe, hold cycle
a burst is always followed by a pace period?

SOE goes low during the strobe period of a read?
the default smi clock on a pi0 is about 125mhz (cat /sys/kernel/debug/clk/smi/clk_rate)
bcm270x.dtsi defines the `assigned-clock-rates` to 125mhz
SA is held during at least strobe and hold, but vanishes during pace, bcm2835_smi.h claims its held during pace!!

during a read, SD0-SD17 are tristated at the start of SETUP, then driven low at the end of HOLD
during a read, SD0-SD17 are sampled at the end of STROBE
if 2 READ's occur back to back, the end of HOLD is the start of SETUP, and SD0-SD17 remain tri-state

in the default mode, SOE will strobe during reads, SWE strobes during writes