#ifndef CARIBOULITE_COMMAND_PROTOCOL_H
#define CARIBOULITE_COMMAND_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONTROL_PROTOCOL_MAX_PACKET_BYTES 1024

typedef enum control_command {
    CONTROL_GET,
    CONTROL_START,
    CONTROL_STOP,
    CONTROL_FREQUENCY,
    CONTROL_MODE,
    CONTROL_SAMPLE_RATE,
    CONTROL_SQUELCH,
    /* These are transport-generated requests, never valid wire commands. */
    CONTROL_INVALID,
    CONTROL_BUSY
} control_command_t;

typedef struct control_request {
    uint64_t id;
    control_command_t command;
    double frequency_hz;
    unsigned sample_rate_hz;
    bool wbfm;
    bool noise_enabled;
    bool carrier_enabled;
    float noise_open_rms;
    float noise_close_rms;
} control_request_t;

typedef struct control_reply {
    uint64_t id;
    bool ok;
    char code[32];
    uint64_t config_generation;
    uint64_t pipeline_generation;
    bool simulated;
    char state[24];
    char channel[8];
    double requested_frequency_hz;
    double actual_frequency_hz;
    unsigned sample_rate_hz;
    bool wbfm;
    bool noise_enabled;
    bool carrier_enabled;
    float noise_open_rms;
    float noise_close_rms;
} control_reply_t;

/* Strict ASCII protocol-v1 packets. Parsers accept ASCII whitespace between
 * tokens, reject embedded NULs, nonfinite/overflowed numbers and trailing
 * tokens. They return 0 on success or -1 with errno on failure. A failed
 * request parse retains a syntactically valid request ID when available. */
int control_request_parse(const char *packet, size_t bytes,
                          control_request_t *request);
int control_reply_parse(const char *packet, size_t bytes,
                        control_reply_t *reply);

/* Formatters include a trailing newline and write a terminating NUL for the
 * caller. The returned byte count excludes that NUL; never transmit it.
 * Returns -1 for an invalid object or insufficient buffer space. */
int control_request_format(const control_request_t *request,
                           char *packet, size_t capacity);
int control_reply_format(const control_reply_t *reply,
                         char *packet, size_t capacity);

#endif
