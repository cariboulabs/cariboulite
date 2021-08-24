#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOU_SMI_Main"

#include "zf_log/zf_log.h"
#include "caribou_smi.h"
#include "bcm2835_smi.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>



static void caribou_smi_print_smi_settings(struct smi_settings *settings);
static void caribou_smi_setup_settings (struct smi_settings *settings);
static void dump_hex(const void* data, size_t size);

//=========================================================================
int caribou_smi_init(caribou_smi_st* dev)
{
    char smi_file[] = "/dev/smi";
    struct smi_settings settings = {0};

    ZF_LOGI("initializing caribou_smi");


    int fd = open(smi_file, O_RDWR);
    if (fd < 0)
    {
        ZF_LOGE("can't open smi driver file '%s'", smi_file);
        return -1;
    }

    dev->filedesc = fd;

    // Get the current settings
    int ret = ioctl(fd, BCM2835_SMI_IOC_GET_SETTINGS, &settings);
    if (ret != 0)
    {
        ZF_LOGE("failed reading ioctl from smi fd (settings)");
        close (fd);
        return -1;
    }

    // apply the new settings
    caribou_smi_setup_settings(&settings);
    ret = ioctl(fd, BCM2835_SMI_IOC_WRITE_SETTINGS, &settings);
    if (ret != 0)
    {
        ZF_LOGE("failed writing ioctl to the smi fd (settings)");
        close (fd);
        return -1;
    }

    ZF_LOGD("Current SMI Settings:");
    caribou_smi_print_smi_settings(&settings);

    // set the address to idle
    ret = ioctl(fd, BCM2835_SMI_IOC_ADDRESS, caribou_smi_address_idle);
    if (ret != 0)
    {
        ZF_LOGE("failed setting smi address (idle / %d) to device", caribou_smi_address_idle);
        close (fd);
        return -1;
    }

    return 0;
}

//=========================================================================
int caribou_smi_close (caribou_smi_st* dev)
{
    ZF_LOGI("closing caribou_smi");
    close (dev->filedesc);
    return 0;
}

//=========================================================================
int caribou_smi_timeout_read(caribou_smi_st* dev, int source, char* buffer, int size_of_buf, int timeout_num_millisec)
{
    fd_set set;
    struct timeval timeout;
    int rv;

    // set the address to idle
    if (source != -1 && CARIBOU_SMI_READ_ADDR(source))
    {
        int ret = ioctl(dev->filedesc, BCM2835_SMI_IOC_ADDRESS, source);
        if (ret != 0)
        {
            ZF_LOGE("failed setting smi address (idle / %d) to device", caribou_smi_address_idle);
            return -1;
        }
    }

    FD_ZERO(&set);                  // clear the set
    FD_SET(dev->filedesc, &set);    // add our file descriptor to the set

    int num_sec = timeout_num_millisec / 1000;
    timeout.tv_sec = num_sec;
    timeout.tv_usec = (timeout_num_millisec - num_sec*1000) * 1000;

    rv = select(dev->filedesc + 1, &set, NULL, NULL, &timeout);
    if(rv == -1)
    {
        ZF_LOGE("smi fd select error");
        return -1;
    }
    else if(rv == 0)
    {
        ZF_LOGD("smi fd timeout");
        return 0;
    }

    return read(dev->filedesc, buffer, size_of_buf);
}


//=========================================================================
static void caribou_smi_print_smi_settings(struct smi_settings *settings)
{
    printf("SMI SETTINGS:\n");
    printf("    width: %d\n", settings->data_width);
    printf("    pack: %c\n", settings->pack_data ? 'Y' : 'N');
    printf("    read setup: %d, strobe: %d, hold: %d, pace: %d\n", settings->read_setup_time, settings->read_strobe_time, settings->read_hold_time, settings->read_pace_time);
    printf("    write setup: %d, strobe: %d, hold: %d, pace: %d\n", settings->write_setup_time, settings->write_strobe_time, settings->write_hold_time, settings->write_pace_time);
    printf("    dma enable: %c, passthru enable: %c\n", settings->dma_enable ? 'Y':'N', settings->dma_passthrough_enable ? 'Y':'N');
    printf("    dma threshold read: %d, write: %d\n", settings->dma_read_thresh, settings->dma_write_thresh);
    printf("    dma panic threshold read: %d, write: %d\n", settings->dma_panic_read_thresh, settings->dma_panic_write_thresh);
}

//=========================================================================
static void caribou_smi_setup_settings (struct smi_settings *settings)
{
    settings->read_setup_time = 1;
    settings->read_strobe_time = 3;
    settings->read_hold_time = 1;
    settings->read_pace_time = 2;
    settings->write_setup_time = 1;
    settings->write_hold_time = 1;
    settings->write_pace_time = 2;
    settings->write_strobe_time = 3;
    settings->data_width = SMI_WIDTH_8BIT;
    settings->dma_enable = 1;
    settings->pack_data = 1;
    settings->dma_passthrough_enable = 1;
}

//=========================================================================
static void dump_hex(const void* data, size_t size)
{
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~')
        {
			ascii[i % 16] = ((unsigned char*)data)[i];
		}
        else
        {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size)
        {
			printf(" ");
			if ((i+1) % 16 == 0)
            {
				printf("|  %s \n", ascii);
			}
            else if (i+1 == size)
            {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8)
                {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j)
                {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}

