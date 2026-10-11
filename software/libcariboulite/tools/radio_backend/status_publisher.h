#ifndef CARIBOULITE_STATUS_PUBLISHER_H
#define CARIBOULITE_STATUS_PUBLISHER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define STATUS_PUBLISHER_MAX_CLIENTS 8
#define STATUS_PUBLISHER_MAX_PACKET_BYTES 8192
#define STATUS_PUBLISHER_SOCKET_PATH_BYTES 108

/* Single owner/thread. This transport never opens radio devices or accepts
 * commands. Each publish performs a bounded number of nonblocking syscalls.
 * Counters describe the monitor transport, not RF/sample losses. */
typedef struct status_publisher {
    int listener;
    int parent_fd;
    int clients[STATUS_PUBLISHER_MAX_CLIENTS];
    unsigned clients_count;
    uint64_t packets_sent;
    uint64_t packets_skipped;
    uint64_t clients_rejected;
    char socket_path[STATUS_PUBLISHER_SOCKET_PATH_BYTES];
    char socket_name[STATUS_PUBLISHER_SOCKET_PATH_BYTES];
    dev_t socket_device;
    ino_t socket_inode;
    int owns_socket;
} status_publisher_t;

/* NULL chooses /tmp/cariboulite-monitor-<effective UID>/status.sock.
 * The immediate parent must be a real, owned directory with mode 0700.
 * A missing parent is created; existing socket paths are always refused.
 * Returns 0 on success, -1 with errno on failure. */
int status_publisher_init(status_publisher_t *publisher, const char *socket_path);

/* Caller supplies one complete UTF-8 protocol-v1 JSON object, without a
 * trailing NUL. The serializer remains responsible for JSON/schema validity.
 * Slow clients skip snapshots, receive failures disconnect only that client.
 * Returns -1 only for an invalid argument or inactive publisher. */
int status_publisher_publish(status_publisher_t *publisher,
                             const char *json, size_t bytes);

/* Close clients/listener and remove only the socket inode created by init.
 * Safe after a failed init and safe to repeat. */
void status_publisher_close(status_publisher_t *publisher);

#endif
