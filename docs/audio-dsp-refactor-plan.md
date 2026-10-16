# Audio and DSP refactoring plan

Status: H0 accepted by Jan on 2026-09-19; steps 1–8 are implemented; H1–H6 passed. Each numbered step is a small,
reviewable change. The demodulator DSP is now independent of application FIFOs, playback and
threading; its pipeline worker retains those responsibilities.
This is the completed September refactoring record. Later 1 MS/s support,
receiver DSP corrections, squelch/control additions and hardware-paced TX
change some of the original implementation details and validation scope;
see the living [interface reference](audio-dsp-interfaces.md) for current behavior.

Target paths:

```text
alsa_source (or tone_source) -> audio -> nbfm_mod -> IQ -> radio
radio -> IQ -> nbfm_demod -> audio -> alsa_sink
```

Interface reference: [Audio and DSP interfaces](audio-dsp-interfaces.md).
Update that reference in the same change as each interface modification. Mark
proposed contracts separately from implemented ones. Do not change modulation,
filtering, audio levels, defaults or radio routing as part of a mechanical move.

## Working rhythm

For every step: inspect current code, make the smallest change, update interface
documentation, build, run relevant software checks, and review the diff. At each
hardware checkpoint below, stop before the next implementation step until Jan
has confirmed the physical result (or explicitly deferred the checkpoint).
Record failures and resolve them before proceeding. Commit accepted increments
individually when requested; do not combine several checkpoints into one change.

Core software checks used for the refactoring (see [testing](testing.md) for
the current hardware-free suite):

```sh
cmake --build build --target cariboulite_test_app -j2
python3 software/libcariboulite/tests/test_nbfm_rate.py
python3 software/libcariboulite/tests/test_rx_lifecycle.py
python3 software/libcariboulite/tests/test_tx_stop_deadline.py
```

Add focused checks when a new boundary creates a meaningful failure mode. These
checks do not establish physical RF performance.

## Steps

### 0. Record the physical baseline

- [ ] Record revision, Pi model, Raspberry Pi OS/kernel, FPGA image, ALSA device
  names, RF modem/connector route, frequency, gains, and the actual RF test setup.
- [x] Accept H0 from Jan's clean rebuild and physical radio test (2026-09-19).
  This is a functional baseline; see the evidence log for its recorded scope.
- [x] Reconcile the current procedure's radio selection: menus 11/12/14 and the
  baseline runner use `radio_high` (RF24/HiF). Earlier RF09 tests and routing
  observations remain labelled as historical; they are not rewritten as RF24
  acceptance. Missing metadata for the original H0 remains a recorded limitation.

Exit: a reproducible baseline with known limitations and measured comparisons.

### 1. Name the existing ALSA capture adapter clearly

- [x] Rename `alsa48k_source.c/.h` and its symbols to `alsa_source`.
- [x] Keep existing 48 kHz mono capture behavior and configuration unchanged.
- [x] Update callers, tests, build inputs and interface reference.

Checks: application build and lifecycle tests passed. Although no separate
physical checkpoint was required, run `20260919T075122.967948Z` completed all eight
sessions successfully after the rename. Jan confirmed all tones at the correct
pitch on 2026-09-19. [Archived results](baselines/20260919T075122.967948Z/summary.json).

### 2. Introduce the audio-source boundary

- [x] Define a small `audio_source` interface with explicit format, read result,
  buffer ownership, and close semantics; adapt ALSA capture to it.
- [x] Preserve blocking capture and cancellation; test partial reads and failures.
  Unrecoverable errors now stop TX streaming instead of retrying indefinitely.
- [x] Keep supported audio at 48 kHz mono initially; reject unsupported formats.

Software build, source-contract, lifecycle and TX-stop checks passed.

**H1:** run `20260919T075807.545875Z` completed all eight sessions with exit
code 0. Jan confirmed correct tones/pitch and perfect microphone modulation in
both option 14 TX sessions. Runner start/stop cycles passed; additional manual
repeated toggles were not separately reported.
[Archived results](baselines/20260919T075807.545875Z/summary.json).

