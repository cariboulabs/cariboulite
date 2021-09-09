#ifndef __CARIBOULITE_H__
#define __CARIBOULITE_H__

#include "cariboulite_config/cariboulite_config.h"

extern cariboulite_st sys;


int cariboulite_init_driver(cariboulite_st *sys, void* signal_handler_cb);
int cariboulite_release_driver(cariboulite_st *sys);

#endif // __CARIBOULITE_H__
