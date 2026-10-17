#include "command_protocol.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "line %d: %s (errno=%d)\n", __LINE__, \
                #expression, errno); \
        exit(1); \
    } \
} while (0)

static void test_requests(void)
{
    static const struct {
        const char *packet;
        control_command_t command;
    } good[] = {
        { "v1 1 get", CONTROL_GET },
        { "v1 18446744073709551615 start\n", CONTROL_START },
        { "\t v1\r 9\nstop\f", CONTROL_STOP },
        { "v1 12 frequency 5999999999", CONTROL_FREQUENCY },
        { "v1 12 frequency 4.30125e8", CONTROL_FREQUENCY },
        { "v1 13 mode nbfm", CONTROL_MODE },
        { "v1 13 mode wbfm", CONTROL_MODE },
        { "v1 14 sample_rate 1000000", CONTROL_SAMPLE_RATE },
        { "v1 14 sample_rate 2000000", CONTROL_SAMPLE_RATE },
        { "v1 14 sample_rate 4000000", CONTROL_SAMPLE_RATE },
        { "v1 15 squelch 1 0 .20 0.30", CONTROL_SQUELCH },
        { "v2 16 get", CONTROL_GET },
        { "v2 17 tx_start", CONTROL_TX_START },
        { "v2 18 tx_stop", CONTROL_TX_STOP },
        { "v2 19 tx_frequency 430125000", CONTROL_TX_FREQUENCY },
        { "v2 20 tx_power -12", CONTROL_TX_POWER },
        { "v2 20 tx_power 2147483647", CONTROL_TX_POWER },
        { "v2 20 tx_power -2147483648", CONTROL_TX_POWER },
        { "v2 21 tx_sample_rate 2000000", CONTROL_TX_SAMPLE_RATE },
        { "v2 22 tx_source mic", CONTROL_TX_SOURCE },
        { "v2 22 tx_source tone", CONTROL_TX_SOURCE },
        { "v2 23 tx_tone 1000 .5", CONTROL_TX_TONE },
        { "v2 24 fpga_program", CONTROL_FPGA_PROGRAM },
        { "\tv2 25\r fpga_program\n", CONTROL_FPGA_PROGRAM },
        { "v2 26 fpga_soft_reset", CONTROL_FPGA_SOFT_RESET },
        { "\t v2 27\r fpga_hard_reset\n", CONTROL_FPGA_HARD_RESET },
        { "v2 28 both_frequency 430125000", CONTROL_BOTH_FREQUENCY },
        { "v2 28 both_frequency 4.30125e8", CONTROL_BOTH_FREQUENCY },
        { "v2 29 both_mode nbfm", CONTROL_BOTH_MODE },
        { "v2 29 both_mode wbfm", CONTROL_BOTH_MODE },
        { "v2 30 both_sample_rate 1000000", CONTROL_BOTH_SAMPLE_RATE },
        { "v2 30 both_sample_rate 2000000", CONTROL_BOTH_SAMPLE_RATE },
        { "\tv2 30\r both_sample_rate\n 4000000\f", CONTROL_BOTH_SAMPLE_RATE },
        { "v2 31 frequency 430125000", CONTROL_FREQUENCY },
        { "v2 32 mode wbfm", CONTROL_MODE },
        { "v2 33 sample_rate 2000000", CONTROL_SAMPLE_RATE }
    };
    static const char *const bad[] = {
        "", "v1", "v1 1", "v3 1 get", "v1 0 get", "v1 -1 get",
        "v1 +1 get", "v1 1.0 get", "v1 18446744073709551616 get",
        "v1 999999999999999999999999999999 get", "v1 1 GET",
        "v1 1 get extra", "v1 1 start extra", "v1 1 stop extra",
        "v1 1 tx", "v1 1 frequency", "v1 1 frequency NaN",
        "v1 1 frequency inf", "v1 1 frequency -Infinity",
        "v1 1 frequency 0x10", "v1 1 frequency 430125000Hz",
        "v1 1 frequency 1e999", "v1 1 frequency 1e-999",
        "v1 1 frequency .", "v1 1 frequency 1e+", "v1 1 frequency 1 2",
        "v1 1 mode fm", "v1 1 mode WBFM", "v1 1 mode nbfm extra",
        "v1 1 sample_rate 500000", "v1 1 sample_rate -1000000",
        "v1 1 sample_rate 1000000.0", "v1 1 sample_rate 4294967296",
        "v1 1 sample_rate 1000000 extra", "v1 1 squelch 1 0 .2",
        "v1 1 squelch yes 0 .2 .3", "v1 1 squelch 1 2 .2 .3",
        "v1 1 squelch 1 0 nan .3", "v1 1 squelch 1 0 .2 1e50",
        "v1 1 squelch 1 0 .2 1e-50", "v1 1 squelch 1 0 .2 .3 extra",
        "v1 1 get\x1b", "v1 1 get\x80", "v1 1 tx_start", "v1 1 tx_stop",
        "v1 1 tx_frequency 430125000", "v2 1 tx_start extra", "v2 1 tx_stop extra",
        "v2 1 tx_frequency nan", "v2 1 tx_frequency 0x1", "v2 1 tx_frequency",
        "v2 1 tx_power 1.0", "v2 1 tx_power 2147483648", "v2 1 tx_power -2147483649",
        "v2 1 tx_sample_rate 3000000", "v2 1 tx_source file", "v2 1 tx_source tone extra",
        "v2 1 tx_tone 1000", "v2 1 tx_tone 1000 nan", "v2 1 tx_tone inf .5",
        "v2 1 tx_tone 1000 .5 extra", "v1 1 fpga_program",
        "v2 1 fpga_program firmware/top.bin", "v2 1 fpga_program extra",
        "v2 1 fpga program", "v2 1 FPGA_PROGRAM", "v2 1 fpga_program\x80",
        "v1 1 fpga_soft_reset", "v1 1 fpga_hard_reset",
        "v2 1 fpga_soft_reset extra", "v2 1 fpga_hard_reset firmware/top.bin",
        "v2 1 fpga_soft reset", "v2 1 fpga_hard reset", "v2 1 FPGA_SOFT_RESET"
    };
    char packet[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    control_request_t request, decoded;
    size_t i;
    int bytes;
    for (i = 0; i < sizeof(good) / sizeof(good[0]); ++i) {
        CHECK(control_request_parse(good[i].packet, strlen(good[i].packet),
                                    &request) == 0);
        CHECK(request.command == good[i].command);
        bytes = control_request_format(&request, packet, sizeof(packet));
        CHECK(bytes > 0 && packet[bytes - 1] == '\n' && packet[bytes] == '\0');
        CHECK(control_request_parse(packet, (size_t)bytes, &decoded) == 0);
        CHECK(decoded.id == request.id && decoded.command == request.command);
        CHECK(decoded.frequency_hz == request.frequency_hz);
        CHECK(decoded.sample_rate_hz == request.sample_rate_hz);
        CHECK(decoded.wbfm == request.wbfm);
        CHECK(decoded.noise_enabled == request.noise_enabled);
        CHECK(decoded.carrier_enabled == request.carrier_enabled);
        CHECK(decoded.noise_open_rms == request.noise_open_rms);
        CHECK(decoded.noise_close_rms == request.noise_close_rms);
        CHECK(decoded.protocol_version == request.protocol_version);
        CHECK(decoded.tx_frequency_hz == request.tx_frequency_hz);
        CHECK(decoded.tx_power_dbm == request.tx_power_dbm);
        CHECK(decoded.tx_sample_rate_hz == request.tx_sample_rate_hz);
        CHECK(decoded.tx_tone_mode == request.tx_tone_mode);
        CHECK(decoded.tx_tone_hz == request.tx_tone_hz);
        CHECK(decoded.tx_tone_amp == request.tx_tone_amp);
    }
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(control_request_parse(bad[i], strlen(bad[i]), &request) == -1);
    CHECK(control_request_parse("v1 77 unknown", 13, &request) == -1);
    CHECK(request.id == 77 && request.command == CONTROL_INVALID);
    CHECK(control_request_parse("v1 1 get\0extra", 14, &request) == -1);
    memset(packet, ' ', sizeof(packet));
    memcpy(packet, "v1 1 get", 8);
    CHECK(control_request_parse(packet, CONTROL_PROTOCOL_MAX_PACKET_BYTES,
                                &request) == 0);
    CHECK(control_request_parse(packet, sizeof(packet), &request) == -1 &&
          errno == EMSGSIZE);
    CHECK(control_request_parse(NULL, 1, &request) == -1);
    CHECK(control_request_parse("v1 1 get", 8, NULL) == -1 && errno == EINVAL);
    request.id = 1;
    request.command = CONTROL_GET;
    CHECK(control_request_format(&request, packet, 4) == -1 && errno == ENOSPC);
    request.command = CONTROL_INVALID;
    CHECK(control_request_format(&request, packet, sizeof(packet)) == -1);
    request.command = CONTROL_FREQUENCY;
    request.frequency_hz = NAN;
    CHECK(control_request_format(&request, packet, sizeof(packet)) == -1);
    request.command = CONTROL_FPGA_PROGRAM;
    request.protocol_version = 1;
    CHECK(control_request_format(&request, packet, sizeof(packet)) == -1);
    request.protocol_version = 0; /* Existing callers default to v1. */
    CHECK(control_request_format(&request, packet, sizeof(packet)) == -1);
    request.protocol_version = 2;
    CHECK(control_request_format(&request, packet, sizeof(packet)) > 0);
    CHECK(!strcmp(packet, "v2 1 fpga_program\n"));
    CHECK(control_request_parse("v2 77 fpga_program\0extra", 24, &request) == -1);
    static const struct {
        control_command_t command;
        const char *packet;
    } resets[] = {
        {CONTROL_FPGA_SOFT_RESET, "v2 1 fpga_soft_reset\n"},
        {CONTROL_FPGA_HARD_RESET, "v2 1 fpga_hard_reset\n"}
    };
    for (unsigned i = 0; i < sizeof(resets) / sizeof(resets[0]); ++i) {
        request.id = 1;
        request.command = resets[i].command;
        for (unsigned version = 0; version < 2; ++version) {
            request.protocol_version = version;
            CHECK(control_request_format(&request, packet, sizeof(packet)) == -1);
        }
        request.protocol_version = 2;
        CHECK(control_request_format(&request, packet, sizeof(packet)) > 0);
        CHECK(!strcmp(packet, resets[i].packet));
    }
}

static void test_combined_requests(void)
{
    static const struct {
        control_command_t command;
        const char *packet;
    } commands[] = {
        { CONTROL_BOTH_FREQUENCY, "v2 1 both_frequency 430125000\n" },
        { CONTROL_BOTH_MODE, "v2 1 both_mode nbfm\n" },
        { CONTROL_BOTH_SAMPLE_RATE, "v2 1 both_sample_rate 2000000\n" }
    };
    static const char *const bad[] = {
        "v1 1 both_frequency 430125000", "v1 1 both_mode nbfm",
        "v1 1 both_sample_rate 1000000", "v2 1 both_frequency",
        "v2 1 both_frequency 430125000 extra", "v2 1 both_frequency nan",
        "v2 1 both_frequency inf", "v2 1 both_frequency -Infinity",
        "v2 1 both_frequency 1e999", "v2 1 both_frequency 1e-999",
        "v2 1 both_frequency 0x10", "v2 1 both_frequency 430125000Hz",
        "v2 1 both_mode", "v2 1 both_mode nbfm extra",
        "v2 1 both_mode fm", "v2 1 both_mode WBFM",
        "v2 1 both_sample_rate", "v2 1 both_sample_rate 1000000 extra",
        "v2 1 both_sample_rate 0", "v2 1 both_sample_rate 500000",
        "v2 1 both_sample_rate 3000000", "v2 1 both_sample_rate -1000000",
        "v2 1 both_sample_rate +1000000", "v2 1 both_sample_rate 1000000.0",
        "v2 1 both_sample_rate 4294967296",
        "v2 1 both_sample_rate 18446744073709551616"
    };
    control_request_t request = {
        .id = 1,
        .frequency_hz = 430125000,
        .sample_rate_hz = 2000000
    }, decoded;
    char packet[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    int bytes;
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        request.command = commands[i].command;
        for (unsigned version = 0; version < 2; ++version) {
            request.protocol_version = version;
            CHECK(control_request_format(&request, packet, sizeof(packet)) == -1);
        }
        request.protocol_version = 2;
        bytes = control_request_format(&request, packet, sizeof(packet));
        CHECK(bytes > 0 && !strcmp(packet, commands[i].packet));
        CHECK(!control_request_parse(packet, (size_t)bytes, &decoded));
        CHECK(decoded.command == request.command && decoded.id == request.id);
        CHECK(decoded.frequency_hz == (request.command == CONTROL_BOTH_FREQUENCY ?
                                      request.frequency_hz : 0));
        CHECK(decoded.sample_rate_hz == (request.command == CONTROL_BOTH_SAMPLE_RATE ?
                                        request.sample_rate_hz : 0));
        CHECK(!decoded.wbfm);
    }
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(control_request_parse(bad[i], strlen(bad[i]), &decoded) == -1);
        CHECK(decoded.id == 1 && decoded.command == CONTROL_INVALID);
    }
    static const char embedded_nul[] = "v2 1 both_mode nbfm\0extra";
    CHECK(control_request_parse(embedded_nul, sizeof(embedded_nul) - 1, &decoded) == -1);
    request.command = CONTROL_BOTH_FREQUENCY;
    /* Like RX/TX frequency packets, the wire accepts every finite frequency;
     * hardware bounds are checked by the backend before applying settings. */
    static const double finite_frequencies[] = { 0, -1, DBL_MAX, -DBL_MAX };
    for (size_t i = 0; i < sizeof(finite_frequencies) / sizeof(finite_frequencies[0]); ++i) {
        request.frequency_hz = finite_frequencies[i];
        bytes = control_request_format(&request, packet, sizeof(packet));
        CHECK(bytes > 0 && !control_request_parse(packet, (size_t)bytes, &decoded));
        CHECK(decoded.frequency_hz == request.frequency_hz);
    }
    static const double nonfinite_frequencies[] = { NAN, INFINITY, -INFINITY };
    for (size_t i = 0; i < sizeof(nonfinite_frequencies) / sizeof(nonfinite_frequencies[0]); ++i) {
        request.frequency_hz = nonfinite_frequencies[i];
        CHECK(control_request_format(&request, packet, sizeof(packet)) == -1);
    }
    request.command = CONTROL_BOTH_SAMPLE_RATE;
    request.sample_rate_hz = 3000000;
    CHECK(control_request_format(&request, packet, sizeof(packet)) == -1);
    request.command = CONTROL_BOTH_MODE;
    request.wbfm = true;
    bytes = control_request_format(&request, packet, sizeof(packet));
    CHECK(bytes > 0 && !strcmp(packet, "v2 1 both_mode wbfm\n"));
    CHECK(!control_request_parse(packet, (size_t)bytes, &decoded) && decoded.wbfm);
}

