#include <stdio.h>
#include "cariboulite.h"
#include "cariboulite_setup.h"
#include "cariboulite_config/cariboulite_config.h"

extern cariboulite_st sys;

int main()
{
    printf("fpga api test program!\n");

    cariboulite_setup_io (&sys, NULL);
    cariboulite_init_submodules (&sys);
    cariboulite_release_submodules(&sys);
    cariboulite_release_io (&sys);

    return 0;
}