#include <stdio.h>
#include <cariboulite.h>

int main ()
{
	cariboulite_init(false, cariboulite_log_level_verbose);
    cariboulite_close();
	return 0;
}
