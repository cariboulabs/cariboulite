#include <arpa/inet.h>
#include <time.h>
#include "io_utils.h"


// DEFINITIONS
#define BCM2708_PERI_BASE_DEFAULT   0x20000000
#define BCM2709_PERI_BASE_DEFAULT   0x3f000000
#define BCM2710_PERI_BASE_DEFAULT   0x3f000000
#define BCM2711_PERI_BASE_DEFAULT   0xfe000000
#define GPIO_BASE_OFFSET            0x200000
#define FSEL_OFFSET                 0   // 0x0000
#define SET_OFFSET                  7   // 0x001c / 4
#define CLR_OFFSET                  10  // 0x0028 / 4
#define PINLEVEL_OFFSET             13  // 0x0034 / 4
#define EVENT_DETECT_OFFSET         16  // 0x0040 / 4
#define RISING_ED_OFFSET            19  // 0x004c / 4
#define FALLING_ED_OFFSET           22  // 0x0058 / 4
#define HIGH_DETECT_OFFSET          25  // 0x0064 / 4
#define LOW_DETECT_OFFSET           28  // 0x0070 / 4
#define PULLUPDN_OFFSET             37  // 0x0094 / 4
#define PULLUPDNCLK_OFFSET          38  // 0x0098 / 4
#define PULLUPDN_OFFSET_2711_0      57
#define PULLUPDN_OFFSET_2711_1      58
#define PULLUPDN_OFFSET_2711_2      59
#define PULLUPDN_OFFSET_2711_3      60
#define PAGE_SIZE  (4*1024)
#define BLOCK_SIZE (4*1024)

// STATIC VARIABLES
static uint32_t *gpio_map;
static int lgpio_handle;
static char *io_utils_gpio_mode_strs[] = {"IN","OUT","ALT5","ALT4","ALT0","ALT1","ALT2","ALT3"};

// STATIC FUNCTIONS
#define IO_UTILS_SHORT_WAIT(N)   {for (int i=0; i<(N); i++) { asm volatile("nop"); }}

