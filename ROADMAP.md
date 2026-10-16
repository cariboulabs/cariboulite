# CaribouLite development roadmap

Recorded 2026-09-16 from Jan's backlog. This is a proposed implementation
order, not a claim that the features below are complete. Original item numbers
are retained for tracking. Update this document as work is verified.

## Current branch milestone — 2026-10-16

The accepted development work through FM refactoring step 4 is merged into
`main` at `7da1ee0`. New receive-mode development starts on
`dev-additional-modes`; the original `dev` branch is retained. NBFM TX/RX and
mono WBFM RX are implemented. CW listening, AM and SSB are next in the
[demodulator plan](docs/demodulator-architecture-research.md); they are not yet
implemented modes. The completed FM extraction preserves signal behavior,
legacy APIs, streaming guards and mode-specific squelch capabilities.

Current checkout: `dev-passive-monitor` at `953c1f0`. It includes the standalone
radio backend, local controller and passive monitor, live RX controls, half-duplex
NBFM TX, FPGA controls and hardware-paced TX merged at `37d2414`. The
[Pi Zero 2 W](docs/pi-zero-tx-queue-test-2026-10-15.md) and
[Pi 4](docs/pi4-tx-validation-2026-10-15.md) reports record the completed transport
and listening checks. Other receive modes, stereo/RDS, DAB+, configurable CTCSS,
network audio/PTT and the external-PA sequencer remain open. The
[2026-10-16 documentation audit](docs/documentation-audit-2026-10-16.md) records
this source review; it is not a new hardware qualification.

## Existing foundation

- Hardware modules already live under `software/libcariboulite/src/`:
  `at86rf215`, `rffc507x`, `caribou_fpga`, `caribou_smi`, `io_utils`, and `hat`.
  Kernel code lives in `driver/`; FPGA RTL and simulations live in `firmware/`.
- The eight-step audio/DSP extraction is complete: `app_menu.c` selects
  configuration and displays status; TX/RX pipelines own workers and transport.
  Standalone NBFM DSP uses explicit sample contracts with ALSA, tone and memory
  adapters. This does not complete the broader shared-radio-core milestone.
- Software regression tests exist in `software/libcariboulite/tests/`.
  FPGA checks and their coverage limits are documented in
  [firmware/tests/README.md](firmware/tests/README.md).
- `software/gr-caribouLite/lib/` contains a receive source implementation.
  The Soapy adapter is in `software/libcariboulite/src/soapy_api/`.
  GNU Radio TX and RX end-to-end validation remains a backlog item.

The audio/DSP work has an [incremental plan with physical checkpoints](docs/audio-dsp-refactor-plan.md)
and a dedicated [interface reference](docs/audio-dsp-interfaces.md). The
[extension guide](docs/audio-dsp-extension-guide.md) includes a hardware-free
round-trip example and the procedure for adding adapters or another modem.

## Intended module boundaries (items 1 and 2)

Keep the existing public API usable while extracting one responsibility at a
time. Define interfaces and build targets before moving entire directories.

| Layer | Responsibilities | Boundary |
| --- | --- | --- |
| Bus access | SPI transactions, GPIO access, SMI transport | Explicit ownership, timeouts, cancellation and error reporting |
| Hardware devices | Modem, mixer, FPGA, board RF switches and PMOD | Register operations and capabilities; no audio or UI dependencies |
| Radio control | Frequency planning, modem path, gains, bandwidth, RX/TX state | Validated configuration and one owner of hardware transitions |
| Sample streaming | RX/TX queues, sample conversion, pacing and shutdown | Document format, rate, timestamps if available, buffer lifetime, partial transfers and overflow/underflow |
| DSP | Modulators, demodulators, resampling and squelch | Hardware-independent processing of sample blocks with explicit state |
| Adapters | ALSA, network audio/PTT, SoapySDR, GNU Radio, possible Dire Wolf integration | Translate external data/control into shared radio and stream APIs |
| Applications | Diagnostic menu and user workflows | Select configuration and report state; delegate radio/DSP operations |

