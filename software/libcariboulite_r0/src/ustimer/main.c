#include <stdio.h>
#include "ustimer.h"


static void test_ustimer_handler(unsigned int id, struct timeval iv)
{
    printf("Got Timer event: %d, %d, %d\n", id, iv.tv_sec, iv.tv_usec);
}

int main()
{
    int id = -1;
    printf("ustimer testing...\n");

    ustimer_init( );
    int err = ustimer_register_function 	( test_ustimer_handler, 1000, &id); 
    if (err == 0)
        printf("Regirtered id = %d\n", id);
    else
        printf("Registration error\n");

    ustimer_start (1);

    printf("push enter to quit...\n");
    while (!getchar())
    {

    }

    ustimer_close ( );

    return 0;
}