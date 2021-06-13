#include <stdio.h>
#include "cariboulite.h"
#include "cariboulite_setup.h"

int main()
{
    printf("ice40 programming test program!\n");

    cariboulite_setup_io ();
    cariboulite_configure_fpga ("/home/pi/projects/Caribou/Software/CaribouLite/libcariboulite/src/latticeice40/ex_firmware/top.bin");
    cariboulite_release_io ();

    return 0;
}