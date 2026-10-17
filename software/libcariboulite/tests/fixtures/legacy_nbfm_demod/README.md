# Frozen NBFM worker reference

These two files are unchanged copies of `src/nbfm_demod.c` and `.h` from
commit `b3da533` (accepted extraction step 4). Keep them frozen: they are test
inputs, not application build inputs.

`test_nbfm_demod.py` replaces FIFO I/O with deterministic IQ and queue-depth
observations, renames legacy symbols and fixes the clock. The old worker is
now the reference for queue framing, priming and clock-control progress.
Its boxcar PCM intentionally differs from the current complex channel filter;
current PCM determinism is checked between independent current workers.
No hardware or ALSA library is used.
