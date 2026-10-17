# Shared application radio configuration and status

The diagnostic test app and standalone radio backend use the same application
radio policy in `software/libcariboulite/src/radio_profile.c`. The RX/TX pipelines
apply that policy and verify modem register readback. Generic library and Soapy
callers retain their own configuration APIs.

## Profiles and ownership

| Setting | NBFM RX | Mono WBFM RX | NBFM TX |
| --- | --- | --- | --- |
| Supported IQ rates | 1, 2, 4 MS/s | 1, 2, 4 MS/s | 1, 2, 4 MS/s |
| Analog bandwidth/cutoff | 160 kHz bandwidth | 250 kHz bandwidth | 80 kHz cutoff |
| Digital cutoff | Fs/8 | Fs/8 | Fs/8 |
| Additional policy | Automatic gain restored | Existing gain policy retained | 16 us PA ramp; calibrated driver power restored |

Frequency limits follow the selected radio and board variant. Full-board HiF
accepts 1 MHz up to, but excluding, 6 GHz; ISM HiF accepts 2385 through 2495 MHz.
RF09 accepts 377 through 530 MHz and 779 through 1020 MHz. Unknown board types
are rejected. TX requests accept -14 through +14 dBm on RF09, and -12 through
+14 dBm on RF24, without silently clamping application requests.

Pipeline initialization rejects unsupported radio settings and nonfinite audio
parameters before allocating resources, opening audio devices or touching
hardware. The finite checks remain effective with the backend's `-ffast-math`
build flags. Invalid retunes and power changes are rejected before hardware
access. A failed RX tune or profile verification unwinds initialization.

RX initialization establishes and verifies its idle modem profile, and every
RX start restores it. TX initialization and every TX start establish and verify
the NBFM transmit profile. Menu 14 no longer overwrites these profiles with
generic radio defaults. Its deliberate 4 MS/s and 430.100 MHz defaults remain;
the backend defaults to 1 MS/s and 430.125 MHz. Menu 14 explicitly requests the
RF24 minimum of -12 dBm instead of relying on the driver's clamp of -13 dBm.

## Requested settings and register observations

Both apps use the shared `src/modem_snapshot.c` sampler and pure register
decoders. Failed blocks become unavailable for the current sample. The sampler
excludes clear-on-read `IRQS` and `IQIFC2`; diagnostics do not consume the
driver's events. Polling does not use legacy TX getters that update cached
power or write the FPGA sample gap.

Menu 14 labels saved requests separately from decoded register observations,
reads SMI timings and the FPGA gap with checked results on each refresh, and
shows unavailable values explicitly.

The backend preserves its existing JSON fields and adds:

- `config.profile.rx` and `.tx`: desired sample rate, analog bandwidth/cutoff and
  digital cutoff for the next stream start. TX also includes `sample_gap`.
- `config.observed.rx` and `.tx`: sample rate, analog bandwidth/cutoff and digital
  cutoff decoded only from the cached active-radio register sample. Unknown or
  reserved fields are `null`, including during a transport-only dry run.

`hardware_monotonic_s` dates those observations. Sampling consists of sequential
bounded register reads, not an atomic snapshot of the whole radio. A saved
configuration can differ from an idle or older readback until it is applied and
sampled. Existing `config.rx_bandwidth_hz` continues to describe the desired RX
profile; `config.tx_bandwidth_hz` continues to describe observed analog TX cutoff.
These values describe programmed settings; they do not measure RF spectrum,
achieved deviation or calibrated output power.

## Verification

Build both applications, then run the focused hardware-free checks:

```sh
cmake --build build --target cariboulite_test_app -j2
make -C software/libcariboulite/tools/radio_backend
python3 software/libcariboulite/tests/test_radio_profile.py
python3 software/libcariboulite/tests/test_rx_lifecycle.py
python3 software/libcariboulite/tests/test_tx_worker_parking.py
python3 software/libcariboulite/tests/test_tx_hardware_pacing.py
make -C software/libcariboulite/tools/radio_backend test
python3 software/libcariboulite/tests/test_passive_monitor.py
```

The profile tests cover both ordinary and production fast-math flags. Lifecycle
checks cover both radios, supported modes/rates, invalid requests without side
effects, failed readbacks, initialization cleanup, switching and retained TX
starts.

### User on-air confirmation — 2026-10-17

Jan confirmed that both the test app and standalone backend worked on air with
NBFM transmit and receive after this update. WBFM reception also worked properly.
This records functional user acceptance of the shared configuration/status change.
Detailed configuration-specific measurements remain separate; the report does not
provide calibrated power, deviation or spectrum results.

The [RX comparison procedure](rx-application-comparison.md) remains available for
reproducible measurements. The next development milestone is the agreed FPGA
build-headroom work, followed by PA control, as recorded in the [roadmap](../ROADMAP.md).
