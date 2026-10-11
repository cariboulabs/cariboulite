#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "status_publisher.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static void reset_publisher(status_publisher_t *publisher)
{
    unsigned i;
    memset(publisher, 0, sizeof(*publisher));
    publisher->listener = -1;
    publisher->parent_fd = -1;
    for (i = 0; i < STATUS_PUBLISHER_MAX_CLIENTS; ++i)
        publisher->clients[i] = -1;
}

static void remove_client(status_publisher_t *publisher, unsigned slot)
{
    if (publisher->clients[slot] >= 0) {
        close(publisher->clients[slot]);
        publisher->clients[slot] = -1;
        --publisher->clients_count;
    }
}

void status_publisher_close(status_publisher_t *publisher)
{
    unsigned i;
    struct stat info;
    if (!publisher)
        return;
    for (i = 0; i < STATUS_PUBLISHER_MAX_CLIENTS; ++i)
        remove_client(publisher, i);
    if (publisher->listener >= 0)
        close(publisher->listener);
    if (publisher->parent_fd >= 0) {
        if (publisher->owns_socket &&
            fstatat(publisher->parent_fd, publisher->socket_name, &info,
                    AT_SYMLINK_NOFOLLOW) == 0 && S_ISSOCK(info.st_mode) &&
            info.st_dev == publisher->socket_device &&
            info.st_ino == publisher->socket_inode)
            unlinkat(publisher->parent_fd, publisher->socket_name, 0);
        close(publisher->parent_fd);
    }
    reset_publisher(publisher);
}

int status_publisher_init(status_publisher_t *publisher, const char *socket_path)
{
    struct sockaddr_un address;
    struct stat parent_info, info;
    char parent[STATUS_PUBLISHER_SOCKET_PATH_BYTES];
    char *separator;
    size_t path_length;
    int saved_errno;

    if (!publisher) {
        errno = EINVAL;
        return -1;
    }
    reset_publisher(publisher);
    if (!socket_path) {
        snprintf(publisher->socket_path, sizeof(publisher->socket_path),
                 "/tmp/cariboulite-monitor-%lu/status.sock",
                 (unsigned long)geteuid());
    } else {
        path_length = strlen(socket_path);
        if (!path_length || path_length >= sizeof(publisher->socket_path)) {
            errno = path_length ? ENAMETOOLONG : EINVAL;
            return -1;
        }
        memcpy(publisher->socket_path, socket_path, path_length + 1);
    }
    strcpy(parent, publisher->socket_path);
    separator = strrchr(parent, '/');
    if (separator) {
        strcpy(publisher->socket_name, separator + 1);
        if (separator == parent)
            separator[1] = '\0';
        else
            *separator = '\0';
    } else {
        strcpy(publisher->socket_name, parent);
        strcpy(parent, ".");
    }
    if (!publisher->socket_name[0] ||
        !strcmp(publisher->socket_name, ".") ||
        !strcmp(publisher->socket_name, "..")) {
        errno = EINVAL;
        return -1;
    }

    if (mkdir(parent, 0700) < 0 && errno != EEXIST)
        return -1;
    publisher->parent_fd = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                       O_CLOEXEC);
    if (publisher->parent_fd < 0)
        goto fail;
    if (fstat(publisher->parent_fd, &parent_info) < 0)
        goto fail;
    if (!S_ISDIR(parent_info.st_mode) || parent_info.st_uid != geteuid() ||
        (parent_info.st_mode & 07777) != 0700) {
        errno = EACCES;
        goto fail;
    }
    if (fstatat(publisher->parent_fd, publisher->socket_name, &info,
                AT_SYMLINK_NOFOLLOW) == 0) {
        errno = EEXIST;
        goto fail;
    }
    if (errno != ENOENT)
        goto fail;

    publisher->listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK |
                                         SOCK_CLOEXEC, 0);
    if (publisher->listener < 0)
        goto fail;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, publisher->socket_path);
    /* Ensure the pathname still resolves to the verified parent. The open
     * directory also anchors cleanup if a pathname is replaced afterward. */
    if (lstat(parent, &info) < 0)
        goto fail;
    if (!S_ISDIR(info.st_mode) || info.st_dev != parent_info.st_dev ||
        info.st_ino != parent_info.st_ino) {
        errno = ESTALE;
        goto fail;
    }
    if (bind(publisher->listener, (struct sockaddr *)&address,
             sizeof(address)) < 0)
        goto fail;
    if (fstatat(publisher->parent_fd, publisher->socket_name, &info,
                AT_SYMLINK_NOFOLLOW) < 0)
        goto fail;
    publisher->owns_socket = 1;
    publisher->socket_device = info.st_dev;
    publisher->socket_inode = info.st_ino;
    /* Parent privacy prevents traversal even during the bind/chmod interval.
     * Do not change process-wide umask in the multithreaded radio owner. */
    if (fchmodat(publisher->parent_fd, publisher->socket_name, 0600, 0) < 0 ||
        listen(publisher->listener, STATUS_PUBLISHER_MAX_CLIENTS) < 0)
        goto fail;
    return 0;

fail:
    saved_errno = errno;
    status_publisher_close(publisher);
    errno = saved_errno;
    return -1;
}

static void accept_clients(status_publisher_t *publisher)
{
    unsigned attempt, slot;
    for (attempt = 0; attempt < STATUS_PUBLISHER_MAX_CLIENTS; ++attempt) {
        struct ucred credentials;
        socklen_t credentials_size = sizeof(credentials);
        int send_buffer = 16384;
        int client = accept4(publisher->listener, NULL, NULL,
                             SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (publisher->clients_count >= STATUS_PUBLISHER_MAX_CLIENTS ||
            getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials,
                       &credentials_size) < 0 ||
            credentials.uid != geteuid() ||
            setsockopt(client, SOL_SOCKET, SO_SNDBUF, &send_buffer,
                       sizeof(send_buffer)) < 0) {
            ++publisher->clients_rejected;
            close(client);
            continue;
        }
        for (slot = 0; slot < STATUS_PUBLISHER_MAX_CLIENTS; ++slot) {
            if (publisher->clients[slot] < 0) {
                publisher->clients[slot] = client;
                ++publisher->clients_count;
                break;
            }
        }
    }
}

int status_publisher_publish(status_publisher_t *publisher,
                             const char *json, size_t bytes)
{
    unsigned slot;
    if (!publisher || !json || !bytes) {
        errno = EINVAL;
        return -1;
    }
    if (bytes > STATUS_PUBLISHER_MAX_PACKET_BYTES) {
        errno = EMSGSIZE;
        return -1;
    }
    if (publisher->listener < 0) {
        errno = ENOTCONN;
        return -1;
    }
    accept_clients(publisher);
    for (slot = 0; slot < STATUS_PUBLISHER_MAX_CLIENTS; ++slot) {
        char incoming;
        ssize_t result;
        int client = publisher->clients[slot];
        if (client < 0)
            continue;
        /* Any input (including a zero-length packet) or EOF ends the passive
         * connection. No command bytes are ever parsed or acted upon. */
        result = recv(client, &incoming, 1, MSG_DONTWAIT);
        if (result >= 0 ||
            (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            remove_client(publisher, slot);
            continue;
        }
        result = send(client, json, bytes, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (result == (ssize_t)bytes) {
            ++publisher->packets_sent;
        } else if (result < 0 &&
                   (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            ++publisher->packets_skipped;
        } else {
            remove_client(publisher, slot);
        }
    }
    return 0;
}
