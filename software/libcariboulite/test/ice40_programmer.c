#include <stdio.h>
#include "cariboulite.h"
#include "cariboulite_setup.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("ICE40 FPGA Programming Tool for the CaribouLite Board\n");
        printf("---------------------------\n\n");
        printf("Usage: use the .bin file output only!\n");
        printf("  ice40_programmer <fpga .bin file full path>\n");
        return 0;
    }
    else
    {
        printf("Programming bin file '%s'\n", argv[1]);
    }

    cariboulite_setup_io ();
    cariboulite_configure_fpga (argv[1]);
    cariboulite_release_io ();

    return 0;
}