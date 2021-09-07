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
       
        // generate a random number
        item = rand();

        int res = tsqueue_insert_push_buffer(&q, (uint8_t*)&item, sizeof(item), 0);
        if (res != 0) 
        {
            fprintf(stderr, " Producer report error condition\n");
        }
        else 
        {
            printf("producer produced %d\n", item);
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

        int res = tsqueue_pop_item(&q, &item);
        if (res != 0) 
        {
            fprintf(stderr, "Consumer report error condition\n");
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