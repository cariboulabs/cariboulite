# Frozen FM DSP reference

`nbfm_demod.c` and `.h` are unchanged copies from commit
`e246267f93d249902d3153ff69576bf3936436c8`. Keep them frozen; they are test
inputs, never application build inputs. This is the combined implementation
before NBFM/WBFM mode extraction. `oracle.c` renames public symbols and compiles
in a separate translation unit.

`test_fm_reference.py` still compares exact WBFM PCM, raw taps and per-call
progress against the frozen implementation at 2/4 MS/s. The production NBFM
channel filter intentionally changes the old response, so NBFM now uses a
second current instance with different chunking at 1/2/4 MS/s. Tests vary
chunks/capacities, resets, clock corrections, audio controls, clean/weak noisy
FM, noise-only IQ, zero-IQ intervals and silence. NBFM worker and signal-quality
tests provide additional independent coverage. No hardware or ALSA is required.
