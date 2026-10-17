#define _GNU_SOURCE
#include "pipeline_runtime.h"
#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int rt_error = EPERM, fallback_error, affinity_error;
static int scheduler_query_error, affinity_query_error, lock_error = ENOMEM;
static int fifo_calls, fallback_calls, lock_calls;

int __real_pthread_setschedparam(pthread_t, int, const struct sched_param*);
int __wrap_pthread_setschedparam(pthread_t thread, int policy, const struct sched_param* sp)
{
    if (policy == SCHED_FIFO) {
        ++fifo_calls;
        assert(sp->sched_priority == 55);
        // Even a successful request is not evidence of the resulting policy:
        // leave the real thread unchanged so its independent query decides.
        return rt_error;
    }
    assert(policy == SCHED_OTHER && sp->sched_priority == 0);
    ++fallback_calls;
    return fallback_error ? fallback_error : __real_pthread_setschedparam(thread, policy, sp);
}

int __real_pthread_setaffinity_np(pthread_t, size_t, const cpu_set_t*);
int __wrap_pthread_setaffinity_np(pthread_t thread, size_t size, const cpu_set_t* set)
{
    return affinity_error ? affinity_error : __real_pthread_setaffinity_np(thread, size, set);
}

int __real_pthread_getschedparam(pthread_t, int*, struct sched_param*);
int __wrap_pthread_getschedparam(pthread_t thread, int* policy, struct sched_param* sp)
{
    return scheduler_query_error ? scheduler_query_error : __real_pthread_getschedparam(thread, policy, sp);
}

int __real_pthread_getaffinity_np(pthread_t, size_t, cpu_set_t*);
int __wrap_pthread_getaffinity_np(pthread_t thread, size_t size, cpu_set_t* set)
{
    return affinity_query_error ? affinity_query_error : __real_pthread_getaffinity_np(thread, size, set);
}

int __wrap_mlockall(int flags)
{
    assert(flags == (MCL_CURRENT | MCL_FUTURE));
    ++lock_calls;
    if (lock_error) { errno = lock_error; return -1; }
    return 0; // Never lock test memory or alter the process's future allocation.
}

static void* worker(void* arg)
{
    pipeline_thread_diagnostics_t* diagnostics = arg;
    int result = set_rt_and_affinity_prio_record(55, diagnostics->status.requested_cpu, diagnostics);
    assert(result == (affinity_error ? -1 : 0));
    return NULL;
}

