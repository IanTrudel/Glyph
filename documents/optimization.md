# Glyph Optimization Notes

## Benchmark Results (2026-02-23, post-TCO)

Compiled with `-O2`. Self-hosted compiler (gen=1 C codegen) with tail-call optimization.

| Benchmark | Glyph | C | Ratio | Pre-TCO | Speedup |
|-----------|-------|---|-------|---------|---------|
| fib(35) | 61.5 ms | 23.1 ms | 2.7x | 61.6 ms | 1.0x |
| sieve(1M) | 42.3 ms | 11.9 ms | 3.6x | 61.2 ms | 1.4x |
| array_push(1M) | 3.7 ms | 4.1 ms | 0.9x | 16.4 ms | **4.4x** |
| array_sum(1M) | 0.7 ms | 0.5 ms | 1.3x | 9.9 ms | **14x** |
| str_concat(10k) | 15.7 ms | 1.7 ms | 9.4x | 17.0 ms | 1.1x |
| str_builder(100k) | 2.8 ms | 0.04 ms | 63x | 4.3 ms | 1.5x |

TCO eliminated stack overflow for push_loop, sum_loop, concat_loop, sb_loop, sieve_init.
sieve_mark still requires `ulimit -s unlimited` (non-tail-recursive: `+` after call).

## Root Causes

### Function call overhead per "loop iteration"
Glyph has no while/for loops — every loop is recursion. Each iteration pays: save registers, push call stack tracker, update `_glyph_current_fn`, allocate locals. A C `for` loop is a single `jmp`.

### Every array/string op is a runtime function call
`arr[i]` compiles to `glyph_array_bounds_check(hdr, i)` + double pointer indirection through the header. In C it's one instruction. The 21x array_sum gap is 1M iterations of this overhead. Similarly, `sb_append` is a function call per character vs an inlined buffer write.

### Debug bookkeeping on every call
Three stores per function entry for crash diagnostics (`_glyph_current_fn`, `_glyph_call_stack`, `_glyph_call_depth`). In fib(35) with 18M calls, that's 54M extra stores.

### Copy chains in generated MIR
Lots of `_4 = _3; _5 = _4;` patterns that waste locals and make the C compiler's register allocator work harder.

## Proposed Optimization Passes (MIR-level)

### 1. Tail-Call Elimination — IMPLEMENTED
**Pattern:** Block ends with `call self(args...)` → `assign result` → `goto return_block`.
**Transform:** Rewrite params via temps + `goto bb_0`.
**11 new definitions** in glyph.glyph: `tco_optimize`, `tco_opt_mirs`, `tco_opt_fn`, `tco_opt_blks`, `tco_is_ret_blk`, `tco_transform`, `tco_alloc_temps`, `tco_build_stmts`, `tco_copy_stmts`, `tco_emit_temps`, `tco_emit_params`.
**Result:** 4.4x on array_push, 14x on array_sum. array_push now matches C. Limitation: only handles direct tail calls (not intermediate-block patterns like sieve_count).

### 2. Inline Runtime Intrinsics (codegen-level)
Instead of emitting `call glyph_array_len`, emit the field access directly in C.
- `array_len(arr)` → `((long long*)hdr)[1]`
- `array_set(arr, i, v)` → bounds check + `((long long*)((long long*)hdr)[0])[i] = v`
- `str_len(s)` → `*(long long*)((char*)s + 8)`
**Impact:** Eliminates function call overhead for the most common operations. Would significantly reduce array_sum and sieve ratios.

### 3. Dead Store / Copy Propagation
Propagate uses through trivial `_x = _y` copies. Reduce local count.
**Impact:** Modest but helps C compiler optimize better.

### 4. Debug Stripping (release mode)
Make call stack tracking conditional on `--debug` flag. Skip `_glyph_current_fn` / `_glyph_call_stack` updates in release builds.
**Impact:** ~3 stores eliminated per function call. Significant for tight recursive code.

## Implementation Notes

- All MIR passes would be written in Glyph (self-hosted compiler)
- MIR is already a flat CFG with basic blocks, statements, terminators — standard compiler IR
- The `long long` everywhere is gen=1 legacy; gen=2 already uses `typedef struct`
- Optimization pass order: TCO first (biggest win), then intrinsic inlining, then copy propagation
