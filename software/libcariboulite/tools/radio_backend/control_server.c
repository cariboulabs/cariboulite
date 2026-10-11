#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "control_server.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static double monotonic_seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static void close_rejected_controller(control_server_t *server)
{
    if (server->rejected_controller >= 0) {
        close(server->rejected_controller);
        server->rejected_controller = -1;
    }
    server->rejected_reply_bytes = 0;
    server->rejected_deadline_s = 0;
}

static void disconnect_controller(control_server_t *server)
{
    if (server->controller >= 0) {
        close(server->controller);
        server->controller = -1;
        ++server->disconnects;
    }
    server->pending_reply_bytes = 0;
}

void control_server_close(control_server_t *server)
{
    if (!server)
        return;
    disconnect_controller(server);
    close_rejected_controller(server);
    status_publisher_close(&server->listener_anchor);
}

int control_server_init(control_server_t *server, const char *socket_path)
{
    char default_path[STATUS_PUBLISHER_SOCKET_PATH_BYTES];
    if (!server) {
        errno = EINVAL;
        return -1;
    }
    memset(server, 0, sizeof(*server));
    server->controller = -1;
    server->rejected_controller = -1;
    if (!socket_path) {
        snprintf(default_path, sizeof(default_path),
                 "/tmp/cariboulite-monitor-%lu/control.sock",
                 (unsigned long)geteuid());
        socket_path = default_path;
    }
    return status_publisher_init(&server->listener_anchor, socket_path);
}

