# Overview
This directory contains the device-tree overlays to be embedded within the HAT ID EEPROM. Important files:
1. `cariboulite-overlay.dts`: The source file of the device-tree overlay
2. `compile.sh`: run this to compile and create the `dtbo` binary file.
   The binary file is the actual overlay that is read by the linux kernel.

Upon compilation the `dts` file is converted into its binary representation (`dtbo`). Then an h-file is created to embed this binary file into our EEPROM utility (see `cariboulite_dtbo.h` in the h_files sub-dir).


Run `sh compile.sh` from this directory with `dtc` and the built
`../utils/generate_bin_blob` available. It writes generated files here and copies
the header into `../libcariboulite/src/`; it does not program the EEPROM.
A validated EEPROM programming procedure remains
[DOC-05](../../ROADMAP.md#documentation-validation-backlog).

The generator expected by the script is prepared as described in
[utils](../utils/README.md). The script is not fail-fast and uses plain
`mkdir h_files`, so an existing output directory can print an error. Check the
`dtc`, generator and copy results before using any generated artifact.
The overlay enables the Broadcom SMI device/pins; it does not replace the
[separate boot settings](../../installation.md#boot-configuration) for I2C/SPI1
or install the SMI stream driver.
