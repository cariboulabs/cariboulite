# CaribouLite C/C++ and SoapySDR library

The build definition is [CMakeLists.txt](CMakeLists.txt). It requires C/C++
compilers, CMake 3.15 or newer, threads, ncurses and ALSA development files.
SoapySDR is detected optionally; without its development files the Soapy module
is skipped. The IIR source dependency must be present.

From the repository root, a userspace build is:

```sh
git submodule update --init --recursive
cmake -S software/libcariboulite -B build
cmake --build build --parallel 2
```

On Debian-family systems the dependency package names include `build-essential`,
`cmake`, `pkg-config`, `libncurses-dev`, `libasound2-dev`, and optionally
`libsoapysdr-dev`. The [installation guide](../../installation.md) describes
the system installer and recorded Pi Zero 2 W / Trixie installation; a CMake
userspace build alone does not validate a fresh OS setup.

The build creates `libcariboulite.so`, `libcariboulite_static.a`, the test
application, utilities, and (when detected) the Soapy module. It does not build
or install the kernel driver; see [driver/README.md](../../driver/README.md).

Installation is a separate operation:

```sh
sudo cmake --install build
sudo ldconfig
```

Inspect installation paths before using a custom prefix: some subdirectories
have absolute install destinations. The test application has no active install
rule; run `./build/cariboulite_test_app` from the repository root so its
`firmware/top.bin` programming path resolves correctly.

Software regression tests live in [tests](tests/); hardware diagnostics also
exist under [test](test/). Building successfully does not verify radio operation.

## Separate radio applications

After building the main library, build the backend, controller and monitor:

```sh
make -C software/libcariboulite/tools/radio_backend
```

Their executables are in `build/radio-backend/`. This is a separate Makefile
build; it is included by the top-level installer, but not by the library's
CMake targets. The backend owns the radio and audio pipelines, the C controller
uses a command socket, and the optional Python 3 monitor reads status.
See [radio controls](../../docs/radio-control.md) for launch options, half-duplex
NBFM TX, mono NBFM/WBFM RX, FPGA programming and reset controls.

The current TX producer follows downstream SMI/DMA demand, with bounded IQ
buffering, microphone clock correction and an optimized NBFM modulator.
It requires the matching current SMI driver, including its TX write timing.
The [TX design and validation record](../../docs/tx-hardware-paced-design.md)
links the Pi Zero 2 W and Pi 4 results at 1/2/4 MS/s. Existing installed drivers
and binaries are not updated by rebuilding userspace alone.

## Hardware-free NBFM example

From the repository root with the build configured:

```sh
cmake --build build --target nbfm_memory_demo -j2
./build/nbfm_memory_demo
python3 software/libcariboulite/tests/test_memory_audio.py
```

The demo connects memory-backed audio to the standalone modulator and demodulator
at 1, 2 and 4 MS/s, without opening audio or radio devices. The
[extension guide](../../docs/audio-dsp-extension-guide.md) also provides a direct
C11/libm build that does not require the application's ALSA dependencies.

## Mono WBFM in menu 14

Menu 14 defaults to NBFM reception. Stop TX, RX and interface loopback, then
press `[m]` to cycle between NBFM and mono WBFM. `[R]` starts reception;
`[G]` edits the saved RX frequency while stopped. Mode changes preserve the
frequency, sample rate and audio settings. TX remains NBFM. Other menu items
keep their existing behavior.

Press `[1]`, `[2]` or `[4]` while TX, RX and interface loopback are stopped to
select 1, 2 or 4 MS/s for both directions. The monitor defaults to 4 MS/s.

TX shutdown retains the 250 ms trailing Quindar tone and waits for accepted
final silence before disabling TX. The finite silence stage uses fast padding
to flush buffered IQ; it no longer adds a wall-clock sleep for every padding
frame. Stop deadlines remain bounded. See
[TX tail timing](../../docs/tx-tail-timing.md) and the
[current hardware-paced design](../../docs/tx-hardware-paced-design.md).

WBFM supports 1, 2 and 4 MS/s IQ input and 48 kHz mono output, with nominal
75 kHz deviation and the monitor's existing 50 microsecond de-emphasis.
It filters the RF channel before demodulation, removes the stereo multiplex
components before audio resampling, and shares buffering, clock correction,
volume and playback with NBFM. Noise squelch is bypassed in WBFM; `[C]` still
toggles carrier squelch. The saved NBFM noise-squelch setting is retained.
Stereo decoding and RDS are not implemented.

Hardware-free validation:

```sh
python3 software/libcariboulite/tests/test_wbfm_demod.py
python3 software/libcariboulite/tests/test_nbfm_demod.py
python3 software/libcariboulite/tests/test_rx_lifecycle.py
```

# License
<a rel="license" href="http://creativecommons.org/licenses/by/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by/4.0/">Creative Commons Attribution 4.0 International License</a>.