Separate modem path (`rf09`/`rf24`) from the board RF connector/mixer route.
Keep audio rate, modem IQ rate, processing rate and RF bandwidth distinct.
Configuration should carry units, defaults, supported ranges and relationships;
reject unsupported combinations before changing hardware. Retain named hardware
constants and justified DSP coefficients rather than making every literal a
user setting. Document thread ownership, stop deadlines and error recovery.

## Milestones and acceptance criteria

### A. Establish documentation and regression coverage (items 0 and 3)

- [ ] Rework [ADDITIONAL-README.md](ADDITIONAL-README.md) into a reproducible
  guide: prerequisites, build/install, FPGA selection, use, troubleshooting.
  Separate historical patches from current instructions and record the tested
  source revision, OS/kernel, Pi model and firmware for each verified recipe.
- [x] Inventory existing tests and provide a [documented entry point](docs/testing.md)
  for hardware-free checks, separate from physical/RF procedures.
- [ ] Add meaningful unit coverage for configuration validation, DSP vectors,
  squelch state and sequencing. Integration tests should exercise partial I/O,
  timeout, overflow/underflow, cancellation and repeated start/stop.
- [ ] Record an RX/TX hardware baseline and reproducible end-to-end procedure,
  including signal source/load, levels, expected output and pass criteria.

Exit: repeatable checks protect the behavior that will be moved during refactoring;
installation claims are tied to evidence. Hardware results are not inferred from
simulations or an existing binary.

### B. Extract a shared radio/stream/DSP core (items 1 and 2)

Progress: the audio/DSP extraction and reusable pipelines are implemented, and
both menu 14 and the standalone backend use them. The backend serializes local
commands and publishes cached status. Broader directory/build reorganization and
configuration across all public adapters remain open; see
[interfaces](docs/audio-dsp-interfaces.md) and [radio controls](docs/radio-control.md).

- [ ] Extract menu-owned streaming, radio control and audio handling behind the
  interfaces above, preserving current NBFM behavior and public entry points.
- [ ] Centralize runtime configuration and capability validation. Replace
  scattered rate, buffer and timeout assumptions with documented settings.
- [ ] Organize build targets and source directories along the module boundaries;
  update includes, installation rules, examples and documentation together.

Exit: the menu and an adapter can use the same control/stream API, DSP can be
exercised without hardware, and regression checks pass after each extraction.

### C. Squelch and a first VHF/UHF transceiver (items 4 and 5)

Implementation update: independent RX noise and RSSI squelch modules and menu-14
toggles are implemented and accepted by Jan after adjusting the carrier
thresholds to -97/-102 dBm. See
[RX squelch](docs/rx-squelch.md) for current thresholds and validation.
Menu 14 now also implements [separate TX/RX frequency controls](docs/monitor-frequency.md);
physical acceptance of this addition is pending.

- [x] Implement NBFM noise/RSSI squelch, threshold validation, hysteresis/hang
  behavior and live noise thresholds in menu 14 and the controller. Regression
  coverage and recorded listening acceptance are in [RX squelch](docs/rx-squelch.md).
- [ ] Complete quantitative weak-signal, opening-delay and muting acceptance,
  and define squelch behavior for later modes.
- [ ] Add menu item **15: VHF/UHF TRX** using the shared core. Offer `rf09` and
  `rf24`, frequency, IQ sample rate, RF bandwidth and mode selection, filtered
  by actual modem/board/DSP capabilities.
- [ ] Deliver NBFM RX/TX first, including audio selection, PTT, squelch, status,
  validation errors and reliable stop/return to the menu.
- [ ] Add SSB (USB/LSB), AM and CW with per-mode DSP tests and hardware checks.
- [ ] Add BPSK/QPSK after defining symbol rates, filtering, synchronization and
  whether the interface exposes symbols, bits or framed packets. Keep further
  modes extensible and expose only implemented modes as selectable.

Exit: supported configurations operate on each supported route, invalid choices
are explained, and mode/path changes leave the radio in a defined state.

### D. FPGA antenna/PTT/PA sequencer (item 6)

