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
  settings->read_strobe_time = 1;
  settings->read_hold_time = 1;
  settings->read_pace_time = 1;
  settings->write_setup_time = 1;
	settings->write_hold_time = 1;
	settings->write_pace_time = 1;
	settings->write_strobe_time = 1;
  settings->data_width = SMI_WIDTH_8BIT;
  settings->dma_enable = 1;
  settings->pack_data = 1;
  settings->dma_passthrough_enable = 1;
}

int main(int argc, char **argv) 
{
  int fd = open("/dev/smi", O_RDWR);
  if (fd < 0) 
  {
    perror("cant open");
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
  
  printf("\n\nNEW settings:\n");
  print_smi_settings(&settings);


  bool writeMode = false;
  //    writeMode = true;

  int count = 512;
  uint32_t buffer[512];   // 512 samples
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
    read(fd, buffer, count*sizeof(uint32_t));
    
    printf("\n\nread words:\n");
    for (int i=0; i<count; i++) 
    {
      printf("%02x ", buffer[i]);
    }
    puts("\n");
  }


   close (fd);
  return 0;

}