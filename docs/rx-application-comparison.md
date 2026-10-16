# RX application comparison — 2026-10-11

Artifact paths under `build/` below refer to local files on the original test
host. Missing raw artifacts are not included in this checkout; committed
reports preserve the recorded results and scope.

The C receiver backend uses **22.3% less CPU** than `test_app` menu 14 in this
NBFM comparison. The Python monitor adds enough work that **backend plus
monitor uses 22.4% more CPU** than menu 14. The absolute difference is small:
**0.44 percentage points of the four-core Pi's total capacity**. Combined
memory use is about **24.5% lower**. Both setups passed the measured pipeline
health checks.

These are historical measurements of the builds used for this comparison.
Since the measurement, inactive TX workers have been changed to park on
condition variables and hardware-paced TX has been added. The idle-TX cost
in the table below therefore does not describe the current menu-14 build.
The menu-14 UI included the added audio FIFO, playback, reader and worker rows
at measurement time. Its layout has since been restored to `main`, while
shared instrumentation remains collected. The CPU and health results below
are unchanged; they are not measurements of the restored UI build.

## Results

Means of two 90-second measurements per setup on a Raspberry Pi 4:

| Application | CPU, % of one core | Share of four-core capacity | PSS memory |
| --- | ---: | ---: | ---: |
| `test_app`, menu 14 | 7.90% | 1.97% | 64.01 MiB |
| RX backend process, with monitor connected | 6.14% | 1.53% | 37.41 MiB |
| Python passive monitor | 3.53% | 0.88% | 10.92 MiB |
| Backend + monitor | 9.66% | 2.42% | 48.33 MiB |

Here 100% means one fully occupied CPU core; divide by four for this Pi's
total capacity. PSS apportions shared memory pages between processes,
making its sum more useful than adding RSS. Memory is sampled at the end of
each measurement window. The backend-only row describes its process while
the monitor is connected; a disconnected-monitor trial was not performed.

The repetitions were consistent:

| Trial order | Radio owner CPU | Monitor CPU | Combined CPU | Combined PSS |
| --- | ---: | ---: | ---: | ---: |
| 1: menu 14 | 7.932% | — | 7.932% | 64.006 MiB |
| 2: backend | 6.131% | 3.488% | 9.619% | 48.327 MiB |
| 3: backend | 6.143% | 3.566% | 9.709% | 48.331 MiB |
| 4: menu 14 | 7.865% | — | 7.865% | 64.006 MiB |

### Where CPU time goes

Mean thread CPU, again as percentages of one core:

| Thread/function | Menu 14 | Backend |
| --- | ---: | ---: |
| NBFM demodulation | 3.977% | 3.966% |
| RX reader | 1.705% | 1.655% |
| Audio writer | 0.305% | 0.350% |
| Main loop: controls/status/polling/rendering/logging | 1.128% | 0.172% |
| Two idle TX workers combined | 0.772% | — |

The shared RX DSP has essentially the same cost. Most of the owner's saving
comes from its lighter main loop and absence of the two idle TX workers,
which menu 14 creates even in an RX-only session. These are observations
about the complete applications, rather than isolated measurements of each
operation. Menu 14 also polls both radios and additional mixer/FPGA status,
and retains DEBUG logging; the backend polls the active radio and filters
logs at INFO. Those existing differences were preserved.

The backend reads its cached modem snapshot in six SPI blocks per update.
The two trials averaged **0.187 ms** and **0.174 ms** per scan, including lock
time; their nearest-rank p99 values were **0.235 ms** and **0.248 ms**.
One scan reached **6.008 ms** in trial 2; trial 3's maximum was **0.271 ms**.
No SPI failures or skipped status packets were recorded. Two passive clients
were connected: the actual monitor and the comparison recorder. They shared
each hardware snapshot, without causing additional register reads.

## Pipeline health

Across all four measurement windows:

