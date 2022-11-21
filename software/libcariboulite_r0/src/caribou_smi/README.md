# Caribou-SMI API Driver
This directory contains a user-mode interface code with the `bcm2835_smi` kernel module.
To start working with the interface, some kernel modules need to be probed first.

By default, CaribouLite boards are pre-programmed to automatically probe the SMI modules on startup. Each CaribouLite contains an EEPROM device that in programmed during production with the updated device tree overlay (for more info [see here](../../../devicetrees/README.md)) file that loads on startup. This device tree "requests" loading the SMI associated kernel modules on startup. So if you have your CaribouLite board mounted on the 40-pin header, the module probing is not necessary.

To check whether these modules are probed, follow the next command in your Raspberry Pi terminal:
```
lsmod | grep smi
```

you should be seeing the following output:
```
bcm2835_smi_dev     16384   0
bcm2835_smi         20480   1   bcm2835_smi_dev
```

If you do not see these modules, please check:
1. The CaribouLite is properly mounted and powered.
2. The CAribouLite is properly flashed - follow [this link](../../../../docs/flashing/README.md)

For detailed hardware-related information on the SMI interface, please click [here](../../../../docs/smi/README.md)

# The API
## Initializing and closing the SMI instance

**Opening** the device and configuring the error-callback-function to be triggered once an error ocurred:
```
int caribou_smi_init(caribou_smi_st* dev,
                    caribou_smi_error_callback error_cb,
                    void* context);
```

**Closing** the device:
```
int caribou_smi_close (caribou_smi_st* dev);
```

## Reading and writing into the device

**Reading** into a buffer with timeout. If the SMI connected device (in this case, CaribouLite) doesn't have enough data to send, this function will wait till the timeout elapses. For non-blocking operation, please set `timeout = 0`
```
int caribou_smi_timeout_read(caribou_smi_st* dev,
                            caribou_smi_address_en source,
                            char* buffer,
                            int size_of_buf,
                            int timeout_num_millisec);
```

```
writing - TBD
```

## Stream operations
In most cases, working with file read/write synchronously is not the right choice. If we want an asynchronous operation with events and callbacks, we should setup a stream as follows:
```
int caribou_smi_setup_stream(caribou_smi_st* dev,
                                caribou_smi_stream_type_en type,
                                caribou_smi_channel_en channel,
                                int batch_length, int num_buffers,
                                caribou_smi_data_callback cb,
                                void* serviced_context);
```
`batch_length` - is the length of a single buffer to serve (in bytes)
`num_buffers` - is the number of batch buffers to allocate - for fluent buffer swapping
`cb` - data callback function that is triggered every time a buffer is ready to be served.
`serviced_context` - the data "requester" that is being serviced by the I/Q data. In most cases that is a higher layered driver / API.
The returned integer is the stream ID used for further operations.

Notes: Once the stream is created it is operational **but paused!** to activate it use the following function (on the specific stream ID). This function is used also for pausing the stream (run = 0).
```
int caribou_smi_run_pause_stream (caribou_smi_st* dev, int id, int run);
```
Gracefully disposing the stream is done using the "destroy" function
```
int caribou_smi_destroy_stream(caribou_smi_st* dev, int id);
```
