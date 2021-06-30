#include <stdio.h>
#include "cariboulite.h"
#include "cariboulite_setup.h"

int main()
{
    printf("fpga api test program!\n");

    cariboulite_setup_io ();
    //cariboulite_configure_fpga ("/home/pi/projects/Caribou/Software/CaribouLite/libcariboulite/src/latticeice40/ex_firmware/leds.bin");
    cariboulite_init_submodules ();
    cariboulite_release_submodules();
    cariboulite_release_io ();

    return 0;
}