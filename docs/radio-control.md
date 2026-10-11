# Separate radio control

`cariboulite_radio_control` is a C terminal client for the independent C radio
backend. It controls RX and half-duplex NBFM TX through a Unix-domain command socket. The
backend remains the sole owner of the radio, SPI, SMI, DSP workers and audio
device. The [passive monitor](passive-monitor.md) uses its separate status
socket and remains optional. Socket controls stay separate from `test_app`;
the shared TX implementation adds parked worker waits and diagnostics to both
applications.

Receiver controls are RX start/stop, frequency, NBFM or mono WBFM, sample
rate, and squelch. Half-duplex NBFM TX controls add start/stop, a separate
frequency and sample rate, output power, and microphone or tone input. Radio
channel, playback and microphone devices remain backend launch settings.
Stereo decoding, RDS, CTCSS and adjustable carrier RSSI thresholds remain
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
| `tx start` or `tx stop` | Start or stop half-duplex NBFM transmission. |
| `tx frequency 430.125` | Set the separate TX frequency in MHz. |
| `tx power -12` | Set requested output power in integer dBm. |
| `tx rate 1`, `tx rate 2` or `tx rate 4` | Select TX sample rate in MS/s. |
| `tx source mic` or `tx source tone` | Select microphone input or synthesized audio. |
| `tx tone 600 0.4` | Set tone frequency in Hz and normalized audio amplitude. |
| `help` or `?` | Show available commands. |
| `q` or `quit` | Disconnect; RX continues, while active TX is stopped. |

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

Closing the controller with `q`, EOF or Ctrl+C leaves stable RX running. If
TX is active or starting, losing the owning controller requests TX shutdown
and restoration of prior RX where applicable. Closing the passive monitor
does not change RX or TX. The backend accepts one controller at a time;
another receives `BUSY`. Passive monitor connections do not reserve the
controller slot.

A single command can also be issued from the shell:

```sh
build/radio-backend/cariboulite_radio_control --command 'status'
build/radio-backend/cariboulite_radio_control --command 'stop'
```

These commands connect, execute once and disconnect. `--command 'tx start'`
is refused locally before connection: transmission requires an interactive
controller to remain connected. Controller options are
`--socket PATH`, `--timeout SECONDS` and `--command 'COMMAND'`. The default
reply timeout is 2 seconds; the accepted range is greater than 0 through 60.

## Half-duplex NBFM transmission

TX starts only after an explicit interactive `tx start` command. The backend
retains the RX settings and records whether reception was running, releases
the RX pipeline, and starts the existing NBFM transmitter. `tx stop` completes
the normal closing Quindar cue and bounded final drain, then restores RX only
if it was running before TX. Starting TX from `IDLE` returns to `IDLE`.
Normal backend shutdown also stops and unkeys TX.

Transitions execute on a lifecycle worker so the owner thread can continue
answering `status` and publishing monitor snapshots. `ACCEPTED` acknowledges
the request, while `TX_STARTING`, `TX_STOPPING` and `RX_STARTING` indicate work
in progress. Inspect `status` or the optional monitor for the completed `TX`,
`RX`, `IDLE` or `FAULT` state. A transition acknowledgement does not confirm
that the carrier is already on or off.

RX frequency, mode and sample-rate changes remain blocked during TX. TX
settings may be saved while RX is stable because they do not program the
hardware until TX starts; they are blocked during active TX and transitions.
RX and TX have separate frequency, rate and pipeline generations.

| TX setting | Default and accepted values |
| --- | --- |
| Frequency | 430.125 MHz; the same board/channel frequency limits as RX. |
| Sample rate | 1 MS/s; 1, 2 or 4 MS/s. |
| Power | −12 dBm; RF24 accepts −12 through +14 dBm, RF09 −14 through +14 dBm. |
| Source | Microphone; `mic` or `tone`. |
| Microphone device | `plughw:Loopback,1,1`, selected at backend launch. |
| Tone frequency | 600 Hz; greater than 0 through 3000 Hz. |
| Tone amplitude | 0.4; greater than 0 through 1.0. |
| Maximum deviation | Fixed at ±2.5 kHz. |

