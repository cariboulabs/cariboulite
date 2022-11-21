#include <stdio.h>

#include "config.h"

int main ()
{
	sys_st sys = {0};

    config_detect_board(&sys);
    config_print_board_info(&sys);
    return 0;
}