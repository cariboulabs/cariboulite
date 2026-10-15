# Separate radio control

`cariboulite_radio_controller` is a C terminal client for the independent C radio
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

This builds `build/radio-backend/cariboulite_radio_backend`,
`build/radio-backend/cariboulite_radio_controller` and
`build/radio-backend/cariboulite_radio_monitor`, using the existing
`build/libcariboulite.so` and C/ALSA development dependencies. The controller
links only its command protocol and system libraries; it does not initialize
the radio library or subscribe to periodic monitoring. While idle, it waits
for terminal or socket events.

The backend and controller are C applications; the monitor is still a Python 3
client, exposed under the same executable naming scheme by the build. The earlier
`cariboulite_rx_backend` name reflected the first RX-only implementation. The
current names describe the combined RX/TX radio applications.

### Tested Pi Zero staging checkout

The hardware-paced candidate is built from the Pi 4 source and tested on the
Pi Zero 2 W in an isolated checkout. The original Pi Zero binaries under
`~/src/cariboulite/build/radio-backend/` remain the earlier implementation.
For this candidate, run the following from the staging checkout in each
terminal:

```sh
cd /home/pi/src/cariboulite/build/tx-hardware-paced-zero-20261014
```

Close other radio-owning applications, then start terminal 1 with RX stopped
and the agreed dummy-load TX frequency of 430.100 MHz:

```sh
./build/radio-backend/cariboulite_radio_backend \
  --channel RF24 --frequency 430125000 --sample-rate 1000000 \
  --mode nbfm --playback plughw:Loopback,0,0 \
  --microphone plughw:Loopback,1,1 \
  --tx-frequency 430100000 --tx-power -12 --tx-sample-rate 1000000 \
  --tx-source mic --start-stopped
```

In terminal 2, use `./build/radio-backend/cariboulite_radio_controller`.
The optional third terminal uses
`./build/radio-backend/cariboulite_radio_monitor`. These relative paths select
the staged candidate; the USB microphone and Loopback bridges are unchanged.

Hardware validation applies to the Pi Zero's current
`6.18.50+rpt-rpi-v8` kernel and runtime module source version
`29FA4AB8842B3807F97F0A5`. The installed original module remains unchanged at
`697F3CF2096C282E312BA14`, so a reboot returns to that original driver.
After a reboot, with other radio owners closed, load the matching candidate
from the staging checkout:

```sh
sudo bash driver/tools/tx_driver_reload.sh "$PWD/smi_stream_dev.ko"
```

The reload helper verifies the kernel and saves the original for rollback.
It intentionally refuses another reload while the candidate is already active
and differs from the installed rollback module. A different kernel needs a
matching module build. This candidate changes no FPGA firmware; these tests
do not establish RF clock accuracy or spectrum quality.

### Pi 4 staging checkout (2026-10-15)

The same candidate is staged separately on the Pi 4 at
`/home/pi/src/cariboulite/build/tx-hardware-paced-pi4-20261015`.
Its backend, controller and monitor compile successfully. The recorded runtime
sources match the passing Pi Zero production build, and the candidate driver
module matches the Pi 4's `6.18.50+rpt-rpi-v8` kernel and architecture.

The CaribouLite and Jabra audio device were transferred to the Pi 4 on October
15. With the user's dummy load connected, the candidate driver was loaded
temporarily. All six short checks and all six 180-second microphone/tone trials
passed at 430.100 MHz, followed by same-backend automatic RX restoration at
1 and 4 MS/s. Independent qualification and provenance audits passed; see the
[Pi 4 validation record](pi4-tx-validation-2026-10-15.md).
Current binaries, shared library, firmware and installed driver are preserved.
See the original staging
[instructions](../build/tx-hardware-paced-pi4-20261015/README.md) and
[source/build provenance](../build/tx-hardware-paced-pi4-20261015/staging-manifest.json).

Use this checkout in each terminal:

```sh
cd /home/pi/src/cariboulite/build/tx-hardware-paced-pi4-20261015
```

