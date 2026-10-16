# SMI stream driver

The current kernel module is `smi_stream_dev`, backed by `bcm2835_smi`.
Its source of truth is this directory. The headers under the userspace
`caribou_smi/kernel/` directory are generated/copied installation artifacts.

## Build and installation

Run the repository's `./install.sh` from the repository root as a normal user.
It builds `software/utils/generate_bin_blob` before invoking this directory's
installer. The top-level script also pulls Git changes and installs dependencies;
review it before using it on a development checkout.

For a driver-only installation after the blob generator and matching kernel
headers are available, run from the repository root:

```sh
./driver/install.sh install 6 2 3
```

The arguments are FIFO MTU multiplier, direction address offset and channel
address offset. The driver-only defaults are `16 2 3`; the top-level installer
chooses multiplier 6 or 2 based on available memory. These are script defaults,
not measured optimal settings for every Pi.

The installer clears `driver/build`, compiles against the running kernel's
`/lib/modules/$(uname -r)/build`, checks the module's vermagic, compresses the
module and generates the userspace blob/header copies. It installs into the
running kernel's existing `bcm2835_smi_dev` directory and runs `depmod`. It then
writes:

- `/etc/modprobe.d/blacklist-bcm_smi.conf`: blacklist `bcm2835_smi_dev`.
- `/etc/modules-load.d/smi_stream_mod.conf`: load `smi_stream_dev` at boot.
- `/etc/modprobe.d/smi_stream_mod_cariboulite.conf`: module parameters.
- `/etc/udev/rules.d/40-cariboulite.rules`: device permissions.

Rebuild after a kernel change or a driver source change. The installer reuses
`/lib/modules/$(uname -r)/build/Makefile` when available; otherwise it requests
`linux-headers-$(uname -r)`. It installs missing build/package tools as needed.
Header availability on other distributions still needs validation (roadmap
DOC-01). Installing a module does not replace an already loaded copy; reboot
before checking the installed setup.

```sh
uname -r
lsmod | grep smi
modinfo -F vermagic smi_stream_dev
ls -l /dev/smi
```

Expect `smi_stream_dev` and `bcm2835_smi`; module sizes and device major numbers
vary. These checks establish installation state, not successful sample streaming.

## Streaming and hardware-paced TX

The current source uses cyclic DMA for RX and TX: a 524,288-byte ring divided
into four 131,072-byte periods. One IQ pair occupies four bytes, so a period
holds 32,768 samples. `/dev/smi` allows one open file description at a time;
a competing open returns `EBUSY`. Passive monitoring uses the backend's status
socket instead of opening the device.

The fixed-width version-1 TX ABI in [smi_stream_dev.h](smi_stream_dev.h) adds:

- `SMI_STREAM_IOC_GET_TX_STATUS`: actual FIFO depth/capacity/admission limit,
  DMA period/ring sizes, session generation, accepted/dequeued bytes, completed
  periods and underrun periods. Counters reset at each new TX session. DMA
  retirement measures transport progress, not RF emission.
- `SMI_STREAM_IOC_SET_TX_BUFFER_LIMIT`: opt into bounded admission while idle.
  A nonzero limit must be a multiple of four bytes, at least one period and
  no greater than actual FIFO capacity. Zero restores legacy behavior.

With a nonzero limit, `POLLOUT` and writes honor the admission bound. Writes
accept whole IQ words, may be partial, and return `EAGAIN` when no capacity is
available. DMA startup and underrun shortages use valid zero-IQ framing
(`0x80000000` in native packed words). Zero IQ does not unkey the modem; stop
must explicitly disable transport and radio. Legacy callers retain allocated
FIFO admission, raw startup padding and the old underrun-repeat policy.

The shared TX pipeline uses two periods of kernel admission and four application
frames, with verified write timing 1/4/2 during TX and saved timing restored on
stop. The standalone backend requires the new ABI; the terminal test application
automatically falls back on an older driver returning `ENOTTY`. See the
[TX design and validation](../docs/tx-hardware-paced-design.md) and
[radio setup](../docs/radio-control.md) for application behavior.

For a runtime-only candidate test, first stop all radio-owning applications:

```sh
bash driver/tools/tx_driver_reload.sh --check /absolute/path/smi_stream_dev.ko
sudo bash driver/tools/tx_driver_reload.sh /absolute/path/smi_stream_dev.ko
```

The helper verifies kernel/module metadata, device ownership and an installed
rollback source, saves a verified backup next to the candidate, and validates
the new ABI while idle after reload. It leaves installed modules unchanged;
the printed rollback commands or a reboot restore the installed original.
`--check` checks metadata only and does not open the device or reload modules.

Hardware-free regression checks run from the repository root:

```sh
python3 driver/tests/test_install.py
python3 driver/tests/test_exclusive_open.py
python3 driver/tests/test_stream_transitions.py
python3 driver/tests/test_tx_demand.py
python3 driver/tests/test_tx_quiet_framing.py
```

The last check needs Icarus Verilog. These checks exercise mocked installer
operations, extracted driver functions and RTL framing; they do not load a
kernel module or establish physical timing.

## Udev rules

The exact rules are in [udev/40-cariboulite.rules](udev/40-cariboulite.rules).
They currently set mode `0666` for SMI, I2C, SPI, GPIO memory and raw memory
matches, with groups on some rules. This is broader than SMI-only access.
The helper [udev/install.sh](udev/install.sh) copies/removes the rules and reloads
udev. A narrower permission policy and unprivileged operation tests are tracked
as [DOC-01](../ROADMAP.md#documentation-validation-backlog).
