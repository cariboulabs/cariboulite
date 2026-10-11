# Separate receiver control

`cariboulite_radio_control` is a C terminal client for the independent C RX
backend. It controls reception through a Unix-domain command socket. The
backend remains the sole owner of the radio, SPI, SMI, DSP workers and audio
device. The [passive monitor](passive-monitor.md) uses its separate status
socket and remains optional. This addition does not change `test_app`.

The initial controls are RX start/stop, frequency, NBFM or mono WBFM, sample
rate, and squelch. Radio channel and playback device remain backend launch
settings. TX, stereo decoding, RDS and adjustable carrier RSSI thresholds are
outside this implementation.

## Build and start

Run from the repository root:

```sh
make -C software/libcariboulite/tools/radio_backend
```

This builds `build/radio-backend/cariboulite_rx_backend` and
`build/radio-backend/cariboulite_radio_control`, using the existing
`build/libcariboulite.so` and C/ALSA development dependencies. The controller
links only its command protocol and system libraries; it does not initialize
the radio library or subscribe to periodic monitoring. While idle, it waits
for terminal or socket events.

Close other radio-owning applications first. Run all clients and the backend
as the same user. In terminal 1, initialize the backend with RX stopped:

```sh
build/radio-backend/cariboulite_rx_backend \
  --channel RF24 --frequency 430125000 --sample-rate 1000000 \
  --mode nbfm --playback plughw:Loopback,0,0 --start-stopped
```

In terminal 2, open the controller:

```sh
build/radio-backend/cariboulite_radio_control
```

The existing monitor can run in an optional third terminal:

```sh
python3 software/libcariboulite/tools/cariboulite_monitor.py
```

Omitting `--start-stopped` preserves the backend's automatic RX startup.
`--seconds N` limits the backend's wall time after initial setup, including
time spent with RX stopped; it is not accumulated reception time. Ctrl+C in
the backend stops reception and releases its hardware and sockets.

## Use the controller

Type a command and press Enter. The controller shows the backend's accepted
settings after each command, including requested and actual frequency,
effective squelch settings, and configuration and pipeline generations.

| Command | Meaning |
| --- | --- |
| `status` or `get` | Request current settings; also reconnect after a lost connection. |
| `start` or `rx start` | Start reception with the saved configuration. |
| `stop` or `rx stop` | Stop reception and release the RX pipeline. |
| `frequency 430.125` | Set receive frequency in MHz; `freq` is an alias. |
| `mode nbfm` or `mode wbfm` | Select NBFM or mono WBFM. |
| `rate 1`, `rate 2` or `rate 4` | Select RF sample rate in MS/s; `sample-rate` is an alias. |
| `noise on` or `noise off` | Enable or bypass NBFM noise squelch. |
| `carrier on` or `carrier off` | Enable or bypass carrier squelch. |
| `levels 0.20 0.30` | Set noise opening and closing RMS thresholds together. |
| `help` or `?` | Show available commands. |
| `q` or `quit` | Disconnect the controller, retaining the receiver's current state. |

For example:

```text
stop
frequency 430.125
mode nbfm
rate 1
levels 0.20 0.30
noise on
carrier off
start
status
q
```

Frequency, mode and sample-rate commands require stopped RX. A stopped
frequency change tunes the radio and verifies that it remains idle. Mode and
sample-rate changes update saved settings; their DSP and hardware profiles
are applied at the next `start`. Every restart creates a fresh pipeline, so
old IQ or PCM frames cannot carry across a retune or mode change.

Squelch controls work during reception and while stopped. Noise thresholds
use 0.001 RMS resolution and must satisfy `0 < open < close <= 8.0` after
rounding. A pair that collapses to the same rounded value is rejected.
Noise thresholds are independent of audio volume.

WBFM has no noise-squelch detector: its effective noise flag is off and
`noise on` returns `WBFM_NOISE`. Switching to WBFM preserves the saved NBFM
noise-enable preference; switching back restores it. Carrier squelch remains
available in both modes. Its existing fixed thresholds open at −97 dBm and
close at −102 dBm, using modem RSSI rather than calibrated connector power.

Closing the controller with `q`, EOF or Ctrl+C only disconnects that client.
RX continues if it was running. Closing the monitor likewise leaves RX
unchanged. The backend accepts one controller at a time; another receives
`BUSY`. Passive monitor connections do not reserve the controller slot.

A single command can also be issued from the shell:

```sh
build/radio-backend/cariboulite_radio_control --command 'status'
build/radio-backend/cariboulite_radio_control --command 'stop'
```

