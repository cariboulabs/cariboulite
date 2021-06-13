#ifndef __IO_UTILS_SYS_INFO_H__
#define __IO_UTILS_SYS_INFO_H__

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

typedef struct
{
   int p1_revision;
   char *ram;
   char *manufacturer;
   char *processor;
   char *type;
   char revision[1024];
} io_utils_sys_info_st;

int io_utils_get_rpi_info(io_utils_sys_info_st *info);
void io_utils_print_rpi_info(io_utils_sys_info_st *info);


#endif // __IO_UTILS_SYS_INFO_H__