### 3. Consolidate tone generation

- [x] Implement `tone_source` through the same audio-source interface.
- [x] Route normal TX tone generation through it, preserving amplitude and phase.
- [x] Then migrate self-test tone generation and transient tone/silence injection,
  preserving override priority, duration, phase continuity and shutdown deadlines.
  Split these callers into separate commits if needed.
- [x] Remove the unused old tone interface only after all callers are migrated.
- [x] Test tone pitch, amplitude, continuity across blocks and source switching.

Software waveform, lifecycle and stop-deadline checks accompany the extraction.

**H2 passed (2026-09-19):** run `20260919T080542.756197Z` completed all eight
sessions. Jan confirmed correct audio pitch and that everything works, including
option 13 tested separately after correcting its playback route to the Jabra
loopback bridge. [Archived results](baselines/20260919T080542.756197Z/summary.json).

### 4. Extract ALSA playback without changing the audio pipeline

- [x] Move playback open/configure/write/recovery/close into `alsa_sink.c/.h`.
- [x] Introduce `audio_sink` and route playback through it.
- [x] Keep worker threads, queues and their sizes in their existing owner for now.
- [x] Test partial writes, xrun recovery, failure cleanup and stop behavior.

Software validation (2026-09-19): application build, `test_audio_sink.py`,
source/tone contracts, RX lifecycle, TX stop deadline and NBFM rate checks passed.
This increment retains signed-16-bit mono PCM at the sink boundary; shared float
normalization remains deferred to the DSP/interface steps to preserve samples.
ALSA configuration failures now clean up and report an error, non-48-kHz negotiated
rates are rejected, and a fatal playback write ends the writer with a diagnostic.
No FIFO sizes, worker ownership, RF routing or DSP algorithms changed.

**H3 passed:** Jan confirmed the automated baseline, option 13, known-signal RX
at both RF rates and numerous RX start/stop actions without odd behaviour.
Interactive retuning is deferred until that control exists and is not required
for this ALSA playback extraction. Step 5 may proceed.

### 5. Separate demodulator DSP from its worker thread

- [x] Create explicit NBFM DSP state with create/process/reset/destroy operations.
- [x] Move FIFO access, ALSA diagnostics, scheduling and thread control into a
  pipeline worker; remove ALSA/FIFO dependencies from the DSP header and source.
- [x] Preserve filtering, resampler state, reset semantics and output levels.
- [x] Let the worker measure FIFO depth and provide the existing clock correction;
  keep the fractional resampler in the DSP for this increment.
- [x] Compare deterministic IQ-to-audio output before/after at 2 and 4 MS/s,
  including varying input blocks, output capacities, reset and correction values.

Software checks passed: 345,600 PCM samples from the extracted worker match the
frozen step-4 implementation exactly across 2 and 4 MS/s, with resets, changing
FIFO depths, audio controls, clipping and FIFO-put failures. Standalone DSP checks
cover varied input blocks/output capacities and correction values of 0/±500 ppm.
Lifecycle checks cover failed DSP creation and cancellation of real waiting
workers. The app and all existing audio/rate/lifecycle checks pass.

**H4 passed (2026-09-19):** Jan confirmed option 13, normal baseline completion,
correct tone pitch, clean modulation and extended RX without unusual behaviour.
Jan explicitly accepted H4. Run `20260919T123837.461278Z` completed all eight
sessions with exit code 0. [Archived results](baselines/20260919T123837.461278Z/summary.json).
Exact extended-test duration and per-rate details were not separately supplied;
acceptance records the reported functional result, not quantified clock stability.
Interactive retuning remains deferred until a control exists. Step 6 may proceed.

### 6. Make the modulator boundary explicit

- [x] Document and normalize configuration, reset, processing progress, errors,
  buffer lifetime and supported rates in `nbfm_mod`.
- [x] Establish explicit audio and IQ formats shared with the demodulator.
- [x] Preserve IQ scale and hardware-specific TX bit packing at the radio boundary.
- [x] Test consumed/produced counts, small buffers and block continuity.

