#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>

#define TIMING_PERF_SYNC  (0)

#if (TIMING_PERF_SYNC)
	#define TIMING_PERF_SYNC_VARS									\
			struct timeval tv_pre = {0};							\
			struct timeval tv_post = {0};							\
			long long total_samples = 0,last_total_samples = 0;		\
			double time_pre = 0, batch_time = 0, sample_rate = 0;	\
			double time_post = 0, process_time = 0;					\
			double temp_pre;										\
			double num_samples = 0, num_samples_avg = 0;

	#define TIMING_PERF_SYNC_TICK									\
			gettimeofday(&tv_pre, NULL);

	#define TIMING_PERF_SYNC_TOCK													\
			gettimeofday(&tv_post, NULL);											\
			num_samples = (double)(st->read_ret_value) / 4.0;						\
			num_samples_avg = num_samples_avg*0.1 + num_samples*0.9;				\
			temp_pre = tv_pre.tv_sec + ((double)(tv_pre.tv_usec)) / 1e6;			\
			time_post = tv_post.tv_sec + ((double)(tv_post.tv_usec)) / 1e6;			\
			batch_time = temp_pre - time_pre;										\
			sample_rate = sample_rate*0.1 + (num_samples / batch_time) * 0.9;		\
			process_time = process_time*0.1 + (time_post - temp_pre)*0.9;			\
			time_pre = temp_pre;													\
			total_samples += st->read_ret_value;									\
			if ((total_samples - last_total_samples) > 4000000*4)					\
			{																		\
				last_total_samples = total_samples;									\
				ZF_LOGD("sample_rate = %.2f SPS, process_time = %.2f usec"			\
						", num_samples_avg = %.1f", 								\
						sample_rate, process_time * 1e6, num_samples_avg);			\
			}
#else
	#define TIMING_PERF_SYNC_VARS
	#define TIMING_PERF_SYNC_TICK
	#define TIMING_PERF_SYNC_TOCK
#endif


void smi_utils_set_realtime_priority(int priority_deter);
void smi_utils_dump_hex(const void* data, size_t size);
void smi_utils_dump_bin(const uint8_t* data, size_t size);
void smi_utils_print_bin(const uint32_t v);
int smi_utils_allocate_buffer_vec(uint8_t*** mat, int num_buffers, int buffer_size);
void smi_utils_release_buffer_vec(uint8_t** mat, int num_buffers, int buffer_size);
int smi_utils_search_offset_in_buffer(uint8_t *buff, int len);
uint8_t smi_utils_lfsr(uint8_t n);
double smi_calculate_performance(size_t bytes, struct timeval *old_time, double old_mbps);

#ifdef __cplusplus
}
#endif

#endif // __UTILS_H__