# CaribouLite development fork

This repository builds on the original CaribouLabs hardware, FPGA and software
project. This fork adds NBFM TX/RX, mono WBFM RX, audio pipelines, streaming
fixes, diagnostics and standalone radio control.

The source reviewed on **16 October 2026** is
[`main`](https://github.com/Habraken/cariboulite/tree/main) at merge
[`069fab3`](https://github.com/Habraken/cariboulite/commit/069fab3).
The merge brings the standalone radio backend, controller, passive monitor
and hardware-paced TX into `main`, following the FM refactoring through
step 4 merged on 4 October.

## Start here

- [Installation](https://github.com/Habraken/cariboulite/blob/main/installation.md): current installer behavior, boot configuration and checks.
- [Additional setup and audio notes](https://github.com/Habraken/cariboulite/blob/main/ADDITIONAL-README.md): reported Pi Zero 2 W / 64-bit Trixie installation, audio routing and historical recipes.
- [Radio backend and controller](https://github.com/Habraken/cariboulite/blob/main/docs/radio-control.md): independent C applications for RX and half-duplex NBFM TX.
- [Passive monitor](https://github.com/Habraken/cariboulite/blob/main/docs/passive-monitor.md): optional Python status display in another terminal.
- [Hardware-free checks](https://github.com/Habraken/cariboulite/blob/main/docs/testing.md): software regression entry points and prerequisites.
- [Documentation index](https://github.com/Habraken/cariboulite/blob/main/docs/README.md): architecture, diagnostics, validation records and hardware references.

## Current applications and modes

The backend owns the radio, SMI/SPI, DSP and audio devices. The controller uses
a Unix-domain command socket for separate RX/TX frequencies and rates,
NBFM/mono-WBFM receive selection, squelch, microphone/tone TX and FPGA programming
or reset commands. Transmission requires an interactive controller to remain
connected; normal stop restores RX when RX was running before TX.
The passive monitor is optional and cannot key the transmitter.

RX starts stopped by default; use `--rx-start` to receive automatically.
Launch options use `--rx-` and `--tx-` prefixes, including independent
`--rx-deviation` and `--tx-deviation` settings. The controller displays compact
Receive/Transmit sections. With both directions stopped, bare `frequency`,
`mode` and `rate` commands update both where supported; `mode wbfm` changes
RX while TX remains NBFM. Use `rx frequency`, `rx mode`, `rx rate`,
`tx frequency` or `tx rate` for independent settings. The linked
[controller guide](https://github.com/Habraken/cariboulite/blob/main/docs/radio-control.md)
contains the current commands and launch examples.

NBFM TX now selects the narrowest modem transmit filters: an 80 kHz analog
cutoff and a digital cutoff of one eighth the sample rate. The passive
monitor's TX bandwidth reports the cached analog-filter readback; it does
not measure occupied signal bandwidth. Spectrum and achieved deviation
remain unmeasured for this filter profile.

The original diagnostic application remains available. Run it from the
repository root so menu 3 can resolve `firmware/top.bin`:

```sh
./build/cariboulite_test_app 2>debug.log
```

Its FM entries are 11 (NBFM TX tone), 12 (NBFM RX), 13 (NBFM modem self-test)
and 14 (modem monitor with TX/RX and digital interface-loopback controls).
Menu 14 selects RF24/HiF, defaults to 430.100 MHz and 4 MS/s, and uses ALSA
Loopback devices. Pressing `T` starts/toggles the configured TX source; it does
not automatically select a tone. Stopped-only controls select NBFM/mono-WBFM,
1/2/4 MS/s and separate RX/TX frequencies.

NBFM supports noise and carrier squelch. WBFM supports carrier squelch; stereo
and RDS decoding are not implemented. CW listening, AM, SSB, PSK and the
planned menu-15 transceiver remain future work. The digital FPGA loopback
checks part of the sample path rather than an RF contact.

## Tested hardware and current limits

A fresh Pi Zero 2 W / 64-bit Raspberry Pi OS Trixie installation was reported
on 14 October. The subsequent hardware-paced TX build passed microphone/tone
transport trials at **1, 2 and 4 MS/s on both Pi Zero 2 W and Pi 4**, with
kernel **`6.18.50+rpt-rpi-v8`**, and same-backend RX restoration at 1/4 MS/s.
The user also reported successful modulation listening checks. See the
[Pi Zero record](https://github.com/Habraken/cariboulite/blob/main/docs/pi-zero-tx-queue-test-2026-10-15.md),
[Pi 4 record](https://github.com/Habraken/cariboulite/blob/main/docs/pi4-tx-validation-2026-10-15.md)
and [TX design](https://github.com/Habraken/cariboulite/blob/main/docs/tx-hardware-paced-design.md).

These records identify exact tested configurations. The trials used separately
staged builds and temporary driver reloads; they do not establish that an
existing installation has been upgraded. The current TX pipeline requires its
matching driver ABI and write-timing profile. Rebuild the SMI module against
matching headers after a kernel change, and keep the userspace library,
applications, driver and FPGA image consistent. Git checkout alone does not
update installed binaries or the loaded firmware.

Independent RF-clock accuracy and spectrum quality remain unqualified.
Physical Soapy RX observations exist, while complete Soapy TX and GNU Radio
workflows remain under validation. The native GNU Radio module supplies a
receive source only. Raspberry Pi 5 is not supported by this SMI hardware path.

## Further development

The [roadmap](https://github.com/Habraken/cariboulite/blob/main/ROADMAP.md)
tracks shared-radio interfaces, configurable transceiver controls, additional
modes, FPGA antenna/PTT/PA sequencing, Ethernet audio/PTT, GNU Radio integration
and hardware design work. It distinguishes implemented work from milestones
that still require acceptance.

For DSP interfaces, filtering and evidence behind the overview, see:

- [Audio/DSP interfaces](https://github.com/Habraken/cariboulite/blob/main/docs/audio-dsp-interfaces.md)
- [NBFM channel filter](https://github.com/Habraken/cariboulite/blob/main/docs/nbfm-channel-filter.md)
- [FM architecture and accepted extraction](https://github.com/Habraken/cariboulite/blob/main/docs/demodulator-architecture-research.md)
- [FPGA tests and coverage limits](https://github.com/Habraken/cariboulite/blob/main/firmware/tests/README.md)
- [Documentation audit and open validation](https://github.com/Habraken/cariboulite/blob/main/docs/documentation-audit-2026-09-16.md)

Useful issue reports include the source revision, Pi model, OS/kernel, FPGA
image, application/API path and reproduction steps.