static void test_v2_replies(void)
{
    const char *packet = "v2 24 OK ACCEPTED 4 2 simulated TX_STARTING RF24 "
        "430125000 430125000 1000000 nbfm 1 0 .2 .3 "
        "1 430225000 430225000 2000000 -12 -11 1 1000 .5 2500 1 1 1\n";
    control_reply_t reply, decoded;
    char formatted[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    CHECK(!control_reply_parse(packet, strlen(packet), &reply));
    CHECK(reply.protocol_version == 2 && reply.ok && !strcmp(reply.code, "ACCEPTED"));
    CHECK(reply.tx_pipeline_generation == 1 && reply.tx_sample_rate_hz == 2000000);
    CHECK(reply.tx_requested_power_dbm == -12 && reply.tx_actual_power_dbm == -11);
    CHECK(reply.tx_requested_frequency_hz == 430225000 && reply.tx_actual_frequency_hz == 430225000);
    CHECK(reply.tx_tone_mode && reply.tx_tone_hz == 1000 && reply.tx_tone_amp == .5f);
    CHECK(reply.tx_deviation_hz == 2500 && reply.tx_requested && reply.rx_resume && reply.transition);
    int bytes = control_reply_format(&reply, formatted, sizeof(formatted));
    CHECK(bytes > 0 && !control_reply_parse(formatted, (size_t)bytes, &decoded));
    CHECK(decoded.tx_tone_amp == reply.tx_tone_amp && decoded.protocol_version == 2);
    static const char *states[] = {"IDLE", "RX", "TX_STARTING", "TX", "TX_STOPPING", "RX_STARTING",
        "FPGA_PROGRAMMING", "FPGA_RESETTING", "FPGA_NEEDS_PROGRAM", "FAULT"};
    for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        strcpy(reply.state, states[i]);
        bytes = control_reply_format(&reply, formatted, sizeof(formatted));
        CHECK(bytes > 0 && !control_reply_parse(formatted, (size_t)bytes, &decoded));
        CHECK(!strcmp(decoded.state, states[i]));
    }
    reply.protocol_version = 1;
    static const char *v2_states[] = {"TX", "FPGA_PROGRAMMING", "FPGA_RESETTING", "FPGA_NEEDS_PROGRAM"};
    for (unsigned i = 0; i < sizeof(v2_states) / sizeof(v2_states[0]); ++i) {
        strcpy(reply.state, v2_states[i]);
        bytes = control_reply_format(&reply, formatted, sizeof(formatted));
        CHECK(bytes > 0 && !control_reply_parse(formatted, (size_t)bytes, &decoded));
        CHECK(decoded.protocol_version == 1 && !strcmp(decoded.state, "FAULT"));
        CHECK(!strcmp(decoded.code, "OK") && !decoded.tx_pipeline_generation);
    }
    reply.protocol_version = 2;
    reply.tx_tone_amp = NAN;
    CHECK(control_reply_format(&reply, formatted, sizeof(formatted)) == -1);
    reply.tx_tone_amp = .5f;
    reply.tx_sample_rate_hz = 3000000;
    CHECK(control_reply_format(&reply, formatted, sizeof(formatted)) == -1);
    reply.tx_sample_rate_hz = 1000000;
    reply.ok = false;
    CHECK(control_reply_format(&reply, formatted, sizeof(formatted)) == -1); /* ERROR ACCEPTED */
    static const char *errors[] = {"TRANSITION", "TX_RUNNING", "LOOPBACK", "TX_FAILED", "FIRMWARE_FILE", "FPGA_REQUIRED"};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        strcpy(reply.code, errors[i]);
        CHECK(control_reply_format(&reply, formatted, sizeof(formatted)) > 0);
    }
    reply.protocol_version = 3;
    CHECK(control_reply_format(&reply, formatted, sizeof(formatted)) == -1);
    CHECK(control_reply_parse(packet, strlen(packet) - 2, &reply) == -1);
}

