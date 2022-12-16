#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "IO_UTILS_FS"
#include "zf_log/zf_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>
#include <endian.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include  <sys/types.h>

#include "io_utils_fs.h"

//===========================================================
int io_utils_file_exists(char* fname, int *size, int *dir, int *file, int *dev)
{
	struct stat st;
	if(stat(fname,&st) != 0)
	{
    	return 0;
	}

	if (dir) *dir = S_ISDIR(st.st_mode);
	if (file) *file = S_ISREG(st.st_mode);
	if (dev) *dev =  S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode);
	if (size) *size = st.st_size;

	return 1;
}

//===========================================================
int io_utils_write_to_file(char* fname, char* data, int size_of_data)
{
	FILE* fid = NULL;

	fid = fopen(fname, "wb");
	if (fid == NULL)
	{
		ZF_LOGE("opening file '%s' for writing failed", fname);
		return -1;
	}
	int wrote = fwrite(data, 1, size_of_data, fid);
	if (wrote != size_of_data)
	{
		ZF_LOGE("Writing to file failed (wrote %d instead of %d)", wrote, size_of_data);
		fclose(fid);
		return -1;
	}
	return fclose(fid);
}

//===========================================================
int io_utils_read_from_file(char* fname, char* data, int len_to_read)
{
	FILE* fid = NULL;

	fid = fopen(fname, "rb");
	if (fid == NULL)
	{
		ZF_LOGE("opening file '%s' for reading failed", fname);
		return -1;
	}
	int bytes_read = fread(data, 1, len_to_read, fid);
	if (bytes_read != len_to_read)
	{
		ZF_LOGE("Reading from file failed (read %d instead of %d)", bytes_read, len_to_read);
		fclose(fid);
		return -1;
	}
	return fclose(fid);
}


//===========================================================
int io_utils_read_string_from_file(char* path, char* filename, char* data, int len)
{
	FILE* fid = NULL;
    int retval =  0;

	char full_path[128] = {0};
	sprintf(full_path, "%s/%s", path, filename);

	fid = fopen(full_path, "r");
	if (fid == NULL) 
	{
		ZF_LOGE("opening file '%s' for reading failed", full_path);
		return -1;
	}

	if (fgets(data, len, fid) == NULL)
	{
		ZF_LOGE("reading from '%s' failed", full_path);
        retval = -1;
	}
	fclose(fid);
    return retval;
}


//===========================================================
int io_utils_i2cbus_exists(void)
{
	int dev = 0;
	// first check 'i2c-9'
	if ( io_utils_file_exists("/dev/i2c-9", NULL, NULL, NULL, &dev) )
	{
		if (dev) return 9;
		ZF_LOGE("i2c-9 was found but not a valid device file");
	}

	// then check 'i2c-0'
	if ( io_utils_file_exists("/dev/i2c-0", NULL, NULL, NULL, &dev) )
	{
		if (dev) return 0;
		ZF_LOGE("i2c-0 was found but not a valid device file");
	}
	return -1;
}

//===========================================================
void io_utils_parse_command(char *line, char **argv)
{
     while (*line != '\0') {       /* if not the end of line ....... */
          while (*line == ' ' || *line == '\t' || *line == '\n')
               *line++ = '\0';     /* replace white spaces with 0    */
          *argv++ = line;          /* save the argument position     */
          while (*line != '\0' && *line != ' ' &&
                 *line != '\t' && *line != '\n')
               line++;             /* skip the argument until ...    */
     }
     *argv = '\0';                 /* mark the end of argument list  */
}

//===========================================================
int io_utils_probe_gpio_i2c(void)
{
	ZF_LOGI("trying to modprobe i2c_dev");
	char modprobe[] = "/usr/sbin/modprobe i2c_dev";
	char *argv[64];
	io_utils_parse_command(modprobe, argv);
	if (io_utils_execute_command(argv) != 0)
	{
		ZF_LOGE("MODPROBE of the eeprom 'i2c_dev' execution failed");
		return -1;
	}

	char dtoverlay[] = "/usr/bin/dtoverlay i2c-gpio i2c_gpio_sda=0 i2c_gpio_scl=1 bus=9";
	io_utils_parse_command(dtoverlay, argv);
	if (io_utils_execute_command(argv) != 0)
	{
		ZF_LOGE("DTOVERLAY execution failed");
		return -1;
	}

	int dev = 0;
	if (io_utils_file_exists("/dev/i2c-9", NULL, NULL, NULL, &dev))
	{
		if (dev) return 0;
		ZF_LOGE("i2c-9 was found but it is not a valid device file");
	}
	else
	{
		ZF_LOGE("i2c-9 was not found");
	}

	return -1;
}

//===========================================================
int io_utils_execute_command(char **argv)
{
     pid_t  pid;
     int    status;

     if ((pid = fork()) < 0) {     // fork a child process
          printf("*** ERROR: forking child process failed\n");
          return -1;
     }
     else if (pid == 0) {          // for the child process:
          if (execvp(*argv, argv) < 0) {     // execute the command
               printf("*** ERROR: exec failed\n");
               exit(1);
          }
     }
     else {                                  /* for the parent:      */
          while (wait(&status) != pid)       /* wait for completion  */
               ;
     }
	 return status;
}

//===========================================================
pid_t io_utils_execute_command_parallel(char **argv)
{
	pid_t  pid;

	// fork a child process
	if ((pid = fork()) < 0) 
	{
		printf("*** ERROR: forking child process failed\n");
		return -1;
	}
	// for the child process:
	else if (pid == 0) 
	{          
		int r = prctl(PR_SET_PDEATHSIG, SIGTERM);
		if (r == -1) 
		{ 
			perror(0); 
			exit(1); 
		}
		// test in case the original parent exited just
		// before the prctl() call
		if (getppid() == 1)
		{
			exit(1);
		}

		// execute the command
		if (execvp(*argv, argv) < 0) 
		{
			printf("*** ERROR: exec failed\n");
			exit(1);
		}
	}
	return pid;
}

//===========================================================
int io_utils_wait_command_parallel(pid_t pid)
{
	int    status;
	while (wait(&status) != pid) {}
    return status;
}

//=======================================================================================
int io_utils_execute_command_read(char *cmd, char* res, int res_size)
{
    int i = 0;
    FILE *p = popen(cmd,"r");
    if (p != NULL )
    {
        while (!feof(p) && (i < res_size) )
        {
            fread(&res[i++],1,1,p);
        }
        res[i] = 0;
        //printf("%s",res);
        pclose(p);
        return 0;
    }
    else
    {
        return -1;
    }
}