# Documentation

Current guides describe `main`, including the `dev-passive-monitor` merge at
`069fab3` on 2026-10-16. Dated investigations and measurements
retain their original revision and test conditions. See the
[current audit](documentation-audit-2026-10-16.md) for coverage and limitations.

## Setup and operation

- [Project overview and quick start](../README.md)
- [Installation guide](../installation.md)
- [Additional installation and usage notes](../ADDITIONAL-README.md)
- [Library build and APIs](../software/libcariboulite/README.md)
- [Examples](../examples/README.md)
- [Hardware-free tests and physical validation entry points](testing.md)
- [Standalone backend, controller and radio commands](radio-control.md)
- [Passive terminal monitor](passive-monitor.md)
- [Menu 14 frequency controls](monitor-frequency.md)
- [RX squelch](rx-squelch.md)
- [Scheduled menu 14 IQ recording](scheduled-menu14-iq-capture.md)
- [Physical RX application comparison](rx-application-comparison.md)

## Architecture and development

- [Development roadmap and unresolved documentation tasks](../ROADMAP.md)
- [FM architecture and accepted refactoring steps](demodulator-architecture-research.md)
- [Modulator architecture and remaining work](modulator-architecture-research.md)
- [Audio/DSP extraction plan and completion record](audio-dsp-refactor-plan.md)
- [Runtime profiling, stereo/RDS, DAB+ and adjustable CTCSS plan](runtime-profiling-and-decoder-plan.md)
- [NBFM complex channel filter](nbfm-channel-filter.md)
- [NBFM discriminator angle correction and CPU measurements](nbfm-discriminator-angle.md)
- [Audio/DSP interfaces](audio-dsp-interfaces.md)
- [Shared application radio configuration and status](shared-radio-configuration.md)
- [Audio/DSP extension guide](audio-dsp-extension-guide.md)
- [Hardware-paced TX implementation](tx-hardware-paced-design.md)
- [TX tail timing and drain contract](tx-tail-timing.md)
- [Wiki overview source](wiki/Home.md)

## Hardware, driver and FPGA

- [Hardware revisions, clocking and outstanding electrical specifications](../hardware/README.md)
- [Kernel driver build, installation and TX telemetry](../driver/README.md)
- [SMI interface](smi/README.md)
- [Userspace SMI API](../software/libcariboulite/src/caribou_smi/README.md)
- [EEPROM notes](flashing/README.md)
- [FPGA build and registers](../firmware/README.md)
- [FPGA simulation coverage](../firmware/tests/README.md)
- [Next FPGA test: LVDS Fmax before sequencer integration](lvds-fmax-next-test-2026-10-08.md)
- [Rev2.8 quotation package and manufacturing holds](../manufacturing/rev2.8-full-eurocircuits/README.md)

## Recorded investigations and measurements

- [Pi Zero 2 W TX baseline and hardware-paced follow-up](pi-zero-tx-queue-test-2026-10-15.md)
- [Pi 4 hardware-paced TX validation](pi4-tx-validation-2026-10-15.md)
- [Menu 14 NBFM RX chain and sensitivity investigation](nbfm-rx-sensitivity-context.md)
- [Physical baseline procedure](physical-baseline-test.md)
- [FM comparison baseline](baselines/20261003-fm-comparison/README.md)
- [Step-4 physical evidence](baselines/20261003-fm-step4/README.md)
- [September software baseline](software-baseline-2026-09-13.md)
- [September code review](code-review-2026-09-12.md)
- [September FPGA gap study](fpga-gap-study-2026-09-13.md)
- [FPGA/SMI engineering history](../cariboulite_fpga_smi_engineering_history.md)
- [September documentation audit](documentation-audit-2026-09-16.md)
- [Rev2.8 reproduction handoff and subsequent corrections](../cariboulite_rev2_8_reproduction_context.md)
- [Initial sourcing snapshot](../cariboulite_rev2_8_bom_sourcing_first_pass.md)
- [Manufacturer sourcing follow-up snapshot](../cariboulite_rev2_8_bom_sourcing_manufacturer_update.md)

# License
<a rel="license" href="http://creativecommons.org/licenses/by-sa/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by-sa/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by-sa/4.0/">Creative Commons Attribution-ShareAlike 4.0 International License</a>.
