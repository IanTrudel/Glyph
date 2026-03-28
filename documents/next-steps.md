# Next Steps for Glyph

## Current State

- **1,662 definitions** (1,317 fn + 332 test + 13 type), ~53k tokens
- **7 libraries** shipping (async, gtk, json, network, scan, web, x11) + stdlib
- **37 monomorphization defs** — the mono pass exists but gen=2 has **0 definitions** currently
- **332 tests** (all gen=1), heaviest coverage in test/typeck/parser/mir/lower
- Recent work: multi-line lambdas, `:=` removal, immutability design docs, data-array model exploration

## Recommendations (roughly priority order)

### 1. Implement `generate` / `accumulate` array constructors
You've already designed this (the Data.Array model analysis from earlier today). `generate(n, f)` eliminates accumulator loops for ~60% of array operations and plays directly into the immutability story. Multi-line lambdas just shipped, making the syntax clean. This is the highest-leverage next step — it's a small runtime addition (`glyph_array_generate`) that immediately makes functional-style array code idiomatic.

### 2. Stdlib expansion
`stdlib.glyph` has only 34 functions. With the library system (`glyph use`/`glyph link`) now working, bulking up stdlib with common operations (map, filter, fold, zip, sort, find, any/all, string utilities like split/join/trim/contains) would make writing example programs much less boilerplate-heavy. Every example currently reinvents these.

### 3. Freeze-bit for arrays (Phase 1 of immutability)
The design docs are written. A single-bit flag on array headers (`frozen`) that makes `array_set`/`array_push` trap on frozen arrays would be a minimal, non-breaking change that enables the immutability migration incrementally. Array literals could freeze by default; explicit `thaw` for mutation.

### 4. Clean up large definitions
The top 5 functions are 400-587 tokens each (`tok_one2`, `tok_loop`, `parse_match_arms`, `tok_one3`, `lower_or_subs`). Some of these (especially the tokenizer chain) could benefit from the guard-based dispatch refactoring that was already applied to ~45 other functions. This is maintenance, not feature work, but it reduces token cost for every future LLM interaction with these defs.

### 5. Gen-2 monomorphization revival
There are 37 `mono_*` functions but 0 gen=2 definitions currently. If monomorphized codegen was previously working (memory mentions gen=2 struct codegen was complete), something has regressed or been removed. Reviving this would unlock `typedef struct` codegen for user-defined types — important for performance and C interop.

### 6. Error handling / Result type in practice
The runtime has `ok`/`err`/`try_read_file`/`try_write_file` but there's no `?` propagation in the self-hosted compiler pipeline. Making the error propagation operator work end-to-end would be a significant usability win, especially for the web/network examples where error handling is real.

---

## Deep-Dive Recommendations (from compiler internals analysis)

### 7. Constant Folding / Dead Code Elimination pass
The compiler has **zero optimization passes** beyond TCO. The TCO pass (`tco_optimize`) is clean and well-structured (11 functions, pattern-matches on MIR blocks). Adding a constant folding pass in the same style would be straightforward — the MIR already separates `ok_const_int`, `ok_const_bool`, `ok_const_str` operands from locals, so recognizing `binop(const, const)` and folding at compile time is mechanical. A DCE pass that removes unused locals after folding would compound the benefit. This directly reduces generated C code size and runtime work.

### 8. String dispatch tables to replace `glyph_str_eq` chains
The `is_runtime_fn` chain (6 functions, ~90 str_eq comparisons) and `nfp2`→`nfp7` namespace prefix chain (another 30+ str_eq calls) are the single most expensive dispatch patterns in the compiler. Every function call and every definition insertion walks these chains linearly. The hashmap runtime (`cg_runtime_map`) already exists and works — using `hm_new`/`hm_set`/`hm_has` to build a lookup table at startup and replacing these chains with `hm_get` would be a measurable speedup on large programs, and would shrink those 6+6 chain functions down to 2 (one init, one lookup).

### 9. `binop_type` and `lower_binop` should be table-driven
`binop_type` (334 tokens) is a 13-deep nested match on integer equality. `lower_binop` (213 tokens) + `lower_binop2` (95 tokens) is the same pattern again. Both map `op_X()` → `mir_Y()` or `op_X()` → type. These are pure lookup tables disguised as code. An array-indexed approach (`result = table[op]`) would collapse ~640 tokens of match chains into ~30 tokens of array construction + 1 index. This also makes adding new operators trivial instead of requiring edits to 3 separate match chains.

### 10. `unify_tags` type coercion is too permissive
`unify_tags` treats `Int`, `Bool`, and `Void` as interchangeable ("int-like") — if both sides are any of these three, unification silently succeeds. This means `Bool` unifies with `Void`, `Void` unifies with `Int`, etc. This was likely expedient for the GVal representation (everything is i64) but it masks real type errors. Tightening this to only coerce `Bool ↔ Int` (which the runtime actually supports) while rejecting `Void` mismatches would catch bugs earlier without breaking working code.

