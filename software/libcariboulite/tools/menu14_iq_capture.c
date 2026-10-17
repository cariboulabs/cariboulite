/*
 * Optional LD_PRELOAD tap for the menu 14 RF09 or RF24 receive path.
 *
 * Build:
 *   cc -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared \
 *      -Isoftware/libcariboulite/src \
 *      software/libcariboulite/tools/menu14_iq_capture.c \
 *      -o build/menu14_iq_capture.so -ldl -pthread
 *
 * Set CARIBOULITE_RX_IQ_FILE to an unused absolute output path. The file
 * contains native decoded samples before software filtering: signed little
 * endian int16 I, then int16 Q, four bytes per complex sample. The hardware
 * values are signed 13-bit values stored in these 16-bit containers.
 * CARIBOULITE_RX_IQ_CHANNEL may require "hif" or "s1g". Otherwise the first
 * successful read selects the channel. Mixing channels fails the recording.
 *
 * A controller MUST treat IQ_CAPTURE_ERROR or a missing successful
 * IQ_CAPTURE_COMPLETE marker as a failed recording. On an I/O error this tap
 * stops writing and leaves normal RX running so the controller can stop the
 * application through its menu without running hardware cleanup in a reader
 * thread. No TX function or hardware setting is intercepted.
 */
#define _GNU_SOURCE
#include <stddef.h>
#include "cariboulite_radio.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int (*read_samples_fn)(cariboulite_radio_state_st *,
    cariboulite_sample_complex_int16 *, cariboulite_sample_meta *, size_t);

static read_samples_fn real_read_samples;
static pthread_mutex_t capture_lock = PTHREAD_MUTEX_INITIALIZER;
static int capture_fd = -1;
static bool capture_enabled;
static bool capture_failed;
static int expected_channel = -1;
static int captured_channel = -1;
static uint64_t sample_count;
static uint64_t byte_count;

_Static_assert(sizeof(cariboulite_sample_complex_int16) == 4,
    "Capture format requires four-byte IQ samples");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "This native IQ tap requires a little-endian host"
#endif

static void capture_error(const char *operation, int error_number)
{
    capture_failed = true;
    fprintf(stderr, "IQ_CAPTURE_ERROR operation=%s errno=%d detail=%s\n",
        operation, error_number, strerror(error_number));
    fflush(stderr);
}

__attribute__((constructor)) static void capture_open(void)
{
    const char *path = getenv("CARIBOULITE_RX_IQ_FILE");
    const char *channel = getenv("CARIBOULITE_RX_IQ_CHANNEL");
    const char *error;
    dlerror();
    *(void **)(&real_read_samples) = dlsym(RTLD_NEXT,
        "cariboulite_radio_read_samples");
    error = dlerror();
    if (error || !real_read_samples) {
        fprintf(stderr, "IQ_CAPTURE_ERROR symbol_resolution=%s\n",
            error ? error : "null function");
        exit(EXIT_FAILURE);
    }
    if (!path || !*path) return;
    if (channel && *channel) {
        if (strcmp(channel, "hif") == 0) expected_channel = cariboulite_channel_hif;
        else if (strcmp(channel, "s1g") == 0) expected_channel = cariboulite_channel_s1g;
        else {
            fprintf(stderr, "IQ_CAPTURE_ERROR invalid_channel=%s\n", channel);
            exit(EXIT_FAILURE);
        }
    }
    if (path[0] != '/') {
        fprintf(stderr, "IQ_CAPTURE_ERROR output_path_must_be_absolute\n");
        exit(EXIT_FAILURE);
    }
    capture_fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (capture_fd < 0) {
        capture_error("open", errno);
        exit(EXIT_FAILURE);
    }
    capture_enabled = true;
    fprintf(stderr, "IQ_CAPTURE_READY file=%s format=ci16_le bytes_per_sample=4\n",
        path);
    fflush(stderr);
}

int cariboulite_radio_read_samples(cariboulite_radio_state_st *radio,
    cariboulite_sample_complex_int16 *buffer, cariboulite_sample_meta *metadata,
    size_t length)
{
    int result = real_read_samples(radio, buffer, metadata, length);
    if (result <= 0 || !capture_enabled || !radio ||
        (radio->type != cariboulite_channel_s1g &&
         radio->type != cariboulite_channel_hif)) return result;

    /* The RX reader can be cancelled at stop. Complete any in-flight disk
     * write and release the lock before allowing cancellation to proceed. */
    int old_cancel_state;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel_state);
    pthread_mutex_lock(&capture_lock);
    if (!capture_failed) {
        int channel = (int)radio->type;
        if (expected_channel >= 0 && channel != expected_channel) {
            capture_error("unexpected_channel", EPROTO);
        } else if (captured_channel >= 0 && channel != captured_channel) {
            capture_error("mixed_channels", EPROTO);
        } else if ((size_t)result > length || !buffer) {
            capture_error("invalid_read_result", EPROTO);
        } else {
            if (captured_channel < 0) {
                captured_channel = channel;
                fprintf(stderr, "IQ_CAPTURE_CHANNEL radio=%s channel=%s\n",
                    channel == cariboulite_channel_s1g ? "s1g" : "hif",
                    channel == cariboulite_channel_s1g ? "S1G/RF09" : "HiF/RF24");
                fflush(stderr);
            }
            const unsigned char *data = (const unsigned char *)buffer;
            size_t remaining = (size_t)result * sizeof(*buffer);
            while (remaining > 0) {
                ssize_t written = write(capture_fd, data, remaining);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) {
                    capture_error("write", written < 0 ? errno : EIO);
                    break;
                }
                data += (size_t)written;
                remaining -= (size_t)written;
                byte_count += (uint64_t)written;
            }
            if (remaining == 0) sample_count += (uint64_t)result;
        }
    }
    pthread_mutex_unlock(&capture_lock);
    pthread_setcancelstate(old_cancel_state, NULL);
    return result;
}

__attribute__((destructor)) static void capture_close(void)
{
    if (!capture_enabled) return;
    pthread_mutex_lock(&capture_lock);
    int rc;
    do {
        rc = fsync(capture_fd);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) capture_error("fsync", errno);
    /* Do not retry close after EINTR: Linux has already released the fd. */
    if (close(capture_fd) < 0) capture_error("close", errno);
    capture_fd = -1;
    fprintf(stderr, "IQ_CAPTURE_COMPLETE samples=%" PRIu64 " bytes=%" PRIu64
        " failed=%d\n", sample_count, byte_count, capture_failed ? 1 : 0);
    fflush(stderr);
    pthread_mutex_unlock(&capture_lock);
}
