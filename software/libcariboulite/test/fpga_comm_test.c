#include <stdio.h>
#include "cariboulite.h"
#include "cariboulite_setup.h"
#include "cariboulite_config/cariboulite_config_default.h"

CARIBOULITE_CONFIG_DEFAULT(cariboulite_sys);

int main()
{
    printf("fpga api test program!\n");

    cariboulite_setup_io (&cariboulite_sys);
    cariboulite_init_submodules (&cariboulite_sys);
    cariboulite_release_submodules(&cariboulite_sys);
    cariboulite_release_io (&cariboulite_sys);

    return 0;
}