Next firmware development test, recorded 2026-10-08: establish repeatable LVDS
timing on the physically tested main firmware before the next sequencer
integration attempt. The [LVDS Fmax test plan](docs/lvds-fmax-next-test-2026-10-08.md)
records the default/CE2 comparison, DDR-pair staging proposal, behavioral and
timing acceptance criteria, and the following PMOD integration sequence.

- [ ] Complete and accept the LVDS baseline test before the next sequencer
  integration test.
- [ ] Specify board pin allocation, electrical polarity, external PA interface,
  PTT sources, timing constraints and host/FPGA responsibility before RTL work.
- [ ] Implement explicit RX, TX preparation, TX active, TX shutdown and fault
  states. Define antenna switching, PA enable and RF start/stop ordering from
  the actual hardware requirements, with configurable delays and TX inhibit.
- [ ] Define reset, host loss, stream starvation, conflicting PTT and emergency
  stop behavior. Add status/capability registers and firmware compatibility checks.
- [ ] Simulate transitions and failures, then measure physical timing with the
  external PA disabled before validating the complete chain.

Exit: measured ordering and failure behavior match the agreed timing specification.
This is required before enabling external-PA operation through any interface.

### E. Platform and external interfaces (items 7, 8 and 9)

- [ ] **Pi Zero:** distinguish original Zero/Zero W from Zero 2 W; record OS
  architecture and build compatibility per board. Measure CPU, RAM, sustained
  rates, dropped samples, audio latency and repeated PTT cycles. Publish supported
  configurations and duration/results of soak tests.
- [ ] **Ethernet audio/PTT:** define audio format, packet timing, jitter buffering,
  control ownership and session behavior. Support NBFM, SSB and AM through the
  same audio/control adapters. Test loss, reordering, disconnect/reconnect and
  PTT timeout; loss of the controlling session must release transmit.
- [ ] **GNU Radio:** evaluate the existing source and Soapy route before deciding
  whether a native sink is needed. Supply working RX and TX flowgraphs and test
  rate/frequency changes, stream errors, sustained operation and shutdown on hardware.
- [ ] **Dire Wolf (exploratory):** establish the desired audio/PTT interface and
  packet workflow before selecting an integration mechanism.

Exit: each supported platform/interface has a repeatable example and recorded
end-to-end results; receive-only success does not count as TX validation.

### F. Reconstruct the hardware as an editable KiCad project (item 10)

Added 2026-09-16. Re-create the schematics in KiCad and reconstruct the PCB
layout from the existing Gerber and drill files. Use the existing Rev2.8
reproduction work as the starting reference; confirm the exact revision and
Full/ISM variant against the source artifacts before reconstruction.

Sources: [schematic PDFs](hardware/rev2/schematics/),
[PCB manufacturing files](hardware/rev2/pcb/),
[BOM and placement data](hardware/rev2/assembly/), and
[reproduction context](cariboulite_rev2_8_reproduction_context.md).

- [ ] Inventory and freeze the reference artifacts, including revision, variant,
  units, layer order, coordinate origin, stack-up and drill spans. Record gaps
  and conflicting information before using it to define the KiCad project.
- [ ] Re-create schematic sheets, symbols, reference designators, component
  values, pin mappings and nets. Cross-check against the original PDFs and BOM;
  keep project-specific symbols and footprints with the project.
- [ ] Reconstruct an editable PCB with footprints, pads, tracks, copper zones,
  vias, board outline, cutouts, mask and silkscreen from the manufacturing data.
  Use placement data and schematics to recover component identity and net
  assignments; record ambiguities that the Gerbers alone cannot resolve.
- [ ] Reconcile schematic and PCB connectivity, footprint geometry, component
  positions/orientations, layer stack and through/blind via definitions. Preserve
  RF routing geometry and grounding details when reproducing the reference.
- [ ] Run electrical and design-rule checks, reviewing and documenting exceptions.
  Export Gerbers and drills from KiCad and compare them with the original files
  layer by layer, including dimensions, holes, copper clearances and connectivity.
