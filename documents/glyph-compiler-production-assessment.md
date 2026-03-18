# Glyph Compiler: Real-World Readiness Assessment

> **Date**: 2026-03-17 (updated 2026-03-17)
> **Status**: Assessment document. Type error gate completed.
> **Data sources**: Live MCP queries to glyph.glyph, Rust crate source, benchmark results,
> existing assessment documents (module-assessment.md, portability-assessment.md).

---

## Executive Summary

Glyph has a working, self-hosted compiler (968 fn / 166 test / 7 type defs, ~85k tokens of
Glyph code) with a complete bootstrap chain, MCP server, coverage tooling, and a C codegen
backend that produces runnable programs. It is a capable research compiler, not yet a
production compiler.

The seven areas where real-world readiness requires the most work, ordered by impact:

| # | Dimension | Gap severity | Effort | Priority |
|---|-----------|-------------|--------|----------|
| 1 | Type system correctness | ~~Type errors advisory-only~~; BUG-006 fixed; gate done | — | P1 ✓ |
| 2 | Language feature completeness | Maps absent (broken); generics absent | M–XL | P2–P3 |
| 3 | Standard library | Collections, strings, hash maps missing | M | P1 |
| 4 | Performance | 3–92× slower than C; known root causes | S–L | P2 |
| 5 | LLM feedback loop | Type errors not in MCP; no build/run tools | M | P2 |
| 6 | Platform support | Linux x86-64 only; macOS 2 changes away | S–M | P3 |
| 7 | Tooling & ecosystem | MCP complete; LSP/fmt/link not started | M | P3–P4 |

**Self-hosted compiler is the target.** All new features go into `glyph.glyph` as new or
modified Glyph definitions. The Rust compiler `glyph0` is bootstrap-only. Any addition that
requires new syntax must be coordinated: add the syntax to `glyph0`'s Rust parser first, then
implement it in `glyph.glyph`.

---

## Current Compiler State

### Definition inventory (MCP-confirmed, 2026-03-17)

```sql
SELECT kind, COUNT(*) FROM def GROUP BY kind;
-- fn: 966   test: 166   type: 7
```

All definitions are `gen=1` (gen=2 was promoted to gen=1 in the Mar 13 consolidation).
Total: 1,139 definitions across 13 logical subsystems (`cg_`, `tc_`, `lower_`, `parse_`,
`tok_`, `mcp_`, `cmd_`, `tco_`, `blt_`, etc.).

### Compilation pipeline

```
.glyph DB
  → read_fn_defs (SELECT kind='fn')
  → parse_all_fns (tokenizer + recursive-descent parser)
  → tc_infer_loop (HM + row polymorphism, all defs)
  → tc_report_errors (gates compilation — exits on errors)
  → compile_fns_parsed (lower → MIR)
  → fix_all_field_offsets (type-registry disambiguation)
  → fix_extern_calls (extern name rewriting)
  → tco_optimize (tail-call → goto transform, 11 defs)
  → cg_program (MIR → C source)
  → cc (system C compiler → native binary)
```

Notable: TCO exists and handles direct tail-recursive functions. All gen=2 struct codegen
(`typedef struct` + `->field` access) is in the main pipeline.

### Benchmark results (RESULTS.md, -O2, self-hosted C codegen)

| Benchmark | Glyph | C | Ratio |
|-----------|-------|---|-------|
| fib(35) | 61.6 ms | 19.6 ms | 3.1× |
| sieve(1M) | 61.2 ms | 7.4 ms | 8.3× |
| array_push(1M) | 16.4 ms | 4.2 ms | 3.9× |
| array_sum(1M) | 9.9 ms | 0.5 ms | 21× |
| str_concat(10k) | 17.0 ms | 1.8 ms | 9.4× |
| str_builder(100k) | 4.3 ms | 0.05 ms | 92× |

---

## 1. Type System Correctness

**Priority: P1 — Effort: S (error gating) + M (traits)**

### Current state

The self-hosted type checker implements Hindley-Milner inference with row polymorphism and
let-polymorphism (`ty_forall`). It has 28 `infer_*` functions covering expressions,
statements, match arms, records, lambdas, pipes, composition, and error propagation.

~~The critical gap is that **type errors do not block compilation**.~~ **Fixed.** `build_program`
now gates on the error count after `tc_report_errors`:

```glyph
eng = mk_engine()
register_builtins(eng)
tc_pre_register(eng, parsed, 0)
tc_results = tc_infer_loop(eng, parsed, 0, [])
tc_report_errors(eng)
match glyph_array_len(eng.errors) > 0
  true -> glyph_exit(1)
  _ -> 0
```

