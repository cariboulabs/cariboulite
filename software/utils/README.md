# Binary blob generator

`generate_bin_blob.c` converts a binary file into a C/C++ header containing a
`uint8_t` array and generation-time `struct tm` metadata. Installation uses it
for the SMI kernel-module blob; the device-tree and FPGA generation scripts
also use it for embedded resources. Generation does not program any hardware.

From the repository root:

```sh
cmake -S software/utils -B software/utils/build
cmake --build software/utils/build
./software/utils/build/generate_bin_blob input_file.bin variable_name output_file.h
```

The output directory must already exist. The output contains an include guard,
C++ `extern "C"` guards, `<stdint.h>`/`<time.h>` includes, a
`variable_name_date_time` timestamp and the `variable_name` byte array. It
contains definitions rather than `extern` declarations, so include it in only
one translation unit. Its timestamp records generation time, not a source
version or reproducible build identifier.

The [top-level installer](../../install.sh) moves the built executable to
`software/utils/generate_bin_blob`, which is the path expected by the driver
and device-tree scripts. For those scripts, prepare that path after building:

```sh
cp software/utils/build/generate_bin_blob software/utils/generate_bin_blob
```

The utility's CMake install rule uses the absolute destination `/usr/lib`;
`cmake --install` is not needed for the local scripts above.