//=============================================================================================
int io_utils_setup()
{
   int mem_fd;
   uint8_t *gpio_mem;
   uint32_t peri_base = 0;
   uint32_t gpio_base;
   uint8_t ranges[12] = { 0 };
   uint8_t rev[4] = { 0 };
   uint32_t cpu = 0;
   FILE *fp;
   char buffer[1024];
   char hardware[1024];
   int found = 0;
   int gpio_chip = 0;

    // open lgpio handle
   lgpio_handle = lgGpiochipOpen(gpio_chip);
   if (lgpio_handle<0)
   {
      printf("Error @ io_utils_setup: lgpio opening failed\n");
      return -1;
   }
   printf("Info @ io_utils_setup: opened lgpio chip #%d, handle: %d\n", gpio_chip, lgpio_handle);

   // try /dev/gpiomem first - this does not require root privs
   if ((mem_fd = open("/dev/gpiomem", O_RDWR|O_SYNC)) > 0)
   {
      if ((gpio_map = (uint32_t *)mmap(NULL, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, 0)) == MAP_FAILED)
      {
         printf("Error @ io_utils_setup: memory map failed\n");
         lgGpiochipClose(lgpio_handle);
         return -1;
      }
      return 0;
   }

   // revert to /dev/mem method - requires root privileges

   if ((fp = fopen("/proc/device-tree/soc/ranges", "rb")) != NULL)
   {
      // get peri base from device tree
      if (fread(ranges, 1, sizeof(ranges), fp) >= 8)
      {
         peri_base = ranges[4] << 24 | ranges[5] << 16 | ranges[6] << 8 | ranges[7] << 0;
         if (!peri_base)
         {
               peri_base = ranges[8] << 24 | ranges[9] << 16 | ranges[10] << 8 | ranges[11] << 0;
         }
      }
      if ((ranges[0] != 0x7e) ||
         (ranges[1] != 0x00) ||
         (ranges[2] != 0x00) ||
         (ranges[3] != 0x00) ||
         ((peri_base != BCM2708_PERI_BASE_DEFAULT) &&
            (peri_base != BCM2709_PERI_BASE_DEFAULT) &&
            (peri_base != BCM2711_PERI_BASE_DEFAULT)))
      {
         // invalid ranges file
         peri_base = 0;
      }
      fclose(fp);
   }

   // guess peri_base based on /proc/device-tree/system/linux,revision
   if (!peri_base)
   {
      if ((fp = fopen("/proc/device-tree/system/linux,revision", "rb")) != NULL)
      {
         if (fread(rev, 1, sizeof(rev), fp) == 4)
         {
            cpu = (rev[2] >> 4) & 0xf;
            switch (cpu)
            {
               case 0 : peri_base = BCM2708_PERI_BASE_DEFAULT;
                        break;
               case 1 :
               case 2 : peri_base = BCM2709_PERI_BASE_DEFAULT;
                        break;
               case 3 : peri_base = BCM2711_PERI_BASE_DEFAULT;
                        break;
            }
         }
         fclose(fp);
      }
   }

   // guess peri_base based on /proc/cpuinfo hardware field
   if (!peri_base)
   {
      if ((fp = fopen("/proc/cpuinfo", "r")) == NULL)
      {
         printf("Error @ io_utils_setup: cpu info retreival failed\n");
         lgGpiochipClose(lgpio_handle);
         return -1;
      }

      while(!feof(fp) && !found && fgets(buffer, sizeof(buffer), fp))
      {
         sscanf(buffer, "Hardware	: %s", hardware);
         if (strcmp(hardware, "BCM2708") == 0 || strcmp(hardware, "BCM2835") == 0)
         {
            // pi 1 hardware
            peri_base = BCM2708_PERI_BASE_DEFAULT;
         }
         else if (strcmp(hardware, "BCM2709") == 0 || strcmp(hardware, "BCM2836") == 0)
         {
            // pi 2 hardware
            peri_base = BCM2709_PERI_BASE_DEFAULT;
         }
         else if (strcmp(hardware, "BCM2710") == 0 || strcmp(hardware, "BCM2837") == 0)
         {
            // pi 3 hardware
            peri_base = BCM2710_PERI_BASE_DEFAULT;
         }
         else if (strcmp(hardware, "BCM2711") == 0)
         {
            // pi 4 hardware
            peri_base = BCM2711_PERI_BASE_DEFAULT;
         }
      }
      fclose(fp);
   }

   if (!peri_base)
   {
      printf("Error @ io_utils_setup: no peripheral address found\n");
      lgGpiochipClose(lgpio_handle);
      return -1;
   }

   gpio_base = peri_base + GPIO_BASE_OFFSET;

   // mmap the GPIO memory registers
   if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0)
   {
      printf("Error @ io_utils_setup: opening /dev/mem failed\n");
      lgGpiochipClose(lgpio_handle);
      return -1;
   }

   if ((gpio_mem = malloc(BLOCK_SIZE + (PAGE_SIZE-1))) == NULL)
   {
      printf("Error @ io_utils_setup: malloc of gpio_mem failed\n");
      lgGpiochipClose(lgpio_handle);
      return -1;
   }

   if ((uint32_t)gpio_mem % PAGE_SIZE)
   {
      gpio_mem += PAGE_SIZE - ((uint32_t)gpio_mem % PAGE_SIZE);
   }

   if ((gpio_map = (uint32_t *)mmap( (void *)gpio_mem, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, mem_fd, gpio_base)) == MAP_FAILED)
   {
      printf("Error @ io_utils_setup: memory mapping /dev/mem failed\n");
      lgGpiochipClose(lgpio_handle);
      return -1;
   }

   return 0;
}

//=============================================================================================
void io_utils_cleanup()
{
   lgGpiochipClose(lgpio_handle);
   munmap((void *)gpio_map, BLOCK_SIZE);
}

//=============================================================================================
void io_utils_set_pullupdn(int gpio, io_utils_pull_en pud)
{
    // Check GPIO register
    int is2711 = *(gpio_map + PULLUPDN_OFFSET_2711_3) != 0x6770696f;
    if (is2711) {
        // Pi 4 Pull-up/down method
        int pullreg = PULLUPDN_OFFSET_2711_0 + (gpio >> 4);
        int pullshift = (gpio & 0xf) << 1;
        unsigned int pullbits;
        unsigned int pull = 0;

        switch (pud)
        {
            case io_utils_pull_off:  pull = 0; break;
            case io_utils_pull_up:   pull = 1; break;
            case io_utils_pull_down: pull = 2; break;
            default: pull = 0; // switch PUD to OFF for other values
        }

        pullbits = *(gpio_map + pullreg);
        pullbits &= ~(3 << pullshift);
        pullbits |= (pull << pullshift);
        *(gpio_map + pullreg) = pullbits;
    }
    else
    {
        // Legacy Pull-up/down method
        int clk_offset = PULLUPDNCLK_OFFSET + (gpio/32);
        int shift = (gpio%32);

        if (pud == io_utils_pull_down) {
            *(gpio_map+PULLUPDN_OFFSET) = (*(gpio_map+PULLUPDN_OFFSET) & ~3) | io_utils_pull_down;
        } else if (pud == io_utils_pull_up) {
            *(gpio_map+PULLUPDN_OFFSET) = (*(gpio_map+PULLUPDN_OFFSET) & ~3) | io_utils_pull_up;
        } else  { // pud == io_utils_pull_off
            *(gpio_map+PULLUPDN_OFFSET) &= ~3;
        }
        IO_UTILS_SHORT_WAIT(150);
        *(gpio_map+clk_offset) = 1 << shift;
        IO_UTILS_SHORT_WAIT(150);
        *(gpio_map+PULLUPDN_OFFSET) &= ~3;
        *(gpio_map+clk_offset) = 0;
    }
}

