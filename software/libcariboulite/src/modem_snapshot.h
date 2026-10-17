#ifndef CARIBOULITE_MODEM_SNAPSHOT_H
#define CARIBOULITE_MODEM_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "at86rf215/at86rf215.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MODEM_SNAPSHOT_COMMON_COUNT = 10,
    MODEM_SNAPSHOT_RADIO_COUNT = 29,
    MODEM_SNAPSHOT_BLOCK_COUNT = 6,
};

/* A negative register value is unavailable, never a fabricated zero byte.
 * The cache is owned by the application control/status thread, not RX workers.
 */
typedef struct {
    int16_t common[MODEM_SNAPSHOT_COMMON_COUNT];
    int16_t radio[MODEM_SNAPSHOT_RADIO_COUNT];
    at86rf215_rf_channel_en channel;
    double monotonic_s;
    double read_duration_s;
    unsigned read_blocks;
    unsigned failed_blocks;
    uint64_t polls;
    uint64_t spi_transactions;
    uint64_t spi_failures;
} modem_snapshot;

extern const char *const modem_snapshot_common_names[MODEM_SNAPSHOT_COMMON_COUNT];
extern const char *const modem_snapshot_radio_names[MODEM_SNAPSHOT_RADIO_COUNT];

void modem_snapshot_init(modem_snapshot *snapshot,
                         at86rf215_rf_channel_en channel);

/* Read the 37 non-destructive defined shared/active-radio registers using
 * six bounded SPI blocks. IRQS and IQIFC2 stay unavailable: these registers
 * clear events/latches when read, and the library owns interrupt servicing.
 *
 * The caller must serialize access to this cache. hardware_mutex is the
 * application's mutex shared with radio controls, and is held for one block at
 * a time; the driver additionally serializes SPI with its own device lock.
 * NULL is allowed only when the caller has no concurrent radio operations.
 * The values of a failed block are unavailable for this sample.
 *
 * Return 0 for a complete safe sample, 1 for a partially/fully failed sample,
 * or -1 for invalid arguments. monotonic_s dates completion of the sample,
 * which is sequential hardware sampling, not one atomic register snapshot.
 */
int modem_snapshot_refresh(modem_snapshot *snapshot, at86rf215_st *modem,
                           at86rf215_rf_channel_en channel,
                           pthread_mutex_t *hardware_mutex);

/* Decode the active radio's cached TXCUTC analog filter cutoff in Hz.
 * Returns 0 for an unavailable byte, reserved cutoff, or invalid cache.
 * This performs no hardware access and does not estimate occupied bandwidth.
 */
int modem_snapshot_tx_analog_cutoff_hz(const modem_snapshot *snapshot);

/* Write the JSON hardware object (without the enclosing "hardware" key).
 * Returns the number of bytes written excluding NUL, or -1 on failure.
 * This only formats the cache and performs no hardware access.
 */
int modem_snapshot_hardware_json(const modem_snapshot *snapshot,
                                 char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
