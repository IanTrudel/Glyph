# Next Steps for Glyph

## Current State

- **1,774 definitions** (1,399 fn + 360 test + 13 type + 2 const), ~192k tokens
- **10 libraries** shipping (async, gtk, json, network, regex, scan, stdlib, thread, web, x11)
- **stdlib.glyph**: 65 definitions — core collection ops, string utilities, base64
- **21 example programs** (api, asteroids, benchmark, calculator, countdown, errors, fibonacci, fsm, gbuild, gled, glint, gstats, gtk, gwm, hello, life, pipeline, prolog, sheet, vulkan, web-api)
- **360 self-hosted tests**, 77 Rust tests (6 crates)
- **4-stage bootstrap** working: glyph0 (Rust/Cranelift) → glyph1 (Cranelift) → glyph2 (C codegen) → glyph (LLVM)
- **23 externs** in compiler, MCP server with 18 tools
- Recent work: full monomorphization (v0.5.0), regex library, scan library expansion, Result type + error propagation, frozen hashmap dispatch, guard-based refactoring

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

### 11. Package manager / library registry
`glyph link` copies definitions into the target database and `glyph use` registers build-time deps, but there's no versioning, no conflict resolution beyond abort-on-collision, and no way to update a linked library. A lightweight package system could:

- Add a `lib_version` or `lib_hash` column to linked definitions so `glyph link` can detect stale copies
- Support `glyph update-libs app.glyph` to re-link all registered libraries from their current source
- Content-hash based diffing: if the library definition hash matches the linked copy, skip re-linking
- Dependency resolution: if library A uses library B, linking A should pull B automatically

The SQL schema already tracks provenance (the `dep` table knows cross-database origins). This is extending what exists rather than building from scratch.

### 12. Incremental test execution
`glyph test` runs all 360 tests every time. The dependency graph (`dep` table + `v_dirty` view) already knows which definitions changed. An incremental test mode would:

- Walk the transitive dependents of each dirty definition
- Intersect that set with test definitions
- Run only the affected tests

For a 1,774-def compiler where a typical change touches 1–5 definitions, this could skip 90%+ of tests. The `v_dirty` view already computes transitive closure — the missing piece is filtering the test list against it. A `glyph test --dirty` flag would be the natural interface.

### 13. Dead code elimination (definition-level)
Not the MIR-level DCE from item #2 — this is whole-definition DCE. Walk the dependency graph forward from `main` (or from all test entry points), and flag any definition not transitively reachable. With 1,774 definitions, there are likely orphaned functions from past refactoring cycles.

`glint` already does a basic version of this (orphan detection). Promoting it to a first-class `glyph gc` or `glyph dead` command would keep the database lean. Implementation is a single SQL query: definitions not in the transitive closure of `main`'s forward dependencies.

### 14. String performance
Synthetic benchmarks show string operations 8–75x slower than C, but these measure pathological cases (single-char concat loops). The actual compiler build profile may be dominated by parsing, type inference, SQLite I/O, or `cc` invocation — profiling needed to confirm whether strings are the real bottleneck. The runtime has three layers, each with distinct bottlenecks:

**`glyph_str_concat`** — allocates `16+len` bytes per call, copies both sides. O(n²) for repeated concat. This is what `+` on strings compiles to.

**`glyph_sb_*` (StringBuilder)** — doubling buffer, O(n). Used for string interpolation. Still 48–75x slower than C because:
- `sb_append` does `malloc+memcpy+free` instead of `realloc` (which can extend in-place)
- Every append unwraps a GVal string to get `{ptr, len}` — two pointer dereferences per call
- `sb_build` allocates a final copy of the entire buffer, then frees the old one
- Each call crosses a function boundary (no inlining without LTO)

**String literals** — every `"foo"` in generated code heap-allocates a `16+len` byte fat pointer at runtime, even if it's a 3-byte constant used thousands of times.

For the compiler's codegen phase, the workload is: build C/LLVM source by concatenating thousands of short fragments across ~1,774 definitions. The hot path is `sb_append` of small strings, over and over.

Practical improvements in order of bang-for-buck:

**A. Fix `sb_append` to use `realloc`** — one-line change, Boehm's `GC_realloc` can extend in-place. Currently it `malloc+memcpy+free`s every growth. Trivial fix.

**B. Static string literals** — string constants known at compile time don't need heap allocation. Emit them as `static` C data with the fat pointer pointing into `.rodata`. Zero malloc, zero GC pressure. The compiler already knows which strings are literals.

