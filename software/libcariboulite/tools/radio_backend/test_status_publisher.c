#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "status_publisher.h"

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

static const char snapshot[] =
    "{\"version\":1,\"source\":\"SIMULATED publisher test\","
    "\"session\":\"test\",\"sequence\":1,\"monotonic_s\":1.0}";

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

static void receive_snapshot(int client)
{
    char buffer[STATUS_PUBLISHER_MAX_PACKET_BYTES];
    ssize_t bytes = recv(client, buffer, sizeof(buffer), 0);
    CHECK(bytes == (ssize_t)(sizeof(snapshot) - 1));
    CHECK(memcmp(buffer, snapshot, sizeof(snapshot) - 1) == 0);
}

static void test_delivery_and_passivity(const char *path)
{
    status_publisher_t publisher, second;
    struct stat info;
    int client;
    char buffer[32];
    CHECK(status_publisher_init(&publisher, path) == 0);
    CHECK(lstat(path, &info) == 0 && S_ISSOCK(info.st_mode));
    CHECK((info.st_mode & 0777) == 0600);
    CHECK((fcntl(publisher.listener, F_GETFL) & O_NONBLOCK) != 0);
    CHECK(status_publisher_init(&second, path) == -1 && errno == EEXIST);
    status_publisher_close(&second);
    CHECK(lstat(path, &info) == 0);
    client = connect_client(path);
    CHECK(status_publisher_publish(&publisher, snapshot,
                                   sizeof(snapshot) - 1) == 0);
    receive_snapshot(client);
    CHECK(publisher.clients_count == 1 && publisher.packets_sent == 1);
    CHECK(send(client, "tx", 2, MSG_NOSIGNAL) == 2);
    CHECK(status_publisher_publish(&publisher, snapshot,
                                   sizeof(snapshot) - 1) == 0);
    CHECK(recv(client, buffer, sizeof(buffer), 0) == 0);
    CHECK(publisher.clients_count == 0 && publisher.packets_sent == 1);
    close(client);
    status_publisher_close(&publisher);
    CHECK(lstat(path, &info) == -1 && errno == ENOENT);
    status_publisher_close(&publisher);
    CHECK(status_publisher_publish(&publisher, snapshot,
                                   sizeof(snapshot) - 1) == -1 &&
          errno == ENOTCONN);
}

static void test_bounded_clients_and_slow_monitor(const char *path)
{
    status_publisher_t publisher;
    int clients[STATUS_PUBLISHER_MAX_CLIENTS], rejected;
    char large[STATUS_PUBLISHER_MAX_PACKET_BYTES], buffer[32];
    unsigned i;
    CHECK(status_publisher_init(&publisher, path) == 0);
    for (i = 0; i < STATUS_PUBLISHER_MAX_CLIENTS; ++i)
        clients[i] = connect_client(path);
    CHECK(status_publisher_publish(&publisher, snapshot,
                                   sizeof(snapshot) - 1) == 0);
    CHECK(publisher.clients_count == STATUS_PUBLISHER_MAX_CLIENTS);
    for (i = 0; i < STATUS_PUBLISHER_MAX_CLIENTS; ++i)
        receive_snapshot(clients[i]);
    rejected = connect_client(path);
    CHECK(status_publisher_publish(&publisher, snapshot,
                                   sizeof(snapshot) - 1) == 0);
    CHECK(recv(rejected, buffer, sizeof(buffer), 0) == 0);
    CHECK(publisher.clients_rejected == 1);
    close(rejected);
    for (i = 0; i < STATUS_PUBLISHER_MAX_CLIENTS; ++i)
        receive_snapshot(clients[i]);
    memset(large, ' ', sizeof(large));
    memcpy(large, snapshot, sizeof(snapshot) - 1);
    /* Valid JSON plus whitespace; a monitor deliberately stops reading. The
     * loop would block if publisher sockets or sends became blocking. */
    for (i = 0; i < 1000; ++i)
        CHECK(status_publisher_publish(&publisher, large, sizeof(large)) == 0);
    CHECK(publisher.packets_skipped > 0);
    CHECK(publisher.clients_count == STATUS_PUBLISHER_MAX_CLIENTS);
    CHECK(status_publisher_publish(&publisher, large, sizeof(large) + 1) == -1
          && errno == EMSGSIZE);
    CHECK(status_publisher_publish(&publisher, snapshot, 0) == -1 &&
          errno == EINVAL);
    for (i = 0; i < STATUS_PUBLISHER_MAX_CLIENTS; ++i)
        close(clients[i]);
    CHECK(status_publisher_publish(&publisher, snapshot,
                                   sizeof(snapshot) - 1) == 0);
    CHECK(publisher.clients_count == 0);
    status_publisher_close(&publisher);
}