- [ ] Deliver the KiCad project, local libraries, reconstruction notes and a
  comparison report identifying every unresolved discrepancy. Document the
  KiCad version and the procedure for regenerating manufacturing outputs.

Exit: the project is editable, schematic and PCB connectivity agree, and
manufacturing-output comparisons have no unexplained discrepancies. Keep the
original manufacturing artifacts as the reference; adoption of regenerated
outputs for a production run requires a separate manufacturing review.

### G. First on-air NBFM QSO (item 11)

A practical milestone: use CaribouLite to complete a two-way NBFM contact.

- [ ] Prepare a working NBFM RX/TX setup with audio routing, PTT and a suitable
  antenna path; verify transmit/receive switching and audio on the bench first.
- [ ] Complete an on-air QSO and confirm intelligible audio in both directions.
- [ ] Record the date, band/frequency, station setup, software/firmware revisions,
  signal/audio reports and any problems to feed back into the roadmap.

Exit: a completed two-way NBFM QSO using CaribouLite for both RX and TX.
Depends on the NBFM work in C and the switching requirements of the chosen setup.

### H. First QO-100 QSO using SSB, CW or PSK (item 12)

A further practical milestone: use CaribouLite in a complete station to make a
QO-100 contact using at least one of SSB, CW or PSK.

- [ ] Define the station architecture and CaribouLite's RX/TX roles, including
  the required external RF equipment, frequency reference, antenna system,
  monitoring and PTT/PA control.
- [ ] Implement and bench-validate the selected mode, frequency stability,
  transmit quality, receive chain and switching/sequencing for that setup.
- [ ] Check the current QO-100 operating guidance and band plan when preparing
  the station; record the selected mode and operating configuration.
- [ ] Complete a two-way QSO and record the station diagram, software/firmware
  revisions, mode, reports and any remaining improvements.

Exit: a completed QO-100 QSO using SSB, CW or PSK with CaribouLite's role
explicitly documented. Depends on the selected mode in C, the station design,
external-PA sequencing in D where used, and audio/data adapters as needed.

### I. Runtime profiling, broadcast decoding and adjustable CTCSS

Added 2026-10-10 from Jan's requested receiver/transceiver extensions. NBFM
and WBFM listening is reported free of obvious gaps or pops. The
[runtime review and decoder plan](docs/runtime-profiling-and-decoder-plan.md)
records implemented instrumentation and the remaining decoder work. The local
backend/controller/monitor and TX transport changes below are implemented;
stereo/RDS, DAB+ and adjustable CTCSS are not.

- [x] **Separate local control and instrumentation terminals.** Implement one
  radio-owning backend with serialized commands, cached bounded telemetry, a
  read-only monitor and a separate controller. Menu 14 remains an alternative
  frontend using the shared pipelines. See [radio controls](docs/radio-control.md)
  and [the monitor](docs/passive-monitor.md).
- [ ] Finish combined backend/client CPU and latency qualification across the
  planned recording and rate matrix, including slow-client/disconnect behavior.
  Existing dry-run and physical comparisons are recorded in the
  [runtime plan](docs/runtime-profiling-and-decoder-plan.md).
- [ ] Complete loss/recovery counters, actual scheduling reports, RX
  discontinuity metadata and worker-health reporting. Profile whole-app CPU,
  stage service/wait latency and queues at 1/2/4 MS/s, with recording off/on.
  Optimize measured contributors and preserve accepted FM behavior.
