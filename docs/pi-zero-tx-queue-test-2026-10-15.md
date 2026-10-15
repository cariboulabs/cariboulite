# Pi Zero 2 W TX queue comparison — observations and follow-up

Links into `build/` identify retained local artifacts excluded from Git and
unavailable in a fresh clone. The measurements summarized here are versioned;
the committed collection tools can produce fresh artifacts.

The backend/controller comparison was originally planned for October 15, 2026.
On October 14, the user reported testing that application on the Pi Zero 2 W
installation and supplied the monitor excerpts recorded below. An automated
four-run baseline subsequently completed on the Pi Zero the same evening;
its measurements and the repeat procedure follow the original observations.
Hardware-paced implementation trials now follow that preserved baseline;
an intermediate qualification stopped on a 4 MS/s clock guard. Subsequent
controlled SMI write-timing trials restored near-nominal transport at every rate;
the integrated production repeat subsequently passed all six 180-second cases.
The [tested candidate launch and reload commands](radio-control.md#tested-pi-zero-staging-checkout)
use the isolated Pi Zero checkout, preserving the original checkout's binaries
and installed driver.
The [earlier October 14 observations](../ADDITIONAL-README.md#tx-fifo-occupancy) were
clean-sounding NBFM TX at 1 MS/s with the application queue filling to 64/64
in about 90 seconds, and no such filling at 4 MS/s.

## User listening checks (2026-10-15)

The user reported successful quick tests of the staged hardware-paced version
on the Pi Zero at both **1 MS/s and 4 MS/s**, with modulation audio quality
**a lot better at 4 MS/s** than with the earlier version. This qualitative
listening result supplements the six timed TX trials and RX restoration checks
below. Independent RF clock, deviation and spectrum measurements, and extended
duration acceptance, remain open.

## Backend/controller observations (2026-10-14)

The user again observed gradual application TX queue growth at 1 MS/s, with
displayed puts/gets around 99–100 frames/s. At 4 MS/s, displayed rates were
around 80–90 frames/s and the supplied snapshot showed an empty queue.
The excerpts contain the following measurements:

| Measurement | 1 MS/s | 4 MS/s |
| --- | --- | --- |
| TX pipeline generation | 3 | 4 |
| Application queue depth/capacity | 29/64 | 0/64 |
| Queue min/max | 0/30 | 0/3 |
| Queue puts/gets | 4582/4553 | 1986/1986 |
| Displayed put/get rate, frames/s | 99.8/99.8 | 79.7/79.7 |
| Queue drops and put/get timeouts | 0 and 0/0 | 0 and 0/0 |
| Writer calls | 4552 | 3972 |
| Writer accepted IQ samples | 45,520,000 | 79,440,000 |
| Writer completed frame sequence | 4552 | 1986 |
| Writer partial/zero writes, would-block, errors | 0/0, 0, 0 | 0/0, 0, 0 |
| Capture read calls | 34,624 | 2390 |
| Capture audio frames read | 2,180,352 | 934,560 |
| Capture overruns | 0 | 48 |
| Capture recovery attempts/successes/failures | 0/0/0 | 48/48/0 |
| Capture read errors/last error | 0/0 | 0/0 |
| Producer/writer actual scheduling | SCHED_OTHER/0, SCHED_OTHER/0 | SCHED_OTHER/0, SCHED_OTHER/0 |
| Producer/writer requested priorities | 40/48 | 40/48 |
| TX fault | none | none |

Both pipelines were running, requested and initialized, with RX restoration
off. Both workers were alive and created with last error zero. RX measurements
were unavailable while RX was stopped. The copied worker rows end before the
complete CPU-affinity and scheduling-error fields. Elapsed times, initial
depths, exact source/device settings, binary revision and listening observations
for this follow-up were not supplied. The two generations are separate runs;
their counters must not be subtracted from each other.

At 1 MS/s, `4582 - 4553 = 29` agrees with the queue depth: **290 ms of queued
signal**, with a recorded maximum of 300 ms. The writer dequeued one more
frame than it had completed, consistent with a frame in progress.
`4552 * 10000 = 45520000` accounts for all accepted samples in the excerpt.
The passive monitor calculates its displayed rates between successive snapshots
(normally about 200 ms apart), then rounds to one decimal place. Equal rates
in one interval do not explain or disprove the reported growth over the run.
The snapshot alone cannot measure the long-term imbalance.

At 4 MS/s, each frame contains 40,000 IQ samples. The observed 79.7 frames/s
corresponds nominally to **3.188 million IQ samples/s**, about 20% below the
requested rate; the reported 80–90 range corresponds to 3.2–3.6 million.
Timed writer-sample deltas are still needed to measure actual API acceptance
over a longer interval. `1986 * 40000 = 79440000` and `3972 = 2 * 1986` show
complete frame acceptance split into two API writes per frame, consistent with
the zero partial-write count. This is application/driver acceptance, not a
measurement of RF output timing.

The low queue occupancy and sub-target rates point toward insufficient frame
production at 4 MS/s. The **48 capture overruns** confirm capture discontinuities;
successful recovery resumes capture but does not restore lost audio. Their event
times and lost duration are unknown. Zero queue drops/timeouts and `TX fault:
none` therefore do not establish continuous audio or RF output. An empty queue
at this rate is not evidence that the 1 MS/s backlog cause has been resolved.
Reproduction in the backend/controller application points toward investigation
of the shared TX path and runtime conditions used by both applications.

Source review of the original TX path identified profiling candidates. Both the
[producer](../software/libcariboulite/src/mod_worker.c) and
[writer](../software/libcariboulite/src/tx_pipeline.c) requested CPU 0; check the
actual affinity before comparing use of separate cores. The observed
`SCHED_OTHER/0` means the requested FIFO priorities were not active; obtain the
retained scheduling error before choosing a remedy. Capture and modulation ran
in the same producer thread, and the
[modulator](../software/libcariboulite/src/nbfm_mod.c) computed trigonometry for
every IQ sample, with four times as many samples per frame at 4 MS/s. These are
candidates, not measured causes. The 1 MS/s imbalance still needs timed
producer/writer and transport measurements.

## Physical baseline results (2026-10-14)

The automated suite completed on **pi0a-trixie, Raspberry Pi Zero 2 W Rev 1.0**,
from 20:01:47 to 20:13:56 Brussels time. It used the existing RF24 backend at
requested **430.100 MHz and −12 dBm** into the user's dummy load. The backend
reported actual tuning of 430,099,999.739 Hz and a driver power estimate of
−11 dBm; neither is an independent calibrated RF measurement. Each of the four runs
requested 180 seconds of stable TX; saved status endpoints cover 179.61–179.75
seconds per run. The complete session took 729.15 seconds. No build, DSP,
driver, priority, affinity, governor or audio-bridge changes were made.

| Source / RF rate | Put / get, frames/s | Accepted IQ, MS/s | Queue start → end (sampled max) | Capture overrun delta |
| --- | ---: | ---: | --- | ---: |
| Microphone / 1 MS/s | 99.886 / 99.591 | 0.995911 | 10 → 63 (64) | 2 |
| Microphone / 4 MS/s | 82.812 / 82.812 | 3.312464 | 0 → 0 (1) | 383 |
| Internal tone / 1 MS/s | 99.886 / 99.591 | 0.995914 | 11 → 64 (64) | unavailable |
| Internal tone / 4 MS/s | 88.844 / 88.844 | 3.553746 | 0 → 0 (1) | unavailable |

These are full stable-interval counter deltas, with actual elapsed time. The
4 MS/s microphone capture counter was already 1 at the first saved stable
snapshot and reached 384 at the last, giving **383 measured overruns**. All
measured capture recoveries succeeded. Tone mode does not open capture, so its
capture measurements are unavailable. Writer errors, partial/zero writes,
would-block counts and application queue drops/timeouts stayed zero in all
four measured intervals. Workers remained alive and no TX fault was reported.

At **1 MS/s**, the first approximately 120 seconds produced almost exactly
100 frames/s, while the writer dequeued about **99.59 frames/s**. Backlog grew
by about **0.4 frames/s**, or 4 ms of additional queued signal per second.
The queue first reached 64/64 about **129 seconds after the first saved stable
snapshot** for both sources. Its initial depths of 10 and 11 already represented
startup backlog. Saturation then limited production, reducing the full-run put
averages shown in the table. The first microphone capture overrun appeared at
148.32 seconds, about 19 seconds after first observed saturation. Queue growth
also occurs with the internal tone, so audio capture is not required for this
imbalance. CPU was well below saturation at this rate.

At **4 MS/s**, both sources produced too few frames and the kernel recorded
repeat/pad events, even without audio capture. The internal tone improved
throughput compared with microphone input but still fell below 100 frames/s.
Both TX workers had applied affinity **CPU 0**, with actual **SCHED_OTHER/0**;
the requested FIFO priorities failed with `rt_error=1` (`EPERM`). Affinity,
fallback, memory-lock and query errors were zero. The SSH session's memory-lock
limit was unlimited, while its real-time-priority limit was zero.

| Run | Producer CPU, % of one core | Writer CPU, % of one core | CPU 0 busy, % | Kernel missed-counter delta / sampled seconds |
| --- | ---: | ---: | ---: | --- |
| Microphone / 1 MS/s | 29.10 | 4.17 | 27.33 | 0 / 175.30 |
| Microphone / 4 MS/s | 87.99 | 9.48 | 98.05 | 4054 / 179.50 |
| Internal tone / 1 MS/s | 26.93 | 4.14 | 37.45 | 0 / 175.30 |
| Internal tone / 4 MS/s | 89.18 | 10.13 | 99.76 | 2691 / 178.85 |

Process/thread CPU uses `/proc` execution counters over its own sampled
interval; host busy percentages use aggregate CPU counters and their own
accounting. Kernel deltas cover the narrower timestamped intervals shown.
The counts indicate driver repeat/pad events, not a measured duration of audio
loss or an independent measurement of RF timing. Other cores had spare capacity.
The strongest measured 4 MS/s candidate is producer/writer contention on CPU 0
and the modulation workload. The 1 MS/s writer deficit still needs transport,
poll/write timing and hardware-clock investigation; these results do not justify
slowing the producer to hide the queue growth.

The recorder itself used **11.64–14.18% of one core**, measured separately from
the backend. This overhead must be preserved or accounted for in comparisons;
these are instrumented baseline results. Status sampling was requested at 5 Hz;
the saved 1 MS/s intervals averaged approximately 4.35 Hz, while 4 MS/s was
approximately 5 Hz. There were no status-sequence gaps in stable intervals.
All rates above use actual timestamps. Temperature remained between 49.39 and
63.38 °C across stable runs, and every recorded `get_throttled` value was `0x0`.
Kernel scheduler wait statistics were disabled and remain unmeasured. No audible
quality or independent RF-continuity check accompanied this automated run.

The backend's register-poll duration averaged approximately **29 ms at 1 MS/s**
and **0.35 ms at 4 MS/s**. Its timer starts before acquiring the shared hardware
mutex, so this includes lock waiting and scheduling as well as SPI work. This
is an additional 1 MS/s transport/monitoring investigation target, not proof
that register polling causes the queue growth. Resource collection itself took
roughly 0.11–0.12 seconds per once-per-second sample; no run with instrumentation
disabled was performed.

Cleanup confirmed `IDLE`; backend and controller exited zero, private sockets
were removed, and a subsequent SSH check found no radio owner or `/dev/smi`
owner. Post-run hashes matched the initial backend, controller, library,
firmware and recorded runtime-source hashes. The Pi Zero checkout remained at
`16510133d55139aa772c08c91ac9d084c9d7d2e5`; its pre-existing submodule/bytecode
changes were left untouched. Measurement scripts were the only files copied
into its checkout.

Artifacts are retained on both Pis in
[`build/radio-backend/tx-baseline-zero-20261014T180052Z/`](../build/radio-backend/tx-baseline-zero-20261014T180052Z/):
[generated report](../build/radio-backend/tx-baseline-zero-20261014T180052Z/README.md),
[summary and 30-second windows](../build/radio-backend/tx-baseline-zero-20261014T180052Z/summary.json),
[metadata](../build/radio-backend/tx-baseline-zero-20261014T180052Z/metadata.json),
[cleanup/hash verification](../build/radio-backend/tx-baseline-zero-20261014T180052Z/verification.json),
raw status/resources/events/kernel logs and copies of the executed collectors.

## Hardware-paced implementation trials (2026-10-14)

The [implemented TX design](tx-hardware-paced-design.md) follows SMI/DMA
backpressure, limits the application queue to four frames and driver admission
to two native periods, separates capture/producer/writer onto CPUs 0/1/2, and
uses single-precision sine/cosine conversion with double phase accumulation.
The candidate driver was loaded at runtime; the original installed module,
firmware and original application binaries remain preserved. Tests use the same
Pi Zero, RF24, **430.100 MHz**, requested −12 dBm and dummy load.

The first prototype had a **40 ms / 1920-frame** audio target and **±2%** clock
guard. These results belong to that prototype, before the reserve/guard revision:

| Source / RF rate | Stable endpoints, s | Completed frames/s | Accepted IQ, MS/s | Producer CPU, % of one core | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| Microphone / 1 MS/s | 179.67 | 99.594 | 0.995973 | 15.11 | Full 180-second trial; bounded queues, no capture loss or driver underruns. |
| Microphone / 2 MS/s | 68.34 | 100.529 | 2.010522 | — | Clock guard stopped the run at about 68.7 seconds. |
| Microphone / 4 MS/s | 179.78 | 101.040 | 4.041762 | 54.24 | Full 180-second trial; no capture loss, ASRC fault or driver underruns. |
| Internal tone / 4 MS/s | 179.67 | 101.096 | 4.043866 | 54.39 | Full 180-second trial; no writer errors or driver underruns. |

The 4 MS/s producer now has measured compute margin on its separate core. Capture
overruns, ASRC underflows/overflows and driver underruns were zero in the saved
microphone stable intervals. The 4 MS/s microphone correction briefly reached
the prototype's limit, but did not fault. Internal tone does not open microphone
capture. A queue at **4/4** is intentional bounded prefill: every new producer
frame waits for downstream capacity, so it cannot accumulate the former
64-frame backlog. The native four-period DMA ring still contributes latency;
queue depth alone is not an RF timing measurement.

At 2 MS/s, the first roughly 60 seconds showed about 2.006 million IQ-equivalents/s
of FIFO-dequeue demand with stable audio occupancy. Around 62 seconds that
demand rose to approximately **2.055 million/s** while real capture remained
near **48000 audio frames/s**. The microphone queue drained, required correction
exceeded the prototype's ±2% guard, and `-ERANGE` stopped TX. Cleanup retained
the fault diagnosis and returned SMI to idle. There were no measured capture
overruns or driver underruns before the guard. The observed transport excursion
and RF sample timing remain separate questions.

The revised implementation uses **3360 frames / 70 ms**, a **±4%** guard and
proportional gain **0.75**, retaining a 0.5-second occupancy filter, integral gain
0.2 and maximum slew **100 ppm per 480-output-frame block**. Five nominal seconds
of continuously clamped *desired* correction cause a clock fault; that count can
begin before the applied ratio finishes slewing. Widening the guard alone was
insufficient: the 40 ms queue starved after 1.16 seconds in a −3.3% startup test.
The theoretical slew reserve alone needs approximately 54 ms, before capture
periods and native DMA bursts.

The [ASRC tests](../software/libcariboulite/tests/test_tx_audio_buffer.py) pass
600-second cases with ±3.3% clock differences and 32768-IQ retirement bursts,
including capture jitter and a step at 60 seconds from 2.006 to 2.060/2.066
million IQ-equivalents/s. Their minimum retained audio was 749/416 frames;
neither starved, overflowed or faulted. A +5% clock mismatch still causes the
explicit ±4% guard fault. Standard and backend compiler flags pass, as do
the real-worker/mock-driver lifecycle tests. These checks validate the buffer
control and signal arithmetic; audible quality, deviation and RF continuity
remain unmeasured by the automated suite.

Prototype artifacts:

- [1/2 MS/s report](../build/radio-backend/tx-hardware-paced-zero-baseline-20261014/README.md)
  and [full status](../build/radio-backend/tx-hardware-paced-zero-baseline-20261014/status.jsonl),
  including the failed 2 MS/s run and cleanup evidence.
- [4 MS/s microphone/tone report](../build/radio-backend/tx-hardware-paced-zero-4M-20261014/README.md),
  [summary](../build/radio-backend/tx-hardware-paced-zero-4M-20261014/summary.json) and
  [cleanup/hash verification](../build/radio-backend/tx-hardware-paced-validation-20261014/verification-cap2-4M.json).

## Intermediate physical qualification — stopped on a clock guard

The intermediate six-run sequence started on October 14 at **22:03:26 Brussels time**,
measuring microphone and tone at **1, 2 and 4 MS/s** for 180 seconds each with
the revised ±4% / 70 ms implementation. The 1 and 2 MS/s microphone trials
completed; 4 MS/s microphone TX stopped after about 169 seconds on `-ERANGE`.
The three tone trials were not reached. SMI and TX were subsequently disabled;
its saved events retain the fault/cleanup evidence. That implementation snapshot
has a failed qualification result; the later controlled timing experiment and
production qualification follow below.

| Microphone rate | Healthy stable endpoints, s | Completed frames/s | Accepted IQ, MS/s | Capture overruns / new stable driver underruns | Outcome |
| --- | ---: | ---: | ---: | --- | --- |
| 1 MS/s | 179.71 | 99.628 | 0.996303 | 0 / 0 | Completed 180-second target. |
| 2 MS/s | 179.67 | 100.142 | 2.002838 | 0 / 0 | Completed 180-second target. |
| 4 MS/s | 169.26 | 100.719 | 4.028847 | 0 / 0 | Clock guard stopped the run immediately after these healthy endpoints. |

The complete 4 MS/s average hides its late transport change. Around 163 seconds,
native dequeue demand accelerated from approximately 4.04 million
IQ-equivalents/s. Over the last **4.83 healthy seconds**, dequeue was
**4.189737 million/s**, producer audio output **50133 frames/s** and actual
capture **48024 frames/s**. Their paired required input/output ratio was
approximately **−4.21%**, beyond the revised guard. Applied correction was still
slewing during the interval, then held −4%; the observed audio queue fell as
low as 33 frames. There were no capture overruns or read errors. A native period
contributes approximately 0.17% endpoint quantization over this interval, which
does not explain the full demand excursion.

The last healthy driver snapshot retained one underrun already present before
stable TX began; no additional underrun occurred in the saved healthy interval.
Fault shutdown adds separate transition counters and must not be folded into
the healthy rate window. The final clock fault is likewise outside the last
healthy `TX` snapshot, so its zero *stable* fault delta does not mean a successful
run. Current evidence points to changing transport demand rather than interrupted
microphone capture. RF emission timing remains independently unmeasured.

Artifacts: [generated report](../build/radio-backend/tx-hardware-paced-zero-final-20261014/README.md),
[summary](../build/radio-backend/tx-hardware-paced-zero-final-20261014/summary.json),
[raw status](../build/radio-backend/tx-hardware-paced-zero-final-20261014/status.jsonl)
and [run/fault/cleanup events](../build/radio-backend/tx-hardware-paced-zero-final-20261014/events.jsonl).

## Controlled SMI write-timing repeat

A diagnostic based on the frozen build changed only write setup/strobe/hold
from **1/5/1 to 1/4/2**, preserving the seven-tick total. It verified actual
readback and preserved read timings, write pace, clock, packing, DMA geometry,
firmware, DSP and audio-buffer policy. Near-nominal transport replaced both the
former 1 MS/s deficit and the later 2/4 MS/s excursions:

| Source / RF rate | Healthy stable endpoints, s | Completed frames/s | Accepted IQ, MS/s | Capture overruns / new stable driver underruns |
| --- | ---: | ---: | ---: | --- |
| Microphone / 1 MS/s | 179.71 | 99.9966 | 0.999964 | 0 / 0 |
| Microphone / 2 MS/s | 179.73 | 100.0024 | 2.000076 | 0 / 0 |
| Microphone / 4 MS/s | 299.69 | 100.0000 | 4.000083 | 0 / 0 |
| Internal tone / 4 MS/s | 179.75 | 100.0007 | 4.000002 | unavailable / 0 |

All four trials completed with normal cleanup. Microphone capture/source errors,
ASRC underflows/overflows and clock faults were zero; mean sampled corrections
were approximately −46/−33/−26 ppm at 1/2/4 MS/s. The 4 MS/s microphone trial
extended past the earlier 169-second failure to five minutes. Original/staged
file provenance, no remaining owner/socket and the preserved installed module
were verified separately. One tone startup underrun preceded its stable window;
the integrated implementation now prepares a first refill while workers are
parked to address that margin.

The timing-only change supports a TX write-interface timing explanation. The
[761-case sampling study](../build/radio-backend/tx-swe-sampling-study-20261014/README.md)
also reproduces framing failures with old timing under selected artificial
jitter. Neither the simulation nor transport counters independently measure
physical SWE jitter or RF sample timing. The observed transport improvement is
established; its precise electrical mechanism and RF calibration remain open.

Artifacts: [1/2 MS/s microphone](../build/radio-backend/tx-timing-142-zero-mic1M2M-20261014/README.md),
[five-minute 4 MS/s microphone](../build/radio-backend/tx-timing-142-zero-mic4M-20261014/README.md)
and [4 MS/s tone](../build/radio-backend/tx-timing-142-zero-tone4M-retry-20261014/README.md).
Their backend logs retain the actual original, changed and verified settings.

## Integrated production TX transport qualification — passed

The production path now applies/verifies **1/4/2** while SMI is idle and restores
the original three write fields on stop, abort or failed start, preserving current
read/DMA/pace settings. Before DMA activation it enqueues one parked, precomputed
zero-audio FM frame as part of the ten opening pre-zero frames; nine follow from
the producer, with unchanged 25-frame cues. Mock tests verify exact prefix counts,
first-refill coverage, calibration before IQ writes and failure-safe restoration.

A fresh six-case microphone/tone suite at 1/2/4 MS/s completed on October 14
between **23:00:11 and 23:18:20 Brussels time**, targeting 180 seconds per case.
An independent raw-observation checker passed all 216 checks, and separate
cleanup/file-hash verification passed. These results belong to the integrated
production implementation and remain separate from both intermediate guard
failures and the timing-only diagnostic.

| Source / RF rate | Stable endpoints, s | Completed frames/s | Accepted IQ, MS/s | Producer / writer CPU, % of one core |
| --- | ---: | ---: | ---: | ---: |
| Microphone / 1 MS/s | 179.61 | 99.9965 | 0.999973 | 13.87 / 4.63 |
| Microphone / 2 MS/s | 179.80 | 99.9992 | 1.999961 | 26.64 / 14.85 |
| Microphone / 4 MS/s | 179.67 | 100.0026 | 4.000036 | 54.67 / 14.60 |
| Internal tone / 1 MS/s | 179.76 | 100.0044 | 1.000017 | 15.24 / 4.64 |
| Internal tone / 2 MS/s | 179.75 | 100.0024 | 2.000036 | 49.84 / 8.16 |
| Internal tone / 4 MS/s | 179.66 | 100.0045 | 4.000111 | 53.78 / 14.64 |

Every trial had zero total driver underruns in valid startup, stable TX and
post-stop observations, zero writer errors and no terminal fault. Microphone
capture overruns, recovery/read errors, ASRC underflows/overflows, lost input and
clock-guard counters were zero. Capture remained near 48 kHz, with sampled mean
ASRC corrections of approximately −100/−96/−59 ppm. Actual TX write timing was
**1/4/2/1** including pace; stopped readback restored **1/5/1/1**.

The application queue's **4/4** occupancy is intentional bounded prefill with
matched rates, rather than the original growing 64-frame backlog. The driver
reservoir stayed within two native periods. Partial writes are expected under
that admission policy and were fully counted. Cumulative driver totals cover
startup/stop outcomes, but transition telemetry does not timestamp the first
DMA callback. The earlier 4 MS/s microphone/tone rates of 82.81/88.84 frames/s
now reach approximately 100; producer CPU fell from 87.99%/89.18% to
54.67%/53.78% of one core. No cause is asserted for other CPU variations.

All four lifecycle fixtures pass on the Pi Zero, including the optimized virtual
stop-deadline simulator with its unchanged 216 scenarios and 30-second limit.
Automatic RX restoration also passed at 1 and 4 MS/s, as recorded below. These
checks establish host queue/capture and SMI transport behavior; they do not
measure RF clock accuracy, deviation, spectrum or cue audibility.

Artifacts: [production report](../build/radio-backend/tx-hardware-paced-zero-production-20261014/README.md),
[raw status](../build/radio-backend/tx-hardware-paced-zero-production-20261014/status.jsonl),
[independent qualification](../build/tx-hardware-paced-live-watch-20261014/qualification/qualification.json),
[cleanup/hash verification](../build/radio-backend/tx-hardware-paced-validation-20261014/verification-production.json)
and [optimized Pi Zero stop-deadline results](../build/radio-backend/tx-hardware-paced-validation-20261014/zero-stop-deadline-optimized.log).

### Physical automatic RX restoration — passed

Within one backend process, stable RX at 1 MS/s was followed by five seconds
of tone TX and normal stop, then **29.925 seconds** of automatically resumed RX.
The sequence repeated at 4 MS/s, with **29.931 seconds** of resumed RX. Actual
TX timing read 1/4/2; RX/off readback restored 1/5/1. RX configuration was
preserved, and both IQ and audio counters advanced.

| Resumed RX rate | Application-reader IQ samples/s | Kernel DMA movement, samples/s |
| --- | ---: | ---: |
| 1 MS/s | 999,834.23 | 1,000,010.83 |
| 4 MS/s | 4,001,229.36 | 4,000,006.56 |

Application-reader estimates use frame endpoints; kernel rates use their own
callback intervals. Neither independently measures RF timing. All IQ/audio
queue drops and put timeouts, reader errors, playback underruns/recovery
failures/write errors and kernel missed counters were zero; workers remained
healthy. Final stop verified modem/SMI idle. Cleanup/hash verification confirmed
no radio owner, private sockets or backend remained and original/staged binaries
and USB/Loopback bridges were unchanged.

Artifacts: [handoff summary](../build/radio-backend/tx-rx-handoff-zero-retry-20261014/handoff-summary.json)
and [final verification](../build/radio-backend/tx-hardware-paced-validation-20261014/verification-handoff.json).
A [first harness attempt](../build/radio-backend/tx-rx-handoff-zero-20261014/handoff-summary.json)
stopped on a logging error before TX, verified idle cleanup, and remains separate
from the successful measurement.

To repeat the current measurement on the Pi Zero, use the isolated staging
checkout with the candidate driver already loaded and other radio owners stopped:

```sh
cd ~/src/cariboulite/build/tx-hardware-paced-zero-20261014
python3 software/libcariboulite/tools/tx_queue_baseline.py \
  --expect-model 'Raspberry Pi Zero 2 W' \
  --frequency 430100000 --duration 180 \
  --sources mic tone --rates 1000000 2000000 4000000 \
  --output "../tx-hardware-paced-zero-repeat-$(date -u +%Y%m%dT%H%M%SZ)"
```

The commands below describe the preserved original baseline checkout. Its
results belong to the original binaries and driver; they should not be mixed
with the revised staged measurements.

## Automated baseline on the Pi Zero 2 W

The subsequent [TX diagnosis and hardware-paced design](tx-hardware-paced-design.md)
records nominal hardware-paced RX rates, a reduced-status-cadence TX repeat,
isolated modulator timings and the requirements for demand-driven TX plus
microphone clock reconciliation. Its implementation trials are recorded above.

The automated runner is
[`tx_queue_baseline.py`](../software/libcariboulite/tools/tx_queue_baseline.py),
with offline analysis in
[`tx_queue_baseline_report.py`](../software/libcariboulite/tools/tx_queue_baseline_report.py).
Development and hardware-free checks run on the Pi 4; the physical baseline runs
on the Pi Zero 2 W. TX frequency is **430.100 MHz**, as requested, with RF24 and
the existing default requested power of −12 dBm. The radio is connected to the
user's dummy load. The runner uses the existing binaries and leaves TX DSP,
driver, scheduling and audio bridge settings unchanged.

The default sequence measures microphone input at 1 and 4 MS/s, then the internal
600 Hz / 0.4-amplitude tone at 1 and 4 MS/s. Each run lasts 180 seconds after
verified stable TX, giving roughly 12 minutes of measured transmission plus
startup and normal closing-cue/drain time. Every start creates a new pipeline
generation. Microphone runs use `plughw:Loopback,1,1`; the existing USB microphone
bridge must remain running. Tone runs help isolate capture from modulation and
transport.

To copy only the new measurement tools from the Pi 4 checkout to the Pi Zero:

```sh
scp software/libcariboulite/tools/tx_queue_baseline*.py \
  pi@192.168.6.37:~/src/cariboulite/software/libcariboulite/tools/
```

Run on the Pi Zero after closing other radio-owning applications:

```sh
cd ~/src/cariboulite
python3 software/libcariboulite/tools/tx_queue_baseline.py \
  --expect-model 'Raspberry Pi Zero 2 W' \
  --frequency 430100000 --duration 180 \
  --output "build/tx-baseline-zero-$(date +%Y%m%d-%H%M%S)"
```

The runner keeps the actual C controller connected throughout TX, uses private
sockets and waits for each transition. Ctrl+C requests normal TX shutdown and
backend cleanup. A backend wall-time limit also bounds the session. Existing
radio owners are left untouched. `--program-fpga` explicitly loads the existing
`firmware/top.bin` before the sequence when programming is needed; it is not
enabled by default.

The output directory contains:

- `metadata.json`: host model, kernel, options, binary/firmware/runtime-source hashes,
  audio setup, commands, result and cleanup status.
- `status.jsonl`: full passive snapshots with run/phase labels, queue, writer,
  capture, worker scheduling and register measurements at the existing 5 Hz rate.
- `resources.jsonl`: once-per-second process/thread CPU and context-switch
  counters, scheduler/affinity, host CPU/interrupt counters, temperature and
  throttling observations. The recorder's own process cost is included separately.
- `events.jsonl`, `backend.log`, `controller.log` and kernel logs: command,
  transition, run-boundary, failure and cleanup evidence.
- `summary.json` and `README.md`: measured rates over independent stable TX
  intervals and approximately 30-second windows, queue growth/saturation,
  capture loss/recovery, CPU use and kernel TX missed-counter deltas where available.

Unavailable measurements remain explicitly unavailable. `/proc` CPU sampling
does not measure individual modulation, capture, FIFO, poll or write service
times. Scheduler wait statistics are unmeasured when kernel scheduler statistics
are disabled. Writer samples describe driver acceptance; RF continuity and
audible quality still require separate observations.

To validate the command lifecycle without hardware or audio:

```sh
python3 software/libcariboulite/tools/tx_queue_baseline.py \
  --dry-run --duration 1 --output /tmp/cariboulite-tx-baseline-dry-run
```

The output directory must be new. Dry-run reports simulated command sequencing
and unavailable physical measurements; it never supplies a physical baseline.
The report can be regenerated later without accessing the radio:

```sh
python3 software/libcariboulite/tools/tx_queue_baseline_report.py \
  build/tx-baseline-zero-YYYYMMDD-HHMMSS
```

The runner and analyzer passed 13 hardware-free checks on the Pi 4 on
2026-10-14. These cover the four-case sequence, distinct generations, graceful
SIGTERM shutdown, host-model rejection, measurement-window calculations,
counter-reset/session guards, missing measurements, CPU/PID accounting and
kernel timestamp attribution. A separate 0.8-second-per-case dry run also
completed on the Pi Zero with normal cleanup. These validate the measurement
tools; physical baseline results are recorded separately below when available.

For a controlled monitoring comparison, `--status-hz 0.5` changes only the
backend's status/register cadence; its default remains 5 Hz. The once-per-second
resource recorder remains active. The cadence option passed a separate
hardware-free integration check before the physical repeat.

## Manual timed follow-up

Use the same radio channel, frequency, power, audio input and listening receiver
as the earlier comparison. Record the input route, starting queue depth and
audible quality. The main comparison uses the same external audio at both rates;
an internal-tone repeat can help isolate audio capture from modulation/transport.

### Start and record

Run from the repository root. Close `cariboulite_test_app` and other radio
owners. Keep the existing ALSA bridges running when using external audio.
Use a fresh output directory for each backend session; the example below is
for the first October 15 run.

```sh
make -C software/libcariboulite/tools/radio_backend
mkdir -p build/tx-queue-test-20261015
git rev-parse HEAD > build/tx-queue-test-20261015/revision.txt
uname -r > build/tx-queue-test-20261015/kernel.txt
```

In terminal 1, start the backend with RX stopped. This example uses RF24;
select the same channel as the earlier test.

```sh
build/radio-backend/cariboulite_radio_backend \
  --channel RF24 --sample-rate 1000000 --mode nbfm --start-stopped \
  --playback plughw:Loopback,0,0 --microphone plughw:Loopback,1,1 \
  --tx-sample-rate 1000000 --tx-source mic \
  > build/tx-queue-test-20261015/backend.log 2>&1
```

In terminal 2, open the persistent interactive controller:

```sh
build/radio-backend/cariboulite_radio_controller
```

Set `tx frequency` in MHz and `tx power` in dBm to the earlier test's values,
then check `status`. Follow the existing
[FPGA startup/programming procedure](radio-control.md#fpga-programming-after-reboot)
if programming is needed. Wait for `IDLE` before the TX comparison.

In terminal 3, open the interactive monitor:

```sh
build/radio-backend/cariboulite_radio_monitor
```

In another terminal, record the same passive status stream:

```sh
build/radio-backend/cariboulite_radio_monitor --json \
  > build/tx-queue-test-20261015/monitor.jsonl \
  2> build/tx-queue-test-20261015/monitor.stderr.log
```

Keep the default 5 Hz status rate. Save the full worker scheduling fields from
the JSON stream and the backend's `[scheduling]` startup lines. During stable TX,
record per-thread CPU usage (for example, `top -H -p BACKEND_PID`, replacing
`BACKEND_PID` with the monitor's owner PID), including `tx_producer` and
`tx_writer`, at both rates. This helps distinguish CPU saturation from time spent
waiting; the displayed scheduling policy alone cannot do so.
Optionally record `sudo dmesg -wH` in another
terminal for kernel `init programmed write. missed:` and stream restart messages.
The monitor does not publish the kernel missed-quarter counter.

### Compare the rates

Enter these commands in the interactive controller, waiting for each transition
to complete. Start timing when the monitor reports stable `TX`.

| Run | Controller settings before `tx start` | Observation period |
| --- | --- | --- |
| A | `tx source mic`, then `tx rate 1` | Approximately 3 minutes; include 30–60 seconds after any 64/64 saturation |
| B | `tx source mic`, then `tx rate 4` | Approximately 3 minutes with the same input |
| C, optional | `tx source tone`, `tx tone 600 0.4`, then `tx rate 1` | Repeat to check whether growth also occurs without audio capture |
| D, optional | `tx source tone`, `tx tone 600 0.4`, then `tx rate 4` | Repeat to check whether production stays below 100 frames/s without audio capture |

For each run, enter `tx start`, collect the observations, then enter `tx stop`
and wait for `IDLE` before changing settings. Keep the controller connected
during TX. Each backend TX start creates a fresh pipeline and counter generation.

Record queue depth and maximum at the start and approximately 30, 60, 90,
120 and 180 seconds. Note modulation quality and opening/closing Quindar tones.
After the comparisons, briefly check RX → TX → RX operation and that closing
the passive monitor leaves the radio state unchanged. Set the saved RX frequency
to the previous receive-test frequency before starting RX.

Stop the logger with Ctrl+C after the final transition, then stop the backend.
Keep the JSON and backend/kernel logs with the listening observations.

### Read the measurements

Calculate rates over 10–30-second intervals within one `session` and
`tx.generation`, using only stable `state == "TX"` and
`control.transition == false`. Startup/shutdown snapshots have unavailable
pipeline measurements. Do not subtract counters across restarts.

| Measurement | Calculation or counter |
| --- | --- |
| Producer frames/s | Change in `tx.queue.puts` divided by change in `monotonic_s` |
| Writer dequeue frames/s | Change in `tx.queue.gets` divided by elapsed monotonic time |
| Accepted IQ samples/s | Change in `tx.writer.samples` divided by elapsed monotonic time |
| Queue growth | Change in `tx.queue.depth`; compare with puts minus gets |
| Writer failures | Changes in `tx.writer.errors` and retained `last_error` |
| Capture loss/recovery | Changes in `tx.capture.overruns`, recovery and error counters, when capture is valid |
| Kernel TX starvation | Growth of `missed` during steady TX in the kernel log |

At 1 MS/s, puts near 100 frames/s with slower dequeue/sample acceptance and
increasing depth identify accumulating application backlog. At 4 MS/s, low
occupancy with production below 100 frames/s and rising kernel misses would
indicate underproduction. Capture overruns after saturation would reveal audio
loss that the blocking application queue's zero drop/timeout counters cannot show.

Writer samples measure acceptance into the driver, rather than RF output timing.
Worker scheduling rows report startup scheduling/affinity; CPU utilization and
physical latency require separate measurements. Use the captured evidence to
choose the next profiling step in the [roadmap](../ROADMAP.md#i-runtime-profiling-broadcast-decoding-and-adjustable-ctcss).
