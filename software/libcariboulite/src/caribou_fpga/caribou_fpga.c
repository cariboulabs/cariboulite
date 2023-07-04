#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "FPGA"
#include "zf_log/zf_log.h"

#include <stdio.h>
#include <string.h>
#include "caribou_fpga.h"

//--------------------------------------------------------------
// Static Definitions
//--------------------------------------------------------------
#define IOC_MOD_VER                 0
#define IOC_SYS_CTRL_SYS_VERSION    1
#define IOC_SYS_CTRL_MANU_ID        2
#define IOC_SYS_CTRL_SYS_ERR_STAT   3
#define IOC_SYS_CTRL_SYS_SOFT_RST   4
#define IOC_SYS_CTRL_DEBUG_MODES    5
#define IOC_SYS_CTRL_SYS_TX_SAMPLE_GAP  6

#define IOC_IO_CTRL_MODE            1
#define IOC_IO_CTRL_DIG_PIN         2
#define IOC_IO_CTRL_PMOD_DIR        3
#define IOC_IO_CTRL_PMOD_VAL        4
#define IOC_IO_CTRL_RF_PIN          5
#define IOC_IO_CTRL_MXR_FM_PS       6
#define IOC_IO_CTRL_MXR_FM_DATA     7

#define IOC_SMI_CTRL_FIFO_STATUS    1
#define IOC_SMI_CHANNEL_SELECT      2
#define IOC_SMI_CTRL_DIR_SELECT         3

//--------------------------------------------------------------
// Internal Data-Types
//--------------------------------------------------------------
#pragma pack(1)
typedef enum
{
    caribou_fpga_rw_read = 0,
    caribou_fpga_rw_write = 1,
} caribou_fpga_rw_en;

typedef enum
{
    caribou_fpga_mid_sys_ctrl = 0,
    caribou_fpga_mid_io_ctrl = 1,
    caribou_fpga_mid_smi_ctrl = 2,
    caribou_fpga_mid_res = 3,
} caribou_fpga_mid_en;

typedef struct
{
    uint8_t ioc                 : 5;
    caribou_fpga_mid_en     mid : 2;
    caribou_fpga_rw_en      rw  : 1;        // MSB
} caribou_fpga_opcode_st;


#pragma pack()


#define CARIBOU_FPGA_CHECK_DEV(d,f)                                                                             \
                    {                                                                                           \
                        if ((d)==NULL) { ZF_LOGE("%s: dev is NULL", (f)); return -1;}          \
                        if (!(d)->initialized) {ZF_LOGE("%s: dev not initialized", (f)); return -1;}   \
                    }
#define CARIBOU_FPGA_CHECK_PTR_NOT_NULL(p,f,n) {if ((p)==NULL) {ZF_LOGE("%s: %s is NULL",(f),(n)); return -1;}}


//===================================================================
void caribou_fpga_interrupt_handler (int event, int level, uint32_t tick, void *data)
{
    //int i;
    //caribou_fpga_st* dev = (caribou_fpga_st*)data;

    /*for (i=0; i<e; i++)
    {
        printf("u=%d t=%"PRIu64" c=%d g=%d l=%d f=%d (%d of %d)\n",
        dev, evt[i].report.timestamp, evt[i].report.chip,
        evt[i].report.gpio, evt[i].report.level,
        evt[i].report.flags, i+1, e);
    }*/
}

//--------------------------------------------------------------
static int caribou_fpga_spi_transfer (caribou_fpga_st* dev, uint8_t *opcode, uint8_t *data)
{
    uint8_t tx_buf[2] = {*opcode, *data};
    uint8_t rx_buf[2] = {0, 0};
    int ret = io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle,
                                        tx_buf, rx_buf, 2, io_utils_spi_read_write);
    if (ret<0)
    {
        ZF_LOGE("spi transfer failed (%d)", ret);
        return -1;
    }
    // on success return 0 (the number of transmitted bytes are same as needed),
    // otherwise '1' (warning / error)

    *data = rx_buf[1];
    return !(ret == sizeof(rx_buf));
}

