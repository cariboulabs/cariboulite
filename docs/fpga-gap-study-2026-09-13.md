# FPGA gap feasibility study — 2026-09-13

Current-source note (2026-10-16): this is a chronological experiment log. Its
early statements about disconnected gap/loopback signals, uncommitted changes,
loaded hardware and next steps describe their respective experiment stages.
Gap/reset changes were promoted on September 13 and loopback on September 16,
as recorded later in this document. The current packaged `firmware/top.bin`
SHA-256 is `dcdf0733ef1ee8088b82381148475a6e4de87c6f00b8d14480a46d48621d31f6`.
Use [firmware/README.md](../firmware/README.md) for the current build/register
contract and the [LVDS timing plan](lvds-fmax-next-test-2026-10-08.md) for planned
firmware work. October's [hardware-paced TX qualification](tx-hardware-paced-design.md)
uses this same FPGA baseline with updated driver/userspace code.

## Original study

No repository HDL, working bitstream, or embedded firmware header was changed.
No FPGA was programmed. Icarus Verilog was installed for simulation.

Local study directory:
`installations/fpga-gap-study/20260913T165728Z/` (ignored by Git).
It contains full firmware copies in `baseline/` and `gap-only/`, a `rollback/`
copy of the working bitstream and both firmware headers, and `hashes.json`.
All three original artifact hashes were rechecked after the study and match.
The working bitstream SHA-256 remains
`fe894f203f8c2fed4861ddac22890cbdc7d748ccf7fb6f1ac32bb950decc048d`.

## Isolated change and synthesis

Only the study's `gap-only/sys_ctrl.v` adds:

```verilog
assign o_tx_sample_gap = tx_sample_gap;
```

Loopback remains disconnected. Each copy was synthesized with:

```sh
yosys -Q -T -p 'synth_ice40 -top top -json study.json' top.v
```

| Synthesis cells | Baseline | Gap connected |
| --- | ---: | ---: |
| SB_LUT4 | 626 | 638 |
| SB_DFF variants, total | 581 | 585 |
| SB_RAM40_4K | 16 | 16 |

These are fresh source builds, not reconstruction of the exact saved bitstream.
They retain the existing reset/undriven-signal issues. Synthesis success does
not resolve those issues.

## Preliminary simulation

Icarus rejected the original `lvds_tx.v` because `INIT` is undefined and its
variable range part-select is not legal standard syntax. The simulation-only
copy uses `IDLE` for reset and `r_fifo_data[2*r_phase_count +: 2]` for the
two-bit select. These adjustments were not applied to the PNR copies.

The bench supplies a constant FIFO word, holds TX enabled with FIFO nonempty,
and reconstructs words from the serializer's two-bit output. For gap values
0 through 9, successive matching data words appeared every `gap + 1` word
slots, with zero spacing mismatches. Bench, simulation source and output are
in `simulation/`.

This is diagnostic steady-state evidence only. It does not validate the
unchanged reset behavior, FIFO latency/data ordering, all intervening idle-word
contents, dynamic gap changes, underruns, or physical DDR timing.

## Place and route

Both isolated builds used:

```sh
nextpnr-ice40 --lp1k --package qn84 --json study.json --pcf io.pcf \
  --asc study.asc --parallel-refine --opt-timing --seed 16 \
  --no-promote-globals --report study-timing.json
```

`--timing-allow-fail` was deliberately omitted. Logs are `study-pnr.log`.

- Gap-connected build: exit 0; 1,027/1,280 logic cells, 16/16 RAM blocks.
  Final LVDS clock maximum 65.70 MHz, passing the 64 MHz constraint.
- Baseline rebuild: exit 1; final LVDS maximum 61.37 MHz, failing 64 MHz.

This run demonstrates that connecting the gap need not exhaust resources.
It does not establish robust timing closure: the passing LVDS margin is small,
and the baseline failure shows sensitivity to placement/routing. Timing passes
apply only to the constraints and paths analyzed by this build.

## Next steps and rollback

Before programming: resolve reset/simulation syntax explicitly, extend HDL
tests to FIFO consumption and zero-word contents, and rerun synthesis/PNR on
the exact candidate source. Review resource/timing results before packing or
programming. No candidate binary or embedded header was generated here.

There is no active HDL change to undo. Study directories can be retained for
comparison; the running FPGA and working firmware files remain unchanged.

## Exact-source follow-up

`candidate/` preserves the gap connection plus `INIT` → `IDLE` and the legal
indexed part-select. A registered-output FIFO model feeding distinct words
exposed dropped samples: the pending load could be overwritten by state-machine
assignments. Constant-word spacing alone had hidden this problem.

`candidate-v2/` preserves a proposed correction:

- Always enter TX_FRAME after the first FIFO pull, allowing its pending load
  to reach the output before scheduling gap frames.
- Preserve a pending final word when the FIFO becomes empty in continuous TX.
- Update the previous TX-enable state at frame boundaries, where edge handling
  occurs, so a one-clock edge indication cannot disappear between boundaries.
- Use a blocking assignment consistently for the `next_sync` temporary.

Tests compile the exact candidate `lvds_tx.v` with Icarus:

```sh
iverilog -g2012 -s tb -o sequence-sim tb_sequence.v lvds_tx.v
vvp sequence-sim
iverilog -g2012 -s tb -o stop-sim tb_stop.v lvds_tx.v
vvp stop-sim
```

