#include <stdio.h>

#include "cariboulite_config.h"

cariboulite_board_info_st info = {0};

int main ()
{
    cariboulite_config_detect_board(&info);
    cariboulite_config_print_board_info(&info);
    return 0;
}