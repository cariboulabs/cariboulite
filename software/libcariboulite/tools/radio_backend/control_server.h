#ifndef CARIBOULITE_CONTROL_SERVER_H
#define CARIBOULITE_CONTROL_SERVER_H

#include "command_protocol.h"
#include "status_publisher.h"

/* All operations, including the handler, belong to the radio owner thread.
 * This object has no radio access and never calls status_publisher_publish. */
typedef struct control_server {
    status_publisher_t listener_anchor;
    int controller;
    char pending_reply[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    size_t pending_reply_bytes;
    /* Linux may reset a connection closed with its startup GET unread. Keep
     * one BUSY handshake alive briefly while the competitor reads the reply. */
    int rejected_controller;
    char rejected_reply[CONTROL_PROTOCOL_MAX_PACKET_BYTES + 1];
    size_t rejected_reply_bytes;
    double rejected_deadline_s;
    uint64_t commands_processed;
    uint64_t invalid_requests;
    uint64_t controllers_rejected;
    uint64_t replies_sent;
    uint64_t replies_deferred;
    uint64_t disconnects;
} control_server_t;

typedef void (*control_command_handler_t)(void *context,
                                          const control_request_t *request,
                                          control_reply_t *reply);

/* NULL selects /tmp/cariboulite-monitor-<effective UID>/control.sock.
 * Same private directory, same-UID peer, existing-path refusal and inode
 * cleanup guarantees as the passive status transport. */
int control_server_init(control_server_t *server, const char *socket_path);

/* Bounded nonblocking I/O: at most one accept and one handler invocation per
 * iteration. Only one controller owns the connection; competitors receive
 * ERROR BUSY (ID 0) with the effective configuration and write-side EOF. One
 * rejected socket remains for at most 0.5 seconds to drain startup packets;
 * additional pending connections remain in the bounded listener backlog.
 * Invalid packets become CONTROL_INVALID requests with no mutation permitted.
 * A single deferred reply prevents further command execution until delivered.
 * The handler must populate a valid reply, even for INVALID/BUSY requests.
 * Returns 0 on normal/no work, -1 with errno for invalid/inactive transport or
 * an invalid handler reply. Closing a client never generates a radio command. */
int control_server_service(control_server_t *server,
                           control_command_handler_t handler, void *context);

void control_server_close(control_server_t *server);

#endif
