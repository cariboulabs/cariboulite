#include <stdio.h>
#include "caribou_smi.h"

//=====================================================================
int caribou_smi_init(caribou_smi_st* dev)
{
    dev->initialized = 1;
    return 0;
}

//=====================================================================
int caribou_smi_close(caribou_smi_st* dev)
{
    if (!dev->initialized)
    {
        return 0;
    }

    dev->initialized = 0;
    return 0;
}