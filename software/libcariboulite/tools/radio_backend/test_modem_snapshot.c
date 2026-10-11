/* Hardware-free checks: this file supplies the sole SPI read function. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "modem_snapshot.h"

static pthread_mutex_t hardware_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint16_t expected_base;
static unsigned calls;
static int fail_block;
static int expect_lock;

static const uint16_t block_addresses[MODEM_SNAPSHOT_BLOCK_COUNT] = {
    0x0005, 0x000D, 0x0000, 0x0016, 0x0021, 0x0025,
};
static const uint8_t block_sizes[MODEM_SNAPSHOT_BLOCK_COUNT] = {
    7, 2, 21, 1, 2, 4,
};

int at86rf215_read_buffer(at86rf215_st *device, uint16_t address,
                         uint8_t *buffer, uint8_t size)
{
    unsigned block = calls % MODEM_SNAPSHOT_BLOCK_COUNT;
    uint16_t expected = block_addresses[block] + (block < 2 ? 0 : expected_base);
    assert(device != NULL && buffer != NULL);
    assert(address == expected && size == block_sizes[block]);
    if (expect_lock) {
        /* Radio controls share this mutex. Each callback must hold it. */
        assert(pthread_mutex_trylock(&hardware_mutex) == EBUSY);
    }
    for (unsigned i = 0; i < size; ++i) {
        uint16_t byte_address = address + i;
        /* Read-clear interrupt/status latches must never be observed here. */
        assert(byte_address != 0x0000 && byte_address != 0x0001);
        assert(byte_address != 0x000C);
        /* Exact ranges additionally reject every reserved address. */
        buffer[i] = (uint8_t)byte_address;
    }
    ++calls;
    return (int)block == fail_block ? -1 : 0;
}

static void configure_mock(at86rf215_rf_channel_en channel, int failure,
                           int lock_expected)
{
    expected_base = channel == at86rf215_rf_channel_900mhz ? 0x0100 : 0x0200;
    calls = 0;
    fail_block = failure;
    expect_lock = lock_expected;
}

static int expected_common(unsigned index)
{
    if (index == 7)
        return -1;
    return 5 + (int)index;
}

static int expected_radio(unsigned index)
{
    static const int values[MODEM_SNAPSHOT_RADIO_COUNT] = {
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 22, 33, 34, 37, 38, 39, 40,
    };
    return values[index];
}

static int common_block(unsigned index)
{
    return index < 7 ? 0 : index >= 8 ? 1 : -1;
}

static int radio_block(unsigned index)
{
    return index == 0 ? -1 : index <= 21 ? 2 : index == 22 ? 3 :
           index <= 24 ? 4 : 5;
}

static void check_values(const modem_snapshot *cache, int failed)
{
    for (unsigned i = 0; i < MODEM_SNAPSHOT_COMMON_COUNT; ++i) {
        int expected = common_block(i) == failed && failed >= 0 ? -1 : expected_common(i);
        assert(cache->common[i] == expected);
    }
    for (unsigned i = 0; i < MODEM_SNAPSHOT_RADIO_COUNT; ++i) {
        int expected = radio_block(i) == failed && failed >= 0 ? -1 : expected_radio(i);
        assert(cache->radio[i] == expected);
    }
}

static void check_mutex_released(void)
{
    assert(pthread_mutex_trylock(&hardware_mutex) == 0);
    assert(pthread_mutex_unlock(&hardware_mutex) == 0);
}

