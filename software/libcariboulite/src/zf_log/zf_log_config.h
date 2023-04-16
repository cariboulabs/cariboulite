#pragma once

#ifndef _ZF_LOG_CONFIG_H_
#define _ZF_LOG_CONFIG_H_

#if !defined(_WIN32) && !defined(_WIN64)

#if defined(__linux__)
    #if !defined(__ANDROID__) && !defined(_GNU_SOURCE)
        #define _GNU_SOURCE
    #endif
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>

#include "zf_log.h"

#define ZF_LOG_EXTERN_GLOBAL_OUTPUT 1

#define DO(expr) while((expr) < 0 && errno == EINTR)

static struct target
{
    const char *ip;             /* IPv4 address */
    const int port;             /* port number  */

    struct sockaddr_in addr;    /* housekeeping */
    int fd;                     /* ...          */

} sink = {"239.192.0.1", 42099, {0}, -2};

static void socket_output_init()
{
    sink.addr.sin_family = AF_INET;
    sink.addr.sin_port   = htons(sink.port);

    if (sink.ip && inet_pton(AF_INET, sink.ip, &sink.addr.sin_addr) == 1)
        if ((sink.fd = socket(AF_INET, SOCK_DGRAM|SOCK_NONBLOCK, 0)) != -1)
        {
            int optval = 0x200000;
            setsockopt(sink.fd, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));
        }

    if (sink.fd < 0) zf_log_set_output_v(ZF_LOG_OUT_STDERR);    /* install fallback */
}

static void socket_output_callback(const zf_log_message *msg, void *arg)
{
    static pthread_once_t socket_is_initialized = PTHREAD_ONCE_INIT;
    pthread_once(&socket_is_initialized, socket_output_init);

    if (sink.fd < 0) zf_log_out_stderr_callback(msg, arg);      /* trigger fallback */
    else
        DO(sendto(sink.fd, msg->buf, (msg->p - msg->buf), 0, (struct sockaddr *)&sink.addr, sizeof(sink.addr)));
}

ZF_LOG_DEFINE_GLOBAL_OUTPUT = {ZF_LOG_PUT_STD, 0, socket_output_callback};

#endif  /* !defined(_WIN32) && !defined(_WIN64) */
#endif  /* _ZF_LOG_CONFIG_H_ */
