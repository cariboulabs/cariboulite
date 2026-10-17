# NBFM discriminator angle correction

Implemented 2026-10-10 after the complex channel filter and earlier successful
listening tests on RF09 and RF24. A subsequent listening test confirmed the
angle correction on **HiF (RF24) at 1 MS/s**.

## Calculation

NBFM now uses `atan2f(im, re)` on the current IQ sample multiplied by the
conjugate of the previous sample. This calculates the phase difference in all
four quadrants, within −π to +π, as described in the
[GNU C library documentation](https://sourceware.org/glibc/manual/latest/html_node/Inverse-Trig-Functions.html).
If both components of that product are zero, the implementation returns zero:
there is no phase information, and signed zero must not create a ±π impulse.

The previous `fast_atan2f_small` approximation had incorrect quadrant handling.
For example, a true 2-radian phase difference returned approximately 4.50
radians. A dense full-circle sweep found a worst absolute error of 5.14 radians.
Even in the nominal ±2.5 kHz range, its worst error was 0.0163 radians, about
5.2% of the full-scale phase increment. The new helper's worst measured error
against double-precision `atan2` was **1.19 × 10⁻⁷ radians**.

The calculation runs after channel filtering at **50 kS/s**, rather than at the
1/2/4 MS/s RF input rate. The implementation is in
[fm_discriminator.h](../software/libcariboulite/src/fm_discriminator.h), used by
[nbfm_demod_dsp.c](../software/libcariboulite/src/nbfm_demod_dsp.c).
NBFM retains its limiter and ±2.5 kHz normalization. WBFM already uses full
`atan2f` and continues to pass exact frozen-reference comparisons.

## CPU measurement

The [benchmark report](baselines/20261010-nbfm-discriminator/benchmark.json)
compares the old and corrected angle calculations using the same channel
filter, limiter and audio processing. It uses Raspberry Pi 4 / Cortex-A72,
GCC 14.2, the production optimization flags, CPU affinity to core 3 and thread
CPU time. The post-run governor was `ondemand` and frequency was 1.8 GHz.
Seven rounds of 100 paired, randomly interleaved 10 ms blocks were measured for
each policy and sample rate, using both clean multitone FM with +500 Hz tuning
offset and deterministic full-band RF noise. Input generation and warm-up are
outside the timed region.

| RF rate | Old DSP CPU (% of one core) | Corrected DSP CPU (% of one core) | Added CPU (percentage points) |
| --- | --- | --- | --- |
| 1 MS/s | 2.66–2.67% | 2.96% | About 0.30 |
| 2 MS/s | 3.13–3.14% | 3.45% | About 0.32 |
| 4 MS/s | 4.20–4.21% | 4.53% | About 0.32 |

The paired median increase is **29.5–32.4 µs per 10 ms block**. The largest
corrected block CPU time measured was **0.517 ms**. These percentages cover
standalone DSP only, including the raw tap, and exclude radio transport,
queues, squelch, ALSA and other threads. They are CPU time measurements rather
than end-to-end latency or a bound on scheduler delays. Full `atan2f` provides
ample measured CPU headroom here, so the production path uses the standard
library calculation.

Reproduce the comparison without radio hardware:

```sh
python3 software/libcariboulite/tools/benchmark_nbfm_discriminator.py \
  --baseline-ref d288d4c --cpu 3 --rounds 7 --blocks 100 \
  --output /tmp/nbfm-discriminator-benchmark.json
```

The tool builds temporary copies of the baseline DSP, replacing only the angle
helper in one copy. The JSON records the source/header hashes, compiler flags,
machine information, individual rounds, medians, peaks and paired differences.
`--baseline-source PATH` can use a saved old source instead of Git.

## Validation and remaining work

The angle tests cover dense and random full-circle phases, magnitude scaling,
actual conjugate IQ rotations, signed axes and zero products with both strict
and production compiler flags. Public receiver checks cover silence, fading,
random IQ and signal recovery at 1/2/4 MS/s. Streaming, reset, worker framing,
channel selectivity, tuned voice recovery and squelch checks also pass.

At the time of the angle correction, full-deviation voice tones recovered
normalized raw amplitudes of about
**0.998 at 600 Hz** and **0.992 at 3 kHz**, including ±500 Hz tuning offsets and
a +20 dB adjacent carrier. The previous approximation lost about 5% of the
wanted amplitude. The then-unchanged 50-to-48 kHz interpolation left about
23% raw-tap residual in the full-deviation 3 kHz test. The subsequent
[interpolation correction](rx-squelch.md#signal-path) reduces that to about
6.34%, with corrected 3 kHz amplitude about 0.979. These measurements precede
audio filtering and are not receiver SINAD.

The existing noise-squelch thresholds are retained and their integration checks
pass. On 2026-10-10, the user confirmed a **successful listening test on
HiF (RF24) at 1 MS/s** with the corrected angle calculation. This records
on-air listening acceptance for that configuration. Calibrated sensitivity
measurements remain pending.

```sh
python3 software/libcariboulite/tests/test_fm_discriminator.py
python3 software/libcariboulite/tests/test_nbfm_channel_filter.py
python3 software/libcariboulite/tests/test_nbfm_demod.py
python3 software/libcariboulite/tests/test_fm_reference.py
python3 software/libcariboulite/tests/test_squelch.py
python3 software/libcariboulite/tests/test_memory_audio.py
cmake --build build --target cariboulite_test_app nbfm_memory_demo -j2
```
