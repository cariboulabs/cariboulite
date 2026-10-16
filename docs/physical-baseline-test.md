# Physical audio/RF baseline runner

Run after building the app with the install script. This is a new repeatable
procedure for the updated Raspberry Pi OS; its first successful run was
confirmed by Jan on 2026-09-19. Earlier H0 acceptance remains a separate recorded result.
The runner still covers **2 and 4 MS/s** only, although the current pipelines
also support 1 MS/s. TX uses AUTO pacing: the current driver selects hardware
backpressure, while an older driver returning `ENOTTY` selects legacy pacing.
The September acceptance records below predate hardware-paced TX; later
[Pi Zero](pi-zero-tx-queue-test-2026-10-15.md) and
[Pi 4](pi4-tx-validation-2026-10-15.md) trials record that transport's validation.

## Recorded setup

- 50 ohm dummy load connected to the **HiF** antenna port.
- Jabra GN 510 USB for audio capture and playback.
- TX monitoring: SDRPlay RSP2PRO, 2 MS/s, tuned to 430.1 MHz, NFM,
  10 kHz audio bandwidth, squelch −120 dBm.
- App sessions: HiF/RF24, 430.1 MHz, requested TX power −3 dBm,
  48 kHz audio, 2.5 kHz FM deviation. Physical RF coupling to the SDRPlay
  has not been specified; retain the same arrangement for comparisons.

## Run

From the repository root after rebuilding:

```sh
python3 software/libcariboulite/tools/physical_baseline.py --dry-run
python3 software/libcariboulite/tools/physical_baseline.py
```

Use the same hardware permissions as the normal app. If it requires root, run
the second command with `sudo`. The script does not install, rebuild, or change
system audio configuration. By default it uses the existing Jabra audio bridges:

```text
Jabra microphone -> arecord -> Loopback playback 0,1 -> app capture 1,1
app playback 0,0 -> Loopback capture 1,0 -> aplay -> Jabra speaker
```

Keep those bridges running. The runner opens `plughw:Loopback,1,1` for capture
and `plughw:Loopback,0,0` for playback, matching the interactive app.
Use `--audio-route direct` only when the Jabra is not owned by those bridges or
another audio process. Direct mode discovers the Jabra by ALSA card ID.
To override either route explicitly:

```sh
python3 software/libcariboulite/tools/physical_baseline.py \
  --capture 'plughw:CARD=YOUR_CARD_ID,DEV=0' \
  --playback 'plughw:CARD=YOUR_CARD_ID,DEV=0' \
  --seconds 10
```

Find card IDs with `arecord -l` and `aplay -l`. Check speaker/microphone mute and
volume before starting. Sessions last 10 seconds by default, with two-second
separations; `--seconds` accepts 2–300. `--firmware` overrides `firmware/top.bin`;
`--app` overrides `build/cariboulite_test_app`.

The runner launches the app once, performs FPGA hard reset, programs the chosen
firmware and performs the post-programming soft reset. It then runs:

| Order | Menu pipeline | Direction | RF rate | Listen for |
| --- | --- | --- | --- | --- |
| 1 | 11 | TX | 2 MS/s | 600 Hz tone; Quindar start/end |
| 2 | 11 | TX | 4 MS/s | Same 600 Hz pitch; Quindar start/end |
| 3 | 12 | RX | 2 MS/s | Idle-channel audio is gated by default noise squelch |
| 4 | 12 | RX | 4 MS/s | Same squelched receiver behavior |
| 5 | 14 | TX | 2 MS/s | Quindar start/end; Jabra microphone audio |
| 6 | 14 | TX | 4 MS/s | Quindar start/end; Jabra microphone audio |
| 7 | 14 | RX | 2 MS/s | Idle-channel audio is gated by default noise squelch |
| 8 | 14 | RX | 4 MS/s | Same squelched receiver behavior |

All pipelines are stopped/destroyed before rate changes. A soft reset between
sessions clears shared FPGA FIFO state. Normal TX startup/shutdown supplies
2525 Hz / 2475 Hz Quindar tones through existing pipeline code.

## What this tests

The app's `--baseline-test` mode calls the same TX/RX pipelines as options 11,
12 and 14; option 14 initializes both pipelines through its existing helper.
It deliberately bypasses menu input and curses rendering. It does not test UI
key handling, live retuning, or the monitor's concurrent register display.
Options 11, 12 and 14 normally select `radio_low` (S1G); this runner explicitly selects
`radio_high` to match the HiF dummy load. Interactive menu defaults are unchanged.
All sessions use the selected audio routes; the default loopback routes match
the menu and reach the Jabra through the existing bridges. These differences are explicit so results are not mistaken for tests of
unchanged interactive menu defaults.

