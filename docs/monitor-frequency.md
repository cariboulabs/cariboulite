# Separate TX and RX frequencies in menu 14

Menu 14 now keeps two independent frequency settings. Both start at
430.100000 MHz. Press **F** to edit TX or **G** to edit RX, enter MHz (for example
`430.125` or `868.500`), and press Enter. Escape cancels; Backspace edits the
entry. Empty or invalid input leaves the previous setting unchanged.

Stop both streams before editing frequencies. The monitor rejects frequency
controls during interface loopback as well. Saving changes configuration only:
it does not transmit, start RX, or immediately tune the radio. The display shows
both requested frequencies to six decimal places in MHz. The hardware may round
the requested value to its synthesizer resolution.

**T** starts TX using the saved TX frequency; **R** starts RX using the saved RX
frequency. Starting either direction stops the other. Both use the same HiF/RF24
radio and tuner, so these are alternate receive/transmit frequencies, not
simultaneous duplex operation. Before activation, the monitor explicitly tunes
the shared hardware for the requested direction, even after pipeline recreation.
A reported tuning failure prevents that stream from starting and displays an
error notice. The requested settings remain available for correction or retry.

Both settings survive TX/RX switches, stopped 1/2/4 MS/s changes and pipeline
recreation within the monitor session. Leaving and re-entering menu 14 restores
defaults; there is no configuration-file persistence in this increment.
Noise/carrier squelch choices remain independent of these frequencies.

Input validation follows the current driver's HiF ranges: **1 MHz to below
6000 MHz** on the full board, or **2385–2495 MHz** on the ISM board. The existing
430.1 MHz default uses the full board's front-end mixer and RF24 receiver.
These are driver limits, not a claim of verified performance throughout those
ranges. Menus 11 and 12 also use HiF/RF24. The earlier S1G listening test remains
recorded in the [receiver context](nbfm-rx-sensitivity-context.md).

Press **1**, **2** or **4** with TX, RX and interface loopback stopped to select
1, 2 or 4 MS/s for both directions. NBFM TX and NBFM/mono-WBFM RX use 10,000,
20,000 or 40,000 IQ pairs per 10 ms block; the audio rate remains 48 kHz.
TX setup verifies the FPGA sample gap (3, 1 or 0 respectively) before starting.

Every NBFM RX start enables AGC using the filtered modem signal and selects
the minimum RX analog bandwidth (**160 kHz**) and digital cutoff (**fS / 8**,
or 125 kHz at 1 MS/s). The pipeline confirms TRXOFF before configuration and
verifies the settings by register readback before activation. Switching to
WBFM restores the wide RX filters on its next start. See the
[modem RX profile](nbfm-rx-sensitivity-context.md#minimum-modem-rx-bandwidth-and-explicit-agc-2026-10-10).

## Implementation and validation

The monitor parses and saves configuration, then calls the pipeline frequency
setters before starting each direction. Setters now propagate driver failures;
TX power is applied only after tuning succeeds. Pipeline initialization passes
local frequency copies to the driver, which writes back the achieved frequency,
so it no longer modifies the caller's const parameter object.

Hardware-free lifecycle tests cover different TX/RX frequencies on the shared
radio at all three sample rates, restoration before activation, failed-tune start
blocking and recovery, and preservation of requested values when the driver
rounds its output. Input tests cover decimal MHz, whitespace, non-finite values,
trailing junk and both HiF and S1G bands. Loopback tests cover the frequency and rate hotkeys.

The application build, RX lifecycle, TX stop-deadline, monitor loopback and
baseline-runner software checks pass. Physical acceptance is pending. With an
appropriate test setup:

1. Stop TX/RX; use F and G to select two distinct frequencies.
2. Start RX with R and verify the signal on the RX frequency; stop/switch to TX
   and verify the transmitted frequency with the monitoring receiver.
3. Switch back to RX, then repeat at the other sample rates with streams stopped
   during the rate change. Verify both requested frequencies remain displayed
   and each direction uses its own setting.
4. Check Escape, invalid input, and attempted edits while streaming. Confirm
   these leave the saved settings unchanged.

These checks exercise the new tuning controls. Earlier refactoring acceptance
records correctly deferred retuning when no interface control existed.
