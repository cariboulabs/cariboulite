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
    }

    return 0;
}

//=====================================================================================
static int io_utils_spi_write_rffc507x(io_utils_spi_st* dev, io_utils_spi_chip_st* chip, uint8_t reg, uint16_t val)
{
    int bits = 25;
    int nop_cnt = 50;
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
    int nop_cnt = 50;
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
    int nop_cnt = 300;
    int data_pin = chip->miso_mosi_swap?dev->miso:dev->mosi;
    int sck_pin = dev->sck;

    // in this case the chipselect is controlled outside due to
    // ice40 FPGA specifics

	for (int byte_num = 0; byte_num < len; byte_num++)
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
    int nop_cnt = 120;
    int cs_pin = chip->cs_pin;
    int mosi_pin = chip->miso_mosi_swap?dev->miso:dev->mosi;
    int miso_pin = chip->miso_mosi_swap?dev->mosi:dev->miso;
    int sck_pin = dev->sck;

    io_utils_write_gpio_with_wait(cs_pin, 0, nop_cnt);

	for (int byte_num = 0; byte_num < len; byte_num++)
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
int io_utils_spi_init(io_utils_spi_st* dev)
{
    if (dev == NULL)
    {
        ZF_LOGE("dev is NULL");
        return -1;
    }
    if (dev->initialized == 1)
    {
        ZF_LOGW("spi_dev already initialized");
    }

    // init the chip list
	memset (dev->chips, 0, sizeof(dev->chips));
	dev->num_of_chips = 0;
	dev->current_chip = NULL;

    // initialize the hard handles
    for (int i = 0; i < IO_UTILS_MAX_CHIPS; i++)
    {
        dev->chips[i].hard_spi_handle = -1;
        dev->chips[i].initialized = 0;
    }

    // Init mutex and unlock
    if (pthread_mutex_init(&dev->mtx, NULL) != 0)
    {
        ZF_LOGE("mutex init failed");
        return -1;
    }

    ZF_LOGI("configuring gpio setups");

    io_utils_set_gpio_mode(dev->miso, io_utils_alt_4);
    io_utils_set_gpio_mode(dev->mosi, io_utils_alt_4);
    io_utils_set_gpio_mode(dev->sck, io_utils_alt_4);

    pthread_mutex_unlock(&dev->mtx);

	dev->initialized = 1;
    return 0;
}

//=====================================================================================
int io_utils_spi_close(io_utils_spi_st* dev)
{
    if (dev == NULL || !dev->initialized)
    {
        ZF_LOGE("closing uninitialized device");
        return -1;
    }

    // first make sure nobody is using the resource - try to lock it with a timeout value
    // So we try to lock it for 1 second which should be more than enough for spi transaction
    // to finish. otherwise we trreminate it.
    struct timespec timeout = {.tv_sec = 1, .tv_nsec = 0};
    int ret = pthread_mutex_timedlock(&dev->mtx, &timeout);
    if (ret == -ETIMEDOUT)
    {
        // timeout locking - some thread is holding spi as a hostage
        ZF_LOGW("timed out trying locking mutex");
    }
    else if (ret<0)
    {
        ZF_LOGE("mutex locking failed");
        return -1;
    }

    dev->initialized = 0;
    pthread_mutex_destroy(&dev->mtx);

    // now terminate all used spi channels
    for (int i = 0; i < dev->num_of_chips; i++)
    {
        if (dev->chips[i].hard_spi_handle >=0)
        {
            if (spiClose(dev->chips[i].hard_spi_handle) < 0)
            {
                ZF_LOGE("pigpio says %d is a BAD HANDLE", dev->chips[i].hard_spi_handle);
            }
        }
        dev->chips[i].initialized = 0;
    }

    memset (dev->chips, 0, sizeof(dev->chips));
	dev->num_of_chips = 0;
	dev->current_chip = NULL;

    return 0;
}

//=====================================================================================
int io_utils_spi_add_chip(io_utils_spi_st* dev, int cs_pin, int speed, int swap_mi_mo, int mode,
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

    // now lets check if we need a hard spi handle (not a bitbanged configuration)
    if (chip_type == io_utils_spi_chip_type_fpga_comm ||
        chip_type == io_utils_spi_chip_type_modem)
    {
        memcpy (&dev->chips[new_chip_index].hard_dev, hard_dev, sizeof(io_utils_hard_spi_st));
        unsigned int spiFlags = ((dev->chips[new_chip_index].hard_dev.spi_dev_id&0x1)<<8);

        res = spiOpen(dev->chips[new_chip_index].hard_dev.spi_dev_channel, speed, spiFlags);
        if (res < 0)
        {
            ZF_LOGE("spiOpen function failed with code %d", res);
            pthread_mutex_unlock(&dev->mtx);
            return -1;
        }
    }

    dev->chips[new_chip_index].hard_spi_handle = res;
    dev->chips[new_chip_index].initialized = 1;
    // finally increase the number of chips
    dev->num_of_chips += 1;

    pthread_mutex_unlock(&dev->mtx);

    return new_chip_index; // this is the chip handle for the app
}

//=====================================================================================
int io_utils_spi_suspend(io_utils_spi_st* dev, bool suspend)
{
	ZF_LOGI("changing an spi device suspension = '%d' state", suspend);
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
		dev->current_chip == NULL;
		io_utils_set_gpio_mode(dev->miso, io_utils_alt_4);
		io_utils_set_gpio_mode(dev->mosi, io_utils_alt_4);
		io_utils_set_gpio_mode(dev->sck, io_utils_alt_4);
	}

	return 0;
}

