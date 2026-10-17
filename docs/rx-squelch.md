# RX noise and carrier squelch

NBFM RX pipelines enable noise squelch by default and leave carrier squelch off.
Menu 14 provides **N** to toggle noise squelch and **C** to toggle carrier squelch.
For NBFM, **S** sets opening and closing noise RMS thresholds while RX runs.
Enter two values, for example `0.200 0.300`, then press Enter; Esc cancels.
The display shows live RMS, the noise detector's decision, each enable state
and the resulting audio gate. Higher thresholds admit noisier signals;
lower thresholds require quieter signals. These choices
survive TX/RX switching and rate changes within that monitor session; a fresh
session restores defaults. Both enabled detectors must permit audio. Turning
both off bypasses the PCM gate; NBFM noise measurement continues for tuning.
Thresholds use 0.001 RMS steps and require `0 < open < close <= 8.0` after
rounding. WBFM has no noise detector or noise-threshold controls.

## Signal path

Noise detection uses 48 kHz demodulated discriminator audio before DC removal,
de-emphasis, the voice low-pass filter and PCM gain. Measuring playback PCM would
make the threshold depend on volume and would discard much of the noise being
measured. `nbfm_demod_process_with_raw` supplies this optional parallel float
output with the same sample count as PCM. The original process API remains a
wrapper with no tap. Adding the tap does not change PCM.

On 2026-10-10, the 50-to-48 kHz linear interpolator was corrected: the residual
phase measures distance backward from the current endpoint, so the output is
`current + fraction * (previous - current)`. The previous reversed weights
created artificial high-frequency energy. For generated clean, full-deviation
3 kHz FM at 1 MS/s, detector RMS decreased from approximately **0.121 to 0.052**,
allowing the original 0.12 opening threshold to qualify. This synthetic result does
not establish a calibrated weak-signal threshold.

`noise_squelch` has two cascaded 6 kHz high-pass biquads followed by a 20 ms
exponential power average. The tap is normalized to approximately +/-1 for
+/-2.5 kHz deviation, not to PCM full scale. It opens below RMS **0.20** after
30 ms of qualifying audio and closes above RMS **0.30** after 120 ms. Between
thresholds it retains its state. It starts closed and resets closed on non-finite
samples. The filter history and noise measurement continue while audio is muted
or noise squelch is disabled. Changing thresholds preserves that history and
the current detector decision, and restarts only the transition qualification.
These starting thresholds were selected by replaying a fresh RF24 capture with
a user-reported weak but intelligible transmission. Its speech RMS was around
0.15–0.20, while idle noise was around 1.2. The former 0.12/0.18 pair never
opened at nominal-timing replay, but opened intermittently with the live
−500 ppm correction. The new pair opens consistently with either correction.
The user subsequently reported satisfactory listening with the updated receiver;
this is not a calibrated sensitivity specification. With all-zero IQ there is no detected
noise, so noise squelch alone can open: it is not a stream-validity detector.

