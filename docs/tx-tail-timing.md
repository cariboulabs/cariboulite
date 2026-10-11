# TX carrier release after the closing Quindar tone

On 2026-10-10, the user reported satisfactory listening with the updated NBFM
receiver, then noticed that TX remained keyed for roughly two seconds after
the closing Quindar tone.

## Cause

The application sends a 250 ms, 2475 Hz closing tone, followed by enough silence
to displace it through the kernel FIFO, cyclic DMA ring and FPGA FIFO. A completed
write acknowledges kernel acceptance rather than actual RF transmission, so
the buffer allowance and final-frame acknowledgement are necessary.

The requested FIFO size may be rounded to a power of two. Current preallocated
drivers round down; older allocating drivers round up, following the
[Linux kfifo implementation](https://raw.githubusercontent.com/torvalds/linux/v6.18/lib/kfifo.c).
The drain calculation
now uses the larger rounded capacity, keeping the fast flush safe with either
driver. This can leave a larger conservative guard with a non-power-of-two
requested multiplier; it is still bounded by downstream buffering rather than
an additional full padding period.

Previously, the modulator generated every padding frame at the normal 10 ms
audio cadence. For a native batch of 131,072 IQ pairs and FIFO multiplier 16,
the conservative padding is 227 frames at 1 MS/s: **2.27 seconds**. That delay
was imposed even when the kernel FIFO was nearly empty. Zero audio produces
an unmodulated FM carrier, so the transmitter stayed visibly keyed throughout.

## Change

Only the finite final silent-padding stage bypasses the producer's audio-clock
sleep. Application FIFO blocking and the existing SMI writer's backpressure
control its progress. Accepting the complete conservative padding still forces
the end tone through the downstream buffers, while an empty FIFO can fill
quickly rather than taking its maximum capacity duration.

The opening cue, closing 250 ms tone, pre-tone silence, microphone and ordinary
test tone retain their normal cadence. Each fast padding frame reanchors the
producer clock; normal pacing resumes when the finite injection ends. Continued
held silence remains paced. Stop still waits for the final complete frame's
write acknowledgement before disabling SMI and the radio, with bounded failure
paths and the existing hardware lock for TX-to-RX handoff.

Implementation:
[tx_pipeline.c](../software/libcariboulite/src/tx_pipeline.c),
[mod_worker.c](../software/libcariboulite/src/mod_worker.c), and the internal
`tone_injector_t.fast_padding` flag in
[tx_pipeline.h](../software/libcariboulite/src/tx_pipeline.h).
No kernel or FPGA update is required.

## Validation

The sample-marker simulation checks actual stop code against kernel FIFO and
four-quarter cyclic DMA behavior at 1/2/4 MS/s, including empty, partial and full
buffer occupancy. It verifies every sample of the closing tone reaches RF
before shutdown, while delayed or stalled writers and producers still finish
within their failure deadlines.

Production-worker tests verify that only finite final silence skips the sleep,
that microphone and tone timing resume with a fresh deadline, and that blocked
or failed enqueue preserves sequence/injection progress. Existing partial-write
and TX-to-RX handoff checks remain in place.

The simulation reproduces **2.106 seconds** of extra carrier with the former
pacing at 1 MS/s and multiplier 16. With fast padding, the largest simulated
post-tone delay for that multiplier is about **62 ms at 1 MS/s**, **28 ms at
2 MS/s**, and **21 ms at 4 MS/s**. For multiplier 6, the current driver's smaller
rounded FIFO leaves an additional conservative allowance for compatibility with
the larger legacy FIFO: approximately **587/292/153 ms** at 1/2/4 MS/s.
The complete, contiguous 250 ms tone is preserved in all cases.

```sh
python3 software/libcariboulite/tests/test_tx_stop_deadline.py
python3 software/libcariboulite/tests/test_tx_tail_progress.py
python3 software/libcariboulite/tests/test_tx_write_progress.py
python3 software/libcariboulite/tests/test_rx_lifecycle.py
cmake --build build --target cariboulite_test_app -j2
```

Simulation reproduces the original approximately two-second hang with an empty
large FIFO at 1 MS/s. Fast final padding reduces that to a short DMA guard while
retaining the complete tone. Simulated timing excludes modulation CPU time,
hardware stalls and RF ramp-down; they are not measured hardware timings.
No over-air TX was performed for these software checks.

## Physical acceptance (2026-10-10)

After installing the shutdown fix, the user confirmed **RX and TX tests
successful**. This records successful physical testing of the updated receiver
and TX shutdown behavior. The channel and sample rate were not restated, and
no numerical carrier-release measurement was supplied.

## Separate controller integration

The independent backend now exposes the same NBFM TX path through the
[separate C radio controller](radio-control.md). `tx start` suspends the RX
pipeline; `tx stop` runs the existing closing-tone and bounded drain path
before unkeying and restoring previously running RX. Lifecycle transitions
are asynchronous, keeping status publication and command responses available
while the TX tail drains. `ACCEPTED` reports a pending request rather than
completed carrier release.

Controller disconnection and normal backend shutdown request TX shutdown;
quitting the passive monitor leaves radio operation unchanged. Interactive
control is required to start TX, and a one-shot `--command 'tx start'` is
refused before connection.

Inactive TX producer and writer threads now park on condition variables.
Parking changes their idle waiting behavior, while opening/closing cues,
ordinary audio pacing, final fast silent padding and complete-frame write
acknowledgement retain the established timing path. The simulated release
times and user acceptance above describe their original validation; they
are not new measurements of the controller/backend integration.
