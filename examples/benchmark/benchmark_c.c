#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* 1. fib — recursive call overhead */
static long long fib(long long n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

/* 2. sieve — integer compute + array mutation */
static void sieve_mark(long long *arr, long long i, long long step, long long limit) {
    while (i < limit) {
        arr[i] = 0;
        i += step;
    }
}

static long long sieve(long long limit) {
    long long *arr = malloc(limit * sizeof(long long));
    for (long long i = 0; i < limit; i++) arr[i] = 1;

    for (long long i = 2; i * i < limit; i++) {
        if (arr[i] == 1) {
            sieve_mark(arr, i * i, i, limit);
        }
    }

    long long count = 0;
    for (long long i = 2; i < limit; i++) {
        if (arr[i] == 1) count++;
    }
    free(arr);
    return count;
}

/* 3. array_push — allocation pressure */
static long long bench_array_push(long long limit) {
    long long cap = 16;
    long long len = 0;
    long long *arr = malloc(cap * sizeof(long long));

    for (long long i = 0; i < limit; i++) {
        if (len >= cap) {
            cap *= 2;
            arr = realloc(arr, cap * sizeof(long long));
        }
        arr[len++] = i;
    }

    long long result = len;
    free(arr);
    return result;
}

/* 4. array_sum — array read throughput */
static long long bench_array_sum(long long *arr, long long len) {
    long long acc = 0;
    for (long long i = 0; i < len; i++) {
        acc += arr[i];
    }
    return acc;
}

/* 5. str_concat — string allocation (O(n^2)) */
static long long bench_str_concat(long long limit) {
    long long len = 0;
    long long cap = 16;
    char *s = malloc(cap);
    s[0] = '\0';

    for (long long i = 0; i < limit; i++) {
        len++;
        if (len + 1 > cap) {
            cap *= 2;
            s = realloc(s, cap);
        }
        /* simulate O(n^2) concat like Glyph: alloc new, copy, append */
        char *new_s = malloc(len + 1);
        memcpy(new_s, s, len - 1);
        new_s[len - 1] = 'x';
        new_s[len] = '\0';
        free(s);
        s = new_s;
        cap = len + 1;
    }

    long long result = len;
    free(s);
    return result;
}

/* 6. str_builder — O(n) string building */
static long long bench_str_builder(long long limit) {
    long long cap = 256;
    long long len = 0;
    char *buf = malloc(cap);

    for (long long i = 0; i < limit; i++) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        buf[len++] = 'x';
    }
    buf[len] = '\0';

    long long result = len;
    free(buf);
    return result;
}

static void run_bench(const char *name, long long result, long long start_ns) {
    long long elapsed = (now_ns() - start_ns) / 1000LL;
    printf("%s: %lld us (result=%lld)\n", name, elapsed, result);
}

int main(void) {
    printf("=== C Benchmark Suite ===\n");

    long long t, r;

    t = now_ns();
    r = fib(35);
    run_bench("fib(35)", r, t);

    t = now_ns();
    r = sieve(1000000);
    run_bench("sieve(1M)", r, t);

    t = now_ns();
    r = bench_array_push(1000000);
    run_bench("array_push(1M)", r, t);

    /* build array for sum benchmark */
    long long *sum_arr = malloc(1000000 * sizeof(long long));
    for (long long i = 0; i < 1000000; i++) sum_arr[i] = i;

    t = now_ns();
    r = bench_array_sum(sum_arr, 1000000);
    run_bench("array_sum(1M)", r, t);
    free(sum_arr);

    t = now_ns();
    r = bench_str_concat(10000);
    run_bench("str_concat(10k)", r, t);

    t = now_ns();
    r = bench_str_builder(100000);
    run_bench("str_builder(100k)", r, t);

    return 0;
}
