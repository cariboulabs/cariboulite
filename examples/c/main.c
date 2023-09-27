#include <stdio.h>
#include <cariboulite.h>

int main ()
{
	cariboulite_init(false, cariboulite_log_level_none);
    cariboulite_close();
	return 0;
}
