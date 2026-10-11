#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "control_server.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "line %d: %s (errno=%d: %s)\n", __LINE__, \
                #expression, errno, strerror(errno)); \
        exit(1); \
    } \
} while (0)

typedef struct owner {
    unsigned handler_calls;
    unsigned mutations;
    bool receiving;
} owner_t;

static void handle_command(void *context, const control_request_t *request,
                            control_reply_t *reply)
{
    owner_t *owner = context;
    ++owner->handler_calls;
    if (request->command == CONTROL_START) {
        ++owner->mutations;
        owner->receiving = true;
    } else if (request->command == CONTROL_STOP) {
        ++owner->mutations;
        owner->receiving = false;
    }
    reply->id = request->id;
    reply->ok = true;
    strcpy(reply->code, "OK");
    reply->config_generation = 3;
    reply->pipeline_generation = 4;
    reply->simulated = true;
    strcpy(reply->state, owner->receiving ? "RX" : "IDLE");
    strcpy(reply->channel, "RF24");
    reply->requested_frequency_hz = 430125000.0;
    reply->actual_frequency_hz = 430125000.0;
    reply->sample_rate_hz = 1000000;
    reply->noise_enabled = true;
    reply->noise_open_rms = 0.2f;
    reply->noise_close_rms = 0.3f;
    reply->tx_requested_frequency_hz = 430125000;
    reply->tx_actual_frequency_hz = 430125000;
    reply->tx_sample_rate_hz = 1000000;
    reply->tx_requested_power_dbm = -12;
    reply->tx_actual_power_dbm = -12;
    reply->tx_tone_hz = 1000;
    reply->tx_tone_amp = .5f;
    reply->tx_deviation_hz = 2500;
}

static int connect_client(const char *path)
{
    struct sockaddr_un address;
    int client = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    CHECK(client >= 0);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    CHECK(strlen(path) < sizeof(address.sun_path));
    strcpy(address.sun_path, path);
    CHECK(connect(client, (struct sockaddr *)&address, sizeof(address)) == 0);
    CHECK(fcntl(client, F_SETFL, O_NONBLOCK) == 0);
    return client;
}

