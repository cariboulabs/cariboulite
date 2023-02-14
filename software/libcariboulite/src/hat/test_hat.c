#include <stdio.h>
#include "../cariboulite_dtbo.h"
#include "hat.h"


hat_st hat = 
{
	.vendor_name = "CaribouLabs LTD",
	.product_name = "CaribouLite RPI Hat",
	.product_id = 0x01,
	.product_version = 0x01,
	.device_tree_buffer = cariboulite_dtbo,
	.device_tree_buffer_size = sizeof(cariboulite_dtbo),

	.dev = {
		.i2c_address =  0x50,    // the i2c address of the eeprom chip
		.eeprom_type = eeprom_type_24c32,
	},
};

int main() 
{
    if (hat_init(&hat) != 0)
    {
        printf("error\n");
        return 0;
    }

    hat_print(&hat);

	hat_board_info_st info = {0};
	hat_detect_board(&info);
	hat_print_board_info(&info);

    hat_close(&hat);

	return 0;
}