**C. Small string optimization (SSO)** — strings ≤14 bytes stored inline in the fat pointer header instead of heap-allocating. Most field names, type tags, and codegen fragments are short. Eliminates malloc for the majority of strings.

**D. Arena allocator for codegen** — bump allocator freed in bulk after each build phase. Fast allocation (increment a pointer), no per-object GC tracking. Requires scoping awareness.

### 15. `glyph fmt` — canonical formatting
Since programs are databases, there's no file format to argue about. But definition bodies still have inconsistent style (spacing, indentation depth, newline placement between arms). A `glyph fmt` command that round-trips each definition through the parser and re-emits with canonical formatting would:

- Normalize style across all definitions
- Enable meaningful content-hash comparison (formatting differences currently create false dirty flags and unnecessary recompilation)
- Make `glyph dump` output machine-consistent

The parser already exists. The missing piece is a pretty-printer that emits canonical source from the AST. This is also a prerequisite for reliable `glyph export` / `glyph import` round-tripping.

### 16. Cross-compilation / target triples
The C codegen backend emits standard C, making it inherently portable. Adding `--target=<triple>` (e.g., `aarch64-linux-gnu`, `x86_64-w64-mingw32`) would mean:

- Selecting the appropriate cross-compiler (`aarch64-linux-gnu-gcc` instead of `cc`)
- Passing `--target` to `llc` for the LLVM backend
- Adjusting pointer size assumptions (currently hardcoded to 64-bit)

This would make Glyph programs deployable on ARM servers, Raspberry Pi, and Windows with zero language changes. The `cc_prepend` / `cc_args` meta keys already support custom compiler flags — cross-compilation extends this naturally.

### 17. Parallel compilation
The build pipeline compiles definitions sequentially. But the dependency graph defines a partial order — definitions at the same depth level are independent and can compile in parallel. With `thread.glyph` already shipping:

- Topologically sort definitions by dependency depth (the `v_context` view already does this)
- Compile each depth level's definitions concurrently using a thread pool
- Merge compiled artifacts into the `compiled` table

For a 1,774-def compiler, even 4-way parallelism could meaningfully cut full-rebuild time. The C codegen backend is naturally parallelizable since each function compiles to an independent C fragment.