The runner checks initialization/start results and RF FIFO consumption progress.
It records valid RSSI readings once per second for each RX session; an invalid
reading fails the run rather than reporting a cached value as a fresh sample.
RSSI is the modem-reported measurement on the HiF path, not a calibrated
connector power measurement. Existing pipeline code does not propagate every
hardware operation's return code; automation completion is not proof of RF output
or audible success. No known RF receive signal is supplied by this dummy-load test.

## Results and confirmation

Each run creates a timestamped directory under `build/physical-baseline/`:

- `metadata.json`: source revision/status, app/firmware SHA-256, OS/kernel/Pi,
  ALSA device listings, exact command and declared external setup.
- `source.diff`: tracked source changes relative to HEAD (untracked files are
  listed in metadata but not included; commit the runner before establishing a
  long-term reference).
- `app.log`: app diagnostics and baseline events.
- `events.csv`: ordered session events and individual RX RSSI samples.
- `summary.json`: per-session RSSI count/minimum/mean/maximum and automation result.
- `listening-checklist.md`: audible checks awaiting Jan's confirmation.

Copy accepted result directories outside `build/` before deleting it for a clean
rebuild, or use `--output /path/to/persistent/results`.

Report whether both option 11 tones had the correct 600 Hz pitch, all four TX
sessions had both Quindar tones, and describe the RX audio/gating. The original
September checklist asks for receiver noise and predates default noise squelch;
the current runner supplies no known RF signal and no squelch-bypass option,
so muted idle RX is expected and cannot by itself establish playback failure.
Use an interactive RX session with noise squelch bypassed for an audible
noise/playback check. Identify
any failure by option and rate. Automation never marks the listening checks passed.
Ctrl-C requests cleanup. The supervisor enforces an overall deadline and, if
cleanup hangs, escalates termination and records that the radio state must be
checked before retrying. On errors it stops the sequence and retains logs.

## Software-only checks

```sh
python3 software/libcariboulite/tests/test_physical_baseline.py
python3 software/libcariboulite/tests/test_rx_lifecycle.py
python3 software/libcariboulite/tests/test_tx_stop_deadline.py
python3 software/libcariboulite/tests/test_nbfm_rate.py
```

The reporting tests use a fake app and do not validate physical reset/programming,
RF, or audio. The hardware run and listening confirmation remain pending.

## First-run failure and correction

The run `20260919T073841.346127Z` completed both option 11 TX sessions, then
failed before starting option 12 RX: `Device or resource busy` opening
`plughw:CARD=USB,DEV=0`. Existing `arecord` and `aplay` bridges already owned
the Jabra capture and playback devices. The runner now defaults to their
Loopback endpoints instead of opening the Jabra exclusively. No bridge process
was stopped and no system audio configuration was changed. The subsequent physical retest passed (see below). This correction only changed
the Python runner, so no rebuild was needed.

## Accepted run — 2026-09-19

Run `20260919T074311.996531Z` completed all eight sessions with exit code 0.
Jan confirmed: “Perfect test. I heard the tones at the correct pitch, as well as
 the receiver noise.” This is a functional listening confirmation, not an
instrumented pitch measurement.

| RX pipeline | Rate | RSSI min / mean / max (dBm) | Samples |
| --- | --- | --- | --- |
| 12 | 2 MS/s | −107 / −104.6 / −103 | 10 |
| 12 | 4 MS/s | −107 / −103.7 / −102 | 10 |
| 14 | 2 MS/s | −110 / −107.0 / −105 | 10 |
| 14 | 4 MS/s | −108 / −105.0 / −102 | 10 |

[Archived metadata](baselines/20260919T074311.996531Z/metadata.json),
[results and confirmation](baselines/20260919T074311.996531Z/summary.json), and
[individual events/RSSI](baselines/20260919T074311.996531Z/events.csv) are preserved
outside the build directory. The tested checkout was `9927d03` plus the
uncommitted runner changes; binary and firmware hashes are in the metadata.
This supplements the earlier H0 with the updated OS and repeatable test setup.

## Step 1 retest — 2026-09-19

Run `20260919T075122.967948Z` completed all eight sessions with exit code 0 after
the ALSA source rename. Jan confirmed: “Test running completed successfully,
all the tones were noticed at the correct pitch.” Each RX session recorded ten
RSSI samples. Receiver-noise listening was not separately stated in this retest.

[Metadata](baselines/20260919T075122.967948Z/metadata.json),
[results](baselines/20260919T075122.967948Z/summary.json), and
[events/RSSI](baselines/20260919T075122.967948Z/events.csv) are archived outside
`build/`. The metadata identifies the tested revision plus uncommitted changes
and the binary/firmware hashes.

## Step 2 / H1 retest — 2026-09-19

Run `20260919T075807.545875Z` completed all eight sessions with exit code 0.
Jan confirmed correct audio tones and pitch, and perfect audio modulation during
the second set of TX tests (option 14, 2 and 4 MS/s). The runner's start/stop
cycles completed; additional manual repeated toggles were not separately reported.
Each RX session recorded ten RSSI samples.

