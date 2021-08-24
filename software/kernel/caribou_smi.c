#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "AT86RF215_Main"

#include "caribou_smi.h"
#include "bcm2835_smi.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

int caribou_smi_init(caribou_smi_st* dev)
{
    ZF_LOGI("initializing caribou_smi");
    return 0;
}

int caribou_smi_close (caribou_smi_st* dev)
{
    ZF_LOGI("closing caribou_smi");
    return 0;
}