//=============================================================================================
void io_utils_setup_gpio(int gpio, io_utils_dir_en direction, io_utils_pull_en pud)
{
    int offset = FSEL_OFFSET + (gpio/10);
    int shift = (gpio%10)*3;

    io_utils_set_pullupdn(gpio, pud);
    if (direction == io_utils_dir_output)
    {
        *(gpio_map+offset) = (*(gpio_map+offset) & ~(7<<shift)) | (1<<shift);
    }
    else  // direction == io_utils_dir_input
    {
        *(gpio_map+offset) = (*(gpio_map+offset) & ~(7<<shift));
    }
}

//=============================================================================================
char* io_utils_get_alt_from_mode(io_utils_alt_en mode)
{
    return io_utils_gpio_mode_strs[mode];
}

//=============================================================================================
int io_utils_get_gpio_mode(int gpio, int print)
{
    int offset = FSEL_OFFSET + (gpio / 10);
    int value = *(gpio_map + offset);

    value >>= ( ( gpio % 10 ) * 3 );
    value &= 7;
    if (print) printf("GPIO #%d, mode: %s (%d)\n", gpio, io_utils_gpio_mode_strs[value], value);
    return value;
}

//=============================================================================================
void io_utils_set_gpio_mode(int gpio, io_utils_alt_en mode)
{
    volatile uint32_t *reg = gpio_map + FSEL_OFFSET + (gpio / 10);
    volatile uint32_t shift = (gpio % 10) * 3;
    *reg = (*reg & ~(7 << shift)) | (mode << shift);
}

//=============================================================================================
void io_utils_write_gpio(int gpio, int value)
{
    int offset;

    if (value) // value == 1
    {
        offset = SET_OFFSET + (gpio/32);
    }
    else
    {
       offset = CLR_OFFSET + (gpio/32);
    }

    *(gpio_map+offset) = 1 << (gpio%32);
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
int io_utils_read_gpio(int gpio)
{
   int offset, value, mask;

   offset = PINLEVEL_OFFSET + (gpio/32);
   mask = (1 << gpio%32);
   value = *(gpio_map+offset) & mask;
   return (value != 0);
}

//=============================================================================================
void io_utils_usleep(int usec)
{
   struct timespec req = {.tv_sec = 0, .tv_nsec = usec * 1000L};
   nanosleep(&req, (struct timespec *)NULL);
}

//=============================================================================================
// edge is one of: LG_RISING_EDGE / LG_FALLING_EDGE / LG_BOTH_EDGES
int io_utils_setup_interrupt( int gpio,
                              int edge,
                              lgGpioAlertsFunc_t cb,
                              void* context)
{
   int e = 0;
   printf("Info @ io_utils_setup_interrupt: LGPIO handle = %d\n", lgpio_handle);
   if ((e = lgGpioSetAlertsFunc(lgpio_handle, gpio, cb, context)) < 0)
   {
      printf("Error @ io_utils_setup_interrupt: 'lgGpioSetAlertsFunc' %s (%d)\n", lguErrorText(e), e);
      return -1;
   }
   if ((e = lgGpioClaimAlert(lgpio_handle, 0, edge, gpio, -1)) < 0)
   {
      printf("Error @ io_utils_setup_interrupt: 'lgGpioClaimAlert' %s (%d)\n", lguErrorText(e), e);
      return -1;
   }

   char edge_st[10] = "rising";
   if (edge == LG_FALLING_EDGE) strcpy(edge_st, "falling");
   else if (edge == LG_BOTH_EDGES) strcpy(edge_st, "both");
   printf("Info @ io_utils_setup_interrupt: set up an interrupt on GPIO%d, %s edge\n",
                  gpio, edge_st);

   return 0;
}
