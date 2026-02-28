# Inline Runtime Intrinsics + Release Mode

## Context

Per `documents/optimization.md`, Glyph's biggest performance gaps vs C are:
- **array_sum 21x slower**: every `array_len`/index is a runtime function call
- **fib 2.7x slower**: 54M extra stores from debug tracking across 18M calls
- **str_builder 63x slower**: function call overhead per append

Two optimizations address these: **inline runtime intrinsics** (codegen-level, #2 in optimization.md) and **debug stripping via --release** (#4, inverted to `--release` since release is rarer → fewer tokens).

## Part 1: Inline Runtime Intrinsics

### Approach

Intercept in `cg_call_stmt` (gen=1, handles all calls including from gen=2). Before the generic `s6(...)` emit, check `callee.ostr` against intrinsic names and emit inline C instead.

### Intrinsics to inline

| Function | Current C | Inline C |
|----------|-----------|----------|
| `array_len(arr)` | `glyph_array_len(_N)` | `((long long*)_N)[1]` |
| `str_len(s)` | `glyph_str_len(_N)` | `*(long long*)((char*)_N + 8)` |
| `str_char_at(s, i)` | `glyph_str_char_at(_N, _M)` | `(long long)(unsigned char)(*(const char**)_N)[_M]` |

NOT inlining `array_set` (needs bounds check consideration) or `array_push` (too complex for inline).

### New definitions (~2)

- **`cg_try_inline_call stmt`** — checks `stmt.sop1.ostr` against intrinsic names, returns inline C string or `""` if not intrinsic
- **`cg_inline_call callee_name dest_str args`** — dispatches to per-intrinsic emitters

### Modified definitions (~1)

- **`cg_call_stmt`** — try inline first, fall through to generic emit if `""` returned:
  ```
  cg_call_stmt stmt =
    inlined = cg_try_inline_call(stmt)
    match glyph_str_len(inlined) > 0
      true -> inlined
      _ -> <existing generic emit code>
  ```

## Part 2: Release Mode (--release flag)

### Approach

Thread a `release` integer (0=debug, 1=release) through the codegen pipeline. In release mode, skip all debug instrumentation. The flag flows:

```
cmd_build → compile_db → build_program → cg_program → cg_function/cg_function2, cg_preamble, cg_main_wrapper
```

Also pass to `cg_term` (return depth decrement), `cg_null_check`, `cg_index_stmt` (bounds check).

### What --release strips

| Category | Location | Release behavior |
|----------|----------|-----------------|
| Function entry tracking | `cg_function`/`cg_function2` (`fntrack`) | `fntrack = ""` |
| Return depth decrement | `cg_term` (`tm_return` case) | Omit `_glyph_call_depth--;` |
| Null pointer checks | `cg_null_check` | Return `""` |
| SIGSEGV handler registration | `cg_main_wrapper` | Omit `signal()` call |
| Array bounds checks | `cg_index_stmt` | Skip `glyph_array_bounds_check()` |
| Runtime debug globals | `cg_runtime_c` | Strip tracking globals/functions |
| Preamble extern decls | `cg_preamble` | Strip debug extern decls |

### New definitions (~3)

- **`cg_preamble_release rel`** — conditionally includes debug extern decls
- **`cg_runtime_c_release rel`** — conditionally includes debug globals/handlers
- **`cg_main_wrapper_release rel fn_name`** — conditionally registers SIGSEGV

### Modified definitions (~12)

Pipeline threading (add `rel` parameter):
- **`cmd_build`** (gen=2) — parse `--release` flag, pass to `compile_db`
- **`compile_db`** (gen=1 + gen=2) — accept `rel`, pass to `build_program`
- **`build_program`** (gen=1 + gen=2) — accept `rel`, pass to `cg_program`
- **`cg_program`** (gen=1 + gen=2) — accept `rel`, pass to sub-functions

Codegen functions (use `rel` to gate debug code):
- **`cg_function`** (gen=1) — conditional `fntrack`
- **`cg_function2`** (gen=2) — conditional `fntrack`
- **`cg_term`** (gen=1) — conditional depth decrement
- **`cg_null_check`** (gen=1) — return `""` in release
- **`cg_index_stmt`** (gen=1) — skip bounds check in release

### cc flag: add `-O2` in release mode

`build_program` already uses `-O1`. In release mode, upgrade to `-O2`.

## Implementation Order

1. **Inline intrinsics** — new `cg_try_inline_call`, modify `cg_call_stmt`
2. **Build & test intrinsics** — `glyph0 build glyph.glyph --full --gen=2`, benchmark
3. **Release mode** — thread `rel` through pipeline, modify codegen functions
4. **Build & test release** — `glyph0 build glyph.glyph --full --gen=2`, verify `--release` works
5. **Benchmark** — re-run array_sum, fib, sieve with both optimizations

## Verification

- `cargo test` — all 68 Rust tests pass (no Rust changes needed)
- Bootstrap: `glyph0 build glyph.glyph --full --gen=2` succeeds
- Test program with array_len: verify inline codegen in `/tmp/glyph_out.c`
- Test `--release` flag: verify no debug tracking in generated C
- Build examples (glint, gstats) with `--release`, verify they run
- Re-run benchmarks from optimization.md, update results