//--------------------------------------------------------------
int caribou_fpga_init(caribou_fpga_st* dev, io_utils_spi_st* io_spi)
{
    if (dev == NULL)
    {
        ZF_LOGE("dev is NULL");
        return -1;
    }

    dev->io_spi = io_spi;

    ZF_LOGI("configuring reset and irq pins");
	// Configure GPIO pins
	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_output, io_utils_pull_up);
	io_utils_setup_gpio(dev->soft_reset_pin, io_utils_dir_output, io_utils_pull_up);
	
	// set to known state
	io_utils_write_gpio(dev->soft_reset_pin, 1);

    ZF_LOGI("Initializing io_utils_spi");
    io_utils_hard_spi_st hard_dev_fpga = {  .spi_dev_id = dev->spi_dev,
                                            .spi_dev_channel = dev->spi_channel, };
	dev->io_spi_handle = io_utils_spi_add_chip(dev->io_spi, dev->cs_pin, 1000000, 0, 0,
                        						io_utils_spi_chip_type_fpga_comm,
                                                &hard_dev_fpga);

	// Init FPGA programming
    if (caribou_prog_init(&dev->prog_dev, dev->io_spi) < 0)
    {
        ZF_LOGE("ice40 programmer init failed");
        return -1;
    }
	
    dev->initialized = 1;
    return 0;
}

//--------------------------------------------------------------
int caribou_fpga_get_status(caribou_fpga_st* dev, caribou_fpga_status_en *stat)
{
	caribou_fpga_get_versions (dev, NULL);
	if (dev->versions.sys_manu_id != CARIBOU_SDR_MANU_CODE)
	{
		dev->status = caribou_fpga_status_not_programmed;
	}
	else
	{
		dev->status = caribou_fpga_status_operational;
	}
	if (stat) *stat = dev->status;
	return 0;
}

//--------------------------------------------------------------
int caribou_fpga_program_to_fpga(caribou_fpga_st* dev, unsigned char *buffer, size_t len, bool force_prog)
{
	caribou_fpga_get_status(dev, NULL);
	if (dev->status == caribou_fpga_status_not_programmed || force_prog)
	{
		if (buffer == NULL || len == 0)
		{
			ZF_LOGE("buffer should be not NULL and len > 0");
        	return -1;
		}

		if (caribou_prog_configure_from_buffer(&dev->prog_dev, buffer, len) < 0)
		{
			ZF_LOGE("Programming failed");
			return -1;
		}

		caribou_fpga_soft_reset(dev);
		io_utils_usleep(100000);

		caribou_fpga_get_status(dev, NULL);
		if (dev->status == caribou_fpga_status_not_programmed)
		{
			ZF_LOGE("Programming failed");
			return -1;
		}
	}
	else
	{
		ZF_LOGI("FPGA already operational - not programming (use 'force_prog=true' to force update)");
	}
	return 0;
}

//--------------------------------------------------------------
int caribou_fpga_program_to_fpga_from_file(caribou_fpga_st* dev, char *filename, bool force_prog)
{
	caribou_fpga_get_status(dev, NULL);
	if (dev->status == caribou_fpga_status_not_programmed || force_prog)
	{
		if (caribou_prog_configure(&dev->prog_dev, filename) < 0)
		{
			ZF_LOGE("Programming failed");
			return -1;
		}
		
		caribou_fpga_soft_reset(dev);
		io_utils_usleep(100000);

		caribou_fpga_get_status(dev, NULL);
		if (dev->status == caribou_fpga_status_not_programmed)
		{
			ZF_LOGE("Programming failed");
			return -1;
		}
	}
	else
	{
		ZF_LOGI("FPGA already operational - not programming (use 'force_prog=true' to force update)");
	}
	return 0;
}

//--------------------------------------------------------------
int caribou_fpga_close(caribou_fpga_st* dev)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_close");
    dev->initialized = 0;
    io_utils_spi_remove_chip(dev->io_spi, dev->io_spi_handle);
	
	return caribou_prog_release(&dev->prog_dev);
}

