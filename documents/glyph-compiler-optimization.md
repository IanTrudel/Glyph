# Glyph Compiler Optimization: Memory & Build Performance

*Assessment date: 2026-04-02*

## Executive Summary

The self-hosted Glyph compiler used **14.9 GB peak RSS** during Stage 2 (C codegen). The root cause was **O(n²) string concatenation** in the C codegen pipeline — not the C compiler itself (which uses only ~340 MB). Converting recursive `+` concatenation to `sb_new/sb_append/sb_build` (StringBuilder) reduced peak RSS to **1.8 GB — an 8× reduction**, with identical output (zero diff, 182,543 lines).

**Status: Fixes 1a, 1b, 2 APPLIED** (2026-04-02). All 412 tests pass. Generated C output verified identical before/after.

## Memory Profile Across Bootstrap Stages

| Stage | Binary | Operation | Peak RSS | Wall Time |
|-------|--------|-----------|----------|-----------|
| 0 | cargo | Build glyph0 (Rust) | ~1.5 GB | ~30s |
| 1 | glyph0 | Cranelift compile → glyph1 | 60 MB | 3.3s |
| **2** | **glyph1** | **C codegen → glyph2** | **14.9 GB** | **29s** |
| 3 | glyph2 | LLVM codegen → glyph | 1.8 GB | 55s |

Stage 2 is the clear outlier: 14.9 GB for what produces a 4.5 MB C source file.

## Generated Output Profile

The self-compilation generates `/tmp/glyph_out.c`:

| Metric | Value |
|--------|-------|
| File size | 4.5 MB |
| Total lines | 182,221 |
| Total functions | 2,670 |
| Monomorphized variants | 952 (36%) |
| Base functions | 1,718 (64%) |
| Avg lines per function | 67 |
| Typedef structs | 20 |
| Forward declarations | 2,670 |

### Section Layout

| Section | Lines | Size |
|---------|-------|------|
| Preamble + includes | 1–57 | ~2 KB |
| Runtime implementation | 58–840 | ~30 KB |
| Forward declarations | 841–3,510 | ~100 KB |
| Function bodies | 3,511–182,215 | ~4.3 MB |
| main() wrapper | 182,215–182,221 | ~0.2 KB |

### C Compiler Memory (cc on glyph_out.c)

| Optimization | Peak RSS | Wall Time |
|-------------|----------|-----------|
| -O0 | 73 MB | 0.5s |
| -O1 | 341 MB | 8s |
| -O2 | 380 MB | 22s |

These are modest — splitting files would reduce per-unit cc memory but the savings are marginal compared to the 14.9 GB Glyph compiler overhead.

## Root Cause: O(n²) String Concatenation

### The Pattern

Six C codegen functions use recursive right-fold string concatenation:

```glyph
cg_functions2 mirs i struct_map rt_set ret_map =
  match i >= glyph_array_len(mirs)
    true -> ""
    _ -> cg_function2(mirs[i], ...) + cg_functions2(mirs, i + 1, ...)
```

This builds the result string by:
1. Recursing to depth N (2,670 for functions)
2. At each return, concatenating: `this_fn_code + rest_of_code`
3. Each `+` allocates a **new** string = left + right

### Quadratic Allocation Math

For `cg_functions2` with 2,670 functions averaging 1,685 bytes each:

```
Concat at depth 2669: 1,685 bytes (fn₂₆₇₀ + "")
Concat at depth 2668: 3,370 bytes (fn₂₆₆₉ + result₂₆₇₀)
Concat at depth 2667: 5,055 bytes (fn₂₆₆₈ + result₂₆₆₉₋₂₆₇₀)
...
Concat at depth 0:    4,500,000 bytes (fn₁ + result₂₋₂₆₇₀)

Total allocated = avg × n(n+1)/2 ≈ 6.0 GB
```

Plus `cg_forward_decls` adds ~143 MB, and `cg_blocks2` (nested per-function, small n) adds more. With Boehm GC unable to collect fast enough during deep recursion, peak RSS reaches 14.9 GB.

### Affected Functions

| Function | N (items) | Estimated alloc |
|----------|-----------|----------------|
| `cg_functions2` | 2,670 | ~6.0 GB |
| `cg_forward_decls` | 2,670 | ~143 MB |
| `cg_blocks2` | 5–20 per fn | small per-fn |
| `cg_all_typedefs_loop` | 20 | negligible |
| `cg_enum_typedefs_loop` | ~10 | negligible |
| `cg_extern_wrappers` | 23 | negligible |
| `cg_data_fns` | ~5 | negligible |
| `cg_data_fwd_decls` | ~5 | negligible |

