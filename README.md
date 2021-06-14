# CaribouLite
CaribouLite is an affordable, open-source SDR platform that is also a certified HAT for the Raspberry-Pi family of boards (40-pin versions only). It is built for makers, hackers, and researchers and was designed to complement the SDR current eco-systems offering with a scalable, standalone dual-channel software-defined radio.

<table>
  <tr>
    <td><img src="https://user-images.githubusercontent.com/616259/113452512-ce80d480-940c-11eb-93c7-d980459d7637.png" alt="Top View" height="200"></td>
  </tr>
  <tr>
    <td>Top view</td>
  </tr>
</table>

Unlike many other HAT projects, CaribouLite utilizes the <B>SMI</B> (Secondary Memory Interface) present on all the 40-pin RPI versions. This interface is scarcely documented by both Raspberry-Pi documentation and Broadcomm's reference manuals. An amazing work done by https://iosoft.blog/2020/07/16/raspberry-pi-smi/ (code in https://github.com/jbentham/rpi) in hacking this interface contributes to CaribouLite's technical feasibility. The SMI interface should allows exchanging ~500+ Mbit/s between the RPI and the HAT, and yet, the results vary between the different versions of RPI. The results further depends on the specific RPI version's DMA speeds.

In our application, each ADC sample contains 13 bit (I) and 13 bit (Q), that are streamed with a maximal sample rate of 4 MSPS from the AT86RF215 IC. Such channel requires 4 bytes (samples padded to 32-bit) per sample (and I/Q pair) => 16 MBytes/sec which are 128 MBits/sec. By packing the samples into 24 bit I/Q pair (converting to 12-bit samples by decimating the samples), this required throughput decreases to 96 Mbit/sec. The AT86RF215 IC from Microchip contains two RX I/Q outputs from its ADCs (one for each physical channel - sub-1GHz and 2.4GHz), and a single TX I/Q intput directed to the DACs.

Deeper project details can be found in our Wiki pages - https://github.com/babelbees/CaribouLite/wiki

# Specifications

<B>RF Channels:</B>
- Sub-1GHz: 389.5-510 MHz / 779-1020 MHz
- Wide tuning channel: 30 MHz - 6 GHz (excluding 2398.5-2400 MHz and 2483.5-2485 MHz)

<table>
  <tr>
    <td><img src="https://user-images.githubusercontent.com/616259/113505667-03924180-9549-11eb-8ced-69ead48a2357.png" alt="spectra"></td>
  </tr>
  <tr>
    <td style="text-align:center">Applicable spectra, S1G - sub-1GHz, WB - Wide tuning channel</td>
  </tr>
</table>
<B>Note</B>: The gaps are defined by the design constarints of the system and may not exist in real-life hardware.


<B>FPGA specifications:</B>
- 160 LABs / CLBs
- 1280 Logic Elements / Cells
- 65536 Total RAM bits
- 67 I/Os, Temp: -40-100 degC

<B>Applicable RPI models</B>: RPI_1(B+/A+), RPI_2B, RPI_Zero(Zero/W/WH), RPI_3(B/A+/B+), RPI_4B

<B>Prices @ 2500 units:</B>
- Total: <$38.5
  - BOM: ~$35
  - PCB: <$1
  - PCBA: <$2.5


Parameter                  |  Sub-1GHz                    | Wide Tuning Channel
---------------------------|------------------------------|------------------------------------------------------------------
Frequency tuner range      | 389.5-510 MHz / 779-1020 MHz | 30 MHz - 6 GHz (excluding 2398.5-2400 MHz and 2483.5-2485 MHz)
Sample rate (ADC / DAC)    | 4 MSPS                       | 4 MSPS
Analog bandwidth (Rx / Tx) | <4 MHz                       | <4 MHz
Max Transmit power         | 14.5 dBm                     | >14 dBm @ 30-2400 MHz, >13 dBm @ 2400-6000 MHz
Receive noise figure       | <4.5 dB                      | <4.5 dB @ 30-3500 MHz, <8 dB @ 3500-6000 MHz

# Board Layout
![2d_nums](https://user-images.githubusercontent.com/616259/113452858-94640280-940d-11eb-85b1-502a10127e64.png)

<B>Description:</B>
1. Rasperry-Pi 40-pin connector
2. A modem - AT86RF215
3. TCXO - 0.5 ppm @ 26 MHz
4. FPGA - ICE40LP serier from Lattice Semi.
5. A frequency mixer with integrated synthysizer - RFFC5072
6. External reference clock connector (may be used to acheive coherence between many CaribouLite units.
7. A PMOD connector for FPGA expantion
8. RPI configuration EEPROM (following RPI-HAT specifications)
9. RF front-end - switched, amplifiers, and filters.
10. Reset switch
11. User custom switch + RPI HAT EEPROM reconfiguration (write-enable) switch
12. Wide band SMA connector
13. Sub 1-GHz SMA connector