The Pi 4 candidate module is at `driver/smi_stream_dev.ko`. After a reboot
restores the installed original, the runtime-only reload command, with the
radio idle and the matching kernel, is:

```sh
sudo bash driver/tools/tx_driver_reload.sh "$PWD/driver/smi_stream_dev.ko"
```

The helper refuses another reload while the candidate is already active.
The application paths are `./build/radio-backend/cariboulite_radio_backend`,
`./build/radio-backend/cariboulite_radio_controller` and
`./build/radio-backend/cariboulite_radio_monitor`. The same backend options as
the Zero candidate select TX at 430.100 MHz. Validation uses temporary
USB/Loopback bridges matching the Zero route, with backend microphone
`plughw:Loopback,1,1` and playback `plughw:Loopback,0,0`. These owned bridges
are separate from the backend and have a finite lifetime; their setup and
cleanup are recorded in the validation report. They were stopped after
validation; the report includes a command to recreate the measured audio route
for a later manual test. The candidate driver remains loaded for the current
boot, and permanent installation remains deferred.

### General checkout commands

Close other radio-owning applications first. Run all clients and the backend
as the same user. In terminal 1, initialize the backend with RX stopped:

```sh
build/radio-backend/cariboulite_radio_backend \
  --channel RF24 --frequency 430125000 --sample-rate 1000000 \
  --mode nbfm --playback plughw:Loopback,0,0 --start-stopped
```

In terminal 2, open the controller:

```sh
build/radio-backend/cariboulite_radio_controller
```

The existing monitor can run in an optional third terminal:

```sh
build/radio-backend/cariboulite_radio_monitor
```

