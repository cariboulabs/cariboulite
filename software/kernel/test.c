#include "bcm2835_smi.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static void print_smi_settings(struct smi_settings *settings)
{
  printf("width: %d\n", settings->data_width);
  printf("pack: %c\n", settings->pack_data ? 'Y' : 'N');
  printf("read setup: %d, strobe: %d, hold: %d, pace: %d\n", settings->read_setup_time, settings->read_strobe_time, settings->read_hold_time, settings->read_pace_time);
  printf("write setup: %d, strobe: %d, hold: %d, pace: %d\n", settings->write_setup_time, settings->write_strobe_time, settings->write_hold_time, settings->write_pace_time);
  printf("dma enable: %c, passthru enable: %c\n", settings->dma_enable ? 'Y':'N', settings->dma_passthrough_enable ? 'Y':'N');
  printf("dma threshold read: %d, write: %d\n", settings->dma_read_thresh, settings->dma_write_thresh);
  printf("dma panic threshold read: %d, write: %d\n", settings->dma_panic_read_thresh, settings->dma_panic_write_thresh);
}

static void setup_settings (struct smi_settings *settings)
{
  settings->read_setup_time = 1;
  settings->read_strobe_time = 3;
  settings->read_hold_time = 1;
  settings->read_pace_time = 2;
  settings->write_setup_time = 1;
	settings->write_hold_time = 1;
	settings->write_pace_time = 2;
	settings->write_strobe_time = 3;
  settings->data_width = SMI_WIDTH_8BIT;
  settings->dma_enable = 1;
  settings->pack_data = 1;
  settings->dma_passthrough_enable = 1;
}

void DumpHex(const void* data, size_t size)
{
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			printf(" ");
			if ((i+1) % 16 == 0) {
				printf("|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}



void timeout_read(int filedesc, char* buffer, int size_of_buf)
{
  fd_set set;
  struct timeval timeout;
  int rv;
  char *buff = buffer;
  int len = size_of_buf;
  //int filedesc = open( "dev/ttyS0", O_RDWR );

  FD_ZERO(&set); /* clear the set */
  FD_SET(filedesc, &set); /* add our file descriptor to the set */

  timeout.tv_sec = 1;
  timeout.tv_usec = 0;

  rv = select(filedesc + 1, &set, NULL, NULL, &timeout);
  if(rv == -1)
    perror("select"); /* an error accured */
  else if(rv == 0)
    printf("timeout"); /* a timeout occured */
  else
    read( filedesc, buff, len ); /* there was data to read */
}


int main(int argc, char **argv)
{
  int fd = open("/dev/smi", O_RDWR);
  if (fd < 0)
  {
    perror("can't open smi driver file");
    exit(1);
  }

  struct smi_settings settings;

  int ret = ioctl(fd, BCM2835_SMI_IOC_GET_SETTINGS, &settings);
  if (ret != 0)
  {
    perror("ioctl 1");
    close (fd);
    exit(1);
  }

  printf("Current settings:\n");
  print_smi_settings(&settings);

  setup_settings(&settings);

  ret = ioctl(fd, BCM2835_SMI_IOC_WRITE_SETTINGS, &settings);
  if (ret != 0)
  {
    perror("ioctl 1");
    close (fd);
    exit(1);
  }

  ret = ioctl(fd, BCM2835_SMI_IOC_ADDRESS, (5<<1));

  printf("\n\nNEW settings:\n");
  print_smi_settings(&settings);


  bool writeMode = false;
  //    writeMode = true;

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