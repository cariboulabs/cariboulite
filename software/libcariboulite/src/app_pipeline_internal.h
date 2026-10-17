#pragma once
// Compatibility include for existing app/worker users and frozen test fixtures.
#include "pipeline_transport.h"
int set_rt_and_affinity_prio(int prio, int cpu_req);