Backend launch options select the saved TX defaults: `--tx-frequency HZ`,
`--tx-power DBM`, `--tx-sample-rate HZ`, `--tx-source mic|tone` and
`--microphone DEVICE`. These do not start transmission. The backend rejects
TX when the modem's IQ loopback bit is set and rechecks it before keying.
Before sending opening-cue IQ, it restores the calibrated driver power
setting after activation and verifies PAC/current reduction, TXDFE, FPGA
sample gap and the armed modem state. Completion additionally requires
modem TX state and positive SMI writer progress. This check waits up to
500 ms after the opening cue, allowing temporary `TXPREP`/`TRANSITION`
readings while buffered IQ reaches the modem. Accepted SMI samples alone
do not establish that RF transmission has started.

Runtime monitoring also rechecks temporary `TXPREP`/`TRANSITION` readings
for up to 500 ms. It requires a return to `TX`; persistent non-TX state,
SPI read failures and worker errors stop transmission. Additional state
reads run only during that recovery window; the monitor publishing cadence
remains unchanged.

Power is a requested modem/driver setting; it is not a calibrated antenna
connector measurement. A tone amplitude of 1.0 requests full configured
deviation. TX is always NBFM, including when the saved receiver mode is WBFM.
The opening and closing Quindar behavior and
[closing-tone carrier-release fix](tx-tail-timing.md) remain in use.

The TX producer and SMI writer can retain allocated resources after stop
and park on condition variables while inactive. They do not poll
the receiver or repeatedly wake while RX is running. Requested worker health,
actual scheduling, capture and writer diagnostics are exposed separately
from the passive display's CPU cost.
Before every TX start, the backend joins both pipelines, confirms SMI idle,
soft-resets the FPGA and creates a fresh TX pipeline. This preserves menu 14's
validated clean-start sequence for clearing stale FPGA FIFO/interface state.
Initialization reapplies the requested sample rate and FPGA gap after reset.
The TX pipeline generation advances on each successful initialization;
counter comparisons must stay within one generation.
A failed TX transition leaves `FAULT` and suppresses automatic RX restoration.
Use `stop` to attempt checked cleanup; after successful recovery, the next
TX start rebuilds any faulted retained pipeline. Closing-cue or drain failures
remain visible in worker diagnostics rather than being reported as normal stops.
TX startup failures print a stage name and observed values in the backend
terminal, and the passive monitor retains the stage in `tx.fault`. For
example, `TX_STATE_TIMEOUT` means the modem did not reach verified TX within
the startup window; `TX_STREAM` indicates failure to enable SMI TX, and
`TX_FPGA_RESET` indicates that the clean-start reset failed.
If pipeline startup was attempted, the failure log also captures `IQIFC0..2`
and FPGA direction/FIFO, frontend and sample-gap readbacks before cleanup.
Each read reports its return code. This consumes the read-to-clear `SYNC`
indicator once on failure; it adds no reads to normal periodic monitoring.

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

Command packets are strict version-1 or version-2 ASCII, at most 1024 bytes. Each request
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

`RESULT` is `OK` or `ERROR`; `SOURCE` is `live` or `simulated`; `STATE` describes
the radio state; `CHANNEL` is `RF09` or `RF24`; `MODE` is `nbfm`
or `wbfm`. Even an error reply includes the current effective settings.
Transport-generated errors use ID `0` when no valid request ID is available;
the unsolicited competing-controller `BUSY` reply also uses ID `0`.

