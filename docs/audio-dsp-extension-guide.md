# Extending audio adapters and DSP

The eight-step [refactoring plan](audio-dsp-refactor-plan.md) is complete.
The [interface reference](audio-dsp-interfaces.md) records the exact contracts;
this guide shows how to compose and extend them. The
[current architecture diagrams](audio-dsp-interfaces.md#current-radio-architecture)
show the production pipelines, workers, FIFOs and offline adapters.

## Run without audio devices or radio hardware

From the repository root, using the configured application build:

```sh
cmake --build build --target nbfm_memory_demo -j2
./build/nbfm_memory_demo
python3 software/libcariboulite/tests/test_memory_audio.py
```

Alternatively, compile directly with a C11 compiler and libm. This avoids the
ALSA and other dependencies of the repository's CMake configuration:

```sh
cc -std=c11 -O2 -Wall -Wextra -Isoftware/libcariboulite/src \
  software/libcariboulite/tools/nbfm_memory_demo.c \
  software/libcariboulite/src/memory_audio.c \
  software/libcariboulite/src/tone_source.c \
  software/libcariboulite/src/nbfm_mod.c \
  software/libcariboulite/src/nbfm_demod.c \
  software/libcariboulite/src/nbfm_demod_dsp.c \
  software/libcariboulite/src/wbfm_demod.c -lm -o /tmp/nbfm_memory_demo
/tmp/nbfm_memory_demo
```

The executable does not open ALSA, access a radio, program the FPGA or create
workers. It links only libc and libm in the tested Raspberry Pi build.

The [example](../software/libcariboulite/tools/nbfm_memory_demo.c) generates
12,137 float samples of a 600 Hz tone, then connects these existing interfaces:

```text
memory_source -> float mono 48 kHz -> nbfm_mod -> IQ16 at 1, 2 or 4 MS/s
                                            -> nbfm_demod -> S16 mono 48 kHz -> memory_sink
```

`run_roundtrip` borrows the source and sink and owns its DSP instances. It
validates formats, processes bounded blocks, handles transfer counts before
terminal status, and destroys DSP on every exit. The caller destroys adapters
before releasing their backing arrays. Its integer clock accounting preserves
RF sample totals across short audio reads. Demodulator output buffers are
intentionally small to exercise partial consumption. Clock correction is zero:
there is no device clock or queue-depth servo in this offline composition.

This is a synchronous example, not a resumable streaming API. AGAIN returns
`-EAGAIN`; successful operations with no progress return `-EIO`; fatal errors
return after accounting for their valid prefix. It does not flush or pad DSP
filter tails. The existing startup/resampling behavior produces 12,136 PCM
samples from this input at each RF rate.

## Dependencies and ownership

| Layer | Dependencies and owner | Error and shutdown responsibility |
| --- | --- | --- |
| `audio_source`, `audio_sink`, `audio_format`, `iq16` | Sample types and contracts; no device or thread dependencies | Counts and status remain separate |
| `tone_source`, `memory_audio` | Standard C and, for tone generation, math; one caller owns each instance | Nonblocking; destroy adapter state after the last call |
| `alsa_source`, `alsa_sink` | ALSA configuration and I/O; pipeline-owned adapters | Capture cancellation and playback recovery follow their documented contracts; join workers before destruction |
| `tx_audio_buffer` | Borrowed source, capture thread and bounded float audio storage | Hardware-paced TX capture/resampling; park/join before source reset/destruction |
| `nbfm_mod`, `nbfm_demod` | C/math and sample types; one serialized owner per DSP state | Explicit progress/errors, module-specific reset; no radio, UI, ALSA, FIFO or scheduler calls |
| `mod_worker`, `demod_worker` | DSP plus application transport/runtime | Pacing, injection, frame packing and RX queue-depth clock correction remain here |
| `tx_pipeline`, `rx_pipeline` | Adapters, DSP, workers, queues and radio | One control owner serializes lifecycle; stop/cancel/join before freeing resources |
| `app_menu` | Pipeline configuration and status | Select routes/settings and request lifecycle changes |

The production pipelines still share process-wide runtime state and a hardware
lock. They are not general concurrent multi-device objects. ALSA capture
retains a shared temporary conversion buffer, so separate instances must not
read concurrently; each instance owns its own ring. Adapter and DSP
calls are not independently thread-safe; ownership is part of the contract.
Retained worker errors and health snapshots let the backend stop faulted
sessions; the control owner remains responsible for shutdown and cleanup.

## Implement another source or sink

Use [memory_audio.h](../software/libcariboulite/src/memory_audio.h) and
[its implementation](../software/libcariboulite/src/memory_audio.c) as small
examples. Embed the common base in adapter state, assign a constant operations
table, and validate the supported format in the factory. Define backing-storage
ownership, blocking behavior and destruction explicitly.

The current source format is 48 kHz mono float; the sink format is 48 kHz mono
signed 16-bit PCM. This intentional asymmetry preserves the existing demodulator
samples and levels. Neither memory adapter converts, normalizes or clips samples.
A format change needs an explicit conversion and its own validation.

Reads and writes report frames, never bytes, and must not exceed the requested
count. Do not retain the caller's transfer-buffer pointer. A source may return
EOF or ERROR with valid samples; consume that prefix before handling the status.
A sink may similarly report a partial write with an error. Distinguish temporary
unavailability from no-progress success so callers cannot spin indefinitely.

Memory adapters borrow their arrays until destruction; keep source data unchanged
and sink storage exclusively writable. Transfer buffers must not overlap those
arrays. Empty arrays are allowed; unsupported formats and invalid arrays fail
with `EINVAL`, allocation failure with `ENOMEM`. Only factory calls allocate.
The source ends with EOF, including its final samples. The sink returns
`ERROR/-ENOSPC` with the prefix that fits when capacity is exhausted. It never
returns AGAIN or overwrites earlier samples. Zero-length generic calls succeed
with zero progress, even at EOF or full capacity.

Connect a new adapter to the offline example first. The
[contract tests](../software/libcariboulite/tests/test_memory_audio.c) demonstrate
substitution of memory and generated-tone sources, reads limited to 13 frames,
and writes limited to 7 frames, with byte-identical recovered PCM at all three RF rates.
They also cover borrowed-buffer bounds, final data with EOF/error, allocation
failures, exhausted sinks and no-progress handling.

Production `tx_pipeline_init` currently constructs ALSA or tone sources;
`rx_pipeline_init` constructs an ALSA sink. There is no public API for injecting
arbitrary adapters into these initializers. Add deliberate factory/configuration
selection there and the new build input when introducing a production adapter.
Keep the workers unchanged only when its format, pacing and cancellation
semantics meet their requirements. Hardware-paced microphone TX specifically
uses a nonblocking source through `tx_audio_buffer` and its independent capture
thread; legacy TX reads the source in the producer. Both paths feed exact
480-frame modulation blocks and treat EOF/error as fatal, discarding an
incomplete final block. Supporting finite streams requires an explicit tail policy; the
offline example does not change it. A fatal sink error ends its writer and still
requires owner cleanup.

## RX squelch extension

The production RX worker now composes independent noise and carrier detectors
with a PCM gate. The noise detector consumes an optional unfiltered audio tap;
the carrier detector receives a checked RSSI measurement with each RF frame.
Neither is embedded in standalone NBFM processing. See [RX squelch](rx-squelch.md)
for controls, defaults and validation. A future modem must define suitable
squelch inputs rather than inherit NBFM noise thresholds automatically.

## Introduce a second modem

For the current NBFM/WBFM split and future AM, SSB, CW/Morse and PSK receivers, see the
[demodulator architecture research](demodulator-architecture-research.md). It
compares implementation boundaries and maps shared DSP components to each mode.
The companion [modulator research](modulator-architecture-research.md) covers
audio, PSK and CW transmit paths, shared FM generation and TX lifecycle policies.

Start with standalone create/process/reset/destroy operations and deterministic
memory-backed checks. Specify rates, sample representations, amplitude scale,
buffering, consumed/produced counts, no-progress behavior and reset semantics.
Do not assume another modem shares NBFM frame sizes or clock-correction behavior.

Then adapt the DSP call sites in `mod_worker` and `demod_worker`, with modem
configuration and construction in the TX/RX pipelines. Keep radio control bits
and hardware sample packing at the transport boundary. Decide explicitly how the
new modem handles queue pacing, clock correction and any required audio-format
conversion. Menu controls should select configuration through the pipeline.

Introduce common modem operations when that second implementation establishes
which lifecycle and processing operations are actually shared. There is currently
no speculative modem vtable. Preserve existing NBFM sample comparisons and scope
new physical tests to signal, routing or lifecycle changes. Menu 14 now has
[separate frequency controls](monitor-frequency.md), with stopped editing and
direction-specific tuning before stream activation.

## Validation recorded on 2026-09-19

Both RF rates recover 599.99 Hz. The demo reports 505,709 IQ samples at 2 MS/s
and 1,011,417 at 4 MS/s, with 12,136 PCM samples in both cases. All eleven audio,
DSP, lifecycle, stop-deadline, monitor and baseline-runner software suites pass.
The application and standalone CMake targets build successfully.

Step 8 adds standalone code and documentation; it changes no production signal
path. The application SHA-256 before and after adding the target is
`98e579453173684fd3ab943d161ed910bb54f6d5d0133dec5b038ffa865ea910`.
This is the application from step-7 commit `09d89e7`, including the RX-source
label clarification made after H6. The [H6 archive](baselines/20260919T133517.433185Z/summary.json)
records the earlier physical run; it is not a hardware test of a new step-8
binary. No additional physical checkpoint is needed for these standalone additions.
