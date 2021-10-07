#include <stdio.h>
#include <unistd.h>
#include "tsqueue.h"

tsqueue_st q = {0};
pthread_t tid;
pthread_attr_t attr;
#define RAND_DIVISOR 1000000000

// Producer Thread
void *producer(void *param) 
{
    int item;

    while(1) 
    {
        // sleep for a random period of time
        /*int rNum = rand() / RAND_DIVISOR;
        sleep(rNum);
        */

        //getchar();
       
        // generate a random number
        item = rand();

        int res = tsqueue_insert_push_buffer(&q, (uint8_t*)&item, sizeof(item), 0, 200);
        if (res != 0) 
        {
            if (res == TSQUEUE_NOT_INITIALIZED) fprintf(stderr, "Queue not inited\n");
            if (res == TSQUEUE_PUSH_FAILED_FULL) fprintf(stderr, "Queue is full!\n");
            if (res == TSQUEUE_PUSH_SEM_FAILED) fprintf(stderr, "Push semaphore failed\n");
            if (res == TSQUEUE_PUSH_TIMEOUT) fprintf(stderr, "Push TIMEOUT\n");
        }
        else 
        {
            printf("producer produced and pushed %d\n", item);
        }
    }
}

// Consumer Thread
void *consumer(void *param) 
{
    tsqueue_item_st* item = NULL;

    while(1) 
    {
        /* sleep for a random period of time */
        //int rNum = rand() / RAND_DIVISOR;
        //sleep(rNum);

        //getchar();

        int res = tsqueue_pop_item(&q, &item, 200);
        if (res != 0) 
        {
            if (res == TSQUEUE_NOT_INITIALIZED) fprintf(stderr, "Queue not inited\n");
            if (res == TSQUEUE_POP_SEM_FAILED) fprintf(stderr, "Pop semaphore failed\n");
            if (res == TSQUEUE_POP_FAILED_EMPTY) fprintf(stderr, "Pop failed - Empty!!\n");
            if (res == TSQUEUE_POP_TIMEOUT) fprintf(stderr, "Pop TIMEOUT\n");
        }
        else 
        {
            int val = *((uint32_t*)item->data);
            printf("consumer consumed %d\n", val);
        }
    }
}

// Main
int main(int argc, char *argv[])
{
    int res = 0;

    if(argc != 4)
    {
        fprintf(stderr, "USAGE:./main.out <INT> <INT> <INT>\n");
        return 1;
    }

    int mainSleepTime = atoi(argv[1]); /* Time in seconds for main to sleep */
    int numProd = atoi(argv[2]); /* Number of producer threads */
    int numCons = atoi(argv[3]); /* Number of consumer threads */

    res = tsqueue_init(&q, 4, 5);

    // Create the producer threads
    for(int i = 0; i < numProd; i++)
    {
        pthread_create(&tid, &attr, producer,NULL);
    }

    // Create the consumer threads
    for(int i = 0; i < numCons; i++)
    {
        pthread_create(&tid, &attr, consumer,NULL);
    }

    sleep(mainSleepTime);
    printf("Exit the program\n");

    res = tsqueue_release(&q);
    return 0;
}