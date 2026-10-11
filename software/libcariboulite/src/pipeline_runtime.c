#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pipeline_runtime.h"
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

pthread_mutex_t g_hw_lock = PTHREAD_MUTEX_INITIALIZER;
bool nbfm_tx_ready = false, nbfm_rx_ready = false;
volatile bool nbfm_tx_active = false, nbfm_rx_active = false;

uint64_t mono_ns(void){
    struct timespec ts; 
	clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + ts.tv_nsec;
}

void pipeline_thread_diagnostics_init(pipeline_thread_diagnostics_t* diagnostics)
{
    if (!diagnostics) return;
    memset(&diagnostics->status, 0, sizeof(diagnostics->status));
    atomic_init(&diagnostics->ready, false);
}

void pipeline_thread_diagnostics_reset(pipeline_thread_diagnostics_t* diagnostics)
{
    if (diagnostics) atomic_store_explicit(&diagnostics->ready, false, memory_order_release);
}

bool pipeline_thread_diagnostics_get(const pipeline_thread_diagnostics_t* diagnostics,
                                     pipeline_thread_status_t* status)
{
    if (!diagnostics || !status ||
        !atomic_load_explicit(&diagnostics->ready, memory_order_acquire)) return false;
    *status = diagnostics->status;
    return true;
}

const char* pipeline_scheduler_policy_name(int policy)
{
    switch (policy) {
        case SCHED_OTHER: return "SCHED_OTHER";
        case SCHED_FIFO: return "SCHED_FIFO";
        case SCHED_RR: return "SCHED_RR";
#ifdef SCHED_BATCH
        case SCHED_BATCH: return "SCHED_BATCH";
#endif
#ifdef SCHED_IDLE
        case SCHED_IDLE: return "SCHED_IDLE";
#endif
        default: return "UNKNOWN";
    }
}

static void query_scheduling(pipeline_thread_status_t* status)
{
    struct sched_param actual;
    int policy;
    int error = pthread_getschedparam(pthread_self(), &policy, &actual);
    if (error == 0) {
        status->policy = policy;
        status->priority = actual.sched_priority;
    } else {
        status->query_error = error;
    }

    cpu_set_t actual_affinity;
    error = pthread_getaffinity_np(pthread_self(), sizeof(actual_affinity), &actual_affinity);
    if (error != 0) {
        if (status->query_error == 0) status->query_error = error;
        return;
    }
    size_t used = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &actual_affinity)) continue;
        int length = snprintf(status->affinity + used, sizeof(status->affinity) - used,
                              "%s%d", used ? "," : "", cpu);
        if (length < 0 || (size_t)length >= sizeof(status->affinity) - used) {
            status->affinity[used] = '\0';
            status->affinity_truncated = true;
            break;
        }
        used += (size_t)length;
    }
}

int set_rt_and_affinity_prio_record(int prio, int cpu_req,
                                   pipeline_thread_diagnostics_t* diagnostics)
{
    pipeline_thread_status_t status = {
        .valid = true,
        .requested_priority = prio,
        .requested_cpu = cpu_req,
        .applied_cpu = -1,
        .policy = -1,
        .priority = -1,
    };
    struct sched_param sp = { .sched_priority = prio };
    status.rt_error = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (status.rt_error != 0) {
        sp.sched_priority = 0;
        status.fallback_error = pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
    }

    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    int cpu = (cpu_req >= 0) ? cpu_req : 0;
    if (cpu >= ncpu) cpu = (int)(ncpu - 1);

    cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
    status.affinity_error = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (status.affinity_error == 0) {
        status.applied_cpu = cpu;
        status.mlock_attempted = true;
        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) status.mlock_error = errno;
    }
    query_scheduling(&status);
    fprintf(stderr, "[scheduling] th=%lu actual=%s/%d affinity=%s%s "
                    "requested=FIFO/%d cpu=%d selected=%d "
                    "rt_error=%d fallback_error=%d affinity_error=%d "
                    "mlock=%s mlock_error=%d query_error=%d\n",
            (unsigned long)pthread_self(), pipeline_scheduler_policy_name(status.policy),
            status.priority, status.affinity[0] ? status.affinity : "unavailable",
            status.affinity_truncated ? ",..." : "", prio, cpu_req, cpu,
            status.rt_error, status.fallback_error, status.affinity_error,
            status.mlock_attempted ? "attempted" : "skipped", status.mlock_error,
            status.query_error);
    if (diagnostics) {
        diagnostics->status = status;
        atomic_store_explicit(&diagnostics->ready, true, memory_order_release);
    }
    return status.affinity_error == 0 ? 0 : -1;
}

int set_rt_and_affinity_prio(int prio, int cpu_req)
{
    return set_rt_and_affinity_prio_record(prio, cpu_req, NULL);
}
