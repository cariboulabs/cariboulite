# CaribouLite Software
The software tree contains:

- [libcariboulite](libcariboulite/README.md): C/C++ SDR APIs, optional SoapySDR
  adapter, diagnostic application, audio/DSP pipelines and regression tests.
- [Standalone radio applications](../docs/radio-control.md): backend,
  controller and passive monitor, built separately from
  `libcariboulite/tools/radio_backend/` into `build/radio-backend/`.
- [devicetrees](devicetrees/README.md): HAT EEPROM overlay source and generated blobs.
- [utils](utils/README.md): the binary-to-header generator used during installation.
- [examples](../examples/README.md): C, C++ and Python examples.
- [gr-caribouLite](gr-caribouLite/README.md): native GNU Radio receive source.
  No native transmit block is implemented.

Start with the [installation guide](../installation.md). The SMI kernel driver
lives in [../driver](../driver/README.md), and the FPGA source and images live in
[../firmware](../firmware/README.md). Hardware-free checks are described in
[testing](../docs/testing.md).

# License
<a rel="license" href="http://creativecommons.org/licenses/by/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by/4.0/">Creative Commons Attribution 4.0 International License</a>.
