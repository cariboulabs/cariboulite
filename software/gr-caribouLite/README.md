# Native GNU Radio receive source

This out-of-tree module provides `caribouLiteSource`, a GNU Radio source backed
by the native CaribouLite C++ API. It outputs complex float samples and optional
byte metadata. There is no transmit sink block. This module is separate from
the [SoapySDR adapter](../libcariboulite/src/soapy_api/README.md).

The [CMake project](CMakeLists.txt) requires GNU Radio 3.10 or newer and finds
an installed native `cariboulite` library through pkg-config. Python/GRC support
is conditional on `ENABLE_PYTHON`; Python bindings use GNU Radio's pybind build
helpers. Build the native library first, then from the repository root:

```sh
pkg-config --modversion cariboulite
cmake -S software/gr-caribouLite -B build/gr-caribouLite -DENABLE_PYTHON=ON
cmake --build build/gr-caribouLite --parallel 2
```

The top-level CaribouLite installer does not build or install this module.
After inspecting its configured prefix, install separately if required:

```sh
sudo cmake --install build/gr-caribouLite
sudo ldconfig
```

The configured GNU Radio Python, library and GRC paths must be visible in the
same environment. The installed import namespace is:

```python
from gnuradio import caribouLite
assert hasattr(caribouLite, "caribouLiteSource")
```

This import check does not open the radio. The attribute check matters because
`__init__.py` suppresses a missing binding-extension import. Constructing the
source initializes the board, configures the selected radio and starts RX.
Its parameters are channel (`0` = S1G, `1` = HiF), AGC, RX gain, RX bandwidth,
sample rate, frequency, metadata output and PMOD state; see
[the header](include/gnuradio/caribouLite/caribouLiteSource.h) and
[GRC block definition](grc/caribouLite_caribouLiteSource.block.yml).

Review the selected channel and frequency together before creating a source;
the inherited defaults select S1G at 900 MHz and 4 MS/s. Connect both outputs
when metadata is enabled; the work function
accesses the second output in that mode. Close other applications that own the
CaribouLite before running a flowgraph.

[Examples](examples/README) include a GRC file and a separately built C++
flowgraph. The [Conda recipe](.conda/README.md) is an incomplete packaging
template. GNU Radio/Python compatibility, installation paths and complete RF
workflows still require validation; see
[DOC-07](../../ROADMAP.md#documentation-validation-backlog). A successful native
library or Soapy test does not establish that this module works end to end.
