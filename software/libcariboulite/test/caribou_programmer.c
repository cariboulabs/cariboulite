#include <stdio.h>
#include "cariboulite.h"
#include "cariboulite_setup.h"
#include "cariboulite_config_default.h"

CARIBOULITE_CONFIG_DEFAULT(cariboulite_sys);

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("ICE40 FPGA Programming Tool for the CaribouLite Board\n");
        printf("---------------------------\n\n");
        printf("Usage: use the .bin file output only!\n");
        printf("  caribou_programmer <fpga .bin file full path>\n");
        return 0;
    }
    else
    {
        printf("Programming bin file '%s'\n", argv[1]);
    }

    cariboulite_setup_io (&cariboulite_sys);
    cariboulite_configure_fpga (&cariboulite_sys, cariboulite_firmware_source_file, argv[1]);
    cariboulite_release_io (&cariboulite_sys);

    return 0;
}