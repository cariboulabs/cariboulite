#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

typedef enum
{
    caribou_smi_address_idle = 0<<1,
    caribou_smi_address_write_900 = 1<<1,
    caribou_smi_address_write_2400 = 2<<1,
    caribou_smi_address_write_res2 = 3<<1,
    caribou_smi_address_read_res1 = 4<<1,
    caribou_smi_address_read_900 = 5<<1,
    caribou_smi_address_read_2400 = 6<<1,
    caribou_smi_address_read_res = 7<<1,
} caribou_smi_address_en;

#define CARIBOU_SMI_READ_ADDR(a) (a>>3)

typedef struct
{
    int initialized;
    int filedesc;
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev);
int caribou_smi_close (caribou_smi_st* dev);
int caribou_smi_timeout_read(caribou_smi_st* dev, int source, char* buffer, int size_of_buf, int timeout_num_millisec);

#endif // __CARIBOU_SMI_H__