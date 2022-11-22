#include <stdio.h>
#include <stdint.h>

uint8_t lfsr(uint8_t n)
{
	uint8_t bit = ((n >> 2) ^ (n >> 3)) & 1;
	return (n >> 1) | (bit << 7);
}

int main1()
{
	uint8_t b = 0;
	uint8_t vec[131072] = {0};
	for (int i = 0; i < 131072; i++)
	{
		vec[i] = b;
		b += 251;
	}

	int count = 0;

	for (int i = 0; i < 131072; i++)
	{
		if (vec[i] != b)
			count ++;
		b += 251;
	}

	printf("num errors %d\n", count);
	return 0;
}

int main2()
{
	int b = 0x56;
	uint8_t vec[131072] = {0};
	for (int i = 0; i < 131072; i++)
	{
		vec[i] = b;
		b = lfsr(b);
		printf("%02X\n", b);
	}

	int count = 0;

	for (int i = 0; i < 131072; i++)
	{
		if (vec[i] != b)
			count ++;

		b = lfsr(b);
	}
	printf("num errors %d\n", count);
	return 0;
}

int main ()
{
	return main2();
}