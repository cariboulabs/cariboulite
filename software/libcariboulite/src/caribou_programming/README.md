# Overview
This Lattice ICE40 programming and management library was originally written by Eric Brombaugh (https://github.com/emeb) and used in https://github.com/emeb/icehat ("...RPi hat which comprises a Lattice iCE40 Ultra FPGA type ice5LP4K-SG48...").

The current [caribou_prog.h](caribou_prog.h) API initializes a programmer with
the shared SPI context, configures from a bitstream file or memory buffer,
controls the configuration-reset pin and releases resources. It is built as
`caribou_prog` by the parent library. Applications should use coordinated radio
controls rather than reset the FPGA while another owner is streaming.
See [FPGA programming and resets](../../../../docs/radio-control.md#fpga-programming-after-reboot)
for the current backend sequence and its `firmware/top.bin` working-directory
requirement.

# License
<a rel="license" href="http://creativecommons.org/licenses/by/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by/4.0/">Creative Commons Attribution 4.0 International License</a>.
