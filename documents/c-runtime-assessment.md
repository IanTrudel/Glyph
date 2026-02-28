# C Runtime Safety Assessment

**Date:** 2026-02-25
**Scope:** All runtime functions, codegen patterns, and safety infrastructure across both the Rust-compiled (`glyph0`/`glyph1`) and self-hosted C-codegen (`glyph`) pipelines.

---

## Executive Summary

The Glyph C runtime uses a uniform `long long` ABI where every value — integers, booleans, pointers, strings, arrays, closures — is a 64-bit integer. Pointers are cast to/from `long long` freely. This makes the FFI simple but eliminates all type-level safety: there is no way at runtime to distinguish an integer from a pointer from a boolean.

The runtime has **array bounds checking** (always-on, the strongest safety guarantee) but most other safety checks are either **debug-only** or **absent entirely**. The self-hosted compiler's runtime (`cg_runtime_c`) is notably less safe than the Rust-side runtime (`RUNTIME_C` in `runtime.rs`), with several functions missing bounds checks that the Rust version has.

**Most common crash pattern:** A Glyph programming error (wrong type, missing match arm, null value) produces no error message — just a SIGSEGV that the debug-mode signal handler catches, or a silent wrong result in release mode.

---

## 1. What Works Well

### Array bounds checking (always-on)
Every array index operation emits a call to `glyph_array_bounds_check(idx, len)` which panics with `"array index N out of bounds (len M)"`. This is the runtime's strongest safety feature. It catches the most common class of bugs immediately with a clear message.

### Debug-mode stack traces
The `GLYPH_DEBUG` infrastructure tracks function entry/exit on a 256-frame call stack. The SIGSEGV handler prints the current function and full stack trace. When a segfault does occur, the developer gets actionable information.

### Debug-mode null checks on field access / array indexing
`_glyph_null_check` is emitted before every field access and array index operation. In debug mode, this catches null pointer dereferences with context like `"field offset 3"` or `"array index"` before they become hardware faults.

### Panic with messages
`glyph_panic(msg)` provides a clear mechanism for runtime errors with context.

---

## 2. Critical Safety Gaps

### 2.1 Self-hosted runtime missing bounds checks

Several functions have bounds checks in the Rust `RUNTIME_C` but are **unchecked in the self-hosted `cg_runtime_c`**:

| Function | Rust version | Self-hosted version |
|---|---|---|
| `glyph_str_char_at` | Returns -1 if `i < 0 \|\| i >= len` | **No check** — reads arbitrary memory at `p[i]` |
| `glyph_str_slice` | Clamps `start` to 0, `end` to `len` | **No check** — `memcpy(d, p+start, end-start)` with unchecked params. Negative length wraps to huge `size_t` → crash or corruption |
| `glyph_array_set` | Panics if `i < 0 \|\| i >= len` | **No check** — writes to `data[i]` unconditionally. Arbitrary write primitive |
| `glyph_array_pop` | Panics if `len <= 0` | **No check** — decrements len to -1, reads `data[-1]` |

These are the most dangerous divergences. A Glyph program that works under `glyph0` (Cranelift + Rust runtime) may corrupt memory silently under `glyph` (C-codegen + self-hosted runtime).

**Recommendation:** Port the bounds checks from the Rust runtime to the self-hosted runtime. These are 1-3 lines each:

```c
// str_char_at: add before the return
if (i < 0 || i >= *(long long*)((char*)s + 8)) return -1;

// str_slice: add clamping
if (start < 0) start = 0;
long long slen = *(long long*)((char*)s + 8);
if (end > slen) end = slen;
if (end < start) end = start;

// array_set: add bounds check
glyph_array_bounds_check(i, h[1]);

// array_pop: add empty check
if (h[1] <= 0) glyph_panic("pop on empty array");
```

**Priority: HIGH** — These are straightforward fixes with zero performance cost in practice.

### 2.2 No null checks on string function inputs