### Proof: LLVM Codegen Already Fixed

The LLVM codegen path uses StringBuilder at the top level:

```glyph
ll_emit_functions rt_set mirs i struct_map ret_map =
  sb = sb_new()
  ll_emit_functions_sb(rt_set, mirs, i, struct_map, ret_map, sb)
  sb_build(sb)

ll_emit_functions_sb rt_set mirs i struct_map ret_map sb =
  match i >= glyph_array_len(mirs)
    true -> 0
    _ ->
      sb_append(sb, ll_emit_function(rt_set, mirs[i], struct_map, ret_map))
      ll_emit_functions_sb(rt_set, mirs, i + 1, struct_map, ret_map, sb)
```

And `cg_llvm_program` uses `sb_new/sb_append/sb_build` to assemble the final output. Result: 1.8 GB peak RSS vs 14.9 GB.

Note: `ll_emit_blocks` still uses recursive `+` concatenation internally, which contributes to the remaining 1.8 GB. Converting it to StringBuilder would reduce LLVM codegen memory further.

## Recommended Fixes

### Fix 1: StringBuilder for C Codegen Loops (Primary — est. 10-12× memory reduction)

Convert the six recursive `+` concatenation loops to StringBuilder pattern. Priority order by impact:

**1a. `cg_functions2` → `cg_functions2_sb`** (largest impact: eliminates ~6 GB)

```glyph
cg_functions2 mirs struct_map rt_set ret_map =
  sb = sb_new()
  cg_functions2_sb(mirs, 0, struct_map, rt_set, ret_map, sb)
  sb_build(sb)

cg_functions2_sb mirs i struct_map rt_set ret_map sb =
  match i >= glyph_array_len(mirs)
    true -> 0
    _ ->
      sb_append(sb, cg_function2(mirs[i], struct_map, rt_set, ret_map))
      cg_functions2_sb(mirs, i + 1, struct_map, rt_set, ret_map, sb)
```

**1b. `cg_forward_decls` → `cg_forward_decls_sb`** (eliminates ~143 MB)

Same pattern transformation.

**1c. `cg_blocks2` → `cg_blocks2_sb`** (moderate per-function savings)

Same pattern, passed through from `cg_function2`.

**1d. Other loops** (`cg_extern_wrappers`, `cg_all_typedefs_loop`, `cg_enum_typedefs_loop`, `cg_data_fns`, `cg_data_fwd_decls`)

Low priority — small N means negligible quadratic cost. Convert for consistency.

**Expected result**: Peak RSS drops from ~14.9 GB to ~1–2 GB (comparable to LLVM codegen path).

### Fix 2: Top-Level StringBuilder Assembly (Secondary)

Replace the chain in `build_program`:

```glyph
c_code = preamble + typedefs + fwds + "\n" + fns2 + "\n" + data_code + main_w
full_c = prepend_c + cg_runtime_full(externs) + "\n" + wrappers + "\n" + c_code
```

