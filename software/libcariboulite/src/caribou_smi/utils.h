#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>

void set_realtime_priority(int priority_deter);
void dump_hex(const void* data, size_t size);
void dump_bin(const uint8_t* data, size_t size);
void print_bin(const uint32_t v);
int allocate_buffer_vec(uint8_t*** mat, int num_buffers, int buffer_size);
void release_buffer_vec(uint8_t** mat, int num_buffers, int buffer_size);
int search_offset_in_buffer(uint8_t *buff, int len);

#ifdef __cplusplus
}
#endif

#endif // __UTILS_H__