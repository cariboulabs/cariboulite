#include <pthread.h>
#include <time.h>
#include <lgpio.h>
#include <errno.h>

#include "io_utils_spi.h"
#include "io_utils.h"

static char *io_utils_chip_types[] =
        {
            "fpga communication icd",
            "mixer - rffc507x / rffc207x",
            "modem - at86rf215",
            "lattice ice40 programmer",
            "generic function - not implemented",
        };

static char *io_utils_transfer_types[] =
        {
            "polling / bitbanging",
            "interrupt",
            "dma",
        };

//=====================================================================================
static int io_utils_spi_setup_chip(io_utils_spi_st* dev, int handle)
{
    io_utils_spi_chip_st* chip = &dev->chips[handle];
    if (dev->current_chip == chip)
    {
        // nothing to setup => return
        return 0;
    }

    if (dev->chips[handle].chip_type == io_utils_spi_chip_ice40_prog)
    {
        // ICE40 PROG
        int data_pin = chip->miso_mosi_swap?dev->miso:dev->mosi;
        int sck_pin = dev->sck;
        io_utils_setup_gpio(data_pin, io_utils_dir_output, io_utils_pull_down);
        io_utils_setup_gpio(sck_pin, io_utils_dir_output, io_utils_pull_down);
        return 0;
    }

    if (dev->chips[handle].chip_type == io_utils_spi_chip_type_rffc)
    {
        // make sure to have a half-duplex spi configuration
        int data_pin = chip->miso_mosi_swap?dev->miso:dev->mosi;
        int sck_pin = dev->sck;
        io_utils_setup_gpio(data_pin, io_utils_dir_output, io_utils_pull_down);
        io_utils_setup_gpio(sck_pin, io_utils_dir_output, io_utils_pull_down);
        return 0;
    }

    if (dev->chips[handle].chip_type == io_utils_spi_chip_type_modem_bitbang )
    {
        int mosi_pin = chip->miso_mosi_swap?dev->miso:dev->mosi;
        int miso_pin = chip->miso_mosi_swap?dev->mosi:dev->miso;
        int sck_pin = dev->sck;
        io_utils_setup_gpio(mosi_pin, io_utils_dir_output, io_utils_pull_down);
        io_utils_setup_gpio(sck_pin, io_utils_dir_output, io_utils_pull_down);
        io_utils_setup_gpio(miso_pin, io_utils_dir_input, io_utils_pull_down);
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
        // Setup the configuration of a regular spi_dev
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
            io_utils_write_gpio_with_wait(data_pin,
                                            (current_tx_byte&0x80)>>7, nop_cnt);

			current_tx_byte <<= 1;
            io_utils_write_gpio_with_wait(sck_pin, 1, nop_cnt);
            io_utils_write_gpio_with_wait(sck_pin, 0, nop_cnt);
		}
	}

    io_utils_write_gpio_with_wait(sck_pin, 0, nop_cnt/2);

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
        printf("ERROR @ io_utils_spi_init: dev is NULL\n");
        return -1;
    }
    if (dev->initialized == 1)
    {
        printf("WARNING @ io_utils_spi_init: spi_dev already initialized\n");
    }

	memset (dev->chips, 0, sizeof(dev->chips));
	dev->num_of_chips = 0;
	dev->current_chip = NULL;

    // Init mutex and unlock
    if (pthread_mutex_init(&dev->mtx, NULL) != 0)
    {
        printf("ERROR @ io_utils_spi_init: mutex init failed\n");
        return -1;
    }

    printf("Info @ io_utils_spi_init: configuring gpio setups\n");

    io_utils_set_gpio_mode(dev->miso, io_utils_alt_gpio_in);
    io_utils_set_gpio_mode(dev->mosi, io_utils_alt_gpio_out);
    io_utils_set_gpio_mode(dev->sck, io_utils_alt_gpio_out);

    io_utils_setup_gpio(dev->miso, io_utils_dir_input, io_utils_pull_down);
    io_utils_setup_gpio(dev->mosi, io_utils_dir_output, io_utils_pull_down);
    io_utils_setup_gpio(dev->sck, io_utils_dir_output, io_utils_pull_down);

    /*io_utils_set_gpio_mode(dev->miso, io_utils_alt_4);
    io_utils_set_gpio_mode(dev->mosi, io_utils_alt_4);
    io_utils_set_gpio_mode(dev->sck, io_utils_alt_4);
*/
    pthread_mutex_unlock(&dev->mtx);

	dev->initialized = 1;
    return 0;
}

//=====================================================================================
int io_utils_spi_close(io_utils_spi_st* dev)
{
    if (dev == NULL || !dev->initialized)
    {
        printf("ERROR @ io_utils_spi_close: closing uninitialized device\n");
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
        printf("WARNING @ io_utils_spi_close: timed out trying locking mutex\n");
    }
    else if (ret<0)
    {
        printf("ERROR @ io_utils_spi_close: mutex locking failed\n");
        return -1;
    }

    dev->initialized = 0;
    pthread_mutex_destroy(&dev->mtx);

    memset (dev->chips, 0, sizeof(dev->chips));
	dev->num_of_chips = 0;
	dev->current_chip = NULL;

    return 0;
}

