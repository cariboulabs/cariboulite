#ifndef __IO_UTILS_H__
#define __IO_UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "pigpio/pigpio.h"

typedef enum
{
    io_utils_pull_off = 0,
    io_utils_pull_down = 1,
    io_utils_pull_up = 2,
} io_utils_pull_en;

typedef enum
{
    io_utils_dir_input = 0,
    io_utils_dir_output = 1,
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

int io_utils_setup(pigpioSigHandler sigHandler);
void io_utils_cleanup();
void io_utils_set_pullupdn(int gpio, io_utils_pull_en pud);
void io_utils_setup_gpio(int gpio, io_utils_dir_en direction, io_utils_pull_en pud);
int io_utils_get_gpio_mode(int gpio, int print);
void io_utils_set_gpio_mode(int gpio, io_utils_alt_en mode);
void io_utils_write_gpio(int gpio, int value);
void io_utils_write_gpio_with_wait(int gpio, int value, int nopcnt);
int io_utils_wait_gpio_state(int gpio, int state, int cnt);
int io_utils_read_gpio(int gpio);
char* io_utils_get_alt_from_mode(io_utils_alt_en mode);
int io_utils_setup_interrupt( int gpio,
                              gpioAlertFuncEx_t cb,
                              void* context);
void io_utils_usleep(int usec);

#ifdef __cplusplus
}
#endif

#endif // __IO_UTILS_H__