**Status (March 2026):** Premature. Profiling (#19) shows the bottleneck is the external compiler backend (`cc`/`llc`), which is a single invocation on one large output file — parallelizing Glyph-side codegen would save at most 1-2s on a 17s build. Glyph is the largest Glyph program; there aren't yet programs large enough for this to matter. Revisit when either: (a) direct object emission eliminates the external compiler, or (b) programs grow significantly beyond ~1,400 definitions.

### 18. LLM refactoring tools (MCP extensions)
The MCP server has 18 tools covering read/write/build operations, but is missing the refactoring primitives that LLMs need most when restructuring code:

- **`rename`** — rename a definition and update all dependents. The `dep` table makes finding call sites trivial; the hard part is rewriting source bodies (regex replacement on the body text, validated by re-parsing).
- **`inline`** — inline a function's body at all call sites and remove the original definition. Useful for collapsing helper functions during simplification.
- **`extract`** — extract a subexpression into a new named function, threading free variables as parameters. The inverse of inline.
- **`move_ns`** — move a definition to a different namespace, updating the `ns` column and renaming the prefix.

These compound operations are error-prone when done manually (edit body, update deps, rename, re-check). As atomic MCP tools they'd make large refactors reliable.

### 19. Compiler build performance
Profiling `glyph build glyph.glyph --full` (1,399 fn defs, March 2026) shows the actual bottlenecks:

```
Wall: 18.2s | User: 12.7s | Sys: 0.6s | CPU: 65%

Type checker:     12.0%   ← DOMINANT BOTTLENECK
  subst_walk        3.43%   (type substitution walking)
  tc_collect_fv     3.04%   (collect free type variables — deeply recursive)
  pool_get          2.51%   (type pool array access)
  subst_find        1.05%   (union-find lookup)
  tc_collect_fv_f   0.67%   (field-level free var collection)
  efv_loop          0.68%   (free var extraction)
  ty_* constructors 1.13%   (ty_var, ty_fn, ty_record, ty_array, ty_opt)
  env_nullify_count 0.54%   (env cleanup)

Env lookup:        3.7%    ← LINEAR STRING SCAN
  glyph_str_eq      2.44%   (called from env_lookup_at)
  env_lookup_at     1.29%   (linear scan of name arrays)

GCC (cc1):        12.7%    ← COMPILING GENERATED C
Boehm GC:          3.0%
Debug checks:      2.0%    (null_check — free in release build)
Monomorphization:  0.9%
Array runtime:     0.7%
String codegen:    ~0%     ← NOT A BOTTLENECK
```

Key findings: string building during codegen is invisible in the profile. The type checker dominates, with `subst_walk` and `tc_collect_fv` doing deep recursive walks over the type pool during generalization of every function. The `glyph_str_eq` cost is from environment name lookups, not string construction.

#### 19.1. `efv_loop` walks entire environment on every generalize (~4% CPU)

Every call to `generalize` calls `env_free_vars` → `efv_loop`, which iterates over **every entry in `eng.env_types`** and calls `tc_collect_fv` on each one to collect free variables. For function #1000 in the inference order, that's walking ~1000 environment types, each of which may be a complex type tree. This happens for every one of 1,399 functions, making generalization **O(n² × type_complexity)** in the number of definitions.

**Fix:** OCaml-style type levels. Each type variable gets a "level" integer when created. The current scope has a level counter. At generalization time, instead of walking the entire environment to find which vars are free, check: is the var's level > current scope level? If yes, it's local and can be generalized. No tree walking needed. Turns generalization from O(n²) to O(size of the function's type). This is the highest-impact algorithmic improvement.

**Effort:** Large — ~15 defs modified/added, ~3 removed. 1-2 sessions. Highest risk but best ROI.

TyNode doesn't need changing — store levels in a parallel array indexed by var id (like `parent` and `bindings` already are). Specific changes:

- Add `levels` array to the engine record (`mk_engine`) — ~5 defs modified
- Set level in `subst_fresh_var` — ~1 def
- Update level during `subst_bind` — when unifying two vars, the bound var takes `min(level_a, level_b)` — ~1 def, but subtle to get right
- Rewrite `generalize` to walk only the function's type and check `levels[var] > scope_level` instead of calling `env_free_vars` — ~2-3 defs rewritten
- Add scope level increment/decrement in `tc_infer_loop_warm` — ~1 def
- Delete or gut `efv_loop`, `env_free_vars`, `subtract_vars_bs` — ~3 defs removed
- Extensive testing needed — generalization correctness is the heart of HM inference, the min-level-on-unify logic is where bugs hide

#### 19.2. `env_lookup_at` linear string scan (~3.7% CPU)

The environment is two parallel arrays (`env_names`, `env_types`). Every lookup scans backwards doing `str_eq` on each entry. After inferring 1,399 functions, late lookups compare against hundreds or thousands of strings. `env_nullify` is worse — scans **forward through the entire array** to null out all entries with a given name, called once per function in `tc_infer_loop_warm`.

**Fix:** Hash map for name→type lookups. The runtime already has a full mutable hash map (`hm_new`, `hm_set`, `hm_get`, `hm_del`, `hm_has` — open addressing with FNV-1a, implemented in `cg_runtime_map`). Add an `env_map` field to the engine record, rewrite `env_insert` to also `hm_set`, rewrite `env_lookup` to use `hm_get`, rewrite `env_nullify` to use `hm_del`. Keep the parallel arrays for scope mark/restore (env_marks) — the map is an acceleration structure on top.

**Effort:** Small-medium — ~5-6 defs modified. Half a session. Recommended as first optimization — quick, safe, immediate payoff. Specific changes:

- Add `env_map` field (hash map of name → type) to the engine record — `mk_engine` (~1 def)
- Rewrite `env_insert` to also `hm_set` into the map (~1 def)
- Rewrite `env_lookup` to use `hm_get` instead of `env_lookup_at` (~1 def)
- Rewrite `env_nullify` to use `hm_del` instead of full-array scan (~1 def)
- Possibly update `env_push`/`env_pop` for scope consistency (~1-2 defs)
- Keep the parallel arrays for scope mark/restore (`env_marks`) — the map is an acceleration structure on top
- Main risk: env_marks/scope push/pop needs to stay consistent with the map

#### 19.3. `subst_walk` redundant resolution (~3.4% CPU)

`subst_walk` resolves a type index through the substitution (check if var, find root, check binding). Called at the start of `tc_collect_fv`, `inst_type`, `unify`, and others. The recursive tree walks visit the same nodes repeatedly with no memoization — a deep type tree gets `subst_walk`ed once per visitor, and each visitor recurses into children that were already walked by a sibling visitor.

**Fix:** Restructure the recursive walks to pass already-resolved indices to children instead of re-resolving. True memoization would require a cache keyed by pool index — a dense array indexed by pool index would work (indices are small integers). Glyph does have mutable hash maps (`hm_*`) but a flat array is simpler here. May not be worth the infrastructure for a 3.4% gain, especially if 19.1 (type levels) eliminates most of the walks that drive this cost.

**Effort:** Small — ~3-4 defs modified. A few hours. Skip if doing 19.1 — most walks disappear anyway. Specific changes:

- Allocate a `resolved` array of size `pool_len`, initialized to -1, at the start of each `tc_collect_fv` / `inst_type` call
- Check `resolved[ti]` before calling `subst_walk`, store result after
- Thread the cache array through recursive calls
- Cache is only valid within a single "read phase" (not across unification steps, since `subst_bind` changes the substitution)

#### 19.4. `pool_get` tag hint overhead (~2.5% CPU)

```
pool_get eng idx =
  node = eng.ty_pool[idx]
  _ = node.tag      ← forces tag field access for codegen disambiguation (BUG-005)
  node
```

The `_ = node.tag` line is a workaround to help C codegen disambiguate field offsets. It generates a dead read on every pool access. At millions of calls, this adds up.

**Fix:** If monomorphization or codegen improvements make the tag hint unnecessary, `pool_get` becomes a raw array access. Alternatively, a type annotation system could let the codegen infer the correct struct type without runtime hints.

**Effort:** 1 def modified, 5 minutes of editing, potentially hours of debugging if codegen breaks. Binary outcome:

- If mono already provides enough type info to disambiguate: delete `_ = node.tag`, run tests, done
- If codegen still relies on the hint for field offset resolution: the real fix is in the codegen itself (much larger scope — would need the codegen to use type information from the mono pass to select the correct struct type)
- Try it and back off if it breaks

#### 19.5. `lookup_var_map` linear scan (~minor)

`lookup_var_map` does stride-2 linear scan over `[key, value, key, value, ...]` flat arrays. Used during `inst_type` (instantiation). Var maps are typically small (a few entries), but for polymorphic functions with many type parameters this could degrade.

**Fix:** Sort var maps by key and binary search, or use a small hash table for maps above a threshold size.

**Effort:** Trivial — 2 defs modified, 15 minutes. But var maps are typically 1-5 entries, where linear scan is likely faster than binary search with branch overhead. **Probably not worth it.**

#### 19.6. External compiler overhead (~12.7% CPU, ~6s wall)

The C backend spends 12.7% of sampled CPU in GCC `cc1`, and the process blocks in `wait4` while the external compiler runs — accounting for the gap between 65% CPU utilization and 100%. This is a structural cost of generating a large `.c` file and handing it to an external compiler.

Build time comparison (2×2 matrix, same binary for both backends):

| Compiler binary | → C backend | → LLVM backend |
|-----------------|-------------|----------------|
| **LLVM-built** | 10.9s user / 17.6s wall | 12.7s user / 19.5s wall |
| **C-built** | 12.2s user / 18.9s wall | 16.9s user / 23.6s wall |

The LLVM-built binary is faster at everything (better optimized code). The C backend is ~2s faster to emit than LLVM (less verbose codegen, `cc` vs `llc`). Neither backend eliminates the external compiler wait — the only way to do that is emitting object files directly (an ELF emitter in Glyph), which is a major project but would make the compiler fully self-contained with no external toolchain dependency.

**Effort:** Direct object emission is a long-term goal, not a quick fix.

#### 19 summary

| Item | Effort | Risk | CPU saved | Verdict |
|------|--------|------|-----------|---------|
| 19.1 Type levels | 1-2 sessions, ~15 defs | High | ~4%, O(n²)→O(n) | Best ROI but needs care |
| 19.2 Hash env | Half session, ~5-6 defs | Low-medium | ~3.7% | Easy win, do first |
| 19.3 Subst cache | Few hours, ~3-4 defs | Low | ~3.4% (mooted by 19.1) | Skip if doing 19.1 |
| 19.4 Pool_get hint | 5 min or rabbit hole | Unknown | ~2.5% | Try it, back off if breaks |
| 19.5 Var map sort | 15 min, 2 defs | Negligible | ~0% | Not worth it |
| 19.6 GCC/cc | 2-3 defs (tcc) or major (ELF) | Low | ~12.7% | Structural cost |

Recommended order: 19.2 → 19.4 → 19.1 → re-profile.

### 20. Shared library output
Glyph can only produce standalone executables. It can't produce `.so`/`.dylib` files that other languages can call into. This is the interop story — Python, Ruby, Lua, and C programs can't use Glyph code as a library. The codegen already produces C functions with stable names; adding `-fPIC -shared` to the `cc` invocation and generating an entry-point header is mostly plumbing. Combined with a `glyph header program.glyph` command that emits a `.h` file for the public API, this would make Glyph a viable implementation language for libraries consumed by other ecosystems.

### 21. REPL / eval
No way to evaluate an expression without a full compile cycle. `glyph eval program.glyph "1 + 2"` would let LLMs test expressions interactively, verify type behavior, and prototype without touching definitions. Given the SQLite-as-program model, this could work by creating a temporary `main` definition, building, running, and cleaning up. A more ambitious version would JIT-compile via the LLVM backend and keep state across evaluations.

### 22. System interaction gaps
The runtime is thin on OS interaction. Missing pieces that show up in real programs:

- **`getenv`** — environment variables. `gstats` had to use a C FFI wrapper for `time()` because the runtime doesn't expose it. `getenv` is the same class of problem — a single-line C function that programs need but can't access without writing FFI wrappers.
- **`random`** — no random number generation at all. Needed for games (asteroids), testing (property-based), simulation, and cryptographic applications.
- **`time`** — no wall-clock time access. Async timers exist but there's no `clock_gettime` or `gettimeofday`. Benchmarking, logging, and any time-stamped output requires this.
- **`readdir`** — no directory listing. `gbuild` and `glint` would benefit. Any file-processing tool needs to enumerate files.
- **`sleep`** — no direct sleep outside the async runtime. Useful for simple polling, rate limiting, and animation loops.

These are all single-extern additions with trivial C wrappers in the runtime. A batch of 5-6 new runtime functions would cover the most common gaps.

### 23. Dynamic library loading (dlopen)
Programs can't load code at runtime. A `dlopen`/`dlsym` FFI (3-4 externs) would enable plugin architectures — the window manager (`gwm`), text editor (`gled`), or web framework could load user extensions without recompilation. This also enables optional dependencies: a program could try to load a library and gracefully degrade if it's not present.

### 24. Serialization beyond JSON
The JSON library is solid (59 defs, full parser and builder). But common interchange formats are missing:

- **CSV** — the most common data exchange format. Parsing is simple (split on delimiters, handle quoting) and would make a good pure-Glyph library alongside `scan.glyph`. Data processing is a natural fit for Glyph's functional style (`map`/`filter`/`fold` over rows).
- **TOML** — configuration file format. Increasingly standard for project configuration (Cargo.toml, pyproject.toml). More structured than JSON for human-readable config.
- **MessagePack** — binary serialization. Compact, fast, schema-less. Useful for network protocols and inter-process communication where JSON's text overhead matters.

### ~~25. Property-based testing~~ / fuzzing
`glyph test` runs assertion-based tests. Property-based testing (generate random inputs, check that invariants hold) would be powerful for the type checker and parser — exactly the kind of code where edge cases hide. A `glyph fuzz` command could:

- Generate random AST nodes and verify the parser round-trips them
- Generate random type trees and verify unification properties (commutativity, idempotence)
- Generate random programs and verify the compiler doesn't crash

Could be implemented as a pure-Glyph library with generators and shrinkers, similar to QuickCheck/Hypothesis. A deterministic PRNG (xorshift128+ is ~5 lines using `bitxor`/`shl`/`shr`) is preferable over OS randomness for reproducible test failures — no dependency on #22 needed. The generator/shrinker/runner pattern is entirely functional and composable with existing stdlib combinators (`map`, `filter`, `fold`, `generate`).

### 26. Compile-time evaluation
No `const` evaluation beyond literal constants. The `const` kind exists in the schema but doesn't evaluate expressions. Compile-time function execution (like Zig's `comptime`, Rust's `const fn`, D's CTFE) would enable:

- Computed lookup tables (e.g., precomputed sine tables, character classification bitmasks)
- Compile-time string processing (template expansion, format string validation)
- Static assertions (`static_assert(sizeof(GVal) == 8)`)
- Conditional compilation based on computed values

The MIR interpreter infrastructure could be reused here — the compiler already has the representation to evaluate expressions, it just doesn't do so at compile time.
