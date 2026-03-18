# Glyph Benchmark Results

Compiled with `-O2`. Glyph/C uses gen=2 C codegen compiled by GCC. Glyph/LLVM uses the LLVM IR backend compiled by clang with LTO (`-flto`). Glyph benchmarks require `ulimit -s unlimited` due to recursive implementation (no loop constructs).

## Results

| Benchmark | Glyph/C | Glyph/LLVM | Pure C | C ratio | LLVM ratio |
|-----------|---------|------------|--------|---------|------------|
| fib(35) | 15.7 ms | 24 ms | 22 ms | **0.71x** | 1.09x |
| sieve(1M) | 9.3 ms | 7.4 ms | 7.9 ms | 1.18x | **0.94x** |
| array_push(1M) | 4.0 ms | 2.7 ms | 3.6 ms | 1.11x | **0.75x** |
| array_sum(1M) | 0.75 ms | 0.52 ms | 0.63 ms | 1.19x | **0.83x** |
| str_concat(10k) | 15 ms | 16 ms | 1.9 ms | 7.9x | 8.4x |
| str_builder(100k) | 3.0 ms | 1.9 ms | 0.04 ms | 75x | **48x** |

*Ratios relative to pure C. Values below 1.0x mean Glyph beats pure C.*

## What Each Benchmark Measures

- **fib(35)**: Naive recursive fibonacci (~18M calls). Measures function call/return overhead.
- **sieve(1M)**: Sieve of Eratosthenes to 1M. Measures array indexing + mutation throughput.
- **array_push(1M)**: Push 1M integers. Measures `array_push` + realloc amortization.
- **array_sum(1M)**: Sum 1M elements. Measures bounds-checked array read speed.
- **str_concat(10k)**: Concatenate "x" 10k times with `+`. O(n^2) worst case.
- **str_builder(100k)**: Build 100k-char string via StringBuilder. O(n) string building.

## Analysis

**Function calls (fib):** The C backend is fastest here — gen=2 TCO emits goto loops that GCC optimizes below pure C's recursive version (0.71x). The LLVM backend is 1.09x vs pure C; the `alloca`-based frame layout is less aggressively collapsed by LLVM than GCC collapses the equivalent C.

**Array operations (sieve, push, sum):** LLVM+LTO wins. LTO enables clang to inline `glyph_array_bounds_check`, `glyph_array_set`, and the data-pointer GEP across the runtime boundary. Without LTO these are external calls in a tight loop; with LTO they vanish. The C backend compiles runtime + user code as a single translation unit (concatenated via `cat`), so GCC already has full visibility — but LLVM's optimizer finds more vectorization opportunities once the calls are inlined.

**String operations (concat, builder):** Both are dominated by malloc/call overhead rather than IR quality. `str_concat` is O(n²) malloc for both backends, giving ~8x vs pure C's in-place buffer. `str_builder` is better (O(n)), but still calls `glyph_sb_append` per character — LLVM+LTO inlines this better than GCC does from C (48x vs 75x ratio).

**LTO is required for the LLVM backend.** Without LTO, runtime functions remain external calls; array_sum degrades to 3ms (4x worse than pure C). With LTO, LLVM+clang inlines and vectorizes the runtime, matching or beating the C backend on array-heavy code.

## Reproduction

```bash
cd examples/benchmark
bash build.sh
ulimit -s unlimited && ./benchmark         # Glyph/C backend
ulimit -s unlimited && ./benchmark_llvm    # Glyph/LLVM backend (with LTO)
./benchmark_c                              # pure C
```
