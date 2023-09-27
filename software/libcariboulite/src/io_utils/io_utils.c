#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "IO_UTILS_Main"

#include <time.h>
//#include "pigpio/pigpio.h"
#include "zf_log/zf_log.h"
#include "io_utils.h"


// DEFINITIONS

// STATIC VARIABLES
static char *io_utils_gpio_mode_strs[] = {"IN","OUT","ALT5","ALT4","ALT0","ALT1","ALT2","ALT3"};

// STATIC FUNCTIONS
#define IO_UTILS_SHORT_WAIT(N)   {for (int i=0; i<(N); i++) { asm volatile("nop"); }}

//=============================================================================================
int io_utils_setup()
{
    ZF_LOGD("initializing rpi");

    rpi_init(0);

    return 0;
}

//=============================================================================================
void io_utils_cleanup()
{
    rpi_close();
}

//=============================================================================================
inline void io_utils_set_pullupdn(int gpio, io_utils_pull_en pud)
{
    gpio_enable_pud(gpio, pud);
}

//=============================================================================================
inline void io_utils_setup_gpio(int gpio, io_utils_dir_en direction, io_utils_pull_en pud)
{
    io_utils_set_pullupdn(gpio, pud);
    io_utils_set_gpio_mode(gpio, (io_utils_alt_en)direction);
}

//=============================================================================================
char* io_utils_get_alt_from_mode(io_utils_alt_en mode)
{
    return io_utils_gpio_mode_strs[mode];
}

//=============================================================================================
inline int io_utils_get_gpio_mode(int gpio, int print)
{   
    int value = get_gpio_config(gpio);

    if (print) printf("GPIO #%d, mode: %s (%d)\n", gpio, io_utils_gpio_mode_strs[value], value);
    return value;
}

//=============================================================================================
inline void io_utils_set_gpio_mode(int gpio, io_utils_alt_en mode)
{
    gpio_config(gpio, mode);
}

//=============================================================================================
inline void io_utils_write_gpio(int gpio, int value)
{
    gpio_write(gpio, value);
}

//=============================================================================================
void io_utils_write_gpio_with_wait(int gpio, int value, int nopcnt)
{
    io_utils_write_gpio(gpio, value);
    for (volatile int i = 0; i < nopcnt; i++)
    {
        __asm("nop");
    }
}

//=============================================================================================
int io_utils_wait_gpio_state(int gpio, int state, int cnt)
{
    while(io_utils_read_gpio(gpio) == !state && cnt--)
    {
        io_utils_usleep(100000);
    }
    if (cnt <= 0)
    {
        return -1;
    }
    return 0;
}

//=============================================================================================
inline int io_utils_read_gpio(int gpio)
{
    return gpio_read(gpio);
}

//=============================================================================================
void io_utils_usleep(int usec)
{
    struct timespec req = {.tv_sec = 0, .tv_nsec = usec * 1000L};
    nanosleep(&req, (struct timespec *)NULL);
}

//=============================================================================================
inline int io_utils_setup_interrupt(int gpio,
                                    gpioAlertFuncEx_t cb,
                                    void* context)
{
   return 0; //gpioSetAlertFuncEx(gpio, cb, context);
}
