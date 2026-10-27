/*
 * Makes PulseAudio report a given latency for FreeDV's record streams, the
 * way some PipeWire versions do (about 2055 seconds in #1601). Everything
 * else is passed on untouched.
 *
 *   $FREEDVTEST_PULSE_RECORD_LATENCY_US   the latency to report, in microseconds
 *   $FREEDVTEST_PULSE_SHIM_LOG            a line is added here each time it's reported
 *
 * Loaded into FreeDV with LD_PRELOAD by freedvtest.pulse. libpulse's own
 * headers aren't needed: the streams are only passed through.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_STREAMS 64

typedef struct pa_stream pa_stream;
typedef struct pa_buffer_attr pa_buffer_attr;
typedef uint64_t pa_usec_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pa_stream* record_streams[MAX_STREAMS];

static void* next(const char* name)
{
    void* fn = dlsym(RTLD_NEXT, name);
    if (fn == NULL)
    {
        fprintf(stderr, "pulse_shim: %s not found\n", name);
        abort();
    }
    return fn;
}

static int is_record_stream(pa_stream* s)
{
    int found = 0;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_STREAMS; i++)
    {
        if (record_streams[i] == s) found = 1;
    }
    pthread_mutex_unlock(&lock);
    return found;
}

static void forget(pa_stream* s)
{
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_STREAMS; i++)
    {
        if (record_streams[i] == s) record_streams[i] = NULL;
    }
    pthread_mutex_unlock(&lock);
}

int pa_stream_connect_record(pa_stream* s, const char* dev, const pa_buffer_attr* attr, int flags)
{
    static int (*real)(pa_stream*, const char*, const pa_buffer_attr*, int);
    if (real == NULL) real = next("pa_stream_connect_record");

    int result = real(s, dev, attr, flags);
    if (result == 0)
    {
        pthread_mutex_lock(&lock);
        for (int i = 0; i < MAX_STREAMS; i++)
        {
            if (record_streams[i] == NULL)
            {
                record_streams[i] = s;
                break;
            }
        }
        pthread_mutex_unlock(&lock);
    }
    return result;
}

int pa_stream_disconnect(pa_stream* s)
{
    static int (*real)(pa_stream*);
    if (real == NULL) real = next("pa_stream_disconnect");

    // A stream made afterwards could be given the same address.
    forget(s);
    return real(s);
}

int pa_stream_get_latency(pa_stream* s, pa_usec_t* r_usec, int* negative)
{
    static int (*real)(pa_stream*, pa_usec_t*, int*);
    if (real == NULL) real = next("pa_stream_get_latency");

    int result = real(s, r_usec, negative);
    const char* latency = getenv("FREEDVTEST_PULSE_RECORD_LATENCY_US");
    if (latency != NULL && is_record_stream(s))
    {
        *r_usec = strtoull(latency, NULL, 10);
        if (negative != NULL) *negative = 0;
        result = 0;

        const char* log = getenv("FREEDVTEST_PULSE_SHIM_LOG");
        FILE* f = log != NULL ? fopen(log, "a") : NULL;
        if (f != NULL)
        {
            fprintf(f, "record latency %llu us\n", (unsigned long long)*r_usec);
            fclose(f);
        }
    }
    return result;
}