[Metadata](baselines/20260919T075807.545875Z/metadata.json),
[results](baselines/20260919T075807.545875Z/summary.json), and
[events/RSSI](baselines/20260919T075807.545875Z/events.csv) are preserved outside
`build/`. This tests step 2's common audio-source interface on the existing
Jabra/ALSA loopback setup; the checkout includes uncommitted step 2 changes.

## Option 13 self-test playback

Option 13 now uses `plughw:Loopback,0,0`, matching the RX menus and existing
Loopback-to-Jabra playback bridge. It previously used `plughw:3,0` (Pi headphones
in the recorded setup). Keep the bridge running. This software-only modem path
should play an opening cue, recovered 600 Hz audio, and a closing cue on the
Jabra; it does not transmit RF. Jan confirmed this correction on 2026-09-19; option 13 works perfectly.

## Step 3 / H2 retest — 2026-09-19

Run `20260919T080542.756197Z` completed all eight sessions with exit code 0.
Jan confirmed all tests worked and audio had the correct pitch. Option 13 was
also tested separately and confirmed working after selecting the correct
Loopback-to-Jabra playback device. H2 is passed.

[Metadata](baselines/20260919T080542.756197Z/metadata.json),
[results and manual confirmation](baselines/20260919T080542.756197Z/summary.json),
and [events/RSSI](baselines/20260919T080542.756197Z/events.csv) are archived outside
`build/`. Each RX session recorded ten RSSI samples. This run includes the
uncommitted step 3 tone-source changes; exact binary/firmware hashes and source
status are in the metadata.

## Step 4 automated retest — 2026-09-19

Run `20260919T121032.048186Z` completed all eight sessions with exit code 0.
Jan confirmed equal TX monitoring levels of approximately -90 dBm on the SDRPlay
RSP2pro, good audio quality and correct tone pitch. Both automated TX cases use
HiF; the earlier interactive comparison used different radio paths and showed
unequal receiver levels. These are receiver observations, not calibrated TX power.

[Results](baselines/20260919T121032.048186Z/summary.json),
[metadata](baselines/20260919T121032.048186Z/metadata.json) and
[events/RSSI](baselines/20260919T121032.048186Z/events.csv) are archived outside build.
Jan separately confirmed option 13 tested correctly and known-signal RX passed
at both 2 and 4 MS/s. Jan also confirmed numerous RX start/stop actions without odd behaviour. H3
is passed. Interactive retuning is deferred until that control exists; it is
not required to accept the ALSA playback extraction.

## Step 5 / H4 retest — 2026-09-19

Run `20260919T123837.461278Z` completed all eight sessions with exit code 0.
Jan confirmed correct tone pitch and clean modulation, separately passed option
13, and reported extended RX without unusual behaviour. Jan explicitly accepted
H4. Exact extended-test duration and per-rate details were not separately supplied;
this records functional acceptance, not a quantified clock-drift measurement.

[Results and confirmation](baselines/20260919T123837.461278Z/summary.json),
[metadata](baselines/20260919T123837.461278Z/metadata.json) and
[events/RSSI](baselines/20260919T123837.461278Z/events.csv) are archived outside build.
The tested checkout includes the uncommitted step 5 changes based on `b3da533`.

## Step 6 / H5 retest — 2026-09-19

Run `20260919T131156.324241Z` completed all eight sessions with exit code 0.
Jan confirmed successful automatic testing and option 13, with clean modulation
audio and correct pitch including test tones. Option 13 was confirmed in addition
to the runner; it is not included in its eight automated sessions. H5 is passed.

[Results and confirmation](baselines/20260919T131156.324241Z/summary.json),
[metadata](baselines/20260919T131156.324241Z/metadata.json) and
[events/RSSI](baselines/20260919T131156.324241Z/events.csv) are archived outside build.
The tested checkout includes uncommitted step 6 changes based on `185086f`.

## Step 7 / H6 retest — 2026-09-19

Run `20260919T133517.433185Z` completed normally with exit code 0. Jan reports
all H6 tests pass. [Results and confirmation](baselines/20260919T133517.433185Z/summary.json),
[metadata](baselines/20260919T133517.433185Z/metadata.json) and
[events/RSSI](baselines/20260919T133517.433185Z/events.csv) are archived outside build.

Jan observed the FPGA SMI channel value change when switching RX to TX in menu
14. `smi_ctrl.v` reports its RX-source register (`r_channel`), which defaults to
zero on reset. Menu 14 resets the FPGA before TX startup; TX does not program
this RX-only selector. HiF RX activation writes one. The selector multiplexes
RX input in `top.v` and does not select TX routing. The display label was changed
after this test from SMI CHANNEL to RX SOURCE, explicitly marked unused in TX.
