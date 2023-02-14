#include <stdio.h>
#include <unistd.h>
#include "hat_powermon.h"


void callback(void* context, hat_powermon_state_st *state)
{
	printf("MON: %d. SW_ST: %d, FLT: %d, I: %.2f, V: %.2f, P: %.2f\n",
			state->monitor_active,
			state->load_switch_state,
			state->fault,
			state->i_ma,
			state->v_mv,
			state->p_mw);
}

int main ()
{
	int ver, subver;
	bool led1=false, led2=false;
	hat_power_monitor_st dev = {0};
	hat_powermon_init(&dev, 0x25, callback, &dev);

	while (1)
	{
		sleep(5);
		led1 = !led1;
		led2 = !led2;
		hat_powermon_set_power_state(&dev, led1);
		//hat_powermon_set_leds_state(&dev, led1, led2);
		
		hat_powermon_read_versions(&dev, &ver, &subver);
		printf("VER: %d, SUBVER: %d\n", ver, subver);
	}
	
	hat_powermon_release(&dev);
	return 0;
}