//=====================================================================================
int io_utils_spi_remove_chip(io_utils_spi_st* dev, int chip_handle)
{
    ZF_LOGI("removing an spi device with handle %d", chip_handle);

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
        if (spiClose(dev->chips[chip_handle].hard_spi_handle) < 0)
        {
            ZF_LOGE("the specified hard_handle - %d - is not valid", dev->chips[chip_handle].hard_spi_handle);
            // no return as we anyway are going to return
        }
    }
    dev->chips[chip_handle].initialized = 0;
    dev->num_of_chips -= 1;
    pthread_mutex_unlock(&dev->mtx);
    return 0;
}

//=====================================================================================
int io_utils_spi_transmit(io_utils_spi_st* dev, int chip_handle,
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
    if (dev->chips[chip_handle].initialized == 0)
    {
        ZF_LOGE("uninitialized spi chip handle %d", chip_handle);
        return -1;
    }

    // lock the resource
    pthread_mutex_lock(&dev->mtx);

    if (io_utils_spi_setup_chip(dev, chip_handle) < 0)
    {
        ZF_LOGE("chip setup failed %d", chip_handle);
        goto io_utils_spi_transmit_error;
    }

    dev->current_chip = &dev->chips[chip_handle];

    switch (dev->current_chip->chip_type)
    {
        // --------------------------------------------------
        case io_utils_spi_chip_type_fpga_comm:
        case io_utils_spi_chip_type_modem:
        {
            // a regular spi communication through lg_spi / spi_dev
            ret = spiXfer(dev->current_chip->hard_spi_handle, (unsigned char*)tx_buf, rx_buf, length);
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
void io_utils_spi_print_setup(io_utils_spi_st* dev)
{
    if (dev == NULL || !dev->initialized)
    {
        ZF_LOGI("uninitialized device");
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
        printf("        Hard spi handle: %d\n", dev->chips[i].hard_spi_handle);
        if (dev->chips[i].hard_spi_handle>=0)
        {
            printf("            Hard spi id: %d\n", dev->chips[i].hard_dev.spi_dev_id);
            printf("            Hard spi channel: %d\n", dev->chips[i].hard_dev.spi_dev_channel);
        }
    }
    pthread_mutex_unlock(&dev->mtx);
}