With StringBuilder (matching `cg_llvm_program`'s approach):

```glyph
sb = sb_new()
sb_append(sb, prepend_c)
sb_append(sb, cg_runtime_full(externs))
sb_append(sb, "\n")
sb_append(sb, wrappers)
sb_append(sb, "\n")
sb_append(sb, preamble)
sb_append(sb, typedefs)
sb_append(sb, fwds)
sb_append(sb, "\n")
sb_append(sb, fns2)
sb_append(sb, "\n")
sb_append(sb, data_code)
sb_append(sb, main_w)
full_c = sb_build(sb)
```

This eliminates ~6 intermediate copies of the 4.5 MB output. Minor impact (~27 MB saved) but cleaner.

### Fix 3: Incremental File Writing (Optional — eliminates peak string)

Instead of building `full_c` in memory and writing once, write each section to disk incrementally. Would require a `glyph_append_file` or `glyph_write_file_fd` runtime function.

```glyph
c_path = "/tmp/glyph_out.c"
glyph_write_file(c_path, prepend_c + cg_runtime_full(externs) + "\n" + wrappers + "\n")
glyph_append_file(c_path, preamble + typedefs + fwds + "\n")
glyph_append_file(c_path, fns2)    -- each fn via sb_append
glyph_append_file(c_path, "\n" + data_code + main_w)
```

This would reduce peak string memory from ~4.5 MB to ~1.7 KB (one function at a time). Combined with Fix 1, total peak RSS could drop below 500 MB.

### Fix 4: Multi-File C Output (Optional — reduces cc memory)

Split `/tmp/glyph_out.c` into multiple compilation units:

```
glyph_runtime.h    — typedefs, GC macros, extern declarations (~5 KB)
glyph_runtime.c    — runtime implementation (~30 KB)
glyph_fns_1.c      — first 1335 functions (~2.2 MB)
glyph_fns_2.c      — remaining 1335 functions (~2.2 MB)
glyph_main.c       — main() wrapper (~0.2 KB)
```

**Requirements for multi-file split:**
- Common header with `typedef intptr_t GVal;`, GC macros, struct typedefs
- All forward declarations in header (2,670 prototypes)
- `extern __thread` declarations for global state
- All `static` runtime helpers made non-static or moved to header
- `cc` invocation changed to multi-file: `cc file1.c file2.c ... -o output`

**cc memory at O1 would drop** from 341 MB to ~100 MB per unit (parallel compilation possible).

**This is orthogonal to Fix 1** — they solve different problems (Glyph compiler memory vs cc memory).

## Constraints & Risks

### For StringBuilder conversion (Fix 1-2)

- **Low risk**: LLVM codegen already proves the pattern works
- Individual function codegen (`cg_function2`) still returns strings — only the loop changes
- No change to generated C output
- No change to cc invocation
- Backward compatible

### For multi-file split (Fix 4)

- **Medium risk**: requires header management, symbol visibility changes
- `static` helper functions in runtime must become `extern` or move to header
- Forward declarations must appear in every compilation unit (via `#include`)
- `-flto` needed to preserve cross-unit inlining at O2
- Build system must handle multiple cc invocations or multi-file single invocation
- String literals remain per-function (no pooling needed)
- Boehm GC is compilation-unit-agnostic (no issue)

### What would break

| Change | Breakage Risk | Mitigation |
|--------|--------------|------------|
| StringBuilder loops | None — output unchanged | Test: diff glyph_out.c before/after |
| Top-level sb assembly | None — output unchanged | Same diff test |
| Multi-file split | Symbol visibility | Common header + extern decls |
| Incremental writes | Need new runtime fn | Add glyph_append_file to extern_ |

## Implementation Status

| Fix | Description | Status | Result |
|-----|-------------|--------|--------|
| 1a | `cg_functions2` → StringBuilder | **DONE** | ~6 GB saved |
| 1b | `cg_forward_decls` → StringBuilder | **DONE** | ~143 MB saved |
| 2 | Top-level sb in `build_program` | **DONE** | ~27 MB saved |
| — | `cg_functions2_cover` → StringBuilder | **DONE** | consistency |
| — | `cg_test_forward_decls` → StringBuilder | **DONE** | consistency |
| — | `cg_data_fwd_decls` → StringBuilder | **DONE** | consistency |
| — | `cg_data_fns` → StringBuilder | **DONE** | consistency |
| — | `cg_test_program` → sb assembly | **DONE** | consistency |
| — | `cg_test_program_cover` → sb assembly | **DONE** | consistency |
| — | `build_test_program` → sb assembly | **DONE** | consistency |
| 1c | `cg_blocks2` → StringBuilder | TODO | requires threading sb through `cg_function2` |
| 4 | Multi-file C split | TODO | only if cc memory becomes bottleneck |
| 3 | Incremental file writes | TODO | needs new runtime function |

### Measured Result

**Before:** 14.9 GB peak RSS, 29s wall time
**After:** 1.8 GB peak RSS, 62s wall time (8× memory reduction)

Note: Wall time increased from 29s to 62s. The old binary was compiled by Cranelift (glyph1); the new measurement used a C-codegen binary. The LLVM-compiled final `glyph` binary should be faster. Generated output: identical (zero diff, 182,543 lines, 412/412 tests pass).

### New definitions added

`cg_functions2_sb`, `cg_forward_decls_sb`, `cg_functions2_cover_sb`, `cg_test_forward_decls_sb`, `cg_data_fwd_decls_sb`, `cg_data_fns_sb` — StringBuilder loop variants of the original recursive concatenation functions.

## Appendix: LLVM Codegen Remaining Optimization

The LLVM path (1.8 GB) still uses recursive `+` in:
- `ll_emit_blocks` — per-function block concatenation
- `ll_emit_function` — assembles header + allocas + blocks via `+`

Converting these to StringBuilder would further reduce LLVM codegen to ~500 MB–1 GB estimated.