The modulator now validates configuration/allocation, reports accepted audio,
produced IQ, held-frequency audio ticks and errors, and supports an explicit cold
reset. Shared `audio_format.h` names float and S16 audio without changing sample
representation; `iq16.h` remains common to both DSP modules. TX and self-test use
the explicit process result; legacy push/pull callers remain supported.

Software checks passed: application build and all eight test suites. The new
modulator check compares 3,120,000 IQ pairs exactly against frozen step-5 code,
covering both RF rates, interpolation modes, pre-emphasis, clipping, silence and
underrun. It also checks varied input/output sizes, queue saturation/retry,
reset, invalid inputs, allocation cleanup and default/full-scale output.

**H5 passed (2026-09-19):** run `20260919T131156.324241Z` completed all eight
sessions with exit code 0. Jan confirmed successful automatic testing and option
13, with clean modulation and correct pitch including test tones.
[Archived results](baselines/20260919T131156.324241Z/summary.json). Step 7 may proceed.

### 7. Move pipeline coordination out of the menu

- [x] Extract shared FIFO implementation into an internal transport module.
- [x] Extract TX lifecycle and worker coordination into `tx_pipeline.c/.h`.
- [x] Extract RX lifecycle and worker coordination into `rx_pipeline.c/.h`.
- [x] Keep each extraction separately reviewable; run lifecycle/stop tests after each.
- [x] Leave configuration selection and status display in `app_menu.c`.

The extraction was performed in three reviewable stages: transport, TX, then RX.
Lifecycle and stop-deadline tests passed after each. The final app build and ten
software suites passed, including DSP waveform comparisons, playback/capture,
monitor loopback and baseline-runner reporting. A move review compared 52 function
bodies with step 6 (ignoring whitespace and the TX worker rename); signal and
lifecycle behavior were retained. Small status accessors now keep menu/runner
callers out of worker frame-size fields and FIFO statistics operations.

New modules: `pipeline_transport`, `pipeline_runtime`, `tx_pipeline`,
`mod_worker`, `rx_pipeline` and `modem_selftest`. The existing `demod_worker`
continues to handle RX DSP coordination. No additional thread is introduced;
FIFO capacities, pacing, cancellation, Quindar timing, routes and sample formats
are unchanged. The obsolete commented-out WBFM worker was removed from the menu.

**H6 passed (2026-09-19):** Jan reports all requested tests pass. Baseline run
`20260919T133517.433185Z` completed normally with exit code 0.
[Archived results](baselines/20260919T133517.433185Z/summary.json).
The observed menu-14 SMI channel flip is the RX-source register returning to its
reset value during TX startup; it does not select TX routing. The display label
was clarified after the physical test. Interactive retuning remains deferred
until that control exists. Step 8 may proceed.

### 8. Prove interchangeability and finish the documentation

- [x] Exercise DSP with memory-backed audio source/sink implementations without
  ALSA or hardware, reusing the documented sample contracts.
- [x] Document module dependencies, error paths, threading, supported formats,
  ownership and an example showing how a new source/sink is connected.
- [x] Document how a future modem plugs into the pipelines. Introduce a shared
  modem operations interface with the second actual modem, unless an earlier
  increment demonstrates a concrete need for one.

Exit: clear source -> modulator and demodulator -> sink paths; no ALSA, UI or radio
control dependencies inside DSP; traceable physical evidence for the production path.

Completed with `memory_audio`, the standalone `nbfm_memory_demo` target and
[extension guide](audio-dsp-extension-guide.md). Memory and tone sources produce
identical PCM at both RF rates, including short reads/writes. The recovered tone
is 599.99 Hz. All eleven relevant software suites and both application/demo
builds pass. Step 8 changes no production signal path; the application hash is
unchanged from `09d89e7`. The guide records the hash and its relationship to H6,
including the subsequent display-only clarification. No new hardware gate is
introduced. Historical baseline metadata gaps and unquantified drift/tolerances
remain documented limitations, not uncompleted refactoring steps.

## Follow-up: preserve requested TX power across radio configuration

