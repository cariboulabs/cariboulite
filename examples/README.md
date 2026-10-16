# CaribouLite examples

Each C/C++ example has its own CMake project. The top-level installer does not
build these examples. Native API examples use an installed `cariboulite`
pkg-config file, headers and library; build/install the
[native library](../software/libcariboulite/README.md) first.

| Directory | Contents and dependencies |
| --- | --- |
| `c_api` | Board detection, library initialization and channel frequency ranges through the C API. |
| `cpp_api/sync_rx_api` | Synchronous C++ sample reads. |
| `cpp_api/async_rx_api` | C++ receive callbacks. |
| `cpp_api/async_sample_process` | Callback-based capture and sample processing with a circular buffer; links pthreads. |
| `cpp_api/sync_tx_api` | C++ sample transmission; frequency, power and waveform are set in source. |
| `adsb_soapy` | `caribou_dump1090`, an ADS-B receiver using SoapySDR and `iir_static`. |
| `gr_lora_receiver` | Despite its historical name, the current source demonstrates C++ receive callbacks; its CMake project also requires `libsigmf`. It does not implement a LoRa decoder or GNU Radio flowgraph. |
| `python` | SoapySDR capture, spectrum, calibration and signal-generation scripts; dependencies vary by script. |

From the repository root, build one native example without running it:

```sh
pkg-config --modversion cariboulite
cmake -S examples/cpp_api/sync_rx_api -B build/examples/sync_rx_api
cmake --build build/examples/sync_rx_api --parallel 2
```

The native example projects create `test_app` in their individual build
directories, except `gr_lora_receiver`, which creates `cariboulite_util`.
With a custom installation prefix, make its `lib/pkgconfig` directory visible
to `pkg-config` and its library directory visible to the compiler/linker and
runtime loader.

Inspect each source's frequency, channel, sample rate and audio/output settings
before launching it. `sync_tx_api` and Python generators transmit when run;
they are not build smoke tests. They also use their own streaming paths and
are not the standalone backend's microphone/NBFM pipeline. Use
[radio controls](../docs/radio-control.md) for that application.

Python scripts import SoapySDR and NumPy; several also import Matplotlib,
PySimpleGUI/Tk or SciPy. There is no pinned Python environment or tested package
matrix in this directory. Existing GUI code and RF defaults need review against
the installed dependencies and attached board before use.

These are source examples, not a validated end-to-end application suite.
Dependency/version checks and hardware RX/TX results remain tracked in
[DOC-07](../ROADMAP.md#documentation-validation-backlog).

# License
<a rel="license" href="http://creativecommons.org/licenses/by/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by/4.0/">Creative Commons Attribution 4.0 International License</a>.
