#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "IO_UTILS_SPI"

#include <pthread.h>
#include <time.h>
#include <errno.h>

#include "zf_log/zf_log.h"
#include "io_utils_spi.h"
#include "io_utils.h"
#include "spidev/spi.h"

static char *io_utils_chip_types[] =
        {
            "fpga communication icd",
            "mixer - rffc507x / rffc207x",
            "modem - at86rf215",
            "lattice ice40 programmer",
            "modem - at86rf215 - bitbanged",
        };

//=====================================================================================
static int io_utils_spi_setup_chip(io_utils_spi_st* dev, int handle)
{
	if (handle >= IO_UTILS_MAX_CHIPS)
	{
		ZF_LOGE("chip handle illegal %d", handle);
		return -1;
	}

    io_utils_spi_chip_st* chip = &dev->chips[handle];
	if (!chip->initialized)
	{
		ZF_LOGE("chip handle %d is not initialized", handle);
		return -1;
	}

    if (dev->current_chip == chip)
    {
        // nothing to setup => return
        return 0;
    }
    
    if (dev->chips[handle].chip_type == io_utils_spi_chip_ice40_prog ||
        dev->chips[handle].chip_type == io_utils_spi_chip_type_rffc ||
        dev->chips[handle].chip_type == io_utils_spi_chip_type_modem_bitbang)
    {
        //printf("Info @ io_utils_spi_setup_chip: Switching SPI to GPIO mode\n");

        // ICE40 PROG
        int mosi_pin = chip->miso_mosi_swap?dev->miso:dev->mosi;
        int miso_pin = chip->miso_mosi_swap?dev->mosi:dev->miso;
        int cs_pin = chip->cs_pin;
        int sck_pin = dev->sck;
        io_utils_set_gpio_mode(cs_pin, io_utils_alt_gpio_out);
        io_utils_set_gpio_mode(miso_pin, io_utils_alt_gpio_in);
        io_utils_set_gpio_mode(mosi_pin, io_utils_alt_gpio_out);
        io_utils_set_gpio_mode(sck_pin, io_utils_alt_gpio_out);
        dev->current_chip = chip;
        return 0;
    }

    // here we have a generic SPI_DEV device
    // -------------------------------------
    int setup_spi_dev = 0;
    if (dev->current_chip == NULL )
    {
        setup_spi_dev = 1;
    }
    else if (dev->current_chip->chip_type == io_utils_spi_chip_ice40_prog ||
             dev->current_chip->chip_type == io_utils_spi_chip_type_rffc ||
             dev->current_chip->chip_type == io_utils_spi_chip_type_modem_bitbang )
    {
        setup_spi_dev = 1;
    }

    if (setup_spi_dev)
    {
        //printf("Info @ io_utils_spi_setup_chip: Switching SPI to hard_spi mode\n");
        // Setup the configuration of a regular spi_dev
        //io_utils_set_gpio_mode(chip->cs_pin, io_utils_alt_4);
        io_utils_set_gpio_mode(dev->miso, io_utils_alt_4);
        io_utils_set_gpio_mode(dev->mosi, io_utils_alt_4);
        io_utils_set_gpio_mode(dev->sck, io_utils_alt_4);
        io_utils_usleep(100);
    }

    return setup_spi_dev;
}

//=====================================================================================
static int io_utils_spi_write_rffc507x(io_utils_spi_st* dev, io_utils_spi_chip_st* chip, uint8_t reg, uint16_t val)
{
    int bits = 25;
    int nop_cnt = 200;
	int msb = 1 << (bits - 1);
    uint32_t data = reg;
	data = ((data & 0x7f) << 16) | val;

    //printf("==> io_utils_spi_write_rffc507x: %06X\n", data);

    int sdata_pin = chip->miso_mosi_swap?dev->miso:dev->mosi;
    int sclk_pin = dev->sck;
    int enx_pin = chip->cs_pin;

    // set SDATA line as output
    io_utils_setup_gpio(sdata_pin, io_utils_dir_output, io_utils_pull_down);

    // make sure everything is starting in the correct state
    io_utils_write_gpio_with_wait(enx_pin, 1, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);
    io_utils_write_gpio_with_wait(sdata_pin, 0, nop_cnt);

	/*
	 * The device requires two clocks while ENX is high before a serial
	 * transaction.  This is not clearly documented.
	 */
    io_utils_write_gpio_with_wait(sclk_pin, 1, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 1, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);

	// start transaction by bringing ENX low
    io_utils_write_gpio_with_wait(enx_pin, 0, nop_cnt);

    while (bits--)
	{
        io_utils_write_gpio_with_wait(sdata_pin, (data & msb)?1:0, nop_cnt);
		data <<= 1;
        io_utils_write_gpio_with_wait(sclk_pin, 1, nop_cnt);
        io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);
	}

	io_utils_write_gpio_with_wait(enx_pin, 1, nop_cnt);

	/*
	 * The device requires a clock while ENX is high after a serial
	 * transaction.  This is not clearly documented.
	 */
	io_utils_write_gpio_with_wait(sclk_pin, 1, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);
    return 0;
}

