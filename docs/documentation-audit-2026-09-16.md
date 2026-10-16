# README validation — 2026-09-16

Historical audit. The [2026-10-16 documentation review](documentation-audit-2026-10-16.md)
records the current branch, subsequent corrections and remaining validation gaps.
The findings below describe the September source revision.

Source revision inspected: `9df1d7c`, plus the working documentation edits.
This is a source/documentation audit, not a fresh installation or RF test.
No installation script, EEPROM write, FPGA programming or transmitter was run.

## Scope and method

Inventoried 27 Markdown READMEs outside `.git`, `build` and `installations`,
including the IIR submodule's three upstream READMEs. Searched for TODO, TBD,
WIP, “coming soon” and equivalent future-work statements, then checked local
instructions against CMake files, installers, headers, menu code and RTL.
The vendored IIR documentation and generated GNU Radio packaging material were
inventoried; their upstream/platform claims were not exhaustively revalidated.

The policy is now in [ROADMAP.md](../ROADMAP.md#documentation-validation-backlog):
fill gaps from evidence, or leave a source-linked, actionable task.

## Resolved or clarified

| Documentation | Finding and disposition | Evidence |
| --- | --- | --- |
| Additional notes | Kernel patches already applied; driver-first build order obsolete; branch checkout unnecessary | `driver/smi_stream_dev.c`, root `install.sh` |
| Additional notes | Menu 14 defaults to ALSA capture, not the documented 650 Hz tone; firmware path depends on working directory | `src/app_menu.c`, `SOURCE`, `txpar`, `rxpar` |
| Additional notes | Fixed malformed headings/commands, retired stale toolchain recipes and nightly download instructions; separated historical captures from current behavior | Firmware Makefile, source inspection, upstream references below |
| Root README | Corrected installer invocation, boot-file behavior, driver source path, PIGPIO/sudo claims and broken local references | Installers, `io_utils/CMakeLists.txt`, `io_utils.c`, `rpi/rpi.c` |
| Driver README | Filled installation and udev TBDs with actual artifacts, defaults and limitations | `driver/install.sh`, `driver/udev/` |
| Userspace SMI README | Replaced removed callback API and writing TBD with current lifecycle, timed I/O and partial-write contract | `caribou_smi.h` |
| Firmware README | Corrected impossible SPI latency claim; documented missing error/FM/LDO/PMOD behavior, actual SYS/SMI opcodes and clocking | `sys_ctrl.v`, `io_ctrl.v`, `smi_ctrl.v`, `top.v` |
| Software/library/examples READMEs | Replaced “examples coming soon”; fixed directory inventory and required build dependencies | Directory inventory and CMake files |
| Hardware/EEPROM READMEs | Added available implementation references and distinguished unfinished electrical/calibration/programming work | `hat/`, `cariboulite_production.c`, top-level CMake and RTL |
| SMI overview | Corrected interface name, modem-to-FPGA transport and obsolete kernel build path | RTL and current driver tree |
| Empty Soapy/Python-binding READMEs | Added implementation map and packaging status | Soapy sources and GNU Radio binding tree |

## Deferred TODOs and validation

All encountered project TODO groups map to DOC-01 through DOC-10 in the roadmap.
This includes fresh OS setup, process permissions/limits, kernel packaging,
register gaps, PMOD/PPS/LVDS, EEPROM programming, calibration, application
examples, the historical board fault, missing references and Pi 5 feasibility.
Existing squelch/configuration/GNU Radio work remains linked to milestones C/E.

The manufacturing README remains a quotation/review document, not a released
manufacturing procedure; no supplier or manufacturing claims were certified.
The firmware simulation README describes limited coverage and was not promoted
to a physical-timing guarantee. Historical serial numbers, register dumps and
May 2025 observations remain historical records.

## External references checked

- [Raspberry Pi kernel documentation](https://www.raspberrypi.com/documentation/computers/linux_kernel.html): kernel/header installation reference.
- [nextpnr build instructions](https://github.com/YosysHQ/nextpnr/blob/main/README.md): iCE40 prerequisites and consistent build-directory flow.
- [SDR++ upstream installation instructions](https://github.com/AlexandreRouma/SDRPlusPlus#installing): artifact selection and local package installation; no particular old nightly asset was certified.

The systemd website could not be fetched during this pass. Consequently the
Trixie limit workaround remains explicitly unverified, with process/session
validation in DOC-01. Other external links and detailed electrical claims are
not fully audited; they remain DOC-04/DOC-06/DOC-09.

## Checks

Checked Markdown fences and local Markdown link destinations in 26 documents
(24 project READMEs, the roadmap and this audit), plus diff whitespace.
The three upstream IIR READMEs were excluded from the local-link/fence check.
Build commands were compared to the source/build definitions rather than executed;
no fresh-build, audio or hardware pass is claimed.