### 11. Lambda lifting boilerplate extraction
`lower_compose`, `lower_field_accessor`, and `wrap_fn_as_closure` each independently build a synthetic lambda MIR from scratch — allocating a lowering context, creating entry block, binding `__env`/`__x` params, emitting body, constructing the MIR record, collecting nested lifts. The three functions share ~70% identical scaffolding. Extracting a `mk_synthetic_lambda(ctx, body_emitter)` helper that handles the boilerplate and takes a callback/closure for the body-specific part would eliminate ~200 tokens of duplication and make adding new synthetic lambdas (e.g., for `generate`) trivial.

### 12. Missing `register_builtins` entries
The runtime has functions that `is_runtime_fn` recognizes but `register_builtins` doesn't register with the type checker: `array_pop`, `str_to_float`, `float_to_str`, `int_to_float`, `float_to_int`, `str_index_of`, `str_starts_with`, `str_ends_with`, `str_trim`, `str_to_upper`, `str_split`, `str_from_code`, `array_reverse`, `array_slice`, `array_index_of`. These functions compile and link fine (the C runtime defines them), but the type checker can't infer their types, meaning any code using them gets type warnings. Adding their signatures to `register_builtins` is mechanical and would clean up type inference for all user programs using these functions.

### 13. Build artifact inspection (`--emit-c`)
Generated C goes to `/tmp/glyph_out.c` unconditionally. There's `--emit-mir` for MIR debugging but no way to inspect the generated C without fishing in `/tmp`. Adding `--emit-c` (or `--emit=c`) that writes the C to a named file (or stdout) would make debugging codegen issues far easier. Similarly, the LLVM path writes to `/tmp/glyph_out.ll`. Both should respect an output path.

### 14. 672 zero-token definitions
Nearly half of all functions (672/1,317) report 0 tokens despite having real bodies (e.g., `lower_compose` is 30+ lines but shows 0 tokens). The token counter appears broken for definitions modified via certain paths. The `tokens` column drives `dump --budget` and `stat` output. A one-time fix (`UPDATE def SET tokens = glyph_tokens(body) WHERE tokens = 0 AND LENGTH(body) > 0`) would restore accurate token accounting.

---

## Additional Recommendations (from /btw analysis)

### ~~15. Fix the `>>` composition operator~~ — VERIFIED WORKING
~~Claimed broken but `test_compose` passes. `lower_compose` is fully implemented in `lower_expr2`.~~

### ~~16. Implement `.field` accessor lowering~~ — VERIFIED WORKING
~~`lower_field_accessor` is fully implemented — generates synthetic lambda with field access.~~

### 17. Four versions of `walk_free_vars` exist (walk_free_vars, 2, 3, 4)
This looks like accumulated copy-paste. Consolidating into one parameterized version would cut ~500 tokens of near-duplicate code and reduce future bug surface.

### 18. Codegen has ~10+ empty stub functions (0 tokens)
`cg_closure_stmt`, `cg_closure_stores`, `cg_aggregate_stmt2`, `cg_stmt2`, `cg_needs_dummy_arg`, etc. — these are either dead or unfinished. Cleaning them out or completing them would clarify what's actually implemented.

### 19. The only optimization pass is TCO — no constant folding, no DCE, no inlining
Adding even basic constant folding (evaluate `3 + 4` at compile time) would be a straightforward MIR pass and meaningfully reduce generated C code size.

### 20. Type checker errors have no source locations
`tc_err` just prints `[tc_err] function_name` with no line/column. The parser already has `format_diagnostic` with caret pointing — threading source positions through type inference would dramatically improve the developer experience.

### 21. C runtime has string functions that aren't exposed to Glyph
Functions like `split`, `trim`, `starts_with`, `ends_with`, `index_of` may already exist in the C runtime but aren't registered in `is_runtime_fn` or `extern_`. Quick wins if they're already written.

### 22. `unify_tags` has 17 nested match expressions
It's the most complex function in the type checker. Refactoring it into a dispatch table or splitting by type-tag pairs would make it much more maintainable and cheaper for LLMs to reason about.

### 23. Build artifacts go to `/tmp/glyph_out.*` with no way to inspect them
A `--keep-intermediates` or `--emit-c` flag to write generated C to a user-specified path would make debugging codegen issues much easier.

### 24. 120+ manual `_loop` helper functions in the compiler itself
The compiler doesn't use its own stdlib. Having the compiler `glyph use stdlib.glyph` and replace manual loop helpers with `map`/`filter`/`fold` would be a strong dogfooding signal and cut significant token bloat.

---

**Top picks for highest novelty + impact:** #19 (constant folding) and #20 (type error locations) are the meatiest new feature work. #12 (register_builtins) and #14 (zero-token fix) are quick wins.
