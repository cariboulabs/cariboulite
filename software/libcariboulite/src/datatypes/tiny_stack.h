#ifndef __TINY_STACK_H__
#define __TINY_STACK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "tiny_list.h"

typedef tiny_list_st* tiny_stack_st;

#define tiny_stack_init(s_ptr) 				(tiny_list_init(s_ptr))
#define tiny_stack_free(s) 					(tiny_list_free(s))
#define tiny_stack_enqueue(s,d_array,l) 	(tiny_list_add((s),(d_array),(l),pos_head))
#define tiny_stack_dequeue(s,d_array,l_ptr) (tiny_list_remove((s),(d_array),(l_ptr),pos_head))
#define tiny_stack_is_empty(s)				(tiny_list_num_elements(s)==0)
#define tiny_stack_empty(s)					(tiny_list_remove_all (s))

#ifdef __cplusplus
}
#endif

#endif //__TINY_STACK_H__