The sequence test passes for all gap values 0–15: 60 distinct words per setting,
in order, no missing/extra words, exact zero-word counts between data words
within each burst, initial transmission, empty/refill and settled stop/restart.
The active-stop test passes all 16 phase offsets at all 16 gap settings: TX
settles idle, stops pulling, and resumes after re-enable. It does not guarantee
delivery of in-flight samples across an active stop. Gaps outside the modem's
documented sample-rate choices are tested as HDL cases only.

The FIFO model matches registered read-data behavior; it is not a simulation
of the full asynchronous FIFO or the physical LVDS I/O cells. Dynamic gap
changes and a complete top-level simulation remain untested. Debug input is
held low in the benches; its top-level output remains disconnected.

Exact candidate synthesis: 637 LUT4 cells, 587 flip-flops, 16 RAM blocks.
PNR with the same options and seed 16 failed during placement (exit 255),
unable to place `r_counter_SB_DFFSR_Q_D_SB_LUT4_O_LC` after 149,605 attempts.
No timing acceptance or candidate bitstream exists for this corrected source.
Logs, benches and `source-hashes.json` remain in `candidate-v2/`.

Next work is placement/resource investigation of this fixed-source candidate,
with any alternative build settings recorded explicitly. Do not program it
based on the earlier gap-only PNR pass: that was different HDL.
Repository HDL and working firmware artifacts remain unchanged.

## Alternative placement trials

Three additional runs used the unchanged `candidate-v2/candidate.json`, the
same device, pin constraints and timing requirements. All exited 255 during
placement; none reached routing or timing acceptance.

| Log prefix | Change from original command | Result |
| --- | --- | --- |
| `candidate-seed1` | `--seed 1` | Could not place `smi_ctrl_ins.r_dir_SB_DFFER_Q_DFFLC` after 149,605 attempts. |
| `candidate-sa16` | `--placer sa`, seed 16 | Relative-constraint legalization failed for a chain starting at `lvds_rx_09_inst.o_fifo_data_SB_DFFESR_Q_D_SB_LUT4_O_LC`. |
| `candidate-heap16-timeout1` | Seed 16, `--placer-heap-cell-placement-timeout 1` | Increased limit to 1,196,836 attempts per cell; still could not place all cells. |

All other options remained `--lp1k --package qn84 --pcf io.pcf
--parallel-refine --opt-timing --no-promote-globals`; each run had separate
ASC/report output paths and a `<prefix>-pnr.log` in `candidate-v2/`.
No timing constraints were relaxed and `--timing-allow-fail` was not used.

Reported utilization is 1,026/1,280 logic cells (80%) and 16/16 RAM blocks
(100%). Aggregate free logic does not guarantee legal placement under the
device's cell-sharing and connectivity constraints. These failures do not
prove that the design can never fit, but seed changes and a larger placement
attempt limit have not resolved the problem.

Next step: inspect packing and constrained cell chains, then seek a small
resource/placement improvement in another isolated candidate. Rerun the
sequence and stop tests on any HDL change before evaluating PNR. Avoid
removing loopback/debug logic blindly: disconnected logic may already have
been optimized away. The corrected candidate is not ready to program.

The recorded hashes of all candidate Verilog files and all three working
firmware artifacts were verified again. No repository HDL was changed and
no bitstream was packed or programmed during these trials.

## PMOD RX-marker experiment

`candidate-no-pmod/` copies the candidate-v2 Verilog and pin constraints.
Only two assignments in `top.v` change: `w_rx_sync_input_09` and
`w_rx_sync_input_24` now take their software marker values directly, rather
than selecting PMOD inputs when the corresponding source bits are set.
Software marker support remains. External RX markers no longer function in
this candidate, although their configuration bits remain readable/writable.
The unused TX-marker wiring and PMOD port declarations are unchanged.

Both existing TX benches pass. These check sample order, gaps and stop/restart;
they do not exercise top-level RX metadata or physical PMOD pins.
`source-hashes.json` records the candidate Verilog files.

Synthesis and PNR used the same commands as candidate-v2, with seed 16 and
the default heap placer/attempt limit. Outputs are `candidate.json`,
`candidate.asc`, `candidate-timing.json`, `candidate-synthesis.log` and
`candidate-pnr.log` in the new directory. Both tools exited zero.

| Metric | candidate-v2 | Software-only RX markers |
| --- | ---: | ---: |
| LUT4 | 637 | 645 |
| Flip-flops | 587 | 587 |
| RAM blocks | 16 | 16 |
| Packed logic cells | 1,026 | 1,031 |
| Seed-16 PNR | Placement failed | Passed |

Final routed timing passes all reported clock targets. LVDS reaches
68.68 MHz against 64 MHz; the system clock reaches 78.49 MHz against
62.5 MHz. This is one passing run under the existing constraints, not a
hardware validation or proof of robust closure across placements.

Removing the input selection did not reduce aggregate logic utilization;
instead, the changed synthesized/packed design happened to place and route
successfully. This supports further evaluation of this candidate, but does
not establish PMOD routing as the sole cause of previous failures.

Working bitstream/header hashes were verified unchanged. No repository HDL
was changed, no binary was packed and no FPGA was programmed. Before hardware
use, review the loss of external-marker selection and prepare an explicit
candidate image and rollback procedure.

## Two-LED hardware-test image

