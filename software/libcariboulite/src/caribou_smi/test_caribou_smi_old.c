#include <stdio.h>
#include "caribou_smi.h"


int main(int argc, char **argv)
{
int count = 4096*32;
uint32_t buffer[count];
uint8_t* b8 = (uint8_t*)buffer;
if (writeMode)
{
for (int i=0; i<count; i++)
{
buffer[i] = i;
}

write(fd, buffer, count*sizeof(uint32_t));
}
else
{
int hist[256] = {0};
for (int j = 0; j < 1; j++)
{
timeout_read(fd, (uint8_t*)buffer, count*sizeof(uint32_t));

for (int i = 1; i<count*sizeof(uint32_t); i++)
{
hist[(uint8_t)(b8[i] - b8[i-1])] ++;
}
}

printf("Histogram , buffer[0] = %d, %d, %d, %d\n", ((uint8_t*)buffer)[0], ((uint8_t*)buffer)[1], ((uint8_t*)buffer)[2], ((uint8_t*)buffer)[3]);
int error_bytes = 0;
int total_bytes = 0;
for (int i =0; i<256; i++)
{
if (hist[i]>0)
{
if (i != 1) error_bytes += hist[i];
total_bytes += hist[i];
printf("  %d: %d\n", i, hist[i]);
}
}
printf(" Byte Error Rate: %.10g, %d total, %d errors\n", (float)(error_bytes) / (float)(total_bytes), total_bytes, error_bytes);

//DumpHex(buffer, count*sizeof(uint32_t));
puts("\n");
}


close (fd);
return 0;

}