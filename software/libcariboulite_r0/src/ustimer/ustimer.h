#ifndef ___USTIMER_H__
#define ___USTIMER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>

#define USTIMER_NUM_SLOTS		30					// maximum 10 timers available
#define USTIMER_DELTA(a,b)		(((b).tv_sec-(a).tv_sec)*1e6+((b).tv_usec-(a).tv_usec))
#define USTIMER_MIN_QUANT		5					// Millisec

// handler function gets the timer id and the timeval from its last occurence
typedef void (*ustimer_handler)(unsigned int,struct timeval);

typedef struct
{
	unsigned int timer_quant[USTIMER_NUM_SLOTS];
	int time_enabled[USTIMER_NUM_SLOTS];
	int timer_func_busy[USTIMER_NUM_SLOTS];
	ustimer_handler handlers[USTIMER_NUM_SLOTS];
	struct timeval timetags[USTIMER_NUM_SLOTS];

	timer_t timer_id;
	unsigned int initialized;
	unsigned int timer_count;
	unsigned int global_running;
	int			 init_err;
} ustimer_t;


// timer function prototypes
int ustimer_init 				( void );
int ustimer_close 				( void );
int ustimer_register_function 	( ustimer_handler func,// IN: the handler function
								  unsigned int interval,// IN: interval in millisec between events
								  int *id );			// OUT: the specific timer id [0..WH_TIMER_NUM_SLOTS-1]
int ustimer_unregister 			( int id );
int ustimer_start 				( unsigned int run );

#ifdef __cplusplus
}
#endif

#endif //___USTIMER_H__
