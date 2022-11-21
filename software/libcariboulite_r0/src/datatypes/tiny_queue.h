#ifndef __TINY_QUEUE_H__
#define __TINY_QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "tiny_list.h"

typedef tiny_list_st* tiny_queue_st;

#define tiny_queue_init(q_ptr) 				(tiny_list_init(q_ptr))
#define tiny_queue_free(q) 					(tiny_list_free(q))
#define tiny_queue_enqueue(q,d_array,l) 		(tiny_list_add((q),(d_array),(l),pos_head))
#define tiny_queue_dequeue(q,d_array,l_ptr) 	(tiny_list_remove((q),(d_array),(l_ptr),pos_tail))
#define tiny_queue_is_empty(q)					(tiny_list_num_elements(q)==0)
#define tiny_queue_num_elements(q)				(tiny_list_num_elements(q))
#define tiny_queue_empty(q)					(tiny_list_remove_all (q))

#ifdef __cplusplus
}
#endif

#endif //__TINY_QUEUE_H__