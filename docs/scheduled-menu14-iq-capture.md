# Scheduled IQ recording through menu 14

The [scheduler](../software/libcariboulite/tools/scheduled_menu14_iq.py) launches
`build/cariboulite_test_app`, selects menu 14, saves the requested RX
frequency, selects 1/2/4 MS/s, and sends **R** at the scheduled start and stop
times. It exits through **Q**, then main-menu **99**, releasing the radio.

The [capture hook](../software/libcariboulite/tools/menu14_iq_capture.c) records
the samples returned by `cariboulite_radio_read_samples` before software
decimation, FM demodulation and squelch. The scheduler builds it as a shared
library and loads it only into the launched test application. The existing
application and its production RX code do not need rebuilding.

Menu 14 now uses **HiF/RF24**. The scheduler defaults to that expected channel,
checks the menu banner before starting RX, and records the observed and captured
channel in new session metadata. On the full board, HiF accepts frequencies from
**1 MHz up to, but not including, 6000 MHz**. The application validates its board's
actual range; an ISM-only board supports HiF only in its native 2.4 GHz band.
The hook accepts either radio, locks each recording to its first channel, and
reports an error if a different or unexpected channel supplies samples.

## Confirmed session

Requested on 2026-10-10:

- RX frequency: **430.125 MHz**, RF09/S1G.
- Sample rate: **1 MS/s**.
- Start: **2026-10-10 14:59:00 Europe/Brussels**.
- Stop: **2026-10-10 15:01:00 Europe/Brussels**.

This historical recording used the earlier **RF09/S1G** menu configuration.
The user explicitly corrected the original stop time of 14:01 to 15:01 and
closed the previously running test application before this session was started.

From the repository root, the executed command is:

```sh
python3 software/libcariboulite/tools/scheduled_menu14_iq.py \
  --start 2026-10-10T14:59:00 \
  --stop 2026-10-10T15:01:00 \
  --frequency-mhz 430.125 \
  --sample-rate 1000000
```

For another session, choose future start and stop times. Naive ISO timestamps
use Europe/Brussels; timestamps with explicit UTC offsets are also accepted.
Use `--dry-run` to inspect the plan without accessing hardware. An existing
output directory or IQ file is refused. The test application needs access to
the radio device nodes and its existing ALSA Loopback audio devices.
For an older build configured for S1G, add `--radio s1g`; that option checks the
expected channel and does not switch the application's channel. The confirmed
historical capture and its metadata are preserved.

## Output and format

The default output directory for the confirmed session is
`build/iq-captures/20261010T145900+0200/`. It contains:

- `rx_iq.cs16`: raw binary, **little-endian signed int16 I, then int16 Q**,
  repeated for every complex sample. Each pair occupies four bytes. Native
  13-bit amplitudes are preserved in the 16-bit containers, approximately
  −4096 through +4095; there is no expansion to 16-bit full scale.
- `metadata.json`: requested frequency/rate, scheduled and observed action
  times, application and hook hashes, source revision, sample count and status.
  New recordings also include the expected, observed and captured radio channel.
- `application.log`: terminal output, driver diagnostics and capture markers.

The confirmed session completed normally. The start key was sent at
**14:59:00.000480**, RX start was confirmed at **14:59:00.027814**, and the stop
key was sent at **15:01:00.001345**, all Europe/Brussels. The file contains
**118,320,000 complex IQ pairs**, or **473,280,000 bytes** (**118.32 seconds**
of samples at the configured rate). The capture hook reported `failed=0`, and
the application exited with status 0. SMI read timeouts were logged; their
presence does not by itself establish where samples were lost. Stream
continuity was not verified.

Post-run checks are stored in `verification.json` beside the recording. They
include a SHA256 checksum, complete-pair validation and sample checks at four
positions; those checked values were nonzero and within the native 13-bit range.

A two-minute recording at 1 MS/s is nominally **480,000,000 bytes**. Start/stop
are software key events; application polling, radio activation, initial stream
discard and reader shutdown affect the exact number of samples. Metadata
records observed action times and the actual sample count. These timestamps are
not hardware sample timestamps.

To load the IQ file with NumPy:

```python
import numpy as np

pairs = np.fromfile("rx_iq.cs16", dtype="<i2").reshape(-1, 2)
iq = pairs[:, 0].astype(np.float32) + 1j * pairs[:, 1].astype(np.float32)
# Sample rate is 1_000_000 Hz; nominal centre frequency is 430_125_000 Hz.
```

## Completion and limitations

Successful completion requires normal application exit, a nonempty IQ file
containing complete I/Q pairs, and an
`IQ_CAPTURE_COMPLETE samples=... bytes=... failed=0` marker without capture
errors. New recordings must also contain one `IQ_CAPTURE_CHANNEL` marker for
the expected radio. The scheduler writes `status: complete` only after these checks.
Failed sessions retain logs, metadata and any partial recording.

The tap writes synchronously in the RX reader, so disk stalls can affect stream
continuity. It preserves samples returned by the existing library but does not
provide complete FPGA/kernel gap detection. Squelch does not change recorded IQ;
menu defaults remain in effect for audio playback. Hardware bandwidth and gain
settings are those of the existing menu 14 configuration.