These commands connect, execute once and disconnect. Controller options are
`--socket PATH`, `--timeout SECONDS` and `--command 'COMMAND'`. The default
reply timeout is 2 seconds; the accepted range is greater than 0 through 60.

## Validation and receiver state

The backend validates the command before changing accepted configuration.
Frequency limits depend on the selected radio and detected board:

| Receive path | Frequency range |
| --- | --- |
| RF09/S1G | 377–530 MHz or 779–1020 MHz, including endpoints. |
| RF24/HiF on a full board | 1 MHz through frequencies below 6 GHz. |
| RF24 on an ISM board | 2385–2495 MHz, including endpoints. |

The reply reports both the requested frequency and the driver's actual tuned
frequency. Unsupported rates, malformed values, nonfinite numbers and
out-of-range settings are rejected. An invalid command retains the accepted
configuration and does not restart the pipeline.

Before acknowledging `start`, the backend checks pipeline setup/tuning,
modem RX state, the SMI streaming operation, positive reader sample progress
and all three RX workers. A failed start releases the pipeline and reports
`HARDWARE` with state `FAULT`. A failed frequency change attempts to restore
the accepted frequency and confirmed idle hardware. A successful rollback
retains `IDLE` and still reports the failed command; a failed rollback reports
`FAULT`. Hardware stop failures are also reported explicitly.

In `FAULT`, `status` remains available and `stop` attempts checked cleanup.
After `stop` successfully returns `IDLE`, settings can be changed and RX
started again. The accepted frequency, mode, rate and squelch configuration
remain available after a failed start or tune.

`config_generation` advances when accepted configuration or requested RX
state changes, and on a fault-state transition. `get`, unchanged settings
while idle, and repeated successful `start`/`stop` commands leave it
unchanged. Frequency/mode/rate commands while RX is running are rejected,
including requests for the existing value.

`pipeline_generation` advances when a new pipeline is successfully
initialized. It also advances if initialization succeeds but subsequent
start verification fails. Stopping does not advance it. FIFO, reader and
playback counters belong to that pipeline generation; after stop the
pipeline is destroyed and these measurements are unavailable until a new
pipeline is initialized. Monitor comparisons should not subtract counters
across pipeline generations or backend sessions.

If a command was sent but its reply times out or fails validation, its outcome
is uncertain. The client disconnects and never automatically retries a
mutation. Reconnect with `status` to inspect the current settings before
deciding on another command. A later explicit command may reconnect and
fetch settings; merely waiting does not retry or change RX.

## Sockets and command protocol

Default socket paths are:

```text
/tmp/cariboulite-monitor-<effective UID>/status.sock
/tmp/cariboulite-monitor-<effective UID>/control.sock
```

The backend's `--socket PATH` selects its status socket. Unless overridden
with `--control-socket PATH`, its command socket is named `control.sock` in
that same directory. Set the controller's `--socket` to the command path and
the monitor's `--socket` to the status path when using custom locations.

Both transports require a private directory owned by the backend user with
mode `0700`, create sockets with mode `0600`, and check same-UID peer
credentials. Existing socket paths are refused rather than removed. Cleanup
removes only the socket inode created by that backend instance. Status and
command sockets use separate `AF_UNIX`/`SOCK_SEQPACKET` connections; the
status channel does not accept radio commands.

Command packets are strict version-1 ASCII, at most 1024 bytes. Each request
uses a nonzero unsigned 64-bit ID, which its reply echoes. Whitespace
separates fields; embedded NULs, truncated/oversized packets, extra tokens
and invalid numeric encodings are rejected. Formatters append a newline but
do not transmit a terminating NUL. Wire frequencies and sample rates use
Hz, unlike the terminal's MHz and MS/s input units.

```text
v1 ID get
v1 ID start
v1 ID stop
v1 ID frequency HZ
v1 ID mode nbfm|wbfm
v1 ID sample_rate 1000000|2000000|4000000
v1 ID squelch NOISE CARRIER OPEN_RMS CLOSE_RMS
```

`NOISE` and `CARRIER` are `0` or `1`. A squelch request carries both flags
and both thresholds. For a partial terminal edit such as `carrier on`, the
controller first requests current settings and preserves the other fields.

Each reply has 17 fields in this order:

```text
v1 ID RESULT CODE CONFIG_GENERATION PIPELINE_GENERATION SOURCE STATE CHANNEL REQUESTED_HZ ACTUAL_HZ RATE_HZ MODE NOISE CARRIER OPEN_RMS CLOSE_RMS
```

