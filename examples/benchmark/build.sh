#!/bin/sh
cd "$(dirname "$0")"

# Build Glyph/C benchmark
../../glyph build benchmark.glyph benchmark 2>/dev/null || true
cat bench_ffi.c /tmp/glyph_out.c > /tmp/bench_full.c
cc -O2 -Wno-int-conversion -Wno-incompatible-pointer-types -o benchmark /tmp/bench_full.c -no-pie -lm
echo "Built benchmark (Glyph/C)"

# Build Glyph/LLVM benchmark
../../glyph build benchmark.glyph /tmp/benchmark_llvm --emit=llvm 2>/dev/null || true
cat bench_ffi.c /tmp/glyph_runtime.c > /tmp/bench_runtime_full.c
clang -O2 -flto -w /tmp/bench_runtime_full.c /tmp/glyph_out.ll -o benchmark_llvm -no-pie -lm
echo "Built benchmark_llvm (Glyph/LLVM)"

# Build C comparison
cc -O2 -o benchmark_c benchmark_c.c
echo "Built benchmark_c (C)"

echo ""
echo "Run: ulimit -s unlimited && ./benchmark         (Glyph/C — needs large stack for recursion)"
echo "Run: ulimit -s unlimited && ./benchmark_llvm    (Glyph/LLVM — needs large stack for recursion)"
echo "Run: ./benchmark_c                              (C)"