//--------------------------------------------------------------
int caribou_fpga_soft_reset(caribou_fpga_st* dev)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_soft_reset");

	io_utils_write_gpio_with_wait(dev->soft_reset_pin, 0, 1000);
	io_utils_write_gpio_with_wait(dev->soft_reset_pin, 1, 1000);
	return 0;
}

//--------------------------------------------------------------
int caribou_fpga_hard_reset(caribou_fpga_st* dev)
{
	CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_hard_reset (disposing firmware)");
	io_utils_write_gpio_with_wait(dev->reset_pin, 0, 1000);
	io_utils_write_gpio_with_wait(dev->reset_pin, 1, 1000);
	return 0;
}

//--------------------------------------------------------------
int caribou_fpga_hard_reset_keep(caribou_fpga_st* dev, bool reset)
{
	if (reset)
	{
		io_utils_write_gpio_with_wait(dev->reset_pin, 0, 1000);
	}
	else
	{
		io_utils_write_gpio_with_wait(dev->reset_pin, 1, 1000);
	}
	return 0;
}

//--------------------------------------------------------------
// System Controller
void caribou_fpga_print_versions (caribou_fpga_st* dev)
{
	printf("FPGA Versions:\n");
	printf("	System Version: %02X\n", dev->versions.sys_ver);
	printf("	Manu. ID: %02X\n", dev->versions.sys_manu_id);
	printf("	Sys. Ctrl Version: %02X\n", dev->versions.sys_ctrl_mod_ver);
	printf("	IO Ctrl Version: %02X\n", dev->versions.io_ctrl_mod_ver);
	printf("	SMI Ctrl Version: %02X\n", dev->versions.smi_ctrl_mod_ver);
}

//--------------------------------------------------------------
int caribou_fpga_get_versions (caribou_fpga_st* dev, caribou_fpga_versions_st* vers)
{
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_read,
        .mid = caribou_fpga_mid_sys_ctrl,
    };

    uint8_t *poc = (uint8_t*)&oc;
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_get_versions");

    oc.ioc = IOC_SYS_CTRL_SYS_VERSION;
    caribou_fpga_spi_transfer (dev, poc, &dev->versions.sys_ver);

    oc.ioc = IOC_SYS_CTRL_MANU_ID;
    caribou_fpga_spi_transfer (dev, poc, &dev->versions.sys_manu_id);

    oc.ioc = IOC_MOD_VER;
    oc.mid = caribou_fpga_mid_sys_ctrl;
    caribou_fpga_spi_transfer (dev, poc, &dev->versions.sys_ctrl_mod_ver);

    oc.mid = caribou_fpga_mid_io_ctrl;
    caribou_fpga_spi_transfer (dev, poc, &dev->versions.io_ctrl_mod_ver);

    oc.mid = caribou_fpga_mid_smi_ctrl;
    caribou_fpga_spi_transfer (dev, poc, &dev->versions.smi_ctrl_mod_ver);

	caribou_fpga_print_versions (dev);

	if (vers)
	{
		memcpy (vers, &dev->versions, sizeof(caribou_fpga_versions_st));
	}

    return 0;
}


//--------------------------------------------------------------
static char caribou_fpga_mode_names[][64] =
{
	"Low Power (0)",
	"RX / TX bypass (1)",
	"RX lowpass (up-conversion) (2)",
	"RX hipass (down-conversion) (3)",
	"TX lowpass (down-conversion) (4)",
	"RX hipass (up-conversion) (5)",
};

char* caribou_fpga_get_mode_name (caribou_fpga_io_ctrl_rfm_en mode)
{
	if (mode >= caribou_fpga_io_ctrl_rfm_low_power && mode <= caribou_fpga_io_ctrl_rfm_tx_hipass)
		return caribou_fpga_mode_names[mode];
	return NULL;
}

