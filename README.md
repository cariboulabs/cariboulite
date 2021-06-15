# CaribouLite
CaribouLite is an affordable, educational, open-source SDR platform that is also a HAT for the Raspberry-Pi family of boards (40-pin versions only). It is built for makers, hackers, and researchers and was designed to complement the SDR current eco-systems offering with a scalable, standalone dual-channel software-defined radio. 

<table>
  <tr>
    <td><img src="hardware/rev1/DSC_1044.jpg" alt="Top View"></td>
  </tr>
  <tr>
    <td>CaribouLite SDR mounted on a RPI-Zero</td>
  </tr>
</table>

Unlike many other HAT projects, CaribouLite utilizes the <B>SMI</B> (Secondary Memory Interface) present on all the 40-pin RPI versions. This interface is not thoroughly documented by both Raspberry-Pi documentation and Broadcomm's reference manuals. An amazing work done by [https://iosoft.blog/2020/07/16/raspberry-pi-smi/] (code in [https://github.com/jbentham/rpi]) in hacking this interface contributes to CaribouLite's technical feasibility. A deeper overview of the interface is provided by G.J. Van Loo, 2017 [https://github.com/cariboulabs/cariboulite/blob/main/docs/Secondary%20Memory%20Interface.pdf]. The SMI interface allows exchanging up to ~500Mbit/s between the RPI and the HAT, and yet, the results vary between the different versions of RPI. The results further depend on the specific RPI version's DMA speeds.

In our application, each ADC sample contains 13 bit (I) and 13 bit (Q), that are streamed with a maximal sample rate of 4 MSPS from the AT86RF215 IC. This channel requires 4 bytes (samples padded to 32-bit) per sample (and I/Q pair) => 16 MBytes/sec which are 128 MBits/sec. In addition to the 13 bit for each of I/Q, the Tx/Rx streams of data contain flow control and configuration bits. The modem (AT86RF215) IC by Microchip contains two RX I/Q outputs from its ADCs (one for each physical channel - sub-1GHz and 2.4GHz), and a single TX I/Q intput directed to the DACs.

**A working prototype version** of the board has been produced and tested to meet product requirements. In a meanwhile, a second revision of the board is being produced with the following main updates (see picture below):
1. Image rejection filtering improvement - U10 and U12 (HPF & LPF) - have been replaced by integrated LTCC filters by MiniCircuits
2. Removing FPGA flash - redundant given the fact that the the RPI configures the FPGA in <1sec over SPI.
3. Board layout improvements and overlays (silkscreen) beautification (including logo)
4. More detailed changes in the schematics.

In CaribouLite-R2 the PCB design has been thoroughly re-thought to meet its educational needs. The RF path has been annotated with icons to ease the orientation in the schematics sheets, friendly silk writing was added describing system's components by their functionality rather than logical descriptors, and more.

<table>
  <tr>
    <td><img src="hardware/rev2/pictures/cad_image_bw.png" alt="Top View"></td>
  </tr>
  <tr>
    <td>Top & Bottom view, Production Rev2</td>
  </tr>
</table>

**Deeper project details will be published shortly in our Wiki pages.**

# Specifications

<B>RF Channels:</B>
- Sub-1GHz: 389.5-510 MHz / 779-1020 MHz
- Wide tuning channel: 30 MHz - 6 GHz (excluding 2398.5-2400 MHz and 2483.5-2485 MHz)

<table>
  <tr>
    <td><img src="hardware/rev1/frequencies.png" alt="spectra"></td>
  </tr>
  <tr>
    <td style="text-align:center">Applicable spectra, S1G - sub-1GHz, WB - Wide tuning channel</td>
  </tr>
</table>
<B>Note</B>: 
The gaps are defined by the design constarints of the system and may not exist in real-life hardware. Actual modem synthisizer outputs test show wider margins ar room temperature than those written in the datatsheet, but, as noted by Microchip, performance may suffer.


<B>FPGA specifications:</B>
- 160 LABs / CLBs
- 1280 Logic Elements / Cells
- 65536 Total RAM bits
- 67 I/Os, Temp: -40-100 degC

<B>Applicable RPI models</B>: RPI_1(B+/A+), RPI_2B, RPI_Zero(Zero/W/WH), RPI_3(B/A+/B+), RPI_4B

Parameter                  |  Sub-1GHz                    | Wide Tuning Channel
---------------------------|------------------------------|------------------------------------------------------------------
Frequency tuner range      | 389.5-510 MHz / 779-1020 MHz | 30 MHz - 6 GHz (excluding 2398.5-2400 MHz and 2483.5-2485 MHz)
Sample rate (ADC / DAC)    | 4 MSPS                       | 4 MSPS
Analog bandwidth (Rx / Tx) | <4 MHz                       | <4 MHz
Max Transmit power         | 14.5 dBm                     | >14 dBm @ 30-2400 MHz, >13 dBm @ 2400-6000 MHz
Receive noise figure       | <4.5 dB                      | <4.5 dB @ 30-3500 MHz, <8 dB @ 3500-6000 MHz

<B>Note</B>: 
(1) Feature comparison table with other SDR devices will be published shortly
(2) Some of the above specifications are simulated rather than tested

# Board Layout
![2d_nums](hardware/rev1/2d_nums.png)

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
