# Pi 4 hardware-paced TX validation — 2026-10-15

Current-source note (2026-10-16): the tested implementation is committed in
`37d2414` and is built by the current installer. Candidate paths, loaded-module
state and preserved installed-file claims below record the October 15 test
session. They do not establish present runtime state. For current setup use
[radio control](radio-control.md#build-and-start) and the
[driver guide](../driver/README.md); frozen staging paths are for reproducing
this experiment.

The CaribouLite was transferred from the Pi Zero to the Pi 4. The user confirmed
the dummy load and requested **430.100 MHz** for all TX tests. Validation uses
the separately staged candidate; permanent installation remains deferred.

Paths under `build/` identify raw artifacts retained on the original test hosts,
excluded from Git and unavailable in a fresh clone. The measurements summarized
here are versioned; the committed collection tools can produce fresh artifacts.

## Candidate and setup

- Host: Raspberry Pi 4 Model B Rev 1.5, aarch64,
  kernel `6.18.50+rpt-rpi-v8`.
- Candidate: `build/tx-hardware-paced-pi4-20261015`; all 1107 files match the
  frozen Pi 4 staging manifest. Its 15 recorded runtime-source files, shared
  library, firmware and candidate driver match the passing Zero build.
- Runtime driver: `29FA4AB8842B3807F97F0A5`, loaded with the staged rollback
  helper. Installed driver `697F3CF2096C282E312BA14` remains unchanged.
- Driver geometry: 131072-byte periods, 524288-byte ring; bounded TX admission
  is two periods. The initial SMI clock readback was 125000003 Hz.
- Radio: RF24, TX 430100000 Hz, requested −12 dBm.
- Audio: Jabra SPEAK 510 USB, native 16 kHz mono capture, converted by ALSA
  `plughw` to 48 kHz/S16_LE/mono. Temporary bridges reproduce the Zero route:
  USB capture → `plughw:Loopback,0,1`, and `plughw:Loopback,1,0` → USB playback.
  Backend microphone/playback are `plughw:Loopback,1,1` and
  `plughw:Loopback,0,0`. The owned bridges have a finite lifetime and cleanup.

The module reload at **19:16:53 Brussels time** verified the new ABI while idle
and saved the original module in
`build/tx-hardware-paced-pi4-20261015/driver/reload-backups/20261015T171653Z-4027/`.
No installed module, firmware or audio configuration file was replaced.

## Short checks — passed

Six five-second microphone/tone trials at 1/2/4 MS/s completed with backend and
controller exit codes zero and no cleanup errors. All stable driver-underrun,
writer-error and microphone capture/ASRC loss/fault deltas were zero. Actual TX
timing read setup/strobe/hold/pace **1/4/2/1**. The same-host provenance audit
passed after the run, including unchanged installed/current files, measured
candidate files and released radio ownership/private sockets.

Artifacts: short-run report (`build/radio-backend/tx-hardware-paced-pi4-smoke-20261015/README.md`)
and provenance audit (`build/radio-backend/tx-hardware-paced-pi4-smoke-20261015/provenance-after.json`).

## Full timed qualification — passed

The six-case run completed between approximately **19:20:49 and 19:39:01
Brussels time**. Each case targeted 180 seconds of stable TX: microphone
1/2/4 MS/s, then tone 1/2/4 MS/s. Stable snapshot endpoints span 179.6–179.7
seconds. The frozen, board-independent qualification passed all **216 checks**.

| Source / rate | Stable seconds | Completed frames/s | Driver-dequeued IQ, MS/s | Producer / writer CPU, % of one core |
| --- | ---: | ---: | ---: | ---: |
| Microphone / 1 MS/s | 179.660 | 100.0056 | 1.000039 | 5.08 / 1.99 |
| Microphone / 2 MS/s | 179.645 | 99.9971 | 1.999877 | 12.33 / 2.84 |
| Microphone / 4 MS/s | 179.689 | 100.0004 | 4.000049 | 17.31 / 7.08 |
| Tone / 1 MS/s | 179.683 | 100.0097 | 1.000095 | 5.00 / 1.98 |
| Tone / 2 MS/s | 179.663 | 100.0037 | 2.000040 | 8.85 / 4.22 |
| Tone / 4 MS/s | 179.699 | 100.0008 | 4.000027 | 16.79 / 7.14 |

All six cases had zero cumulative driver underruns in valid startup, healthy
TX and post-stop observations, zero writer errors/drops and no terminal fault.
Microphone capture overrun/read/recovery-error totals and audio-buffer
underflow/overflow/clock-guard totals were zero. Capture worker CPU used
1.45/1.45/1.74% of one core at 1/2/4 MS/s. Intentional cue replacement is
reported separately from stable loss. Transition snapshots can omit metrics;
surrounding valid cumulative counters retain their outcomes.

Actual TX setup/strobe/hold/pace read **1/4/2/1**; every post-stop idle readback
restored **1/5/1/1**. Application capacity stayed at four frames, and kernel
admission stayed within two native periods. A full application queue is bounded
prefill. The separate provenance audit passed: backend/controller exited zero,
cleanup had no errors, measured/preserved files matched their hashes, and no
radio owner, tested backend or private socket remained.

At 4 MS/s, producer CPU was 17.31/16.79% for microphone/tone on the Pi 4,
versus 54.67/53.78% in the passing Zero run. The Pi 4's sampled clock varied
between 0.9 and 1.8 GHz (1.0–1.8 GHz for the 4 MS/s microphone case), with
per-case sampled means around 1.70–1.79 GHz; the Zero ran around 1.0 GHz.
These are descriptive results across different hosts and clock conditions.

Artifacts: timed report (`build/radio-backend/tx-hardware-paced-pi4-production-20261015/README.md`),
independent qualification (`build/radio-backend/tx-hardware-paced-pi4-production-20261015/qualification.json`),
provenance audit (`build/radio-backend/tx-hardware-paced-pi4-production-20261015/provenance-after.json`)
and Zero comparison (`build/radio-backend/tx-hardware-paced-pi4-production-20261015/versus-zero.json`).

## Physical automatic RX restoration — passed

Within one backend process, stable RX was followed by five-second tone TX and
normal stop at 1 and 4 MS/s. RX resumed automatically with its configuration
preserved and the original write timing restored. All TX used 430.100 MHz and
requested −12 dBm.

| Resumed RX rate | Stable seconds | Application-reader IQ samples/s | Kernel DMA movement, samples/s |
| --- | ---: | ---: | ---: |
| 1 MS/s | 29.883 | 999885.06 | 999982.91 |
| 4 MS/s | 29.886 | 4000574.56 | 3999949.50 |

Kernel rates use their own callback intervals; application estimates are
quantized by frame endpoints. IQ/audio queue drops and put timeouts, reader
errors, playback underruns/recovery failures/write errors and kernel missed
counts were zero. RX workers remained healthy. The complete canonical idle
SMI settings and DMA geometry matched before and after both handoffs.

The handoff and provenance audits passed, including final modem/SMI idle,
backend/controller exits zero, unchanged original/staged files and released
radio ownership/private sockets. The temporary audio bridges remained intact
through the measurements, then their supervisor was stopped successfully and
reaped its four owned children. No audio bridge service or configuration file
was installed.

Artifacts: handoff results (`build/radio-backend/tx-rx-handoff-pi4-20261015/handoff-summary.json`),
handoff provenance (`build/radio-backend/tx-rx-handoff-pi4-20261015/provenance-after.json`)
and audio lifecycle (`build/radio-backend/tx-hardware-paced-pi4-audio-20261015/manifest.json`).

## User RSP2pro listening check — 4 MS/s (2026-10-15)

After the automated validation, the user manually tested the staged Pi 4
application at **4 MS/s**, with a direct cable connection to an **RSP2pro**
through **30 dB / 2 W and 20 dB / 2 W attenuators** in series. Their nominal
total attenuation is 50 dB. **SDRconnect, with AGC enabled, indicated −61.2 dBm.**

The user described the modulation audio as **perfect**, with **no noticeable
hiss when the microphone was quiet**. This provides qualitative listening
confirmation for the Pi 4's 4 MS/s path in addition to the automated transport
and RX restoration results.

Adding the nominal attenuation to the indicated receiver level gives
`−61.2 + 50 = −11.2 dBm` before the attenuators, neglecting cable loss and
attenuation/display corrections. This is a conditional level estimate, close
to the −12 dBm setting used in the earlier automated tests. The manual TX
power setting and receiver measurement conditions were not independently
verified, so this observation does not establish calibrated TX power, SINAD,
deviation, frequency accuracy or spectrum.

## Recorded state after automated validation (2026-10-15)

The staged candidate has passed the recorded Pi 4 transport and RX restoration
checks. At the end of this test, the candidate driver remained loaded for that
boot and the radio was idle. Installed driver and current-checkout
binaries/library/firmware were unchanged; permanent deployment was deferred.
A reboot at that stage restored the installed original driver; candidate reload
requires the matching kernel and the staged helper described in
[radio control](radio-control.md#pi-4-staging-checkout-2026-10-15).

The validation's temporary audio bridges stopped. A later manual microphone
test needs its audio route started separately. The audio supervisor below is a
local test artifact, unavailable in a fresh clone. If it is retained on the
original host, it can recreate the measured routing for up to 40 minutes.
Run it in a dedicated terminal and wait for the `ready` event before opening
the backend. Ctrl+C cleans up these owned bridges; do so after stopping the
radio test. For a new checkout, use available ALSA devices with the current
[backend audio setup](radio-control.md#build-and-start).

```sh
cd /home/pi/src/cariboulite
python3 -B build/radio-backend/tx-hardware-paced-pi4-validation-20261015/audio_bridges.py \
  --seconds 2400 \
  --output "build/radio-backend/pi4-manual-audio-$(date -u +%Y%m%dT%H%M%SZ)"
```

The staged backend/controller commands use microphone `plughw:Loopback,1,1`,
playback `plughw:Loopback,0,0` and TX frequency 430100000 Hz, as in these
measurements. The audio supervisor records metadata/stderr only and saves no
audio recording. Its `ready` event confirms process health; the timed test
results above separately establish successful audio flow.

Transport counters describe host/SMI sample movement. They do not independently
measure RF waveform quality, deviation, frequency accuracy or spectrum.

## Evidence

- Staging manifest (`build/tx-hardware-paced-pi4-20261015/staging-manifest.json`).
- Preparation and original-file fingerprints (`build/radio-backend/tx-hardware-paced-pi4-validation-20261015/preparation.json`).
- Temporary audio bridge metadata (`build/radio-backend/tx-hardware-paced-pi4-audio-20261015/manifest.json`).
- Final idle, ownership, audio cleanup and original-file check (`build/radio-backend/tx-hardware-paced-pi4-validation-20261015/final-state.json`).
- [Zero comparison and design](tx-hardware-paced-design.md).