//--------------------------------------------------------------
int caribou_fpga_set_debug_modes (caribou_fpga_st* dev, bool dbg_fifo_push, bool dbg_fifo_pull, bool dbg_smi)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_set_debug_modes");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_write,
        .mid = caribou_fpga_mid_sys_ctrl,
        .ioc = IOC_SYS_CTRL_DEBUG_MODES
    };
    uint8_t mode = ((dbg_fifo_push & 0x1) << 0) | ((dbg_fifo_pull & 0x1) << 1) | ((dbg_smi & 0x1) << 2 );
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), &mode);
}
//--------------------------------------------------------------
int caribou_fpga_get_errors (caribou_fpga_st* dev, uint8_t *err_map)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_get_errors");
    CARIBOU_FPGA_CHECK_PTR_NOT_NULL(err_map,"caribou_fpga_get_errors","err_map");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_read,
        .mid = caribou_fpga_mid_sys_ctrl,
        .ioc = IOC_SYS_CTRL_SYS_ERR_STAT
    };

    *err_map = 0;
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), err_map);
}

//--------------------------------------------------------------
int caribou_fpga_set_sys_ctrl_tx_sample_gap (caribou_fpga_st* dev, uint8_t gap)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_set_sys_ctrl_tx_sample_gap");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_write,
        .mid = caribou_fpga_mid_sys_ctrl,
        .ioc = IOC_SYS_CTRL_SYS_TX_SAMPLE_GAP,
    };
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), &gap);
}

//--------------------------------------------------------------
int caribou_fpga_get_sys_ctrl_tx_sample_gap (caribou_fpga_st* dev, uint8_t *gap)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_get_sys_ctrl_tx_sample_gap");

    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_read,
        .mid = caribou_fpga_mid_sys_ctrl,
        .ioc = IOC_SYS_CTRL_SYS_TX_SAMPLE_GAP
    };

    *gap = 0;
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), gap);
}


// I/O Controller
int caribou_fpga_set_io_ctrl_mode (caribou_fpga_st* dev, uint8_t debug_mode, caribou_fpga_io_ctrl_rfm_en rfm)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_set_io_ctrl_mode");

    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_write,
        .mid = caribou_fpga_mid_io_ctrl,
        .ioc = IOC_IO_CTRL_MODE
    };

    uint8_t mode = (debug_mode << 0) | (rfm&0x7)<<2;
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), &mode);
}

//--------------------------------------------------------------
int caribou_fpga_get_io_ctrl_mode (caribou_fpga_st* dev, uint8_t* debug_mode, caribou_fpga_io_ctrl_rfm_en* rfm)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_get_io_ctrl_mode");

    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_read,
        .mid = caribou_fpga_mid_io_ctrl,
        .ioc = IOC_IO_CTRL_MODE
    };

    uint8_t mode = 0;
    caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), &mode);

    *debug_mode = mode & 0x3;
    *rfm = (mode >> 2) & 0x7;
    return 0;
}

//--------------------------------------------------------------
int caribou_fpga_set_io_ctrl_dig (caribou_fpga_st* dev, int led0, int led1)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_set_io_ctrl_dig");

    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_write,
        .mid = caribou_fpga_mid_io_ctrl,
        .ioc = IOC_IO_CTRL_DIG_PIN
    };

    uint8_t pins = led1<<1 | led0<<0;
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), &pins);
}

//--------------------------------------------------------------
int caribou_fpga_get_io_ctrl_dig (caribou_fpga_st* dev, int* led0, int* led1, int* btn, int* cfg)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_get_io_ctrl_dig");

    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_read,
        .mid = caribou_fpga_mid_io_ctrl,
        .ioc = IOC_IO_CTRL_DIG_PIN
    };

    uint8_t pins = 0;
    caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), &pins);
    if (led0) *led0 = (pins>>0)&0x01;
    if (led1) *led1 = (pins>>1)&0x01;
    if (cfg) *cfg = (pins>>3)&0x0F;
    if (btn) *btn = (pins>>7)&0x01;

    return 0;
}

//--------------------------------------------------------------
int caribou_fpga_set_io_ctrl_pmod_dir (caribou_fpga_st* dev, uint8_t dir)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_set_io_ctrl_pmod_dir");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_write,
        .mid = caribou_fpga_mid_io_ctrl,
        .ioc = IOC_IO_CTRL_PMOD_DIR
    };
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), &dir);
}

