#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "command_protocol.h"

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 30

static int protocol_error(void)
{
    errno = EPROTO;
    return -1;
}

static int ascii_space(unsigned char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
           ch == '\v' || ch == '\f';
}

static int tokenize(const char *packet, size_t bytes, char *copy,
                    char **tokens, size_t *count)
{
    size_t i;
    int in_token = 0;
    *count = 0;
    if (!packet || !bytes)
        return protocol_error();
    if (bytes > CONTROL_PROTOCOL_MAX_PACKET_BYTES) {
        errno = EMSGSIZE;
        return -1;
    }
    for (i = 0; i < bytes; ++i) {
        unsigned char ch = (unsigned char)packet[i];
        if (ascii_space(ch)) {
            copy[i] = '\0';
            in_token = 0;
        } else {
            if (ch < 0x21 || ch > 0x7e)
                return protocol_error();
            copy[i] = (char)ch;
            if (!in_token) {
                if (*count >= MAX_TOKENS)
                    return protocol_error();
                tokens[(*count)++] = copy + i;
                in_token = 1;
            }
        }
    }
    copy[bytes] = '\0';
    return 0;
}

static int parse_uint64(const char *token, uint64_t *value)
{
    uint64_t result = 0;
    const unsigned char *p = (const unsigned char *)token;
    if (!*p)
        return -1;
    for (; *p; ++p) {
        unsigned digit;
        if (*p < '0' || *p > '9')
            return -1;
        digit = *p - '0';
        if (result > (UINT64_MAX - digit) / 10)
            return -1;
        result = result * 10 + digit;
    }
    *value = result;
    return 0;
}

static int decimal_syntax(const char *token)
{
    const char *p = token;
    unsigned digits = 0;
    if (*p == '+' || *p == '-')
        ++p;
    while (*p >= '0' && *p <= '9') {
        ++digits;
        ++p;
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            ++digits;
            ++p;
        }
    }
    if (!digits)
        return 0;
    if (*p == 'e' || *p == 'E') {
        unsigned exponent_digits = 0;
        ++p;
        if (*p == '+' || *p == '-')
            ++p;
        while (*p >= '0' && *p <= '9') {
            ++exponent_digits;
            ++p;
        }
        if (!exponent_digits)
            return 0;
    }
    return *p == '\0';
}

static int parse_double(const char *token, double *value)
{
    locale_t numeric_locale;
    char *end;
    double result;
    int saved_errno;
    if (!decimal_syntax(token))
        return -1;
    numeric_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric_locale)
        return -1;
    errno = 0;
    result = strtod_l(token, &end, numeric_locale);
    saved_errno = errno;
    freelocale(numeric_locale);
    if (*end || saved_errno == ERANGE || !isfinite(result))
        return -1;
    *value = result;
    return 0;
}

static int parse_float(const char *token, float *value)
{
    double result;
    float converted;
    if (parse_double(token, &result) < 0 || fabs(result) > FLT_MAX)
        return -1;
    converted = (float)result;
    if (!isfinite(converted) || (result != 0.0 && converted == 0.0f))
        return -1;
    *value = converted;
    return 0;
}

static int parse_bool(const char *token, bool *value)
{
    if (!strcmp(token, "0"))
        *value = false;
    else if (!strcmp(token, "1"))
        *value = true;
    else
        return -1;
    return 0;
}

static int parse_int(const char *token, int *value)
{
    const char *p = token;
    if (*p == '-' || *p == '+') ++p;
    if (!*p) return -1;
    for (; *p; ++p) if (*p < '0' || *p > '9') return -1;
    char *end;
    errno = 0;
    long result = strtol(token, &end, 10);
    if (errno || *end || result < INT_MIN || result > INT_MAX) return -1;
    *value = (int)result;
    return 0;
}

static int parse_mode(const char *token, bool *wbfm)
{
    if (!strcmp(token, "nbfm"))
        *wbfm = false;
    else if (!strcmp(token, "wbfm"))
        *wbfm = true;
    else
        return -1;
    return 0;
}

