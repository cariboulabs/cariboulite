#include "tsqueue.h"
#include <string.h>

//=======================================================================
int tsqueue_init(tsqueue_st* q, int item_size_bytes, int total_element_count)
{
    if (q == NULL) return -1;
    if (item_size_bytes == 0 || total_element_count == 0) return -2;

    q->sema_inited = 0;
    if (pthread_mutex_init(&q->mutex, NULL) != 0)
    {
        return -4;
    }

    if (sem_init(&q->full, 0, 0) != 0)
    {
        pthread_mutex_destroy(&q->mutex);
        return -4;
    }

    if (sem_init(&q->empty, 0, total_element_count) != 0)
    {
        sem_destroy(&q->full);
        pthread_mutex_destroy(&q->mutex);
        return -4;
    }

    q->sema_inited = 1;

    q->items = (tsqueue_item_st*)malloc(total_element_count*sizeof(tsqueue_item_st));
    if (q->items == NULL)
    {
        return -3;
    }

    memset (q->items, 0, total_element_count*sizeof(tsqueue_item_st));

    for (int i = 0; i < total_element_count; i++)
    {
        q->items[i].metadata = 0;
        q->items[i].data = (uint8_t*)malloc(q->item_size_bytes);
        if (q->items[i].data == NULL)
        {
            tsqueue_release(q);
            return -3;
        }
        q->items[i].length = 0;
    }

    q->max_num_items = total_element_count;
    q->item_size_bytes = item_size_bytes;
    q->current_num_of_items = 0;
    q->num_items_dropped = 0;
    return 0;
}

//=======================================================================
int tsqueue_release(tsqueue_st* q)
{
    if (q == NULL) return -1;
    for (int i = 0; i < q->max_num_items; i++)
    {
        if (q->items[i].data != NULL) free (q->items[i].data);
    }
    free(q->items);

    if (q->sema_inited)
    {
        sem_destroy(&q->empty);
        sem_destroy(&q->full);
        pthread_mutex_destroy(&q->mutex);
    }
    return 0;
}

//=======================================================================
int tsqueue_insert_push_item(tsqueue_st* q, tsqueue_item_st* item)
{
    return tsqueue_insert_push_buffer(q, item->data, item->length, item->metadata);
}

//=======================================================================
int tsqueue_insert_push_buffer(tsqueue_st* q, uint8_t* buffer, int length, uint32_t metadata)
{
    int error = 0;
    if (q == NULL) return -1;

    // if the queue is full, drop the new data
    if (tsqueue_number_of_items(q) >= q->max_num_items) 
    {
        q->num_items_dropped ++;
        return -1;
    }

    // acquire the empty lock
    sem_wait(&q->empty);

    // acquire the mutex lock
    pthread_mutex_lock(&q->mutex);

    // insert the item
    // When the buffer is not full add the item and increment the counter
    if(q->current_num_of_items < q->max_num_items) 
    {
        int data_size = length;
        q->items[q->current_num_of_items].metadata = metadata;
        if (data_size > q->item_size_bytes)
        {
            printf("Incorrect item data size\n");
            data_size = q->item_size_bytes;
        }
        memcpy (q->items[q->current_num_of_items].data, buffer, data_size);
        q->items[q->current_num_of_items].length = data_size;
        q->current_num_of_items ++;
    } else error = 1;

    // release the mutex lock
    pthread_mutex_unlock(&q->mutex);
    // signal full
    sem_post(&q->full);

    return -error;
}

//=======================================================================
int tsqueue_pop_item(tsqueue_st* q, tsqueue_item_st** item)
{
    int error = 0;
    if (q == NULL) return -1;

    sem_wait(&q->full);
    // aquire the mutex lock
    pthread_mutex_lock(&q->mutex);

    // pop the item
    if(q->current_num_of_items > 0) 
    {
        int popped_item = q->current_num_of_items-1;
        *item = &q->items[popped_item];
        q->current_num_of_items --;
    } else error = 1;

    // release the mutex lock
    pthread_mutex_unlock(&q->mutex);
    // signal empty
    sem_post(&q->empty);
    return -error;
}

//=======================================================================
int tsqueue_number_of_items(tsqueue_st* q)
{
    int num_items = 0;
    if (q == NULL) return -1;
    // acquire the mutex lock
    pthread_mutex_lock(&q->mutex);

    num_items = q->current_num_of_items;
    // release the mutex lock
    pthread_mutex_unlock(&q->mutex);
    return num_items;
}

//=======================================================================
int tsqueue_reset_dropped_counter(tsqueue_st* q)
{
    if (q == NULL) return -1;
    pthread_mutex_lock(&q->mutex);
    q->num_items_dropped = 0;
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

//=======================================================================
int tsqueue_get_number_of_dropped(tsqueue_st* q)
{
    int num_items = 0;
    if (q == NULL) return -1;
    pthread_mutex_lock(&q->mutex);
    num_items = q->num_items_dropped;
    pthread_mutex_unlock(&q->mutex);
    return num_items;
}