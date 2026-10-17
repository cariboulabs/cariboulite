#define _POSIX_C_SOURCE 200809L

#include "modem_snapshot.h"
#include "radio_profile.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

const char *const modem_snapshot_common_names[MODEM_SNAPSHOT_COMMON_COUNT] = {
    "RST", "CFG", "CLKO", "BMDVC", "XOC", "IQIFC0", "IQIFC1", "IQIFC2",
    "PN", "VN",
};

const char *const modem_snapshot_radio_names[MODEM_SNAPSHOT_RADIO_COUNT] = {
    "IRQS", "IRQM", "AUXS", "STATE", "CMD", "CS", "CCF0L", "CCF0H",
    "CNL", "CNM", "RXBWC", "RXDFE", "AGCC", "AGCS", "RSSI", "EDC",
    "EDD", "EDV", "RNDV", "TXCUTC", "TXDFE", "PAC", "PADFE", "PLL",
    "PLLCF", "TXCI", "TXCQ", "TXDACI", "TXDACQ",
};

typedef struct {
    uint16_t address;
    uint8_t count;
    uint8_t first_index;
    uint8_t common;
} register_block;

/* Atmel-42415E, section 8: these ranges contain defined registers only.
 * Never coalesce through IRQS (0x0000/1), IQIFC2 (0x000C), or reserved gaps.
 */
static const register_block blocks[MODEM_SNAPSHOT_BLOCK_COUNT] = {
    {0x0005, 7, 0, 1},  /* RST through IQIFC1, excluding IQIFC2. */
    {0x000D, 2, 8, 1},  /* PN, VN. */
    {0x0000, 21, 1, 0}, /* Active-radio base: IRQM through PAC. */
    {0x0016, 1, 22, 0}, /* PADFE. */
    {0x0021, 2, 23, 0}, /* PLL, PLLCF. */
    {0x0025, 4, 25, 0}, /* TXCI through TXDACQ. */
};

static int valid_channel(at86rf215_rf_channel_en channel)
{
    return channel == at86rf215_rf_channel_900mhz ||
           channel == at86rf215_rf_channel_2400mhz;
}

static void clear_registers(modem_snapshot *snapshot)
{
    for (unsigned i = 0; i < MODEM_SNAPSHOT_COMMON_COUNT; ++i)
        snapshot->common[i] = -1;
    for (unsigned i = 0; i < MODEM_SNAPSHOT_RADIO_COUNT; ++i)
        snapshot->radio[i] = -1;
}

static int monotonic_seconds(double *seconds)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return -1;
    *seconds = (double)value.tv_sec + (double)value.tv_nsec / 1e9;
    return 0;
}

void modem_snapshot_init(modem_snapshot *snapshot,
                         at86rf215_rf_channel_en channel)
{
    if (snapshot == NULL)
        return;
    *snapshot = (modem_snapshot){.channel = channel};
    clear_registers(snapshot);
}

int modem_snapshot_refresh(modem_snapshot *snapshot, at86rf215_st *modem,
                           at86rf215_rf_channel_en channel,
                           pthread_mutex_t *hardware_mutex)
{
    if (snapshot == NULL || modem == NULL || !valid_channel(channel)) {
        errno = EINVAL;
        return -1;
    }
    double started;
    if (monotonic_seconds(&started) != 0)
        return -1;

    clear_registers(snapshot);
    snapshot->channel = channel;
    snapshot->read_blocks = 0;
    snapshot->failed_blocks = 0;
    snapshot->read_duration_s = 0;
    ++snapshot->polls;

    const uint16_t base = channel == at86rf215_rf_channel_900mhz ? 0x0100 : 0x0200;
    for (unsigned i = 0; i < MODEM_SNAPSHOT_BLOCK_COUNT; ++i) {
        const register_block *block = &blocks[i];
        uint8_t values[21];
        uint16_t address = block->common ? block->address : base + block->address;
        ++snapshot->read_blocks;

        if (hardware_mutex != NULL && pthread_mutex_lock(hardware_mutex) != 0) {
            ++snapshot->failed_blocks;
            continue;
        }
        ++snapshot->spi_transactions;
        int result = at86rf215_read_buffer(modem, address, values, block->count);
        if (hardware_mutex != NULL)
            pthread_mutex_unlock(hardware_mutex);

        if (result != 0) {
            ++snapshot->failed_blocks;
            ++snapshot->spi_failures;
            continue;
        }
        int16_t *registers = block->common ? snapshot->common : snapshot->radio;
        for (unsigned j = 0; j < block->count; ++j)
            registers[block->first_index + j] = values[j];
    }

    double completed;
    if (monotonic_seconds(&completed) != 0)
        return -1;
    snapshot->monotonic_s = completed;
    snapshot->read_duration_s = completed - started;
    return snapshot->failed_blocks == 0 ? 0 : 1;
}

