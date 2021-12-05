#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "IO_UTILS_Main"

#include <time.h>
#include "pigpio/pigpio.h"
#include "zf_log/zf_log.h"
#include "io_utils.h"


// DEFINITIONS

// STATIC VARIABLES
static uint32_t *gpio_map;
static char *io_utils_gpio_mode_strs[] = {"IN","OUT","ALT5","ALT4","ALT0","ALT1","ALT2","ALT3"};

// STATIC FUNCTIONS
#define IO_UTILS_SHORT_WAIT(N)   {for (int i=0; i<(N); i++) { asm volatile("nop"); }}

//=============================================================================================
int io_utils_setup(pigpioSigHandler sigHandler)
{
   ZF_LOGI("initializing pigpio");
   gpioCfgInterfaces(PI_DISABLE_FIFO_IF|PI_DISABLE_SOCK_IF|PI_LOCALHOST_SOCK_IF);
   int status = gpioInitialise();
   if (status < 0)
   {
      ZF_LOGE("initializing pigpio failed");
      return -1;
   }
   ZF_LOGI("pigpio version %d", status);

   if (sigHandler != NULL)
   {
      change_sigaction_unhandled_condition(sigHandler);
   }
   return 0;
}

//=============================================================================================
void io_utils_cleanup()
{
   gpioTerminate();
}

//=============================================================================================
inline void io_utils_set_pullupdn(int gpio, io_utils_pull_en pud)
{
   gpioSetPullUpDown(gpio, pud);
}

//=============================================================================================
inline void io_utils_setup_gpio(int gpio, io_utils_dir_en direction, io_utils_pull_en pud)
{
   io_utils_set_pullupdn(gpio, pud);
   io_utils_set_gpio_mode(gpio, direction);
}

//=============================================================================================
char* io_utils_get_alt_from_mode(io_utils_alt_en mode)
{
   return io_utils_gpio_mode_strs[mode];
}

//=============================================================================================
inline int io_utils_get_gpio_mode(int gpio, int print)
{
   int value = gpioGetMode(gpio);

   if (print) printf("GPIO #%d, mode: %s (%d)\n", gpio, io_utils_gpio_mode_strs[value], value);
   return value;
}

//=============================================================================================
inline void io_utils_set_gpio_mode(int gpio, io_utils_alt_en mode)
{
   if (gpioSetMode(gpio, mode) < 0)
   {
      ZF_LOGE("couldn't set mode for gpio %d", gpio);
   }
}

//=============================================================================================
inline void io_utils_write_gpio(int gpio, int value)
{
   if (gpioWrite(gpio, value) < 0)
   {
      ZF_LOGE("couldn't write to gpio %d", gpio);
   }
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
inline int io_utils_read_gpio(int gpio)
{
   return gpioRead(gpio);
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
   return gpioSetAlertFuncEx(gpio, cb, context);
}
