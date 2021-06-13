#include <time.h>
#include "pigpio/pigpio.h"
#include "io_utils.h"


// DEFINITIONS

// STATIC VARIABLES
static uint32_t *gpio_map;
static char *io_utils_gpio_mode_strs[] = {"IN","OUT","ALT5","ALT4","ALT0","ALT1","ALT2","ALT3"};

// STATIC FUNCTIONS
#define IO_UTILS_SHORT_WAIT(N)   {for (int i=0; i<(N); i++) { asm volatile("nop"); }}

//=============================================================================================
int io_utils_setup()
{
   printf("Info @ io_utils_setup: initializing pigpio\n");
   int status = gpioInitialise();
   if (status < 0)
   {
      printf("Error @ io_utils_setup: initializing pigpio failed\n");
      return -1;
   }
   printf("Info @ io_utils_setup: pigpio version %d\n", status);
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
      printf("Error @ io_utils_set_gpio_mode\n");
   }
}

//=============================================================================================
inline void io_utils_write_gpio(int gpio, int value)
{
   if (gpioWrite(gpio, value) < 0)
   {
      printf("Error @ io_utils_write_gpio\n");
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