`candidate-leds-on/` derives from `candidate-no-pmod/`, changing only
`io_ctrl.v`'s `led0_state` reset value to 1. Both LED registers now reset to
1, which lights both LEDs according to schematic page 4 (active-high outputs).
Software LED control remains available and can change this indication.
Earlier low-output experiments in `candidate-leds/` had the wrong polarity
and must not be used as the requested two-LED image.

The exact two-LED source passes both TX simulation suites, synthesis and
seed-16 PNR with unchanged constraints. Final LVDS Fmax is 75.60 MHz
(required 64); system Fmax is 79.26 MHz (required 62.5). `icepack candidate.asc
candidate.bin` produced SHA-256
`8441beeb12c2484d98bea8c5108b9bcc7cf61ba59943adf8af0f794b8454aaea`.
Source hashes and all build/test logs are saved with the image.

`gap-program.c` and its compiled helper are saved in that directory. The
helper uses production initialization to claim device ownership before GPIO
setup, forces file-based programming, checks the result and releases resources.
It links to the local build library. With the radio released, use:

```sh
study=installations/fpga-gap-study/20260913T165728Z
"$study/candidate-leds-on/gap-program" "$study/candidate-leds-on/candidate.bin"
# Rollback with the same helper:
"$study/candidate-leds-on/gap-program" "$study/rollback/firmware/top.bin"
```

After the user released their app, programming succeeded (`PROGRAM_RESULT=0`,
`program.log`). The local test app initialized and exercised option 11 TX
briefly at its displayed 430099936 Hz / -3 dBm, option 12 RX, and option 14
TX/RX toggles. It stopped the streams and exited normally. The monitor's
terminal rendering was incomplete in the tool output; no instrumentation
values are asserted from that screen.

`hardware-test.log` contains repeated RX timeouts in both receive runs.
The user subsequently confirmed that both LEDs are lit and the TX and RX
tests succeeded. This completes the basic hardware smoke test with user
confirmation of the visible indicator and radio operation. The RX timeouts
remain an unresolved log observation; successful audio does not establish
loss-free streaming. Nonzero sample gaps have not yet been tested on
hardware. The candidate remains loaded with the app
exited and transmission stopped. Original firmware/header files remain
unchanged; no image has been installed over the normal option-3 firmware path.

## 4 MS/s versus 2 MS/s TX experiment

The standalone `tx-rate-test.c` and executable in the study directory generate
a periodic 600 Hz FM tone with 2.5 kHz deviation at the selected RF sample
rate. They use S1G at 430099936 Hz and -3 dBm, transmit for approximately
three seconds, count samples accepted by the radio write API and stop TX.
The first 0.75 seconds are excluded from the reported steady measurement
to reduce initial buffer-fill bias. Partial writes retain the source offset.
This measures host-to-driver acceptance, not physical FPGA FIFO counters or
RF waveform fidelity. The option-14 puts/gets counters count software frames;
with 10 ms frames they should remain approximately 100/s at either rate.

The initial 2 MS/s attempt aborted before TX because gap readback was zero.
Investigation found a library return-code mismatch:
`io_utils_spi_transmit` returns zero on success, whereas
`caribou_fpga_spi_transfer` expects two transferred bytes and returns 1.
Consequently, the gap setter aborts its read-modify-write before writing.
The radio rate setter ignores that error. Delayed retries confirmed this.
Logs: `tx-rate-2m.out/.log` and `tx-rate-2m-retry.out/.log`.

The final isolated helper bypasses that wrapper for system register 6,
checks zero-success SPI returns, preserves the upper nibble when setting
the gap, and verifies readback before TX. No normal library source was
modified. Its cleanup restores modem 4 MS/s and writes gap/source register
zero (software sync sources); the normal application's source was unchanged.

| Test | Modem SR | FPGA gap | Accepted samples/s after warmup |
| --- | ---: | ---: | ---: |
| 4 MS/s baseline | 1 | 0 | 3,998,293.410 |
| 2 MS/s direct-register test | 2 | 1 | 1,999,154.792 |

Both runs exited zero and reported zero zero-length write returns. The
measured 2 MS/s rate is approximately 50% of baseline. Logs/results are
`tx-rate-4m.out/.log` and `tx-rate-2m-direct.out/.log`. The initial baseline
preceded the helper's direct-register workaround; its default gap was zero.
Finite buffering and short test duration limit precision; these results do
not independently prove absence of hardware underflow or sample loss.

The candidate FPGA remains loaded, TX is stopped and the app is released.
User confirmation of the reduced-rate tone remains pending. Next software
work is correcting and testing the FPGA SPI wrapper's success convention
before integrating adjustable TX rate into the normal audio pipeline.

## Intermittent TX startup follow-up

The user confirmed hearing a tone consistent with 600 Hz on the successful
2 MS/s repeat (`tx-rate-2m-repeat.out`, 1,999,168.327 accepted samples/s).
They heard nothing on the next repeat (`tx-rate-2m-repeat2.out`): only
524,288 samples were accepted, followed by 29 zero-length write returns
and zero steady-state progress. This is an intermittent functional failure,
despite earlier successful reduced-rate operation.

The helper previously returned zero after cleanup even for stalled traffic.
It now returns 7 when steady acceptance is outside 90–110% of the requested
sample rate. This is a coarse smoke-test threshold, not RF validation.

A subsequent 4 MS/s / gap-0 comparison on the same candidate also stalled:
524,288 accepted samples, 29 zero returns, zero steady progress, exit 7.
Results are `tx-start-4m.out/.log`; `tx-start-4m-kernel.log` preserves the
kernel tail. Thus the failure is not restricted to nonzero sample gaps.
This does not rule out a shared FPGA or host-side startup problem.

