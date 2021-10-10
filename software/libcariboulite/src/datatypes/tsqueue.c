#include "tsqueue.h"
#include <string.h>
#include <errno.h>
#include <time.h>

//=======================================================================
int tsqueue_init(tsqueue_st* q, int item_size_bytes, int total_element_count)
{
    if (q == NULL) return -1;
    if (item_size_bytes == 0 || total_element_count == 0) return -2;

    q->max_num_items = total_element_count;
    q->item_size_bytes = item_size_bytes;

    q->sema_inited = 0;
    if (pthread_mutex_init(&q->mutex, NULL) != 0)
    {
        //printf("tsqueue1\n");
        return TSQUEUE_MUTEX_INIT_FAILED;
    }

    if (sem_init(&q->full, 0, 0) != 0)
    {
        //printf("tsqueue2\n");
        pthread_mutex_destroy(&q->mutex);
        return TSQUEUE_MUTEX_INIT_FAILED;
    }

    if (sem_init(&q->empty, 0, total_element_count) != 0)
    {
        //printf("tsqueue3\n");
        sem_destroy(&q->full);
        pthread_mutex_destroy(&q->mutex);
        return TSQUEUE_SEM_INIT_FAILED;
    }

    q->sema_inited = 1;

    q->items = (tsqueue_item_st*)malloc(total_element_count*sizeof(tsqueue_item_st));
    if (q->items == NULL)
    {
        //printf("tsqueue4\n");
        return TSQUEUE_MEM_ALLOC_FAILED;
    }

    memset (q->items, 0, total_element_count*sizeof(tsqueue_item_st));

    for (int i = 0; i < total_element_count; i++)
    {
        q->items[i].metadata = 0;
        q->items[i].data = (uint8_t*)malloc(q->item_size_bytes);
        if (q->items[i].data == NULL)
        {
            //printf("tsqueue5 %d\n", i);
            tsqueue_release(q);
            return TSQUEUE_MEM_ALLOC_FAILED;
        }
        q->items[i].length = 0;
    }

    q->current_num_of_items = 0;
    q->head = 0;
    q->tail = 0;
    q->num_items_dropped = 0;
    return TSQUEUE_OK;
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
    return TSQUEUE_OK;
}

//=======================================================================
int tsqueue_insert_push_item(tsqueue_st* q, tsqueue_item_st* item, int timeout_us)
{
    return tsqueue_insert_push_buffer(q, item->data, item->length, item->metadata, timeout_us);
}


//=======================================================================
void tsqueue_add_ms_to_timespec(struct timespec *ts, int us)
{
    long int sec = ts->tv_sec;
    long int nsec = ts->tv_nsec;
    long int added_ns = us * 1000L;
    nsec += added_ns;
    int added_sec = nsec / 1000000000L;
    sec += added_sec;
    nsec -= added_sec * 1000000000L;
    ts->tv_nsec = nsec;
    ts->tv_sec = sec;
}

//=======================================================================
int tsqueue_wait_on_sem(sem_t* sem, int timeout_us)
{
    // check if timeout is needed
    if (timeout_us <= 0)
    {
        int s = 0;
        while ((s = sem_wait(sem)) == -1 && errno == EINTR) continue;
        if (s == -1) return TSQUEUE_SEM_FAILED;
    }
    else
    {
        struct timespec ts = {0};
        int s = 0;

        // get current time
        clock_gettime(CLOCK_REALTIME, &ts);
        tsqueue_add_ms_to_timespec(&ts, timeout_us);
        while ((s = sem_timedwait(sem, &ts)) == -1 && errno == EINTR)
        {
            continue;       /* Restart if interrupted by handler. */
        }

        // Check what happened - it was not an interruption
        if (s == -1) 
        {
            if (errno == ETIMEDOUT)
            {
                return TSQUEUE_TIMEOUT;
            }
            else
            {
                return TSQUEUE_SEM_FAILED;
            }
        }
    }
    return 0;
}

//=======================================================================
int tsqueue_insert_push_buffer(tsqueue_st* q, uint8_t* buffer, int length, uint32_t metadata, int timeout_us)
{
    int error = 0;
    if (q == NULL) return TSQUEUE_NOT_INITIALIZED;

    int res = tsqueue_wait_on_sem(&q->empty, timeout_us);
    if (res < 0) return res;
    
    // acquire the mutex lock
    pthread_mutex_lock(&q->mutex);

    // insert the item
    // When the buffer is not full add the item and increment the counter
    if(q->current_num_of_items < q->max_num_items) 
    {
        int data_size = length;
        q->items[q->head].metadata = metadata;
        if (data_size > q->item_size_bytes)
        {
            printf("Incorrect item data size\n");
            data_size = q->item_size_bytes;
        }
        memcpy (q->items[q->head].data, buffer, data_size);
        q->items[q->head].length = data_size;
        q->head ++;
        if (q->head >= q->max_num_items) q->head = 0;
        q->current_num_of_items ++;
    } 
    else 
    {
        error = TSQUEUE_FAILED_FULL;
    }

    // release the mutex lock
    pthread_mutex_unlock(&q->mutex);

    // signal full
    sem_post(&q->full);

    return error;
}

//=======================================================================
int tsqueue_peek_item(tsqueue_st* q, tsqueue_item_st** item, int timeout_us)
{
    int error = 0;
    if (q == NULL) return TSQUEUE_NOT_INITIALIZED;

    int res = tsqueue_wait_on_sem(&q->full, timeout_us);
    if (res < 0) return res;

    // aquire the mutex lock
    pthread_mutex_lock(&q->mutex);

    // pop the item
    if(q->current_num_of_items > 0) 
    {
        *item = &q->items[q->tail];
    } 
    else 
    {
        error = TSQUEUE_FAILED_EMPTY;
    }

    // release the mutex lock
    pthread_mutex_unlock(&q->mutex);
    // signal full to note that we didn't pop the item
    sem_post(&q->full);
    return error;
}

//=======================================================================
int tsqueue_pop_item(tsqueue_st* q, tsqueue_item_st** item, int timeout_us)
{
    int error = 0;
    if (q == NULL) return TSQUEUE_NOT_INITIALIZED;

    int res = tsqueue_wait_on_sem(&q->full, timeout_us);
    if (res < 0) return res;

    // aquire the mutex lock
    pthread_mutex_lock(&q->mutex);

    // pop the item
    if(q->current_num_of_items > 0) 
    {
        *item = &q->items[q->tail];
        q->tail ++;
        if (q->tail >= q->max_num_items) q->tail = 0;
        q->current_num_of_items --;
    } 
    else 
    {
        error = TSQUEUE_FAILED_EMPTY;
    }

    // release the mutex lock
    pthread_mutex_unlock(&q->mutex);
    // signal empty
    sem_post(&q->empty);
    return error;
}

//=======================================================================
int tsqueue_number_of_items(tsqueue_st* q)
{
    int num_items = 0;
    if (q == NULL) return TSQUEUE_NOT_INITIALIZED;
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
    if (q == NULL) return TSQUEUE_NOT_INITIALIZED;
    pthread_mutex_lock(&q->mutex);
    q->num_items_dropped = 0;
    pthread_mutex_unlock(&q->mutex);
    return TSQUEUE_OK;
}

//=======================================================================
int tsqueue_get_number_of_dropped(tsqueue_st* q)
{
    int num_items = 0;
    if (q == NULL) return TSQUEUE_NOT_INITIALIZED;
    pthread_mutex_lock(&q->mutex);
    num_items = q->num_items_dropped;
    pthread_mutex_unlock(&q->mutex);
    return num_items;
}