static void test_replacement_preserved(const char *path)
{
    status_publisher_t publisher;
    struct sockaddr_un address;
    struct stat info;
    int replacement;
    CHECK(status_publisher_init(&publisher, path) == 0);
    CHECK(unlink(path) == 0);
    replacement = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    CHECK(replacement >= 0);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, path);
    CHECK(bind(replacement, (struct sockaddr *)&address, sizeof(address)) == 0);
    status_publisher_close(&publisher);
    CHECK(lstat(path, &info) == 0 && S_ISSOCK(info.st_mode));
    close(replacement);
    CHECK(unlink(path) == 0);
}

static void test_path_validation(const char *directory, const char *path)
{
    status_publisher_t publisher;
    char link_path[STATUS_PUBLISHER_SOCKET_PATH_BYTES];
    char long_path[STATUS_PUBLISHER_SOCKET_PATH_BYTES + 1];
    int file;
    CHECK(chmod(directory, 0755) == 0);
    CHECK(status_publisher_init(&publisher, path) == -1 && errno == EACCES);
    status_publisher_close(&publisher);
    CHECK(chmod(directory, 0700) == 0);
    file = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    CHECK(file >= 0);
    close(file);
    CHECK(status_publisher_init(&publisher, path) == -1 && errno == EEXIST);
    status_publisher_close(&publisher);
    CHECK(unlink(path) == 0);
    CHECK(symlink("missing-target", path) == 0);
    CHECK(status_publisher_init(&publisher, path) == -1 && errno == EEXIST);
    status_publisher_close(&publisher);
    CHECK(unlink(path) == 0);
    CHECK(snprintf(link_path, sizeof(link_path), "%s/link", directory) > 0);
    CHECK(symlink(directory, link_path) == 0);
    {
        char through_link[STATUS_PUBLISHER_SOCKET_PATH_BYTES];
        CHECK(snprintf(through_link, sizeof(through_link), "%s/x", link_path)
              > 0);
        CHECK(status_publisher_init(&publisher, through_link) == -1);
        status_publisher_close(&publisher);
    }
    CHECK(unlink(link_path) == 0);
    memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    CHECK(status_publisher_init(&publisher, long_path) == -1 &&
          errno == ENAMETOOLONG);
    status_publisher_close(&publisher);
    CHECK(status_publisher_init(&publisher, "") == -1 && errno == EINVAL);
    status_publisher_close(&publisher);
    CHECK(status_publisher_init(NULL, path) == -1 && errno == EINVAL);
}

int main(void)
{
    char directory[] = "/tmp/cariboulite-publisher-test-XXXXXX";
    char path[STATUS_PUBLISHER_SOCKET_PATH_BYTES];
    CHECK(mkdtemp(directory) != NULL);
    CHECK(snprintf(path, sizeof(path), "%s/status.sock", directory) > 0);
    test_delivery_and_passivity(path);
    test_bounded_clients_and_slow_monitor(path);
    test_replacement_preserved(path);
    test_path_validation(directory, path);
    CHECK(rmdir(directory) == 0);
    puts("status publisher: delivery, passive clients, limits, slow clients, "
         "permissions and inode cleanup passed");
    return 0;
}
