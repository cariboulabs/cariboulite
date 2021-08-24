#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

typedef struct
{
    int initialized;
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev);
int caribou_smi_close (caribou_smi_st* dev);

#endif // __CARIBOU_SMI_H__