//=====================================================================================
int io_utils_spi_add_chip(io_utils_spi_st* dev, int cs_pin, int speed, int swap_mi_mo, int mode,
                            io_utils_spi_chip_type_en chip_type,
                            io_utils_spi_chip_transfer_type_en txrx_type,
							int lgpio_spi_handle)
{
    if (dev == NULL || !dev->initialized)
    {
        printf("ERROR @ io_utils_spi_add_chip: uninitialized device\n");
        return -1;
    }

    // lock the resource - no concurrent changes
    pthread_mutex_lock(&dev->mtx);

    // will never be greater but still it is good to check
    if (dev->num_of_chips >= IO_UTILS_MAX_CHIPS)
    {
        printf("ERROR @ io_utils_spi_add_chip: cannnot add - exceeded max %d\n", IO_UTILS_MAX_CHIPS);
        pthread_mutex_unlock(&dev->mtx);
        return -1;
    }

    int new_chip_index = dev->num_of_chips;
    dev->chips[new_chip_index].cs_pin = cs_pin;
    dev->chips[new_chip_index].clock = speed;
    dev->chips[new_chip_index].mode = mode;
    dev->chips[new_chip_index].miso_mosi_swap = swap_mi_mo;
    dev->chips[new_chip_index].chip_type = chip_type;
    dev->chips[new_chip_index].transfer_type = txrx_type;
    dev->chips[new_chip_index].lg_spi_handle = lgpio_spi_handle;

    // finally increase the number of chips
    dev->num_of_chips += 1;
    pthread_mutex_unlock(&dev->mtx);

    return new_chip_index; // this is the chip handle for the app
}

//=====================================================================================
int io_utils_spi_remove_chip(io_utils_spi_st* dev, int cs_pin)
{
    printf("WARNING @ io_utils_spi_remove_chip: function not implemented\n");
    // this function is intentially not available in this stage to
    // keep things simple. In the future a better linked list implementation
    // will be used
    return 0;
}

//=====================================================================================
int io_utils_spi_transmit(io_utils_spi_st* dev, int chip_handle,
							const unsigned char* tx_buf,
							unsigned char* rx_buf,
							size_t length,
                            io_utils_spi_dir_en dir)
{
    if (dev == NULL || !dev->initialized)
    {
        printf("ERROR @ io_utils_spi_transmit: uninitialized device\n");
        return -1;
    }

    // lock the resource
    pthread_mutex_lock(&dev->mtx);

    if (chip_handle >= dev->num_of_chips)
    {
        printf("ERROR @ io_utils_spi_transmit: invalid chip handle %d\n", chip_handle);
        goto io_utils_spi_transmit_error;
    }

    if (io_utils_spi_setup_chip(dev, chip_handle) < 0)
    {
        printf("ERROR @ io_utils_spi_transmit: chip setup failed %d\n", chip_handle);
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
            int ret = lgSpiXfer(dev->current_chip->lg_spi_handle, tx_buf, rx_buf, length);
            if (ret<0)
            {
                printf("Error @ io_utils_spi_transmit: spi transfer failed (%d): %s\n", ret, lguErrorText(ret));
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
                    printf("Error @ io_utils_spi_transmit: rffc507x read transfer failed\n");
                    goto io_utils_spi_transmit_error;
                }
                *((uint16_t*)rx_buf) = (uint16_t)(r & 0xFFFF);
            }
            else
            {
                uint16_t val = ((uint16_t)(tx_buf[1]))<<8 | tx_buf[2];
                int r = io_utils_spi_write_rffc507x(dev, dev->current_chip, reg, val);
                if (r < 0)
                {
                    printf("Error @ io_utils_spi_transmit: rffc507x write transfer failed\n");
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
	    case io_utils_spi_chip_type_generic_function:
        default:
        {
            printf("WARNING @ io_utils_spi_transmit: generic function transfer not implemented\n");
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
        printf("INFO @ io_utils_spi_print_setup: uninitialized device\n");
        return;
    }

    pthread_mutex_lock(&dev->mtx);

    printf("  IO_UTILS_SPI Setup:\n");
    printf("    MISO Pin: %d\n", dev->miso);
    printf("    MOSI Pin: %d\n", dev->mosi);
    printf("    SCK Pin: %d\n", dev->sck);
    printf("    Number of chips: %d\n", dev->num_of_chips);

    for (int i = 0; i < dev->num_of_chips; i++)
    {
        printf("      CHIP #%d:\n", i);
        printf("        CS Pin: %d\n", dev->chips[i].cs_pin);
        printf("        CLK Speed: %d\n", dev->chips[i].clock);
        printf("        SPI Mode: %d\n", dev->chips[i].mode);
        printf("        MISO / MOSI swap: %d\n", dev->chips[i].miso_mosi_swap);
        printf("        LGPIO spi handle: %d\n", dev->chips[i].lg_spi_handle);
        printf("        Chip type: %s (%d)\n", io_utils_chip_types[dev->chips[i].chip_type],
                                                            dev->chips[i].chip_type);
        printf("        TxRx type: %s (%d)\n", io_utils_transfer_types[dev->chips[i].transfer_type],
                                                            dev->chips[i].transfer_type);
    }
    pthread_mutex_unlock(&dev->mtx);
}