Every string runtime function (`glyph_str_eq`, `glyph_str_len`, `glyph_str_concat`, `glyph_println`, etc.) dereferences its pointer argument immediately with no null check. A null string value causes an immediate segfault inside the runtime function, with a confusing stack trace pointing to the runtime rather than the user's code.

Common scenario:
```
# Glyph code that produces null:
x = read_file("nonexistent.txt")   # returns {ptr=NULL, len=-1}
println(x)                          # segfault in glyph_println
```

**Recommendation (phased):**
- **Phase 1:** Add null checks to the 4 most-called string functions: `glyph_str_eq`, `glyph_str_len`, `glyph_println`, `glyph_str_concat`. On null, either panic with a clear message or return a safe default (empty string, length 0, false).
- **Phase 2:** Consider a convention where null string pointer = empty string (like `glyph_cstr_to_str` already does for NULL input).

### 2.3 Release mode has zero safety diagnostics

In the self-hosted runtime, ALL safety infrastructure is guarded by `#ifdef GLYPH_DEBUG`:
- Null checks
- Stack trace tracking
- SIGSEGV handler
- Function name tracking

A release-mode binary (`--release`) has **no null checks, no stack traces, no signal handler**. A segfault produces the kernel's default message (`Segmentation fault`) with no context.

In contrast, the Rust `RUNTIME_C` always installs the SIGSEGV handler (not debug-guarded).

**Recommendation:**
- Always install the SIGSEGV handler, even in release mode. The cost is a single `signal()` call at startup.
- Consider keeping `_glyph_current_fn` tracking in release mode (1 global write per function call) for minimal crash context. The full call stack can remain debug-only.

### 2.4 Non-exhaustive match produces silent wrong behavior

When a `match` expression exhausts all arms without matching, the self-hosted MIR lowering leaves the block unterminated. The generated C code simply falls through to the next block in sequence, producing garbage results silently.

The Rust MIR lowering correctly emits `Terminator::Unreachable` → `__builtin_trap()` for this case.

Example:
```
classify x =
  match x
    1 -> "one"
    2 -> "two"
# If x is 3, behavior is undefined — falls through to whatever code follows
```

**Recommendation:** In `lower_match_arms`, when `i >= array_len(arms)` and no wildcard was seen, emit `mir_terminate(ctx, mk_term_unreachable())` instead of returning 0. This makes non-exhaustive matches trap immediately with a clear error. Low-effort, high-impact fix.

---

## 3. Moderate Safety Gaps

### 3.1 No type safety at the C level

The uniform `long long` ABI means:
- An integer passed where a string pointer is expected → reads address 42 as a string struct → segfault or corruption
- A string passed where an integer is expected → pointer value used in arithmetic → wrong result
- A record passed to a function expecting a different record → wrong field offsets → garbage data

The compiler suppresses all C warnings with `-Wno-int-conversion -Wno-incompatible-pointer-types`.

This is a fundamental design choice (simplicity over safety), not a bug. But it means **every type error is a potential segfault**, not a compile-time error.

**Recommendation (long-term):** Consider a lightweight tagging scheme for debug mode:
- Reserve the low 3 bits of `long long` as a type tag (0=int, 1=ptr, 2=bool, etc.)
- Check tags at function boundaries and before dereferences
- Strip tags before use
- Only in debug mode; zero-cost in release

This is a significant effort but would catch the most common class of bugs (type mismatches between function arguments).

### 3.2 String operator detection is heuristic

The MIR lowering detects string operations by checking if either operand is known to be a string (literal, or local with string type in `local_types`). When both operands are unknown type (e.g., function parameters), `+` generates integer addition instead of `str_concat`, and `==` generates integer comparison instead of `str_eq`.

```
# This works:
x = "hello" + " world"       # "hello" is a literal → str_concat

# This might not work:
combine a b = a + b           # both params unknown → integer addition
```