int modem_snapshot_tx_analog_cutoff_hz(const modem_snapshot *snapshot)
{
    if (snapshot == NULL || !valid_channel(snapshot->channel)) return 0;
    return (int)radio_profile_tx_bandwidth_hz(snapshot->radio[19]);
}

typedef struct {
    char *buffer;
    size_t capacity;
    size_t used;
    int failed;
} json_writer;

static void append(json_writer *writer, const char *format, ...)
{
    if (writer->failed)
        return;
    va_list arguments;
    va_start(arguments, format);
    int count = vsnprintf(writer->buffer + writer->used,
                          writer->capacity - writer->used, format, arguments);
    va_end(arguments);
    if (count < 0 || (size_t)count >= writer->capacity - writer->used) {
        writer->failed = 1;
        return;
    }
    writer->used += (size_t)count;
}

static void register_map(json_writer *writer, const char *const *names,
                         const int16_t *values, unsigned count)
{
    append(writer, "{\"registers\":{");
    for (unsigned i = 0; i < count; ++i) {
        append(writer, "%s\"%s\":", i == 0 ? "" : ",", names[i]);
        if (values[i] >= 0 && values[i] <= 255)
            append(writer, "%d", (int)values[i]);
        else
            append(writer, "null");
    }
    append(writer, "}}");
}

int modem_snapshot_hardware_json(const modem_snapshot *snapshot,
                                 char *buffer, size_t capacity)
{
    if (snapshot == NULL || buffer == NULL || capacity == 0 ||
        !valid_channel(snapshot->channel)) {
        errno = EINVAL;
        return -1;
    }
    json_writer writer = {buffer, capacity, 0, 0};
    const char *active = snapshot->channel == at86rf215_rf_channel_900mhz ?
                         "rf09" : "rf24";
    const char *inactive = snapshot->channel == at86rf215_rf_channel_900mhz ?
                           "rf24" : "rf09";
    append(&writer, "{\"common\":");
    register_map(&writer, modem_snapshot_common_names, snapshot->common,
                 MODEM_SNAPSHOT_COMMON_COUNT);
    append(&writer, ",\"%s\":", active);
    register_map(&writer, modem_snapshot_radio_names, snapshot->radio,
                 MODEM_SNAPSHOT_RADIO_COUNT);
    append(&writer, ",\"%s\":{\"valid\":false},\"register_poll\":{"
                    "\"read_blocks\":%u,\"failed_blocks\":%u,"
                    "\"polls\":%" PRIu64 ",\"spi_transactions\":%" PRIu64 ","
                    "\"spi_failures\":%" PRIu64 ",\"read_duration_s\":%.9f}}",
           inactive, snapshot->read_blocks, snapshot->failed_blocks,
           snapshot->polls, snapshot->spi_transactions, snapshot->spi_failures,
           snapshot->read_duration_s);
    if (writer.failed) {
        buffer[0] = '\0';
        errno = ENOSPC;
        return -1;
    }
    return (int)writer.used;
}
