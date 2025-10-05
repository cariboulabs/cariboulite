// src/cariboulite_sys_accessor.c
#include "cariboulite_setup.h"

// cariboulite_sys is defined inside the library build.
// This accessor returns its address without exposing the data symbol.
extern sys_st cariboulite_sys;

sys_st *cariboulite_get_sys(void) {
    return &cariboulite_sys;
}