//=====================================================================================
static int io_utils_spi_read_rffc507x(io_utils_spi_st* dev, io_utils_spi_chip_st* chip, uint8_t reg)
{
	int bits = 9;
    int nop_cnt = 200;
	int msb = 1 << (bits -1);
	uint32_t data = 0x80 | (reg & 0x7f);

    int sdata_pin = chip->miso_mosi_swap?dev->miso:dev->mosi;
    int sclk_pin = dev->sck;
    int enx_pin = chip->cs_pin;

    // set SDATA line as output
    io_utils_setup_gpio(sdata_pin, io_utils_dir_output, io_utils_pull_down);

	// make sure everything is starting in the correct state
    io_utils_write_gpio_with_wait(enx_pin, 1, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);
    io_utils_write_gpio_with_wait(sdata_pin, 0, nop_cnt);

	/*
	 * The device requires two clocks while ENX is high before a serial
	 * transaction.  This is not clearly documented.
	 */
    io_utils_write_gpio_with_wait(sclk_pin, 1, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 1, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);

	// start transaction by bringing ENX low
    io_utils_write_gpio_with_wait(enx_pin, 0, nop_cnt);

	while (bits--)
	{
        io_utils_write_gpio_with_wait(sdata_pin, (data & msb)?1:0, nop_cnt);
		data <<= 1;
        io_utils_write_gpio_with_wait(sclk_pin, 1, nop_cnt);
        io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);
	}

    io_utils_write_gpio_with_wait(sclk_pin, 1, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);

	bits = 16;
	data = 0;

	// set SDATA line as input - TBD - check if pull is needed
    io_utils_setup_gpio(sdata_pin, io_utils_dir_input, io_utils_pull_down);

	while (bits--)
	{
		data <<= 1;

        io_utils_write_gpio_with_wait(sclk_pin, 1, nop_cnt);
        io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);
        data |= io_utils_read_gpio(sdata_pin) & 0x1;
	}

	// set SDATA line as output
    io_utils_setup_gpio(sdata_pin, io_utils_dir_output, io_utils_pull_down);

	io_utils_write_gpio_with_wait(enx_pin, 1, nop_cnt);

	/*
	 * The device requires a clock while ENX is high after a serial
	 * transaction.  This is not clearly documented.
	 */
	io_utils_write_gpio_with_wait(sclk_pin, 1, nop_cnt);
    io_utils_write_gpio_with_wait(sclk_pin, 0, nop_cnt);

    //printf("==>The read data is: %06X\n", data);

	return data;
}

//---------------------------------------------------------------------------
static int io_utils_ice40_transfer_spi(io_utils_spi_st* dev, io_utils_spi_chip_st* chip,
                                        const uint8_t *tx, unsigned int len)
{
    int nop_cnt = 400;
    int data_pin = chip->miso_mosi_swap?dev->miso:dev->mosi;
    int sck_pin = dev->sck;

    // in this case the chipselect is controlled outside due to
    // ice40 FPGA specifics

	for (unsigned int byte_num = 0; byte_num < len; byte_num++)
	{
		uint8_t current_tx_byte = tx[byte_num];

		for (int bit = 0; bit < 8; bit ++)
		{
            io_utils_write_gpio_with_wait(data_pin, (current_tx_byte&0x80)>>7, nop_cnt);

			current_tx_byte <<= 1;
            io_utils_write_gpio_with_wait(sck_pin, 1, nop_cnt);
            io_utils_write_gpio_with_wait(sck_pin, 0, nop_cnt);
		}
	}

    io_utils_write_gpio_with_wait(sck_pin, 0, nop_cnt / 2);

	return 0;
}

