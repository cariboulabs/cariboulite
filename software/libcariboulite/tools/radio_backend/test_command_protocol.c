#include "command_protocol.h"

#include <errno.h>
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
        { "v1 15 squelch 1 0 .20 0.30", CONTROL_SQUELCH }
    };
    static const char *const bad[] = {
        "", "v1", "v1 1", "v2 1 get", "v1 0 get", "v1 -1 get",
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
        "v1 1 get\x1b", "v1 1 get\x80"
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
    test_replies();
    puts("command protocol: strict packets, numeric limits and roundtrips passed");
    return 0;
}
