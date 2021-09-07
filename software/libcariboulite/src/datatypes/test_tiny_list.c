#include <stdio.h>
#include "tiny_list.h"
#include "tiny_queue.h"

int a = 0;

tiny_queue_st queue;

int main ()
{
	tiny_queue_init (&queue);
	
	tiny_queue_enqueue (queue, &a, sizeof (a)); a++;
	tiny_queue_enqueue (queue, &a, sizeof (a)); a++;
	tiny_queue_enqueue (queue, &a, sizeof (a)); a++;
	
	tiny_list_print (queue);
	
	tiny_queue_dequeue (queue, &a, NULL);
	tiny_list_print (queue);
	tiny_queue_dequeue (queue, &a, NULL);
	tiny_list_print (queue);
	tiny_queue_dequeue (queue, &a, NULL);
	tiny_list_print (queue);
	
	tiny_queue_free (queue);
}