#include <stdio.h>
#include "caribou_smi.h"


caribou_smi_st dev = 
{
    .data_0_pin = 8,
    .num_data_pins = 8,
    .soe_pin = 6,
    .swe_pin = 7,
    .addr0_pin = 2,
    .num_addr_pins = 3,
    .read_req_pin = 24,
    .write_req_pin = 25,
};

int main ()
{
    printf("Hello from CaribouSMI!\n");

	io_utils_setup();

    caribou_smi_init(&dev);

    caribou_smi_close(&dev);
    return 0;
}