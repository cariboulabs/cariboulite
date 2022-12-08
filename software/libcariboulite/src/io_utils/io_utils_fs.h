#ifndef __IO_UTILS_FS_H__
#define __IO_UTILS_FS_H__

#ifdef __cplusplus
extern "C" {
#endif

// files
int io_utils_file_exists(char* fname, int *size, int *dir, int *file, int *dev);
int io_utils_write_to_file(char* fname, char* data, int size_of_data);
int io_utils_read_from_file(char* fname, char* data, int len_to_read);
int io_utils_read_string_from_file(char* path, char* filename, char* data, int len);

// i2c
int io_utils_i2cbus_exists(void);
void io_utils_parse_command(char *line, char **argv);
int io_utils_probe_gpio_i2c(void);

// command execution
int io_utils_execute_command(char **argv);
int io_utils_execute_command_read(char *cmd, char* res, int res_size);

#ifdef __cplusplus
}
#endif

#endif // __IO_UTILS_FS_H__
