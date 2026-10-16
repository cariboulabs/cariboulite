# Step 4 physical follow-up — 2026-10-03

Final status: **accepted by Jan on 2026-10-04**. The sections below preserve
the investigation sequence, including superseded pending-acceptance notes.
The unresolved 96.2 MHz reception issue remains separate from acceptance of
the extraction; see the final entry.

Jan reports NBFM TX/RX works as expected and mode, sample-rate and frequency
switching are blocked while streaming. WBFM has clipping-like noise, worse at
4 MS/s and less at 2 MS/s. Jan confirms the same Pi headphone-jack playback
route, 96.2 MHz station and bridge as the clean step-3 test. Hardware acceptance
remains pending; do not proceed to step 5.

The combined application log has 723 audio heartbeats. Ten report ALSA=XRUN;
all others report RUNNING. There are 5,369 zero-result SMI read timeouts, no
warning/error-level records or DSP processing failures, and normal driver
release. The XRUN count is the number of sampled states, not a complete count
of underrun events. The log includes multiple streams/frequencies and does not
explicitly annotate selected demodulator modes or RF rates, so these events
cannot all be assigned to WBFM or a particular rate.

Audio queue levels are low and correction often reaches +500 ppm. Earlier
step-1/step-2 comparison logs showed no sampled XRUN state. Playback starvation
is a concrete lead for crackle, not proof of its cause or a step-4 regression.
PCM clipping is not measured by current diagnostics. The bridge's stderr is
not included. A same-setup comparison with the preserved step-2 executable
can help distinguish a new step-4 issue from shared runtime conditions.

## Additional comparison

Jan reports that the preserved step-2 executable also now produces noise on
headphones, with no errors displayed by the external ALSA bridge. Its saved
`fm-step2-headphones.log` has 141 heartbeats, all ALSA=RUNNING, 880 zero-result
SMI read timeouts, no warning/error records, and normal shutdown.
`fm-step4a.log` has 363 heartbeats, three sampled ALSA=XRUN states, 3,642
zero-result SMI read timeouts, no warning/error records, and normal shutdown.
The earlier clean step-3 log has 1,151 heartbeats and no sampled XRUN states.
Noise in step 2 without a sampled XRUN weakens underruns as a complete
explanation. Heartbeat sampling cannot exclude brief underruns. Neither these
logs nor bridge stderr measure PCM clipping or establish an RF cause.
A loopback PCM recording for peak/clipping and independent playback analysis
is the next diagnostic boundary; no signal algorithm changes are justified yet.

## Loopback PCM capture

The first WAV attempt contained 30 seconds of digital silence and supplies no
signal-quality evidence. The subsequent raw capture contains 30 seconds of
48 kHz mono S16 audio and is preserved as `wbfm-headphones.wav` for playback.
Peak absolute sample is 6,769 (about -13.7 dBFS); RMS is 1,773.8 (about -25.3
dBFS). There are no samples at either S16 clipping rail. Per-second RMS ranges
from 1,560.5 to 1,984.8, with no full silent seconds. This rules out S16 rail
clipping in the captured interval, but not RF/earlier distortion or downstream
playback clipping. Jan confirms RX was at 4 MS/s, noise was audible during capture, and the noise
is present in the saved recording. This locates the artifact at or before the
loopback PCM capture boundary; the headphone speaker alone does not explain
it. Samples are not classified as speech/music from numerical measurements.
RF/IQ clipping, IQ discontinuities, DSP behavior and upstream loopback feeding
remain possible; no specific cause or refactoring regression is established.

## Repeat capture at both rates

Jan made separate 2 and 4 MS/s recordings and reports no apparent noise or
distortion in this repeat. They are preserved as `wbfm-headphones-2msps.wav`
and `wbfm-headphones-4msps.wav`. The earlier 4 MS/s capture with confirmed
noise remains separate. This intermittent observation does not establish a
fixed rate-dependent defect or resolve its cause. Sequential broadcast captures
contain different program material and are not identical-input DSP comparisons.

Both recordings contain 30 seconds of non-silent PCM, no S16 clipping-rail
samples, and no zero run longer than one sample. Peaks are 6,607 (-13.91 dBFS)
at 2 MS/s and 6,688 (-13.80 dBFS) at 4 MS/s; RMS is 1,888.4 and 1,992.6.
These measurements exclude final rail clipping/extended digital silence in
these captures, not every form of distortion or IQ discontinuity.

## Independent receiver observation — 2026-10-04

Jan reports the same type of distortion using CaribouLite through SDR++ in
server mode. This is evidence against the custom audio-demodulator refactor
being the sole cause: the repository's Soapy stream adapter supplies radio IQ
and does not invoke the custom FM demodulator or application ALSA bridge.
The exact SDR++ source/server configuration, RF rate, gain, frequency and
playback device were not supplied, so shared components must not be inferred
beyond the CaribouLite receiver setup. RF reception, gain/overload, hardware,
shared driver/IQ transport and any shared playback device remain candidates;
no specific cause or step-4 regression is established.

## Strong adjacent stations — 2026-10-04

Jan identifies the desired 96.2 MHz station as 3FM and observes much stronger
signals at 95.9 and 96.5 MHz (each 300 kHz away). Jan describes them as local
pirate stations based on their RDS text; that identification is user-reported,
not independently verified. Tuning to a stronger adjacent station gives perfect
audio, including through the Jabra speaker. This makes a general speaker or FM
DSP defect less likely and points toward conditions specific to reception of
96.2 MHz. Adjacent-channel interference, receiver overload and selectivity are
hypotheses, not confirmed causes. No gain-reduction or attenuation outcome has
yet been reported. Jan explicitly accepted step 4 on 2026-10-04 based on these results.

Step 4 is accepted. Earlier pending-acceptance notes above describe the
investigation history; the 96.2 MHz reception issue remains separately open.