Its default `--registers auto` view follows RX/TX state and displays the
active radio's TX registers during `TX_STARTING`, `TX` and `TX_STOPPING`.
Use `--registers tx` to inspect those settings while idle or in `FAULT`,
`--registers rx` for the RX register view, or `--registers all` for the full
inventory. These options affect only the passive register display; pipeline
instrumentation still follows radio state, and hardware polling is unchanged.
See the [register layouts](passive-monitor.md#display-and-protocol).

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
| `rx start` or `rx stop` | Start reception with the saved configuration, or stop and release the RX pipeline. |
| `rx frequency 430.125` | Set receive frequency in MHz. |
| `rx mode nbfm` or `rx mode wbfm` | Select NBFM or mono WBFM. |
| `rx rate 1`, `rx rate 2` or `rx rate 4` | Select RF sample rate in MS/s. |
| `squelch noise on` or `squelch noise off` | Enable or bypass NBFM noise squelch. |
| `squelch carrier on` or `squelch carrier off` | Enable or bypass carrier squelch. |
| `squelch levels 0.20 0.30` | Set noise opening and closing RMS thresholds together, in that order. |
| `tx start` or `tx stop` | Start or stop half-duplex NBFM transmission. |
| `tx frequency 430.125` | Set the separate TX frequency in MHz. |
| `tx power -12` | Set requested output power in integer dBm. |
| `tx rate 1`, `tx rate 2` or `tx rate 4` | Select TX sample rate in MS/s. |
| `tx source mic` or `tx source tone` | Select microphone input or synthesized audio. |
| `tx tone 600 0.4` | Set tone frequency in Hz and normalized audio amplitude. |
| `fpga program` | Load `firmware/top.bin` using the menu-3 sequence; requires stopped RX and TX. |
| `fpga soft reset` | Reset FPGA runtime logic while preserving the loaded firmware; requires stopped RX and TX. |
| `fpga hard reset` | Pulse FPGA configuration reset; requires stopped RX/TX and subsequent `fpga program`. |
| `help` or `?` | Show available commands. |
| `q` or `quit` | Disconnect; RX continues, while active TX is stopped. |

Squelch commands also accept an explicit RX prefix, for example
`rx squelch noise on`. The original unprefixed RX and squelch commands remain
accepted as compatibility aliases. `freq` and `sample-rate` also remain aliases
for `frequency` and `rate`.

For example:

```text
rx stop
rx frequency 430.125
rx mode nbfm
rx rate 1
squelch levels 0.20 0.30
squelch noise on
squelch carrier off
rx start
status
q
```

Frequency, mode and sample-rate commands require stopped RX. A stopped
frequency change tunes the radio and verifies that it remains idle. Mode and
sample-rate changes update saved settings; their DSP and hardware profiles
are applied at the next `rx start`. Every restart creates a fresh pipeline, so
old IQ or PCM frames cannot carry across a retune or mode change.

At RX start, NBFM selects 160 kHz modem analog bandwidth and WBFM selects
250 kHz. Both select the minimum digital cutoff, `fS / 8` (±125 kHz at
1 MS/s), and verify the filter register readback before receiving. WBFM's
internal shifted IF is 320 kHz nominal; the modem delivers zero-centered IQ.
See the [WBFM modem profile](nbfm-rx-sensitivity-context.md#wbfm-modem-rx-bandwidth-2026-10-11)
for the per-rate settings and listening-test status.

Squelch controls work during reception and while stopped. Noise thresholds
use 0.001 RMS resolution and must satisfy `0 < open < close <= 8.0` after
rounding. A pair that collapses to the same rounded value is rejected.
Noise thresholds are independent of audio volume.

WBFM has no noise-squelch detector: its effective noise flag is off and
`squelch noise on` returns `WBFM_NOISE`. Switching to WBFM preserves the saved NBFM
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
build/radio-backend/cariboulite_radio_controller --command 'status'
build/radio-backend/cariboulite_radio_controller --command 'rx stop'
```

These commands connect, execute once and disconnect. `--command 'tx start'`
is refused locally before connection: transmission requires an interactive
controller to remain connected. Controller options are
`--socket PATH`, `--timeout SECONDS` and `--command 'COMMAND'`. The default
reply timeout is 2 seconds; the accepted range is greater than 0 through 60.

## FPGA programming after reboot

Start the backend from the repository root with `--start-stopped`, as shown
above. In the controller, use:

```text
fpga program
status
```

The command loads **`firmware/top.bin` relative to the backend's working
directory**, exactly like menu option 3 in `cariboulite_test_app`. It checks
that the file is readable, regular and nonempty before resetting. A missing
or unsuitable file returns `FIRMWARE_FILE` without resetting the FPGA.
If reception is already running, issue `rx stop` first. Active TX and other
lifecycle transitions also block programming.

The backend preserves the menu sequence: hard reset, force reprogramming
from the file, soft reset, wait 100 ms, read versions, then select debug mode
0 and low-power frontend mode. `cariboulite_configure_fpga` also restores
the SMI GPIO setup and performs its existing programming/status operations.
Checked SPI reads additionally confirm communication, the manufacturer ID
and final low-power mode. Version numbers alone cannot distinguish this
file from the older embedded firmware.

`ACCEPTED` acknowledges the request. Programming then runs asynchronously
in state `FPGA_PROGRAMMING`; use `status` or the monitor to wait for `IDLE`
before `rx start`. Hardware register polling pauses during the operation;
pipeline measurements are unavailable and register snapshots retain their
last sampling time. Saved RX/TX settings remain, and both pipelines stay
stopped. A programming failure leaves `FAULT` and logs its stage in the backend
terminal. `fpga program` can be retried from that state without requiring
communication with the failed FPGA beforehand.

After a known configuration reset, the backend relies on the modem-idle proof
obtained before that reset, joins any retained workers and checks SMI idle
again. It defers modem SPI verification until the firmware has been loaded.
The FPGA and modem share SPI clock/data pins, and the FPGA may use them during
configuration; requiring a modem read beforehand can block the operation
needed to restore communication. The final programming check explicitly
commands modem TRXOFF and verifies it, allowing up to 50 ms to settle, before
clearing the programming requirement. A failed final check logs `FINAL_IDLE`
with component return codes and the observed modem `STATE`.

Both resets are part of this programming command. The hard reset prepares
the FPGA for loading its configuration; the subsequent soft reset initializes
its running logic. This loads the FPGA's volatile configuration and does
not replace the boot firmware. Repeat after a reboot when the default firmware
does not match the software.

The controller can disconnect while programming continues, including with
`--command 'fpga program'`. A disconnected controller does not resume RX or
cancel programming. Backend shutdown waits for an accepted programming job
to finish before releasing hardware.

### Standalone FPGA resets

`fpga soft reset` and `fpga hard reset` use the same GPIO reset calls as the
test app's reset options. Both require stopped RX/TX and no active lifecycle
transition. They do not need a firmware file. The backend destroys retained
pipeline resources, confirms modem/SMI idle, and performs the reset on its
lifecycle worker. `ACCEPTED` acknowledges the request; `FPGA_RESETTING`
indicates work in progress. Status IPC remains available, hardware polling
pauses, and controller disconnection does not cancel the reset or start RX.

Soft reset preserves the loaded firmware and clears runtime interface/FIFO
state, including the TX sample gap. The backend selects low-power mode and
checks SPI communication afterward. Successful completion returns `IDLE`;
the next RX/TX start rebuilds the pipeline and reapplies routing and sample-rate
settings. A failed reset or verification leaves `FAULT`.

Hard reset discards the running configuration. Any default firmware loaded
afterward is not proof that it matches this software. Successful completion
reports `FPGA_NEEDS_PROGRAM`; run `fpga program`, wait for `IDLE`, then start
RX or TX. Other setting changes and soft reset return `FPGA_REQUIRED` until
programming succeeds. `rx stop` and `tx stop` remain harmless, idempotent
commands and cannot clear this requirement. A hard-reset failure reports
`FAULT` and also requires programming if the configuration reset was attempted.

The programming operation also records this requirement when it hard-resets
the FPGA. Failed programming cannot be bypassed with `rx stop` or a soft
reset; only successful programming clears it. Another hard reset is still
allowed while programming is required, without requiring a modem SPI read.
Both resets can be issued through `--command`, for example
`--command 'fpga soft reset'`; check
completion through a subsequent `status` command or the passive monitor.

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

TX now follows SMI/DMA backpressure. The backend requires the updated streaming
driver ABI and refuses an older driver before activation; the terminal menu
uses demand pacing when available and retains legacy pacing on `ENOTTY`.
Driver admission is bounded to two native periods and the application queue to
four frames. A steady **4/4** queue is deliberate prefill within this limit.
The producer and writer use CPUs 1 and 2 where affinity permits.

Hardware-paced TX snapshots actual SMI settings while idle, applies write
setup/strobe/hold **1/4/2**, and verifies the readback before activation. Read
timing, write pace and DMA settings remain intact. Stop, abort and failed starts
restore the original write tuple before RX handoff; `tx.smi_timing` shows actual
settings. A parked precomputed FM frame covers the first refill and counts as
one of the ten opening zero-audio frames. The writer still waits for calibrated
hardware verification before sending application IQ.

Microphone capture runs independently on CPU 0 through opening and closing
cues. Its bounded audio buffer targets **3360 frames / 70 ms**; gradual adaptive
resampling allows **±4%** correction, with proportional gain 0.75 and maximum
slew 100 ppm per 10 ms output block. Five nominal seconds of continuously
clamped desired correction report a worker fault and stop TX. Audio starvation
and overflow fault separately, without that five-second delay. Monitor fields
distinguish real capture loss, cue replacement,
clock correction and driver underruns. Accepted/dequeued IQ counts describe
transport and do not independently establish RF timing. The
[design and qualification record](tx-hardware-paced-design.md) explains the
measured clock-step reserve and current physical validation.

The TX producer, capture worker and SMI writer can retain allocated resources after stop
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
Use `rx stop` to attempt checked cleanup; after successful recovery, the next
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

Before acknowledging `rx start`, the backend checks pipeline setup/tuning,
modem RX state, the SMI streaming operation, positive reader sample progress
and all three RX workers. A failed start releases the pipeline and reports
`HARDWARE` with state `FAULT`. A failed frequency change attempts to restore
the accepted frequency and confirmed idle hardware. A successful rollback
retains `IDLE` and still reports the failed command; a failed rollback reports
`FAULT`. Hardware stop failures are also reported explicitly.

In `FAULT`, `status` remains available and `rx stop` attempts checked cleanup.
`fpga program` also remains available with both pipelines stopped, allowing
recovery when the FPGA is not communicating after a failed programming job.
When a configuration reset has been attempted, `rx stop` leaves the programming
requirement and fault state intact; use `fpga program` for that recovery.
After `rx stop` successfully returns `IDLE`, settings can be changed and RX
started again. The accepted frequency, mode, rate and squelch configuration
remain available after a failed start or tune.

`config_generation` advances when accepted configuration or requested RX
state changes, and on a fault-state transition. `get`, unchanged settings
while idle, and repeated successful `rx start`/`rx stop` commands leave it
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

These executable and terminal-command names do not change the existing socket
paths, status schema or wire protocol.

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
and both thresholds. For a partial terminal edit such as `squelch carrier on`, the
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
| `RX_RUNNING` | Stop RX before changing frequency, mode or sample rate, or programming/resetting the FPGA. |
| `TX_RUNNING` | Stop TX before changing active-radio or TX settings. |
| `TRANSITION` | Wait for the ongoing lifecycle transition to complete. |
| `INVALID` | Invalid supported-control value or threshold pair. |
| `FREQ_RANGE` | Frequency outside the active board/channel range. |
| `HARDWARE` | Tuning, pipeline, verification or control application failed. |
| `FAULT` | Recover pipeline faults with `rx stop`; after a configuration reset, use `fpga program`. |
| `WBFM_NOISE` | Noise squelch was requested in WBFM. |
| `LOOPBACK` | The modem's IQ loopback mode prevents starting RF transmission. |
| `TX_FAILED` | TX setup or readiness verification failed. |
| `FIRMWARE_FILE` | `firmware/top.bin` is missing, unreadable, empty or not a regular file; no reset performed. |
| `FPGA_REQUIRED` | Program the current firmware with `fpga program` before operating the radio or changing settings. |

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
v2 ID fpga_program
v2 ID fpga_soft_reset
v2 ID fpga_hard_reset
```

The boolean suffix fields use `0` or `1`; `TX_TONE_MODE=1` selects the tone
source. `TX_REQUESTED` describes requested TX state, `RX_RESUME` records prior
RX restoration intent, and `TRANSITION` marks an unfinished lifecycle job.
FPGA programming reports `STATE=FPGA_PROGRAMMING` and `TRANSITION=1` until
completion. It accepts no firmware path argument. Version-1 replies map
version-2-only states, including programming, to `FAULT` as before.
Reset requests use `STATE=FPGA_RESETTING` and `TRANSITION=1`; a successful
hard reset completes in `FPGA_NEEDS_PROGRAM` until programming succeeds.
TX settings in an error reply remain the accepted settings, so rejection
does not overwrite configuration with the attempted values.

## Hardware-free validation

Use a dry-run backend with the same controller and optional monitor:

```sh
build/radio-backend/cariboulite_radio_backend \
  --dry-run --start-stopped --seconds 60
```

Dry-run opens no radio or audio devices. Controller replies are explicitly
`simulated`; `RX` in a simulated reply means requested test intent, not
physical reception. Status snapshots retain `state: "DRY_RUN"`,
`rx.running: false`, unavailable worker/pipeline measurements and unavailable
register values. Their optional `control` section reports requested state
and generations. Dry-run simulates a new pipeline generation for each
stopped-to-started transition without creating a real pipeline.
`fpga program` in dry-run simulates acceptance and completion without reading
a firmware file or resetting/programming any device.

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

The current build passed the four C transport/register/protocol suites and
108 Python checks: 12 backend, 13 RX-control, 20 controller, 13 TX-control and
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

## Pi Zero 2 W TX observations (2026-10-14)

The user also tested the backend/controller application on the Pi Zero 2 W
installation. Gradual TX application queue growth persisted at 1 MS/s; the
supplied excerpt showed 29/64 queued frames and 99.8/99.8 frames/s.
At 4 MS/s the queue was empty, but rates were reported around 80–90 frames/s
(79.7/79.7 in the excerpt), with 48 capture overruns and 48 successful recoveries.
Both workers reported `SCHED_OTHER/0`, despite requested priorities 40/48.
These observations identify accumulating latency at 1 MS/s and a separate
production/capture concern at 4 MS/s. Timed counter deltas and CPU/transport
measurements remain necessary to identify the causes. See the
[Pi Zero TX observations and follow-up](pi-zero-tx-queue-test-2026-10-15.md).

The [automated Pi Zero baseline](pi-zero-tx-queue-test-2026-10-15.md#automated-baseline-on-the-pi-zero-2-w)
uses the existing backend and a persistent C controller to measure microphone
and tone TX at 1 and 4 MS/s, with 430.100 MHz TX frequency. It records passive
status, process/thread CPU, scheduling, capture recovery and kernel diagnostics
before changing the TX implementation.

That [physical baseline](pi-zero-tx-queue-test-2026-10-15.md#physical-baseline-results-2026-10-14)
completed all four three-minute runs with normal cleanup. Both 1 MS/s sources
filled the queue after approximately 129 seconds; 4 MS/s microphone/tone averaged
82.81/88.84 frames/s while both TX workers shared nearly saturated CPU 0. The
existing binaries and runtime sources remained unchanged, and the radio returned
to idle afterward.

The [TX rate diagnosis and hardware-paced design](tx-hardware-paced-design.md)
explains why healthy RX differs from TX, records additional pacing/compute
measurements, and describes the implemented demand-driven IQ generation with
bounded buffering, independent microphone capture and adaptive resampling.
The backend now requires the updated SMI driver ABI for TX; start refuses an old
driver before activation. The terminal menu uses hardware pacing when available
and retains legacy pacing when an older driver returns `ENOTTY`. Physical Pi Zero
acceptance now uses the candidate driver loaded at runtime; the original installed
module, application binaries and firmware are preserved. The first prototype
completed three-minute microphone trials at 1 and 4 MS/s and a tone trial at
4 MS/s, with the 4 MS/s producer using about 54% of one core. Its 2 MS/s
microphone run reached a real transport/audio-clock mismatch beyond its initial
±2% guard and stopped explicitly. The revised **±4% / 70 ms** implementation
was tested in a fresh six-run qualification: microphone trials at 1/2 MS/s
completed, then 4 MS/s stopped at about 169 seconds after changing transport
demand exceeded the ±4% audio-clock guard. Tone trials were not reached.
Subsequent timing-only 1/4/2 trials completed at near-nominal cadence for
microphone 1/2/4 MS/s, including five minutes at 4 MS/s, and tone at 4 MS/s,
without stable capture loss, driver underruns or ASRC faults. The integrated
apply/restore/first-refill implementation then passed all six 180-second
microphone/tone trials at 1/2/4 MS/s, with completed rates of
99.9965–100.0045 frames/s, zero total driver underruns including startup, and
zero capture loss, ASRC faults or writer errors. Its 4 MS/s microphone/tone
producer used 54.67%/53.78% of one core. Actual TX timing read 1/4/2/1 and
stopped readback restored 1/5/1/1. An application queue at 4/4 is the intentional
bounded prefill with matched rates; it does not grow into a large backlog.
Independent qualification, file hashes and cleanup passed. Physical automatic
RX restoration also passed at 1/4 MS/s after five-second TX windows, with about
30 seconds of healthy resumed RX, restored 1/5/1 timing and final idle cleanup.
See the [handoff evidence](../build/radio-backend/tx-rx-handoff-zero-retry-20261014/handoff-summary.json)
and [final verification](../build/radio-backend/tx-hardware-paced-validation-20261014/verification-handoff.json).
Exact SWE sampling behavior and RF waveform/calibration remain unproven. See the
[implementation trials](pi-zero-tx-queue-test-2026-10-15.md#hardware-paced-implementation-trials-2026-10-14).
