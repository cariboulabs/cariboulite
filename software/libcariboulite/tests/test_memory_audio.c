#define NBFM_MEMORY_DEMO_NO_MAIN
#include "../tools/nbfm_memory_demo.c"
#include <assert.h>
#include <string.h>

static int fail_allocation, live_allocations;
void* __real_calloc(size_t, size_t);
void __real_free(void*);
void* __wrap_calloc(size_t count, size_t size)
{
    if (fail_allocation && --fail_allocation == 0) { errno = ENOMEM; return NULL; }
    void* p = __real_calloc(count, size);
    if (p) ++live_allocations;
    return p;
}
void __wrap_free(void* p)
{
    if (p) --live_allocations;
    __real_free(p);
}

// Adapter wrappers force short transfers through the same example runner.
typedef struct {
    audio_source_t base;
    audio_source_t* inner;
    size_t chunk;
    int stop; // 1: AGAIN, 2: invalid no-progress OK, 3: ERROR with valid samples
} short_source_t;
typedef struct {
    audio_sink_t base;
    audio_sink_t* inner;
    size_t chunk;
} short_sink_t;
static audio_source_result_t short_read(audio_source_t* base, float* dst, size_t count)
{
    short_source_t* s = (short_source_t*)base;
    if (s->stop == 1) return (audio_source_result_t){0, AUDIO_SOURCE_AGAIN, 0};
    if (s->stop == 2) return (audio_source_result_t){0, AUDIO_SOURCE_OK, 0};
    if (count > s->chunk) count = s->chunk;
    audio_source_result_t r = audio_source_read(s->inner, dst, count);
    if (s->stop == 3) { r.status = AUDIO_SOURCE_ERROR; r.error = -EIO; }
    return r;
}
static audio_sink_result_t short_write(audio_sink_t* base, const int16_t* src, size_t count)
{
    short_sink_t* s = (short_sink_t*)base;
    if (count > s->chunk) count = s->chunk;
    return audio_sink_write(s->inner, src, count);
}
static const audio_source_ops_t short_source_ops = {short_read, NULL};
static const audio_sink_ops_t short_sink_ops = {.write=short_write};

static void adapter_contracts(void)
{
    const audio_format_t format = {48000, 1};
    float input[] = {-1, 0.25f, 1};
    float out[5] = {91, 92, 93, 94, 95};
    assert(!memory_source_open(input, 3, (audio_format_t){44100, 1}) && errno == EINVAL);
    assert(!memory_source_open(input, 3, (audio_format_t){48000, 2}));
    assert(!memory_source_open(NULL, 1, format));
    assert(!memory_source_open(input, SIZE_MAX, format));
    audio_source_t* source = memory_source_open(input, 3, format); assert(source);
    audio_source_result_t r = audio_source_read(source, NULL, 0);
    assert(r.status == AUDIO_SOURCE_OK && !r.frames);
    r = audio_source_read(source, out+1, 2);
    assert(r.status == AUDIO_SOURCE_OK && r.frames == 2);
    assert(out[0] == 91 && out[1] == -1 && out[2] == 0.25f && out[3] == 94);
    r = audio_source_read(source, out+3, 2);
    assert(r.status == AUDIO_SOURCE_EOF && r.frames == 1 && out[3] == 1 && out[4] == 95);
    r = audio_source_read(source, out, 1);
    assert(r.status == AUDIO_SOURCE_EOF && !r.frames && out[0] == 91);
    audio_source_destroy(source);
    assert(input[0] == -1); // adapter did not free or modify borrowed stack array
    source = memory_source_open(NULL, 0, format); assert(source);
    assert(audio_source_read(source, out, 1).status == AUDIO_SOURCE_EOF);
    audio_source_destroy(source);

    int16_t pcm[] = {-32768, -1, 0, 32767};
    int16_t backing[5] = {77, 78, 79, 80, 81};
    assert(!memory_sink_open(backing, 3, (audio_format_t){48000, 2}));
    assert(!memory_sink_open(backing, 3, (audio_format_t){44100, 1}));
    assert(!memory_sink_open(NULL, 1, format));
    assert(!memory_sink_open(backing, SIZE_MAX, format));
    audio_sink_t* sink = memory_sink_open(backing+1, 3, format); assert(sink);
    assert(!strcmp(audio_sink_state(sink), "READY"));
    audio_sink_result_t w = audio_sink_write(sink, NULL, 0);
    assert(w.status == AUDIO_SINK_OK && !w.frames && !memory_sink_frames(sink));
    w = audio_sink_write(sink, pcm, 2);
    assert(w.status == AUDIO_SINK_OK && w.frames == 2 && memory_sink_frames(sink) == 2);
    w = audio_sink_write(sink, pcm+2, 2);
    assert(w.status == AUDIO_SINK_ERROR && w.error == -ENOSPC && w.frames == 1);
    assert(memory_sink_frames(sink) == 3 && !strcmp(audio_sink_state(sink), "FULL"));
    assert(backing[0] == 77 && backing[1] == -32768 && backing[2] == -1 && backing[3] == 0 && backing[4] == 81);
    w = audio_sink_write(sink, pcm, 1);
    assert(w.status == AUDIO_SINK_ERROR && !w.frames);
    audio_sink_destroy(sink);
    sink = memory_sink_open(backing, 4, format); assert(sink);
    w = audio_sink_write(sink, pcm, 4);
    assert(w.status == AUDIO_SINK_OK && w.frames == 4 && backing[3] == 32767);
    audio_sink_destroy(sink);
    sink = memory_sink_open(NULL, 0, format); assert(sink);
    assert(audio_sink_write(sink, pcm, 1).error == -ENOSPC);
    audio_sink_destroy(sink);
    assert(!memory_sink_frames(NULL));
    fail_allocation = 1;
    assert(!memory_source_open(input, 3, format) && errno == ENOMEM);
    fail_allocation = 1;
    assert(!memory_sink_open(backing, 3, format) && errno == ENOMEM);
    assert(!live_allocations);
    puts("PASS: finite EOF, partial capacity/error, unchanged samples, borrowed buffers, validation and allocation failures");
}

