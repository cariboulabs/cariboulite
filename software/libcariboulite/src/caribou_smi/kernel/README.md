# Generated SMI driver resources

The source of truth is [driver/](../../../../../driver/README.md).
Its installer copies `bcm2835_smi.h` and `smi_stream_dev.h` here and generates
`smi_stream_dev_gen.h` from the module built for the running kernel.
Do not edit those generated/copied resources directly; update the driver and
regenerate them. The embedded module is kernel-specific, rather than a portable
replacement for rebuilding against matching kernel headers.