static int parse_rate(const char *token, unsigned *rate)
{
    uint64_t value;
    if (parse_uint64(token, &value) < 0 ||
        (value != 1000000 && value != 2000000 && value != 4000000))
        return -1;
    *rate = (unsigned)value;
    return 0;
}

int control_request_parse(const char *packet, size_t bytes,
                          control_request_t *request)
{
    char copy[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    char *tokens[MAX_TOKENS];
    size_t count;
    if (!request) {
        errno = EINVAL;
        return -1;
    }
    memset(request, 0, sizeof(*request));
    request->command = CONTROL_INVALID;
    if (tokenize(packet, bytes, copy, tokens, &count) < 0)
        return -1;
    if (count < 2 || (strcmp(tokens[0], "v1") && strcmp(tokens[0], "v2")) ||
        parse_uint64(tokens[1], &request->id) < 0 || !request->id)
        return protocol_error();
    request->protocol_version = !strcmp(tokens[0], "v2") ? 2 : 1;
    if (count < 3)
        return protocol_error();
    if (!strcmp(tokens[2], "get") && count == 3)
        request->command = CONTROL_GET;
    else if (!strcmp(tokens[2], "start") && count == 3)
        request->command = CONTROL_START;
    else if (!strcmp(tokens[2], "stop") && count == 3)
        request->command = CONTROL_STOP;
    else if (!strcmp(tokens[2], "frequency") && count == 4 &&
             parse_double(tokens[3], &request->frequency_hz) == 0)
        request->command = CONTROL_FREQUENCY;
    else if (!strcmp(tokens[2], "mode") && count == 4 &&
             parse_mode(tokens[3], &request->wbfm) == 0)
        request->command = CONTROL_MODE;
    else if (!strcmp(tokens[2], "sample_rate") && count == 4 &&
             parse_rate(tokens[3], &request->sample_rate_hz) == 0)
        request->command = CONTROL_SAMPLE_RATE;
    else if (!strcmp(tokens[2], "squelch") && count == 7 &&
             parse_bool(tokens[3], &request->noise_enabled) == 0 &&
             parse_bool(tokens[4], &request->carrier_enabled) == 0 &&
             parse_float(tokens[5], &request->noise_open_rms) == 0 &&
             parse_float(tokens[6], &request->noise_close_rms) == 0)
        request->command = CONTROL_SQUELCH;
    else if (request->protocol_version == 2 && !strcmp(tokens[2], "tx_start") && count == 3)
        request->command = CONTROL_TX_START;
    else if (request->protocol_version == 2 && !strcmp(tokens[2], "tx_stop") && count == 3)
        request->command = CONTROL_TX_STOP;
    else if (request->protocol_version == 2 && !strcmp(tokens[2], "tx_frequency") && count == 4 &&
             !parse_double(tokens[3], &request->tx_frequency_hz))
        request->command = CONTROL_TX_FREQUENCY;
    else if (request->protocol_version == 2 && !strcmp(tokens[2], "tx_power") && count == 4 &&
             !parse_int(tokens[3], &request->tx_power_dbm))
        request->command = CONTROL_TX_POWER;
    else if (request->protocol_version == 2 && !strcmp(tokens[2], "tx_sample_rate") && count == 4 &&
             !parse_rate(tokens[3], &request->tx_sample_rate_hz))
        request->command = CONTROL_TX_SAMPLE_RATE;
    else if (request->protocol_version == 2 && !strcmp(tokens[2], "tx_source") && count == 4 &&
             (!strcmp(tokens[3], "mic") || !strcmp(tokens[3], "tone"))) {
        request->command = CONTROL_TX_SOURCE;
        request->tx_tone_mode = !strcmp(tokens[3], "tone");
    } else if (request->protocol_version == 2 && !strcmp(tokens[2], "tx_tone") && count == 5 &&
               !parse_float(tokens[3], &request->tx_tone_hz) &&
               !parse_float(tokens[4], &request->tx_tone_amp))
        request->command = CONTROL_TX_TONE;
    else
        return protocol_error();
    return 0;
}

static int valid_code(const char *code)
{
    static const char *const codes[] = {
        "OK", "BUSY", "PROTOCOL", "RX_RUNNING", "INVALID", "FREQ_RANGE",
        "HARDWARE", "FAULT", "WBFM_NOISE", "ACCEPTED", "TRANSITION",
        "TX_RUNNING", "LOOPBACK", "TX_FAILED"
    };
    size_t i;
    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
        if (!strcmp(code, codes[i]))
            return 1;
    return 0;
}

static int valid_state(const char *state, unsigned version)
{
    if (!strcmp(state, "IDLE") || !strcmp(state, "RX") || !strcmp(state, "FAULT")) return 1;
    return version == 2 && (!strcmp(state, "TX_STARTING") || !strcmp(state, "TX") ||
        !strcmp(state, "TX_STOPPING") || !strcmp(state, "RX_STARTING"));
}

int control_reply_parse(const char *packet, size_t bytes, control_reply_t *reply)
{
    char copy[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    char *tokens[MAX_TOKENS];
    size_t count;
    if (!reply) {
        errno = EINVAL;
        return -1;
    }
    memset(reply, 0, sizeof(*reply));
    if (tokenize(packet, bytes, copy, tokens, &count) < 0)
        return -1;
    if (!count || (strcmp(tokens[0], "v1") && strcmp(tokens[0], "v2")))
        return protocol_error();
    reply->protocol_version = !strcmp(tokens[0], "v2") ? 2 : 1;
    if (count != (reply->protocol_version == 2 ? 30U : 17U) ||
        parse_uint64(tokens[1], &reply->id) < 0)
        return protocol_error();
    if (!strcmp(tokens[2], "OK"))
        reply->ok = true;
    else if (strcmp(tokens[2], "ERROR"))
        return protocol_error();
    if (!valid_code(tokens[3]) ||
        (reply->ok && ((strcmp(tokens[3], "OK") &&
                       (reply->protocol_version != 2 || strcmp(tokens[3], "ACCEPTED"))) || !reply->id)) ||
        (!reply->ok && (!strcmp(tokens[3], "OK") || !strcmp(tokens[3], "ACCEPTED"))) ||
        parse_uint64(tokens[4], &reply->config_generation) < 0 ||
        parse_uint64(tokens[5], &reply->pipeline_generation) < 0)
        return protocol_error();
    strcpy(reply->code, tokens[3]);
    if (!strcmp(tokens[6], "simulated"))
        reply->simulated = true;
    else if (strcmp(tokens[6], "live"))
        return protocol_error();
    if (!valid_state(tokens[7], reply->protocol_version))
        return protocol_error();
    strcpy(reply->state, tokens[7]);
    if (strcmp(tokens[8], "RF24") && strcmp(tokens[8], "RF09"))
        return protocol_error();
    strcpy(reply->channel, tokens[8]);
    if (parse_double(tokens[9], &reply->requested_frequency_hz) < 0 ||
        parse_double(tokens[10], &reply->actual_frequency_hz) < 0 ||
        parse_rate(tokens[11], &reply->sample_rate_hz) < 0 ||
        parse_mode(tokens[12], &reply->wbfm) < 0 ||
        parse_bool(tokens[13], &reply->noise_enabled) < 0 ||
        parse_bool(tokens[14], &reply->carrier_enabled) < 0 ||
        parse_float(tokens[15], &reply->noise_open_rms) < 0 ||
        parse_float(tokens[16], &reply->noise_close_rms) < 0)
        return protocol_error();
    if (reply->protocol_version == 2 &&
        (parse_uint64(tokens[17], &reply->tx_pipeline_generation) ||
         parse_double(tokens[18], &reply->tx_requested_frequency_hz) ||
         parse_double(tokens[19], &reply->tx_actual_frequency_hz) ||
         parse_rate(tokens[20], &reply->tx_sample_rate_hz) ||
         parse_int(tokens[21], &reply->tx_requested_power_dbm) ||
         parse_int(tokens[22], &reply->tx_actual_power_dbm) ||
         parse_bool(tokens[23], &reply->tx_tone_mode) ||
         parse_float(tokens[24], &reply->tx_tone_hz) ||
         parse_float(tokens[25], &reply->tx_tone_amp) ||
         parse_float(tokens[26], &reply->tx_deviation_hz) ||
         parse_bool(tokens[27], &reply->tx_requested) ||
         parse_bool(tokens[28], &reply->rx_resume) ||
         parse_bool(tokens[29], &reply->transition)))
        return protocol_error();
    return 0;
}

static int formatted_size(int bytes, size_t capacity)
{
    if (bytes < 0)
        return -1;
    if ((size_t)bytes >= capacity) {
        errno = ENOSPC;
        return -1;
    }
    if (bytes > CONTROL_PROTOCOL_MAX_PACKET_BYTES) {
        errno = EMSGSIZE;
        return -1;
    }
    return bytes;
}

int control_request_format(const control_request_t *request,
                           char *packet, size_t capacity)
{
    locale_t numeric_locale, previous_locale;
    control_request_t checked;
    int bytes = -1;
    if (!request || !packet || !capacity || !request->id || request->protocol_version > 2) {
        errno = EINVAL;
        return -1;
    }
    numeric_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric_locale)
        return -1;
    previous_locale = uselocale(numeric_locale);
    unsigned version = request->protocol_version ? request->protocol_version : 1;
    switch (request->command) {
    case CONTROL_GET:
    case CONTROL_START:
    case CONTROL_STOP:
        bytes = snprintf(packet, capacity, "v%u %" PRIu64 " %s\n", version, request->id,
                         request->command == CONTROL_GET ? "get" :
                         request->command == CONTROL_START ? "start" : "stop");
        break;
    case CONTROL_FREQUENCY:
        bytes = snprintf(packet, capacity, "v%u %" PRIu64 " frequency %.17g\n",
                         version, request->id, request->frequency_hz);
        break;
    case CONTROL_MODE:
        bytes = snprintf(packet, capacity, "v%u %" PRIu64 " mode %s\n",
                         version, request->id, request->wbfm ? "wbfm" : "nbfm");
        break;
    case CONTROL_SAMPLE_RATE:
        bytes = snprintf(packet, capacity, "v%u %" PRIu64 " sample_rate %u\n",
                         version, request->id, request->sample_rate_hz);
        break;
    case CONTROL_SQUELCH:
        bytes = snprintf(packet, capacity,
                         "v%u %" PRIu64 " squelch %u %u %.9g %.9g\n",
                         version, request->id, request->noise_enabled ? 1U : 0U,
                         request->carrier_enabled ? 1U : 0U,
                         (double)request->noise_open_rms,
                         (double)request->noise_close_rms);
        break;
    case CONTROL_TX_START:
    case CONTROL_TX_STOP:
        bytes = snprintf(packet, capacity, "v%u %" PRIu64 " %s\n", version, request->id,
                         request->command == CONTROL_TX_START ? "tx_start" : "tx_stop");
        break;
    case CONTROL_TX_FREQUENCY:
        bytes = snprintf(packet, capacity, "v%u %" PRIu64 " tx_frequency %.17g\n",
                         version, request->id, request->tx_frequency_hz);
        break;
    case CONTROL_TX_POWER:
        bytes = snprintf(packet, capacity, "v%u %" PRIu64 " tx_power %d\n",
                         version, request->id, request->tx_power_dbm);
        break;
    case CONTROL_TX_SAMPLE_RATE:
        bytes = snprintf(packet, capacity, "v%u %" PRIu64 " tx_sample_rate %u\n",
                         version, request->id, request->tx_sample_rate_hz);
        break;
    case CONTROL_TX_SOURCE:
        bytes = snprintf(packet, capacity, "v%u %" PRIu64 " tx_source %s\n",
                         version, request->id, request->tx_tone_mode ? "tone" : "mic");
        break;
    case CONTROL_TX_TONE:
        bytes = snprintf(packet, capacity, "v%u %" PRIu64 " tx_tone %.9g %.9g\n",
                         version, request->id, (double)request->tx_tone_hz,
                         (double)request->tx_tone_amp);
        break;
    default:
        errno = EINVAL;
        break;
    }
    uselocale(previous_locale);
    freelocale(numeric_locale);
    bytes = formatted_size(bytes, capacity);
    if (bytes >= 0 && control_request_parse(packet, (size_t)bytes, &checked) < 0)
        return -1;
    return bytes;
}

int control_reply_format(const control_reply_t *reply,
                         char *packet, size_t capacity)
{
    locale_t numeric_locale, previous_locale;
    control_reply_t checked;
    int bytes;
    if (!reply || !packet || !capacity ||
        !memchr(reply->code, '\0', sizeof(reply->code)) ||
        !memchr(reply->state, '\0', sizeof(reply->state)) ||
        !memchr(reply->channel, '\0', sizeof(reply->channel)) || reply->protocol_version > 2) {
        errno = EINVAL;
        return -1;
    }
    numeric_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric_locale)
        return -1;
    previous_locale = uselocale(numeric_locale);
    unsigned version = reply->protocol_version ? reply->protocol_version : 1;
    const char *state = reply->state;
    if (version == 1 && !valid_state(state, 1) && valid_state(state, 2)) state = "FAULT";
    const char *code = reply->code;
    if (version == 1 && reply->ok && !strcmp(code, "ACCEPTED")) code = "OK";
    bytes = snprintf(packet, capacity,
                     "v%u %" PRIu64 " %s %s %" PRIu64 " %" PRIu64
                     " %s %s %s %.17g %.17g %u %s %u %u %.9g %.9g",
                     version, reply->id, reply->ok ? "OK" : "ERROR", code,
                     reply->config_generation, reply->pipeline_generation,
                     reply->simulated ? "simulated" : "live", state,
                     reply->channel, reply->requested_frequency_hz,
                     reply->actual_frequency_hz, reply->sample_rate_hz,
                     reply->wbfm ? "wbfm" : "nbfm",
                     reply->noise_enabled ? 1U : 0U,
                     reply->carrier_enabled ? 1U : 0U,
                     (double)reply->noise_open_rms,
                     (double)reply->noise_close_rms);
    if (bytes >= 0 && (size_t)bytes < capacity) {
        int extra;
        if (version == 2)
            extra = snprintf(packet + bytes, capacity - (size_t)bytes,
                             " %" PRIu64 " %.17g %.17g %u %d %d %u %.9g %.9g %.9g %u %u %u\n",
                             reply->tx_pipeline_generation, reply->tx_requested_frequency_hz,
                             reply->tx_actual_frequency_hz, reply->tx_sample_rate_hz,
                             reply->tx_requested_power_dbm, reply->tx_actual_power_dbm,
                             reply->tx_tone_mode ? 1U : 0U, (double)reply->tx_tone_hz,
                             (double)reply->tx_tone_amp, (double)reply->tx_deviation_hz,
                             reply->tx_requested ? 1U : 0U, reply->rx_resume ? 1U : 0U,
                             reply->transition ? 1U : 0U);
        else extra = snprintf(packet + bytes, capacity - (size_t)bytes, "\n");
        bytes = extra < 0 ? -1 : bytes + extra;
    }
    uselocale(previous_locale);
    freelocale(numeric_locale);
    bytes = formatted_size(bytes, capacity);
    if (bytes >= 0 && control_reply_parse(packet, (size_t)bytes, &checked) < 0)
        return -1;
    return bytes;
}