//--------------------------------------------------------------
int caribou_fpga_get_io_ctrl_pmod_dir (caribou_fpga_st* dev, uint8_t *dir)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_get_io_ctrl_pmod_dir");
    CARIBOU_FPGA_CHECK_PTR_NOT_NULL(dir,"caribou_fpga_get_io_ctrl_pmod_dir","dir");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_read,
        .mid = caribou_fpga_mid_io_ctrl,
        .ioc = IOC_IO_CTRL_PMOD_DIR
    };
    *dir = 0;
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), dir);
}

//--------------------------------------------------------------
int caribou_fpga_set_io_ctrl_pmod_val (caribou_fpga_st* dev, uint8_t val)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_set_io_ctrl_pmod_val");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_write,
        .mid = caribou_fpga_mid_io_ctrl,
        .ioc = IOC_IO_CTRL_PMOD_VAL
    };
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), &val);
}

//--------------------------------------------------------------
int caribou_fpga_get_io_ctrl_pmod_val (caribou_fpga_st* dev, uint8_t *val)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_get_io_ctrl_pmod_val");
    CARIBOU_FPGA_CHECK_PTR_NOT_NULL(val,"caribou_fpga_get_io_ctrl_pmod_val","val");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_read,
        .mid = caribou_fpga_mid_io_ctrl,
        .ioc = IOC_IO_CTRL_PMOD_VAL
    };
    *val = 0;
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), val);
}

//--------------------------------------------------------------
int caribou_fpga_set_io_ctrl_rf_state (caribou_fpga_st* dev, caribou_fpga_rf_pin_st *pins)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_set_io_ctrl_rf_state");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_write,
        .mid = caribou_fpga_mid_io_ctrl,
        .ioc = IOC_IO_CTRL_RF_PIN
    };
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), (uint8_t*)pins);
}

//--------------------------------------------------------------
int caribou_fpga_get_io_ctrl_rf_state (caribou_fpga_st* dev, caribou_fpga_rf_pin_st *pins)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_get_io_ctrl_rf_state");
    CARIBOU_FPGA_CHECK_PTR_NOT_NULL(pins,"caribou_fpga_get_io_ctrl_rf_state","pins");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_read,
        .mid = caribou_fpga_mid_io_ctrl,
        .ioc = IOC_IO_CTRL_RF_PIN
    };
    memset(pins, 0, sizeof(caribou_fpga_rf_pin_st));
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), (uint8_t*)pins);
}

//--------------------------------------------------------------
int caribou_fpga_get_smi_ctrl_fifo_status (caribou_fpga_st* dev, caribou_fpga_smi_fifo_status_st *status)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_get_smi_ctrl_fifo_status");
    CARIBOU_FPGA_CHECK_PTR_NOT_NULL(status,"caribou_fpga_get_smi_ctrl_fifo_status","status");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_read,
        .mid = caribou_fpga_mid_smi_ctrl,
        .ioc = IOC_SMI_CTRL_FIFO_STATUS
    };
    memset(status, 0, sizeof(caribou_fpga_smi_fifo_status_st));
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), (uint8_t*)status);
}

//--------------------------------------------------------------
int caribou_fpga_set_smi_channel (caribou_fpga_st* dev, caribou_fpga_smi_channel_en channel)
{
    uint8_t val = 0;
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_set_smi_channel");
    
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_write,
        .mid = caribou_fpga_mid_smi_ctrl,
        .ioc = IOC_SMI_CHANNEL_SELECT
    };
    val = (channel == caribou_fpga_smi_channel_0) ? 0x0 : 0x1;
   
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), &val);
}

//--------------------------------------------------------------
int caribou_fpga_set_smi_ctrl_data_direction (caribou_fpga_st* dev, uint8_t dir)
{
    CARIBOU_FPGA_CHECK_DEV(dev,"caribou_fpga_set_smi_ctrl_data_direction");
    caribou_fpga_opcode_st oc =
    {
        .rw  = caribou_fpga_rw_write,
        .mid = caribou_fpga_mid_smi_ctrl,
        .ioc = IOC_SMI_CTRL_DIR_SELECT
    };
    return caribou_fpga_spi_transfer (dev, (uint8_t*)(&oc), (uint8_t*)&dir);
}