**Recommendation:** The type checker integration (Phase C.4, currently advisory-only via `glyph check`) would solve this. When the type checker is stable enough for the build pipeline, string operation detection becomes exact.

### 3.3 `glyph_str_concat` has no integer overflow check

```c
long long tl = a_len + b_len;
char* r = (char*)malloc(16 + tl);
memcpy(r + 16, a_ptr, a_len);
memcpy(r + 16 + a_len, b_ptr, b_len);
```

If `a_len + b_len` overflows `long long` (two strings each > 4 exabytes — practically impossible), the malloc size wraps and the memcpy overflows. Theoretical, but worth noting.

### 3.4 `glyph_read_file` doesn't check `fread` return

```c
fread(buf, 1, (size_t)sz, f);
```

If the file changes size between `ftell` and `fread` (race condition), or if `fread` returns a short read (disk error), the remaining buffer is uninitialized. The string struct reports the original `ftell` size, so reading includes garbage bytes.

**Recommendation:** Check `fread` return value, set `len` to actual bytes read.

### 3.5 No division-by-zero protection

Integer division `a / b` generates `_2 = _0 / _1;` in C. Division by zero is undefined behavior in C — it may trap (SIGFPE) or may not. The SIGSEGV handler doesn't catch SIGFPE.

**Recommendation:** Either:
- Add a `glyph_div(a, b)` runtime function that checks for zero
- Or install a SIGFPE handler alongside the SIGSEGV handler

### 3.6 Memory is never freed

All allocations (strings, arrays, records, variants) are heap-allocated and never freed. This is fine for short-lived programs but problematic for long-running ones (like the MCP server).

The MCP server processes JSON requests in a loop. Each request allocates a JNode pool, strings, result builders, etc. None are freed. Over time, memory grows unboundedly.

**Recommendation (long-term):** Consider arena allocation per request — allocate from a bump allocator, free the entire arena after each response. This is much simpler than GC and fits the request/response model well.

---

## 4. Minor Issues

### 4.1 `glyph_read_line` uses 64KB stack buffer
A 65536-byte stack allocation is large. Deep call stacks might overflow. Consider heap allocation or a smaller initial buffer with dynamic growth.

### 4.2 `glyph_args()` leaks on every call
Each call allocates a fresh array of string structs. Called once in practice, but a loop calling `args()` would leak.

### 4.3 `glyph_cstr_to_str(NULL)` returns inconsistent string
Returns `{ptr=<string_literal>, len=0}` where ptr points to a string literal `""`, not heap memory. All other strings have ptr pointing into a heap allocation. If anyone tries to free this string, it's UB.

### 4.4 `glyph_sb_build` use-after-free potential
`sb_build` frees the builder header and old buffer. If the builder is used after `sb_build`, it's a use-after-free. No protection against this.

### 4.5 Closure codegen not implemented in self-hosted compiler
`rv_make_closure` in `cg_stmt` hits the unknown rvalue case and generates a comment. Programs using closures must be compiled by the Cranelift backend.

### 4.6 SIGSEGV handler stack depth limit
The call stack is limited to 256 frames. In deeply recursive programs (which Glyph encourages due to its functional style), this silently truncates the stack trace.

---

## 5. Prioritized Recommendations

### Immediate (high impact, low effort)

1. **Port bounds checks to self-hosted runtime** — Add bounds checks to `str_char_at`, `str_slice`, `array_set`, `array_pop` matching the Rust versions. ~20 lines of C total.

2. **Always install SIGSEGV handler** — Remove the `#ifdef GLYPH_DEBUG` guard around the signal handler installation in `main()`. One-line change.

3. **Trap on non-exhaustive match** — In `lower_match_arms`, emit `tm_unreachable` when all arms are exhausted without a wildcard. Small MIR lowering change.

4. **Install SIGFPE handler** — Catch division-by-zero with a diagnostic message instead of silent UB.

### Short-term (moderate effort)

