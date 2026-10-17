# NBFM complex channel filter

Implemented 2026-10-10 for the menu 14 receiver and other callers of the shared
NBFM demodulator. TX remains at **±2.5 kHz** maximum nominal deviation.

The app now routes menus 11/12/14 through **HiF (RF24)**. The filter operates
on decoded complex IQ from either radio path. Successful listening results
below cover **S1G (RF09)** and **HiF (RF24)**, both at **1 MS/s**.

## Design

The requested 2.5 kHz deviation and 3 kHz voice bandwidth give an approximate
occupied half-bandwidth of 5.5 kHz. The filter passes **±6 kHz**, allowing about
**±500 Hz tuning error** beyond that estimate. This allowance is fixed; the
filter does not track a drifting carrier or correct tuning automatically.

```mermaid
flowchart LR
    A["Complex IQ: 1 / 2 / 4 MS/s"] --> B["Third-order CIC anti-alias filter"]
    B -->|"200 kS/s"| C["321-tap complex FIR; decimate by 4"]
    C -->|"50 kS/s"| D["Limiter and existing FM discriminator"]
    D --> E["Existing 48 kHz audio and squelch"]
```

The same real, symmetric FIR coefficients filter I and Q independently, before
the nonlinear limiter and discriminator. The FIR uses a Kaiser window with
beta 7.5 and a windowed-sinc cutoff of 7.5 kHz; that cutoff is the middle of
the transition, rather than the flat passband edge. Its DC gain is unity.
The [window-method design equations](https://www.dsprelated.com/freebooks/sasp/Window_Method_FIR_Filter.html)
provide the starting point; the committed float coefficients are checked
numerically against the requested response.

| Property | Computed response of committed coefficients |
| --- | --- |
| Passband | −6 to +6 kHz |
| FIR passband gain range | −0.00061 to +0.00177 dB |
| CIC passband droop at 6 kHz | Less than 0.039 dB at all supported rates |
| Transition band | 6–9 kHz on either side |
| FIR stopband, 9–100 kHz | At least 76.58 dB attenuation |
| Total filter group delay | Approximately 0.807 ms |
| FIR equivalent noise bandwidth | 14.403 kHz, counting both sides |

The CIC filters before the first rate reduction, suppressing RF frequencies
that would alias into the wanted passband near multiples of 200 kHz. Its worst
alias-band edge into ±6 kHz is below −88.9 dB. The FIR filters before the second
rate reduction, rejecting energy that would alias from around multiples of
50 kHz. Adding only a narrow filter after the original boxcar decimators would
leave their in-band aliases intact.

A carrier at 12.5 kHz is deep in the FIR stopband. An adjacent **modulated**
channel is different: its nearest occupied edge can reach about 7 kHz, inside
the transition. The stopband number therefore does not establish rejection of
an entire adjacent FM transmission.

## Implementation and reset

[nbfm_channel_filter.h](../software/libcariboulite/src/nbfm_channel_filter.h)
owns the private streaming filter state. The CIC uses defined unsigned 32-bit
wraparound arithmetic, avoiding accumulating floating-point error. Even
full-range signed 16-bit IQ gives a final CIC result within signed 32-bit range
at the largest decimation factor. The symmetric FIR is evaluated only at the
50 kS/s output instants, using mirrored ring histories to avoid modulo inside
the convolution. Processing and reset allocate no memory.

NBFM reset now clears **all** filter histories and decimator phases, including
at a partial input interval. It is equivalent to a fresh receiver with the
same configuration. The previous compatibility reset retained partial boxcar
accumulators. Discriminator normalization, audio filters and rate correction
remain as before. The subsequent [angle correction](nbfm-discriminator-angle.md)
replaces the old approximation with full `atan2f` and a zero-product guard.

The noise-squelch defaults are now 0.20/0.30 RMS after replaying a fresh RF24
capture; see [RX squelch](rx-squelch.md). Channel filtering changes the
discriminator's noise statistics, so thresholds require physical re-evaluation.
Disable both squelches when measuring receiver sensitivity or SINAD. Filter
response and synthetic FM checks do not establish an antenna-port sensitivity
improvement.

## On-air listening result (2026-10-10)

The user confirmed a successful listening test on **S1G (RF09) at 1 MS/s**
after installing the filter:
the repeater's scheduled transmission was clearly audible and its message
understandable, with a reported definite improvement. This physical listening
result complements the automated checks. After switching back to
**HiF (RF24)**, the user also confirmed **good reception at 1 MS/s**. See the
[receiver investigation context](nbfm-rx-sensitivity-context.md#successful-listening-tests-2026-10-10).

## Reproduction

Generate or verify coefficients with the offline NumPy tool:

```sh
python3 software/libcariboulite/tools/design_nbfm_channel_filter.py --check
```

Omit `--check` to regenerate the header. `--csv PATH` exports the dense FIR
frequency response. NumPy is only needed by this design tool; the receiver
depends on C and the existing math library.

```sh
python3 software/libcariboulite/tests/test_nbfm_channel_filter.py
python3 software/libcariboulite/tests/test_nbfm_demod.py
python3 software/libcariboulite/tests/test_fm_reference.py
python3 software/libcariboulite/tests/test_squelch.py
python3 software/libcariboulite/tests/test_memory_audio.py
cmake --build build --target cariboulite_test_app nbfm_memory_demo -j2
```

The new filter checks measure the actual complex response, RF alias rejection,
FM voice recovery with ±500 Hz tuning offsets, an adjacent carrier, streaming
equivalence, cold reset and CPU cost at 1/2/4 MS/s. WBFM retains exact frozen
reference comparisons. NBFM keeps worker framing/control checks against the
frozen worker, while its intentionally changed PCM is validated using the new
filter tests and independently segmented production instances.

Initial filter validation, before the angle correction, passed with strict and
production compiler flags. Production mean
DSP CPU time per 10 ms RF block was **0.269/0.314/0.426 ms** at **1/2/4 MS/s**
on this Raspberry Pi; this excludes the radio, FIFO and playback workers.
The synthetic full-deviation 3 kHz test also reports approximately **23% raw-tap
residual**, compared with about **21%** in the original receiver. The existing
interpolation was an audio-quality limit at that stage. The subsequent
[interpolation correction](rx-squelch.md#signal-path) reduces this residual to
approximately **6.34%** at 3 kHz and **0.02%** at 600 Hz, with corrected 3 kHz
amplitude about **0.979**. These raw measurements precede voice/audio filtering
and are not receiver SINAD. Noise thresholds can now be adjusted in menu 14
using **S**, while **N** bypasses gating and retains live noise measurement.
