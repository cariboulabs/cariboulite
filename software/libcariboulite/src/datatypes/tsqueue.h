#ifndef __TSQUEUE_H__
#define __TSQUEUE_H__

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

typedef struct
{
    uint8_t *data;
    int length;
    uint32_t metadata;  // can be a pointer context, a number, a set of flags, etc.
} tsqueue_item_st;

typedef struct
{
    tsqueue_item_st *items;
    int max_num_items;
    int item_size_bytes;
    int current_num_of_items;

    // The mutex lock, and the semaphores
    pthread_mutex_t mutex;
    sem_t full, empty;
    int sema_inited;

    int num_items_dropped;
} tsqueue_st;

/*
 * Init the queue
 *  q: a pointer to a pre-allocated structure
 *  item_size_bytes: the maximal container size of each item in the queue (bytes)
 *  total_element_count: the total capacity of the cyclic queue
 */
int tsqueue_init(tsqueue_st* q, int item_size_bytes, int total_element_count);

/*
 * Release the queue
 */
int tsqueue_release(tsqueue_st* q);

/*
 * Push an item to the queue
 *  item: the contents of the item to be pushed. The item contents are cloned (copied)
 *        to the internal local copy, thus, this parameter may be changed as needed after the call
 * Note: if the queue is full, the function returned immediatelly with an error. This dunctionality can
 *       be changed by letting the function block until space is freed up in the queue. When there is not
 *       sufficient space in the queue, the item is dropped and the "dropped" counter is increased.
 */
int tsqueue_insert_push_item(tsqueue_st* q, tsqueue_item_st* item);

/*
 * Push a buffer to the queue
 *  The same as before, but rather than an "item", a specific buffer, length and metadata are
 *  cloned into the internal item local copy, and pushed to the queue
 */
int tsqueue_insert_push_buffer(tsqueue_st* q, uint8_t* buffer, int length, uint32_t metadata);

/*
 * Pop an item from the queue
 *  item: the output of the function is a pointer to a single cell in the queue that has been
 *        popped. This item pointer is not cloned to the user but rather passed by reference.
 *        it is up to the user to create his own private copy of its contents as soon as he gets
 *        the pointer.
 * Note: this function blocked until there is anything to pop. If the queue is empty, the queue will
 *       hold the calling thread in a blocking condition.
 */
int tsqueue_pop_item(tsqueue_st* q, tsqueue_item_st** item);

/*
 * Return the current number of items in the queue
 */
int tsqueue_number_of_items(tsqueue_st* q);

/*
 * Clears the "dropped" counter value to zero.
 */
int tsqueue_reset_dropped_counter(tsqueue_st* q);

/*
 * Return the current count value of the dropped items - those we wanted to push but couldn't due
 * to insufficient capacity
 */
int tsqueue_get_number_of_dropped(tsqueue_st* q);

#endif // __TSQUEUE_H__