static void check_channel(at86rf215_rf_channel_en channel)
{
    modem_snapshot cache;
    at86rf215_st device = {0};
    char json[4096];
    modem_snapshot_init(&cache, channel);
    for (unsigned i = 0; i < MODEM_SNAPSHOT_COMMON_COUNT; ++i)
        assert(cache.common[i] == -1);
    for (unsigned i = 0; i < MODEM_SNAPSHOT_RADIO_COUNT; ++i)
        assert(cache.radio[i] == -1);
    assert(cache.polls == 0 && cache.spi_transactions == 0 && cache.monotonic_s == 0);

    configure_mock(channel, -1, 1);
    assert(modem_snapshot_refresh(&cache, &device, channel, &hardware_mutex) == 0);
    assert(calls == 6 && cache.read_blocks == 6 && cache.failed_blocks == 0);
    assert(cache.polls == 1 && cache.spi_transactions == 6 && cache.spi_failures == 0);
    assert(cache.monotonic_s > 0 && cache.read_duration_s >= 0);
    check_values(&cache, -1);
    check_mutex_released();

    int length = modem_snapshot_hardware_json(&cache, json, sizeof json);
    assert(length > 0 && (size_t)length == strlen(json));
    assert(json[0] == '{' && json[length - 1] == '}');
    assert(strstr(json, "\"IRQS\":null") != NULL);
    assert(strstr(json, "\"IQIFC2\":null") != NULL);
    assert(strstr(json, "\"AGCC\":11,\"AGCS\":12,\"RSSI\":13") != NULL);
    assert(strstr(json, "\"read_blocks\":6,\"failed_blocks\":0") != NULL);
    if (channel == at86rf215_rf_channel_900mhz) {
        assert(strstr(json, "\"rf09\":{\"registers\":{") != NULL);
        assert(strstr(json, "\"rf24\":{\"valid\":false}") != NULL);
    } else {
        assert(strstr(json, "\"rf24\":{\"registers\":{") != NULL);
        assert(strstr(json, "\"rf09\":{\"valid\":false}") != NULL);
    }

    /* Every block failure must discard its prior successful cached values. */
    for (int failed = 0; failed < MODEM_SNAPSHOT_BLOCK_COUNT; ++failed) {
        configure_mock(channel, -1, 1);
        assert(modem_snapshot_refresh(&cache, &device, channel, &hardware_mutex) == 0);
        configure_mock(channel, failed, 1);
        double previous_time = cache.monotonic_s;
        assert(modem_snapshot_refresh(&cache, &device, channel, &hardware_mutex) == 1);
        assert(calls == 6 && cache.read_blocks == 6 && cache.failed_blocks == 1);
        assert(cache.monotonic_s >= previous_time && cache.read_duration_s >= 0);
        assert(cache.polls == (uint64_t)(3 + 2 * failed));
        assert(cache.spi_transactions == cache.polls * 6);
        assert(cache.spi_failures == (uint64_t)(failed + 1));
        check_values(&cache, failed);
        check_mutex_released();
        assert(modem_snapshot_hardware_json(&cache, json, sizeof json) > 0);
        assert(strstr(json, "\"failed_blocks\":1") != NULL);
    }

    /* NULL mutex is valid only for a caller with no concurrent controls. */
    configure_mock(channel, -1, 0);
    assert(modem_snapshot_refresh(&cache, &device, channel, NULL) == 0);
    check_values(&cache, -1);
}

static void check_invalid_arguments_and_bounds(void)
{
    modem_snapshot cache;
    at86rf215_st device = {0};
    char storage[4096];
    modem_snapshot_init(&cache, at86rf215_rf_channel_2400mhz);
    configure_mock(cache.channel, -1, 0);
    assert(modem_snapshot_refresh(NULL, &device, cache.channel, NULL) == -1);
    assert(errno == EINVAL);
    assert(modem_snapshot_refresh(&cache, NULL, cache.channel, NULL) == -1);
    assert(errno == EINVAL);
    assert(modem_snapshot_refresh(&cache, &device, (at86rf215_rf_channel_en)9, NULL) == -1);
    assert(errno == EINVAL && calls == 0);
    modem_snapshot_init(NULL, cache.channel);

    int length = modem_snapshot_hardware_json(&cache, storage, sizeof storage);
    assert(length > 0);
    /* Exact-fit capacity includes the trailing NUL; one less must fail. */
    assert(modem_snapshot_hardware_json(&cache, storage, (size_t)length + 1) == length);
    memset(storage, 'X', sizeof storage);
    assert(modem_snapshot_hardware_json(&cache, storage, (size_t)length) == -1);
    assert(errno == ENOSPC && storage[0] == '\0' && storage[length] == 'X');
    for (size_t capacity = 1; capacity <= 7; ++capacity) {
        memset(storage, 'X', sizeof storage);
        assert(modem_snapshot_hardware_json(&cache, storage, capacity) == -1);
        assert(errno == ENOSPC && storage[0] == '\0' && storage[capacity] == 'X');
    }
    assert(modem_snapshot_hardware_json(NULL, storage, sizeof storage) == -1);
    assert(errno == EINVAL);
    assert(modem_snapshot_hardware_json(&cache, NULL, sizeof storage) == -1);
    assert(errno == EINVAL);
    assert(modem_snapshot_hardware_json(&cache, storage, 0) == -1);
    assert(errno == EINVAL);
    cache.channel = (at86rf215_rf_channel_en)9;
    assert(modem_snapshot_hardware_json(&cache, storage, sizeof storage) == -1);
    assert(errno == EINVAL);
    cache.channel = at86rf215_rf_channel_2400mhz;
    cache.radio[23] = 256;
    assert(modem_snapshot_hardware_json(&cache, storage, sizeof storage) > 0);
    assert(strstr(storage, "\"PLL\":null") != NULL);
}

int main(void)
{
    check_channel(at86rf215_rf_channel_2400mhz);
    check_channel(at86rf215_rf_channel_900mhz);
    check_invalid_arguments_and_bounds();
    assert(pthread_mutex_destroy(&hardware_mutex) == 0);
    puts("modem snapshot tests passed (mock SPI, no hardware access)");
    return 0;
}