Status checked 2026-10-16: the generic driver power-conversion cleanup below
remains open. The TX pipeline now reapplies requested power after rate setup
and activation; the backend additionally checks the calibrated register profile
before sending cue IQ. These local mitigations do not make the generic
bandwidth/rate setters preserve the calibrated code for every caller.

`cariboulite_radio_set_tx_power()` in `software/libcariboulite/src/cariboulite_radio.c`
maps requested dBm to the modem power register using a
channel-specific polynomial. `cariboulite_radio_set_tx_bandwidth()`
instead writes `18 + radio->tx_power`; the TX sample-rate/cutoff setter
does the same. TX activation calls the bandwidth setter, so starting TX can
overwrite the register value selected by the power setter.

- [x] Unify menu 14's requested power under `txpar.tx_power_dbm`, including
  startup setup, pipeline configuration and display; remove the duplicate local
  `tx_power`. The then-requested value was -6 dBm; incremental application build
  passed.
- [ ] Make the power setter the single authority for converting requested dBm
  to the modem power code. Bandwidth and sample-rate/cutoff changes must preserve
  that code and unrelated PA control bits, rather than recomputing power with
  `18 + radio->tx_power`.
- [ ] Audit TX initialization, activation, retuning and restart paths for power
  overwrites. Distinguish requested dBm from register-derived estimates so a
  readback does not silently replace the requested setting.
- [ ] Add focused register-level regression checks for both radio channels:
  setting power followed by bandwidth changes, sample-rate changes and TX
  activation must preserve the selected power code. Cover range limits and
  repeated start/stop sequences.
- [ ] Build `cariboulite_test_app` and update the interface documentation to
  describe requested power, conversion and readback behavior.
- [ ] Compare physical RF levels before and after the correction at fixed
  frequency, route and IQ scale, including -6 and -3 dBm requests. Record modem
  power codes and measured levels; obtain Jan's hardware acceptance.

This correction may change RF output for the same requested dBm because it
removes an existing register overwrite. Jan reported that changing menu 14's
request from -3 to -6 dBm halved the external PA's RF output; this is a useful
relative observation, not an absolute output calibration. Keep requested power
distinct from measured CaribouLite or external PA output.

## Repeatable test runner

See [Physical baseline test](physical-baseline-test.md) for the automated
HiF/Jabra test sequence and Jan's SDRPlay monitoring setup. Its successful run `20260919T074311.996531Z` on the updated Raspberry Pi OS
was confirmed by Jan on 2026-09-19, supplementing the earlier accepted H0.

## Physical checkpoints

H0 and H6 use the full procedure. Intermediate checkpoints use the indicated
subset, always with the same recorded setup and newly built binary.

1. Confirm the intended FPGA image, modem/connector route and ALSA routes.
2. At 4 MS/s, send a known audio tone and real audio through TX; observe the RF
   with the recorded receiver/analyzer. Record pitch, level and audible quality.
3. Feed a known RF signal into RX and observe/record ALSA output. Check pitch,
   level, intelligibility and continuity. Repeat TX and RX at 2 MS/s.
4. Stop streaming before changing sample rate. Repeat start/stop five times and
   check for hangs, stuck streaming, missing audio and new errors. Retuning is
   deferred until an interactive tuning control exists.
5. Exercise menu self-test and monitor controls used in the baseline. Digital
   interface loopback is useful additional evidence, but does not verify RF audio.
6. For full checkpoints and H4, run RX and TX separately for at least five minutes
   per rate; observe underruns/overruns, FIFO depth and drift where reported.
7. Exit normally and restart the app; verify devices can be reopened.

Agree baseline tolerances for pitch, level and stop time at H0, using available
instrumentation. Later results must remain within those tolerances, with no new
hangs, dropouts or errors. Record pre-existing failures as such; a failed baseline
case is not a pass. Do not mark an untested combination as verified.

## Evidence log

