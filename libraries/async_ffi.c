/* async_ffi.c — Minimal coroutine + epoll FFI for Glyph async library
 *
 * Provides: ucontext-based coroutines, epoll wrappers, timerfd wrappers.
 * Everything else (scheduler, channels, etc.) is written in Glyph.
 *
 * Prepended before Glyph runtime via cc_prepend.
 */
#include <ucontext.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <gc/gc.h>

typedef intptr_t GVal;

/* ── Coroutine ────────────────────────────────────────────────────────── */

typedef struct {
    ucontext_t ctx;
    ucontext_t caller_ctx;
    void      *stack;
    int        status;   /* 0=SUSPENDED, 1=RUNNING, 2=DEAD */
    GVal       entry_fn; /* Glyph closure */
    GVal       arg;
    GVal       result;
    size_t     stack_size;
} Coro;

static Coro *_current_coro = NULL;
static GVal  _current_task_id = 0;
static GVal  _global_sched = 0;

static void _coro_entry(unsigned int hi, unsigned int lo) {
    Coro *co = (Coro *)(((uintptr_t)hi << 32) | (uintptr_t)lo);
    GVal *cl = (GVal *)co->entry_fn;
    co->result = ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, co->arg);
    co->status = 2; /* DEAD */
    GC_enable(); /* Balance the GC_disable() in coro_resume */
    swapcontext(&co->ctx, &co->caller_ctx);
}

GVal coro_create(GVal entry_fn, GVal arg, GVal stack_size) {
    Coro *co = (Coro *)GC_malloc(sizeof(Coro));
    memset(co, 0, sizeof(Coro));
    /* Allocate stack via mmap — Boehm GC crashes if RSP points into
       GC-managed heap during collection (it can't scan coroutine stacks).
       mmap gives us proper page-aligned memory outside the GC heap.
       Register as GC roots so pointers on the coroutine stack are found. */
    size_t sz = (size_t)stack_size;
    void *stk = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    GC_add_roots(stk, (char *)stk + sz);
    co->stack = stk;
    co->stack_size = sz;
    co->entry_fn = entry_fn;
    co->arg = arg;
    co->status = 0; /* SUSPENDED */
    co->result = 0;
    getcontext(&co->ctx);
    co->ctx.uc_stack.ss_sp = co->stack;
    co->ctx.uc_stack.ss_size = co->stack_size;
    co->ctx.uc_link = NULL;
    uintptr_t p = (uintptr_t)co;
    makecontext(&co->ctx, (void (*)())_coro_entry, 2,
                (unsigned int)(p >> 32), (unsigned int)(p & 0xFFFFFFFF));
    return (GVal)co;
}

GVal coro_resume(GVal handle) {
    Coro *co = (Coro *)handle;
    co->status = 1; /* RUNNING */
    Coro *prev = _current_coro;
    _current_coro = co;
    /* Disable GC before entering coroutine — Boehm's stack scanner
       doesn't know about coroutine stacks and crashes if RSP is in
       mmap'd memory. GC_enable() is called by coro_yield/_coro_entry
       before switching back, keeping the counter balanced. */
    GC_disable();
    swapcontext(&co->caller_ctx, &co->ctx);
    /* Back on OS stack — GC already re-enabled by yield/completion */
    _current_coro = prev;
    return co->result;
}

GVal coro_yield(GVal dummy) {
    (void)dummy;
    Coro *co = _current_coro;
    co->status = 0; /* SUSPENDED */
    /* Re-enable GC before switching back to caller (which runs on OS stack).
       The matching GC_disable() is in coro_resume(). When resumed later,
       coro_resume will GC_disable() again before switching back to us. */
    GC_enable();
    swapcontext(&co->ctx, &co->caller_ctx);
    /* We've been resumed — GC is disabled again by coro_resume's GC_disable() */
    return 0;
}

GVal coro_status(GVal handle) { return (GVal)((Coro *)handle)->status; }

GVal coro_destroy(GVal handle) {
    Coro *co = (Coro *)handle;
    co->status = 2;
    if (co->stack) {
        GC_remove_roots(co->stack, (char *)co->stack + co->stack_size);
        munmap(co->stack, co->stack_size);
        co->stack = NULL;
    }
    return 0;
}

/* ── Epoll ────────────────────────────────────────────────────────────── */

GVal async_epoll_new(GVal dummy) { (void)dummy; return (GVal)epoll_create1(0); }

GVal async_epoll_add(GVal epfd, GVal fd, GVal events) {
    struct epoll_event ev = {0};
    ev.events = (uint32_t)events;
    ev.data.fd = (int)fd;
    return (GVal)epoll_ctl((int)epfd, EPOLL_CTL_ADD, (int)fd, &ev);
}

GVal async_epoll_del(GVal epfd, GVal fd) {
    return (GVal)epoll_ctl((int)epfd, EPOLL_CTL_DEL, (int)fd, NULL);
}

GVal async_epoll_poll(GVal epfd, GVal max_events, GVal timeout_ms) {
    struct epoll_event evs[64];
    int n = (int)max_events;
    if (n > 64) n = 64;
    int ready = epoll_wait((int)epfd, evs, n, (int)timeout_ms);
    if (ready <= 0) return 0;
    /* Build a Glyph array {data*, len, cap} */
    GVal *hdr = (GVal *)GC_malloc(3 * sizeof(GVal));
    GVal *data = (GVal *)GC_malloc(ready * sizeof(GVal));
    for (int i = 0; i < ready; i++) data[i] = (GVal)evs[i].data.fd;
    hdr[0] = (GVal)data;
    hdr[1] = (GVal)ready;
    hdr[2] = (GVal)ready;
    return (GVal)hdr;
}

/* ── Timer ────────────────────────────────────────────────────────────── */

GVal async_timer_create_ms(GVal ms) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    struct itimerspec its = {0};
    its.it_value.tv_sec = (time_t)(ms / 1000);
    its.it_value.tv_nsec = (long)((ms % 1000) * 1000000);
    timerfd_settime(tfd, 0, &its, NULL);
    return (GVal)tfd;
}

GVal async_timer_read(GVal fd) {
    uint64_t val = 0;
    read((int)fd, &val, sizeof(val));
    return (GVal)val;
}

/* ── Utilities ────────────────────────────────────────────────────────── */

GVal async_fd_close(GVal fd) { close((int)fd); return 0; }

GVal async_fd_set_nonblock(GVal fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
    return 0;
}

/* ── Global state ─────────────────────────────────────────────────────── */

GVal async_sched_global_set(GVal ptr) { _global_sched = ptr; return 0; }
GVal async_sched_global_get(GVal dummy) { (void)dummy; return _global_sched; }

GVal async_current_task_id(GVal dummy) { (void)dummy; return _current_task_id; }

GVal async_set_task_id(GVal id) { _current_task_id = id; return 0; }
