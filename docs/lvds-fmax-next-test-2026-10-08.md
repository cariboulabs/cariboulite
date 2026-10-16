# Next FPGA development test: LVDS Fmax before sequencer integration

Recorded 2026-10-08. Status: **planned; optimization tests have not started**.
This is the next firmware development test, followed by a separate PMOD
sequencer integration test after the acceptance criteria below are met.

Current-source review on 2026-10-16: the receive RTL, synthesis flags and routing
flags remain unchanged, so this is still a plan. The October 15 hardware-paced
TX implementation changes driver/userspace behavior and uses the existing
FPGA image; its [Pi Zero/Pi 4 qualification](tx-hardware-paced-design.md)
does not satisfy the optimization or sequencer acceptance criteria below.

## Objective and starting point

Establish a repeatable firmware build with LVDS timing margin while preserving
receive data, framing, sync metadata and FIFO behavior. Start from the user's
physically tested firmware on `main`, revision
`46346e5cf69eb81ffc6778d18cd36d3e7b3b5478`, without the PMOD prototype.

Run experiments in isolated source copies or a development branch. Preserve the
known working bitstream and generated firmware headers. This plan records future
work; saving it does not change synthesis settings, RTL or hardware.

Previous PMOD work is saved on branch `fpga-pmod-sequencer-fit-test` and in stash
`3dd33ebab28aff563068c714e29b69ba09754c08`
("Save FPGA PMOD experiments before main firmware CE2 test"). Recover the
required prototype changes selectively for the later integration test so that
old build settings do not overwrite the selected LVDS baseline.

## Evidence motivating the test

Fresh isolated builds of the committed main firmware produced these results:

| Synthesis setting | Logic cells / 1280 | RAM / 16 | System Fmax | LVDS Fmax | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| Main default | 1022 | 16 | 73.10 MHz | 74.96 MHz | Full build and bitstream packing passed |
| `-dffe_min_ce_use 2` (CE2) | 1035 | 16 | 85.70 MHz | 56.24 MHz | Routed LVDS timing failed; no new bitstream packed |
| Required | — | — | 62.5 MHz | 64 MHz | All constrained clocks must pass |

These fresh build results are distinct from the user's physical validation of
the original main firmware. Tools: Yosys `0.60+64` (`d523c88c3`) and
nextpnr-ice40 `0.9-49-g7bd1336f`; target LP1K/QN84, seed 16, parallel
refinement, `--opt-timing` and `--no-promote-globals`.

The failing CE2 path is:

```text
iq_rx_24.D_IN_1 (falling edge)
  -> two LUTs
  -> shared enable of 32 receive-data registers (rising edge)
Total: 8.89 ns = 1.36 ns logic + 7.53 ns routing
Available at 64 MHz: 7.8125 ns; shortfall approximately 1.08 ns
```

The default build has the same type of half-cycle receive path, with a shorter
6.67 ns routed delay. The decoder's input-dependent wide enable is visible in
[`lvds_rx.v`](../firmware/lvds_rx.v), particularly idle sync detection and
`o_fifo_data` updates. CE2 removes rarely shared enables; this 32-register
enable remains shared.

Earlier CE2 builds of the same synthesized design reached 63.14 and 69.57 MHz
LVDS. All three CE2 netlists had SHA256
`873d6c6f695139563b9cc8c8567ea50f87ad66a5df9c606dd5edb3cdc8bc15ac`.
This demonstrates routed-result variation; it does not establish the precise
cause. Parallel refinement is a candidate to investigate.

Original local artifacts, if still available, are under
`/tmp/cariboulite-main-ce2-test.stnjg5ew/`: `README.md`, `metadata.json`,
`results.json` and each variant's synthesis/nextpnr logs. The evidence summary
above is retained here because temporary artifacts may disappear.

## Ordered experiments

### 1. Establish build repeatability

- [ ] Reproduce unchanged main/default and main/CE2 builds in isolation with
  the original flags, using fresh outputs for every attempt.
- [ ] Compare serial refinement by removing `--parallel-refine`. Keep the
  source, synthesis setting, seed 16, target, constraints and other flags fixed.
  Retain the existing `--opt-timing` option.
- [ ] Repeat each comparison at least three times. Record source/netlist hashes,
  placement checksums, tool versions, complete commands, resource use, routed
  timing and build/pack exit status. Investigate remaining variation before
  attributing a small Fmax change to an optimization.

Serial refinement is a reproducibility experiment; improved Fmax is not assumed.
Keep default and CE2 synthesis as separate comparison columns throughout.

### 2. Register each complete DDR pair before receive decoding

- [ ] Add a rising-edge stage for both bits of each receiver's DDR pair before
  the RX state machine. Preserve `{D_IN_0, D_IN_1}` ordering and RX24 inversion
  in [`top.v`](../firmware/top.v). The initial data-stage cost is four registers
  across the two receivers; metadata/startup handling may need more.
