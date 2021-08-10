#include "bcm2835_smi.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static void fail(const char *msg) {
  perror(msg);
  exit(1);
} 

static void print_smi_settings(struct smi_settings *settings) {
  printf("width: %d\n", settings->data_width);
  printf("pack: %c\n", settings->pack_data ? 'Y' : 'N');
  printf("read setup: %d, strobe: %d, hold: %d, pace: %d\n", settings->read_setup_time, settings->read_strobe_time, settings->read_hold_time, settings->read_pace_time);
  printf("write setup: %d, strobe: %d, hold: %d, pace: %d\n", settings->write_setup_time, settings->write_strobe_time, settings->write_hold_time, settings->write_pace_time);
  printf("dma enable: %c, passthru enable: %c\n", settings->dma_enable ? 'Y':'N', settings->dma_passthrough_enable ? 'Y':'N');
  printf("dma threshold read: %d, write: %d\n", settings->dma_read_thresh, settings->dma_write_thresh);
  printf("dma panic threshold read: %d, write: %d\n", settings->dma_panic_read_thresh, settings->dma_panic_write_thresh);
}

int main(int argc, char **argv) {
  int count = 8;
  int opt;
  int fd = open("/dev/smi", O_RDWR);
  if (fd < 0) fail("cant open");
  struct smi_settings settings;
  int ret = ioctl(fd, BCM2835_SMI_IOC_GET_SETTINGS, &settings);
  if (ret != 0) fail("ioctl 1");
  settings.read_setup_time = 10;
  settings.read_strobe_time = 20;
  settings.read_hold_time = 40;
  settings.read_pace_time = 80;
  bool writeMode = false;

  while ((opt = getopt(argc, argv, "b:e:s:h:p:wE:S:H:P:")) != -1) {
    switch (opt) {
    case 'b':
      count = atoi(optarg);
      break;
    case 'e':
      settings.read_setup_time = atoi(optarg);
      break;
    case 's':
      settings.read_strobe_time = atoi(optarg);
      break;
    case 'h':
      settings.read_hold_time = atoi(optarg);
      break;
    case 'p':
      settings.read_pace_time = atoi(optarg);
      break;
    case 'E':
      settings.write_setup_time = atoi(optarg);
      break;
    case 'S':
      settings.write_strobe_time = atoi(optarg);
      break;
    case 'H':
      settings.write_hold_time = atoi(optarg);
      break;
    case 'P':
      settings.write_pace_time = atoi(optarg);
      break;
    case 'w':
      writeMode = true;
      break;
    }
  }

  ret = ioctl(fd, BCM2835_SMI_IOC_WRITE_SETTINGS, &settings);
  if (ret != 0) fail("ioctl 2");
  print_smi_settings(&settings);
  uint16_t buffer[count];
  if (writeMode) {
    for (int i=0; i<count; i++) buffer[i] = i;
    write(fd, buffer, count*2);
  } else {
    read(fd, buffer, count*2);
    for (int i=0; i<count; i++) printf("%02x ", buffer[i]);
    puts("\n");
  }
}