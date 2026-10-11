# Standalone passive monitor

This development branch adds a separate terminal monitor and an independent
C RX backend using a local Unix-domain socket. The existing `test_app`, menu 14,
radio controls, DSP, hardware polling, scheduling and build configuration
remain unchanged.
Their current behavior and performance are the baseline.

The monitor is a Python 3 client with no radio-library dependency. It receives
status snapshots and displays them; it sends no commands and never opens SPI,
SMI, ALSA or radio devices. Closing it only closes its socket connection.

The radio-owning backend uses C and the existing radio library and RX DSP.
Python remains suitable for this prototype and can also serve as the final
passive monitor if measured CPU/memory cost is acceptable.
The status protocol is independent of language, so a native C/C++ monitor can
replace this client later. CPU overhead relative to the baseline has not yet
been profiled.

The backend runs **instead of `test_app`** and owns the hardware and receive
pipeline. It automatically starts RX using launch settings, demodulates NBFM
or mono WBFM, plays audio, and publishes status. This first backend has no TX
or interactive radio controls; restart it to change launch settings. The
unchanged `test_app` does not publish socket telemetry. The included Python
demo publisher remains available for explicitly simulated data.

## Build the independent RX backend

Run from the repository root:

```sh
make -C software/libcariboulite/tools/radio_backend
```

This creates `build/radio-backend/cariboulite_rx_backend`. The separate build
reuses the existing `build/libcariboulite.so` and compiles the unchanged RX
pipeline, workers, DSP and ALSA adapter into the new executable. It does not
modify the existing CMake configuration or rebuild or replace `test_app`.
For the backend only, a generated compilation copy of the demodulator worker
expands the diagnostic label `aud_fifo` to `audio_fifo`; its processing and
rate controller are unchanged. Rebuilding replaces the backend executable
atomically and does not restart a running instance. Label changes appear
after the next backend restart.
The existing radio library build and C/ALSA development dependencies must be
available before building the backend.

## Receive with a monitor in another terminal

Close `test_app` and any other radio-owning application first. Use the same
user for backend and monitor, or pass the same accessible `--socket PATH` to
both. The backend requires the same hardware permissions as `test_app`.

In terminal 1:

```sh
build/radio-backend/cariboulite_rx_backend \
  --channel RF24 --frequency 430125000 --sample-rate 1000000 \
  --mode nbfm --playback plughw:Loopback,0,0
```

In terminal 2:

```sh
python3 software/libcariboulite/tools/cariboulite_monitor.py
```

These launch settings select HiF/RF24 at 430.125 MHz and 1 MS/s. The playback
device matches menu 14's ALSA loopback output; select another playback device
with `--playback DEVICE` if needed. The existing NBFM channel filter,
discriminator, AGC profile and squelch remain in use. WBFM is selected with
`--mode wbfm`; choose the station frequency with `--frequency HZ`.

Press Ctrl+C in the backend terminal to stop RX, join workers and release the
radio. Pressing `q` in the monitor closes only that monitor; reception and
audio continue. Monitor connections do not trigger additional hardware reads
and cannot control the radio.

Launch options:

| Option | Meaning/default |
| --- | --- |
| `--channel RF24\|RF09` | Active radio; default `RF24`. |
| `--frequency HZ` | Receive frequency; default `430125000`. |
| `--sample-rate HZ` | `1000000`, `2000000` or `4000000`; default `1000000`. |
| `--mode nbfm\|wbfm` | Demodulation; default `nbfm`. WBFM audio is mono. |
| `--playback DEVICE` | ALSA device; default `plughw:Loopback,0,0`. |
| `--socket PATH` | Status socket; default path described below. |
| `--seconds N` | Stop automatically after the requested duration. |
| `--status-hz HZ` | Hardware/status update frequency; default `5`. |
| `--no-noise-squelch` | Bypass NBFM noise squelch, enabled by default. |
| `--carrier-squelch` | Enable carrier squelch, disabled by default. |
| `--noise-open RMS` | NBFM opening threshold; default `0.20`. |
| `--noise-close RMS` | NBFM closing threshold; default `0.30`. |
| `--gain VALUE` | PCM gain; default `8000`. |
| `--deemphasis-us VALUE` | Audio deemphasis time; default `50` microseconds. |
| `--dry-run` | Simulated status/IPC exercise; no hardware or audio access. |