| Diagnostic | Menu 14 | Backend + monitor |
| --- | --- | --- |
| Application IQ queue overwrites / wait timeouts | 0 / 0 | 0 / 0 |
| Audio FIFO rejected enqueues / wait timeouts | 0 / 0 | 0 / 0 |
| ALSA playback underruns / recoveries / write errors | 0 / 0 / 0 | 0 / 0 / 0 |
| RX read errors / worker exits | 0 / 0 | 0 / 0 |
| IQ enqueue rate, mean | 99.987 frames/s | 99.999 frames/s |
| IQ queue sampled depth / lifetime maximum | 0–2 / 4 of 64 | 0–3 / 4 of 64 |
| `audio_fifo` sampled depth / lifetime maximum | 0–5 / 6 of 24 | 0–5 / 6 of 24 |
| Audio enqueue / dequeue rate, mean | 100.031 / 100.004 frames/s | 100.033 / 100.010 frames/s |
| Kernel SMI `missed` observations | 56 readings, all 0 | 56 readings, all 0 |

An IQ frame contains 10,000 complex samples at 1 MS/s; an audio frame contains
480 mono samples at 48 kHz. Short RX reads and zero-return reads occurred in
both setups: 1,155/3,036 for menu 14 and 1,159/3,046 for the backend across
their two measured intervals. Short reads are accumulated and zero reads are
retried; these counts do not establish lost samples.

Queue accounting balances with the observed start/end depths. In trial 4,
two more IQ frames were dequeued than enqueued because its initial queue
depth was two and its final depth was zero. Trial 3's final ALSA snapshot had
one 480-sample write outstanding. The separately sampled counters describe
adjacent instants, so an in-progress write can appear between submission and
completion without a write failure.

All three RX workers remained alive, with retained error zero. Actual policy
was **`SCHED_OTHER`, priority 0**, in both apps. Requested FIFO priorities
70/55/46 failed with `EPERM`; the existing fallback was used. Reader,
demodulator and audio writer affinity was CPU 2/1/0 respectively. Affinity,
fallback, scheduler-query and memory-lock error codes were zero. This test
demonstrates operation with the applied scheduler, rather than real-time
scheduling guarantees.

The audio FIFO was still in its gradual initial fill: final depths were
4/2/2/3 slots. These two-minute sessions do **not** validate the long-term
42–50% equilibrium observed in the earlier listening session. A sampled empty
queue is also not an ALSA underrun; the explicit underrun counters stayed zero.

Kernel logs contained 28 cumulative `missed:0` readings in each measurement
window. This supports absence of reported kernel RX FIFO rejection during
the observed periods. It does not establish FPGA sample continuity or detect
every possible upstream discontinuity. The last kernel observation preceded
each window's end by about one second.

## Test conditions and scope

The applications ran sequentially in the order menu 14, backend, backend,
menu 14. Each fresh process had a 30-second warmup followed by a 90-second
CPU window, totaling 360.09 measured seconds. Both radio owners and the
monitor exited normally after their trials; no TX or IQ recording was used.
The existing ALSA bridge processes stayed running for both setups.

Matched receive settings were:

- HiF/RF24, requested **430.125 MHz**, **1 MS/s**, NBFM.
- Existing ±2.5 kHz deviation profile and complex channel filter.
- Modem `STATE=5`, `RXBWC=0x10`, `RXDFE=0x04`, AGC enabled,
  AGCS target code 0 (−21 dBFS).
- 48 kHz mono output to `plughw:Loopback,0,0`, gain 8000, 50 µs de-emphasis.
- Noise squelch enabled, open/close RMS 0.20/0.30; carrier squelch disabled.
- Status refresh approximately 5 Hz; both pseudo-terminals 120 rows × 240 columns.

The larger terminal made every counter in the then-expanded menu-14 display
visible to the recorder. The
measured rendering workload may differ from a 57-row laptop terminal. PTY
output was continuously drained to raw logs; terminal emulation and menu
parsing ran after the CPU window and application shutdown. Linux process and
thread CPU counters were sampled, with approximately 1 Hz host/process
observations. The profiler used 1.09–1.16% of one core during the CPU windows
and is excluded from the application totals. Its overhead and the existing
audio bridges are included in whole-host busy time, which ranged from
4.29–5.84% and should not be attributed entirely to the radio applications.