//---------------------------------------------------------------------------
static int io_utils_modem_bitbang_transfer_spi(io_utils_spi_st* dev, io_utils_spi_chip_st* chip,
                                                const uint8_t *tx, uint8_t *rx, unsigned int len)
{
    int nop_cnt = 200;
    int cs_pin = chip->cs_pin;
    int mosi_pin = chip->miso_mosi_swap?dev->miso:dev->mosi;
    int miso_pin = chip->miso_mosi_swap?dev->mosi:dev->miso;
    int sck_pin = dev->sck;

    io_utils_write_gpio_with_wait(cs_pin, 0, nop_cnt);

	for (unsigned int byte_num = 0; byte_num < len; byte_num++)
	{
		uint8_t current_tx_byte = tx[byte_num];
        uint8_t rx_byte = 0;
        int bit = 8;

		while (bit--)
		{
            io_utils_write_gpio_with_wait(mosi_pin, (current_tx_byte&0x80)>>7, nop_cnt);

			current_tx_byte <<= 1;
            io_utils_write_gpio_with_wait(sck_pin, 1, nop_cnt);
            rx_byte <<= 1;
            rx_byte |= io_utils_read_gpio(miso_pin);
            io_utils_write_gpio_with_wait(sck_pin, 0, nop_cnt/2);
		}
        rx[byte_num] = rx_byte;
	}

    io_utils_write_gpio(sck_pin, 0);
    io_utils_write_gpio_with_wait(cs_pin, 1, nop_cnt/2);

	return 0;
}

//=====================================================================================
static int spi_init_impl(io_utils_spi_st* dev)
{
    if (dev == NULL)
    {
        ZF_LOGE("dev is NULL");
        return -1;
    }
    if (dev->initialized == 1)
    {
        ZF_LOGW("spi_dev already initialized");
        return 0;
    }

    // init the chip list
	memset (dev->chips, 0, sizeof(dev->chips));
	dev->num_of_chips = 0;
	dev->current_chip = NULL;

    // initialize the hard handles
    for (int i = 0; i < IO_UTILS_MAX_CHIPS; i++)
    {
        dev->chips[i].is_hard_spi = 0;
        dev->chips[i].initialized = 0;
    }

    // Initialize an unlocked mutex.
    if (pthread_mutex_init(&dev->mtx, NULL) != 0)
    {
        ZF_LOGE("mutex init failed");
        return -1;
    }

    ZF_LOGD("configuring gpio setups");

    io_utils_set_gpio_mode(dev->miso, io_utils_alt_4);
    io_utils_set_gpio_mode(dev->mosi, io_utils_alt_4);
    io_utils_set_gpio_mode(dev->sck, io_utils_alt_4);

	dev->initialized = 1;
    return 0;
}

//=====================================================================================
static int spi_close_impl(io_utils_spi_st* dev)
{
    if (dev == NULL || !dev->initialized)
    {
        ZF_LOGE("closing uninitialized device");
        return -1;
    }

    // The lifecycle writer lock excludes API users and new entrants.
    int ret = pthread_mutex_lock(&dev->mtx);
    if (ret != 0) return -1;
    dev->initialized = 0;

    // now terminate all used spi channels
    for (int i = 0; i < IO_UTILS_MAX_CHIPS; i++)
    {
        if (dev->chips[i].initialized && dev->chips[i].is_hard_spi)
        {
            spi_free(&dev->chips[i].hard_dev.spidev);
        }
        dev->chips[i].initialized = 0;
    }

    memset (dev->chips, 0, sizeof(dev->chips));
	dev->num_of_chips = 0;
	dev->current_chip = NULL;

    pthread_mutex_unlock(&dev->mtx);
    return pthread_mutex_destroy(&dev->mtx) == 0 ? 0 : -1;
}

