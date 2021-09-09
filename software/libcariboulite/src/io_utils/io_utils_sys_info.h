#ifndef __IO_UTILS_SYS_INFO_H__
#define __IO_UTILS_SYS_INFO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

typedef enum
{
    io_utils_processor_BCM2835 = 0,
    io_utils_processor_BCM2836 = 1,
    io_utils_processor_BCM2837 = 2,
    io_utils_processor_BCM2711 = 3,
    io_utils_processor_UNKNOWN = 4,
} io_utils_processor_type_en;

typedef struct
{
   int p1_revision;
   char *ram;
   char *manufacturer;
   char *processor;
   char *type;
   char revision[1024];

   io_utils_processor_type_en processor_type;
   uint32_t ram_size_mbytes;
   uint32_t phys_reg_base;
   uint32_t sys_clock_hz;
   uint32_t bus_reg_base;
} io_utils_sys_info_st;

int io_utils_get_rpi_info(io_utils_sys_info_st *info);
void io_utils_print_rpi_info(io_utils_sys_info_st *info);

#ifdef __cplusplus
}
#endif

#endif // __IO_UTILS_SYS_INFO_H__