| Checkpoint | Revision / dirty diff | Setup / procedure | Measurements and result | Confirmed by |
| --- | --- | --- | --- | --- |
| H0 | `0af6f2d`; working tree clean when recorded | Raspberry Pi OS; Jan deleted the build folder, rebuilt the app using the install script, and physically tested with a radio | Jan reports everything works as expected; accepted functional baseline, no numerical measurements supplied | Jan, 2026-09-19 |
| H1 | See step 2 archive | Eight baseline sessions | Passed, including microphone modulation | Jan, 2026-09-19 |
| H2 | See step 3 archive | Eight baseline sessions plus option 13 | Passed; correct pitch and self-test audio | Jan, 2026-09-19 |
| H3 | Step 4 working tree based on `76e6b9b` | Physical RX at both rates, start/stop and self-test | Passed: baseline, option 13, known-signal RX at both rates, repeated RX start/stop; retuning deferred | Jan, 2026-09-19 |
| H4 | Step 5 working tree based on `b3da533` | Physical comparison including sustained RX/FIFO stability | Passed: baseline, option 13, correct pitch/clean modulation and extended RX; explicitly accepted | Jan, 2026-09-19 |
| H5 | Step 6 working tree based on `185086f` | Tone/ALSA TX at both RF rates and option 13 | Passed: baseline and option 13; clean modulation and correct pitch including tones | Jan, 2026-09-19 |
| H6 | Step 7 working tree based on `0f1ae5b` | Baseline, self-test, switching, sustained audio and exit/restart | Passed: Jan reports all requested tests pass; RX-source display clarification noted | Jan, 2026-09-19 |

Link longer logs or recordings here. Record software and hardware results
separately, including any explicitly deferred tests and remaining limitations.

### H0 acceptance notes

Jan accepted this as a good baseline after the clean rebuild and physical test.
This satisfies the user-confirmation gate for beginning step 1. It does not
establish that every item in the proposed full H0 procedure was exercised.
Specific tested sample rates, TX/RX cases, duration, restart counts, hardware and
firmware details, ALSA routes and numerical tolerances were not supplied. The
remaining baseline checklist items track documentation follow-up, not a request
to repeat the accepted test before step 1. Record those details when available
and use Jan's working setup for subsequent physical comparisons.

### H3 investigation: reported TX level difference

Jan reports option 11 does not open the monitoring receiver's squelch, with
approximately -120 dBm versus -90 dBm for option 14 on the SDRPlay RSP2pro
(first reading supplied as `~120dBm`, interpreted as negative pending confirmation).
These are external receiver readings, not calibrated CaribouLite output power.
The subsequent same-path comparison below resolves the reported TX level concern.

Source inspection finds identical requested TX power (-3 dBm) and IQ scale
(4000) but different **interactive** routes: option 11 uses `radio_low`; option
14 uses `radio_high`/HiF. This difference is also present in pre-step-4 HEAD
`76e6b9b`. The automated baseline runner overrides option 11 to HiF, so this
route difference cannot explain a comparison made within that runner. Jan confirmed the comparison used the interactive menu options. The two tests
therefore selected different radio paths; these measurements do not establish a
TX power regression from step 4. The subsequent baseline runner comparison used the same HiF path. Physical coupling and the exact cause
of the measured 30 dB difference remain unverified. Interactive routing is unchanged.

### Step 4 automated physical retest — 2026-09-19

Run `20260919T121032.048186Z` completed all eight sessions with exit code 0.
Jan confirmed all automated TX levels were approximately -90 dBm on the SDRPlay
RSP2pro, audio quality was good and tone pitch was correct. The same-path test
shows no reported TX level discrepancy between options 11 and 14; the earlier
interactive comparison used different radio paths. No TX routing change is needed
for this refactoring increment.

[Archived results](baselines/20260919T121032.048186Z/summary.json), metadata and
RX RSSI events preserve the runner evidence. Jan also confirmed option 13 tested correctly and known-signal RX passed at
both 2 and 4 MS/s. Jan confirmed numerous RX start/stop actions without odd behaviour. H3 is passed.
Retuning is deferred until an interactive tuning control exists; it is not an
acceptance requirement for the ALSA extraction.
