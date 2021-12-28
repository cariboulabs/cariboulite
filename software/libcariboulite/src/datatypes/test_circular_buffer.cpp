#include "circular_buffer.h"
#include <unistd.h>
#include <stdio.h>


circular_buffer<uint32_t> *buf = NULL;

uint32_t data1[100] = {0};
uint32_t data2[100] = {0};

void producer(int times) 
{
	while (true)
	{
		sleep(0);
		printf("THREAD! PUT1 100, ret %lu\n", buf->put(data1, 100));
		sleep(0);
		printf("PUT1 60, ret %lu\n", buf->put(data1, 60));
	}
}

void consumer(int times)
{
	printf("Capacity = %lu\n", buf->capacity());
	while (true)
	{	
		printf("GET1 100, ret %lu\n", buf->get(data2, 100));
		printf("GET1 100, ret %lu\n", buf->get(data2, 100));
		printf("GET1 100, ret %lu\n", buf->get(data2, 100));
		printf("GET1 100, ret %lu\n", buf->get(data2, 60));
	}

	printf("finished\n");
}

int main ()
{
	for (int i = 0; i < 100; i++)
	{
		data1[i] = i;
		data2[i] = i*i;
	}

	buf = new circular_buffer<uint32_t>(200, true, true);

	int times = 1;
	std::thread t1(producer, times);
	std::thread t2(consumer, times);

	t2.join();
	t1.join();

	delete buf;
	return 0;
}