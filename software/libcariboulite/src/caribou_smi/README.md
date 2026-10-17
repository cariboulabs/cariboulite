# Caribou-SMI userspace API

This module opens `/dev/smi` provided by `smi_stream_dev`. Follow the
[driver instructions](../../../../driver/README.md) for kernel installation;
`bcm2835_smi_dev` is the legacy device driver replaced by this module.

The current API is declared in [caribou_smi.h](caribou_smi.h). Older documentation
for callback streams (`setup_stream`, `run_pause_stream`, `destroy_stream`) and
`caribou_smi_timeout_read` described an earlier interface and does not apply.

## Lifecycle

```c
int caribou_smi_init(caribou_smi_st *dev, void *context);
int caribou_smi_init_with_fd(caribou_smi_st *dev, void *context, int owned_fd);
int caribou_smi_close(caribou_smi_st *dev);
```

`init_with_fd` duplicates the descriptor; the caller retains ownership of the
original. Radio applications should normally use the higher-level radio API so
modem, FPGA and driver state transitions remain coordinated.

## Reading and writing

```c
int caribou_smi_read_timed(caribou_smi_st *dev, caribou_smi_channel_en channel,
    caribou_smi_sample_complex_int16 *samples, caribou_smi_sample_meta *metadata,
    size_t count, long timeout_us);
int caribou_smi_write_timed(caribou_smi_st *dev, caribou_smi_channel_en channel,
    const caribou_smi_sample_complex_int16 *samples, size_t count, long timeout_us);
```

Counts and positive return values are complex samples, not bytes. Each sample
contains signed 16-bit `i` and `q` fields (four bytes total). The timed functions
process at most one native batch; `timeout_us <= 0` means try once. They return
partial sample counts, zero on timeout/backpressure, and negative values on error.
Query the batch size with `caribou_smi_get_native_batch_samples`.

After a short write, retry from `samples + returned_count`. A partly transmitted
sample is not counted until complete; the device retains its byte offset. Preserve
the unaccepted samples and serialize writes and stream transitions for the device.
The compatibility `caribou_smi_write` and `caribou_smi_write_samples` functions
also return accepted samples without padding; see the header for signatures.

`caribou_smi_channel_900` selects modem RF09 and `caribou_smi_channel_2400`
selects RF24. These names do not describe the board's complete mixer tuning range.
`caribou_smi_read_loopback_timed` is for interface-loopback diagnostics: it accepts
echoed TX control bits; normal reception uses the strict decoder.

## Hardware-paced TX driver contract

The shared FM TX pipeline additionally requires the current driver's versioned
`SMI_STREAM_IOC_GET_TX_STATUS` and `SMI_STREAM_IOC_SET_TX_BUFFER_LIMIT` interfaces.
Their structs and `SMI_STREAM_TX_ABI_VERSION` are defined in the source-of-truth
[driver header](../../../../driver/smi_stream_dev.h). The pipeline validates the
ABI/geometry, bounds kernel admission to two DMA periods and applies the TX
write-timing profile while idle. DMA retirement measures progress into the SMI
peripheral; it is not an independent RF sample-clock measurement.

The raw timed-write API above is still a partial-write transport API; calling it
alone does not supply the FM pipeline's startup, pacing or shutdown sequence.
See [TX design](../../../../docs/tx-hardware-paced-design.md) for ownership,
buffering, timing and physical-validation details. Building userspace without
loading its matching driver does not update the running ABI.

## Validation

The regression harness is [test_smi_timed.c](../../tests/test_smi_timed.c),
with related lifecycle and TX progress checks in [tests](../../tests/).
For the physical interface see [SMI notes](../../../../docs/smi/README.md).
These software tests do not establish RF performance or physical bus timing.