For a hardware-free check of the compiled backend, start it with
`--dry-run --seconds 30`, then connect the monitor in another terminal. The
source is explicitly labelled **SIMULATED DATA**. This verifies the compiled
publisher and monitor connection, not physical reception or DSP performance.

## Modem status collection

The live backend collects 37 defined modem register bytes in six SPI reads,
using the existing block-read driver. Only the active radio is polled:

| Read | Contiguous range | Bytes |
| --- | --- | ---: |
| Shared RF | `RST` through `IQIFC1` (`0x0005`–`0x000B`) | 7 |
| Shared RF | `PN`, `VN` (`0x000D`–`0x000E`) | 2 |
| Active radio | `IRQM` through `PAC` (offsets `0x00`–`0x14`) | 21 |
| Active radio | `PADFE` (offset `0x16`) | 1 |
| Active radio | `PLL`, `PLLCF` (offsets `0x21`–`0x22`) | 2 |
| Active radio | `TXCI` through `TXDACQ` (offsets `0x25`–`0x28`) | 4 |

The active-radio base is `0x0100` for RF09 or `0x0200` for RF24. Reserved gaps
are excluded. Reading the TX configuration bytes here does not enable TX.
Each register value is cached for publication; all monitors share the same
snapshot. Values can change during the reads, so the snapshot is not a
globally atomic hardware sample.

`IRQS` and `IQIFC2` are left unavailable. The existing modem interrupt handler
owns read-to-clear interrupt status and supplies no monitor cache, and reading
`IQIFC2.SYNC` also clears it. The backend avoids consuming these fields for
display. Fewer SPI transactions are a design benefit; actual CPU cost and
reception behavior still require comparison with the accepted baseline.

## Try the two-terminal demo

Run these commands from the repository root, using the same user in both
terminals. Python's standard library is sufficient; there is no build or
installation step.

In terminal 1, start the simulated source:

```sh
python3 software/libcariboulite/tools/monitor_demo_source.py
```

In terminal 2, start the monitor:

```sh
python3 software/libcariboulite/tools/cariboulite_monitor.py
```

The display labels these readings **SIMULATED DATA**. Press `q` to close the
monitor, use Up/Down or Page Up/Page Down to scroll vertically, Left/Right to
scroll wide rows horizontally, and press Ctrl+C in the demo-source terminal
to stop the source. The monitor can start before its
source, and reconnects when a source becomes available again.

The source defaults to a simulated RF24 radio. Use `--channel RF09` on the
demo-source command to try the RF09 layout; this changes simulated data only.

The default monitor view is compact for testing in a 57-row terminal: six
common registers followed by eleven RX/status registers for the selected
radio. To display the complete register inventory, use:

```sh
python3 software/libcariboulite/tools/cariboulite_monitor.py --registers all
```

`--registers rx` selects the compact view explicitly. The option changes the
display only; socket snapshots can still carry the full register inventory.

The default socket is `/tmp/cariboulite-monitor-<effective UID>/status.sock`.
All publishers and the monitor accept `--socket PATH` for an alternative.
The demo requires a private directory owned by its user, creates its directory with mode `0700`
and its socket with mode `0600`, and refuses to replace any existing path.
After an interrupted demo leaves a socket behind, choose another path or
remove the old socket after confirming its publisher has exited.

For a terminal-independent check:

```sh
python3 software/libcariboulite/tools/cariboulite_monitor.py --once --timeout 5
```

`--once` prints one valid JSON snapshot and exits, returning a failure if none
arrives before the timeout. `--json` streams JSON; `--plain` streams readable
status. Neither mode sends requests to the publisher.

## Display and protocol

