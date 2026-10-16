#pragma once

// Shared nominal NBFM deviation for application TX defaults, RX normalization
// and the fixed raw-discriminator/noise-squelch scale.
#ifndef NBFM_DEFAULT_DEVIATION_HZ
#define NBFM_DEFAULT_DEVIATION_HZ 2500.0f
#endif

// Receive audio normalization limits. These do not change channel bandwidth.
#define NBFM_RX_MIN_DEVIATION_HZ 1.0f
#define NBFM_RX_MAX_DEVIATION_HZ 24000.0f
