#pragma once
#include <signal.h>

// Internal test entry point; implemented beside the app's pipeline code.
int app_baseline_test(sys_st* sys, const char* firmware, const char* capture,
                      const char* playback, unsigned seconds,
                      volatile sig_atomic_t* running);
