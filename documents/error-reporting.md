# Error Reporting Improvements

Some of the assessment's suggestions have already been partially addressed (SIGSEGV handler with stack traces, `_glyph_current_fn` in release mode, `tm_unreachable` trap, bounds checks). Here's what would still make a meaningful difference, ordered by impact/effort:

## 1. Source locations in panic messages (high impact, moderate effort)

The compiler has token spans (line/col) during parsing but discards them before codegen. Threading source locations through MIR into the generated C would let every `panic()`, `tm_unreachable`, bounds check, and `?`/`!` failure print `"error at foo.glyph:42:5"` instead of just `"panic in some_fn"`. Implementation: add a `loc` field to MIR statements, emit `_glyph_current_loc = "fn_name:line:col"` at key points in generated C.

## 2. Match exhaustiveness warnings at compile time (high impact, moderate effort)

Currently non-exhaustive matches silently emit `tm_unreachable` which traps at runtime. A compile-time check could warn when a match doesn't cover all cases — especially for enums where the variant set is known. This catches bugs before they become runtime traps.

## 3. Stack traces in release mode (medium impact, low effort)

Stack traces are currently `#ifdef GLYPH_DEBUG` only. Since `_glyph_current_fn` is already tracked in release mode, you could maintain a lightweight shadow call stack (push/pop function names on entry/exit) and print it on any error. Cost: one array push/pop per function call — negligible for most programs.

## 4. Richer assert/unwrap failure messages -- DONE

**Implemented 2026-03-25.** Three fixes:

1. **`assert_str_eq` shows actual strings**: `"foo" != "bar"` instead of `"strings differ"` (`cg_test_runtime`)
2. **Option unwrap correctness**: Swapped None/Some discriminants (None=1, Some=0) so tag 0 = "has value" consistently. Fixed both `!` and `?` for Option types. Previously `None!` silently returned garbage and `Some(x)!` panicked. (`variant_discriminant`, `lower_unwrap`, `lower_propagate`)
3. **Err payload in unwrap panic**: `Err("connection refused")!` now prints `"panic: unwrap failed: connection refused (in fn_name)"` instead of generic `"panic: unwrap failed in fn_name"`. Uses heuristic payload detection in `glyph_panic_unwrap` runtime function. (`cg_runtime_result`, `lower_unwrap`, `is_runtime_fn6`)

New definitions: `scan_args_for_str`, `glyph_panic_unwrap` (C runtime). Modified: `cg_test_runtime`, `variant_discriminant`, `lower_unwrap`, `cg_runtime_result`, `is_runtime_fn6`, `scan_stmts_for_str`, `test_variant_disc`. New tests: `test_unwrap_some`, `test_unwrap_ok`, `test_option_match`, `test_result_match`. 307/307 tests pass.

## 5. Division by zero / arithmetic overflow (low effort)

The SIGFPE handler exists but could be enhanced. For division, codegen could emit an explicit check before `%` and `/` with a message like `"division by zero in fn_name"` rather than relying on the signal handler.