A program with type errors is now rejected before codegen. The gate required resolving
five heterogeneous-array false positives in `ffc_search`, `cg_struct_typedef`, `blt_find_best`,
`emit_capture_loads`, and `cg_all_typedefs_loop` — all cases where a mixed `[S, [S], [S]]`
struct_map entry caused the type checker to unify distinct element types. Fixed by using
`glyph_arr_get_str` (a raw-GVal accessor that does not constrain the array's element type) for
all string-typed element reads from heterogeneous arrays. `glyph_arr_get_str` was also added to
the Rust Cranelift runtime so that `glyph0` and `glyph1` can call it during bootstrap.

**BUG-006 (fixed)**: `lower_str_interp_parts` previously checked `vt == 3` (float) but fell
through to `glyph_int_to_str` for all other types, including strings (`vt == 4`). A string
expression inside `"text {expr}"` got `int_to_str` applied, corrupting the value. Fixed by
adding a `vt == 4` branch that pushes the operand directly without any conversion:

```glyph
match vt == 4
  true ->
    glyph_array_push(acc, val)          -- string: emit directly, no conversion
  _ ->
    conv_fn = match vt == 3
      true -> "glyph_float_to_str"
      _ -> "glyph_int_to_str"
    mir_emit_call(ctx, conv, mk_op_func(conv_fn), [val])
    glyph_array_push(acc, mk_op_local(conv))
```

Regression test: `test_str_interp_str_type` asserts 0 `rv_call` statements in block 0 for
`f u = s = "world" / "hello {s}"` — the string local is passed through without conversion.

**Trait/impl coverage**: `tk_trait` and `tk_impl` token constants and their `keyword_kind`
dispatch arms have been removed. `trait` and `impl` are no longer reserved keywords. No
parser, type-checker, or codegen coverage for traits or impls exists or is planned.

### Gap

- ~~Type errors are printed as warnings; the compiler produces a binary regardless.~~ **Fixed** — `build_program` calls `glyph_exit(1)` when `eng.errors` is non-empty.
- ~~BUG-006: string expressions in interpolation produce garbage output.~~ **Fixed.**
- Traits and impls have been removed entirely — not a planned feature.
- ~~The `<200 defs` threshold disables type checking entirely for large programs.~~ **Fixed** — threshold removed; type inference always runs.

### Implementation path

All changes are in `glyph.glyph`, no glyph0 changes needed for (1) and (2):

1. ~~**Gate type errors** (S): In `build_program`, after `tc_report_errors(eng)`, check
   `glyph_array_len(eng.errors) > 0` and call `glyph_exit(1)`. One line.~~ **Done.**
   Required fixing five heterogeneous-array false positives and adding `glyph_arr_get_str`
   to the Rust bootstrap runtime before the gate could be enabled without false exits.

2. ~~**Fix BUG-006**~~ **Done.** `lower_str_interp_parts` now checks `vt == 4` and pushes the
   operand directly. Regression test: `test_str_interp_str_type`.

3. ~~**Lift the 200-def threshold**~~ **Done.** `glyph check glyph.glyph` confirmed that
   `tc_infer_loop` already runs cleanly on all 965 defs (0 errors, no crashes). The tmap it
   produces contains only resolved type-tag integers (not pool indices), so passing it to MIR
   lowering is safe. The `n_defs < 200` guard in `build_program` was removed — type inference
   now always runs regardless of program size.

**Bootstrap impact**: (1) — `glyph_arr_get_str` added to Rust runtime (done). (3) — none (done).

---

## 2. Language Feature Completeness

**Priority: P1 (maps) — P2 (enums, generics)**

From an LLM-as-user perspective, the meaningful gaps are expressiveness gaps: things an LLM
cannot write that it genuinely needs. Abstractions that exist to make code more readable for
humans — traits as named interfaces, consts as named literals, FSMs as a syntactic pattern —
are not real gaps. An LLM that wants a constant inlines the value or writes a zero-argument
function; an LLM that wants a state machine writes a recursive function with a state
parameter. The features that matter are those with no workaround, or those that are
silently broken:

### Map type `{K:V}` — P1

The map type `{K:V}` is distinct from record literals `{field: expr}` and is entirely
unimplemented. Records (fixed named fields) work end-to-end: `parse_record` → `lower_record`
→ `ag_record` MIR aggregate → `cg_record_aggregate`. Maps are a different concept — a dynamic
key-value store addressable by runtime values — and have no implementation at any layer.

**Literal syntax**: `{{...}}` double-brace distinguishes map literals from record literals
unambiguously — `parse_atom` peeks one token after `{`; a second `{` dispatches to
`parse_map` instead of `parse_record`. Keys are any expression (typically string or int
literals). Access reuses the existing index syntax:

```glyph
m = {{"x": 1, "y": 2, "z": 3}}
v = m["y"]
```

Map literals desugar in MIR lowering to `hm_new()` + `hm_set` calls, the same pattern as
array literals desugaring to `array_new()` + `array_push`. `lower_index` is extended to emit
`hm_get(m, k)` when the type checker identifies the base as a map type. Mutation uses
`hm_set(m, k, v)` directly.

The `{K:V}` notation is retained as a type annotation only, where `K` and `V` are
type-level variables — no collision with record type `{field: T}`.

**Implementation**: Gen=2 definitions only. The parser/lowering/codegen additions
(`parse_map`, `lower_map`, `cg_map_aggregate`) are gen=2 definitions in glyph.glyph written
in normal Glyph syntax — they add handling *for* `{{...}}` without *using* it themselves, so
glyph0 compiles them without needing a Rust parser change. A `cg_runtime_map` definition
contains the C hash map implementation (`hm_new`/`hm_set`/`hm_get`/`hm_del`/`hm_keys`/
`hm_len`/`hm_has`), included conditionally in `cg_runtime_full` (same pattern as
`cg_runtime_sqlite`). Extend the `is_runtime_fn` chain for the new names.

**Effort: M. Bootstrap impact: none.**

### User-defined enums — P2

Enum infrastructure exists in glyph.glyph but only works for six hardcoded built-in
constructors: `None`, `Some`, `Ok`, `Err`, `Left`, `Right`. The discriminant table is
hardcoded in `variant_discriminant`:

```glyph
variant_discriminant name =
  match glyph_str_eq(name, "None")  true -> 0
  _ -> match glyph_str_eq(name, "Some") true -> 1
  ...
  _ -> 0   -- all user-defined constructors silently get discriminant 0
```

The pipeline around it is complete: `lower_call` lowers `Ctor(args)` to an `ag_variant` MIR
aggregate, `cg_variant_aggregate` heap-allocates `{tag, payload...}`, and `lower_match_ctor`
does tag comparison for pattern matching. The problem is purely the hardcoded table.

**Why this matters for LLMs**: an LLM that writes `Status = Pending | Running | Done` and
then matches on it gets no error and silently wrong runtime behavior — all constructors have
discriminant 0, so every match arm after the first is unreachable. This is a trap, not a
missing feature.

A workaround exists (integer constants + match on integers) but it's verbose, loses the
constructor name scoping, and is type-unsafe in ways the type checker won't catch.

**Implementation path**: three changes in glyph.glyph, no glyph0 changes:

1. Add an enum body parser that extracts variant names from `kind='type'` definitions whose
   body uses `|` syntax (e.g., `Color = Red | Green | Blue(I)`).
2. Build a discriminant table from the parsed type defs (variant index = declaration order)
   and thread it through `build_program` alongside `struct_map`.
3. Register constructors in the type environment (analogous to `register_builtins`) so
   `infer_ctor_pattern` can type-check them rather than falling back to a fresh variable.

`variant_discriminant` becomes a lookup into the table rather than a hardcoded chain.
The six built-in constructors become entries in the same table (or a small fixed prelude),
removing the special case entirely.

**Effort: S–M. Bootstrap impact: none.**

### Generics / monomorphization — P2

No monomorphization pass exists:

```sql
SELECT COUNT(*) FROM def WHERE name LIKE 'mono_%';  -- 0
```

Without generics, an LLM must write `sort_int`, `sort_str`, `sort_float` as separate
definitions. At small scale this is manageable — the LLM can just generate the variants — but
it inflates definition counts, hurts token efficiency, and means the type checker cannot
verify that the variants are consistent.

Glyph already has HM inference with `ty_forall` (let-polymorphism). A function like
`sort arr cmp = ...` already gets inferred type `[a] → (a → a → Bool) → [a]`. The missing
piece is a monomorphization pass that acts on what HM inference already produces:

1. Walk `tc_infer_loop` output, find all call sites where a polymorphic function is
   instantiated with concrete types
2. Emit specialized copies under synthesized names (`sort__int`, `sort__str`, etc.)
3. Rewrite call sites to use the concrete versions

This is a post-type-check transform on existing AST/MIR — a `mono_*` definition family
(~30–50 defs) in glyph.glyph. No new syntax, no glyph0 changes, bootstrap chain unaffected.

Explicit type parameter syntax (`type Stack T = {items: [T]}`) is a separate optional feature
layered on top, aimed at human readability. It would require glyph0 parser changes and is not
needed for LLMs to benefit from monomorphization.

**Effort: XL. Bootstrap impact: none.**

### Everything else — not LLM gaps

- **`const` definitions**: An LLM can inline a literal or write `pi = 3.14159` as a
  zero-arg function. The schema supports `kind='const'` but the compiler ignores it. Not a
  real expressiveness gap.
- **Traits and impls**: Removed. `tk_trait`/`tk_impl` constants and `keyword_kind` dispatch
  arms deleted. HM row polymorphism handles dispatch for LLMs; SQL handles discovery
  (`WHERE name LIKE '%_for_MyType'`). Not a planned feature.
- **`fsm`, `srv`, `macro`**: Pattern sugar. State machines are recursive functions with a
  state parameter; services are loops; macros are what LLMs do via `put_def`. None of these
  represent expressiveness gaps for an LLM author.

### Module system

The `module` / `module_member` join tables are schema-only with zero rows (MCP-confirmed) and
should be dropped. They are a human-language concept — modules as a syntactic namespace, with
visibility enforced at call sites. For an LLM-native system that queries definitions via SQL,
this design is strictly worse than a flat column.

The right replacement is an `ns TEXT` column directly on `def`, populated automatically from
the name prefix (`cg`, `tc`, `lower_`, `mcp_`, etc.) on insert. This gives LLMs exactly what
they need:

```sql
-- what subsystems exist?
SELECT DISTINCT ns FROM def ORDER BY ns

-- everything an LLM needs to start a task in the codegen subsystem
SELECT name, body FROM def WHERE ns = 'cg'

-- everything an LLM needs to start a task in the MIR lowering subsystem
SELECT name, body FROM def WHERE ns = 'lower'
```

This formalises what LLMs already do implicitly when they recognise `cg_` or `tc_` prefixes
— no join, no language syntax changes, no call-site rewrites. The `glyph dump --budget`
command can use `ns` to prioritise which definitions to export.

The full analysis of options is in `module-assessment.md`.

**Effort: S (`ns TEXT` column + `cmd_put` update to extract ns from name prefix). Bootstrap: none.**

---

## 3. Standard Library

**Priority: P1 — Effort: M**

### Current runtime modules (13 total in glyph.glyph)

| Module | Contents |
|--------|----------|
| `cg_runtime_c` | memory (alloc/realloc/dealloc), strings (concat/eq/len/slice/char_at), arrays (push/len/set/pop/bounds_check), basic I/O (println/eprintln), exit, str↔int conversions |
| `cg_runtime_io` | file I/O (read_file/write_file), system(), exit, args |
| `cg_runtime_sb` | string builder (sb_new/sb_append/sb_build) |
| `cg_runtime_math` | sin/cos/sqrt/atan2/fabs/pow/floor/ceil (via libm) |
| `cg_runtime_result` | ok/err/try_read_file/try_write_file |
| `cg_runtime_sqlite` | db_open/close/exec/query_rows/query_one |
| `cg_runtime_float` | float↔int conversions, float↔str |
| `cg_runtime_raw` | glyph_raw_set (pointer mutation for MIR) |
| `cg_runtime_mcp` | JSON helpers for MCP server |
| `cg_runtime_coverage` | coverage instrumentation |
| `cg_runtime_args` | argc/argv access |
| `cg_runtime_extra` | print (no-newline), array_new, read_line, flush |

### Glyph-level library functions (in glyph.glyph)

`str_contains` (substring search), `str_lt` (lexicographic compare), `sort_str` /
`sort_str_do` / `sort_str_insert` (insertion sort — string arrays only).

### What is missing

**Strings** (no C runtime equivalents, must be added to `cg_runtime_c` or a new
`cg_runtime_str2` definition):

| Function | Purpose |
|----------|---------|
| `str_split(s, delim)` | Split string by delimiter → `[S]` |
| `str_join(arr, sep)` | Join string array with separator |
| `str_starts_with(s, prefix)` | Prefix check |
| `str_ends_with(s, suffix)` | Suffix check |
| `str_to_upper` / `str_to_lower` | Case conversion |
| `str_trim` | Strip leading/trailing whitespace |
| `str_replace(s, from, to)` | Substring replace |

**Collections** (needs closure ABI for callbacks):

| Function | Notes |
|----------|-------|
| `array_sort(arr, cmp_fn)` | Generic sort with comparator fn ptr |
| `array_map(arr, f)` | Map closure over array |
| `array_filter(arr, pred)` | Filter by predicate closure |
| `array_fold(arr, init, f)` | Fold/reduce |
| `array_find(arr, pred)` | First match → `?T` |
| `array_concat(a, b)` | Concatenate two arrays |

**Hash maps** (entirely absent, needed for map type `{K:V}`):

`hm_new` / `hm_set` / `hm_get` / `hm_del` / `hm_keys` / `hm_len` / `hm_has`

**Environment and OS** (needed for cross-platform builds and practical programs):

`glyph_getenv(name)` — needed for `CC` env var support and Windows temp dir portability.
`glyph_temp_dir()` — returns `$TMPDIR` on Unix, `%TEMP%` on Windows.
`glyph_list_files(dir)` — directory listing (currently uses shell `find` invocations).
`glyph_mkdir_p(path)` — create directories (currently uses shell `mkdir -p`).

**Implementation path**: Add new `cg_runtime_str2`, `cg_runtime_collections`,
`cg_runtime_hashmap` definitions to glyph.glyph. Each adds a C string to `cg_runtime_full`.
Extend `is_runtime_fn` chain (currently 4 levels: fn→fn2→fn3→fn4) for new function names.
`glyph_getenv` and `glyph_temp_dir` are 5-line C functions — high value, low cost.

**Bootstrap impact: none.** Pure glyph.glyph additions.

---

## 4. Performance

**Priority: P2 — Effort: S (quick wins) to L (LLVM backend)**

### Benchmark analysis

The benchmark data (RESULTS.md) points to four distinct bottlenecks:

**Function call overhead (fib 3.1×)**: Every function entry emits:
```c
_glyph_current_fn = "fn_name";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "fn_name";
  _glyph_call_depth++;
#endif
```
The call-stack push is already DEBUG-guarded. The `_glyph_current_fn` assignment is
unconditional — it runs in every release build, adding a store instruction to every
function prologue. On a recursive benchmark, this multiplies across 18M calls.

**Array operation overhead (sieve 8.3×, sum 21×)**: Every array access calls C functions:
- `glyph_array_set(hdr, i, v)` — function call + bounds check
- `glyph_array_len(hdr)` — function call for `((long long*)hdr)[1]`
- `glyph_array_bounds_check(idx, len)` — separate function call

A tight loop reading 1M array elements makes 2M+ function calls. C does a single instruction
per access. This is the 21× gap for `array_sum`.

**String overhead (concat 9.4×)**: `glyph_str_concat` allocates a new heap string per
concatenation. The string builder exists and is correct, but `str_builder` at 92× shows that
even `glyph_sb_append` (called per character) is a function call overhead vs. C's inline
buffer write.

**TCO already exists**: The `tco_optimize` pass (11 definitions: `tco_opt_fn`,
`tco_opt_blks`, `tco_transform`, etc.) transforms direct tail-recursive calls to `goto`
loops. This is a significant existing optimization. It applies only when the recursive call
is in tail position — fib's two-branch recursion cannot be TCO'd, which is why the
benchmark still needs `ulimit -s unlimited`.

### Fix 1 — Guard `_glyph_current_fn` in release (S)

Modify `cg_function2` in glyph.glyph. The `fntrack` variable currently emits the
`_glyph_current_fn` assignment unconditionally. Move it inside the `#ifdef GLYPH_DEBUG`
guard. This removes one store per function call in release mode. Expected improvement:
fib 3.1× → ~2×.

```glyph
-- current fntrack in cg_function2:
fntrack = s6("  _glyph_current_fn = \"", mir_fn.fn_name, "\";\n#ifdef GLYPH_DEBUG\n...")
-- fix: wrap _glyph_current_fn assignment in #ifdef GLYPH_DEBUG too
```

**Bootstrap impact: none.**

### Fix 2 — Inline array operations as C macros (M)

In `cg_runtime_c`, replace `glyph_array_len` and `glyph_array_set` with macros:

```c
#define glyph_array_len(hdr) (((long long*)(hdr))[1])
#define glyph_array_get(hdr, i) (((long long*)((long long*)(hdr))[0])[i])
```

Keep `glyph_array_bounds_check` as a function (called only in debug builds). In release mode,
bounds checks can be conditional on `GLYPH_DEBUG`. Expected improvement: array_sum 21× → ~3×.

**Bootstrap impact: none.** Change is in the C string emitted by `cg_runtime_c`.

### Fix 3 — LLVM IR backend (L)

Add a `cg_llvm_*` definition family in glyph.glyph that emits LLVM IR text format instead
of C. LLVM IR is structurally similar to Glyph's MIR (SSA, explicit types, CFG). LLVM's
`opt` pass would handle inlining, vectorization, and loop optimization that C codegen cannot
easily control. This eliminates the dependency on an external C compiler.

**Bootstrap impact: none** (new glyph.glyph definitions only, no new syntax).

---

## 5. Error Reporting & LLM Feedback Loop

**Priority: P2 — Effort: S (type error gating) to M (MCP diagnostics)**

Glyph's users are LLMs. "Developer experience" means the quality of structured, machine-
parseable feedback that an LLM receives through MCP tools — not IDE integration, formatters,
or REPLs. The relevant question is: when an LLM writes a definition and submits it, how
quickly and accurately does the compiler tell it what is wrong?

### Current state

**Parse errors**: `format_diagnostic` in glyph.glyph already produces line:col + source
context with caret pointer. `cmd_put` rejects broken definitions at insert time. Parse errors
gate compilation (`check_parse_errors` → `glyph_exit(1)` before codegen).

**Runtime errors**: SIGSEGV + SIGFPE handlers installed unconditionally. `_glyph_current_fn`
tracks the current function (useful despite the performance cost discussed above). Under
`GLYPH_DEBUG`, full call-stack traces with all frames.

**Type errors**: Advisory only — see §1. A program with 5 type errors produces the same
binary as one with none.

**Coverage**: `glyph test --cover` + `glyph cover` report function-level hit rates. A
complete feature that is unique to Glyph's toolchain.

**MCP feedback**: The existing `check_def` tool validates a single definition and returns a
JSON result. This is the primary channel through which an LLM receives per-definition
feedback without triggering a full build.

### Gaps in the LLM feedback loop

**Type errors not surfaced through MCP** (M): `tc_report_errors` emits to stderr as
unstructured text. An LLM calling `check_def` via MCP gets parse errors as JSON but receives
no type error information. The fix is to have `check_def` run `tc_infer_loop` on the
definition and return type errors in the same structured JSON envelope as parse errors.

**No MCP `build` or `run` tool** (M): An LLM that wants to compile and test a program must
drop out of MCP and invoke the CLI directly. Adding `build` and `run` tools to the MCP server
would close the loop: write definitions via `put_def`, compile via `build`, observe output via
`run` — all within a single MCP session. The JSON subsystem and `build_program` pipeline are
already in glyph.glyph; the new tools are wrappers that capture stdout/stderr and return them
as JSON fields.

**`dump --budget` token ordering** (S): `dump --budget` exports definitions up to a token
budget but iterates flatly. Adding dep-depth ordering (definitions closer to `main` get higher
priority) would give LLMs a more useful context window slice when exploring an unfamiliar
database. The dep table already has this ordering via `v_context`.

---

## 6. Platform Support

**Priority: P3 — Effort: S (macOS) to M (Windows)**

The full analysis is in `portability-assessment.md`. Summary by tier:

### Tier 1: macOS (2 changes, effort S)

Both blockers are the same flag. In glyph.glyph `build_program` and `build_test_program`:

```glyph
-- current:
glyph_system(s5("cc ", cc_flags, " ", c_path, " -o ", output_path, " -no-pie", ...))
-- fix: remove " -no-pie"
```

In glyph0 Rust `linker.rs:52`: conditionalize `-no-pie` on Linux. Two targeted edits — one
in glyph.glyph, one in Rust. No other blockers on macOS (SystemV ABI is used on macOS x86-64;
`cranelift_native` auto-detects Apple Silicon).

### Tier 2: Linux AArch64 (already works)

C codegen targets whatever `cc` compiles for. No changes needed.

### Tier 3: Windows (6 changes, effort M)

1. Replace `/tmp` hardcoding with `glyph_temp_dir()` (5 occurrences across `build_program`,
   `build_test_program`, `cmd_run`, `cmd_import`, `cmd_test`)
2. Fix exit code extraction in `cg_runtime_io`: `(rc >> 8) & 0xFF` → `rc` on Windows
3. Guard POSIX signals in `cg_main_wrapper`: `#ifndef _WIN32` around `SIGSEGV`/`SIGFPE`
4. Remove `-no-pie` (same as macOS fix)
5. Fix `CallConv::SystemV` in glyph0 `abi.rs:31` → conditionalize on Windows
6. Adapt `build.ninja` bootstrap rules for Windows shell

### Tier 4: Cross-compilation (already works, one enhancement)

Set `CC=<triple>` and `./glyph build` targets that architecture. The self-hosted compiler
hard-codes `"cc"` in the invocation; adding a `glyph_getenv("CC")` lookup (also needed for
the stdlib) makes cross-compilation first-class:

```glyph
cc_cmd = match glyph_getenv("CC") / "" -> "cc" / s -> s
```

---

## 7. Tooling & Ecosystem

**Priority: P2 (MCP extensions) — P3 (link) — P4 (package versioning)**

Glyph's tooling strategy is MCP-first. An LSP server, formatter, REPL, and doc generator
are human-IDE primitives — irrelevant when the user is an LLM. The MCP server already covers
the equivalent functionality:

| Human tool | MCP equivalent (already exists) |
|-----------|--------------------------------|
| LSP goto-definition | `deps` / `rdeps` tools |
| LSP workspace symbol | `search_defs` tool |
| LSP diagnostics | `check_def` tool |
| LSP hover / type info | `check_def` (partial) |
| REPL | `put_def` → `build` → `run` loop |
| Code formatter | not needed — LLMs write token-minimal source directly |
| Doc generator | LLMs read definition bodies directly via `get_def` / `sql` |

The right investment is deepening the MCP server, not adding human-facing tools.

### MCP server extensions (P2, M)

Current tools: `get_def`, `put_def`, `list_defs`, `search_defs`, `remove_def`, `deps`,
`rdeps`, `sql`, `check_def`, `coverage`.

**`build` tool**: Trigger `build_program` from MCP and return structured JSON with success
flag, compiler output, and output binary path. Closes the write→compile loop without leaving
MCP. The pipeline is already in glyph.glyph; the tool is a thin wrapper.

**`run` tool**: Build and execute `main`, capturing stdout/stderr, returning them as JSON
fields. Gives an LLM the full observe-act cycle within a single MCP session.

**`init` tool**: Create a new `.glyph` database from MCP. Currently `glyph init` is CLI-only.

**Richer `check_def` output**: Currently returns parse errors as JSON. Extend to also run
`tc_infer_loop` on the definition and return type errors in the same envelope. This gives LLMs
type feedback without a full build.

### Package / library system (P3, M, needs module system first)

`glyph link <lib.glyph> <app.glyph>` — copies exported definitions from `lib` into `app`,
erroring on name collisions. New `cmd_link` definition. Requires the `ns` column (§2) to be
in place first, so the tool knows which definitions to copy.

This is the correct composition mechanism for LLMs: copy definitions by SQL query, not by
import syntax. The LLM can then call `search_defs` or `sql` on the merged database to verify
what was brought in.

---

## 8. LLM-Native Language Features

**Priority: design space — not current gaps**

The previous sections cover features missing from a conventional compiler checklist. This
section asks a different question: given that LLMs are the users, what features would a
language designed *specifically* for LLM authors look like — features that may never appear
in human-oriented languages because they solve problems humans don't have?

The relevant properties of LLMs as programmers:

- **Fixed context window**: they cannot hold the whole program in working memory; they query
  what they need via MCP. Context efficiency is a hard constraint, not a preference.
- **Probabilistic generation**: they make type errors, forget signatures, write partially-
  correct code. The compiler's job is not just to report errors but to close the feedback
  loop tightly enough that the LLM can self-correct.
- **No muscle memory**: syntactic cost is measured in tokens, not keystrokes. A feature that
  saves 20 tokens per use is meaningfully better, even if it would seem trivial to humans.
- **Definition-centric thinking**: LLMs naturally think in functions, not files. Glyph
  already matches this — every improvement that leans further into it is compounding.
- **They cannot inspect a running program interactively**: all feedback comes through the
  compiler and MCP tools. The richer and more structured that feedback, the better.

### Typed holes

A `?` placeholder in an expression, understood by the compiler as "I don't know what goes
here yet." When `check_def` encounters a hole, instead of failing, it reports the inferred
type expected at that position:

```
sort arr =
  sorted = array_sort(arr, ?)   -- what comparator type is expected here?
```

`check_def` response: `hole at sort/1: expected (I → I → Bool)`

This is already well-understood in dependently-typed languages (Agda, Idris) but unusual
elsewhere. For LLMs it is particularly valuable: an LLM cannot "look sideways" at surrounding
context the way a human reading source code does. The type checker can see the full context;
surfacing it at each hole closes the feedback loop without requiring a complete, compiling
program. Implementation: a new `ex_hole` AST node, handled in `infer_expr` to emit the
inferred expected type into the error list rather than failing.

### Inline examples stored in the database

A first-class way to attach input/output examples to a definition, stored as queryable DB
rows rather than embedded in comments:

```
sort arr cmp = ...
@example sort([3,1,2], int_lt) == [1,2,3]
@example sort([], int_lt) == []
```

Examples would be stored in the `tag` table (currently defined but unused) with
`key='example'` and `val='sort([3,1,2], int_lt) == [1,2,3]'`. The compiler can run them as
lightweight regression tests. More importantly, an LLM querying an unfamiliar function via
`get_def` or `search_defs` gets its behavior illustrated concretely — not just a type
signature. At scale, `SELECT val FROM tag WHERE def_id=? AND key='example'` is a direct
semantic grounding mechanism that names and types alone cannot provide.

This leans into what Glyph's database design already makes natural: examples are data, not
comments, and SQL is the query language.

### `@memo` directive

A compiler-emitted memoization wrapper, declared with a single annotation:

```
@memo
fib n = match n < 2 / true -> n / _ -> fib(n-1) + fib(n-2)
```

The compiler emits a hash-map cache keyed on arguments, wrapping the original function. LLMs
write recursive algorithms naturally but cannot reliably reason about whether a particular
recursion pattern benefits from memoization — they would need to analyse the call graph,
identify overlapping subproblems, and verify referential transparency. A declarative hint
offloads that judgement to the author at definition time. The compiler implementation is a
`@memo` attribute in the `tag` table, recognized by codegen to wrap the function body.

### `@priority` for context budget control

A hint to `dump --budget` and MCP context exports:

```
@priority high
cmd_build db args = ...   -- always include this in budget exports

@priority low
cg_block_stmts2 mir bi = ...   -- internal detail, omit when budget-constrained
```

Stored as `tag(key='priority', val='high'|'low')`. `dump --budget` and the `build` MCP tool
use it to rank definitions when the token budget forces a cutoff. This is a concept with no
analogue in human-oriented languages — human developers do not have context window limits.
For LLMs, making the author's intent about importance explicit and queryable is directly
useful: an LLM working in an unfamiliar database can bootstrap understanding by requesting
only high-priority definitions first.

### `spec` definitions

A `kind='spec'` definition that declares what a function *should* do, independently of its
implementation:

```
spec sort arr cmp =
  result = sort(arr, cmp)
  assert_eq(glyph_array_len(result), glyph_array_len(arr))
  assert(is_sorted(result, cmp))
```

The computational model is essentially **QuickCheck**: write a property that should hold
across inputs, run it against generated values, report failures. QuickCheck's core insight —
separate the property from specific test cases and let the framework supply inputs — is
exactly what makes `spec` distinct from the existing `kind='test'` definitions. A `test`
definition is a fixed scenario; a `spec` is a claim about all inputs.

**What `spec` adds over QuickCheck**: the storage model. In QuickCheck, properties are
functions identified by naming convention (`prop_sort_length`). In Glyph, a `spec` is a
first-class DB entry with an explicit association to the function it specifies. This means:

```sql
-- what contracts exist for functions in the sort subsystem?
SELECT s.name, s.body FROM def s WHERE s.kind = 'spec' AND s.name LIKE 'sort%'

-- which functions have no spec at all?
SELECT f.name FROM def f
WHERE f.kind = 'fn'
AND NOT EXISTS (SELECT 1 FROM def s WHERE s.kind = 'spec' AND s.name = f.name)
```

An LLM modifying a subsystem can query its contracts before touching the implementation.
An LLM writing a new function can be prompted to also write a `spec` for it. Neither is
possible when properties are just `prop_`-prefixed functions buried in a test file.

**The generator problem**: QuickCheck's most important infrastructure is `Arbitrary` —
a typeclass that tells the framework how to generate random values of each type. Without
generators, `spec` degrades to `test` with different syntax — the LLM still has to supply
concrete inputs by hand. For Glyph, the options are:

- *Built-in generators for primitive types*: the runtime knows how to generate random `I`,
  `S`, `B`, `[I]`, `[S]` without any author input. This covers a large fraction of
  practical specs.
- *`@gen` annotation for user-defined types*: the author writes a generator function and
  tags it. Weaker than a typeclass but workable without a trait system.
- *Fixed-input fallback*: if no generator is available, the spec runs only against
  `@example` values. Still more structured than a `test` definition.

**Shrinking**: QuickCheck's most practically useful feature is counterexample minimization —
when a property fails on a 100-element list, it finds the smallest failing case automatically.
Shrinking requires knowing how to reduce a value of each type, which again ties back to a
generator/typeclass mechanism. Without shrinking, a failing `spec` reports the full random
input that caused the failure, which can be large and hard to interpret. Shrinking is
a significant implementation investment but essential for the feature to be useful in
practice: an LLM receiving a 47-element failing array has to reason about it; an LLM
receiving a 2-element failing case can act immediately.

**Relation to existing `test` definitions**: `spec` does not replace `test`. Fixed scenarios
(`test_sort_empty`, `test_sort_single`) remain valuable precisely because they document
specific cases the author cared about. `spec` and `test` are complementary: tests are
examples of correct behavior; specs are claims about all behavior. Both are `kind=` entries
in the DB, both are queryable, both run under `glyph test`.

### Semantic `doc` column on `def`

A `doc TEXT` column on the `def` table, populated by the LLM on insert:

```bash
./glyph put program.glyph fn -b 'sort arr cmp = ...' \
  --doc 'sort an array in-place using comparator cmp; returns sorted copy'
```

`search_defs` today matches on definition body text — useful for finding structural patterns
but poor for semantic discovery. A `doc` column enables `WHERE doc LIKE '%sort%'` or, more
powerfully, embedding-based semantic search layered on top of the MCP server. At scale (thousands
of definitions), the difference between "find functions whose body contains the string `cmp`"
and "find functions that sort things" becomes the difference between a working context and a
useless one. The `tag` table could serve this purpose today, but a dedicated column with
indexing is cleaner.

---

## Recommended Roadmap

### Phase 1 — Correctness foundation (P1)

These are small, high-impact changes that fix silent correctness failures:

1. ~~**Gate type errors in `build_program`**~~ **Done** — `glyph_exit(1)` on non-empty `eng.errors`. Also required: `glyph_arr_get_str` Rust runtime addition + 5 heterogeneous-array false-positive fixes.
2. ~~**Fix BUG-006**~~ **Done** — `lower_str_interp_parts` string type check applied, string
   interpolation no longer corrupts string-typed expressions.
3. **`glyph_getenv` runtime function** — 5-line C addition; unblocks CC env var and Windows
   temp dir support.
4. **`str_split` / `str_join` / `str_starts_with`** in `cg_runtime_c` — highest-value missing
   string operations.

### Phase 2 — Performance and MCP feedback (P2)

5. **User-defined enums** — parse variant names from `kind='type'` bodies, replace
   hardcoded `variant_discriminant` table, register constructors in type env. Fixes
   silent-wrong behavior; no glyph0 changes.
6. **Release-mode `_glyph_current_fn` guard** — move assignment inside `#ifdef GLYPH_DEBUG`
   in `cg_function2`; reduces function-call overhead in release builds.
7. **Inline array operations as macros** — `glyph_array_len` and `glyph_array_get` as `#define`
   in `cg_runtime_c`; eliminates the 21× array-sum gap.
8. **macOS support** — remove `-no-pie` from `build_program`/`build_test_program` and
   `linker.rs`; 2 edits.
9. **MCP `build` / `run` / `init` tools** — closes the write→compile→observe loop within
   a single MCP session; extends `check_def` with type error JSON output.

### Phase 3 — Ecosystem features (P3)

10. **`ns` column on `def`** — formalizes the naming convention; enables `glyph dump --budget`
    and MCP queries to filter by subsystem.
11. **`dump --budget` dep-depth ordering** — use `v_context` ordering so budget exports
    prioritise definitions closest to `main`.
12. **Windows portability** — 6 targeted changes after `glyph_getenv`/`glyph_temp_dir` exist.
13. **Map type `{K:V}`** — hash map runtime (`hm_*`) + MIR lowering; the one missing
    data structure with no workaround.
14. **`glyph link`** — library composition tool (after module columns).

### Phase 4 — Language advancement (P4)

15. **Generics / monomorphization** — `mono_*` definition family; post-type-check transform
    on existing HM output, no glyph0 changes needed. Largest single engineering investment.
16. **LLVM IR backend** — `cg_llvm_*` definitions; eliminates C compiler dependency,
    enables LLVM optimization passes.

---

## Summary Table

| Feature | Status | Effort | Priority | Bootstrap? |
|---------|--------|--------|----------|-----------|
| Type errors gate compilation | **Fixed** | S | P1 | No |
| BUG-006: string interpolation | **Fixed** | S | P1 | No |
| `glyph_getenv` runtime | Missing | S | P1 | No |
| `str_split`/`str_join`/`str_starts_with` | Missing | S | P1 | No |
| User-defined enums | Silently broken | S–M | P2 | No |
| Release-mode perf (_glyph_current_fn) | Unoptimized | S | P2 | No |
| Inline array macros | Unoptimized | S | P2 | No |
| macOS support | Blocked (−no-pie) | S | P3 | Yes (linker.rs) |
| `ns` column on `def` | Not present | S | P3 | No |
| MCP build / run / init tools | Missing | M | P2 | No |
| Richer check_def (type errors as JSON) | Partial | M | P2 | No |
| Map type `{K:V}` + hash map runtime | Not implemented | M | P3 | No |
| Windows support | Multiple blockers | M | P3 | Yes (abi.rs) |
| `glyph link` | Missing | M | P3 | No |
| Generic array_sort / map / filter | Missing | M | P3 | No |
| Lift 200-def type-check threshold | **Fixed** | L | P2 | No |
| LLVM IR backend | Not started | L | P4 | No |
| Generics / monomorphization | Absent | XL | P2 | No |
| `fsm` / `srv` / `macro` | Human abstractions | — | — | — |
| Traits / impls | **Removed** | — | — | — |
