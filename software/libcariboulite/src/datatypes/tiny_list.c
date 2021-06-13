#include <stdio.h>
#include <pthread.h>
#include "tiny_list.h"

//============================================================
int tiny_list_init (tiny_list_st** list)
{
	tiny_list_st* new_list = (tiny_list_st*)malloc(sizeof (tiny_list_st));
	if (new_list == NULL)
	{
		printf("'tiny_list_init' - allocation failed\n");
		return -1;
	}
	
	if (pthread_mutex_init(&new_list->mtx, NULL) != 0)
	{
		printf("'tiny_list_init' - mutex initialization failed\n");
		free (new_list);
		return -1;
	}
	pthread_mutex_unlock (&new_list->mtx);
	
	new_list->head = NULL;
	new_list->tail = NULL;
	new_list->num_of_elements = 0;
	
	*list = new_list;
	
	return 0;
}

//============================================================
void tiny_list_free (tiny_list_st* list)
{
	tiny_list_remove_all (list);
	free (list);
}

//============================================================
int tiny_list_add (tiny_list_st* list, void* data, unsigned int len, tiny_list_pos_en pos)
{
	if (list == NULL)
	{
		printf("'tiny_list_add' - list not initialized\n");
		return -1;
	}
	
	tiny_list_node_st* new_node = tiny_list_create_node (data, len);
	if (new_node == NULL)
	{
		printf("'tiny_list_add' - node creation failed\n");
		return -1;
	}
	
	pthread_mutex_lock (&list->mtx);
	if (pos == pos_head)
	{
		new_node->next = list->head;
		new_node->last = NULL;
		list->head = new_node;
		if (new_node->next) new_node->next->last = new_node;
		if (list->num_of_elements == 0) list->tail = list->head;
	}
	else	// default = tail
	{
		new_node->next = NULL;
		new_node->last = list->tail;
		list->tail = new_node;
		
		if (new_node->last) new_node->last->next = new_node;
		if (list->num_of_elements == 0) list->head = list->tail;
	}
	
	list->num_of_elements ++;
	
	pthread_mutex_unlock (&list->mtx);
	
	return 0;
}

//============================================================
int tiny_list_remove (tiny_list_st* list, void* data, unsigned int *len, tiny_list_pos_en pos)
{
	tiny_list_node_st* node = NULL;
	if (list == NULL)
	{
		printf("'tiny_list_remove' - list not initialized\n");
		return -1;
	}
	
	pthread_mutex_lock (&list->mtx);
	if (list->num_of_elements == 0)
	{
		printf("'tiny_list_remove' - the list is empty - nothing to remove\n");
		pthread_mutex_unlock (&list->mtx);
		return -1;
	}
	
	if (pos == pos_head)
	{
		node = list->head;
		list->head = node->next;
		if (list->head) list->head->last = NULL;
	}
	else	// default = tail
	{
		node = list->tail;
		list->tail = node->last;
		if (list->tail) list->tail->next = NULL;
	}
	
	list->num_of_elements --;
	if (list->num_of_elements == 0)
	{
		list->tail = NULL;
		list->head = NULL;
	}
	pthread_mutex_unlock (&list->mtx);
	
	if (len) *len = node->len;
	if (data) memcpy(data, node->data, node->len);

	tiny_list_destroy_node (node);
	
	return 0;
}

//============================================================
void tiny_list_remove_all (tiny_list_st* list)
{
	tiny_list_node_st* node = NULL;
	if (list == NULL)
	{
		printf("'tiny_list_free' - trying to free not initialized list\n");
		return;
	}
	
	node = list->head;
	pthread_mutex_unlock (&list->mtx);
	pthread_mutex_destroy (&list->mtx);
	
	while (node != NULL)
	{
		node = tiny_list_destroy_node (node);
	}
	
	list->num_of_elements = 0;
	list->head = NULL;
	list->tail = NULL;

}

//============================================================
int tiny_list_peek (tiny_list_st* list, void* data, unsigned int *len, tiny_list_pos_en pos)
{
	tiny_list_node_st* node = NULL;
	if (list == NULL)
	{
		printf("'tiny_list_peek' - list not initialized\n");
		return -1;
	}
	
	pthread_mutex_lock (&list->mtx);
	if (list->num_of_elements == 0)
	{
		printf("'tiny_list_remove' - the list is empty - nothing to peek\n");
		pthread_mutex_unlock (&list->mtx);
		return -1;
	}
	
	if (pos == pos_head)
	{
		node = list->head;
	}
	else	// default = tail
	{
		node = list->tail;
	}
	
	if (len) *len = node->len;
	if (data) memcpy(data, node->data, node->len);
	
	pthread_mutex_unlock (&list->mtx);
	
	return 0;
}

//============================================================
int tiny_list_num_elements (tiny_list_st* list)
{
	int num;
	if (list == NULL)
	{
		printf("'tiny_list_num_elements' - list not initialized\n");
		return -1;
	}
	pthread_mutex_lock (&list->mtx);
	num = list->num_of_elements;
	pthread_mutex_unlock (&list->mtx);
	return num;
}

//============================================================
void tiny_list_print (tiny_list_st* list)
{
	tiny_list_node_st* cur_node;
	if (list == NULL)
	{
		printf("'tiny_list_print' - list is not initialized\n");
		return;
	}
	
	pthread_mutex_lock (&list->mtx);
	printf("Number of elements: %d - \n", list->num_of_elements);
	cur_node = list->head;
	while (cur_node)
	{
		tiny_list_print_node (cur_node);
		cur_node = cur_node->next;
	}
	pthread_mutex_unlock (&list->mtx);
}


//============================================================
tiny_list_node_st* tiny_list_create_node (void *data, unsigned int len)
{
	tiny_list_node_st* new_node = (tiny_list_node_st*)malloc (sizeof (tiny_list_node_st));
	if (new_node == NULL)
	{
		return NULL;
	}
	
	new_node->data = malloc (len);
	if (new_node->data == NULL)
	{
		free (new_node);
		return NULL;
	}
	
	memcpy (new_node->data, data, len);
	new_node->len = len;
	new_node->next = NULL;
	new_node->last = NULL;
	return new_node;
}

//============================================================
tiny_list_node_st* tiny_list_destroy_node (tiny_list_node_st* node)
{
	tiny_list_node_st* temp = NULL;
	if (node == NULL)
	{
		return NULL;
	}
	
	temp = node->next;
	
	free (node->data);
	node->len = 0;
	node->next = NULL;
	node->last = NULL;
	free (node);
	
	return temp;
}

//============================================================
void tiny_list_print_node (tiny_list_node_st* node)
{
	if (node == NULL)
	{
		printf("empty node\n");
		return;
	}
	
	printf("	PTR ADDR: %08llX, FIRST BYTE: %02X, LEN: %d\n", (long)(node->data), ((unsigned char*)(node->data))[0], node->len);
}