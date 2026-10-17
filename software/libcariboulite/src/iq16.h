#pragma once
#include <stdint.h>
// Signed interleaved I then Q; retains the existing modem/transport layout.
typedef struct __attribute__((__packed__)) { int16_t i; int16_t q; } iq16_t;