`carrier_squelch` uses the selected AT86RF215 channel's RSSI. It opens at
**-97 dBm or stronger** for three consecutive 10 ms RF blocks and closes below
**-102 dBm** for fifteen blocks. Values in between retain the state. Invalid or
failed reads immediately close the detector. The chip reports signed RSSI,
with 127 indicating invalid, as specified in the
[AT86RF215 datasheet, section 6.2.5.5](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-42415-WIRELESS-AT86RF215_Datasheet.pdf).
These thresholds refer to the modem measurement, not calibrated power at the
external connector or an SDRPlay reading; front-end losses and bandwidth matter.
The squelch detectors do not change AGC configuration. The
[NBFM RX profile](nbfm-rx-sensitivity-context.md#minimum-modem-rx-bandwidth-and-explicit-agc-2026-10-10)
explicitly enables filtered AGC at each RX start.

The NBFM [complex channel filter](nbfm-channel-filter.md) changes the RF noise
reaching the discriminator. Re-evaluate thresholds after receive-chain changes;
disable both squelches for sensitivity measurements.

The RX reader performs one checked register read per captured 10 ms block only
when carrier squelch is enabled. The register follows the selected RF09/RF24
radio. It carries the value and validity with the IQ block through the RF FIFO,
so delayed DSP processing does not use a newer measurement from another block.
This is an end-of-block measurement, not sample-synchronous RSSI. It uses the
shared hardware lock with cancellation disabled until the lock is released.
This adds a short SPI transaction to the reader when enabled. It deliberately
checks the read result rather than using the existing cached-RSSI helper, which
can misinterpret a negative SPI error as a valid signed RSSI value.

Both detectors run in `demod_worker`, outside the standalone NBFM DSP. A combined
gate applies a 5 ms linear fade to PCM before the audio FIFO. Muted PCM still
contains 480 samples per block: clocks, queue feedback and playback keep running.
Existing buffering contributes additional audible latency. There is no new thread
or allocation in processing. Detector resets accompany DSP reset; changing the
enable flags re-qualifies the detectors. Controls and gate status use C atomics;
only the demod worker owns detector/filter state. Ordinary pipeline stop/join
and cleanup still own the hardware and buffers.

## Interfaces and scope

- [noise_squelch.h](../software/libcariboulite/src/noise_squelch.h): caller-owned
  state, `noise_squelch_reset` and one-sample `noise_squelch_process` at 48 kHz.
- [carrier_squelch.h](../software/libcariboulite/src/carrier_squelch.h): caller-owned
  state, `carrier_squelch_reset` and `carrier_squelch_process` once per 10 ms block.
  Both modules are device-independent and allocation-free; callers provide valid
  state pointers and serialize access.
- `rx_params_t.noise_squelch_disabled` defaults false;
  `carrier_squelch_enabled` defaults false. Zero-initialized RX parameters
  therefore select the requested defaults, including the normal RX menu and
  automated baseline runner.
- `rx_params_t.noise_squelch_open_rms` and `noise_squelch_close_rms` both zero
  select the default 0.20/0.30 pair. An invalid supplied pair fails initialization.
- `rx_pipeline_set_squelch(p, noise_enabled, carrier_enabled)` changes enable flags
  during an initialized pipeline's lifetime. The control owner serializes this
  with init/destroy. `rx_pipeline_squelch_open` reports the effective decision,
  not the completion of the fade or the state of already queued playback.
- `rx_pipeline_set_noise_squelch_levels(p, open_rms, close_rms)` publishes a
  validated threshold pair atomically without stopping RX or resetting DSP.
  The control owner serializes pipeline lifetime operations. Menu 14 retains
  the pair across pipeline reinitialization within its session.
- `rx_pipeline_get_noise_squelch_status` returns configured levels and the
  detector RMS/decision. Measurement is valid only during NBFM RX after data
  arrives. The combined gate remains a separate status: bypass or carrier
  squelch can make it differ from the noise detector. The worker publishes
  telemetry once per RF block; display RMS is rounded to 0.001.
- Threshold defaults remain named constants in the module headers. Carrier
  thresholds and timing are unchanged; there is no persistence to disk.

Option 13 and the memory demo remain unsquelched diagnostic compositions.
Their controls do not create a production RX pipeline. Existing standalone DSP
and worker comparisons with both squelches disabled retain exact PCM equality.

## Validation and physical acceptance

Run `python3 software/libcariboulite/tests/test_squelch.py` for independent
hysteresis/timing checks and actual demod-worker tests at 1, 2 and 4 MS/s under
strict and production compiler flags. Tests
include speech-band audio, high-frequency audio noise, synthetic broadband RF
noise, clean FM, strong/weak/invalid RSSI, combined gating, live bypass and
continued PCM production while muted. Runtime tests check pair validation,
preserved detector history, measurement during bypass and threshold changes
without replacing DSP. A parallel-tap test verifies independence
from playback gain and de-emphasis. Lifecycle tests additionally mock RSSI reads
for both channels, disabled sampling, invalid registers and failed SPI access.
No RF hardware is accessed by these software tests. The interpolation-time
oracle and runtime pipeline controls can be reproduced with:

```sh
python3 software/libcariboulite/tests/test_nbfm_interpolation.py
python3 software/libcariboulite/tests/test_squelch.py
python3 software/libcariboulite/tests/test_rx_lifecycle.py
```

Software validation passed: all twelve relevant suites (source, sink, tone,
modulator, demodulator, rate, memory adapters, squelch, RX lifecycle, TX stop,
monitor loopback and baseline-runner reporting), plus the application and
memory-demo builds. The frozen pre-refactoring worker remains an oracle for
framing, progress and clock control. Its original boxcar PCM is no longer the
NBFM numerical oracle: the later channel filter, angle and interpolation
corrections deliberately changed those samples. Current NBFM instances are
compared across independent chunking at all three RF rates; WBFM retains exact
frozen-reference PCM checks.

Jan accepted the earlier squelch implementation after physical testing; see the
historical feedback below. That acceptance predates the current interpolation
and runtime-level changes. Following those changes and the new 0.20/0.30 defaults,
the user also reported satisfactory listening; that feedback did not restate
the RF channel or rate. The following procedure remains available for
regression testing in menu 14 at each of 1, 2 and 4 MS/s; earlier acceptance
does not imply that every listed combination was separately reported:

1. With default noise ON/carrier OFF, start RX without a signal and check that
   noise is muted. Apply the known NBFM signal and check clean opening and speech;
   remove it and check closure without chatter. **N** should restore unsquelched
   noise when disabled.
2. Disable noise squelch with **N**, enable carrier squelch with **C**, and vary
   the received signal level across the opening/closing region. Compare the
   displayed modem RSSI, not the external receiver's power reading. Confirm
   independent opening and closure; then enable both and verify combined action.
3. Check RX stop/start and a stopped rate change, with the selected enable flags
   retained. Confirm option 13 still plays normally.

For threshold calibration, keep carrier squelch disabled, use **N** to listen
ungated if needed, and record RMS for the idle channel, a strong transmission
and the weakest intelligible transmission. Use **S** to select an opening level
above the wanted signal's noise, with a higher closing level below idle-channel
noise. Check fading, speech beginnings and the closing tail. Idle data alone
cannot establish the wanted-signal threshold. The historical RF09 capture
predates the receiver improvements and must not calibrate the current RF24 path.

Production CPU measurements on this Pi put detector processing, control loading
and block-rate telemetry at **8.51–8.63 microseconds per 10 ms audio block**,
about **0.086% of one core**. The added controls/telemetry cost approximately
0.46–0.55 microseconds per block. Seven paired rounds used thread CPU time,
core 3 affinity and production flags; this excludes RF transport and playback.
See the [measurement report](baselines/20261010-nbfm-noise-squelch/benchmark.json).

### Earlier physical feedback

Jan reports that noise squelch works and carrier squelch opens, but initially
would not close. The test signal is approximately -90 dBm and the idle RF24 RSSI
is mostly -102 to -105 dBm. That explains why the original strictly-below
-105 dBm closing threshold did not qualify.

At Jan's suggestion, both thresholds were raised by 3 dB: open at -97 dBm,
close strictly below -102 dBm, retaining 5 dB hysteresis and the existing timing.
Jan confirmed satisfaction with the adjusted result and requested a commit.
Noise and carrier squelch are accepted for the reported setup. A reading of exactly
-102 dBm still resets the 150 ms closing qualification; frequent excursions to
that level may require further tuning. Tested RF rates were not specified.

The earlier apparent echo was confirmed to be simultaneous RSP2pro and
CaribouLite playback, not duplicated CaribouLite audio.
