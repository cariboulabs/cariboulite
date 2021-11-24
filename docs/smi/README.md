# Overview
The SMI interface is used as memory interface that pipes the I/Q complex samples from the CaribouLite to the RPI Broadcomm SoC (on RX) and from the Broadcomm SoC to the board (on TX).
A single ADC sample contains 13 bit (I) and 13 bit (Q), that are streamed with a maximal sample rate of 4 MSPS from the AT86RF215 IC to an FPGA. The FPGA emulated SMI compliant memory interface for the RPI SoC.
Each RF channel (CaribouLite has two of them) requires 4 bytes (samples padded to 32-bit) per sample (and I/Q pair) => 16 MBytes/sec which are 128 MBits/sec. In addition to the 13 bit for each of I/Q, the Tx/Rx streams of data contain flow control and configuration bits. The modem (AT86RF215) IC by Microchip contains two RX I/Q outputs from its ADCs (one for each physical channel - sub-1GHz and 2.4GHz), and a single TX I/Q input directed to the DACs.

# API
Our implemented API (user-mode) driver is located in: [Caribou-SMI API Driver](https://github.com/cariboulabs/cariboulite/blob/main/software/libcariboulite/src/caribou_smi/README.md)

# Connections and Description
Coming soon

# Operation
Coming soon