static void roundtrip(unsigned rate)
{
    enum { FRAMES = 12137 };
    static float input[FRAMES];
    static int16_t memory_output[FRAMES], tone_output[FRAMES], short_output[FRAMES];
    audio_format_t format = {48000, 1};
    audio_source_t* tone = tone_source_open(600, 0.4f, format); assert(tone);
    assert(audio_source_read(tone, input, FRAMES).frames == FRAMES);
    audio_source_destroy(tone);
    audio_source_t* source = memory_source_open(input, FRAMES, format); assert(source);
    audio_sink_t* sink = memory_sink_open(memory_output, FRAMES, format); assert(sink);
    roundtrip_counts_t a, b, c;
    assert(!run_roundtrip(source, sink, rate, FRAMES+1, &a));
    assert(a.audio_read == FRAMES && a.pcm_written >= FRAMES-2 && a.pcm_written <= FRAMES);
    assert(a.iq_produced == ((uint64_t)FRAMES*rate+47999)/48000);
    assert(memory_sink_frames(sink) == a.pcm_written);
    double pitch = recovered_pitch(memory_output, a.pcm_written);
    assert(fabs(pitch-600) < 2);
    audio_source_destroy(source); audio_sink_destroy(sink);

    tone = tone_source_open(600, 0.4f, format); assert(tone);
    sink = memory_sink_open(tone_output, FRAMES, format); assert(sink);
    assert(!run_roundtrip(tone, sink, rate, FRAMES, &b));
    assert(a.audio_read == b.audio_read && a.iq_produced == b.iq_produced && a.pcm_written == b.pcm_written);
    assert(!memcmp(memory_output, tone_output, a.pcm_written*sizeof(int16_t)));
    audio_source_destroy(tone); audio_sink_destroy(sink);

    source = memory_source_open(input, FRAMES, format); assert(source);
    sink = memory_sink_open(short_output, FRAMES, format); assert(sink);
    short_source_t short_in = {{format, &short_source_ops}, source, 13, 0};
    short_sink_t short_out = {{48000, &short_sink_ops}, sink, 7};
    assert(!run_roundtrip(&short_in.base, &short_out.base, rate, FRAMES+1, &c));
    assert(a.audio_read == c.audio_read && a.iq_produced == c.iq_produced && a.pcm_written == c.pcm_written);
    assert(!memcmp(memory_output, short_output, a.pcm_written*sizeof(int16_t)));
    audio_source_destroy(source); audio_sink_destroy(sink);

    source = memory_source_open(input, FRAMES, format); assert(source);
    sink = memory_sink_open(short_output, 17, format); assert(sink);
    assert(run_roundtrip(source, sink, rate, FRAMES, &c) == -ENOSPC);
    assert(c.pcm_written == 17 && memory_sink_frames(sink) == 17);
    assert(!memcmp(memory_output, short_output, 17*sizeof(int16_t)));
    audio_source_destroy(source); audio_sink_destroy(sink);
    assert(!live_allocations);
    printf("PASS: %u MS/s, memory/tone/short-transfer paths identical; %.2f Hz, final partial input and full sink\n", rate/1000000, pitch);
}
static void runner_errors(void)
{
    float input[480] = {0}; int16_t output[480];
    audio_format_t format = {48000, 1};
    audio_source_t* source = memory_source_open(input, 480, format); assert(source);
    audio_sink_t* sink = memory_sink_open(output, 480, format); assert(sink);
    roundtrip_counts_t counts;
    int owned = live_allocations;
    for (int failure = 1; failure <= 3; ++failure) {
        fail_allocation = failure;
        assert(run_roundtrip(source, sink, 4000000, 480, &counts) == -ENOMEM);
        assert(!counts.audio_read && live_allocations == owned);
    }
    assert(run_roundtrip(source, sink, 3000000, 480, &counts) == -EINVAL);
    assert(!run_roundtrip(source, sink, 4000000, 0, &counts) && !counts.audio_read);
    short_source_t transient = {{format, &short_source_ops}, source, 13, 1};
    assert(run_roundtrip(&transient.base, sink, 4000000, 480, &counts) == -EAGAIN);
    transient.stop = 2;
    assert(run_roundtrip(&transient.base, sink, 4000000, 480, &counts) == -EIO);
    transient.stop = 3;
    assert(run_roundtrip(&transient.base, sink, 4000000, 480, &counts) == -EIO);
    assert(counts.audio_read == 13 && counts.pcm_written > 0);
    audio_source_destroy(source); audio_sink_destroy(sink);
    assert(!live_allocations);
    puts("PASS: runner error progress, no-progress protection and DSP allocation cleanup");
}
int main(void)
{
    adapter_contracts();
    roundtrip(1000000); roundtrip(2000000); roundtrip(4000000);
    runner_errors();
    return 0;
}