| Code | Meaning |
| --- | --- |
| `OK` | Command accepted, including an idempotent start/stop. |
| `ACCEPTED` | Asynchronous transition accepted; check status for completion. |
| `BUSY` | Another controller owns the command connection. |
| `PROTOCOL` | Malformed or unsupported packet. |
| `RX_RUNNING` | Stop RX before changing frequency, mode or sample rate. |
| `TX_RUNNING` | Stop TX before changing active-radio or TX settings. |
| `TRANSITION` | Wait for the ongoing lifecycle transition to complete. |
| `INVALID` | Invalid supported-control value or threshold pair. |
| `FREQ_RANGE` | Frequency outside the active board/channel range. |
| `HARDWARE` | Tuning, pipeline, verification or control application failed. |
| `FAULT` | Recover the existing fault with `stop` before making changes. |
| `WBFM_NOISE` | Noise squelch was requested in WBFM. |
| `LOOPBACK` | The modem's IQ loopback mode prevents starting RF transmission. |
| `TX_FAILED` | TX setup or readiness verification failed. |

Version 2 retains the first 17 reply fields and appends 13 TX fields in this
order:

```text
TX_PIPELINE_GENERATION TX_REQUESTED_HZ TX_ACTUAL_HZ TX_RATE_HZ TX_REQUESTED_DBM TX_ACTUAL_DBM TX_TONE_MODE TX_TONE_HZ TX_TONE_AMP TX_DEVIATION_HZ TX_REQUESTED RX_RESUME TRANSITION
```

Its first token is `v2`; all packets retain the 1024-byte limit. The new
controller uses version 2. Version-1 receiver requests and 17-field replies
remain supported. In addition to the existing RX commands, version 2 accepts:

```text
v2 ID tx_start
v2 ID tx_stop
v2 ID tx_frequency HZ
v2 ID tx_power DBM
v2 ID tx_sample_rate 1000000|2000000|4000000
v2 ID tx_source mic|tone
v2 ID tx_tone HZ AMPLITUDE
```

The boolean suffix fields use `0` or `1`; `TX_TONE_MODE=1` selects the tone
source. `TX_REQUESTED` describes requested TX state, `RX_RESUME` records prior
RX restoration intent, and `TRANSITION` marks an unfinished lifecycle job.
TX settings in an error reply remain the accepted settings, so rejection
does not overwrite configuration with the attempted values.

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

TX checks also cover RX/TX handoff, clean-start FPGA reset/recreation, parked-worker
inactivity, startup readback failures before cue IQ, faulted-pipeline rebuild,
timeout cancellation, and closing-cue/drain failure handling. The existing Quindar frame counts and
writer acknowledgement are checked at 1, 2 and 4 MS/s. The user's first
physical TX attempts failed without a carrier or opening cue. A follow-up
failed attempt remained in `TXPREP` after 880000 samples were accepted by Linux.
The backend omitted the FPGA reset that menu 14 performs before each TX
start; that sequence is now restored.

On 2026-10-11, the user confirmed successful TX after this correction:
both opening and closing Quindar tones were audible, as was modulation
from the test-tone and microphone sources. The user also confirmed the
RX → TX → RX transitions and that disconnecting the controller during TX
stops transmission and restores RX. This validates the audible TX path and
half-duplex handoff on the tested setup; no new CPU or sample-continuity measurement
accompanied this listening test.

The final TX build passed the four C transport/register/protocol suites and
104 Python checks: 10 backend, 12 RX-control, 19 controller, 13 TX-control and
50 passive-monitor checks. Separate production-worker, audio-adapter,
Quindar/drain and menu-14 lifecycle fixtures also passed. The actual C
controller was exercised against the dry-run backend, including disconnect
and queued stop during startup.

```sh
python3 software/libcariboulite/tests/test_tx_worker_parking.py
python3 software/libcariboulite/tests/test_tx_stop_deadline.py
python3 software/libcariboulite/tests/test_tx_tail_progress.py
python3 software/libcariboulite/tests/test_audio_source.py
python3 software/libcariboulite/tests/test_rx_lifecycle.py
```

The RX-only milestone validation passed four C test suites and 81 Python tests:
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
