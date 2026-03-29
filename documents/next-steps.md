# Next Steps for Glyph

## Current State

- **1,728 definitions** (1,353 fn + 360 test + 13 type + 2 const), ~184k tokens
- **9 libraries** shipping (async, gtk, json, network, scan, stdlib, thread, web, x11)
- **stdlib.glyph**: 65 definitions, 1,574 tokens
- **21 example programs** (api, asteroids, benchmark, calculator, countdown, errors, fibonacci, fsm, gbuild, gled, glint, gstats, gtk, gwm, hello, life, pipeline, prolog, sheet, vulkan, web-api)
- **360 self-hosted tests**, 77 Rust tests (6 crates)
- **3-stage bootstrap** working: glyph0 (Rust/Cranelift) → glyph1 → glyph (LLVM)
- **23 externs** in compiler, MCP server with 18 tools
- Recent work: Result type + error propagation, frozen hashmap dispatch, guard-based refactoring, `unify_tags` flattening

## Recommendations

### 1. Structured type error messages
`tc_err` prints only `[tc_err] function_name` with no detail about what went wrong. Since Glyph's audience is LLMs, line/column carets aren't useful — but structured diagnostics are: expected vs actual types, which subexpression or call site triggered the mismatch, and the definition name. E.g. `[tc_err] my_fn: expected I but got S in argument 1 of add_nums`. This would let an LLM fix type errors without re-reading the entire function.

### 2. MIR inlining (optimization unlock)
The compiler has zero optimization passes beyond TCO. Most "constant-like" values come from zero-arg function calls (`op_add()`, `mir_eq()`) which are opaque at the MIR level. Inlining small functions is the key unlock that makes all downstream passes effective. The optimization stack should be built bottom-up:

1. **Inlining** — inline small/zero-arg functions into call sites. Requires: call graph analysis, size heuristics, parameter substitution, recursion detection.
2. **Constant propagation** — replace locals assigned a constant and never reassigned with the constant value.
3. **Constant folding** — evaluate `binop(const, const)` at compile time. MIR already distinguishes `ok_const_int`/`ok_const_bool`/`ok_const_str`.
4. **Branch folding** — when `tm_branch` condition is constant, replace with `tm_goto`.
5. **Dead code elimination** — remove unused locals, unreachable blocks.

### 3. ~~Monomorphization~~ ✅ COMPLETE (v0.5.0)
Full C codegen monomorphization shipped. Six-phase plan executed: typed locals → typed parameters → typed returns → enum typedefs → mono pass → `-w` removal. 23/23 examples build clean, all 463 tests pass. See [monomorphization-lessons-learned.md](monomorphization-lessons-learned.md) for the full postmortem — the hardest phase was the last one (removing `-w` surfaced every latent cast boundary).

### 4. Build artifact inspection (`--emit-c`)
Generated C goes to `/tmp/glyph_out.c` unconditionally; LLVM IR to `/tmp/glyph_out.ll`. There's `--emit-mir` for MIR debugging but no way to inspect generated C without fishing in `/tmp`. Adding `--emit-c` (or `--emit=c`) that writes to a named file or stdout would make debugging codegen far easier.

### ~~5. `generate` / `accumulate` array constructors~~
`generate(n, f)` eliminates accumulator loops for array construction and plays into the immutability story. Multi-line lambdas are shipped, making the syntax clean. Small runtime addition (`glyph_array_generate`) that makes functional-style array code idiomatic.

**Without (accumulator loop pattern):**
```
squares n =
  result = []
  loop i = 0
    match i >= n
      true -> result
      _ ->
        result = array_push(result, i * i)
        loop(i + 1)
```

**With `generate`:**

```
squares n = generate(n, \i -> i * i)
```

**Implementation:**

```
generate n f =
    loop i = 0, acc = []
      match i >= n
        true -> acc
        _ -> loop(i + 1, array_push(acc, f(i)))
```

**More realistic example — building a lookup table:**

Without:
```
ascii_table =
  result = []
  loop i = 0
    match i >= 128
      true -> result
      _ ->
        entry = {code: i, char: chr(i)}
        result = array_push(result, entry)
        loop(i + 1)
```

With:
```
ascii_table = generate(128, \i -> {code: i, char: chr(i)})
```

**And `accumulate` (fold that collects intermediate results):**

Without:
```
running_sum arr =
  result = []
  loop i = 0, total = 0
    match i >= len(arr)
      true -> result
      _ ->
        total = total + arr[i]
        result = array_push(result, total)
        loop(i + 1, total)
```