Kernel diagnostics for the failed 2 MS/s run showed ACTIVE with SMIL=0
immediately after TX startup and repeated watchdog reports of that state.
The failed 4 MS/s run instead showed SMIL=0x3f just after startup and
0x24e at shutdown, but no steady host-data consumption. These observations
are not identical and do not yet establish the root cause.

The driver initializes its TX kfifo from six 512 KiB buffers; kfifo uses the
largest fitting power-of-two capacity, 2 MiB, matching 524,288 four-byte
samples. The plateau is therefore consistent with filling the host queue
without sustained DMA consumption, rather than evidence of transmitted data.

No driver or FPGA change was made in this follow-up. TX was stopped and
the helper restored 4 MS/s / gap 0. Next investigation should examine SMI/DMA
startup and callback progress, including the current ordering that issues
DMA before prefilling its TX buffers. A cause or correction is not yet proven.

## Startup-order experiment and reconfiguration recovery

`driver-start-order/` contains an isolated driver copy, build log and module.
It moves DMA preparation/issue after TX-buffer prefill, SMI FIFO clearing
and transfer-length setup; it also propagates programmed-transfer setup
failure and logs period count / TX queue occupancy on stop. It built against
the running 6.18.39+rpt-rpi-v8 headers. The installed module file was untouched.

Temporarily loading this module with parameters 6/2/3 did not recover TX:
`tx4m.out` reports the same 524,288-sample plateau, 29 zero returns and exit 7.
`kernel.log` records zero completed DMA periods and 2,097,152 queued TX bytes
at stop. Startup ordering is a real buffer-ownership concern, but this
experiment does not demonstrate it as the cause of this persistent stall.

The installed driver was restored with modprobe and the same parameters.
Then the exact two-LED FPGA binary was reprogrammed successfully
(`reprogram.log`), without HDL changes. The next 4 MS/s test passed:
3,998,331.284 accepted samples/s, no zero returns, exit zero
(`tx4m-reconfigured.out/.log`).

This narrows the investigation toward persistent FPGA/interface state or
reset/handshake interaction. It does not prove an FPGA-only cause: driver
reload and programming also affect the surrounding hardware state. No
startup-order patch has been promoted to the repository or installed.
The installed driver and experimental two-LED FPGA are currently loaded;
TX is stopped and the helper restored 4 MS/s / gap 0.

Next: inspect reset coverage and SMI request generation, then compare soft
reset with full reconfiguration if the stall recurs. Keep the previously
passing gap logic unchanged until the failure mechanism is better understood.

## Reset-clock defect reproduced in simulation

`top.v` holds `r_counter` at zero during reset; this also stops `w_clock_sys`.
`complex_fifo.v` resets pointers and flags synchronously, so the FIFO side
clocked by `w_clock_sys` receives no rising edges while reset is asserted.
The opposite, LVDS-clocked side can reset, leaving inconsistent pointers.
This affects the TX write side and RX read side. SMI request generation uses
FIFO full/empty flags, so stale FIFO state can affect the interface handshake.

`reset-study/tb_reset.v` uses the actual FIFO module with the top-level clock
divider behavior: after four writes, reset leaves the write pointer at 4
and the FIFO reports nonempty afterward (`result.txt`). The counterpart
`tb_reset_fixed.v` keeps the divider running: both pointers clear and the
FIFO is empty (`fixed-result.txt`). These are controlled reset simulations,
not proof that this defect caused the observed hardware stall.

`candidate-reset-clock/` copies the two-LED source and changes only the
divider behavior in `top.v`: initialize it to zero at configuration and
toggle it regardless of soft reset. Other logic in that block remains
gated by reset. This preserves clock edges for synchronous FIFO reset.
Both TX sequence/stop benches pass, and synthesis plus seed-16 PNR pass
with unchanged timing constraints (`synthesis.log`, `pnr.log`, `timing.json`).
The candidate has not been packed or programmed. No tracked HDL or installed
driver was changed. Hardware testing must explicitly exercise reset and
restart before claiming this resolves the intermittent failure.

### Reset-clock hardware checks

The candidate was subsequently packed and programmed successfully. Binary
SHA-256: `a90a908f0e4e57a7a2f5e26e97303a3f43cadcaeb5b1be39eca4a604e0e00bfe`.
The installed driver remains in use. `tx-reset-test.c` extends the standalone
test with an explicit FPGA soft reset and 10 ms settling interval after
driver initialization, before configuring the tone, modem rate and gap.
Each run transmits for approximately three seconds and then stops/restores
4 MS/s and gap zero. The SPI return-code workaround remains local to the test.

The 4 MS/s run measured 3,998,232.831 accepted samples/s, and the first
2 MS/s run measured 1,999,171.858. Both exited zero with no zero-length
write returns. Logs and outputs are `reset4m.*`, `reset2m.*` and
`reset2m-repeat.*` in `candidate-reset-clock/`.

These checks exercise explicit soft reset in addition to normal app startup.
They are short throughput smoke tests, not proof of long-term reliability,
RF waveform quality or normal-app restart behavior without an explicit reset.
The reset-clock candidate remains loaded; previous images remain available.
The user confirmed hearing all three tones clearly and at the same perceived
pitch. This supports correct audible tone behavior at both sample rates;
it is not an instrumented frequency or modulation-quality measurement.
The repeated 2 MS/s run measured 1,999,167.044 accepted samples/s, with no
zero-length write returns and exit zero.

