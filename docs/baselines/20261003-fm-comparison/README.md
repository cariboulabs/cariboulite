# FM step-1/step-2 listening comparison — 2026-10-03

Jan compared frozen step-1 DSP and step-2 extraction at 96.2 MHz and
reported no discernible audio difference. Remaining speech distortion and
hiss/crackle have an unconfirmed cause; indicated RX level was around -45 dBm.
The logs capture application stderr, not the external arecord/aplay bridge.
RF rate, gain settings and the selected demodulation mode are not explicitly
recorded in these logs; WBFM identification comes from Jan's test report.

| Log | Build | Last audio heartbeat | Zero-result SMI read timeouts |
| --- | --- | --- | --- |
| fm-step1.log | Frozen DSP | 27.2 s | 164 |
| fm-step1a.log | Frozen DSP | 104.8 s | 614 |
| fm-step2.log | Extracted DSP | 33.3 s | 194 |
| fm-step2a.log | Extracted DSP | 161.2 s | 943 |

All four logs tune to approximately 96,199,998.21 Hz on channel 1, open
48 kHz mono S16 ALSA playback on plughw:Loopback,0,0, and end with driver
release. No warning/error-level messages, ALSA xruns or DSP failures were found.
Heartbeat ALSA states are RUNNING. Audio queue starts around 1/24 blocks;
longer sessions finish at 3/24 and 5/24. Rate correction is initially +500 ppm
and ends at +300 ppm in the longer sessions.

SMI zero-result read timeouts occur in both builds at similar approximate
rates. They indicate no samples returned for those calls, not proof of sample
loss or audible discontinuity. Low queue levels and correction saturation are
shared observations requiring further diagnosis if pursuing the crackle.
Application logs do not rule out bridge underruns, PCM clipping or speaker
artifacts. These short sessions are not extended endurance validation.

Jan explicitly accepted step 2 as completed on 2026-10-03, with no evidence
of regression. The shared distortion observation remains unresolved.
