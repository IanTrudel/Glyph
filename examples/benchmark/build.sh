#!/bin/sh
cd "$(dirname "$0")"

# Build Glyph benchmark
../../glyph build benchmark.glyph benchmark 2>/dev/null || true
cat bench_ffi.c /tmp/glyph_out.c > /tmp/bench_full.c
cc -O2 -Wno-int-conversion -Wno-incompatible-pointer-types -o benchmark /tmp/bench_full.c -no-pie
echo "Built benchmark (Glyph)"

# Build C comparison
cc -O2 -o benchmark_c benchmark_c.c
echo "Built benchmark_c (C)"

echo ""
echo "Run: ulimit -s unlimited && ./benchmark    (Glyph — needs large stack for recursion)"
echo "Run: ./benchmark_c                         (C)"
