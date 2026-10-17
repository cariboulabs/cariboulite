#include "carrier_squelch.h"
#include <math.h>
void carrier_squelch_reset(carrier_squelch_t* s) { *s = (carrier_squelch_t){0}; }
bool carrier_squelch_process(carrier_squelch_t* s, float rssi, bool valid)
{
    if (!valid || !isfinite(rssi) || rssi < -127 || rssi > 4) {
        carrier_squelch_reset(s);
        return false;
    }
    bool transition = s->open ? rssi < CARRIER_SQUELCH_CLOSE_DBM : rssi >= CARRIER_SQUELCH_OPEN_DBM;
    if (!transition) s->qualify = 0;
    else if (++s->qualify >= (s->open ? 15u : 3u)) {
        s->open = !s->open;
        s->qualify = 0;
    }
    return s->open;
}