- [x] **Pi Zero 2 W TX queue growth and 4 MS/s compute margin.** Preserve the
  [pre-change baseline](docs/pi-zero-tx-queue-test-2026-10-15.md#physical-baseline-results-2026-10-14):
  1 MS/s filled the legacy 64-frame queue; 4 MS/s saturated CPU 0 and lost
  capture/transport continuity. The implemented
  [hardware-paced design](docs/tx-hardware-paced-design.md) uses bounded driver
  admission, a four-frame application queue, independent capture/adaptive
  resampling, separate worker cores and faster IQ conversion. The verified
  1/4/2 SMI write profile is applied for TX and restored before RX handoff.
  Both Zero 2 W and Pi 4 completed all six microphone/tone trials at 1/2/4 MS/s
  for 180 seconds each with approximately 100 frames/s, bounded queues and zero
  reported driver/capture/audio losses or faults. Same-backend RX→TX→RX passed
  at 1/4 MS/s. User listening checks reported improved 4 MS/s modulation.
  Full conditions and limitations remain in the
  [Zero report](docs/pi-zero-tx-queue-test-2026-10-15.md) and
  [Pi 4 report](docs/pi4-tx-validation-2026-10-15.md).
- [ ] **Complete TX RF and deployment qualification.** Measure physical SWE
  sampling, RF calibration and quantitative signal quality; extend soak and
  repeated PTT testing. The source is merged into this branch, while the cited
  physical trials used staged binaries/runtime driver reloads. A clean install
  and permanent deployment across the supported Pi/OS matrix remain distinct
  acceptance steps.
- [ ] Add adjustable CTCSS TX encoding and optional RX tone squelch, including
  explicit tone deviation, total ±2.5 kHz modulation budget, tone rejection
  from listening audio and compatible Quindar/PTT/tail timing.
- [ ] Add a WBFM multiplex interface, pilot recovery and true stereo DSP/audio
  transport with mono fallback. Measure separation, distortion and lock recovery.
- [ ] Add independent RDS extraction, synchronization/checkwords and station
  metadata, with block-error and reacquisition measurements.
- [ ] Establish DAB+ reception on the full-board HiF/RF24 path using an existing
  decoder first. Verify wideband IQ/filtering and 2/4 MS/s to 2.048 MS/s
  resampling, then choose a separate decoder/backend integration and profile it.

Exit: each enabled feature has repeatable signal-quality, continuity, CPU/latency
and physical-reception results; unsupported configurations fail explicitly.

## Suggested order

Start A, then B, then deliver C incrementally. Specify D early so the shared
control API accommodates sequencing. Start Pi testing after B and repeat as DSP
and adapters are added. Ethernet and GNU Radio integration can build on the same
core once stream/control ownership is stable. Maintain documentation and tests
through every milestone, rather than leaving them until the end. Hardware
reconstruction (F) can proceed alongside the software work and support the
revision-specific PMOD/LVDS and calibration documentation (DOC-04/DOC-06).

Decisions to resolve during design: exact Pi Zero models, physical antenna/PA
wiring, supported rate/bandwidth combinations, initial SSB/AM/CW requirements,
BPSK/QPSK data interface and network latency targets.

## Documentation validation backlog

Process: when a README TODO or an outdated instruction is encountered, complete
it using implementation or measured evidence where possible. Otherwise retain a
short status note at its source and add a linked task here. Do not treat source
inspection, successful compilation and hardware validation as interchangeable.
The [2026-09-16 audit](docs/documentation-audit-2026-09-16.md) records the initial
pass; the [2026-10-16 audit](docs/documentation-audit-2026-10-16.md) records the
current review and corrected status claims.

- [ ] **DOC-01 — Reproducible installation and permissions (milestone A).**
  Sources: [additional installation notes](ADDITIONAL-README.md),
  [root installation instructions](README.md#installation-troubleshooting),
  [driver installation](driver/README.md), and
  [library build](software/libcariboulite/README.md).
  Current installers use matching versioned kernel headers and discover the
  running-kernel module destination; the root installer builds the standalone
  applications and selects `/boot/firmware/config.txt` when present. Preserve the
  reported Zero 2 W/Trixie installation and Pi 4 recursive-build evidence, then
  validate clean installs across the claimed platform matrix. Fix presence-only
  boot-setting checks and verify udev reload/trigger behavior. Establish the actual
  need for memlock/RT limits per launch method, replace broad device permissions
  with the required access, and test unprivileged operation. Record commands,
  package/tool versions and Pi model. The original Zero and Zero 2 W must be
  distinguished; the existing 64-bit recipe cannot establish original-Zero support.
- [ ] **DOC-02 — Kernel maintenance and SMI generalization (A/B).**
  Sources: [SMI overview](docs/smi/README.md), [driver](driver/README.md).
  Decide between maintained out-of-tree packaging (including kernel-update
  handling) and an upstream submission effort. Define the proposed generic
  streaming API and bus timing requirements before promising general peripheral
  support. Validate loading, upgrade and rollback across supported kernels.
- [ ] **DOC-03 — Complete FPGA register contract (B/D).**
  Source: [firmware README](firmware/README.md).
  Decide whether to implement or retire `sys_error_status`, mixer FM registers,
  LDO control, generic PMOD direction/value control and external RX sync selection.
  Resolve mixer-enable readback versus disconnected top-level output and reserved
  bit/read behavior. Document reset states, clock domains and all opcode access
  semantics; add register-level regression coverage. The obsolete error/PMOD/FM
  claims were corrected in the README rather than presented as working features.
- [ ] **DOC-04 — PMOD and LVDS hardware specification (D).**
  Sources: [hardware PMOD/LVDS sections](hardware/README.md),
  [firmware README](firmware/README.md).
  Provide revision-specific connector pin numbering, electrical levels, maximum
  current, synchronization/GPS PPS behavior and peripheral examples. Complete
  LVDS timing/electrical calculations and physical measurements. Coordinate pin
  allocation with antenna/PTT/PA sequencing; RTL array indices are not a wiring guide.
- [ ] **DOC-05 — EEPROM programming guide (A).**
  Sources: [EEPROM reprogramming](docs/flashing/README.md#reprogramming-the-eeprom),
  [hardware EEPROM section](hardware/README.md).
  Establish a supported utility/build target; document backup, identity fields,
  write protection, programming, readback and restore. Verify the actual proprietary
  data/calibration layout against `hat` code and a board dump before documenting
  it as populated. Keep EEPROM and FPGA programming procedures distinct.
- [ ] **DOC-06 — Calibration and RF acceptance testing (A/E).**
  Sources: [hardware calibration section](hardware/README.md),
  [root specifications](README.md#specifications).
  Document fixtures, instruments, revision-specific procedures and limits for
  IQ balance, frequency reference, TX power, sensitivity/noise and route behavior.
  Separate simulated specifications from measured results with recorded conditions.
- [ ] **DOC-07 — Application recipes and examples (C/E).**
  Sources: [ALSA, SDR++ and GNU Radio notes](ADDITIONAL-README.md),
  [examples](examples/README.md), [Soapy adapter](software/libcariboulite/src/soapy_api/README.md),
  [GNU Radio conda recipe](software/gr-caribouLite/.conda/README.md).
  Reproduce ALSA loopback routing and overruns, SDR++ source/build/server setup,
  GNU Radio RX/TX flowgraphs, and each example's dependencies. Verify the native
  module's Python/GRC packaging and generated conda recipe on supported targets.
  Replace hardware card numbers with selectable devices during configuration work;
  supply the still-missing native TX integration or validate the Soapy alternative.
- [ ] **DOC-08 — Resolve historical board failure report (A).**
  Source: [additional troubleshooting notes](ADDITIONAL-README.md).
  Preserve the May 2025 logs, identify the board/modem variant and firmware,
  reproduce or close the reported sync/product-ID failures, and record the
  manufacturer's response if available. Do not diagnose board failure from
  a synchronization error alone.
- [ ] **DOC-09 — Missing reference material (A).**
  Source: [root README](README.md).
  Recover or replace the missing Secondary Memory Interface PDF, Rev1 source
  material and Rev2 shield 3D assets with verified references. Produce the promised
  SDR comparison only with dated, attributable specifications. Review remaining
  external links and hardware specification claims; a local link check does not
  validate external pages or embedded images.
- [ ] **DOC-10 — Pi 5 feasibility (scope decision).**
  Source: [root Pi 5 note](README.md#note-no-support-for-rpi-5).
  Decide whether the historical alternate-transport proposal belongs in scope.
  Identify actual connectors/interfaces and demonstrate a transport before
  claiming Pi 5 support. This is separate from the requested Pi Zero validation.