//=====================================================================================
static int spi_add_chip_impl(io_utils_spi_st* dev, int cs_pin, int speed, int swap_mi_mo, int mode,
                            io_utils_spi_chip_type_en chip_type, io_utils_hard_spi_st *hard_dev)
{
    int res = -1;
    if (dev == NULL || !dev->initialized)
    {
        ZF_LOGE("uninitialized device");
        return -1;
    }

    // lock the resource - no concurrent changes
    pthread_mutex_lock(&dev->mtx);

    // will never be greater but still it is good to check
    if (dev->num_of_chips >= IO_UTILS_MAX_CHIPS)
    {
        ZF_LOGE("cannot add - exceeded max %d", IO_UTILS_MAX_CHIPS);
        pthread_mutex_unlock(&dev->mtx);
        return -1;
    }

    int i = 0;
    // find a new slot for the new device
    for (i = 0; i < IO_UTILS_MAX_CHIPS; i++)
    {
        if (dev->chips[i].initialized == 0)
        {
            // due to the fact that we already checked number of
            // active chips, we should find an empty slot somewhere
            break;
        }
    }
    int new_chip_index = i;
    dev->chips[new_chip_index].cs_pin = cs_pin;
    dev->chips[new_chip_index].miso_mosi_swap = swap_mi_mo;
    dev->chips[new_chip_index].chip_type = chip_type;
    dev->chips[new_chip_index].is_hard_spi = 0;

    // now lets check if we need a hard spi handle (not a bitbanged configuration)
    if (chip_type == io_utils_spi_chip_type_fpga_comm ||
        chip_type == io_utils_spi_chip_type_modem)
    {
        memcpy (&dev->chips[new_chip_index].hard_dev, hard_dev, sizeof(io_utils_hard_spi_st));
        char spi_device_file[32];
        sprintf(spi_device_file, "/dev/spidev%d.%d", hard_dev->spi_dev_id, hard_dev->spi_dev_channel);
        
        res = spi_init(&dev->chips[new_chip_index].hard_dev.spidev,
             spi_device_file,       // filename like "/dev/spidev0.0"
             mode,                  // SPI_* (look "linux/spi/spidev.h")
             0,                     // bits per word (usually 8)
             speed);                // max speed [Hz]
        if (res < 0)
        {
            ZF_LOGE("spi_init function failed with code %d, (%s)", res, spi_get_code_desc(res));
            pthread_mutex_unlock(&dev->mtx);
            return -1;
        }
        
        dev->chips[new_chip_index].is_hard_spi = 1;
    }

    dev->chips[new_chip_index].initialized = 1;
    
    // finally increase the number of chips
    dev->num_of_chips += 1;

    pthread_mutex_unlock(&dev->mtx);

    return new_chip_index; // this is the chip handle for the app
}

//=====================================================================================
static int spi_suspend_impl(io_utils_spi_st* dev, bool suspend)
{
	ZF_LOGD("changing an spi device suspension = '%d' state", suspend);
	if (dev == NULL)
	{
		ZF_LOGE("provided SPI struct is NULL");
		return -1;
	}

	if (suspend)
	{
		io_utils_setup_gpio(dev->miso, io_utils_dir_input, io_utils_pull_off);
		io_utils_setup_gpio(dev->mosi, io_utils_dir_input, io_utils_pull_off);
		io_utils_setup_gpio(dev->sck, io_utils_dir_input, io_utils_pull_off);
	}
	else
	{
		dev->current_chip = NULL;
		io_utils_set_gpio_mode(dev->miso, io_utils_alt_4);
		io_utils_set_gpio_mode(dev->mosi, io_utils_alt_4);
		io_utils_set_gpio_mode(dev->sck, io_utils_alt_4);
	}

	return 0;
}

//=====================================================================================
static int spi_remove_chip_impl(io_utils_spi_st* dev, int chip_handle)
{
    ZF_LOGD("removing an spi device with handle %d", chip_handle);

    pthread_mutex_lock(&dev->mtx);
    if (dev->num_of_chips <= 0)
    {
        ZF_LOGE("the device is already empty");
        pthread_mutex_unlock(&dev->mtx);
        return -1;
    }

    if (dev->chips[chip_handle].initialized == 0)
    {
        ZF_LOGE("the specified handle - %d - is not initialized", chip_handle);
        pthread_mutex_unlock(&dev->mtx);
        return -1;
    }

    if (dev->chips[chip_handle].chip_type == io_utils_spi_chip_type_fpga_comm ||
        dev->chips[chip_handle].chip_type == io_utils_spi_chip_type_modem)
    {
        spi_free(&dev->chips[chip_handle].hard_dev.spidev);
    }
    dev->chips[chip_handle].initialized = 0;
    dev->num_of_chips -= 1;
    pthread_mutex_unlock(&dev->mtx);
    return 0;
}

