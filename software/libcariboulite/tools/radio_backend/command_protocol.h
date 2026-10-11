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
    CONTROL_TX_START,
    CONTROL_TX_STOP,
    CONTROL_TX_FREQUENCY,
    CONTROL_TX_POWER,
    CONTROL_TX_SAMPLE_RATE,
    CONTROL_TX_SOURCE,
    CONTROL_TX_TONE,
    CONTROL_FPGA_PROGRAM,
    CONTROL_FPGA_SOFT_RESET,
    CONTROL_FPGA_HARD_RESET,
    /* These are transport-generated requests, never valid wire commands. */
    CONTROL_INVALID,
    CONTROL_BUSY
} control_command_t;

typedef struct control_request {
    /* Zero selects v1 when formatting existing callers' requests. */
    unsigned protocol_version;
    uint64_t id;
    control_command_t command;
    double frequency_hz;
    unsigned sample_rate_hz;
    bool wbfm;
    bool noise_enabled;
    bool carrier_enabled;
    float noise_open_rms;
    float noise_close_rms;
    double tx_frequency_hz;
    int tx_power_dbm;
    unsigned tx_sample_rate_hz;
    bool tx_tone_mode;
    float tx_tone_hz;
    float tx_tone_amp;
} control_request_t;

typedef struct control_reply {
    unsigned protocol_version;
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
    uint64_t tx_pipeline_generation;
    double tx_requested_frequency_hz;
    double tx_actual_frequency_hz;
    unsigned tx_sample_rate_hz;
    int tx_requested_power_dbm;
    int tx_actual_power_dbm;
    bool tx_tone_mode;
    float tx_tone_hz;
    float tx_tone_amp;
    float tx_deviation_hz;
    bool tx_requested;
    bool rx_resume;
    bool transition;
} control_reply_t;

/* Strict ASCII protocol-v1/v2 packets. v1 carries RX commands/settings; v2
 * extends them with TX controls, FPGA programming and transition states. Parsers accept ASCII whitespace between
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
