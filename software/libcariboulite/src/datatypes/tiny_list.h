#ifndef __TINY_LIST_H__
#define __TINY_LIST_H__

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

typedef enum
{
	pos_head		= 0,
	pos_tail		= 1,
} tiny_list_pos_en;

struct tiny_list_node_t
{
	void* data;
	unsigned int len;
	
	struct tiny_list_node_t *last;
	struct tiny_list_node_t *next;
};

typedef struct tiny_list_node_t tiny_list_node_st;

typedef struct
{
	pthread_mutex_t mtx;
	struct tiny_list_node_t* head;
	struct tiny_list_node_t* tail;
	
	unsigned int num_of_elements;
} tiny_list_st;


int tiny_list_init (tiny_list_st** list);
void tiny_list_free (tiny_list_st* list);
int tiny_list_add (tiny_list_st* list, void* data, unsigned int len, tiny_list_pos_en pos);
int tiny_list_remove (tiny_list_st* list, void* data, unsigned int *len, tiny_list_pos_en pos);
void tiny_list_remove_all (tiny_list_st* list);
int tiny_list_peek (tiny_list_st* list, void* data, unsigned int *len, tiny_list_pos_en pos);
int tiny_list_num_elements (tiny_list_st* list);
void tiny_list_print (tiny_list_st* list);
tiny_list_node_st* tiny_list_create_node (void *data, unsigned int len);
tiny_list_node_st* tiny_list_destroy_node (tiny_list_node_st* node);
void tiny_list_print_node (tiny_list_node_st* node);

#endif //__TINY_LIST_H__