static control_reply_t receive_reply(int client)
{
    char packet[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    ssize_t bytes = recv(client, packet, sizeof(packet), MSG_DONTWAIT);
    control_reply_t reply;
    CHECK(bytes > 0);
    CHECK(control_reply_parse(packet, (size_t)bytes, &reply) == 0);
    return reply;
}

static void send_packet(int client, const char *packet, size_t bytes)
{
    CHECK(send(client, packet, bytes, MSG_DONTWAIT | MSG_NOSIGNAL) ==
          (ssize_t)bytes);
}

static void service_once(control_server_t *server, owner_t *owner)
{
    unsigned before = owner->handler_calls;
    CHECK(control_server_service(server, handle_command, owner) == 0);
    CHECK(owner->handler_calls - before <= 1);
}

static void test_commands_exclusivity_and_disconnect(const char *path)
{
    control_server_t server, second;
    owner_t owner = { 0 };
    control_reply_t reply;
    struct stat info;
    int client, competitor, replacement;
    char packet[16];
    CHECK(control_server_init(&server, path) == 0);
    CHECK(lstat(path, &info) == 0 && S_ISSOCK(info.st_mode));
    CHECK((info.st_mode & 0777) == 0600);
    CHECK(fcntl(server.listener_anchor.listener, F_GETFL) & O_NONBLOCK);
    CHECK(control_server_init(&second, path) == -1 && errno == EEXIST);
    control_server_close(&second);
    CHECK(lstat(path, &info) == 0);
    client = connect_client(path);
    send_packet(client, "v1 1 start", 10);
    send_packet(client, "v1 2 get", 8);
    service_once(&server, &owner);
    CHECK(owner.handler_calls == 1 && owner.mutations == 1 && owner.receiving);
    reply = receive_reply(client);
    CHECK(reply.id == 1 && reply.ok && !strcmp(reply.state, "RX"));
    service_once(&server, &owner);
    CHECK(owner.handler_calls == 2 && owner.mutations == 1);
    reply = receive_reply(client);
    CHECK(reply.id == 2 && reply.ok);
    competitor = connect_client(path);
    service_once(&server, &owner);
    reply = receive_reply(competitor);
    CHECK(reply.id == 0 && !reply.ok && !strcmp(reply.code, "BUSY"));
    CHECK(!strcmp(reply.state, "RX") && owner.mutations == 1);
    CHECK(recv(competitor, packet, sizeof(packet), 0) == 0);
    close(competitor);
    close(client);
    service_once(&server, &owner);
    CHECK(server.controller == -1 && owner.receiving && owner.mutations == 1);
    replacement = connect_client(path);
    send_packet(replacement, "v1 3 stop", 9);
    service_once(&server, &owner);
    reply = receive_reply(replacement);
    CHECK(reply.id == 3 && reply.ok && !strcmp(reply.state, "IDLE"));
    CHECK(owner.mutations == 2 && !owner.receiving);
    close(replacement);
    control_server_close(&server);
    CHECK(lstat(path, &info) == -1 && errno == ENOENT);
    control_server_close(&server);
    CHECK(control_server_service(&server, handle_command, &owner) == -1 &&
          errno == ENOTCONN);
}

static void test_malformed_packets(const char *path)
{
    control_server_t server;
    owner_t owner = { 0 };
    control_reply_t reply;
    char large[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    int client;
    CHECK(control_server_init(&server, path) == 0);
    client = connect_client(path);
    send_packet(client, "v1 44 start extra", 17);
    service_once(&server, &owner);
    reply = receive_reply(client);
    CHECK(reply.id == 44 && !reply.ok && !strcmp(reply.code, "PROTOCOL"));
    CHECK(owner.mutations == 0);
    send_packet(client, "v1 45 get\0extra", 15);
    service_once(&server, &owner);
    reply = receive_reply(client);
    CHECK(!reply.ok && !strcmp(reply.code, "PROTOCOL"));
    memset(large, ' ', sizeof(large));
    memcpy(large, "v1 46 start", 11);
    send_packet(client, large, sizeof(large));
    service_once(&server, &owner);
    reply = receive_reply(client);
    CHECK(!reply.ok && !strcmp(reply.code, "PROTOCOL"));
    send_packet(client, "", 0);
    service_once(&server, &owner);
    reply = receive_reply(client);
    CHECK(!reply.ok && !strcmp(reply.code, "PROTOCOL"));
    send_packet(client, "v1 47 get", 9);
    service_once(&server, &owner);
    reply = receive_reply(client);
    CHECK(reply.ok && reply.id == 47 && owner.mutations == 0);
    CHECK(server.invalid_requests == 4);
    close(client);
    control_server_close(&server);
}

static void test_slow_controller(const char *path)
{
    control_server_t server;
    owner_t owner = { 0 };
    unsigned i, before;
    int client, competitor;
    char packet[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    control_reply_t reply;
    CHECK(control_server_init(&server, path) == 0);
    client = connect_client(path);
    /* Do not consume any replies until the bounded server send buffer fills. */
    for (i = 0; i < 1000 && !server.pending_reply_bytes; ++i) {
        send_packet(client, "v1 1 get", 8);
        service_once(&server, &owner);
    }
    CHECK(server.pending_reply_bytes > 0 && server.replies_deferred > 0);
    send_packet(client, "v1 2 start", 10);
    before = owner.handler_calls;
    for (i = 0; i < 1000; ++i)
        service_once(&server, &owner);
    CHECK(owner.handler_calls == before && owner.mutations == 0);
    competitor = connect_client(path);
    service_once(&server, &owner);
    reply = receive_reply(competitor);
    CHECK(!reply.ok && !strcmp(reply.code, "BUSY"));
    close(competitor);
    while (recv(client, packet, sizeof(packet), MSG_DONTWAIT) > 0)
        ;
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
    service_once(&server, &owner);
    reply = receive_reply(client);
    CHECK(reply.id == 1 && reply.ok && owner.mutations == 0);
    service_once(&server, &owner);
    reply = receive_reply(client);
    CHECK(reply.id == 2 && reply.ok && owner.mutations == 1);
    for (i = 0; i < 10; ++i)
        service_once(&server, &owner);
    CHECK(owner.mutations == 1);
    close(client);
    service_once(&server, &owner);
    CHECK(owner.receiving && owner.mutations == 1);
    control_server_close(&server);
}

static void test_busy_startup_handshake(const char *path)
{
    control_server_t server;
    owner_t owner = { 0 };
    control_reply_t reply;
    unsigned i, j;
    int client, competitor;
    char packet[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    CHECK(control_server_init(&server, path) == 0);
    client = connect_client(path);
    send_packet(client, "v1 1 get", 8);
    service_once(&server, &owner);
    reply = receive_reply(client);
    CHECK(reply.ok);
    /* Reproduce radio_control's GET-immediately-after-connect admission race.
     * A simple BUSY send followed by close resets Linux seqpacket connections
     * when this GET remains unread and can discard the queued BUSY packet. */
    for (i = 0; i < 50; ++i) {
        competitor = connect_client(path);
        if (i & 1U)
            send_packet(competitor, "v1 1 get", 8);
        service_once(&server, &owner);
        CHECK(server.rejected_controller >= 0);
        if (!(i & 1U))
            send_packet(competitor, "v1 1 get", 8);
        reply = receive_reply(competitor);
        CHECK(reply.id == 0 && !reply.ok && !strcmp(reply.code, "BUSY"));
        CHECK(recv(competitor, packet, sizeof(packet), MSG_DONTWAIT) == 0);
        close(competitor);
        service_once(&server, &owner);
        /* A queued startup packet may be read before the following EOF. */
        service_once(&server, &owner);
        CHECK(server.rejected_controller == -1);
        CHECK(owner.mutations == 0);
    }
    competitor = connect_client(path);
    for (j = 0; j < 10; ++j)
        send_packet(competitor, "v1 2 start", 10);
    service_once(&server, &owner);
    reply = receive_reply(competitor);
    CHECK(!reply.ok && !strcmp(reply.code, "BUSY"));
    CHECK(recv(competitor, packet, sizeof(packet), MSG_DONTWAIT) == 0);
    CHECK(owner.mutations == 0);
    /* Draining a rejected peer must not interfere with the active owner or
     * execute rejected mutation packets. Pending rejection memory is fixed. */
    send_packet(client, "v1 3 start", 10);
    service_once(&server, &owner);
    reply = receive_reply(client);
    CHECK(reply.ok && reply.id == 3 && owner.receiving && owner.mutations == 1);
    CHECK(server.rejected_controller >= 0);
    for (j = 0; j < 10; ++j)
        service_once(&server, &owner);
    CHECK(owner.mutations == 1);
    /* Expire an idle peer deterministically; no blocking timeout/sleep is
     * part of service and descriptor retention cannot grow without bound. */
    server.rejected_deadline_s = 0;
    service_once(&server, &owner);
    CHECK(server.rejected_controller == -1);
    close(competitor);
    competitor = connect_client(path);
    service_once(&server, &owner);
    CHECK(server.rejected_controller >= 0);
    control_server_close(&server);
    CHECK(server.rejected_controller == -1);
    close(competitor);
    close(client);
}

static void test_secure_path_and_cleanup(const char *directory, const char *path)
{
    control_server_t server;
    struct stat info;
    struct sockaddr_un address;
    int file, replacement;
    CHECK(chmod(directory, 0755) == 0);
    CHECK(control_server_init(&server, path) == -1 && errno == EACCES);
    control_server_close(&server);
    CHECK(chmod(directory, 0700) == 0);
    file = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    CHECK(file >= 0);
    close(file);
    CHECK(control_server_init(&server, path) == -1 && errno == EEXIST);
    control_server_close(&server);
    CHECK(lstat(path, &info) == 0 && S_ISREG(info.st_mode));
    CHECK(unlink(path) == 0);
    CHECK(symlink("missing-target", path) == 0);
    CHECK(control_server_init(&server, path) == -1 && errno == EEXIST);
    control_server_close(&server);
    CHECK(lstat(path, &info) == 0 && S_ISLNK(info.st_mode));
    CHECK(unlink(path) == 0);
    CHECK(control_server_init(&server, path) == 0);
    CHECK(unlink(path) == 0);
    replacement = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    CHECK(replacement >= 0);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, path);
    CHECK(bind(replacement, (struct sockaddr *)&address, sizeof(address)) == 0);
    control_server_close(&server);
    CHECK(lstat(path, &info) == 0 && S_ISSOCK(info.st_mode));
    close(replacement);
    CHECK(unlink(path) == 0);
    CHECK(control_server_init(&server, "") == -1 && errno == EINVAL);
    control_server_close(&server);
    CHECK(control_server_init(NULL, path) == -1 && errno == EINVAL);
}

static void test_session_generation_and_disconnect_visibility(const char *path)
{
    control_server_t server;
    owner_t owner = {0};
    CHECK(!control_server_init(&server, path));
    int first = connect_client(path);
    send_packet(first, "v2 1 get", 8);
    service_once(&server, &owner);
    control_reply_t reply = receive_reply(first);
    CHECK(reply.protocol_version == 2 && reply.tx_sample_rate_hz == 1000000);
    CHECK(server.controller_generation == 1 && server.disconnected_generation == 0);
    close(first);
    /* Queue a replacement before servicing EOF. The owner must receive a
     * complete service turn to release TX before this session can execute. */
    int replacement = connect_client(path);
    send_packet(replacement, "v2 2 get", 8);
    service_once(&server, &owner);
    CHECK(server.controller == -1 && server.controller_generation == 1);
    CHECK(server.disconnects == 1 && server.disconnected_generation == 1);
    CHECK(owner.handler_calls == 1);
    service_once(&server, &owner);
    reply = receive_reply(replacement);
    CHECK(reply.protocol_version == 2 && reply.id == 2);
    CHECK(server.controller_generation == 2 && server.disconnected_generation == 1);
    /* A lost socket with a deferred reply must expose the same event. */
    memcpy(server.pending_reply, "ignored", 7);
    server.pending_reply_bytes = 7;
    close(replacement);
    int third = connect_client(path);
    send_packet(third, "v1 3 get", 8);
    service_once(&server, &owner);
    CHECK(server.controller == -1 && server.controller_generation == 2);
    CHECK(server.disconnects == 2 && server.disconnected_generation == 2);
    service_once(&server, &owner);
    reply = receive_reply(third);
    CHECK(reply.protocol_version == 1 && server.controller_generation == 3);
    close(third);
    control_server_close(&server);
}

int main(void)
{
    char directory[] = "/tmp/cariboulite-control-test-XXXXXX";
    char path[STATUS_PUBLISHER_SOCKET_PATH_BYTES];
    /* A blocking transport regression must fail instead of hanging tests. */
    alarm(10);
    CHECK(mkdtemp(directory) != NULL);
    CHECK(snprintf(path, sizeof(path), "%s/control.sock", directory) > 0);
    test_commands_exclusivity_and_disconnect(path);
    test_malformed_packets(path);
    test_slow_controller(path);
    test_busy_startup_handshake(path);
    test_session_generation_and_disconnect_visibility(path);
    test_secure_path_and_cleanup(directory, path);
    CHECK(rmdir(directory) == 0);
    puts("control server: exclusive ownership, bounded dispatch, malformed "
         "packets, slow clients and secure cleanup passed");
    return 0;
}