int main(void)
{
    cpu_set_t initial;
    assert(!__real_pthread_getaffinity_np(pthread_self(), sizeof(initial), &initial));
    int cpu = -1;
    for (int i = 0; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, &initial)) { cpu = i; break; }
    }
    assert(cpu >= 0 && cpu < sysconf(_SC_NPROCESSORS_ONLN));
    char expected_affinity[32];
    snprintf(expected_affinity, sizeof(expected_affinity), "%d", cpu);

    pipeline_thread_diagnostics_t diagnostics;
    pipeline_thread_status_t status;
    pipeline_thread_diagnostics_init(&diagnostics);
    assert(!pipeline_thread_diagnostics_get(&diagnostics, &status));
    assert(!pipeline_thread_diagnostics_get(NULL, &status));
    assert(!pipeline_thread_diagnostics_get(&diagnostics, NULL));
    assert(!set_rt_and_affinity_prio_record(55, cpu, &diagnostics));
    assert(pipeline_thread_diagnostics_get(&diagnostics, &status));
    assert(status.valid && status.requested_priority == 55 && status.requested_cpu == cpu);
    assert(status.applied_cpu == cpu && status.policy == SCHED_OTHER && status.priority == 0);
    assert(!strcmp(status.affinity, expected_affinity) && !status.affinity_truncated);
    assert(status.rt_error == EPERM && !status.fallback_error && !status.affinity_error);
    assert(status.mlock_attempted && status.mlock_error == ENOMEM && !status.query_error);
    assert(fifo_calls == 1 && fallback_calls == 1 && lock_calls == 1);
    assert(!strcmp(pipeline_scheduler_policy_name(status.policy), "SCHED_OTHER"));
    assert(!strcmp(pipeline_scheduler_policy_name(SCHED_FIFO), "SCHED_FIFO"));
    assert(!strcmp(pipeline_scheduler_policy_name(-1), "UNKNOWN"));

    // The snapshot is cached: failed queries after publication cannot change it.
    scheduler_query_error = EIO;
    assert(pipeline_thread_diagnostics_get(&diagnostics, &status));
    assert(status.policy == SCHED_OTHER && !status.query_error);
    scheduler_query_error = 0;

    pipeline_thread_diagnostics_reset(&diagnostics);
    assert(!pipeline_thread_diagnostics_get(&diagnostics, &status));
    rt_error = 0;
    lock_error = 0;
    int previous_fallback_calls = fallback_calls;
    assert(!set_rt_and_affinity_prio_record(55, cpu, &diagnostics));
    assert(pipeline_thread_diagnostics_get(&diagnostics, &status));
    assert(!status.rt_error && fallback_calls == previous_fallback_calls);
    assert(status.policy == SCHED_OTHER && status.priority == 0);
    assert(status.mlock_attempted && !status.mlock_error);

    // Affinity failure retains the actual mask and records that mlock was skipped.
    pipeline_thread_diagnostics_reset(&diagnostics);
    rt_error = EPERM;
    fallback_error = EACCES;
    affinity_error = EINVAL;
    int previous_lock_calls = lock_calls;
    assert(set_rt_and_affinity_prio_record(55, cpu, &diagnostics) == -1);
    assert(pipeline_thread_diagnostics_get(&diagnostics, &status));
    assert(status.rt_error == EPERM && status.fallback_error == EACCES);
    assert(status.affinity_error == EINVAL && status.applied_cpu == -1);
    assert(!status.mlock_attempted && !status.mlock_error && lock_calls == previous_lock_calls);
    assert(!strcmp(status.affinity, expected_affinity));

    pipeline_thread_diagnostics_reset(&diagnostics);
    affinity_error = fallback_error = 0;
    scheduler_query_error = EIO;
    affinity_query_error = ESRCH;
    assert(!set_rt_and_affinity_prio_record(55, cpu, &diagnostics));
    assert(pipeline_thread_diagnostics_get(&diagnostics, &status));
    assert(status.policy == -1 && status.priority == -1 && !status.affinity[0]);
    assert(status.query_error == EIO);
    pipeline_thread_diagnostics_reset(&diagnostics);
    scheduler_query_error = 0;
    assert(!set_rt_and_affinity_prio_record(55, cpu, &diagnostics));
    assert(pipeline_thread_diagnostics_get(&diagnostics, &status));
    assert(status.policy == SCHED_OTHER && !status.affinity[0] && status.query_error == ESRCH);
    affinity_query_error = 0;

    // Reuse the same control for new workers; release/acquire publication needs
    // no registry and cannot mistake a reused pthread ID for an old snapshot.
    for (int i = 0; i < 32; ++i) {
        pipeline_thread_diagnostics_reset(&diagnostics);
        assert(!pipeline_thread_diagnostics_get(&diagnostics, &status));
        diagnostics.status.requested_cpu = cpu;
        pthread_t thread;
        assert(!pthread_create(&thread, NULL, worker, &diagnostics));
        int tries = 0;
        while (!pipeline_thread_diagnostics_get(&diagnostics, &status)) {
            assert(++tries < 1000);
            usleep(1000);
        }
        assert(status.policy == SCHED_OTHER && !strcmp(status.affinity, expected_affinity));
        assert(!pthread_join(thread, NULL));
    }
    // Original API still performs the same requests and preserves its return.
    assert(!set_rt_and_affinity_prio(55, cpu));
    assert(!__real_pthread_setaffinity_np(pthread_self(), sizeof(initial), &initial));
    puts("PASS: actual policy/affinity, RT fallback and errors, mlock outcome, cached publication, restart reset, legacy API");
    return 0;
}
