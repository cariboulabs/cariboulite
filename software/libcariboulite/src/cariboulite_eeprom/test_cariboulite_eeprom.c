#include <stdio.h>
#include "cariboulite_eeprom.h"


cariboulite_eeprom_st ee = 
{
    .i2c_address = 0x50,    // the i2c address of the eeprom chip
    .eeprom_type = eeprom_type_24c32,
};

int main(int argc, char *argv[]) 
{
    if (cariboulite_eeprom_init(&ee) != 0)
    {
        printf("error\n");
        return 0;
    }

    cariboulite_eeprom_print(&ee);

    cariboulite_eeprom_close(&ee);

	return 0;
}