//=====================================================================================
static int spi_transmit_impl(io_utils_spi_st* dev, int chip_handle,
							const unsigned char* tx_buf,
							unsigned char* rx_buf,
							size_t length,
                            io_utils_spi_dir_en dir)
{
    int ret = 0;
    if (dev == NULL || !dev->initialized)
    {
        ZF_LOGE("uninitialized device");
        return -1;
    }
    // Chip validation and transaction share the device lock with removal.
    pthread_mutex_lock(&dev->mtx);

    int set_up_hard = io_utils_spi_setup_chip(dev, chip_handle);
    if (set_up_hard < 0)
    {
        ZF_LOGE("chip setup failed %d", chip_handle);
        goto io_utils_spi_transmit_error;
    }

    dev->current_chip = &dev->chips[chip_handle];
    
    //printf("dev->current_chip->chip_type ====== %d\n", dev->current_chip->chip_type);

    switch (dev->current_chip->chip_type)
    {
        // --------------------------------------------------
        case io_utils_spi_chip_type_fpga_comm:
        case io_utils_spi_chip_type_modem:
        {
            //printf("SPI XFER chiptype = %d\n", dev->current_chip->chip_type);
            
            // a regular spi communication
            ret = spi_exchange(&dev->current_chip->hard_dev.spidev, (char*)rx_buf, (char*)tx_buf, length);
            if (ret < 0)
            {
                ZF_LOGE("spi transfer failed (%d)", ret);
                goto io_utils_spi_transmit_error;
            }
        }
        break;

        // --------------------------------------------------
        case io_utils_spi_chip_type_rffc:
        {
            uint8_t reg = tx_buf[0];
            if (dir == io_utils_spi_read)
            {
                int r = io_utils_spi_read_rffc507x(dev, dev->current_chip, reg);
                if (r < 0)
                {
                    ZF_LOGE("rffc507x read transfer failed");
                    goto io_utils_spi_transmit_error;
                }
                *((uint16_t*)rx_buf) = (uint16_t)(r & 0xFFFF);
            }
            else
            {
                uint16_t val = ((uint16_t)(tx_buf[2]))<<8 | tx_buf[1];
                //ZF_LOGI("rffc507x writing to reg %02X, data %04X", reg, val);
                int r = io_utils_spi_write_rffc507x(dev, dev->current_chip, reg, val);
                if (r < 0)
                {
                    ZF_LOGE("rffc507x write transfer failed");
                    goto io_utils_spi_transmit_error;
                }
            }
        }
        break;

        // --------------------------------------------------
        case io_utils_spi_chip_ice40_prog:
        {
            io_utils_ice40_transfer_spi(dev, dev->current_chip, tx_buf, length);
        }
        break;

        // --------------------------------------------------
        case io_utils_spi_chip_type_modem_bitbang:
        {
            io_utils_modem_bitbang_transfer_spi(dev, dev->current_chip, tx_buf, rx_buf, length);
        }
        break;

        // --------------------------------------------------
	    default:
        {
            ZF_LOGW("generic function transfer not implemented");
        }
        break;
    }

    pthread_mutex_unlock(&dev->mtx);
    return 0;

io_utils_spi_transmit_error:
    pthread_mutex_unlock(&dev->mtx);
    return -1;
}