### Normal FPGA API restored

The repository FPGA SPI wrapper now returns zero after a successful
`io_utils_spi_transmit`, matching that layer's status contract instead of
expecting a two-byte return value. Negative failures remain failures.
`test_fpga_spi_status.py` compiles the actual wrapper and gap API against
a simulated SPI register, checking changed/unchanged gaps, preserved upper
sync bits, readback and injected read/write failures. It passes, as does
the local CMake build and `git diff --check`.

`candidate-reset-clock/tx-api-test.c` removes the direct-register workaround
from the reset test and verifies restoration through the normal getter.
Its 2 MS/s hardware run read back modem SR=2 and FPGA gap=1, measured
1,999,100.350 accepted samples/s without zero returns, exited zero and
read back restored gap=0 (`api2m.out/.log`). It still performs an explicit
soft reset before setup; normal-app repeated startup remains a separate
validation step. The local build library/app were rebuilt; nothing was
installed system-wide or committed in this step.

The user subsequently completed their usual application test cycle and
reported TX and RX working normally. This adds user-confirmed normal-app
functional coverage to the API test above; the number of process restarts
and long-duration reliability were not established by that report.

## Option 11 selectable TX rate

The local test app now offers `2` (2 MS/s) and `4` (4 MS/s) inside option 11.
Rate changes are rejected while TX is running. A stopped rate change destroys
and recreates the pipeline so all workers use the same immutable frame size:
20,000 or 40,000 samples per 10 ms. The FM modulator uses the selected rate;
audio remains 48 kHz. Setup verifies modem rate and FPGA gap through the
normal APIs and displays the verified settings. Returning from option 11
restores its radio to 4 MS/s/gap 0. Other TX pipeline callers default to
4 MS/s; RX processing is unchanged.

Lifecycle tests now mock the rate APIs and check both RF rates, frame sizing
and modulator output length. Those checks and the local CMake build pass.
A live UI check selected 2 MS/s, started/stopped TX, rejected a rate change
while active, selected 4 MS/s and started/stopped again, then exited cleanly.
`option11-rate-test.log` records that run. Audible quality and repeated
user-driven restarts still require user confirmation.

To test: run `build/cariboulite_test_app 2> debug.log`, choose `11`, choose
`2`, and toggle TX with `1`. Stop TX before choosing `4` for comparison.
The currently loaded experimental FPGA supports gaps; option 3 still loads
the older saved image, so do not use it to prepare this experiment.

### Pitch correction and remaining startup failure

User testing found correct 4 MS/s audio, approximately 300 Hz at 2 MS/s,
and a failed subsequent start. `nbfm_pull_iq` still hard-coded the 250/3
audio-to-RF ratio for 4 MS/s despite accepting a configurable RF rate.
It now advances the resampling phase using the configured audio and RF
rates. `test_nbfm_rate.py` demodulates generated IQ and measures 599.59 Hz
at both 4 and 2 MS/s, checks all audio blocks are accepted, and passes.
The build and pipeline lifecycle tests also pass.

Two live option-11 starts at 2 MS/s were attempted after this correction.
Both UI starts returned ON, but both shutdown tone injections aborted;
the second start again showed ACTIVE with SMIL=0. Logs are
`option11-pitch-fixed.log` and `option11-pitch-fixed-kernel.log`.
These are not successful hardware validations of the pitch correction.
TX was stopped, the app exited, and option 11 restored 4 MS/s / gap 0.
The sample-rate-independent modulator defect is corrected; the intermittent
stream startup defect remains unresolved and no changes were committed.

### Option-11 clean-start correction

Before each option-11 TX start, the app now destroys/joins the previous
pipeline, soft-resets the FPGA, and recreates the pipeline with the selected
rate before activating TX. This clears stale FPGA FIFO state and DSP/audio
queues and prevents old workers from overlapping the reset. It relies on
the loaded reset-clock candidate for effective synchronous FIFO reset.
The monitor pipeline is unchanged. This is a controlled reset-based recovery
sequence, not a complete proof of the underlying stall mechanism.

After rebuilding and passing lifecycle tests, three consecutive 2 MS/s
starts and a 4 MS/s start through option 11 completed without start/tail
tone aborts. Kernel logs show continuing DMA callbacks. The 4 MS/s run
reported one missed-buffer event, so loss-free streaming is not established.
Logs: `option11-clean-restart.log`, `option11-clean-restart-kernel.log`.
TX was stopped and the app exited; 4 MS/s/gap 0 was restored by option 11.
Changes are uncommitted, pending user confirmation of pitch and repeated
start/stop behavior. Original FPGA images and installed driver remain intact.

The user subsequently confirmed successful TX start/stop and sample-rate
changes in their test cycle. This validates the option-11 clean-start and
rate-selection workflow on the currently loaded reset-clock FPGA candidate.
Changes remain uncommitted; this confirmation does not establish long-term
or loss-free streaming performance.

## Promotion of the tested app and FPGA

The user authorized committing both after confirming successful TX start/stop
and rate switching. The reset-clock candidate HDL, binary, routed ASC and
synthesized JSON were copied to `firmware/`; BLIF was exported from that same
JSON. Both embedded headers were regenerated from the tested binary and their
payloads verified byte-for-byte. Its SHA-256 remains
`a90a908f0e4e57a7a2f5e26e97303a3f43cadcaeb5b1be39eca4a604e0e00bfe`.
No synthesis or routing change was introduced during promotion.

