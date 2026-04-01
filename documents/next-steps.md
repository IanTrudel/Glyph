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

### 27. Definition-level diffing (`glyph diff`)
Git can't meaningfully diff SQLite files — every commit to a `.glyph` database shows `1 file changed, 0 insertions(+), 0 deletions(-)`. A `glyph diff` command that operates at the definition level would make code review possible:

- `glyph diff a.glyph b.glyph` — compare two databases, show added/removed/modified definitions by name with body diffs
- `glyph diff --last program.glyph` — compare current state against the most recent `def_history` snapshot
- `glyph diff --git program.glyph` — compare working copy against the last committed version (extract from `git show HEAD:program.glyph` into a temp file)

Output would be per-definition: name, kind, and a unified diff of the body text. This integrates naturally with the `def_history` table (which already tracks old bodies via triggers) and with `glyph export` (which can produce diffable file trees as a workaround today, but shouldn't be required).

### ~~28. Pattern match exhaustiveness checking~~
The compiler silently accepts incomplete matches — a missing arm hits `tm_unreachable` at runtime (trap with "non-exhaustive match" message). Static exhaustiveness checking would catch these at compile time:

- **Enum/variant exhaustiveness**: if a type has variants `Some`/`None`, a match must cover both (or include a wildcard). The type checker already knows variant sets from `type` definitions.
- **Boolean exhaustiveness**: `match b / true -> ...` without a `false` or `_` arm is incomplete.
- **Redundancy warnings**: a wildcard `_` after all variants are covered means dead code.
- **Guard interaction**: arms with guards (`pat ? expr -> ...`) can't guarantee coverage — treat guarded arms as potentially non-matching for exhaustiveness purposes.

This is high-value for LLM consumers who can't mentally simulate all paths. The information is already available in the type system — enum variant lists from `type` definitions and inferred match subject types from HM inference.

### 29. Benchmark definitions (`glyph bench`)
Like `test` but measures execution time. A `bench` definition kind (e.g., `bench_sort seed = ...`) that the compiler recognizes and `glyph bench program.glyph` runs:

- Execute each bench definition N iterations (default 1000, configurable via `--iter`)
- Warm up with a few discarded runs to stabilize caches
- Report mean, min, max, and standard deviation per benchmark
- Output in both human-readable and machine-parseable (TSV or JSON) formats

The `benchmark` example program already demonstrates the pattern manually (timing loops with `clock_gettime`). Promoting this to a first-class definition kind eliminates the boilerplate and makes performance regression detection automatable. The existing `prop` seed-passing convention could be reused — bench definitions receive iteration state the same way property tests receive seeds.

### 30. Static analysis

The compiler already has two forms of static analysis: HM type inference and exhaustiveness checking. A broader static analysis pass would catch more classes of bugs at compile time, ordered by value vs. effort:

**Low effort, high value:**
- **Dead definition detection*** — the `dep` table already exists. Any def not transitively reachable from `main` (or tests) is dead. Practically free with a SQL query.
- **Unused variables** — at MIR level, any local that's assigned but never read. Straightforward walk over blocks.

**Medium effort, high value:**

- **Unreachable code*** — blocks with no predecessors after a `tm_return`/`tm_unreachable`/`panic`. The block graph already exists in MIR.
- **Shadowed variable warnings** — scope stack already exists in lowering; just check if a name is already bound in an outer scope.

**Higher effort, interesting:**
- **Purity analysis** — tag functions as pure/impure based on whether they call I/O, mutate state, or call impure functions. Propagates through the dep graph. Useful for LLMs reasoning about what's safe to reorder.
- **Definite initialization** — ensure all locals are assigned before use on every code path.

### ~~31. Char literals~~ ✅ COMPLETE
The tokenizer and parser are full of magic numbers: `45` for `-`, `40` for `(`, `123` for `{`, `10` for newline. Every function in `tok_one` through `tok_one4` is a wall of integer comparisons against ASCII codes. A char literal syntax (e.g., `'-'` or `c"-"`) that compiles to the integer code point would make this code self-documenting:

```
-- Before:
match c
  45 -> ...   -- what is 45?
  40 -> ...   -- or 40?

-- After:
match c
  '-' -> ...
  '(' -> ...
```

Implementation is minimal: the tokenizer recognizes `'x'` as a new token kind (`tk_char`), the parser treats it as `ex_int_lit` with the char's code point as the value. No new types, no runtime changes — it's pure syntactic sugar over integers. The compiler itself has ~200+ char code comparisons that would immediately benefit, plus libraries like scan.glyph and regex.glyph.

### 32. `glyph rename` — automated definition renaming
Renaming a definition today requires: (1) get the body, (2) find all reverse dependencies via `rdeps`, (3) get each dependent's body, (4) text-replace the old name with the new name in each, (5) put each modified dependent back, (6) put the renamed definition, (7) remove the old one. That's 2N+3 MCP calls for N dependents, each requiring careful text replacement that could break if the old name appears as a substring of another identifier.

A single `glyph rename old_name new_name` command (and MCP tool) would:
- Rename the definition itself (update `name` column + first token in body)
- Find all dependents via the `dep` table
- Replace whole-word occurrences in each dependent's body (with word-boundary awareness)
- Re-validate all modified definitions by parsing
- Abort and roll back if any parse fails

This is the highest-value refactoring primitive. The `dep` table already provides the call graph; the hard part is reliable token-level replacement in bodies rather than naive string substitution. A two-pass approach (tokenize body → find token spans matching old name → splice new name) would be robust. The recently-completed negative-literal refactor (179 definitions) took a bash+perl script — `glyph rename` would make such changes routine.

### 33. `glyph replace` — bulk regex replacement across definitions
The negative-literal migration exposed a real workflow gap: transforming a pattern across many definitions requires shelling out to sqlite3+perl. A built-in `glyph replace` command would make bulk refactors safe and fast:

```bash
glyph replace program.glyph 'old_pattern' 'new_text' [--regex] [--dry-run] [--kind fn]
```

- `--dry-run` shows which definitions would change and the diff, without writing
- `--regex` enables regex patterns (default is literal string match)
- `--kind` filters to specific definition kinds
- Validates each modified body by parsing before committing
- Reports count of definitions modified

This is distinct from `rename` (#32): rename operates on identifiers using the dep graph; replace operates on arbitrary text patterns. Both are needed — rename for safe identifier changes, replace for syntactic migrations like the `0 - N` → `-N` cleanup.

### ~~34. Bulk `get_defs` MCP tool~~
When investigating a call chain or understanding a subsystem, I routinely call `get_def` 5-10 times in sequence — each a separate MCP round trip. A `get_defs` tool that accepts a list of names and returns all bodies in one call would cut latency significantly:

```json
mcp__glyph__get_defs(db="app.glyph", names=["parse_atom", "parse_unary", "parse_postfix"])
```

Returns `[{name, kind, body, gen, tokens}, ...]`. The `list_defs` tool already returns metadata; this extends it to include bodies. For subsystem exploration, combine with `deps`: get a function's dependency list, then fetch all their bodies in one call. This directly reduces the number of tool calls in the most common research pattern.

### ~~35. Per-definition C/LLVM emit~~
`--emit-c` dumps the entire generated C for all 1,400+ definitions. When debugging codegen for a single function, I have to search through a 30,000+ line file. A targeted emit would be far more useful:

```bash
glyph emit program.glyph fn_name [--format=c|llvm|mir]
```

This would compile just the named function (and its transitive dependencies for type resolution) and output only its generated code. The compiler already compiles definitions individually — the `compile_fn` pipeline produces per-function MIR and C. The plumbing is there; it just needs a CLI entry point that stops after one function instead of concatenating everything.

For debugging, the most common scenario is: "this function segfaults, what C did we generate for it?" Currently that requires `--emit-c` then searching `/tmp/glyph_out.c` for the function name. A targeted emit would make this instant.

### 36. Cross-database search
When building a library or application, I often need to find which library provides a function, or check if a name is already used somewhere. Currently I search each `.glyph` file individually — there's no way to search across a project's registered libraries in one operation.

```bash
glyph search app.glyph 'pattern' [--libs] [--kind fn]
```

With `--libs`, search bodies across the app database AND all registered libraries (from `lib_dep` table). Returns results tagged with their source database. The MCP equivalent would be invaluable for "where is this function defined?" questions that come up constantly during library integration work (like the recent xml/svg/vie integration).

### ~~37. Negative match patterns~~ ✅ COMPLETE
Match currently supports integer literal patterns, string patterns, and constructor patterns. With negative literals now in the tokenizer, negative numbers should work in patterns too:

```
match x
  -1 -> "negative one"
  0 -> "zero"
  _ -> "other"
```

Currently this requires a guard: `_ ? x == -1 -> "negative one"`. The parser's `parse_single_pattern` already handles `tk_int` — it just needs to also accept `tk_minus` followed by `tk_int` (or, with the new negative literal support, a `tk_int` whose value is negative). Since `cur_ival` already returns the correct negative value from the token text, the pattern match compilation in MIR should work unchanged — it's comparing against an integer constant either way.

### 38. Direct machine code emission (ELF backend)
The external C compiler (`cc`/`gcc`) is now the dominant build bottleneck — ~12.7% of CPU time and the primary source of wall-clock latency. It's also the only external toolchain dependency. Emitting ELF object files directly from Glyph would eliminate both problems, making the compiler fully self-contained: a single binary that takes a `.glyph` database and produces an executable with zero external dependencies.

**What this means concretely:**
- Replace the `cg_program → write .c → cc → executable` pipeline with `cg_program → emit .o → link → executable`
- The compiler would produce ELF relocatable objects (`.o` files) containing x86-64 machine code
- A minimal linker pass (or shelling out to `ld`) combines objects with the C runtime and libc

**Scope of work:**
1. **x86-64 instruction encoder** — emit machine code bytes for the operations MIR uses: integer arithmetic, comparisons, branches, function calls (System V ABI), loads/stores, stack frame management. The MIR instruction set is small (~20 statement kinds + ~5 terminators), so the x86-64 subset needed is bounded.
2. **Register allocator** — map MIR locals to x86-64 registers. A simple linear scan allocator would work initially; the MIR is already in SSA-like form with explicit locals.
3. **ELF object writer** — emit the ELF header, `.text` section (machine code), `.data`/`.rodata` sections (string literals, constants), `.symtab` (function symbols), and `.rela.text` (relocations for function calls and data references). The format is well-documented and the subset needed for relocatable objects is modest.
4. **Linker integration** — either shell out to `ld` (like the current `cc` invocation) or implement a minimal static linker that combines the generated `.o` with `libc.a` and the Glyph runtime.

**What makes this tractable:**
- MIR is already a flat CFG with explicit locals — it maps naturally to machine code
- The C codegen already handles ABI, calling conventions, and data layout — those decisions carry over
- Glyph programs use a small set of types (GVal/i64, pointers, floats) — no complex type-directed instruction selection
- The runtime is already C code that can be pre-compiled to `.o` once and linked statically

**What makes this hard:**
- x86-64 variable-length encoding is fiddly (REX prefixes, ModR/M, SIB bytes)
- Relocations must be correct for the linker to resolve cross-function calls
- Debugging support (DWARF) is a rabbit hole — could be deferred entirely
- The C runtime (Boehm GC, libc) still needs linking against system libraries

**Payoff:** Build times drop by the full `cc` overhead (~6s wall on a full rebuild). The compiler becomes a single binary with zero toolchain dependencies — no `gcc`, no `clang`, no `llc`. Distribution simplifies to one file. The bootstrap chain could eventually eliminate the Rust compiler too, if the ELF backend can compile `glyph.glyph` directly.

### 39. WebAssembly backend
A WASM target would expand Glyph's deployment story from "Linux CLI binaries" to browser, edge, serverless, and WASI environments. Two implementation paths:

**Path A: C codegen → Emscripten** — the quick path. The C backend already produces portable C. Passing it through `emcc` instead of `cc` produces WASM. Main work: adapt the runtime (no `mmap`, no signals, Boehm GC needs WASM support or replacement), handle Emscripten's async patterns for I/O, and adjust the string/memory model for WASM's linear memory.

**Path B: Direct WASM emission** — emit `.wasm` binary directly from MIR. WASM's instruction set is a stack machine, which is a straightforward lowering from MIR's flat CFG. WASM's type system is simple (i32, i64, f32, f64, funcref, externref). The binary format is well-specified and simpler than ELF. No external toolchain needed.

**What this enables:**
- Glyph programs running in web browsers (compiled to WASM, loaded via JavaScript)
- Serverless deployment (Cloudflare Workers, Fastly Compute, WASI runtimes like Wasmtime)
- Sandboxed execution — WASM's memory safety guarantees make it safe to run untrusted Glyph programs
- Plugin systems — host applications load `.wasm` modules produced by Glyph

**Key challenges:**
- Garbage collection — Boehm GC won't work in WASM without significant porting. WASM GC proposal is still maturing. Practical options: reference counting, arena allocation, or compiling Boehm to WASM via Emscripten (Path A handles this).
- I/O model — WASM has no direct file/network access. WASI provides a capability-based I/O interface. The runtime's `read_file`/`write_file`/`println` would need WASI backends.
- 32-bit pointers — WASM is 32-bit address space (wasm64 is experimental). GVal as `intptr_t` would be 32 bits, halving the available payload. Fat pointers and array headers shrink accordingly.

### 40. Bytecode interpreter
Instead of always compiling to native code, interpret MIR directly. This creates an instant-feedback execution mode with no compile step — the compiler reads a definition from the database, lowers to MIR, and evaluates it immediately.

**What this enables:**
- **REPL / eval** — `glyph eval program.glyph "1 + 2"` with sub-millisecond response. No temp files, no `cc` invocation.
- **Fast test iteration** — `glyph test --interp` runs tests by interpreting MIR, skipping the entire codegen→compile→link cycle. For the common case of changing 1-5 definitions and running their tests, this would be near-instant.
- **Debugging** — step through MIR blocks, inspect locals, set breakpoints at the definition level. The interpreter has full visibility into program state.
- **Compile-time evaluation** (#26) — `const` definitions could be evaluated by the interpreter during compilation, enabling computed lookup tables and static assertions.
- **Portable execution** — MIR interpretation requires no platform-specific code. A `.glyph` database becomes directly executable on any platform with the Glyph binary, no cross-compilation needed.

**Implementation outline:**
1. **MIR evaluator** — walk blocks, execute statements (use/binop/call/field/index/construct), follow terminators (goto/branch/return). Locals are a value array indexed by local ID. The MIR is already designed for this — flat CFG, explicit operands, no implicit state.
2. **Value representation** — tagged union: Int(i64), Float(f64), Str(String), Bool(bool), Array(Vec), Record(fields), Closure(fn_name, captured_values), Enum(tag, payload). Richer than GVal but necessary for correct interpretation without type erasure.
3. **Runtime function dispatch** — the ~40 runtime functions (`println`, `str_concat`, `array_push`, etc.) need interpreter implementations. These are thin wrappers around host functionality.
4. **FFI boundary** — extern calls can't be interpreted. Options: (a) compile FFI-using functions to native and call out, (b) restrict interpreter to pure Glyph code, (c) provide a dlopen-based FFI bridge.

**Effort:** Medium-large. The core evaluator (statements + terminators) is probably ~30-40 definitions. Runtime function wrappers are another ~40. The MIR data structures already exist — this is writing an evaluator over them.

### 41. Metaprogramming / compile-time code generation
Compile-time code generation written in Glyph itself. Since programs are SQLite databases, macros can do something no file-based macro system can: query the program's own structure, generate definitions, and insert them — all using SQL.

**Concept:**
A `macro` definition kind that runs at compile time. The macro receives the program's database handle and can:
- Query existing definitions (`SELECT name, body FROM def WHERE ...`)
- Generate new definitions (`INSERT INTO def ...`)
- Transform existing definitions (read body → modify → write back)
- Access the dependency graph, type information, and metadata

```
-- A macro that generates accessor functions for all fields of a type
macro gen_accessors db type_name =
  fields = db_query_rows(db, "SELECT body FROM def WHERE name = '" + type_name + "' AND kind = 'type'")
  -- parse the type body, extract field names
  -- for each field, insert a `get_<field>` function definition
```

**Why this is different from other macro systems:**
- **Database-native** — macros operate on SQL, not syntax trees or token streams. The program's structure is already queryable. No special AST API needed.
- **Definition-granular** — macros produce whole definitions, not inlined code fragments. This fits the "unit of storage is the definition" model.
- **Incremental** — generated definitions are stored in the database with a `generated_by` provenance column. Re-running the macro only regenerates stale outputs (content-hash comparison).
- **Debuggable** — generated definitions are visible in the database, can be inspected with `glyph get`, tested individually, and have full dependency tracking.

**Use cases:**
- Derive patterns: auto-generate `eq`, `to_str`, `hash` for record types
- Serialization: generate JSON encoders/decoders from type definitions
- FFI bindings: parse a C header and generate extern declarations + wrapper functions
- Boilerplate elimination: generate repetitive dispatch tables, test scaffolding, or CLI argument parsers from declarative specifications

### 42. Multi-agent concurrent development
SQLite's WAL mode + Glyph's definition-level granularity enable something no file-based language supports: multiple LLM agents working on the same program simultaneously with conflict resolution at the definition level rather than the line level.

**The problem with file-based concurrency:**
Two agents editing the same file produce merge conflicts at the text level — even if they modified completely independent functions. Git's line-based merge has no semantic understanding. In practice, concurrent work on a single codebase requires careful coordination to avoid conflicts.

**What the database model enables:**
- Each agent works on different definitions (rows in `def` table)
- SQLite WAL mode allows concurrent readers with a single writer
- Conflicts are detectable at the definition level: two agents modified `foo` → conflict; one modified `foo` while the other modified `bar` → clean merge
- The dependency graph (`dep` table) can automatically identify which definitions are safe to work on concurrently (no shared dependents)

**Architecture:**
1. **Work partitioning** — given a task (e.g., "refactor the type checker"), use the dependency graph to identify independent subgraphs that can be assigned to different agents
2. **Locking protocol** — agents claim definitions before modifying them (a `locked_by` column or a separate `locks` table). Advisory, not mandatory — the database enforces consistency at write time.
3. **Merge protocol** — when agents complete their work, validate that no conflicting writes occurred. If they did, present the conflict at the definition level (old body vs. agent A's body vs. agent B's body) for resolution.
4. **Orchestrator** — a coordinator process that assigns work, monitors progress, and merges results. Could be another Glyph program using the MCP tools.

**Why this matters:**
Large refactoring tasks (like the recent 179-definition negative literal migration) are inherently parallelizable at the definition level. An orchestrator could partition the work across N agents, each handling a subset of definitions, and merge the results — turning a serial hour-long task into a parallel minutes-long one.

See [multi-agent-concurrency.md](multi-agent-concurrency.md) for full analysis: empirical namespace coupling data, the compare-and-swap protocol, the Squeak Trunk-inspired changeset system, and the interface freeze protocol.

### 43. Library expansion

13 libraries ship today (stdlib, scan, json, regex, network, web, async, thread, x11, cairo, gtk, xml, svg) providing 662 functions total. Gaps identified from examples that had to build their own FFI wrappers for missing functionality.

#### ~~43.1. math.glyph — Standard math functions (HIGH PRIORITY)~~

`sin`, `cos`, `sqrt`, `atan2`, `pow`, `log`, `exp`, `fabs`, `floor`, `ceil`, `round`. The vie example (GTK vector editor) wrote its own 5-line FFI wrapper for these. Every graphics, game, physics, or scientific program needs them.

**Implementation:** Thin `math_ffi.c` wrapping `<math.h>`, link with `-lm`. Each function takes/returns floats via the `_glyph_i2f`/`_glyph_f2i` bitcast helpers already in the runtime. ~15 functions, ~20 lines of C, ~15 Glyph wrapper definitions.

**Functions:**
- Trigonometry: `sin`, `cos`, `tan`, `asin`, `acos`, `atan2`
- Powers/roots: `sqrt`, `pow`, `exp`, `log`, `log2`, `log10`S
- Rounding: `floor`, `ceil`, `round`, `fabs`
- Constants: `pi`, `e` (as zero-arg functions or consts)

#### 43.2. time.glyph — Time and timing (HIGH PRIORITY)

Timestamps, monotonic clocks, and delays. The benchmark example wrote `bench_now_ns`/`bench_elapsed_us`, gstats wrote a `time()` wrapper. Needed for benchmarking, logging, rate limiting, animation loops, and any time-stamped output.

**Implementation:** `time_ffi.c` wrapping `<time.h>` and `<unistd.h>`. ~8 functions, ~30 lines of C.

**Functions:**
- `clock_ns()` → monotonic nanoseconds (`clock_gettime(CLOCK_MONOTONIC)`)
- `timestamp()` → Unix epoch seconds (`time()`)
- `timestamp_ms()` → epoch milliseconds (`gettimeofday`)
- `sleep_ms(n)` → sleep for N milliseconds (`usleep`)
- `format_time(epoch)` → human-readable string (`strftime`)

#### 43.3. os.glyph — Environment and process (HIGH PRIORITY)

The gstats example had to FFI `getenv` manually. Every real program that reads configuration, checks its environment, or interacts with the OS needs at least one of these.

**Implementation:** `os_ffi.c` wrapping `<stdlib.h>`, `<unistd.h>`, `<dirent.h>`. ~12 functions, ~50 lines of C.

**Functions:**
- Environment: `getenv(name)`, `setenv(name, val)`
- Process: `getpid()`, `getcwd()`
- Filesystem: `readdir(path)` → array of filenames, `is_dir(path)`, `mkdir(path)`, `remove(path)`
- System: `system(cmd)` (already in runtime as `glyph_system`, but not exposed as a library function)

#### 43.4. csv.glyph — CSV parsing and generation (MEDIUM PRIORITY)

The most common data exchange format. Pairs with json.glyph for data interchange. Pure Glyph implementation — no FFI needed, built on scan.glyph combinators.

**Implementation:** ~25-30 definitions. Parser handles quoting (RFC 4180), configurable delimiter, header row extraction.

**Functions:**
- `csv_parse(text)` → array of rows (each row is array of strings)
- `csv_parse_headers(text)` → `{headers: [S], rows: [[S]]}`
- `csv_emit(rows)` → CSV string with proper quoting
- `csv_emit_headers(headers, rows)` → CSV with header row
- Options: `csv_parse_delim(text, delim)` for TSV or other separators

#### 43.5. sdl.glyph — SDL2 bindings (MEDIUM PRIORITY)

The asteroids example wrote a 200-line FFI wrapper for SDL2 (window, rendering, keyboard, gamepads, timing). SDL2 is the standard cross-platform library for games and interactive graphics — it provides windowing, 2D rendering, input handling, and audio without X11 low-level work.

**Implementation:** `sdl_ffi.c` wrapping SDL2 (`-lSDL2`). ~40-50 Glyph definitions.

**Functions:**
- Window: `sdl_init(w, h, title)`, `sdl_quit()`, `sdl_present()`
- Rendering: `sdl_clear(r, g, b)`, `sdl_draw_rect(x, y, w, h)`, `sdl_draw_line(x1, y1, x2, y2)`, `sdl_fill_rect(...)`, `sdl_set_color(r, g, b, a)`
- Input: `sdl_poll_event()`, `sdl_key_pressed(key)`, `sdl_mouse_pos()`
- Timing: `sdl_ticks()`, `sdl_delay(ms)`
- Gamepad: `sdl_gamepad_open(idx)`, `sdl_gamepad_axis(pad, axis)`, `sdl_gamepad_button(pad, btn)`

#### 43.6. ncurses.glyph — Terminal UI (MEDIUM PRIORITY)

The gled text editor hand-rolled ncurses FFI. Any terminal-based application (TUI dashboards, interactive tools, text editors, file managers) needs this.

**Implementation:** `ncurses_ffi.c` wrapping `<ncurses.h>` (`-lncurses`). ~30 Glyph definitions.

**Functions:**
- Init: `nc_init()`, `nc_end()`, `nc_raw()`, `nc_noecho()`, `nc_keypad()`
- Output: `nc_mvprint(y, x, str)`, `nc_clear()`, `nc_refresh()`, `nc_attron(attr)`, `nc_attroff(attr)`
- Input: `nc_getch()`, `nc_has_key()`
- Window: `nc_lines()`, `nc_cols()`, `nc_color_init()`, `nc_color_pair(fg, bg)`
- Cursor: `nc_move(y, x)`, `nc_curs_set(visibility)`

#### 43.7. sqlite.glyph — SQLite as a data store (MEDIUM PRIORITY)

Glyph programs *are* SQLite databases, but programs that want to use SQLite as a data store (separate from themselves) currently use raw externs. The glint example does this. A proper library would provide a clean API.

The runtime already has `db_open`, `db_close`, `db_exec`, `db_query_rows`, `db_query_one` — but they're only available through the `extern_` table. A library would wrap these with ergonomic helpers.

**Functions:**
- Core: `sql_open(path)`, `sql_close(db)`, `sql_exec(db, query)`, `sql_query(db, query)` → array of row arrays
- Helpers: `sql_query_one(db, query)` → single value, `sql_insert(db, table, record)`, `sql_table_exists(db, name)`
- Parameterized: `sql_exec_params(db, query, params)` (prevents SQL injection)

#### 43.8. toml.glyph — TOML configuration files (LOWER PRIORITY)

Configuration file format. Increasingly standard (Cargo.toml, pyproject.toml). More structured than JSON for human-readable config. Pure Glyph implementation using scan.glyph.

**Implementation:** ~40-50 definitions. TOML is more complex to parse than CSV (nested tables, inline tables, arrays of tables, datetime types) but scan.glyph provides the combinators needed.

#### 43.9. test.glyph — Extended testing utilities (LOWER PRIORITY)

Testing utilities beyond the built-in `assert`/`assert_eq`/`assert_str_eq`. The existing test framework is minimal — this library would add structure.

**Functions:**
- Assertions: `assert_ne`, `assert_true`, `assert_false`, `assert_gt`, `assert_lt`, `assert_contains(arr, elem)`, `assert_str_contains(haystack, needle)`
- Property-based: `gen_int_range(lo, hi)`, `gen_str(len)`, `gen_array(n, gen)`, `gen_one_of(arr)`, `check_property(name, gen, prop)` — generate random inputs, verify predicate holds
- Output: `test_group(name)` for structured test organization

#### 43.10. log.glyph — Structured logging (LOWER PRIORITY)

Structured logging with levels and timestamps. Depends on time.glyph.

**Functions:**
- `log_debug(msg)`, `log_info(msg)`, `log_warn(msg)`, `log_error(msg)`
- `log_set_level(level)` — filter by severity
- `log_to_file(path)` — redirect output
- Each log entry includes: timestamp, level, message, optional key-value fields

### 44. Cross-platform portability strategy (follow-up on #43)

Glyph currently targets Linux only. When targeting macOS and Windows, each library and FFI file has a different portability profile. This item classifies every library and proposes the strategy for cross-platform support.

#### Portability classification

**Tier 1 — Already cross-platform (no work needed):**

These are pure Glyph or depend only on standard C (`<stdlib.h>`, `<stdio.h>`, `<string.h>`, `<math.h>`). They work on any platform with a C compiler.

| Library | Reason |
|---------|--------|
| stdlib.glyph | Pure Glyph |
| json.glyph | Pure Glyph |
| xml.glyph | Pure Glyph |
| svg.glyph | Pure Glyph |
| scan.glyph | `scan_ffi.c` uses only standard C (character sets, string ops) |
| regex.glyph | Pure Glyph |
| csv.glyph (proposed) | Pure Glyph |
| toml.glyph (proposed) | Pure Glyph |
| test.glyph (proposed) | Pure Glyph |
| math.glyph (proposed) | `<math.h>` is standard C, `-lm` on all platforms |
| sqlite.glyph (proposed) | SQLite is cross-platform |

**Tier 2 — Portable with `#ifdef` in FFI (same Glyph API, platform-conditional C):**

The Glyph-side API stays identical. The `*_ffi.c` file uses preprocessor guards to select the right system call per platform. Since Glyph's C codegen goes through `cc`/`gcc`/`clang`, `#ifdef` is free.

| Library | Linux | macOS | Windows |
|---------|-------|-------|---------|
| time.glyph (proposed) | `clock_gettime(CLOCK_MONOTONIC)`, `usleep` | `clock_gettime` (available since macOS 10.12), `usleep` | `QueryPerformanceCounter`, `Sleep` |
| os.glyph (proposed) | `getenv`, `readdir`, `getcwd`, `getpid` | Same (POSIX) | `getenv`/`_getcwd`/`GetCurrentProcessId`/`FindFirstFile` |
| thread.glyph | pthreads | pthreads | Win32 threads (`CreateThread`, `InitializeCriticalSection`) or pthreads-win32 |
| network.glyph | POSIX sockets | POSIX sockets | Winsock2 (`WSAStartup` init, `closesocket` instead of `close`, `ws2_32.lib`) |
| log.glyph (proposed) | Depends on time.glyph | Same | Same |

Example pattern for `time_ffi.c`:

```c
#ifdef _WIN32
#include <windows.h>
long long glyph_clock_ns(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (long long)(count.QuadPart * 1000000000LL / freq.QuadPart);
}
void glyph_sleep_ms(long long ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
#include <unistd.h>
long long glyph_clock_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
void glyph_sleep_ms(long long ms) { usleep(ms * 1000); }
#endif
```

**Tier 3 — Cross-platform via third-party library:**

These wrap a library that is itself cross-platform. The Glyph API is the same on all platforms; the FFI links against the third-party library, which handles platform differences internally.

| Library | Underlying library | Platforms |
|---------|-------------------|-----------|
| sdl.glyph (proposed) | SDL2 (`-lSDL2`) | Linux, macOS, Windows, more |
| ncurses.glyph (proposed) | ncurses (Linux/macOS) / PDCurses (Windows) | All 3 (API-compatible) |
| cairo.glyph | Cairo (`-lcairo`) | Linux, macOS, Windows |
| gtk.glyph | GTK4 (`-lgtk-4`) | Linux (native), macOS/Windows (works but non-native) |

SDL2 is the strongest cross-platform story here — it provides windowing, 2D rendering, input, audio, and timing on all platforms with a single API. For graphical programs, `sdl.glyph` replaces the need for platform-specific window management.

**Tier 4 — Inherently platform-specific (no portability expected):**

These bind directly to OS-specific APIs. They don't port — instead, equivalent libraries exist per platform.

| Library | Platform | Equivalent on other platforms |
|---------|----------|-------------------------------|
| x11.glyph | Linux/BSD | Cocoa (macOS), Win32 (Windows), or SDL2 (all) |
| async.glyph | Linux (epoll) | kqueue (macOS), IOCP (Windows) |

For async, the options are:
1. **`#ifdef` in `async_ffi.c`** — epoll (Linux) / kqueue (macOS) / IOCP (Windows) behind the same Glyph API. Feasible because the Glyph-level API (`async_spawn`, `async_await`, channels) is abstract enough.
2. **libuv** — wrap libuv instead, which already abstracts epoll/kqueue/IOCP. Single FFI file, cross-platform.

#### Recommended strategy

**For now:** `#ifdef` in FFI files (Option A). It's the pragmatic C-ecosystem approach, requires no Glyph-level changes, and the C compiler handles it automatically. Each `*_ffi.c` file gains platform blocks as needed. Most FFI files are small (30-200 lines of C), so the `#ifdef` overhead is manageable.

**Long-term:** A platform abstraction layer (Option C) — a single `platform_ffi.c` that provides the lowest-level OS primitives (monotonic clock, sleep, env vars, threads, mutexes, sockets, filesystem ops, event polling) behind a uniform C API. All other library FFI files call into `platform_ffi.c` instead of system headers directly. Only `platform_ffi.c` needs porting per platform; everything above it is platform-agnostic.

This is how SDL2, libuv, and Go's runtime work: one carefully ported platform layer, everything else builds on top.

**For GUI:** Don't try to port x11.glyph. Use sdl.glyph as the cross-platform graphics answer. Keep x11.glyph, gtk.glyph, and cairo.glyph as Linux-specific options for programs that want native integration.

#### Impact on the compiler runtime

The compiler's own C runtime (`cg_runtime_c`) also has platform dependencies:

| Runtime feature | Linux | macOS | Windows |
|----------------|-------|-------|---------|
| Signal handlers (SIGSEGV/SIGFPE) | `<signal.h>` | Same | Structured Exception Handling (SEH) or `signal.h` (limited) |
| Stack traces | `backtrace()` (`<execinfo.h>`) | Same | `CaptureStackBackTrace` |
| Boehm GC | Works | Works | Works (needs configuration) |
| `mmap` (if used) | `<sys/mman.h>` | Same | `VirtualAlloc` |

The runtime is small enough (~300 lines) that `#ifdef` blocks are sufficient. The Boehm GC already handles its own platform abstraction — it builds on Linux, macOS, and Windows.

#### Build system considerations

When targeting non-Linux platforms, the build system needs to:
- Select the right C compiler (`cc`/`gcc` on Linux, `clang` on macOS, `cl.exe` or `gcc` on Windows)
- Pass platform-appropriate flags (`-lm` on Unix, `-lws2_32` on Windows for networking)
- Handle library discovery (`pkg-config` on Linux, `brew --prefix` on macOS, vcpkg/manual paths on Windows)

The `cc_prepend` and `cc_args` meta keys already support custom compiler flags per program. A `--target` flag (#16) would formalize this, selecting the compiler and flags automatically based on the target triple.

### 45. Array destructuring in patterns

Pattern matching on arrays by positional element binding, with an optional rest pattern for the tail (Elixir-style `| rest`). This eliminates index-based array access (`arr[0]`, `arr[1]`, ...), which is one of the most error-prone patterns for LLMs — off-by-one errors, missing bounds checks, and unclear intent.

**Syntax:**

```
-- Bind first elements, ignore rest
[first, second] = arr

-- Bind with rest/spread
[head | tail] = arr

-- In match expressions
match tokens
  [] -> "empty"
  [only] -> "single: {only}"
  [first, second, | rest] -> "multi: {first}, {second} + {len(rest)} more"
```

**Semantics:**

- `[a, b] = arr` binds `a = arr[0]`, `b = arr[1]`. If `arr` has fewer than 2 elements, runtime panic (consistent with current out-of-bounds behavior).
- `[a, b, | rest] = arr` binds `a = arr[0]`, `b = arr[1]`, `rest = str_slice(arr, 2, len(arr))` (a new array containing the remaining elements). Requires `len(arr) >= 2`.
- `[]` in a match pattern matches only empty arrays (`len(arr) == 0`).
- `[x]` matches arrays of exactly length 1.
- `[x, | rest]` matches arrays of length >= 1.
- `[x, y, | rest]` matches arrays of length >= 2.
- Fixed-length patterns (`[a, b, c]`) match arrays of exactly that length.
- Patterns without `| rest` are exact-length matches; patterns with `| rest` are minimum-length matches.

**Current workaround (what this replaces):**

```
-- Today: error-prone index arithmetic
process tokens =
  match len(tokens) >= 2
    true ->
      cmd = tokens[0]
      arg = tokens[1]
      rest = slice(tokens, 2, len(tokens))
      handle(cmd, arg, rest)
    _ -> "need at least 2 tokens"
```

```
-- With array destructuring:
process tokens =
  match tokens
    [cmd, arg, | rest] -> handle(cmd, arg, rest)
    _ -> "need at least 2 tokens"
```

**Implementation outline:**

1. **Parser** — extend `parse_single_pattern` to recognize `[` as an array pattern start. Parse comma-separated sub-patterns inside brackets. If `| ident` is encountered after an element, record it as the rest binding (Elixir-style). Produce a new pattern kind (`pat_array` or similar) storing the element patterns and optional rest name.

2. **MIR lowering** — `lower_match_arms` gets a new case for array patterns. Emit:
   - Length check: `len(subject) == N` for fixed patterns, `len(subject) >= N` for rest patterns
   - Element bindings: `local_i = subject[i]` for each positional element
   - Rest binding (if present): `rest = slice(subject, N, len(subject))`
   - Branch: if length check fails, fall through to next arm

3. **Let destructuring** — extend `parse_stmt` to recognize `[` as array destructuring (currently only `{` triggers record destructuring). Same MIR lowering: length check + indexed access + optional rest slice.

4. **Type inference** — array patterns constrain the subject to array type `[T]`. Each element sub-pattern unifies with `T`. The rest binding has type `[T]`.

**Scope:** ~10-15 new/modified definitions. New parser node, new MIR lowering path, extended let-destructuring, type checker support. No runtime changes needed — uses existing `array_bounds_check`, indexing, and `slice`.

### 46. Range patterns in match

Match on contiguous integer ranges instead of enumerating every value with or-patterns. Reduces token count proportional to range size and makes intent explicit — "any value from 48 to 57" clearly communicates "ASCII digit" in a way that `48 | 49 | 50 | 51 | 52 | 53 | 54 | 55 | 56 | 57` does not.

**Syntax:**

```
match c
  #a..#z -> "lowercase"
  #A..#Z -> "uppercase"
  #0..#9 -> "digit"
  _ -> "other"
```

The `..` operator creates an inclusive range pattern. Both bounds must be integer literals (or char literals like `#a`, which are integer literals). This is a pattern-only construct — `..` does not create range values at the expression level.

**More examples:**

```
-- HTTP status code classification
classify status =
  match status
    200..299 -> "success"
    300..399 -> "redirect"
    400..499 -> "client error"
    500..599 -> "server error"
    _ -> "unknown"

-- Combined with or-patterns and guards
categorize n =
  match n
    0 -> "zero"
    1..9 -> "single digit"
    10..99 -> "double digit"
    _ ? n < 0 -> "negative"
    _ -> "large"
```

**Current workaround (what this replaces):**

```
-- Today: long or-pattern chains
match c
  48 | 49 | 50 | 51 | 52 | 53 | 54 | 55 | 56 | 57 -> "digit"
  _ -> "other"

-- Or guard-based (loses pattern matching benefits):
match c
  _ ? c >= 48 && c <= 57 -> "digit"
  _ -> "other"
```

```
-- With range patterns:
match c
  #0..#9 -> "digit"
  _ -> "other"
```

**Semantics:**

- `a..b` matches any integer `x` where `a <= x <= b` (inclusive on both ends).
- If `a > b`, the pattern matches nothing (empty range — could be a compile-time warning).
- Both bounds must be compile-time integer constants (literals or char literals). No variable bounds.
- Range patterns can be combined with or-patterns: `1..5 | 10..15 -> body`.
- Range patterns participate in exhaustiveness: `0..255` with a `_` fallback covers all byte values.

**Implementation outline:**

1. **Parser** — in `parse_single_pattern`, after parsing an integer/char literal, check for `..` token. If present, parse the upper bound literal. Produce a new pattern kind (`pat_range`) storing the lower and upper bounds.

2. **MIR lowering** — `lower_match_arms` gets a new case for range patterns. Emit:
   - Two comparisons: `subject >= lower` and `subject <= upper`
   - Branch: if both true, enter the arm body; otherwise fall through
   - This compiles to: `tm_branch(subject >= lower, check_upper, next_arm)` then `tm_branch(subject <= upper, arm_body, next_arm)`

3. **Tokenizer** — `..` needs to be recognized as a token. Check for conflicts with existing syntax: `.` is field access. `..` is unambiguous — single `.` followed by a non-`.` character is field access, `..` is range. No `...` token needed since #45 uses `|` (Elixir-style) for rest patterns.

4. **Type inference** — range patterns constrain the subject to integer type. Both bounds must be integers (enforced by parser limiting to integer/char literals).

**Scope:** ~8-12 new/modified definitions. New token kind, new parser pattern case, new MIR lowering path. No runtime changes — compiles to existing comparison and branch operations.

### 47. Structured runtime error output

When a Glyph program crashes at runtime, the error output is human-formatted: a prose message, a function name, and a stack trace. An LLM consuming this output has to parse unstructured text to diagnose the problem. If runtime errors emitted structured JSON, an LLM could programmatically identify the failure, locate the relevant definition, and generate a fix.

**Current output:**
```
segfault in parse_atom
--- stack trace ---
  parse_atom
  parse_unary
  parse_expr
  main
```

**Structured output:**
```json
{
  "error": "segfault",
  "function": "parse_atom",
  "signal": "SIGSEGV",
  "stack": ["parse_atom", "parse_unary", "parse_expr", "main"]
}
```

**Additional structured errors:**
```json
{"error": "index_out_of_bounds", "function": "process_row", "index": 5, "length": 3}
{"error": "non_exhaustive_match", "function": "dispatch", "subject_value": "unknown_cmd"}
{"error": "unwrap_none", "function": "find_user", "context": "Optional was None"}
```

This pairs with #1 (structured type errors at compile time) to give LLMs machine-readable diagnostics across the full compile→run cycle. The crash handler already captures function names via `_glyph_current_fn` and the stack trace via `backtrace()` — the change is formatting, not data collection.

**Implementation:** Modify the SIGSEGV/SIGFPE handlers, `array_bounds_check`, `tm_unreachable` trap, and `panic` in `cg_runtime_c` to emit JSON when an environment variable (`GLYPH_JSON_ERRORS=1`) or build flag is set. Default remains human-readable for terminal use. The MCP `run` tool could set this automatically since its consumer is always an LLM.

**Scope:** ~5-8 definitions modified in runtime codegen. No language changes.

### 48. Guaranteed tail call optimization

Glyph programs use recursive `loop` patterns instead of explicit loop constructs:

```
count_loop i n =
  match i >= n
    true -> i
    _ -> count_loop(i + 1, n)
```

The `tco_` namespace in the compiler implements tail call optimization, converting self-tail-calls into C `while` loops. But TCO eligibility is silent — if a function is *not* TCO-eligible (e.g., the recursive call isn't in tail position, or there's work after the call), it compiles to actual recursion and will stack overflow on large inputs. The programmer (LLM) gets no feedback about whether its loop pattern is safe.

**Two improvements:**

1. **Compile-time warning for non-TCO-eligible recursive calls** — if a function calls itself but the call isn't in tail position, emit a warning: `"recursive call in count_loop is not in tail position (line 4); may stack overflow on large inputs"`. This catches the common LLM mistake of accidentally adding work after a recursive call:

```
-- TCO-eligible (tail position):
loop(i + 1, n)

-- NOT TCO-eligible (addition after recursive call):
1 + loop(i + 1, n)
```

2. **Mutual tail call optimization** — currently TCO only handles self-recursion. Two mutually recursive functions (`is_even` calls `is_odd`, `is_odd` calls `is_even`) don't get optimized. Mutual TCO via trampoline or loop fusion would handle this pattern.

The warning (#1) is high-value and low-cost. Mutual TCO (#2) is nice-to-have.

**Scope:** Warning: ~3-5 definitions (detect self-calls not in tail position during MIR lowering, emit diagnostic). Mutual TCO: ~10-15 definitions (trampoline codegen or loop merging).

### 49. Definition-level profiling (`glyph profile`)

Item #19 profiled the compiler using external tools (`perf`). A built-in profiling mode would let any Glyph program self-report performance data at the definition level — which functions are hot, how many times they're called, and where time is spent.

```bash
glyph build program.glyph --profile
./program
# on exit, writes program.profile (TSV or JSON)

glyph profile program.glyph
# reads .profile, displays report
```

**Output:**
```
Function            Calls      Time(μs)    Avg(μs)   Allocs
parse_expr          14,203     8,420       0.59      2,104
str_concat          89,012     6,110       0.07      89,012
array_push          45,891     3,200       0.07      12,440
tc_collect_fv       12,800     2,890       0.23      0
...
Total: 342,109 calls, 28,400μs, 103,556 allocs
```

This is the performance counterpart to `glyph cover` (which tracks which functions are *called*, not *how often* or *how long*). The instrumentation infrastructure from coverage (`#ifdef GLYPH_COVERAGE`, `atexit` handler, TSV output) can be reused — extend the per-function counter from a boolean hit flag to a call count + cumulative time.

An LLM optimizing a program needs this data to know where to focus. Without it, the LLM guesses — with it, it can target the actual hot path.

**Implementation:**

1. **Instrumentation** — at each function entry, increment a call counter and record `clock_gettime(CLOCK_MONOTONIC)`. At function exit, accumulate elapsed time. Use a global array indexed by function ID (same scheme as coverage).
2. **Report** — `atexit` handler writes TSV/JSON with function name, call count, cumulative time, and allocation count (if tracked).
3. **CLI** — `glyph profile program.glyph` reads the report file and displays sorted tables.
4. **MCP** — `profile` tool returns JSON for LLM consumption.

**Scope:** ~15-20 new definitions. Reuses coverage infrastructure. Requires `clock_gettime` in the runtime (pairs with proposed `time.glyph` library, #43.2).

### 50. Hot definition reloading

Unique to Glyph's database-as-program model: update a definition in a running program's `.glyph` database, and the program picks up the change without restarting.

**Concept:** A long-running program (web server, GUI app, game) watches its own `.glyph` database for changes. When a definition is modified (via `glyph put` or MCP), the runtime:

1. Detects the change (SQLite `data_version` pragma, or poll the `def` table's hash column)
2. Recompiles the dirty definition to a shared object (`.so`)
3. Loads it via `dlopen` and swaps the function pointer

This means an LLM can modify a server's request handler, a game's physics function, or a GUI's event callback — and see the effect immediately without losing application state (open connections, in-memory data, window position).

**Why Glyph is uniquely suited:** The program *is* the database. There's no build system, no file watching, no "save and rebuild" — the definition table is the source of truth, and SQLite provides change detection for free. No other language can do this as naturally because no other language stores its source in a queryable, mutable database.

**Architecture:**

1. **Watcher thread** — polls `PRAGMA data_version` periodically (or uses `sqlite3_update_hook` for push-based notification). When the version changes, query `v_dirty` for changed definitions.
2. **Incremental compiler** — compile only the dirty definitions to a temporary `.so` via the C codegen pipeline. This is a subset of what `glyph build` already does.
3. **Hot swap** — `dlopen` the new `.so`, resolve the new function symbol, update the program's function pointer table. Old code is `dlclose`d after all in-flight calls complete.
4. **Safety** — type signature changes require a full restart (the caller's assumptions about argument count/types may be invalid). Body-only changes within the same signature are safe to hot-swap.

**Limitations:**
- Only works for functions, not types (struct layout changes can't be hot-swapped)
- Requires `dlopen` support (#23)
- Global state may become inconsistent if the swapped function has different assumptions
- Closures capturing the old function pointer won't see the update

**Scope:** Large — ~30-40 definitions (watcher, incremental compiler, loader, pointer table). Depends on #23 (dlopen). High payoff for interactive development workflows.

### 51. Automatic type signature catalog (`glyph doc`)

The type checker infers a complete type signature for every function, but this information is discarded after compilation. A `glyph doc` command would persist and expose inferred signatures as API documentation — the LLM equivalent of reading a library's header file.

```bash
glyph doc program.glyph
```

**Output:**
```
map       : (a -> b) -> [a] -> [b]
filter    : (a -> B) -> [a] -> [a]
fold      : (b -> a -> b) -> b -> [a] -> b
str_split : S -> S -> [S]
db_query  : I -> S -> [[S]]
```

**Structured output (JSON, for MCP):**
```json
[
  {"name": "map", "signature": "(a -> b) -> [a] -> [b]", "deps": ["array_push", "array_len"]},
  {"name": "filter", "signature": "(a -> B) -> [a] -> [a]", "deps": ["array_push", "array_len"]}
]
```

**Why this matters for LLMs:** When an LLM needs to use a library function, it currently reads the full body to understand the parameter types and return type. With inferred signatures persisted, it can scan the catalog and pick the right function without reading implementations. This is especially valuable for libraries like `stdlib.glyph` (65 defs) or `json.glyph` (59 defs) where reading every body is expensive.

**Implementation:**

1. **Persist signatures** — after type inference, write each function's generalized type signature to a `sig` column on the `def` table (or a separate `doc` table). The type pretty-printer already exists for error messages — reuse it.
2. **CLI** — `glyph doc program.glyph [--json] [--ns=NAME]` reads and displays signatures, optionally filtered by namespace.
3. **MCP** — `doc` tool returns signatures as JSON. Could be integrated into `list_defs` output.
4. **Incremental** — only re-infer signatures for dirty definitions (same as incremental compilation).

**Scope:** ~10-15 definitions. Type pretty-printer exists. Main work is plumbing the inferred types from the type checker to persistent storage and building the CLI/MCP output.

### 52. Streaming / lazy sequences

Functional pipelines like `range(0, 1000000) |> filter(is_prime) |> take(10)` currently materialize every intermediate array. With a million elements, that's three million-element allocations for 10 results. Lazy sequences would produce elements on demand, fusing the pipeline into a single pass.

**Concept:** A `Seq` type representing a lazy sequence — a function that produces the next element (or signals completion) each time it's called. Pipeline operations (`map`, `filter`, `take`, `fold`) return new `Seq` values without doing work until a terminal operation (`collect`, `fold`, `each`) drives the pipeline.

```
-- Lazy: no intermediate arrays, stops after 10 results
primes = seq_range(0, 1000000)
  |> seq_filter(is_prime)
  |> seq_take(10)
  |> seq_collect()

-- Infinite sequences
naturals = seq_from(0)
fibs = seq_unfold({0, 1}, \{a, b} -> {a, {b, a + b}})
```

**API:**
```
-- Constructors
seq_range   : I -> I -> Seq        -- finite range
seq_from    : I -> Seq             -- infinite counter
seq_of      : [a] -> Seq           -- from array
seq_unfold  : s -> (s -> {a, s}) -> Seq  -- from state function

-- Transformers (lazy, return Seq)
seq_map     : (a -> b) -> Seq -> Seq
seq_filter  : (a -> B) -> Seq -> Seq
seq_take    : I -> Seq -> Seq
seq_drop    : I -> Seq -> Seq
seq_zip     : Seq -> Seq -> Seq
seq_flat_map: (a -> Seq) -> Seq -> Seq

-- Terminals (eager, drive the pipeline)
seq_collect : Seq -> [a]           -- to array
seq_fold    : (b -> a -> b) -> b -> Seq -> b
seq_each    : (a -> V) -> Seq -> V
seq_any     : (a -> B) -> Seq -> B
seq_find    : (a -> B) -> Seq -> ?a
```

**Implementation:** A `Seq` is a closure — `{state, next_fn}` where `next_fn(state)` returns `Some({value, new_state})` or `None`. This is implementable today as a pure library using closures and optionals, no language changes needed. However, a library implementation would have overhead from closure allocation and indirect calls per element. A compiler-recognized `Seq` type could fuse adjacent operations into a single loop (stream fusion), eliminating intermediate closures entirely.

**Two-phase approach:**
1. **Library** (`seq.glyph`) — implement as closures. Works today. Performance is acceptable for most use cases. ~30-40 definitions.
2. **Compiler optimization** (later) — recognize `seq_*` chains and fuse them into imperative loops during MIR lowering. High effort, big payoff for tight loops.

**Scope:** Phase 1: ~30-40 library definitions, no compiler changes. Phase 2: ~20-30 compiler definitions for fusion optimization.

### 53. Error context chaining

The `?` operator propagates errors from Result types, but with no context about where in the call chain the error originated. When an LLM sees `"file not found"`, it doesn't know whether that came from loading a config file, reading a template, or importing data. Error context chaining would let each `?` site annotate the error with its purpose.

**Syntax:**

```
-- Current: bare propagation, no context
load_config path =
  data = read_file(path)?
  parsed = json_parse(data)?
  validate(parsed)?

-- With context chaining:
load_config path =
  data = read_file(path) ? "reading config from {path}"
  parsed = json_parse(data) ? "parsing config JSON"
  validate(parsed) ? "validating config schema"
```

**Result on failure:**
```
"validating config schema: parsing config JSON: reading config from /etc/app.conf: file not found"
```

Each `?` with a string argument wraps the error: if the Result is `Err(msg)`, it becomes `Err(context + ": " + msg)`. If the Result is `Ok(val)`, the context string is ignored and `val` is returned as normal.

**Ambiguity concern:** Currently `?` is a postfix operator. Adding a string after it could conflict with the next expression. However, since `?` currently either ends a statement (bare propagation) or is followed by a newline/operator, a string literal immediately after `?` is unambiguous — it can only be the context annotation.

**Semantics:**
- `expr?` (no context) — unchanged, propagates the error as-is
- `expr ? "context"` — on `Err(msg)`, returns `Err("context: " + msg)`; on `Ok(val)`, returns `val`
- Context strings support interpolation: `expr ? "loading {filename}"`
- Chaining naturally builds a trace from inner to outer context

**Implementation:**

1. **Parser** — after parsing `?` postfix, check if the next token is a string literal. If so, parse it as context.
2. **MIR lowering** — the current `?` lowers to: branch on tag, Ok → unwrap, Err → return Err. With context: Err path concatenates context string before returning.
3. **No runtime changes** — uses existing `str_concat`.

**Note:** This reuses the `? guard_expr` syntax from match guards, but the context is different — in match arms, `?` introduces a boolean guard; in expressions, `?` is error propagation. The parser already distinguishes these by position (match arm vs. expression statement).

**Scope:** ~5-8 definitions modified (parser, MIR lowering for `?` operator). Small, safe, high-value.

### 54. Build-time dependency fetching

Item #11 covers package management for linked libraries. A simpler prerequisite: `glyph use` registers a library by local path, but that path must already exist on disk. URL-based library registration would make library distribution trivial.

```bash
# Today: must manually download first
wget https://example.com/libs/json.glyph
glyph use app.glyph json.glyph

# With URL support:
glyph use app.glyph https://example.com/libs/json.glyph
```

**Why this is simpler than a package manager:** Glyph libraries are single SQLite files. There's no archive format to unpack, no manifest to parse, no dependency tree to resolve (that's #11's scope). Fetching a `.glyph` file from a URL and saving it locally is the minimal useful step — it automates the `wget` that every library consumer does manually.

**Behavior:**

1. `glyph use app.glyph <url>` downloads the `.glyph` file to a local `libs/` directory (sibling to `app.glyph`)
2. Registers the local path in the `lib_dep` table (same as today)
3. Stores the source URL in a `lib_url` column for future updates
4. `glyph update-libs app.glyph` re-fetches all URL-sourced libraries and re-links if content hash changed

**Security:** Only HTTPS URLs. Content hash verification — the downloaded file's BLAKE3 hash is stored on first fetch; subsequent fetches verify the hash matches (or warn on change). No arbitrary code execution during fetch.

**Implementation:**

1. **HTTP fetch** — the runtime already has HTTP client support via `network.glyph`. Alternatively, shell out to `curl`/`wget` (simpler, no dependency on network library).
2. **URL column** — add `url TEXT` to the `lib_dep` table schema. Migration #N.
3. **CLI** — modify `cmd_use` to detect URL arguments (starts with `https://`), download, then proceed as normal.
4. **Update command** — `glyph update-libs` iterates `lib_dep` rows with non-null URLs, re-fetches, and re-links if hash differs.

**Scope:** ~10-15 definitions. Depends on either `network.glyph` or shelling out to `curl`.

### 55. Fix silent lowering failures for parsed features

Function composition `>>` and field accessor shorthand `.field` are parsed and type-checked but produce wrong results at runtime — `>>` silently produces unit, `.field` doesn't generate correct accessor code. No error, no warning. This is a compiler correctness issue: the parser accepts syntax that the backend can't handle, and the program runs with wrong behavior instead of failing loudly.

**Current silent failures:**

- `f >> g` — parsed as `ex_compose`, type-checked, but MIR lowering produces unit. A program using `>>` compiles clean and runs — it just doesn't compose the functions.
- `.field` (point-free accessor) — parsed as a lambda `\x -> x.field`, but the generated closure may not correctly capture the field name through lowering.

**Fix:** For each parsed-but-unlowered feature, either:

1. **Complete the lowering** — implement the MIR lowering and C codegen for the feature. `>>` is straightforward: `f >> g` desugars to `\x -> g(f(x))`, which the closure infrastructure already handles.
2. **Emit a compile-time error** — if lowering is deferred, the compiler should reject the syntax with a clear message: `"function composition (>>) is not yet supported in codegen"`. Silent wrong behavior is worse than a compilation failure.

Option 1 is preferred for `>>` since the desugaring is trivial and closures work. Option 2 is the safety net for any other parsed-but-unlowered constructs.

**Scope:** ~3-5 definitions modified. Small effort, high correctness value. Audit all `ex_*` AST node kinds to verify each has a corresponding MIR lowering path, and add a fallback error for any that don't.

### 56. Recursive / inductive type definitions

Trees, linked lists, and other recursive data structures are a gap. The type checker has cycle detection (BUG-008, BUG-010) precisely because recursive types stress it — the current approach is to detect and bail out rather than support them. A deliberate recursive type system would make these structures first-class.

**What's needed:**

```
-- Currently not expressible:
type List = Nil | Cons({head: I, tail: List})
type Tree = Leaf(I) | Branch({left: Tree, right: Tree})
```

The challenge is in three places:

1. **Type inference** — HM inference with recursive types requires equi-recursive or iso-recursive handling. Equi-recursive (the type *is* its unfolding) is simpler but can cause infinite loops in the type checker — exactly the cycle bugs already encountered. Iso-recursive (explicit fold/unfold via constructors) is safer and what ML/Haskell use: constructing `Cons(1, Nil)` folds, pattern matching unfolds.

2. **Memory layout** — recursive types must be heap-allocated (a `List` can't be stack-allocated because its size is unbounded). The enum codegen already heap-allocates variants as `{tag, payload...}` — recursive types fit naturally if the recursive field is stored as a pointer (which it already is, since GVal is pointer-sized).

3. **Monomorphization** — recursive types create infinite type unfoldings if monomorphized naively. The monomorphizer needs to recognize recursive type cycles and generate a single C struct with a self-referential pointer field, rather than trying to inline the recursion.

**Implementation approach:**

- **Iso-recursive via constructors** — constructors fold, pattern match unfolds. This is the safe path that avoids equi-recursive cycle issues. The type checker doesn't need to "see through" the recursion — `Cons` produces a `List`, and matching on `List` reveals `Cons({head: I, tail: List})`.
- **Type definition analysis** — detect recursive references in type bodies during definition loading. Mark types as recursive. Thread this information into the monomorphizer.
- **Codegen** — recursive struct fields emit as pointers (`GVal` already is pointer-sized, so this may require no codegen changes at all — the recursive field is just another GVal slot).

**Scope:** ~15-25 definitions across type checker, monomorphizer, and codegen. The hardest part is getting the type checker to handle recursive type references without triggering the cycle detection that currently protects against them. Medium-large effort, but unlocks a fundamental class of data structures.

### 57. Multi-error reporting (error recovery)

The parser and type checker stop at the first error. An LLM fixing a broken definition has to compile → get one error → fix → compile → get next error → fix, in a serial loop. If the compiler reported all errors in one pass, the LLM could fix everything at once.

This is distinct from #1 (structured error messages) — #1 is about error *content*, this is about error *quantity per compilation*.

**Parser error recovery:**

After a parse error, synchronize to a recovery point (next newline at the current indentation level, or next top-level definition boundary) and continue parsing. Each error is collected into a list rather than aborting. The parser already tracks indentation depth, making recovery points detectable.

```
-- Input with two errors:
foo x =
  y = x +         -- error: unexpected end of expression
  z = bar(         -- error: expected ')'
  y + z

-- Current output: 1 error, stops at line 2
-- With recovery: 2 errors, both reported, rest of function parsed
```

**Type checker error recovery:**

When type inference fails for a subexpression, assign it `ty_error` and continue. Downstream expressions that consume `ty_error` propagate it silently (no cascading errors). Only report the *root cause* errors. The type checker already has `ty_error=99` — extend its use from "abort" to "continue with poison type."

```
-- Input with two independent type errors:
process x =
  a = x + "hello"    -- error: can't add I and S
  b = len(42)         -- error: len expects [T], got I
  a + b

-- Current output: 1 error (first one), type checking aborts
-- With recovery: 2 errors, both independent root causes reported
```

**Key design choice:** Don't report cascading errors. If `a` has type `ty_error`, then `a + b` should not produce a second "type mismatch" error — the error poisons silently through dependent expressions. Only subexpressions where the *inputs* are well-typed but the *operation* fails produce errors.

**Implementation:**

1. **Parser** — replace `return pr_err(...)` with `collect_err(...)` + synchronize. Add an `errors: [S]` field to `ParseResult` (currently just `pr_err: S`). ~8-10 definitions modified.
2. **Type checker** — replace early-return on error with `ty_error` propagation. Add `tc_errors: [S]` accumulator to the engine. Modify `unify` to short-circuit on `ty_error` inputs. ~10-12 definitions modified.
3. **CLI / MCP** — format all collected errors in the response. `check_def` MCP tool returns a JSON array of errors instead of a single string. ~3-4 definitions modified.

**Scope:** ~20-25 definitions modified across parser, type checker, and CLI. Medium effort. The parser recovery is easier; the type checker recovery requires careful handling of error propagation to avoid cascading noise.

### 58. Persistent compilation cache

The incremental compilation system tracks content hashes per definition (BLAKE3), but compiled artifacts are discarded on clean builds. A persistent cache keyed by definition hash → compiled output would make `glyph build --full` skip recompilation for any definition whose hash matches a previously compiled version.

**How it works:**

The `compiled` table already caches per-definition compilation artifacts. Currently it's invalidated by `--full` rebuilds and lost when the build process doesn't store results. A persistent cache extends this:

1. Before compiling a definition, check: does the cache have an entry for this `(name, hash, target_backend)` triple?
2. If yes, use the cached MIR/C/LLVM output directly — skip tokenize → parse → type-check → lower → codegen entirely.
3. If no, compile normally and store the result.
4. Cache eviction: LRU or size-bounded (e.g., keep last 3 versions of each definition).

**What gets cached:**

- The generated C code fragment for each function (the output of `cg_function2`)
- The generated LLVM IR fragment (the output of `ll_function`)
- Optionally, the monomorphized type specializations (the most expensive step to recompute)

**Why this matters:**

During iterative development, a typical change touches 1-5 definitions out of 1,939. The current incremental system recompiles dirty definitions and their transitive dependents. But after a `--full` rebuild (or switching branches, or bootstrapping), *everything* recompiles even though most definitions haven't changed since the last time they were compiled. A hash-keyed cache means "I compiled this exact function body before, here's the result" regardless of how the build was triggered.

**For the bootstrap chain specifically:** stages 1-3 each compile the full compiler (~1,500 fn defs). Between stages, only the compiler binary changes, not the source — a cache would make stages 2 and 3 near-instant if the definitions haven't changed.

**Implementation:**

1. **Cache table** — `CREATE TABLE compile_cache (name TEXT, hash BLOB, backend TEXT, output TEXT, mono_specs TEXT, ts INTEGER, PRIMARY KEY (name, hash, backend))`. Stored in the `.glyph` database itself or a sibling `.cache` file.
2. **Cache lookup** — before `compile_fn`, check `compile_cache` for the definition's current hash. ~2-3 definitions modified in the build pipeline.
3. **Cache store** — after `compile_fn`, insert/update the cache entry. ~2-3 definitions.
4. **Cache invalidation** — content-hash keyed, so no explicit invalidation needed. Old entries are naturally superseded. Add `glyph build --no-cache` to bypass. Size-bounded eviction keeps the database from growing unboundedly.

**Scope:** ~10-15 definitions. The main complexity is deciding what to cache (just C/LLVM fragments? or also MIR? type inference results?) and handling monomorphization correctly (a cached function's type specializations depend on its callers, which may change).

### 59. Compiler fuzzing / random program generation

The 69 smoke tests are manually written, targeting known compiler stress points. A random valid-program generator would find edge cases systematically — the CSmith/SQLsmith approach applied to Glyph.

**Concept:**

Generate random but *well-typed* Glyph programs and verify the compiler:
1. Doesn't crash (no segfaults, no panics)
2. Produces a program that runs without crashing
3. Produces deterministic output (same program → same result across C and LLVM backends)

**Generator design:**

Build programs bottom-up from a type-directed grammar:

1. **Type generation** — random types: `I`, `S`, `B`, `[I]`, `{x: I, y: S}`, `?I`, `I -> S`, nested to configurable depth.
2. **Expression generation** — given a target type, generate an expression of that type:
   - `I` → integer literal, arithmetic expression, function call returning I
   - `S` → string literal, string concat, interpolation
   - `[T]` → array literal with elements of type T
   - `{fields}` → record literal with correct field types
   - `T -> U` → lambda with parameter of type T, body of type U
3. **Function generation** — random parameter count and types, body expression matching return type.
4. **Program generation** — N random functions + a `main` that calls them and prints results.

**Differential testing:**

Compile the same program with both C and LLVM backends. Both should produce identical output. Any difference is a codegen bug. The dual-backend architecture makes this a natural testing strategy.

**Shrinking:**

When a crash-triggering program is found, minimize it: remove functions, simplify expressions, reduce nesting — find the minimal program that still triggers the bug. The property-based testing infrastructure (prop kind, shrinking) could be reused here.

**Implementation:**

This is best implemented as a Glyph program itself — a `tests/fuzz/fuzz.glyph` that:
1. Uses a deterministic PRNG (xorshift128+, already mentioned in #25)
2. Generates random ASTs using the type-directed approach
3. Pretty-prints them to Glyph source
4. Writes them to a temp `.glyph` database via `db_exec`
5. Shells out to `./glyph build` and `./glyph run` to verify

**Scope:** ~40-60 definitions for a basic generator + differential tester. The generator grows in coverage over time as more language features are added to the grammar. Could run as a CI job (`glyph fuzz --iterations 1000 --seed $RANDOM`).

### 60. `glyph watch` — continuous build mode

Auto-rebuild when the database changes. SQLite's `PRAGMA data_version` changes on every write transaction, making change detection trivial without polling file modification times.

```bash
glyph watch program.glyph              # rebuild on every change
glyph watch program.glyph --test       # rebuild + run tests on every change
glyph watch program.glyph --run        # rebuild + run on every change
```

**Why this matters for LLM workflows:**

The core LLM development loop is: `put_def` → `build` → see errors → `put_def` → `build`. Currently each `build` is a separate command. With `watch`, the LLM (or MCP client) just writes definitions and gets immediate feedback — errors appear as soon as the definition is saved. The MCP `put_def` tool could even return build results inline if `watch` is running.

**Implementation:**

1. **Change detection** — poll `PRAGMA data_version` every 100-200ms. When it changes, trigger a rebuild. SQLite guarantees `data_version` increments on any write — no false negatives.
2. **Debounce** — if multiple writes happen in quick succession (e.g., MCP `put_defs` writing 5 definitions), wait 200ms after the last write before rebuilding. Prevents redundant builds during batch operations.
3. **Output** — clear terminal, show build result (success + binary size, or error list). On `--test`, show test results after successful build. On `--run`, execute the program.
4. **Incremental** — use the existing dirty-definition tracking. Only recompile what changed since the last successful build.

**Integration with MCP:**

The `watch` process could expose a simple IPC interface (Unix socket or named pipe) that the MCP server queries: "what was the result of the last build?" This lets `put_def` return not just "definition saved" but "definition saved, build succeeded" or "definition saved, build failed: type error in foo."

**Scope:** ~15-20 definitions. Core loop is small (poll + rebuild). The debounce and MCP integration add complexity but are optional enhancements.

### 61. Foreign type declarations

Currently all extern functions traffic in `GVal` (i64). An opaque handle from `gtk_window_new` or `fopen` is just an integer — the type system can't distinguish a window handle from a file descriptor from a database connection. A `foreign type` declaration would let the type checker track these as distinct types.

```
foreign type GtkWindow
foreign type FILE
foreign type SqliteDb

extern gtk_window_new : I -> GtkWindow
extern fclose : FILE -> I
extern db_open : S -> SqliteDb
```

**Semantics:**

- A foreign type is an opaque integer-sized value. No fields, no construction from Glyph, no pattern matching.
- Foreign types are distinct from each other and from `I`. Passing a `GtkWindow` where `FILE` is expected is a type error.
- Foreign types unify only with themselves and with type variables (for generic code).
- At the C level, foreign types are still `GVal` / `intptr_t` — no runtime representation change.
- Casting between foreign types and `I` requires an explicit `unsafe_cast` or similar escape hatch.

**What this catches:**

```
-- Today: compiles fine, segfaults at runtime
win = gtk_window_new(0)
fclose(win)    -- passing a GtkWindow to fclose — wrong!

-- With foreign types: compile-time type error
-- "expected FILE but got GtkWindow in argument 1 of fclose"
```

**Implementation:**

1. **Parser** — recognize `foreign type Name` declarations. Store in the `def` table with a new kind or in a `foreign_type` table.
2. **Type checker** — add a `ty_foreign` tag (e.g., tag=16) to TyNode, carrying the type name. Foreign types unify only with themselves (same name) or type variables. No structural equality — `foreign type A` and `foreign type B` are always distinct even if both are opaque.
3. **Extern declarations** — extend the `extern_` table or extern syntax to reference foreign type names instead of just `I`/`S`/etc.
4. **Codegen** — no changes. Foreign types are `GVal` at the C level. The type safety is purely compile-time.

**Scope:** ~10-15 definitions across parser, type checker, and extern handling. No runtime changes. Medium effort. Pairs naturally with #23 (dlopen) and the growing FFI library ecosystem (gtk, network, async, thread all use opaque handles).

### 62. Library test coverage requirements

network.glyph (34 defs) and gtk.glyph (41 defs) have **zero tests**. cairo.glyph, svg.glyph, xml.glyph, and the new math.glyph have minimal or no testing. Of 14 shipped libraries, only stdlib.glyph (49 tests) and json.glyph have substantial test suites.

**The problem:** Untested libraries break silently when the compiler changes. The recent monomorphization work, codegen fixes, and type checker hardening each could have introduced regressions in library code — there's no way to know without tests.

**Proposed standard:**

| Library tier | Test requirement | Examples |
|-------------|-----------------|----------|
| **Pure Glyph** | Full unit tests, >80% function coverage | stdlib, json, scan, regex, csv |
| **Light FFI** (standard C headers) | Unit tests for Glyph logic + compile-and-link smoke test | math, time, os |
| **Heavy FFI** (system libraries) | Compile-and-link test + basic smoke test (create/destroy) | network, gtk, cairo, x11, async, thread |

**Compile-and-link tests:**

For FFI-heavy libraries that can't easily run headless, verify at minimum that:
1. The library compiles without errors (`glyph build`)
2. The generated C links against the required system libraries
3. Basic object creation and destruction works (e.g., `gtk_init` / `gtk_main_quit`)

```
-- Minimal network.glyph smoke test:
test_network_compiles =
  -- Just verify the extern wrappers link correctly
  -- Don't actually open connections
  assert(1 == 1)
```

**CI integration:**

- `glyph test --libs` runs all library tests
- Libraries with missing system dependencies (GTK, X11) skip gracefully with a message
- Pure Glyph libraries always run
- Track which libraries have tests and which don't (`glyph stat --libs`)

**Scope:** Ongoing effort. Initial pass: write 5-10 tests per untested library (~50-70 test definitions total). The infrastructure change (`--libs` flag, skip-on-missing-dep) is ~5-8 definitions.

### 63. Namespace-scoped operations

The `ns` column exists on every definition with auto-derived namespaces (`cg_` → "codegen", `tc_` → "typeck", `parse_` → "parser", etc.), but CLI and MCP operations don't leverage it uniformly. Namespace-scoped commands would make targeted work on a 1,939-definition compiler practical.

**Proposed commands:**

```bash
glyph check glyph.glyph --ns=typeck       # type-check only typeck namespace
glyph test glyph.glyph --ns=parser         # run only parser tests
glyph cover glyph.glyph --ns=codegen       # coverage for codegen namespace
glyph ls glyph.glyph --ns=llvm             # list only LLVM backend defs
glyph stat glyph.glyph --ns=lower          # stats for MIR lowering
glyph dead glyph.glyph --ns=mcp            # dead code in MCP namespace
```

**MCP equivalents:**

```
mcp__glyph__check_all(db="glyph.glyph", ns="typeck")
mcp__glyph__list_defs(db="glyph.glyph", ns="parser")
mcp__glyph__coverage(db="glyph.glyph", ns="codegen")
```

**Why this matters:**

When working on the type checker, running all 403 tests is wasteful — only the ~60 typeck-related tests are relevant. When reviewing codegen coverage, the ~200 codegen definitions are what matter, not the full 1,939. Namespace scoping turns an O(all-defs) operation into an O(namespace-defs) operation, both in compute time and in cognitive load for the LLM reviewing results.

**Implementation:**

Most commands already accept a SQL `WHERE` clause internally. Adding `--ns=NAME` means appending `AND ns = ?` to the query. The changes are mechanical:

1. **CLI argument parsing** — add `--ns=NAME` to `cmd_check`, `cmd_test`, `cmd_cover`, `cmd_ls`, `cmd_stat`. ~5-6 definitions modified.
2. **Query modification** — thread the namespace filter into the SQL queries that select definitions. ~5-6 definitions modified.
3. **MCP tools** — add optional `ns` parameter to `check_all`, `list_defs`, `coverage`, `test`. ~4-5 definitions modified.
4. **Namespace listing** — `glyph ns glyph.glyph` lists all namespaces with definition counts. ~2-3 new definitions.

**Scope:** ~15-20 definitions modified/added. Small-medium effort. Mostly mechanical SQL filter threading. High value for targeted development workflows.
