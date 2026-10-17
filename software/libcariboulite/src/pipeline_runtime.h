#pragma once
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
// Shared by menu hardware controls and both pipelines: never duplicate this lock.
extern pthread_mutex_t g_hw_lock;
#define HW_LOCK() pthread_mutex_lock(&g_hw_lock)
#define HW_UNLOCK() pthread_mutex_unlock(&g_hw_lock)
extern bool nbfm_tx_ready, nbfm_rx_ready;
extern volatile bool nbfm_tx_active, nbfm_rx_active;
uint64_t mono_ns(void);

// Startup observations, rather than the scheduling settings we hoped to apply.
// Error fields contain positive errno values; zero means no recorded error.
// mlock_attempted distinguishes a skipped call (failed affinity) from success.
typedef struct {
    bool valid;
    int requested_priority;
    int requested_cpu;
    int applied_cpu;
    int policy;
    int priority;
    char affinity[128];
    bool affinity_truncated;
    int rt_error;
    int fallback_error;
    int affinity_error;
    bool mlock_attempted;
    int mlock_error;
    int query_error;
} pipeline_thread_status_t;

// The owning control initializes this before first use and resets it after the
// old worker has joined, before creating a replacement. A worker publishes once;
// its immutable snapshot can then be read without locks or scheduler syscalls.
typedef struct {
    atomic_bool ready;
    pipeline_thread_status_t status;
} pipeline_thread_diagnostics_t;

void pipeline_thread_diagnostics_init(pipeline_thread_diagnostics_t* diagnostics);
void pipeline_thread_diagnostics_reset(pipeline_thread_diagnostics_t* diagnostics);
bool pipeline_thread_diagnostics_get(const pipeline_thread_diagnostics_t* diagnostics,
                                     pipeline_thread_status_t* status);
const char* pipeline_scheduler_policy_name(int policy);
int set_rt_and_affinity_prio_record(int prio, int cpu_req,
                                   pipeline_thread_diagnostics_t* diagnostics);
int set_rt_and_affinity_prio(int prio, int cpu_req);