The earlier warnings that option 3 loads the old image are historical:
**option 3 now loads the promoted image**, and the rebuilt local library
embeds it too. Both user LEDs default on. External PMOD RX markers are
removed; software markers remain. Loopback remains disconnected.

FPGA benches are preserved under `firmware/tests/`. The modulator pitch,
pipeline lifecycle, SPI regression and FPGA simulations passed, and the
local application/library rebuild passed. The installed kernel driver was
not changed. System-wide userspace libraries were not installed.

The previous committed firmware can be recovered from the parent of the
promotion commit; the ignored study rollback directory also retains the
original artifacts. Earlier candidate results and limitations above remain
part of the experiment record, not promises of exhaustive validation.

## Additional step 1: option-12 RX rate selection

Option 12 now accepts 2/4 for 2 or 4 MS/s while stopped, recreating the RX
pipeline when the rate changes. The reader uses 20,000/40,000 samples per
10 ms and applies the selected modem RX rate on start. The demodulator's
first decimation stage changes from 20 to 10 at 2 MS/s; its second stage
remains 4, retaining the 50 kHz intermediate and 48 kHz output audio rates.
The output remains `plughw:Loopback,0,0`; leaving option 12 restores 4 MS/s.

Local build, lifecycle checks at both RX frame sizes and diff checks pass.
The live UI test started/stopped both rates and rejected changes while
running, then exited cleanly (`option12-rate-test.log`). RX timeouts remain
in the log, so audible RX validation is pending user testing. No TX was
started in this check. No firmware or driver change was made. Option 14's
rate-selection UI is deferred until this first additional step is validated.

The user subsequently confirmed that option 12's changes work correctly,
including blocking sample-rate changes while the RX chain is active.
This completes user functional validation of additional step 1. The changes
remain uncommitted; option 14 is the next requested step.
The user additionally confirmed correct audio pitch and quality and authorized
committing the option-12 changes before proceeding to option 14.

## Additional step 2: option-14 TX/RX rate selection

Monitor keys `2` and `4` select a shared 2 or 4 MS/s rate for both pipelines.
Selection is rejected while either chain runs; stopped changes recreate
both pipelines. The header displays the selected rate and the status area
explains blocked changes. TX starts join both old pipelines, soft-reset the
FPGA and recreate them before activating TX, following the validated option-11
clean-start sequence. Exit restores both modem rates to 4 MS/s and gap zero.
The existing ALSA input/output devices remain unchanged.

The shared RX start routine also now chooses the SMI stream for the bound
radio; previously it unconditionally selected S1G even in the HiF monitor.
Lifecycle regression checks cover both radio selections and pass. Build
and whitespace checks pass. A live monitor UI check exercised RX/TX at both
rates and active-chain rate blocking. RX frame counts advanced and no TX
start/tail aborts were logged (`option14-rate-test.log`); RX timing warnings
still occurred. The app exited with TX/RX stopped. Audio quality and repeated
user-driven switching await user confirmation. No changes are committed yet.

The user confirmed all option-14 tests passed, including repeated TX/RX
start/stop and rate changes. At their request, each FIFO heading now shows
samples per 10 ms frame (20,000 at 2 MS/s, 40,000 at 4 MS/s), using its
pipeline configuration. Existing puts/gets counters still count frames.

The user also tested option 14 with actual microphone input and reported
that it works correctly, adding live audio-input validation to the previous
TX/RX start/stop and sample-rate switching tests.


## Single loopback-connection build attempt — 2026-09-15

Jan authorized one isolated attempt, with no retries if it failed.
Copied the current committed Verilog, constraints and Makefile to a temporary
directory and added only this assignment in `sys_ctrl.v`:

```verilog
assign o_debug_loopback_tx = debug_loopback_tx;
```

One `make top.bin` invocation passed synthesis, placement/routing, timing and
packing (exit zero, approximately 28 seconds). The existing seed 16 and all
Makefile constraints/options were retained; timing failure was not allowed.
Final reported LVDS Fmax was 70.19 MHz against 64 MHz and system-clock Fmax
was 83.50 MHz against 62.5 MHz. All reported clock targets passed. Packed
logic usage was 1,022/1,280 cells (79%); RAM usage remains 16 blocks.

The candidate synthesis JSON contains 634 LUT4 cells versus 639 in the
committed promoted JSON, and three additional flip-flops. This is an artifact
comparison, not a fresh paired baseline build; no second build was attempted.
Connecting loopback did not prevent this candidate from fitting or meeting
its analyzed timing constraints.

Candidate binary SHA-256:
`dcdf0733ef1ee8088b82381148475a6e4de87c6f00b8d14480a46d48621d31f6`.
Sources, Makefile, constraints, synthesis/routing logs, timing report, binary,
exit status and original-file hashes are preserved locally under
`installations/loopback-one-try/cariboulite-loopback-one-try-_30zy23j/`.

This establishes build feasibility only. No loopback functional simulation,
new hardware validation or FPGA programming was performed. Repository HDL,
validated binary and both firmware headers were verified unchanged by hash.
Issue 10 remains open pending behavior tests and promotion; a successful
build alone does not close it.


### Loopback functional check — 2026-09-15