The client displays the source identity, RX/TX state, frequencies, sample
rate, bandwidths, application RF queue counters, squelch/noise status, RSSI,
selected RF registers, FPGA status and loopback
status when supplied. Shared RF registers appear once, followed by only the
selected RF09 or RF24 radio. Each register has one row containing its name,
raw hexadecimal byte and decoded fields on that same row. Register order in
the compact view follows the requested RX/status layout; fields within each
register follow datasheet order from the highest bit to the lowest. For example:

```text
RF24 AGCC=0x01 AGCI=0(FILT) AVGS=0(8) RST=0 FRZS=0 FRZC=0 EN=1
RF24 AGCS=0x15 TGT=0(-21dBFS) GCW=21/21(MAX)
```

The raw byte is retained even when some bits are reserved. Reserved selections
are labelled, and fields are not decoded from missing or invalid bytes.
Snapshot and cached hardware ages are
separate; readings older than one second are marked stale.

The compact view contains these 17 rows, in this order:

| Block | Registers |
| --- | --- |
| Common (6) | `CFG`, `CLKO`, `RST`, `IQIFC0`, `IQIFC1`, `IQIFC2` |
| Active RF09 or RF24 (11) | `RSSI`, `RXDFE`, `RXBWC`, `AGCC`, `AGCS`, `PAC`, `PLL`, `PLLCF`, `IRQM`, `IRQS`, `STATE` |

The full view's 39 register rows cover the defined shared and active-radio register map
in the [AT86RF215 datasheet](<datasheet/Atmel-42415-WIRELESS-AT86RF215_Datasheet 2.pdf>),
sections 4–6 and 8:

| Block | Registers |
| --- | --- |
| Shared RF (10) | `RST`, `CFG`, `CLKO`, `BMDVC`, `XOC`, `IQIFC0`, `IQIFC1`, `IQIFC2`, `PN`, `VN` |
| Active RF09 or RF24 (29) | `IRQS`, `IRQM`, `AUXS`, `STATE`, `CMD`, `CS`, `CCF0L`, `CCF0H`, `CNL`, `CNM`, `RXBWC`, `RXDFE`, `AGCC`, `AGCS`, `RSSI`, `EDC`, `EDD`, `EDV`, `RNDV`, `TXCUTC`, `TXDFE`, `PAC`, `PADFE`, `PLL`, `PLLCF`, `TXCI`, `TXCQ`, `TXDACI`, `TXDACQ` |

Reserved addresses and the baseband packet-modem (`BBCn_*`) registers are
excluded. Section 5.1.1 states that accesses to the respective baseband
registers have no effect in I/Q radio mode, including its phase-measurement
unit and counters.

**Reading `IRQS` or `IQIFC2.SYNC` clears those hardware values.** A publisher
must reuse the radio owner's cached observations of these fields, or leave
them unavailable, rather than add observer polling. The live backend leaves
them unavailable; simulated examples may supply explicitly simulated cached
observations. `STATE` reports transceiver state;
`CMD` and `RST` are command readbacks. `RNDV` is an ancillary raw value whose
RNG validity is not established by merely reading the byte. Displaying it
does not require changing the receiver's bandwidth.

Queue drops refer to the application's queue counters. Queue timeouts are
wait events, and an empty queue alone does not establish a dropped frame.
The client does not infer kernel/DMA loss or ALSA underruns from these values.

The transport is `AF_UNIX` / `SOCK_SEQPACKET`, with one UTF-8 JSON object per
packet and an 8192-byte maximum. Version 1 requires:

| Field | Meaning |
| --- | --- |
| `version` | Integer `1`. |
| `session` | Nonempty identifier, changed when the publisher starts a new session. |
| `sequence` | Nonnegative integer, increasing within a session. Gaps are allowed. |
| `monotonic_s` | Snapshot time from the local host's monotonic clock, in seconds. |

Optional sections are `config`, `rx`, `tx`, `hardware`, `loopback` and
`publisher`. Optional `source` identifies the origin of the readings, as a
string or an object with `kind` and `label`. Without it the display reports
an unknown source, rather than assuming live radio data. Optional
`hardware_monotonic_s` dates the cached hardware sample; `realtime_s` dates
the snapshot in wall-clock seconds. The demo source is the complete example
of the current display fields.