5. **Null checks on critical string functions** — Add null input checks to `str_eq`, `str_len`, `println`, `str_concat`. Either panic with context or treat null as empty string. ~30 lines.

6. **Keep `_glyph_current_fn` in release mode** — Remove the `#ifdef GLYPH_DEBUG` guard around function name tracking. Minimal performance cost (1 global write per call), significant diagnostic value on crash.

7. **Check `fread` return value** in `glyph_read_file`.

### Medium-term (significant effort)

8. **Arena allocator for MCP server** — Per-request arena allocation to prevent unbounded memory growth. Requires identifying allocation scope boundaries.

9. **Type checker in build pipeline** — Enable the existing type checker (currently advisory via `glyph check`) during `glyph build`. This would catch type errors before codegen, preventing the entire class of "wrong type → segfault" bugs.

### Long-term (research effort)

10. **Debug-mode type tagging** — Tag low bits of `long long` values with type info. Check at dereference points. Strip before use. Zero-cost in release.

11. **Garbage collection or reference counting** — For long-running programs. Arena allocation covers the MCP case but not general interactive programs.

---

## 6. Divergence Summary: Rust Runtime vs Self-Hosted Runtime

| Aspect | Rust `RUNTIME_C` | Self-hosted `cg_runtime_c` |
|---|---|---|
| `str_char_at` | Bounds-checked | **Unchecked** |
| `str_slice` | Clamped | **Unchecked** |
| `array_set` | Bounds-checked | **Unchecked** |
| `array_pop` | Empty-checked | **Unchecked** |
| `str_concat` malloc | Checked | **Unchecked** |
| `panic` return type | `void` | `long long` |
| `alloc` param type | `unsigned long long` | `long long` |
| `array_push` growth | malloc+memcpy+free | realloc |
| SIGSEGV handler | Always installed | **Debug-only** |
| Stack trace | Not available | Debug-only, 256-frame |
| Null checks in codegen | Not emitted | Debug-only |

The ideal state is parity: the self-hosted runtime should have at least the same safety guarantees as the Rust runtime. Currently the self-hosted runtime is strictly less safe.

---

## Appendix: Runtime Function Inventory

### Core (`cg_runtime_c`)
`glyph_panic`, `glyph_alloc`, `glyph_realloc`, `glyph_dealloc`, `glyph_print`, `glyph_println`, `glyph_eprintln`, `glyph_str_concat`, `glyph_int_to_str`, `glyph_str_to_int`, `glyph_str_eq`, `glyph_str_len`, `glyph_str_char_at`, `glyph_str_slice`, `glyph_str_to_cstr`, `glyph_cstr_to_str`, `glyph_array_bounds_check`, `glyph_array_push`, `glyph_array_len`, `glyph_array_set`, `glyph_array_pop`

### Args (`cg_runtime_args`)
`glyph_set_args`, `glyph_args`

### IO (`cg_runtime_io`)
`glyph_read_file`, `glyph_write_file`, `glyph_system`, `glyph_read_line`, `glyph_flush`, `glyph_exit`

### StringBuilder (`cg_runtime_sb`)
`glyph_sb_new`, `glyph_sb_append`, `glyph_sb_build`

### Result (`cg_runtime_result`)
`glyph_ok`, `glyph_err`, `glyph_try_read_file`, `glyph_try_write_file`

### Raw (`cg_runtime_raw`)
`glyph_raw_set`

### SQLite (`cg_runtime_sqlite`)
`glyph_db_open`, `glyph_db_close`, `glyph_db_exec`, `glyph_db_query_rows`, `glyph_db_query_one`

### Test (`cg_test_runtime`)
`glyph_assert`, `glyph_assert_eq`, `glyph_assert_str_eq`

### Debug infra (embedded in codegen preamble)
`_glyph_current_fn`, `_glyph_call_stack[256]`, `_glyph_call_depth`, `_glyph_sigsegv`, `_glyph_null_check`, `_glyph_print_stack`
