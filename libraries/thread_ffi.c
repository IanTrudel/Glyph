/* thread_ffi.c — pthreads FFI for Glyph thread library
 *
 * Provides: thread spawn/join, mutex, channels (bounded + unbounded).
 * Prepended before Glyph runtime via cc_prepend.
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#ifndef GC_THREADS
#define GC_THREADS
#endif
#include <gc/gc.h>

typedef intptr_t GVal;

/* ── Thread ──────────────────────────────────────────────────────────── */

typedef struct {
    GVal closure;
    GVal result;
    pthread_t thread;
    int joined;
} GlyphThread;

extern __thread const char* _glyph_current_fn;

static void* glyph_thread_entry(void* arg) {
    GlyphThread* gt = (GlyphThread*)arg;
    _glyph_current_fn = "(spawned thread)";
    GVal (*fp)(GVal, GVal) = (GVal(*)(GVal,GVal))((GVal*)gt->closure)[0];
    gt->result = fp(gt->closure, 0);
    return NULL;
}

GVal thread_spawn(GVal closure) {
    GlyphThread* gt = (GlyphThread*)GC_malloc(sizeof(GlyphThread));
    gt->closure = closure;
    gt->joined = 0;
    gt->result = 0;
    GC_pthread_create(&gt->thread, NULL, glyph_thread_entry, gt);
    return (GVal)gt;
}

GVal thread_join(GVal handle) {
    GlyphThread* gt = (GlyphThread*)handle;
    if (!gt->joined) {
        GC_pthread_join(gt->thread, NULL);
        gt->joined = 1;
    }
    return gt->result;
}

/* ── Mutex ───────────────────────────────────────────────────────────── */

GVal thread_mutex_new(GVal dummy) {
    (void)dummy;
    pthread_mutex_t* m = (pthread_mutex_t*)GC_malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(m, NULL);
    return (GVal)m;
}

GVal thread_mutex_lock(GVal handle) {
    pthread_mutex_lock((pthread_mutex_t*)handle);
    return 0;
}

GVal thread_mutex_unlock(GVal handle) {
    pthread_mutex_unlock((pthread_mutex_t*)handle);
    return 0;
}

/* ── Channel ─────────────────────────────────────────────────────────── */

typedef struct {
    GVal* buffer;
    long long head;
    long long tail;
    long long len;
    long long cap;       /* allocated buffer size (always > 0) */
    long long max_len;   /* 0 = unbounded (grows), >0 = bounded (blocks at max) */
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int closed;
} GlyphChannel;

static void chan_grow(GlyphChannel* ch) {
    long long new_cap = ch->cap * 2;
    GVal* new_buf = (GVal*)GC_malloc(new_cap * sizeof(GVal));
    for (long long i = 0; i < ch->len; i++) {
        new_buf[i] = ch->buffer[(ch->head + i) % ch->cap];
    }
    ch->buffer = new_buf;
    ch->head = 0;
    ch->tail = ch->len;
    ch->cap = new_cap;
}

GVal thread_chan_new(GVal dummy) {
    (void)dummy;
    GlyphChannel* ch = (GlyphChannel*)GC_malloc(sizeof(GlyphChannel));
    memset(ch, 0, sizeof(GlyphChannel));
    ch->buffer = (GVal*)GC_malloc(16 * sizeof(GVal));
    ch->cap = 16;
    ch->max_len = 0;  /* unbounded */
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
    return (GVal)ch;
}

GVal thread_chan_bounded(GVal capacity) {
    GlyphChannel* ch = (GlyphChannel*)GC_malloc(sizeof(GlyphChannel));
    memset(ch, 0, sizeof(GlyphChannel));
    ch->buffer = (GVal*)GC_malloc(capacity * sizeof(GVal));
    ch->cap = capacity;
    ch->max_len = capacity;  /* bounded */
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
    return (GVal)ch;
}

GVal thread_chan_send(GVal handle, GVal value) {
    GlyphChannel* ch = (GlyphChannel*)handle;
    pthread_mutex_lock(&ch->lock);
    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        fprintf(stderr, "panic: send on closed channel\n");
        exit(1);
    }
    if (ch->max_len > 0) {
        /* bounded: block if full */
        while (ch->len >= ch->max_len && !ch->closed) {
            pthread_cond_wait(&ch->not_full, &ch->lock);
        }
        if (ch->closed) {
            pthread_mutex_unlock(&ch->lock);
            fprintf(stderr, "panic: send on closed channel\n");
            exit(1);
        }
    } else {
        /* unbounded: grow buffer if needed */
        if (ch->len >= ch->cap) {
            chan_grow(ch);
        }
    }
    ch->buffer[ch->tail] = value;
    ch->tail = (ch->tail + 1) % ch->cap;
    ch->len++;
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

GVal thread_chan_recv(GVal handle) {
    GlyphChannel* ch = (GlyphChannel*)handle;
    pthread_mutex_lock(&ch->lock);
    while (ch->len == 0 && !ch->closed) {
        pthread_cond_wait(&ch->not_empty, &ch->lock);
    }
    if (ch->len == 0 && ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return -1;  /* sentinel: channel closed and empty */
    }
    GVal value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->cap;
    ch->len--;
    if (ch->max_len > 0) {
        pthread_cond_signal(&ch->not_full);
    }
    pthread_mutex_unlock(&ch->lock);
    return value;
}

GVal thread_chan_close(GVal handle) {
    GlyphChannel* ch = (GlyphChannel*)handle;
    pthread_mutex_lock(&ch->lock);
    ch->closed = 1;
    pthread_cond_broadcast(&ch->not_empty);
    pthread_cond_broadcast(&ch->not_full);
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

GVal thread_chan_len(GVal handle) {
    GlyphChannel* ch = (GlyphChannel*)handle;
    pthread_mutex_lock(&ch->lock);
    long long n = ch->len;
    pthread_mutex_unlock(&ch->lock);
    return n;
}

GVal thread_chan_closed(GVal handle) {
    GlyphChannel* ch = (GlyphChannel*)handle;
    return ch->closed ? 1 : 0;
}

/* ── Atomic Ref ──────────────────────────────────────────────────────── */

GVal thread_atomic_new(GVal val) {
    GVal* r = (GVal*)GC_malloc(sizeof(GVal));
    __atomic_store_n(r, val, __ATOMIC_SEQ_CST);
    return (GVal)r;
}

GVal thread_atomic_load(GVal handle) {
    return __atomic_load_n((GVal*)handle, __ATOMIC_SEQ_CST);
}

GVal thread_atomic_store(GVal handle, GVal val) {
    __atomic_store_n((GVal*)handle, val, __ATOMIC_SEQ_CST);
    return 0;
}

GVal thread_atomic_add(GVal handle, GVal delta) {
    return __atomic_fetch_add((GVal*)handle, delta, __ATOMIC_SEQ_CST);
}

GVal thread_atomic_cas(GVal handle, GVal expected, GVal desired) {
    GVal exp = expected;
    return __atomic_compare_exchange_n((GVal*)handle, &exp, desired,
                                       0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? 1 : 0;
}