Within `rx.queue` and `tx.queue`, the fields are `capacity`, `depth`, `puts`,
`gets`, `drops`, `timeouts_put`, `timeouts_get`, `min_depth` and `max_depth`.
Counter rates reset when the session changes or counters decrease.

Registers are supplied in `hardware.common.registers` and the active
`hardware.rf09.registers` or `hardware.rf24.registers` map. Keys are short
names such as `AGCC`, or full names such as `RF24_AGCC`; values are integer
bytes from 0 through 255, or `null` for unavailable readings. For example:

```json
{"hardware":{"common":{"registers":{"IQIFC1":18}},"rf24":{"registers":{"AGCC":1,"AGCS":21,"RXBWC":16}}}}
```

This is an optional fragment of a version 1 snapshot; the mandatory metadata
still applies. If a register group contains a `valid` field, it must be
boolean `true` for its bytes to be displayed. Without that field, new maps
are validated byte by byte. Missing/invalid entries never become zero and
cannot suppress other valid entries. Legacy flat register fields remain
supported with group-wide `valid: true`. AGC target is decoded as
`-21 - 3 * TGT` dBFS, distinct from RSSI in dBm; the maximum gain code is
derived from the active radio's `RXBWC` only.

The client rejects malformed, oversized, incompatible and out-of-order
packets. It drains a bounded number of queued packets per display update to
catch up to the newest status, and retains the last snapshot with its age
when disconnected. No IQ or PCM data travels through this protocol.

## Baseline and validation

All runtime files in the existing radio app remain identical to the branch
baseline. The backend reuses their source without changing it and adds its
own lifecycle and status publisher. The user has confirmed good audio and
normal squelch with both applications running. RF sensitivity and CPU cost
still need comparison with the accepted baseline.

The backend is the sole hardware owner, identifies live versus simulated
measurements and publishes bounded snapshots. Radio control through another
terminal remains a later step. No socket publisher or other integration has
been added to `test_app`.

Hardware-free client checks:

```sh
python3 software/libcariboulite/tests/test_passive_monitor.py
```

Hardware-free backend checks (mock SPI, Unix sockets and the actual monitor):

```sh
make -C software/libcariboulite/tools/radio_backend test
```

The tests cover both radio register maps, partial SPI failures, exclusion of
read-to-clear addresses, bounded clients, slow readers, permissions, cleanup,
CLI validation, monitor quit/reconnect independence and transport-only mode.

On 2026-10-11, a 12-second live RF24/NBFM run at 430.125 MHz and 1 MS/s
produced 60 live snapshots and confirmed the RX modem profile: `STATE=5`,
`RXBWC=0x10`, `RXDFE=0x04`, AGC enabled. The application RF queue recorded
1202 puts and gets by shutdown, zero drops and zero wait timeouts; the audio queue
reported a zero `drops` field. That audio-queue field is not maintained by
the shared queue implementation and cannot establish that no audio enqueue
loss or ALSA underrun occurred. SPI reported zero failures. The six-block register scan
averaged 0.323 ms and peaked at 1.200 ms, including lock time. Shutdown
published `STOPPED`, removed the owned socket and released the radio.
This was a short transport/lifecycle check; it does not establish long-term
sample continuity, audio quality or a CPU improvement. Artifacts are in
`build/radio-backend/live-check/`.

Later on 2026-10-11, the user started the backend and passive monitor in
separate terminals and reported good-quality received audio with squelch
operating normally. This validates the combined live setup in a listening
test. The report did not include measured CPU load or RF sensitivity results.

The user also observed the audio FIFO slowly filling and then holding at
42–50%. The existing worker deliberately targets 50% fill to balance the
radio sample clock and audio playback clock, with gradual resampling-rate
correction. Its 24 slots each contain 10 ms of audio, so 10–12 occupied slots
correspond to about 100–120 ms queued before ALSA buffering. Each slot changes
the displayed fill by roughly four percentage points. This steady range is
consistent with the controller's intended operation. Squelch continues to
produce timed silence while closed, so the queue remains active.
