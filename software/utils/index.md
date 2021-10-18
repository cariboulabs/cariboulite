# Utilities

## `generate_bin_blob.c`
converts a binary file into C/C++ h-file code with versioning and dating. We use this converter to embed the device-tree overlays binaries and FPGA firmware binaries directly into the library code.
In such way, the lib is self contained and doesn't have floating around dependencies.

Usage:
```
$ ./generate_bin_blob input_file.bin variable_name output_file.h
```

**The output file will have the following structure:**

```
#ifndef __cariboulite_dtbo_h__
#define __cariboulite_dtbo_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <time.h>

/*
 * Time tagging of the module through the 'struct tm' structure 
 *     Date: 2021-09-17
 *     Time: 09:56:44
 */
struct tm cariboulite_dtbo_date_time = {
   .tm_sec = 44,
   .tm_min = 56,
   .tm_hour = 9,
   .tm_mday = 17,
   .tm_mon = 8,   /* +1    */
   .tm_year = 121,  /* +1900 */
};

/*
 * Data blob of variable cariboulite_dtbo:
 *     Size: 1112 bytes
 *     Original filename: ./cariboulite.dtbo
 */
uint8_t cariboulite_dtbo[] = {
	0xD0, 0x0D, 0xFE, 0xED, 0x00, 0x00, 0x04, 0x58, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x03, 0xDC, 
	0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 
    ...
    	0x6D, 0x69, 0x00, 0x67, 0x70, 0x69, 0x6F, 0x00, 
};

#ifdef __cplusplus
}
#endif

#endif // __cariboulite_dtbo_h__
```