`RESULT` is `OK` or `ERROR`; `SOURCE` is `live` or `simulated`; `STATE` is
`IDLE`, `RX` or `FAULT`; `CHANNEL` is `RF09` or `RF24`; `MODE` is `nbfm`
or `wbfm`. Even an error reply includes the current effective settings.
Transport-generated errors use ID `0` when no valid request ID is available;
the unsolicited competing-controller `BUSY` reply also uses ID `0`.

| Code | Meaning |
| --- | --- |
| `OK` | Command accepted, including an idempotent start/stop. |
| `BUSY` | Another controller owns the command connection. |
| `PROTOCOL` | Malformed or unsupported packet. |
| `RX_RUNNING` | Stop RX before changing frequency, mode or sample rate. |
| `INVALID` | Invalid supported-control value or threshold pair. |
| `FREQ_RANGE` | Frequency outside the active board/channel range. |
| `HARDWARE` | Tuning, pipeline, verification or control application failed. |
| `FAULT` | Recover the existing fault with `stop` before making changes. |
| `WBFM_NOISE` | Noise squelch was requested in WBFM. |

## Hardware-free validation

Use a dry-run backend with the same controller and optional monitor:

```sh
build/radio-backend/cariboulite_rx_backend \
  --dry-run --start-stopped --seconds 60
```

Dry-run opens no radio or audio devices. Controller replies are explicitly
`simulated`; `RX` in a simulated reply means requested test intent, not
physical reception. Status snapshots retain `state: "DRY_RUN"`,
`rx.running: false`, unavailable worker/pipeline measurements and unavailable
register values. Their optional `control` section reports requested state
and generations. Dry-run simulates a new pipeline generation for each
stopped-to-started transition without creating a real pipeline.

Run the test target after building:

```sh
make -C software/libcariboulite/tools/radio_backend test
python3 software/libcariboulite/tests/test_passive_monitor.py
```

The tests exercise protocol bounds, command-server ownership and cleanup,
real backend/controller/monitor connections using dry-run, and controller
disconnect and uncertain-reply handling. Mocked live-handler tests cover
tune rollback, failed startup and stop, reader/worker verification, SMI
errors and preservation of accepted settings. These checks do not establish
physical reception quality or a new CPU performance baseline.

The completed validation passed four C test suites and 81 Python tests:
10 existing backend checks, 12 receiver-control backend checks, 14 controller
checks and 45 passive-monitor checks.

A production-binary dry-run check held the interactive controller idle for
8.000 seconds. Its Linux user/system CPU accounting remained at zero ticks
at 10 ms resolution. A second actual controller received a clear `BUSY`
message, and quitting the first released control without changing receiver
settings. Both owned processes and sockets cleaned up normally. This measures
idle client behavior, not physical RX CPU usage. Results are in
[the idle/controller check report](../build/radio-backend/control-idle-check-20261011/report.json).

## Physical control check (2026-10-11)

The rebuilt backend, actual C controller and actual Python monitor completed
28 controller commands across five fresh RF24 RX sessions. NBFM was exercised
at 1/2/4 MS/s near 430.125 MHz, and mono WBFM at 2 MS/s at 100.1 MHz.
The check covered frequency changes while stopped, rejection of retuning
while receiving, live noise/carrier flag and threshold edits, rejection of
noise squelch in WBFM, mode/rate changes, restart and controller disconnect.

The monitor stayed connected across every transition and recorded 170
snapshots, including 142 while RX was active. Application IQ/audio queue
drops, ALSA underruns/write errors and reader errors remained zero in those
snapshots. Telemetry identified pipeline generations 1–5 and only `IDLE`,
`RX` and the final `STOPPED` state. Every one-shot controller disconnected
after its command; reception continued between commands. Final backend
shutdown returned zero and removed both owned sockets.

These were short control and pipeline-health checks, without IQ recording or
TX. They do not establish reception quality, upstream sample continuity,
long-term FIFO stability or a new CPU baseline. The applications used for the
check have been stopped.

The receiver controller was subsequently confirmed working successfully in
the user's normal setup.

Artifacts are in
[`build/radio-backend/control-live-check-20261011/`](../build/radio-backend/control-live-check-20261011/):
[summary](../build/radio-backend/control-live-check-20261011/summary.json),
[command replies](../build/radio-backend/control-live-check-20261011/commands.json),
[monitor snapshots](../build/radio-backend/control-live-check-20261011/monitor.jsonl),
[backend log](../build/radio-backend/control-live-check-20261011/backend.log)
and [source/binary metadata](../build/radio-backend/control-live-check-20261011/metadata.json).
