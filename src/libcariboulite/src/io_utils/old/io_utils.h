#ifndef __IO_UTILS_H__
#define __IO_UTILS_H__

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <lgpio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

typedef enum
{
    io_utils_pull_off = 0,
    io_utils_pull_down = 1,
    io_utils_pull_up = 2,
} io_utils_pull_en;

typedef enum
{
    io_utils_dir_output = 0,
    io_utils_dir_input = 1,
} io_utils_dir_en;

typedef enum
{
    io_utils_alt_gpio_in = 0,
    io_utils_alt_gpio_out = 1,
    io_utils_alt_0 = 4,
    io_utils_alt_1 = 5,
    io_utils_alt_2 = 6,
    io_utils_alt_3 = 7,
    io_utils_alt_4 = 3,
    io_utils_alt_5 = 2,
} io_utils_alt_en;

int io_utils_setup();
void io_utils_cleanup();
void io_utils_set_pullupdn(int gpio, io_utils_pull_en pud);
void io_utils_setup_gpio(int gpio, io_utils_dir_en direction, io_utils_pull_en pud);
int io_utils_get_gpio_mode(int gpio, int print);
void io_utils_set_gpio_mode(int gpio, io_utils_alt_en mode);
void io_utils_write_gpio(int gpio, int value);
void io_utils_write_gpio_with_wait(int gpio, int value, int nopcnt);
int io_utils_read_gpio(int gpio);
char* io_utils_get_alt_from_mode(io_utils_alt_en mode);
int io_utils_setup_interrupt( int gpio,
                              int edge,
                              lgGpioAlertsFunc_t cb,
                              void* context);
void io_utils_usleep(int usec);


#endif // __IO_UTILS_H__