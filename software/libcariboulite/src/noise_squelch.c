#include "noise_squelch.h"
#include <math.h>
#include <string.h>
static bool finite_float(float x)
{
    // Keep validation effective under the production -ffast-math flags.
    uint32_t bits;
    _Static_assert(sizeof(bits) == sizeof(x), "32-bit float required");
    memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}
static bool valid_levels(float open_rms, float close_rms)
{
    return finite_float(open_rms) && finite_float(close_rms) &&
        open_rms > 0.0f && open_rms < close_rms && close_rms <= NOISE_SQUELCH_MAX_RMS;
}
bool noise_squelch_set_thresholds(noise_squelch_t* s, float open_rms, float close_rms)
{
    if (!s || !valid_levels(open_rms, close_rms)) return false;
    float open_power = open_rms * open_rms, close_power = close_rms * close_rms;
    if (s->open_power != open_power || s->close_power != close_power) {
        s->open_power = open_power;
        s->close_power = close_power;
        s->qualify = 0;
    }
    return true;
}
bool noise_squelch_pack_levels(float open_rms, float close_rms, uint32_t* levels)
{
    if (!levels || !valid_levels(open_rms, close_rms)) return false;
    unsigned open = (unsigned)lroundf(open_rms * 1000.0f);
    unsigned close = (unsigned)lroundf(close_rms * 1000.0f);
    if (!open || open >= close || close > 8000u) return false;
    *levels = (open << 16) | close;
    return true;
}
bool noise_squelch_unpack_levels(uint32_t levels, float* open_rms, float* close_rms)
{
    if (!open_rms || !close_rms) return false;
    if (!levels) {
        *open_rms = NOISE_SQUELCH_OPEN_RMS;
        *close_rms = NOISE_SQUELCH_CLOSE_RMS;
        return true;
    }
    unsigned open = levels >> 16, close = levels & 0xffffu;
    if (!open || open >= close || close > 8000u) return false;
    *open_rms = (float)open / 1000.0f;
    *close_rms = (float)close / 1000.0f;
    return true;
}
uint32_t noise_squelch_default_levels(void)
{
    uint32_t levels = 0;
    noise_squelch_pack_levels(NOISE_SQUELCH_OPEN_RMS, NOISE_SQUELCH_CLOSE_RMS, &levels);
    return levels;
}
void noise_squelch_reset(noise_squelch_t* s)
{
    memset(s, 0, sizeof(*s));
    noise_squelch_set_thresholds(s, NOISE_SQUELCH_OPEN_RMS, NOISE_SQUELCH_CLOSE_RMS);
}
static void invalid_sample(noise_squelch_t* s)
{
    float open_power = s->open_power, close_power = s->close_power;
    memset(s, 0, sizeof(*s));
    s->open_power = open_power;
    s->close_power = close_power;
}
bool noise_squelch_process(noise_squelch_t* s, float x)
{
    if (!s->close_power)
        noise_squelch_set_thresholds(s, NOISE_SQUELCH_OPEN_RMS, NOISE_SQUELCH_CLOSE_RMS);
    if (!finite_float(x)) { invalid_sample(s); return false; }
    for (unsigned k = 0; k < 2; ++k) {
        float y = 0.5690356f * x + s->z1[k];
        s->z1[k] = -1.1380712f * x + 0.9428090f * y + s->z2[k];
        s->z2[k] = 0.5690356f * x - 0.3333333f * y;
        x = y;
    }
    s->power += 0.001041124f * (x*x - s->power); // 1-exp(-1/(48000*.020))
    if (!finite_float(s->power)) { invalid_sample(s); return false; }
    bool transition = s->open ? s->power > s->close_power
                              : s->power < s->open_power;
    if (!transition) s->qualify = 0;
    else if (++s->qualify >= (s->open ? 5760u : 1440u)) {
        s->open = !s->open;
        s->qualify = 0;
    }
    return s->open;
}