To control CPU frequency, the governor was temporarily changed from
`ondemand` to `performance`. Every recorded frequency sample was **1.8 GHz**;
temperatures ranged from **53.1–57.9 °C**, and throttling checks reported
`0x0`. The governor was restored to **`ondemand`** afterward. Radio processes
ran as the normal user, UID 1000, with unchanged scheduling requests. These
absolute CPU figures should not be directly compared with an uncontrolled
frequency measurement.

The production DSP compilation flags match for the main optimizations
(`-O3 -ffast-math -fno-math-errno -funroll-loops`). Both executables include
the new shared instrumentation. This compares the instrumented applications;
it does not isolate the cost of adding the instrumentation itself. Source
revision and executable SHA-256 hashes are retained in the plan; the source
also includes the uncommitted shared-instrumentation changes.

Live RF was uncontrolled and mostly idle. The gate was open in 10/0/77/0
status snapshots respectively; trial 3 caught activity for roughly 15 seconds.
This is a CPU and pipeline-health comparison, without a matched RF signal
for sensitivity or audio-quality measurement. Two repeats at 1 MS/s do not
establish sustained worst-case latency or the budget for WBFM stereo, RDS,
DAB+ or recording at higher sample rates.

## Verdict and next measurement

The separate backend provides a lighter hardware owner and lower combined
memory use, with healthy measured RX queues and audio playback. The current
Python display makes the combined setup slightly more expensive in CPU
terms. The measured NBFM load is low, so there is no indication here that a
large RX rewrite is needed for this configuration.

For a reduction in total CPU, the monitor is the clearest next target:
profile its parsing/formatting/rendering, then compare reduced display cadence
and cached register decoding with the current 5 Hz baseline. Keep the
backend's counters and hardware collection independent of display cadence.
A monitor-disconnected run, longer FIFO settling run, and WBFM/higher-rate
profiles would extend this baseline. None of those changes is implemented by
this comparison.

## Artifacts and reproduction

The full artifacts remain in
`build/radio-backend/comparison-20261011/`:
derived summary (`build/radio-backend/comparison-20261011/summary.json`),
all trial reports (`build/radio-backend/comparison-20261011/report.json`),
settings and binary hashes (`build/radio-backend/comparison-20261011/plan.json`),
clock changes (`build/radio-backend/comparison-20261011/clock-control.json`),
SMI observations (`build/radio-backend/comparison-20261011/smi-summary.json`)
and filtered kernel log (`build/radio-backend/comparison-20261011/smi-kernel.log`).
Each trial also retains raw terminal output, timestamped output chunks,
snapshots, process samples and reconstructed screens. Pilot runs are separate
and excluded from these results.

The reusable runner is
[`compare_rx_applications.py`](../software/libcariboulite/tools/compare_rx_applications.py).
It requires exclusive radio ownership and refuses to start while an existing
`test_app` or backend is running. It stops only the processes it launches.
It does not change the CPU governor:

```sh
python3 software/libcariboulite/tools/compare_rx_applications.py \
  --output build/radio-backend/comparison-new \
  --warmup 30 --duration 90 --order menu14,backend,backend,menu14
```

Use a fresh output directory. `--dry-run` prints the plan without starting
hardware. The runner now also accepts the restored menu-14 layout. Its reports
mark audio FIFO, ALSA playback, reader and worker measurements unavailable
when those display rows are absent, rather than treating missing counters as
zero. Such a run can compare IQ queue counters, CPU, memory, configuration and
register observations, but cannot repeat the full menu-14 health comparison
above. Backend snapshots still include the complete shared instrumentation.
The exact temporary
clock wrapper (`build/radio-backend/comparison-20261011/clock-wrapper.py`)
used for this run is retained with the artifacts; it uses the host's existing
`sudo` permission to set `performance` and restores the original governor in
`finally`, while launching applications as the invoking user.

The runner's counter/settings/process checks and terminal parser passed
**6 + 11 hardware-free tests**, and the four physical trials exercised the
real menu-14 controls, backend, monitor and graceful shutdown. This work adds
comparison tools and documentation; no production RX/TX behavior was changed
for the measurement.
