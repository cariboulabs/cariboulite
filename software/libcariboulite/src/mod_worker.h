#pragma once
#include <pthread.h>
// Internal TX producer/injection synchronization. No extra thread is introduced.
extern pthread_mutex_t g_tx_injection_lock;
extern pthread_cond_t g_tx_injection_changed;
void* nbfm_mod_thread(void* arg);
