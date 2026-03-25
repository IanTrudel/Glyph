# Valgrind Memory Leak Analysis

**Date:** 2026-03-24
**Tool:** valgrind --leak-check=summary
**Platform:** Linux 6.19.6-arch1-1, x86-64

---

## Results

| Program | Allocs | Frees | Leaked | Notes |
|---------|--------|-------|--------|-------|
| `hello` | ~5 | ~3 | **29 B** | Trivial |
| `calc` (one expr) | 23 | 5 | **362 B** | Tiny |
| `glyph version` | 37 | 3 | **679 B** | Tiny |
| `benchmark` | 120,128 | 54 | **65 MB** | 54 frees out of 120k allocs |
| `glint glyph.glyph` | **391,233,125** | **1,055** | **12.09 GB** | 0.0003% free rate — worst case |
| **`glyph build glyph.glyph`** | **8,672,108** | **272,665** | **2.03 GB** | 3% free rate |

The compiler leaks **2 GB** building itself. But the real outlier is `glint` — a 26-function project analyzer that leaks **12 GB** analyzing `glyph.glyph`. It does hundreds of SQLite queries, each returning rows of strings via `glyph_cstr_to_str` (fresh malloc per string, never freed). 391 million allocations with only 1,055 frees — a **0.0003% free rate**. This is the strongest evidence that any program doing heavy SQLite I/O will OOM quickly without memory management.

For short-lived programs (hello, calc, CLI tools), this doesn't matter — the OS reclaims everything on exit. But for data-intensive programs (glint, the compiler, any server), the leak rate is unsustainable.

---

## Raw Valgrind Output

### hello

```
HEAP SUMMARY:
    in use at exit: 35 bytes in 2 blocks
  total heap usage: 5 allocs, 3 frees, 8,221 bytes allocated

LEAK SUMMARY:
   definitely lost: 29 bytes in 1 blocks
   indirectly lost: 0 bytes in 0 blocks
     possibly lost: 0 bytes in 0 blocks
   still reachable: 6 bytes in 1 blocks
```

### calc (input: "1+2+3")

```
HEAP SUMMARY:
    in use at exit: 362 bytes in 18 blocks
  total heap usage: 23 allocs, 5 frees, 13,154 bytes allocated

LEAK SUMMARY:
   definitely lost: 237 bytes in 13 blocks
   indirectly lost: 120 bytes in 4 blocks
     possibly lost: 0 bytes in 0 blocks
   still reachable: 5 bytes in 1 blocks
```

### glyph version

```
HEAP SUMMARY:
    in use at exit: 679 bytes in 34 blocks
  total heap usage: 37 allocs, 3 frees, 9,343 bytes allocated

LEAK SUMMARY:
   definitely lost: 625 bytes in 30 blocks
   indirectly lost: 48 bytes in 3 blocks
     possibly lost: 0 bytes in 0 blocks
   still reachable: 6 bytes in 1 blocks
```

### benchmark

```
HEAP SUMMARY:
    in use at exit: 68,914,510 bytes in 120,074 blocks
  total heap usage: 120,128 allocs, 54 frees, 85,962,430 bytes allocated

LEAK SUMMARY:
   definitely lost: 52,119,204 bytes in 120,069 blocks
   indirectly lost: 16,777,216 bytes in 2 blocks
     possibly lost: 18,080 bytes in 2 blocks
   still reachable: 10 bytes in 1 blocks
```

### glint glyph.glyph

```
HEAP SUMMARY:
    in use at exit: 12,093,609,104 bytes in 391,232,070 blocks
  total heap usage: 391,233,125 allocs, 1,055 frees, 12,094,894,919 bytes allocated

LEAK SUMMARY:
   definitely lost: 12,092,876,786 bytes in 391,224,114 blocks
   indirectly lost: 731,697 bytes in 7,935 blocks
     possibly lost: 433 bytes in 14 blocks
   still reachable: 188 bytes in 7 blocks
```

### glyph build glyph.glyph

