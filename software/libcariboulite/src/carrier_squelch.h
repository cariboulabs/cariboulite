#pragma once
#include <stdbool.h>
// Single-owner, allocation-free RSSI detector. Call once per 10 ms RF block.
// Thresholds are modem RSSI dBm, not calibrated external connector power.
#define CARRIER_SQUELCH_OPEN_DBM (-97.0f)
#define CARRIER_SQUELCH_CLOSE_DBM (-102.0f)
typedef struct { unsigned qualify; bool open; } carrier_squelch_t;
void carrier_squelch_reset(carrier_squelch_t* s);
// Three strong blocks open; fifteen weak blocks close. Invalid/read-failed
// measurements immediately close, with no reuse of cached RSSI.
bool carrier_squelch_process(carrier_squelch_t* s, float rssi_dbm, bool valid);
