# Software baseline — 2026-09-13

Captured at 13:33:32 UTC before addressing the September 12 code review.
This is an inventory of the existing setup, not a new RX/TX validation.
All versions, hashes, installed-file statements and reset instructions below
describe that dated snapshot. They do not identify the current build or loaded
driver. Use the [documentation index](README.md) and
[current radio controls](radio-control.md) for present operation.

- Source: branch `dev`, commit `1b75a92a5d3ac49715bf722d56a8519a0b1abd1f`.
  Working tree was clean at capture.
- OS: Debian 13.7 (trixie), Raspberry Pi kernel
  `6.18.39+rpt-rpi-v8`, aarch64.
- `smi_stream_dev` was loaded. Its sysfs source-version identifier and the
  installed module's identifier both read `C1DF6DFBA631E5496ACB191`.
  This supports correspondence but is not a byte-for-byte hash of loaded memory.
- The installed and local build copies of `libcariboulite.so` are identical.
- The local test application dynamically depends on `libcariboulite.so` and
  has `/home/pi/src/cariboulite/build` in its RUNPATH.
- `firmware/top.bin` is 32,220 bytes. Both generated firmware headers contain
  exactly this payload. The same complete byte sequence is present in the
  local and installed shared library and installed Soapy module.
  The test executable uses the shared library rather than containing this image.

## Reference hashes (SHA-256)

| File | SHA-256 |
| --- | --- |
| `firmware/top.bin` | `fe894f203f8c2fed4861ddac22890cbdc7d748ccf7fb6f1ac32bb950decc048d` |
| `/usr/local/lib/libcariboulite.so` | `4068a9377ad21446b04fce4391dbbe6db5d1d3960d52bbac7c56b962eb640e86` |
| Current kernel's installed `smi_stream_dev.ko.xz` | `c5e0f8ae12525a5cbe8232a322190b60bc458add938d1b05c39bba5b6490360a` |
| Reference archive | `dc61a54fcf9824c153f09dbe3ddfeed50493bdd0b650b03fdfd833c8e7a0a2f9` |

## Preserved local files

The snapshot is in
`installations/baselines/2026-09-13T133332Z/` under the repository root.
`installations` is already ignored by Git; these binary backups remain local
and are outside the build directories. Copy this folder elsewhere if an
independent backup is needed.

- `reference-files.tar.gz`: 48 files, including installed libraries, utility,
  headers, all located installed SMI module versions, local build artifacts,
  FPGA images and generated headers, and relevant boot/module/udev configuration.
  Archive paths preserve original absolute paths without the leading slash.
- `manifest.json`: original paths, sizes, SHA-256 hashes and symlink targets.
- `environment.json`: source/submodule state, OS/kernel, compiler/CMake versions,
  loaded modules, driver metadata, and library cache. Attempts to read active
  driver parameters returned permission denied; those failures are recorded.
- `firmware-comparison.json`: byte comparisons described above.
- `application-linkage.txt`: test application's ELF dynamic dependencies.
- `SHA256SUMS`: integrity checks for the snapshot files.

Every archived file was read back and checked against its manifest hash.
To check the snapshot container files, run `sha256sum -c SHA256SUMS` from
the snapshot directory. Extract into a separate directory for inspection;
restoring installed files is a separate operation.

## Development FPGA setup (reported by Jan)

On first boot, the FPGA is loaded with the original bitstream. To use the
`dev` branch, Jan selects option 3 in `cariboulite_test_app` to load
the latest development FPGA bitstream. One of the user LEDs illuminates as
a recognizable indication of that image.

For subsequent hardware validation, perform this setup and confirm the LED
indication before judging development RX/TX behavior. This procedure and
indicator are reported by Jan; they were not exercised during baseline capture.
The FPGA image active at capture remains unverified. The on-disk firmware
hash above does not identify the original boot image or prove the active image.

## Manual application checks (reported by Jan)

Jan tests from an SSH terminal, launching from the repository root:

```sh
build/cariboulite_test_app 2> debug.log
```

This redirects standard error to `debug.log`, replacing the previous log on
each launch. Preserve a relevant log before starting another test session.
After loading the development FPGA image through option 3 as described above,
the usual checks are:

- Option 11: check NBFM TX operation.
- Option 14: inspect modem registers and additional instrumentation.

These are Jan's established manual checks, not tests performed during
this baseline capture. Use this workflow when planning validation of fixes,
with additional checks specific to each issue.

## Verification limits

No application was launched, driver reloaded, FPGA programmed, or RF test run.
The FPGA's currently programmed image was not read back. The hashes establish
the software and firmware available on disk, not present hardware behavior or
proof that existing binaries were built from this exact source revision.
The snapshot preserves relevant artifacts, not a complete OS image or all
third-party runtime dependencies.
Active driver parameter values could not be read with the current permissions;
the saved modprobe configuration records configured values only.