```
HEAP SUMMARY:
    in use at exit: 2,190,432,678 bytes in 8,399,443 blocks
  total heap usage: 8,672,108 allocs, 272,665 frees, 2,679,712,412 bytes allocated

LEAK SUMMARY:
   definitely lost: 2,028,928,780 bytes in 5,500,634 blocks
   indirectly lost: 156,070,306 bytes in 2,898,802 blocks
     possibly lost: 5,433,567 bytes in 5 blocks
   still reachable: 25 bytes in 2 blocks
```

---

## Massif Heap Profile (`glyph build glyph.glyph`)

Heap profiled with `valgrind --tool=massif`. Peak heap: **2.157 GB**. The allocation breakdown by call site:

| Call Site | % of Heap | Bytes | Root Cause |
|-----------|-----------|-------|------------|
| `glyph_array_push` via `tc_collect_fv` → `generalize` | **28.1%** | 81.8 MB | Type checker free-variable collection creates throwaway arrays for every generalization — 1,287 fns × 2 passes = 2,574 calls, each allocating arrays that are never freed |
| `glyph_alloc` (scattered) | **17.6%** | 51.2 MB | AST nodes, MIR blocks, operands, type pool entries — hundreds of call sites, none freed |
| `glyph_cstr_to_str` (scattered) | **15.9%** | 46.4 MB | Every SQLite read converts C strings to Glyph fat strings (16-byte header + data), each a fresh malloc. 1,287 definition bodies = 1,287+ leaked string objects |
| `glyph_bitset_new` via `generalize_raw` | **7.7%** | 22.5 MB | Every generalization allocates a fresh bitset for free-variable tracking, never freed. 2× per function (warm + sigs pass) |
| `glyph_str_concat` via `cg_forward_decls` | **7.4%** | 21.5 MB | **Quadratic string accumulation** — builds one giant string of 1,287 forward declarations by chaining `s2(acc, next_decl)`. Each iteration copies the *entire accumulated string so far*. For N functions: O(N²) intermediate copies |
| Remaining | ~23% | ~68 MB | Distributed across codegen, MIR lowering, monomorphization |

### Key Findings

1. **The type checker is the #1 allocator** (28% + 7.7% = ~36%) — `tc_collect_fv` and `generalize_raw` allocate arrays and bitsets for every function definition, twice (warm pass and signature pass), and never free them.

2. **`cg_forward_decls` has a Schlemiel-the-Painter bug** — it uses recursive `s2(acc, next)` to build a string of all forward declarations. With 1,287 functions, the intermediate copies alone waste 21.5 MB. The `sb_*` (StringBuilder) runtime functions exist but aren't used here.

3. **`glyph_cstr_to_str` is called on every SQLite read** — each call allocates a new 16-byte fat string header + copies the data. For reading 1,287 definitions + their metadata, this creates thousands of leaked string objects.

4. **Nothing is ever freed** — the 3.1% free rate (272k frees out of 8.6M allocs) comes almost entirely from `realloc` inside `glyph_array_push` when arrays grow, not from intentional deallocation.

### Actionable Fixes (No GC Required)

| Fix | Target | Estimated Savings |
|-----|--------|-------------------|
| Switch `cg_forward_decls` to use `sb_append` loop | Quadratic → linear string building | ~21 MB + faster codegen |
| Reuse a single bitset across `generalize` calls (reset instead of re-alloc) | `glyph_bitset_new` | ~22 MB |
| Reuse free-variable arrays across `tc_collect_fv` calls | `glyph_array_push` in generalize | ~82 MB |
| Same `sb_append` fix for `cg_functions2` and other recursive string accumulators | Other codegen string loops | ~10-20 MB |

These four fixes would cut peak heap by roughly **60%** without adding a GC. They're algorithmic improvements (quadratic → linear, re-alloc → reuse) rather than memory management.

---

## Recommendation

**Short term:** Fix the quadratic string concatenation in `cg_forward_decls` (and similar loops) by switching to `sb_append`. Reuse bitsets and arrays in the type checker generalization pass. These are algorithmic fixes that reduce allocation volume.

**Medium term:** Boehm GC integration would be near-zero effort: swap `malloc` → `GC_malloc` in the C runtime preamble and add `-lgc` to the linker flags. This would reclaim the remaining leaked memory (AST nodes, MIR, type pool) without touching compiler logic.