The isolated candidate passed a new RTL bench connecting its actual
`sys_ctrl.v` and `lvds_tx.v`, with independent register/serializer clocks and
a registered-output FIFO model. Register commands are applied at the sys_ctrl
interface; the SPI pin decoder and physical DDR cells are not instantiated.
The test checks chip-select/address gating and all eight debug-register bits,
then 1,024 combinations of 16 sample gaps, 16 serializer phases for launching
register writes, TX enabled/disabled and FIFO empty/nonempty.

All cases passed: debug bit 3 reaches the output, other writes preserve it as
appropriate, settled loopback serializes exactly `0x84037048`, and no FIFO
samples are consumed while settled in loopback. Clearing debug resumes normal
TX when enabled with data, or returns to idle zero/sync frames otherwise.
Reset during loopback clears the register and transmitter. Entry/exit checks
allow 512 serializer clocks to settle; each frame-content check decodes 24
complete words. The original disconnected sys_ctrl fails this same bench at
the reset-output assertion, confirming sensitivity to the missing connection.

The test also confirms existing semantics: debug selection overrides normal
TX enable and remains active across TX-enable toggles. Clear the debug bit
(or reset) to leave this FPGA test-pattern mode. This is not a demonstration
of analog/RF loopback through the modem.

Existing TX sequence, TX stop/restart and FIFO-reset simulations also passed
against candidate modules. Bench, reproduction instructions and logs are in
the preserved candidate directory as `tb_loopback.v`, `FUNCTIONAL-README.md`,
`functional-check.log` and `disconnected-control.log`.

No additional synthesis/routing build was attempted. Candidate HDL was checked
against the one-line experiment, its binary hash still matches the successful
build, and original repository HDL/image/header hashes remain unchanged.
No hardware was accessed or image promoted. Physical timing and modem behavior,
full SPI/top-level integration, metastability and lossless preservation of
in-flight user samples during debug switching remain outside these checks.


### Menu 14 interface-loopback control — 2026-09-15

Menu 14 now accepts `L`/`l` to toggle the combined FPGA test-pattern and
AT86RF215 external-interface loopback. This is intended for the isolated
candidate above; the validated repository FPGA binary remains unchanged.
A new FPGA API writes debug register bit 3, clearing the other debug bits.
The modem's EXTLB bit and the FPGA pattern enable are separate controls.

Entry stops the normal TX/RX pipelines, idles SMI, clears modem EEC and EXTLB,
commands both radios to TRXOFF and checks their states. It enables both I/Q
interfaces, keeps the RF front end in low-power mode, selects FPGA/SMI RX24,
and commands only RF24 RX for the interface clock. After checking RF09 is
TRXOFF and RF24 is RX, it enables EXTLB with EEC disabled, enables the FPGA
pattern and starts SMI RX24. It never issues TX or TX_PREP commands.
This follows the datasheet section 4.5.7 restriction against normal RF
transmission with EXTLB enabled.

The monitor takes bounded 10 ms reads of up to 4,096 decoded samples and
shows up to 16 fresh I/Q pairs, total captured samples and timeout count.
Normal audio processing stays stopped. This is a sample preview, not a
continuous loss-free capture or automatic pattern-verification result.
A timeout clears the preview rather than showing old data as fresh. Read
errors initiate cleanup. The displayed `0x84037048` is the generated FPGA
frame; the preview is decoded HiF I/Q, not raw serialized words.

While loopback is enabled or cleanup is incomplete, TX, normal RX and
sample-rate changes are blocked. `L` disables it; `Q` cleans up before exit.
Cleanup clears the FPGA pattern, EXTLB and EEC, stops both radios, and restores
saved interface settings after successful shutdown (never restoring EXTLB).
If cleanup fails, the monitor stays open with controls locked; `L` retries.
Normal TX starts also check EXTLB and reject failed register reads. An already
running normal TX can still be stopped without that register read succeeding.

Validation: the complete local application/library/Soapy build passed.
`test_monitor_loopback.py` checks actual controller sequencing, control-key
interlocks, no TX/TX_PREP commands, register restoration, fresh/short/timeout/
failed captures, every start/stop I/O failure, register mismatch, failure to
reach RX and cleanup retries. `test_fpga_spi_status.py` now also verifies the
actual FPGA loopback command's opcode, enable/disable values and error paths.
The TX/RX lifecycle/monitor-startup/FIFO cancellation regression passed.
`git diff --check` passed. No app was launched against hardware and no FPGA
was programmed during this implementation.

#### Manual check with the candidate loaded

1. Release all radio applications. Temporarily program the isolated candidate
   using the existing `gap-program` helper, with the candidate binary path:

   ```sh
   installations/fpga-gap-study/20260913T165728Z/candidate-leds-on/gap-program \
     installations/loopback-one-try/cariboulite-loopback-one-try-_30zy23j/top.bin
   ```

2. Launch `build/cariboulite_test_app 2> debug.log` and select menu `14`.
   Do not select option 3 for this experiment: the repository's validated
   image still has the loopback output disconnected.
3. Press `L`. Expect loopback ON, RF09 STATE `0x02`, RF24 STATE `0x05`, and
   IQIFC0 bit 7 set with bit 0 clear. Look for fresh, repeating I/Q values in
   the loopback preview. Capture the actual observed values for comparison;
   ON confirms setup commands succeeded, not that the FPGA pattern arrived.
