# Tests and validation

Run commands from the repository root. These commands describe the tests in
`dev-passive-monitor` at `953c1f0` (reviewed 2026-10-16).

## Prerequisites

Use Python 3, a C/C++ compiler, Make, CMake, pthreads, ncurses and ALSA development
libraries. Initialize the pinned IIR submodule and build the library and test
application in `build/` using the [manual build guide](../software/libcariboulite/README.md).
Several pipeline/startup tests link to `build/libcariboulite.so` or launch
`build/cariboulite_test_app` with mocked hardware calls. Build the standalone
applications before running their tests:

```sh
make -C software/libcariboulite/tools/radio_backend
```

The complete suite also needs SoapySDR development headers/library for
`test_soapy_stream.py`, and Icarus Verilog (`iverilog` and `vvp`) for the driver's
quiet-framing check. The core library can build without SoapySDR, but this
adapter test cannot. Tests assume a Linux host with Unix sockets, pseudo
terminals and a writable temporary directory.

## Hardware-free regression checks

For the focused backend/controller suite:

```sh
PYTHONDONTWRITEBYTECODE=1 make -C software/libcariboulite/tools/radio_backend test
```

For all Python regression scripts in the library and driver, plus the firmware
build-dependency test, use this single entry point after the prerequisites above:

```sh
(
  set -eu
  export PYTHONDONTWRITEBYTECODE=1
  for test in software/libcariboulite/tests/test_*.py driver/tests/test_*.py firmware/tests/test_build.py; do
    echo "Running $test"
    python3 "$test"
  done
)
```

The scripts compile C/C++ harnesses where needed and use simulated backends,
mocked system/radio calls or ALSA's null device. The driver installer test uses
mock build/system commands. They do not install a module, program an FPGA or
transmit RF. Read each script's result: a skipped case is not a pass, and a
missing build/dependency should be resolved before calling the full suite complete.

Coverage includes audio contracts, FM discrimination/interpolation, channel
filtering, squelch, partial SMI I/O, worker lifecycle, TX demand pacing, independent
capture/clock adaptation, tail draining, command validation and monitor parsing.
The frozen [NBFM modulator](../software/libcariboulite/tests/fixtures/legacy_nbfm_mod/README.md)
and [demodulator fixtures](../software/libcariboulite/tests/fixtures/legacy_nbfm_demod/README.md)
have specific comparison limits; current NBFM output is not claimed to be
byte-identical to every historical implementation.

Additional RTL sequence/reset/loopback simulations and their exact commands are
in [FPGA simulation checks](../firmware/tests/README.md). A passing behavioral
simulation does not establish physical LVDS timing or complete clock-domain coverage.

## Physical and RF validation

The following tools and procedures access hardware; they are separate from the
regression commands above:

- [Physical RX baseline procedure](physical-baseline-test.md).
- [Menu/backend RX comparison procedure](rx-application-comparison.md).
- [Pi Zero 2 W TX baseline and hardware-paced follow-up](pi-zero-tx-queue-test-2026-10-15.md).
- [Pi 4 TX qualification and RX restoration record](pi4-tx-validation-2026-10-15.md).
- [Future LVDS timing test](lvds-fmax-next-test-2026-10-08.md).

These reports retain the original source, firmware, board, OS, duration and
measurement conditions. Raw `build/` artifacts named in them were local to the
original test hosts and are not part of the checkout. Recorded transport and
listening results do not establish calibrated RF power, spectrum or sensitivity.