//=====================================================================================
static void spi_print_setup_impl(io_utils_spi_st* dev)
{
    if (dev == NULL || !dev->initialized)
    {
        ZF_LOGD("uninitialized device");
        return;
    }

    pthread_mutex_lock(&dev->mtx);

    printf("  IO_UTILS_SPI Setup:\n");
    printf("    MISO Pin: %d\n", dev->miso);
    printf("    MOSI Pin: %d\n", dev->mosi);
    printf("    SCK Pin: %d\n", dev->sck);
    printf("    Number of chips: %d\n", dev->num_of_chips);

    for (int i = 0; i < IO_UTILS_MAX_CHIPS; i++)
    {
        if (!dev->chips[i].initialized) continue;

        printf("      CHIP handle: #%d\n", i);
        printf("        CS Pin: %d\n", dev->chips[i].cs_pin);
        printf("        CLK Speed: %d\n", dev->chips[i].clock);
        printf("        SPI Mode: %d\n", dev->chips[i].mode);
        printf("        MISO / MOSI swap: %d\n", dev->chips[i].miso_mosi_swap);
        printf("        Chip type: %s (%d)\n", io_utils_chip_types[dev->chips[i].chip_type],
                                                            dev->chips[i].chip_type);
        printf("        Is hard SPI: %d\n", dev->chips[i].is_hard_spi);
        if (dev->chips[i].is_hard_spi)
        {
            printf("            Hard spi id: %d\n", dev->chips[i].hard_dev.spi_dev_id);
            printf("            Hard spi channel: %d\n", dev->chips[i].hard_dev.spi_dev_channel);
        }
    }
    pthread_mutex_unlock(&dev->mtx);
}

/* Protect mutex lifetime without changing the public device layout. Calls on
 * different devices may run concurrently; init/close briefly exclude all SPI
 * calls. Lock order: lifecycle -> device mtx. No implementation calls wrappers.
 * Cancellation is deferred until both locks have been released.
 */
static pthread_rwlock_t spi_lifecycle = PTHREAD_RWLOCK_INITIALIZER;

static int spi_enter(bool exclusive, bool timed)
{
    if (!exclusive) return pthread_rwlock_rdlock(&spi_lifecycle);
    if (!timed) return pthread_rwlock_wrlock(&spi_lifecycle);
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) return errno;
    deadline.tv_sec += 1;
    return pthread_rwlock_timedwrlock(&spi_lifecycle, &deadline);
}

#define SPI_CALL(exclusive, timed, expression) do { \
    int previous_cancel; \
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous_cancel); \
    int lock_result = spi_enter(exclusive, timed); \
    int result = -1; \
    if (lock_result == 0) { \
        result = (expression); \
        pthread_rwlock_unlock(&spi_lifecycle); \
    } else { ZF_LOGE("SPI lifecycle lock failed (%d)", lock_result); } \
    pthread_setcancelstate(previous_cancel, NULL); \
    return result; \
} while (0)

int io_utils_spi_init(io_utils_spi_st* dev)
{ SPI_CALL(true, false, spi_init_impl(dev)); }
int io_utils_spi_close(io_utils_spi_st* dev)
{ SPI_CALL(true, true, spi_close_impl(dev)); }
int io_utils_spi_add_chip(io_utils_spi_st* dev, int cs_pin, int speed,
                         int swap, int mode, io_utils_spi_chip_type_en type,
                         io_utils_hard_spi_st* hard)
{ SPI_CALL(false, false, spi_add_chip_impl(dev, cs_pin, speed, swap, mode, type, hard)); }
int io_utils_spi_remove_chip(io_utils_spi_st* dev, int handle)
{ SPI_CALL(false, false, dev && dev->initialized && handle >= 0 &&
           handle < IO_UTILS_MAX_CHIPS ? spi_remove_chip_impl(dev, handle) : -1); }
int io_utils_spi_suspend(io_utils_spi_st* dev, bool suspend)
{ SPI_CALL(true, false, dev && dev->initialized ? spi_suspend_impl(dev, suspend) : -1); }
int io_utils_spi_transmit(io_utils_spi_st* dev, int handle,
                        const unsigned char* tx, unsigned char* rx,
                        size_t length, io_utils_spi_dir_en dir)
{ SPI_CALL(false, false, handle >= 0 && handle < IO_UTILS_MAX_CHIPS ?
           spi_transmit_impl(dev, handle, tx, rx, length, dir) : -1); }
void io_utils_spi_print_setup(io_utils_spi_st* dev)
{
    int previous_cancel;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous_cancel);
    if (spi_enter(false, false) == 0) {
        spi_print_setup_impl(dev);
        pthread_rwlock_unlock(&spi_lifecycle);
    }
    pthread_setcancelstate(previous_cancel, NULL);
}
#undef SPI_CALL
