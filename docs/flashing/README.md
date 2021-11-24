# EEPROM
Each CaribouLite is pre-configured by the contract manufacturer before shipping. The Full and the ISM versions are further distinguishable by the configuration resistors placed on the back side of the board (marked as "**CFG/1234**").

CaribouLite doesn't have an FPGA configuration flash device, as the ICE40 device is dynamically configured by the RPI quickly and on-demand. It rather has a general board configuration device (EEPROM) as required by RPi's HAT device rules.

## EEPROM Data Structure
The code is located [here](https://github.com/cariboulabs/cariboulite/tree/main/software/libcariboulite/src/cariboulite_eeprom) and is based on [RPi EERPROM Utils Tools](https://github.com/raspberrypi/hats/tree/master/eepromutils) provided by RaspberryPi. The ID EEPROM internal structure is described [here](https://github.com/raspberrypi/hats/blob/master/eeprom-format.md).

The general structure is as follows:
1. **HEADER**: containing a valid header key (SIGN), its version, the number of ATOMs that follow the header and the total size of the EEPROM contents.
2. **VENDOR INFO**: Product information and Vendor information, and the product UID (unique 128bit identifier / serial number).
3. **GPIO MAP**: a listing of CaribouLite's RPi GPIO states and flags, for each GPIO pin (GPIO2-GPIO27).
4. **DEVICE TREE**: as the system is booting, the RPi kernel automatically reads this ATOM from the EEPROM and sets it as an overlay. As a result, the kernel modules needed for CaribouLite to operate (SMI, SPI, etc.) are probed on startup and configured according to the overlay.
5. **PROPRIETARY DATA**: this part contains additional configuration information for the board - I/Q imbalance calibration values, Tx-Power calibration, TCXO calibration, GPIO and PMOD states, etc. Some of these values shall be programmed by the CM, others are just place-holders for the user to better organize his work.

## Gracefully Accessing Data
To access the EEPROM data, one can use our code which also validates and decodes the information. The easier way to access the information within the programmed EEPROM is to use Raspbian's Linux `sysfs` as follows:

```
cd /proc/device-tree/hat/

# name
cat name

# product: "CaribouLite RPI Hat"
cat product

# product_id: "0x0001"
cat product_id

# product_ver: "0x0001"
cat product_ver

# uuid: "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE" GUID format
#       which is also the board's raw serial number
cat uuid

# vendor: "CaribouLabs.co"
cat vendor
```

## Reprogramming the EEPROM
Reprogramming the EEPROM is possible using our EEPROM utilities.
More info - coming soon.

# License
<a rel="license" href="http://creativecommons.org/licenses/by-sa/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by-sa/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by-sa/4.0/">Creative Commons Attribution-ShareAlike 4.0 International License</a>.