4. Press `T`, `R`, `2` or `4` while loopback is active and check that each is
   rejected. Press `L` to stop; both radios should return to `0x02` and EXTLB
   should clear. Repeat loopback start/stop, then quit directly with `Q` while
   enabled and confirm cleanup completes in `debug.log`.
5. Only after successful loopback cleanup, perform ordinary RX/TX regression
   checks separately. To restore the validated image, quit the app and use
   the same programming helper with `firmware/top.bin`, or use option 3.

These are operator instructions, not completed hardware-validation results.
The candidate and both local APIs are available for the next hardware test;
source changes are uncommitted and system-wide userspace is unchanged.


### First menu-loopback run: decoder correction — 2026-09-15

Jan reported an error after pressing `L`. Preserved their log at
`installations/loopback-one-try/validation/user-test-20260915-2210.log`.
It records repeated successful setup/cleanup and later normal-RX synchronization
errors, but the original loopback reader did not log its failure code. Thus
this log alone does not identify the precise returned loopback read error.

A deterministic software defect was found: the fixed TX frame `0x84037048`
has I_DATA[0] (bit 16) set. The normal SMI RX framing check uses mask
`0xC001C000` and requires `0x80004000`, rejecting that frame at every byte
alignment. EXTLB can return this TX control bit unchanged. The RTL loopback
bench did not exercise the userspace RX decoder, and the controller test had
mocked already-decoded samples, so neither caught this integration mismatch.

Menu 14 now calls a separate `caribou_smi_read_loopback_timed` diagnostic API.
It accepts the echoed TX control bit while checking I/Q framing, decodes only
complete frames, and does not interpolate missing tail samples. Normal RX
retains its stricter validation. Loopback read failures now log the error code.
This software correction does not change the candidate FPGA image or its
single-build result.

Tests against the actual SMI implementation reproduce normal-reader rejection
of the exact pattern and verify diagnostic decoding on both channels, all
three nonzero byte offsets, exact returned counts, short/empty requests,
corrupt data, I/O failures and timeout budgets. Controller tests and the full
Soapy/SMI regression pass; the local app/library/Soapy build passes.
Expected decoded HiF values for an unchanged echoed frame are I=`0xF824`
(-2012), Q=`0x0201` (513), shown as signed 16-bit hexadecimal in the preview.
Actual hardware return mapping still needs confirmation in the next run.

Restart the rebuilt app, select 14 and press L again with the candidate still
loaded. No FPGA rebuild or reprogramming is required for this software fix.
Jan's previous message confirms testing reached L, not a programming
failure; successful pattern capture remains unverified.


### Confirmed hardware pattern capture — 2026-09-15

Jan supplied menu 14 output showing loopback ON, 1,412,895 captured
samples, zero read timeouts, and all 16 preview entries equal to I=`F824`,
Q=`0201`. These exactly match the expected decoded HiF values for the FPGA
frame `0x84037048`. This validates the fixed pattern through the FPGA-to-modem
interface, modem external loopback, FPGA receive path and userspace decoder
for the displayed capture. The cumulative sample count is not a full-pattern
comparison: only the shown preview values were supplied, and this snapshot
view does not establish loss-free streaming.

The accompanying log is preserved at
`installations/loopback-one-try/validation/user-pattern-match-20260915-2221.log`.
It includes four completed loopback enable/cleanup pairs, then a final enable
followed by signal-2 termination and general driver teardown. The last session
has no monitor-loopback cleanup-complete entry; signal-path restoration is not
validated by this run. Use L to clear loopback or Q to run monitor cleanup.
Normal RX timeout messages elsewhere in the log are separate from the zero
loopback-read-timeout counter in the supplied capture.

The isolated candidate now has a successful build, RTL tests and a matching
physical loopback capture. Promotion into the repository firmware and final
commit remain outstanding; this report did not change or program firmware.


### Loopback validation and promotion — 2026-09-16

Jan confirmed that the debug-loopback tests were successful and authorized
promotion and commit. This adds user functional confirmation to the matching
hardware pattern capture above; it does not add a new signal-termination test
or a loss-free-streaming measurement.

The exact single-build candidate was promoted to `firmware/`: the one-line
`sys_ctrl.v` connection plus its binary, ASC, JSON and BLIF. Both embedded
firmware headers were regenerated from that binary. SHA-256:
`dcdf0733ef1ee8088b82381148475a6e4de87c6f00b8d14480a46d48621d31f6`.
All other repository HDL and pin constraints match the candidate. No new
synthesis/routing run or hardware programming was performed during promotion.
The combined loopback RTL bench is now preserved in `firmware/tests/`.

Option 3 and the rebuilt local library now use the loopback-capable image;
earlier instructions warning that option 3 loads a disconnected image are
historical. Menu 14's loopback controller, diagnostic decoder and regression
tests are included in this promotion. Issue 10 is resolved. System-wide
userspace libraries and the installed kernel driver are unchanged.

The previous committed firmware is available from the parent of this promotion
commit. Its SHA-256 is
`a90a908f0e4e57a7a2f5e26e97303a3f43cadcaeb5b1be39eca4a604e0e00bfe`.

Promotion checks passed: byte-for-byte header payload verification, TX sequence,
active-stop, FIFO-reset and 1,024-case loopback RTL simulations, firmware build
failure/dependency checks, monitor-loopback failure/cleanup tests, FPGA SPI,
TX/RX lifecycle, NBFM rate and Soapy/SMI regressions. The local full CMake build
passed. Whitespace checks pass with CRLF recognized in the existing SMI files.
