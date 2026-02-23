# Glyph Benchmark Results

Compiled with `-O2`. Glyph uses the self-hosted compiler (gen=1 C codegen). Glyph benchmarks require `ulimit -s unlimited` due to recursive implementation (no loop constructs).

## Results

| Benchmark | Glyph | C | Ratio |
|-----------|-------|---|-------|
| fib(35) | 61.6 ms | 19.6 ms | 3.1x |
| sieve(1M) | 61.2 ms | 7.4 ms | 8.3x |
| array_push(1M) | 16.4 ms | 4.2 ms | 3.9x |
| array_sum(1M) | 9.9 ms | 0.5 ms | 21x |
| str_concat(10k) | 17.0 ms | 1.8 ms | 9.4x |
| str_builder(100k) | 4.3 ms | 0.05 ms | 92x |

## What Each Benchmark Measures

- **fib(35)**: Naive recursive fibonacci (~18M calls). Measures function call/return overhead.
- **sieve(1M)**: Sieve of Eratosthenes to 1M. Measures array indexing + mutation throughput.
- **array_push(1M)**: Push 1M integers. Measures `array_push` + realloc amortization.
- **array_sum(1M)**: Sum 1M elements. Measures bounds-checked array read speed.
- **str_concat(10k)**: Concatenate "x" 10k times with `+`. O(n^2) worst case.
- **str_builder(100k)**: Build 100k-char string via StringBuilder. O(n) string building.

## Analysis

**Function call overhead (fib, 3.1x):** The main cost is per-call bookkeeping — updating `_glyph_current_fn`, push/pop call stack for crash reporting, and no tail-call optimization. Pure computation within a function is close to C speed.

**Array operations (sieve 8.3x, push 3.9x, sum 21x):** Every array access goes through a runtime function call (`glyph_array_set`, `glyph_array_len`, bounds check). C uses direct pointer arithmetic. The sum benchmark shows the worst case: a tight loop of bounds-checked reads vs raw pointer iteration.

**String operations (concat 9.4x, builder 92x):** String concat allocates a new heap string per operation — the 9.4x ratio reflects malloc overhead vs C's in-place buffer. StringBuilder's 92x gap is because Glyph calls `glyph_sb_append` (a C function) per character, while the C version inlines the buffer write to a single instruction.

## Reproduction

```bash
cd examples/benchmark
bash build.sh
ulimit -s unlimited && ./benchmark    # Glyph
./benchmark_c                         # C
```
