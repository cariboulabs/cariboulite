#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <pthread.h>
#include "../caribou_smi.h"
#include "bcm2835_smi.h"


#define SMI_STREAM_IOC_GET_NATIVE_BUF_SIZE		_IO(BCM2835_SMI_IOC_MAGIC, 3)
#define SMI_STREAM_IOC_SET_NON_BLOCK_READ	 	_IO(BCM2835_SMI_IOC_MAGIC, 4)
#define SMI_STREAM_IOC_SET_NON_BLOCK_WRITE	 	_IO(BCM2835_SMI_IOC_MAGIC, 5)
#define SMI_STREAM_IOC_SET_STREAM_STATUS	 	_IO(BCM2835_SMI_IOC_MAGIC, 6)

static void setup_settings (struct smi_settings *settings)
{
    settings->read_setup_time = 0;
    settings->read_strobe_time = 5;
    settings->read_hold_time = 0;
    settings->read_pace_time = 0;
    settings->write_setup_time = 0;
    settings->write_hold_time = 0;
    settings->write_pace_time = 0;
    settings->write_strobe_time = 4;
    settings->data_width = SMI_WIDTH_8BIT;
    settings->dma_enable = 1;
    settings->pack_data = 1;
    settings->dma_passthrough_enable = 1;
}

pthread_t tid;
int fd = -1;
size_t native_batch_length_bytes = 0;
int thread_running = 0;

void* read_thread(void *arg)
{  
	fd_set set;
	int rv;
	int timeout_num_millisec = 500;
	uint8_t *buffer = malloc(native_batch_length_bytes);
	int size_of_buf = native_batch_length_bytes;

    while (thread_running)
	{
		while (1)
		{
			struct timeval timeout = {0};
			FD_ZERO(&set);       // clear the set mask
			FD_SET(fd, &set);    // add our file descriptor to the set - and only it
			int num_sec = timeout_num_millisec / 1000;
    		timeout.tv_sec = num_sec;
    		timeout.tv_usec = (timeout_num_millisec - num_sec*1000) * 1000;

			rv = select(fd + 1, &set, NULL, NULL, &timeout);
			if(rv == -1)
			{
				int error = errno;
				switch(error)
				{
					case EINTR:	        // A signal was caught.
						continue;

					case EBADF:         // An invalid file descriptor was given in one of the sets. 
										// (Perhaps a file descriptor that was already closed, or one on which an error has occurred.)
					case EINVAL:        // nfds is negative or the value contained within timeout is invalid.
					case ENOMEM:        // unable to allocate memory for internal tables.
					default: goto exit;
				};
			}
			else if (rv == 0)
			{
				printf("Read poll timeout\n");
				break;
			}
			else if (FD_ISSET(fd, &set))
			{
				int num_read = read(fd, buffer, size_of_buf);
				printf("Read %d bytes\n", num_read);
				break;
			}
		}
	}

exit:
	free(buffer);
    return NULL;
}


int main()
{
	char smi_file[] = "/dev/smi";
    struct smi_settings settings = {0};

    fd = open(smi_file, O_RDWR);
    if (fd < 0)
    {
        printf("can't open smi driver file '%s'\n", smi_file);
        return -1;
    }

	// Get the current settings
    int ret = ioctl(fd, BCM2835_SMI_IOC_GET_SETTINGS, &settings);
    if (ret != 0)
    {
        printf("failed reading ioctl from smi fd (settings)\n");
        close (fd);
        return -1;
    }

	// apply the new settings
    setup_settings(&settings);
    ret = ioctl(fd, BCM2835_SMI_IOC_WRITE_SETTINGS, &settings);
    if (ret != 0)
    {
        printf("failed writing ioctl to the smi fd (settings)\n");
        close (fd);
        return -1;
    }

	// set the address to idle
    ret = ioctl(fd, BCM2835_SMI_IOC_ADDRESS, caribou_smi_address_idle);
    if (ret != 0)
    {
        printf("failed setting smi address (idle / %d) to device\n", caribou_smi_address_idle);
        close (fd);
        return -1;
    }

	// get the native batch length in bytes
	ret = ioctl(fd, SMI_STREAM_IOC_GET_NATIVE_BUF_SIZE, &native_batch_length_bytes);
    if (ret != 0)
    {
        printf("failed reading native batch length, setting default\n");
		native_batch_length_bytes = (1024)*(1024)/2;
    }
	printf("Native batch size: %u\n", native_batch_length_bytes);

	// start streaming data
	ret = ioctl(fd, SMI_STREAM_IOC_SET_STREAM_STATUS, 1);

	// start the reader thread
	thread_running = 1;
	int err = pthread_create(&tid, NULL, &read_thread, NULL);
	if (err != 0)
	{
		printf("\ncan't create thread :[%s]", strerror(err));
	}

	getchar();
	thread_running = 0;

	pthread_join(tid, NULL);

	ret = ioctl(fd, SMI_STREAM_IOC_SET_STREAM_STATUS, 0);

	close (fd);
    return 0;
}