- [ ] Delay the associated sync-input metadata by the same cycle. Define reset
  and startup validity so stale or unknown staged data cannot create a frame.
- [ ] Document the added one-LVDS-cycle receive latency. Preserve channel
  selection behavior and evaluate FIFO fullness at the delayed completion event;
  do not indiscriminately delay the FIFO-full flag with the input pair.
- [ ] Inspect the routed critical paths. The intended benefit is a short
  half-cycle input-to-stage path followed by a full-cycle decode path. Verify
  that synthesis retained the intended stage and that another path has not
  become the limiting bottleneck.

This is the strongest RTL candidate, with an expected benefit that still needs
measurement. The device's separate positive/negative-edge input capture is
described in the [Lattice iCE40 LP/HX datasheet](https://www.latticesemi.com/view_document?document_id=49312).

### 3. Reduce the wide enable dependency if needed

- [ ] Evaluate separating receive shifting from frame qualification so raw
  DDR data no longer drives the enable of all 32 receive-data registers.
- [ ] Preserve the completed word and registered `o_fifo_push`/`o_fifo_data`
  alignment. The real FIFO consumes their previous values on the next rising
  edge; continuous shifting must not overwrite the word before that edge.
- [ ] Compare this change independently against the staged receiver. Record
  timing and area costs, and retain it only if the measured result justifies it.

### 4. Compare routing and global-control options separately

- [ ] Test `--tmg-ripup` against the selected source and placement flow.
- [ ] In a separate comparison, remove `--no-promote-globals` to allow eligible
  high-fanout controls to use global routing. The LVDS clock already has an
  explicit global buffer; inspect which other nets are promoted and whether
  placement, routing or either clock domain regresses.
- [ ] Inspect receive-stage/decode locality near the input pins if routing is
  still dominant before considering additional placement constraints.

Keep each option independent initially. Test any selected combination explicitly.
Judge final routed timing, retaining the real edge relationships and all current
frequency constraints; placement estimates alone are insufficient.

## Behavioral verification

Existing tests in [`firmware/tests/README.md`](../firmware/tests/README.md) cover
TX sequencing, stop, FIFO reset and controller/TX loopback. They do not validate
the RX decoder or physical DDR capture. Add a targeted RX comparison bench for
any receive RTL change, covering:

- [ ] Exact I/Q words, bit order, sync-bit association and the documented added
  latency for both receivers, including RX24 inversion at the top-level boundary.
- [ ] Consecutive frames, idle periods, malformed Q sync and resynchronization.
- [ ] Reset/startup and reset during every receive phase; no stale frame pushes.
- [ ] FIFO-full transitions near completion, correct push/data alignment and
  no lost or duplicate accepted words. Compare behavior with the reference while
  accounting explicitly for latency and completion-time fullness.
- [ ] Channel selection and changes, with no cross-channel data contamination.
- [ ] Existing sequence, stop, reset and loopback regressions after RTL changes.

## Acceptance before the sequencer integration test

- [ ] Freeze the selected source revision, synthesis setting, tool versions and
  nextpnr flags. Produce at least three passing fresh builds at seed 16 and
  compare seeds 1, 2 and 3 for sensitivity. Retain every result, including failures.
- [ ] Every acceptance build completes synthesis, placement, routing, timing
  checks and bitstream packing. Routed LVDS Fmax is at least 64 MHz, system Fmax
  at least 62.5 MHz, and every other constrained clock passes. Record the worst
  observed margin and any variation rather than selecting only the best run.
- [ ] Behavioral checks pass. Record logic/global-buffer use and remaining
  capacity; RAM is already fully used at 16/16, so this test must not add RAM.
- [ ] Perform a separate physical RX/TX smoke test of the selected candidate
  on both modem channels, including sync behavior and repeated reset/start/stop.
  Record the programmed image hash and observations against the working main
  baseline. Simulation and timing closure do not count as physical acceptance.
- [ ] Save a baseline report, selected build artifacts and rollback image before
  adding sequencer logic. CE2 is ready for adoption only if its own selected
  configuration meets these criteria.

## Following test: sequencer integration

After the LVDS baseline is accepted, start a separate integration experiment:

1. Recover and review the saved PMOD prototype, its pin/register mapping and
   timing contract against the selected baseline.
2. Add the single-bit PMOD TX request first, then the sequencer, measuring each
   increment with the same tools, constraints and repeatability checks.
3. Re-run RX/TX regressions, sequencer transition/reset/failure tests and routed
   timing for both clock domains. Compare resource use and worst timing margin
   with the accepted LVDS-only baseline.
4. Follow the physical ordering and failure checks in
   [roadmap milestone D](../ROADMAP.md#d-fpga-antennapttpa-sequencer-item-6).
   Keep the existing external-PA validation checkpoint in that milestone.

Do not label the sequencer integrated based solely on the earlier isolated
prototype placement successes. Integration has its own behavioral, timing and
physical acceptance results.