static void service_rejected_controller(control_server_t *server)
{
    char incoming[CONTROL_PROTOCOL_MAX_PACKET_BYTES];
    struct iovec iov = { .iov_base = incoming, .iov_len = sizeof(incoming) };
    struct msghdr message;
    ssize_t result;
    int client = server->rejected_controller;
    if (client < 0)
        return;
    if (server->rejected_reply_bytes) {
        result = send(client, server->rejected_reply,
                      server->rejected_reply_bytes, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (result == (ssize_t)server->rejected_reply_bytes) {
            server->rejected_reply_bytes = 0;
            ++server->replies_sent;
            /* EOF follows BUSY while the descriptor remains alive: a C
             * controller that immediately sends GET can still read BUSY.
             * Immediate close with GET unread can cause Linux ECONNRESET. */
            if (shutdown(client, SHUT_WR) < 0) {
                close_rejected_controller(server);
                return;
            }
        } else if (result < 0 &&
                   (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            ++server->replies_deferred;
        } else {
            close_rejected_controller(server);
            return;
        }
    }
    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    /* Drain at most one packet in this call; never parse or execute it. */
    result = recvmsg(client, &message, MSG_DONTWAIT | MSG_TRUNC);
    if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        close_rejected_controller(server);
        return;
    }
    if (!result) {
        struct pollfd peer = { .fd = client, .events = POLLRDHUP };
        if (poll(&peer, 1, 0) < 0 ||
            (peer.revents & (POLLHUP | POLLRDHUP | POLLERR | POLLNVAL))) {
            close_rejected_controller(server);
            return;
        }
    }
    if (monotonic_seconds() >= server->rejected_deadline_s)
        close_rejected_controller(server);
}

/* EAGAIN keeps exactly one reply pending; it never repeats the mutation. */
static void send_pending_reply(control_server_t *server)
{
    ssize_t result;
    if (!server->pending_reply_bytes)
        return;
    result = send(server->controller, server->pending_reply,
                  server->pending_reply_bytes, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (result == (ssize_t)server->pending_reply_bytes) {
        server->pending_reply_bytes = 0;
        ++server->replies_sent;
    } else if (result < 0 &&
               (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        ++server->replies_deferred;
    } else {
        disconnect_controller(server);
    }
}

static int make_reply(control_server_t *server,
                      control_command_handler_t handler, void *context,
                      const control_request_t *request, char *packet)
{
    control_reply_t reply;
    int bytes;
    memset(&reply, 0, sizeof(reply));
    handler(context, request, &reply);
    /* Matching IDs and transport errors are enforced here, independent of
     * handler policy. The owner still supplies its current configuration. */
    reply.id = request->id;
    if (request->command == CONTROL_INVALID || request->command == CONTROL_BUSY) {
        reply.ok = false;
        strcpy(reply.code, request->command == CONTROL_BUSY ? "BUSY" :
                                                             "PROTOCOL");
    }
    bytes = control_reply_format(&reply, packet,
                                  CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1);
    if (bytes < 0)
        return -1;
    (void)server;
    return bytes;
}

static int receive_command(control_server_t *server,
                           control_command_handler_t handler, void *context)
{
    char packet[CONTROL_PROTOCOL_MAX_PACKET_BYTES];
    struct iovec iov = { .iov_base = packet, .iov_len = sizeof(packet) };
    struct msghdr message;
    control_request_t request;
    ssize_t bytes;
    int reply_bytes;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    bytes = recvmsg(server->controller, &message, MSG_DONTWAIT | MSG_TRUNC);
    if (bytes < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            disconnect_controller(server);
        return 0;
    }
    if (!bytes) {
        struct pollfd peer = {
            .fd = server->controller, .events = POLLRDHUP
        };
        /* SOCK_SEQPACKET also permits an empty packet. Distinguish that
         * malformed command from EOF without a blocking peek/read. */
        if (poll(&peer, 1, 0) < 0 ||
            (peer.revents & (POLLHUP | POLLRDHUP | POLLERR | POLLNVAL))) {
            disconnect_controller(server);
            return 0;
        }
    }
    memset(&request, 0, sizeof(request));
    request.command = CONTROL_INVALID;
    if ((message.msg_flags & MSG_TRUNC) ||
        bytes > CONTROL_PROTOCOL_MAX_PACKET_BYTES ||
        control_request_parse(packet, (size_t)bytes, &request) < 0) {
        request.command = CONTROL_INVALID;
        ++server->invalid_requests;
    } else {
        ++server->commands_processed;
    }
    reply_bytes = make_reply(server, handler, context, &request,
                             server->pending_reply);
    if (reply_bytes < 0) {
        disconnect_controller(server);
        return -1;
    }
    server->pending_reply_bytes = (size_t)reply_bytes;
    send_pending_reply(server);
    return 1;
}

static int accept_controller(control_server_t *server,
                             control_command_handler_t handler, void *context)
{
    struct ucred credentials;
    socklen_t credentials_bytes = sizeof(credentials);
    int buffer_bytes = 4096;
    int client;
    if (server->rejected_controller >= 0)
        return 0;
    client = accept4(server->listener_anchor.listener, NULL, NULL,
                      SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client < 0)
        return 0;
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials,
                   &credentials_bytes) < 0 ||
        credentials.uid != geteuid() ||
        setsockopt(client, SOL_SOCKET, SO_SNDBUF, &buffer_bytes,
                   sizeof(buffer_bytes)) < 0 ||
        setsockopt(client, SOL_SOCKET, SO_RCVBUF, &buffer_bytes,
                   sizeof(buffer_bytes)) < 0) {
        ++server->controllers_rejected;
        close(client);
        return 0;
    }
    if (server->controller >= 0) {
        control_request_t busy;
        int bytes;
        memset(&busy, 0, sizeof(busy));
        busy.command = CONTROL_BUSY;
        ++server->controllers_rejected;
        bytes = make_reply(server, handler, context, &busy,
                            server->rejected_reply);
        if (bytes < 0) {
            close(client);
            return -1;
        }
        server->rejected_controller = client;
        server->rejected_reply_bytes = (size_t)bytes;
        server->rejected_deadline_s = monotonic_seconds() + 0.5;
        service_rejected_controller(server);
        return 1;
    }
    server->controller = client;
    return 0;
}

int control_server_service(control_server_t *server,
                           control_command_handler_t handler, void *context)
{
    int result;
    if (!server || !handler) {
        errno = EINVAL;
        return -1;
    }
    if (server->listener_anchor.listener < 0) {
        errno = ENOTCONN;
        return -1;
    }
    service_rejected_controller(server);
    if (server->controller >= 0 && server->pending_reply_bytes) {
        send_pending_reply(server);
        /* A stalled reader does not grow memory or execute more commands. */
        if (server->controller >= 0 && server->pending_reply_bytes) {
            result = accept_controller(server, handler, context);
            if (result < 0)
                return -1;
        }
        return 0;
    }
    if (server->controller >= 0) {
        result = receive_command(server, handler, context);
        if (result)
            return result < 0 ? -1 : 0;
    }
    /* Commands already queued by the active controller have priority over
     * accepting a competitor. Human-operated clients leave ample idle turns. */
    result = accept_controller(server, handler, context);
    if (result)
        return result < 0 ? -1 : 0;
    if (server->controller >= 0) {
        result = receive_command(server, handler, context);
        if (result < 0)
            return -1;
    }
    return 0;
}
