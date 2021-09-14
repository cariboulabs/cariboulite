#include <stdio.h>
#include "cariboulite_eeprom.h"


cariboulite_eeprom_st ee = 
{
    .i2c_address = 0x50,    // the i2c address of the eeprom chip
    .eeprom_type = eeprom_type_24c32,
};

int main(int argc, char *argv[]) {
	/*int ret;
	
	if (argc<3) {
		printf("Wrong input format.\n");
		printf("Try 'eepdump input_file output_file'\n");
		return 0;
	}
	
	
	ret = cariboulite_eeprom_read_from_file(&ee, argv[1], argv[2]);
	if (ret) {
		printf("Error reading input, aborting\n");
		return 0;
	}
	*/

    //test();

	return 0;
}
