# Documentation review — 2026-10-16

Source inspected: `dev-passive-monitor` at `953c1f0`, including hardware-paced
TX merged at `37d2414`, plus the documentation changes from this review.
The [documentation index](README.md) links current guides and dated evidence.
The [September audit](documentation-audit-2026-09-16.md) remains a historical record.

## Coverage and method

Reviewed all 66 existing first-party Markdown documents, the two prose README
files and both Doxygen prose pages. Added a GNU Radio module README, a
[test guide](testing.md) and this audit: **73 first-party documentation files**
in total. Compared installation/build instructions, API contracts, defaults,
CLI commands and feature status with installers, CMake/Makefiles, headers,
application/tool sources, driver code, RTL and existing validation reports.

Upstream IIR READMEs were inspected as dependency documentation; the submodule
was not edited. Datasheets, schematic/assembly PDFs, spreadsheet/BOM/PnP data,
Gerbers, drill files, images and recorded measurements remain reference
artifacts. They were not rewritten to describe software changes. YAML/build
metadata was inspected where it determines documented build behavior.

## Corrections

| Area | Updated documentation and findings |
| --- | --- |
| Overview/navigation | Root README, documentation index, wiki and roadmap now identify the current branch, standalone applications and merged hardware-paced TX. Completed local control/squelch/transport work is separated from open decoder, PA-sequencer and RF-qualification work. |
| Installation | Retired the blanket Bookworm failure/Bullseye recommendation and obsolete upstream-only clone. Documented recursive development checkout, running-kernel headers, installer Git/system changes, boot-file selection and text-only configuration checks. Soapy detection is based on `SoapySDRUtil`, not a separate SoapyRemote check. |
| Build/components | Updated library, utility, device-tree, programming, EEPROM, Soapy and examples guides. Distinguished the separate backend Makefile from CMake and system install targets. Added all native C++ examples; the directory named `gr_lora_receiver` currently contains a receive-callback example, not LoRa decoding. |
| GNU Radio | Added the actual module guide and replaced inherited README/Conda/Doxygen placeholders. Documented the Python namespace, conditional bindings, receive-only block, example version requirements and incomplete packaging. |
| RX/DSP | Corrected 1/2/4 MS/s support, current filtering/discrimination/reset behavior, squelch defaults, nonblocking ALSA capture and the 64-frame RX queue. Updated interface/extension guides and frozen-fixture comparison limits. |
| TX/runtime | Documented four-frame hardware-paced buffering versus the 64-frame legacy fallback, independent microphone capture, clock adaptation, driver requirements and shutdown behavior. Separated merged source from historical staged deployments and original queue-growth observations. |
| Driver/FPGA | Corrected header installation and cyclic-DMA descriptions; documented exclusive ownership and the TX telemetry/admission ABI. Kept the current firmware baseline, forced TX-enable behavior, simulation coverage and planned LVDS/sequencer work explicit. |
| Historical evidence | Retained dates, source identities and original measured values. Added current-status notes where old plans or findings could be mistaken for current behavior. Missing raw `build/` artifact links now retain labeled literal paths and availability notes. |
| Hardware/manufacturing | Linked the quotation package from older handoffs, corrected the preliminary stack-up interpretation and highlighted blind-via fabrication holds. Marked sourcing and budget reports as dated snapshots. Fixed the manufacturer's support ZIP/checksum drift described below. |

Guides that remained accurate after source comparison were retained, including
the menu-frequency guide, NBFM channel-filter/discriminator reports, scheduled
IQ procedure, initial FM comparison baseline and attribution-only components.

## Verification performed

- Repository-wide local Markdown link destinations, heading anchors and code
  fences checked; `git diff --check` passed.
- Backend Makefile test target passed: four C suites and **77 Python tests**
  (12 backend, 13 RX control, 28 controller, 13 TX control, 11 FPGA control).
  The passive-monitor suite passed **59 tests**, for **136 Python tests**
  across these application suites. Tests used dry-run backends or mocked local
  peers. Unix-socket tests required execution outside the restricted sandbox.
- `test_nbfm_rate.py`, `test_tx_audio_buffer.py` and
  `test_tx_hardware_pacing.py` passed. These check 1/2/4 MS/s modulation,
  independent capture/clock adaptation under both compiler configurations and
  real TX worker logic against mocked transport/capture.
- All five driver regression scripts passed: mocked installation (13 cases),
  exclusive open (100 concurrent rounds), stream transitions, TX demand and
  quiet framing. Firmware build-dependency checks and all four documented
  Icarus sequence/stop/reset/loopback benches passed.
- Blob generator and all five native C/C++ example projects configured and
  compiled in temporary directories against the installed native library.
  A small binary-to-header fixture passed; radio examples were not executed.
- The documented direct offline FM demo built and ran at 1/2/4 MS/s, producing
  successful status and approximately 600 Hz recovered audio. Tool help and
  hardware-free dry-runs for physical-baseline, scheduled IQ and RX comparison
  commands passed.

These are selected checks supporting the corrected documentation, not a fresh
run of every script listed in [testing.md](testing.md). No system installer,
driver reload, FPGA programming, EEPROM write or RF transmission was performed.

## Manufacturing documentation integrity

The initial `sha256sum -c SHA256SUMS` check found three pre-existing failures:
the package README, technical review and changelog had been corrected after
their archived/checksummed versions were generated. Refreshed the manifest and
support ZIP to include those existing corrections and the new changelog entry.

The final checksum check passes **52 entries**. All **53 support-ZIP entries**
match the local package and pass ZIP integrity checking. Only `README.md`,
`technical-review.md`, `CHANGELOG.md` and `SHA256SUMS` differ from the previous
archive. All **34 source-manifest entries** and **31 copied originals** match
the repository sources. PCB upload ZIP, BOM/CPL, procurement data and validation
outputs are byte-for-byte unchanged. No manufacturing release was made.

## Remaining limits and follow-up

The [roadmap documentation backlog](../ROADMAP.md#documentation-validation-backlog)
retains fresh-install/platform/permissions checks, kernel maintenance, incomplete
FPGA registers, PMOD/LVDS electrical specifications, EEPROM programming,
calibration, complete application workflows, historical board-failure evidence,
missing reference assets and Pi 5 feasibility.

The recorded Zero 2 W/Trixie install, Pi 4 recursive build and TX transport/
listening tests remain evidence for those specific setups. They do not establish
clean installation on every listed platform, calibrated RF performance or
unbounded-duration operation. GNU Radio is not installed on this host, so its
native module build/import and end-to-end flowgraphs remain unvalidated.

Raw test artifacts named under ignored `build/` directories were not available
here; committed reports preserve their recorded summaries rather than freshly
verified raw evidence. Supplier stock, prices, lifecycle and external websites
were not revalidated; the sourcing reports explicitly retain their September
snapshot dates. Quotation compatibility, preflight and procurement release
remain open. Existing unrelated IIR-submodule and Python-bytecode changes were
left untouched.