static void test_replies(void)
{
    static const char valid[] =
        "v1 18446744073709551615 OK OK 2 3 simulated RX RF24 "
        "5999999999 5999999998.125 1000000 nbfm 1 0 .2 .3\n";
    static const char busy[] =
        "v1 0 ERROR BUSY 2 3 live IDLE RF09 "
        "430125000 430125000 2000000 wbfm 0 1 .2 .3\n";
    static const char *const bad[] = {
        "v1 1 OK OK 2 3 simulated RX RF24 430125000 430125000 1000000 nbfm 1 0 .2",
        "v1 1 OK OK 2 3 simulated RX RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3 extra",
        "v1 0 OK OK 2 3 simulated RX RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 ERROR OK 2 3 simulated RX RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK BUSY 2 3 simulated RX RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 ERROR UNKNOWN 2 3 simulated RX RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK -2 3 simulated RX RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 18446744073709551616 simulated RX RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 3 MOCK RX RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 3 simulated TX RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 3 simulated FPGA_PROGRAMMING RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 3 simulated FPGA_RESETTING RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 3 simulated FPGA_NEEDS_PROGRAM RF24 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 3 simulated RX RFXX 430125000 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 3 simulated RX RF24 NaN 430125000 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 3 simulated RX RF24 430125000 inf 1000000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 3 simulated RX RF24 430125000 430125000 500000 nbfm 1 0 .2 .3",
        "v1 1 OK OK 2 3 simulated RX RF24 430125000 430125000 1000000 fm 1 0 .2 .3",
        "v1 1 OK OK 2 3 simulated RX RF24 430125000 430125000 1000000 nbfm 2 0 .2 .3"
    };
    control_reply_t reply, decoded;
    char packet[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    size_t i;
    int bytes;
    CHECK(control_reply_parse(valid, sizeof(valid) - 1, &reply) == 0);
    CHECK(reply.id == UINT64_MAX && reply.ok && reply.simulated);
    CHECK(reply.requested_frequency_hz == 5999999999.0);
    bytes = control_reply_format(&reply, packet, sizeof(packet));
    CHECK(bytes > 0 && packet[bytes - 1] == '\n' && packet[bytes] == '\0');
    CHECK(control_reply_parse(packet, (size_t)bytes, &decoded) == 0);
    CHECK(decoded.actual_frequency_hz == reply.actual_frequency_hz);
    CHECK(decoded.noise_open_rms == reply.noise_open_rms);
    CHECK(control_reply_parse(busy, sizeof(busy) - 1, &reply) == 0);
    CHECK(!reply.id && !reply.ok && !reply.simulated && reply.wbfm);
    CHECK(control_reply_format(&reply, packet, sizeof(packet)) > 0);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(control_reply_parse(bad[i], strlen(bad[i]), &decoded) == -1);
    CHECK(control_reply_format(&reply, packet, 8) == -1 && errno == ENOSPC);
    reply.actual_frequency_hz = INFINITY;
    CHECK(control_reply_format(&reply, packet, sizeof(packet)) == -1);
    reply.actual_frequency_hz = 430125000.0;
    memset(reply.code, 'X', sizeof(reply.code));
    CHECK(control_reply_format(&reply, packet, sizeof(packet)) == -1 &&
          errno == EINVAL);
    CHECK(control_reply_parse(NULL, 1, &decoded) == -1);
    CHECK(control_reply_parse(valid, sizeof(valid) - 1, NULL) == -1 &&
          errno == EINVAL);
}

int main(void)
{
    test_requests();
    test_combined_requests();
    test_replies();
    test_v2_replies();
    puts("command protocol: strict packets, numeric limits and roundtrips passed");
    return 0;
}
