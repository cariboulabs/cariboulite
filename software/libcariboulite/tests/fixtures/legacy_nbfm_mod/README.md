# Frozen NBFM modulator reference

The C source and header are unchanged copies from accepted extraction step-5
commit `185086f`. Keep them frozen as the legacy waveform reference for
`test_nbfm_mod.py`; they are not application build inputs.

The current optimized oscillator permits at most one int16 count of I/Q
change against this reference for tested waveforms. The test additionally
checks exact current chunking/reset behavior, 1/2/4 MS/s, interpolation,
pre-emphasis, clipping, silence and underrun. Independent phase measurements
check recovered frequency, tone fidelity and envelope continuity under strict
and production compiler settings. It does not require hardware or ALSA.