With:
```
running_sum arr = accumulate(arr, 0, \acc x -> acc + x)
```

**Implementation:**

```
 accumulate arr init f =
    loop i = 0, acc = init, result = []
      match i >= len(arr)
        true -> result
        _ ->
          next = f(acc, arr[i])
          loop(i + 1, next, array_push(result, next))
```

The pattern is everywhere — any time you see `result = []` followed by a loop with `array_push`, that's a `generate` or `map` waiting to happen. `map` already exists in stdlib but `generate` fills the gap where you're building from an index, not transforming an existing array.

### ~~6. Stdlib expansion~~
stdlib.glyph has 40 functions covering collection operations (`map`/`filter`/`fold`/`zip`/`sort`/`join`/`flat_map`/`any`/`all`/`find_index`/`contains`/`each`/`range`/`take`/`drop`/`sum`/`product`/`min`/`max`/`clamp`/`iabs`) plus base64. What's missing:

**Character classification** (reinvented 3+ times across examples):
- `is_digit`, `is_alpha`, `is_space`, `is_upper`, `is_lower`, `is_alnum` — calculator, gstats, sheet, gled all hand-roll these

**String utilities** (reinvented 1-2 times each):
- `pad_left`/`pad_right` — gstats, sheet both build this
- `repeat_str` — sheet, gled (`make_spaces`)
- `split_lines`/`join_lines` — gled builds this, generally useful
- `starts_with`/`ends_with` — api example reinvents this
- `str_replace` — compiler has `str_replace_all` (241 tokens), not in stdlib
- `count_lines` — glint, errors both build this

**Missing collection operations:**
- `reverse` — not in stdlib
- `enumerate` — (index, value) pairs
- `last` — get last element

### ~~7. Scan library expansion~~
scan.glyph is inspired by SNOBOL/Icon-style pattern matching — position-advancing scanners over character sets with success/fail signaling. It has character sets (`cs_digit`/`cs_alpha`/`cs_alnum`/`cs_hex`/`cs_upper`/`cs_lower`/`cs_ws`/`cs_print`) and scanner combinators (`sc_char`/`sc_take`/`sc_take0`/`sc_upto`/`sc_between`/`sc_literal`/`sc_quoted`/`sc_ident`/`sc_int`/`sc_skip_ws`/`sc_rest`). What's missing:

**Combinator gaps:**
- `sc_sep_by` — parse items separated by delimiter (CSV, argument lists). Currently requires manual looping with `sc_upto` + position arithmetic.
- `sc_map` — transform a scanner's result value (e.g. `sc_map(sc_int, \x -> x * 2)`)
- `sc_or` — try first scanner, fall back to second on `None`
- `sc_seq` — sequence two scanners, combine results
- `sc_optional` — try scanner, return default on `None`
- `sc_float` — parse float literals (currently only `sc_int`)
- `sc_line` — scan to next newline

**Character set gaps:**
- `cs_punct` — punctuation characters
- `cs_not` / complement shorthand — `scan_cset_compl` exists as runtime but no `cs_` convenience wrapper

### 8. Generational language evolution
Generations (`gen` column) form a bootstrapping ladder: gen=1 is compilable by glyph0 (Rust), gen=2 can use features only gen=1+ understands, gen=3 needs gen=2+, etc. A higher-gen definition overrides a lower-gen one of the same name when building with `--gen=N`. Currently the entire compiler is gen=1. Higher generations would let the self-hosted compiler adopt newer Glyph features (Result types, guards, or-patterns) that glyph0 can't compile, decoupling language evolution from the Rust bootstrap.

### ~~9. LLVM backend monomorphization~~
The C backend has full monomorphization (v0.5.0), but the LLVM backend (`--emit=llvm`) still uses untyped `i64` for everything. Porting typed signatures would let LLVM optimize struct access via GEP instead of opaque pointer arithmetic. The [lessons learned doc](monomorphization-lessons-learned.md) covers LLVM-specific predictions (SSA typed locals, phi node reconciliation, GEP field indices). Start with typed locals only — LLVM's strict type checking surfaces problems immediately, unlike C where issues hid until `-w` was removed.

### 10. Pedantic-clean C codegen
`cc -pedantic -Wall -Wextra` on generated C produces ~417 warnings (unused labels, unused-but-set variables, unused parameters, misleading indentation). All structural artifacts of MIR→C generation, not bugs. Fixing would mean: eliding unreachable block labels, suppressing dummy `_d` parameters on zero-arg wrappers, multi-line runtime helpers, and DCE for unused locals.
