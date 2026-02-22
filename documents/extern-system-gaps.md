# Extern System Gaps

Discovered 2026-02-22 while building the first real Glyph program (an expression calculator REPL in `examples/calculator/`).

## Summary

The `extern_` table exists to let Glyph programs call C functions, but the self-hosted C codegen doesn't fully use it. Several gaps make it impossible to call common libc functions like `getchar` or `fflush` from a Glyph program.

## Issues Found

### 1. C codegen ignores extern_ `symbol` column (BUG)

The `extern_` table has a `symbol` column (the C-side function name), but the C codegen never reads it. Instead, `cg_call_stmt` → `cg_fn_name` either prefixes with `glyph_` (for runtime functions) or uses the Glyph name verbatim:

```
-- glyph.glyph
cg_fn_name name =
  match is_runtime_fn(name)
    true -> s2("glyph_", name)
    _ -> name     -- ← uses glyph name, ignores extern_ symbol
```

This means declaring `extern getchar → my_wrapper` still generates `getchar(...)` in C, not `my_wrapper(...)`.

### 2. C macro collision with libc (LIMITATION)

Many libc functions (`getchar`, `putchar`, `getc`, `putc`) are implemented as macros in glibc headers. Since the generated C code includes `<stdio.h>`, calling `getchar(0LL)` triggers macro expansion which fails because the macro expects 0 arguments.

Even if we fixed issue #1, wrapping in `(getchar)(0LL)` to suppress macro expansion would still fail because getchar's function signature is `int getchar(void)` — it truly takes 0 args.

### 3. No support for zero-argument externs (LIMITATION)

Glyph's extern system requires at least one argument (`I -> I`, not `-> I`). The self-hosted type checker and lowering assume all function calls have arguments. Callers must pass a dummy `0`:

```
ch = getchar(0)    -- dummy arg, getchar actually takes no args
```

But the C codegen emits `getchar(0LL)`, which is an error because `int getchar(void)` rejects extra arguments.

### 4. No mechanism for user-provided C code (MISSING FEATURE)

There's no way for a `.glyph` program to include custom C code in the generated output. This means:
- No wrapper functions for problematic externs
- No inline C helpers
- No way to bridge the gap between Glyph's uniform `long long` ABI and C's typed signatures

The only C code in the output comes from the compiler's embedded runtime (`cg_runtime_full`).

### 5. No support for extern library linking (INCOMPLETE)

The `extern_` table has a `lib` column, but the build command uses a hardcoded `cc` invocation:

```
cc -w ... /tmp/glyph_out.c -o output -no-pie -lsqlite3
```

Custom `-l` flags from `extern_.lib` are not collected and passed to the linker.

### 6. Modulo operator double-escaped in C codegen (BUG — FIXED)

`cg_binop_str` returned `" %% "` for modulo instead of `" % "`. The `%` character has no special meaning in Glyph strings (only `{` triggers interpolation), so doubling was unnecessary. **Fixed** in this session.

## Impact

These gaps make it impossible to write programs that:
- Read interactive input (getchar, fgetc)
- Call any zero-arg C function
- Use libc functions that are macros
- Link custom C libraries (beyond sqlite3)

This effectively limits Glyph programs to batch processing (file I/O, command-line args) and database operations.

## What Works Today

- Runtime functions (println, str_len, array_push, etc.) — hardcoded in the compiler
- SQLite functions — hardcoded `-lsqlite3` linkage
- The `system()` wrapper — can shell out for anything, but lossy

## Proposed Fixes (see plan)

1. **Use extern_ symbol in codegen** — `cg_fn_name` should look up extern_ entries
2. **Support zero-arg externs** — `V -> I` or `-> I` sigs should emit calls with no arguments
3. **C preamble injection** — let programs embed C helper code (new table or special def kind)
4. **Collect -l flags** — scan extern_ `lib` column and append to cc invocation
5. **Wrap macro-conflicting externs** — generate `(fn_name)()` syntax to suppress C macros, or auto-generate wrappers
