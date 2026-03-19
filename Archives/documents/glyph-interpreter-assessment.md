# Glyph Interpreter Assessment

## Motivation

The current `glyph run` flow is: read defs → tokenize → parse → lower to MIR → generate C source → write `/tmp/glyph_out.c` → invoke `cc` → execute binary. Every feedback cycle pays the cost of C code generation, file I/O, and a full C compilation. An interpreter would execute MIR directly, eliminating the codegen-compile-link pipeline entirely.

**Primary value proposition:** Faster development iteration — sub-second feedback through CLI and MCP instead of multi-second compile-link cycles.

**Secondary values:**
- Simpler bootstrap (no C compiler dependency for `run`/`test`)
- Interactive REPL potential
- Safer exploration (no native code crashes from memory bugs)
- Easier debugging (interpreter can inspect values, trace execution)

---

## Architecture Overview

### What Changes

```
CURRENT:   Source → Tokenize → Parse → MIR → C codegen → cc → binary → execute
                                         ↓
PROPOSED:  Source → Tokenize → Parse → MIR → interpret (walk MIR blocks)
```

The interpreter replaces **everything after MIR lowering**. Tokenizer, parser, MIR lowering, field-offset fixing, extern resolution, and TCO optimization are all reused unchanged.

### What Stays the Same

| Component | Reused? | Notes |
|-----------|---------|-------|
| Tokenizer (`tokenize`, `tok_one*`) | Yes | Produces `[Token]` array, no changes |
| Parser (`parse_fn_def`, 30+ parse functions) | Yes | Produces AST pool, no changes |
| MIR lowering (`lower_fn_def`, 50+ lower functions) | Yes | Produces block/stmt/term arrays, no changes |
| Field offset resolution (`fix_all_field_offsets`) | Yes | Still needed for `rv_field` execution |
| Extern call renaming (`fix_extern_calls`) | Yes | Maps to interpreter's native function table |
| TCO optimization (`tco_optimize`) | Yes | Converts tail calls to gotos |
| Type checking (`tc_infer_all`) | Optional | Could run for error reporting |
| CLI dispatch (`dispatch_cmd*`) | Modified | Add `interp`/`irun` command |
| Test framework (`cmd_test`) | Modified | Execute tests via interpreter instead of binary |
| MCP server | Inherited | Works unchanged |

### What's New

| Component | Estimated Size | Description |
|-----------|---------------|-------------|
| Value representation | ~15 defs | Tagged union for runtime values (int, str, bool, array, record, closure, unit) |
| MIR executor | ~25 defs | Walk blocks, execute stmts, handle terminators |
| Statement dispatch | ~15 defs | One handler per rvalue kind (9 kinds) |
| Terminator dispatch | ~5 defs | One handler per terminator kind (5 kinds) |
| Call stack / frames | ~10 defs | Frame allocation, local variable storage, call/return |
| Native runtime functions | ~40 defs | Interpreter-native versions of 50+ runtime functions |
| Operand evaluation | ~5 defs | Resolve operand to value (const, local, func ref) |
| Entry point / CLI | ~5 defs | `cmd_irun`, `cmd_itest`, integration |
| **Total** | **~120 defs** | |

---

## Detailed Design

### Value Representation

All Glyph values at runtime are `i64` (long long). The interpreter must decide how to represent values in its execution environment.

**Option A: Unboxed i64 (match compiled semantics)**

Store everything as `i64`, exactly like the compiled code does. Strings are `{ptr, len}` pairs (two i64 slots), arrays are `{ptr, len, cap}` (three slots), records are N contiguous i64 slots. This matches the C runtime's memory model exactly.

- Pro: Perfect semantic match, can call C runtime functions directly
- Pro: No value boxing overhead
- Con: Must manage raw memory (alloc/dealloc) from Glyph code
- Con: Same memory safety issues as compiled code (dangling pointers, etc.)
- Con: Hard to inspect/debug values

**Option B: Tagged values (interpreter-native)**

Use a tagged representation where each value carries its type:

```
val_int = 1      {vtag: 1, vint: I}
val_str = 2      {vtag: 2, vstr: S}       -- use Glyph's native string
val_bool = 3     {vtag: 3, vint: I}
val_array = 4    {vtag: 4, varr: [Val]}   -- array of Val, not i64
val_record = 5   {vtag: 5, vfields: [Val], vnames: [S]}
val_closure = 6  {vtag: 6, vfn: S, venv: [Val]}
val_unit = 7     {vtag: 7}
val_ptr = 8      {vtag: 8, vint: I}       -- opaque pointer (for FFI)
```

- Pro: Type-safe execution, meaningful error messages on type mismatch
- Pro: No manual memory management (Glyph arrays/strings handle it)
- Pro: Easy to inspect, print, debug
- Con: Boxing overhead (every operation wraps/unwraps)
- Con: Cannot directly call C runtime functions (different memory layout)
- Con: Must reimplement all runtime functions against tagged values

**Recommendation: Option B (tagged values).** The whole point of the interpreter is faster, safer development. Type-safe values with good error messages serve this goal. Runtime functions are reimplemented but simpler (no pointer arithmetic, no alloc/dealloc).

### MIR Executor

The core interpreter loop walks MIR basic blocks:

```
execute_fn(mir, args) =
  frame = new_frame(mir.fn_locals count)
  store args into frame locals at mir.fn_params indices
  cur_block = mir.fn_entry

  loop:
    stmts = mir.fn_blocks_stmts[cur_block]
    execute each stmt against frame

    term = mir.fn_blocks_terms[cur_block]
    match term.tkind
      tm_goto    -> cur_block = term.tgt1
      tm_branch  -> cur_block = if eval(term.top) then term.tgt1 else term.tgt2
      tm_return  -> return eval(term.top)
      tm_unreachable -> panic
```

Each **frame** is an array of `Val` (one per MIR local). Function calls push a new frame, returns pop it.

### Statement Execution

For each `skind`:

| Kind | Semantics |
|------|-----------|
| `rv_use` | `frame[sdest] = eval(sop1)` |
| `rv_binop` | `frame[sdest] = apply_binop(sival, eval(sop1), eval(sop2))` |
| `rv_unop` | `frame[sdest] = apply_unop(sival, eval(sop1))` |
| `rv_call` | `frame[sdest] = call_fn(eval(sop1), map(eval, sops))` |
| `rv_field` | `frame[sdest] = eval(sop1).vfields[sival]` |
| `rv_index` | `frame[sdest] = eval(sop1).varr[eval(sop2).vint]` |
| `rv_aggregate` | `frame[sdest] = make_aggregate(sival, sstr, map(eval, sops))` |
| `rv_str_interp` | `frame[sdest] = concat_parts(map(eval, sops))` |
| `rv_make_closure` | `frame[sdest] = {val_closure, sstr, map(eval, sops)}` |

### Function Dispatch

When executing `rv_call`, the callee is either:
1. **User-defined function** → look up MIR by name, call `execute_fn` recursively
2. **Runtime function** → dispatch to interpreter-native implementation
3. **Closure** → extract `{fn_name, captures}`, prepend captures as first arg, call lifted function

The function lookup table is built once from all compiled MIRs:

```
fn_table: {S: MIR}   -- name → MIR mapping
```

Runtime functions are identified by the existing `is_runtime_fn` chain and dispatched to native handlers.

### Runtime Function Reimplementation

With tagged values, runtime functions become straightforward:

```
-- String operations (use Glyph's native strings directly)
rt_str_concat(a, b) = mk_val_str(str_concat(a.vstr, b.vstr))
rt_str_len(s)       = mk_val_int(str_len(s.vstr))
rt_str_eq(a, b)     = mk_val_bool(str_eq(a.vstr, b.vstr))
rt_str_slice(s,i,j) = mk_val_str(str_slice(s.vstr, i.vint, j.vint))
rt_int_to_str(i)    = mk_val_str(itos(i.vint))
rt_str_to_int(s)    = mk_val_int(str_to_int(s.vstr))

-- Array operations (use Glyph's native arrays directly)
rt_array_new()      = mk_val_arr([])
rt_array_push(a, v) = push v onto a.varr; return a
rt_array_len(a)     = mk_val_int(len(a.varr))

-- I/O (delegate to existing Glyph builtins)
rt_println(s)       = println(s.vstr); mk_val_unit()
rt_print(s)         = print(s.vstr); mk_val_unit()
rt_read_file(p)     = mk_val_str(read_file(p.vstr))
rt_write_file(p, c) = write_file(p.vstr, c.vstr); mk_val_unit()

-- DB operations (pass through to glyph_db_* calls)
rt_db_open(p)       = mk_val_ptr(db_open(p.vstr))
rt_db_close(db)     = db_close(db.vint); mk_val_unit()
rt_db_exec(db, sql) = db_exec(db.vint, sql.vstr); mk_val_unit()
rt_db_query_rows(db, sql) = ... -- returns array of arrays
```

Key insight: most runtime functions can delegate to the **same Glyph builtins** the compiler already uses (`glyph_db_open`, `glyph_str_concat`, etc.), just wrapping/unwrapping tagged values.

### Binop / Unop Dispatch

Binary operators dispatch on operator kind (`sival` field):

```
apply_binop(op, lhs, rhs) =
  match op
    op_add -> match lhs.vtag
      val_int -> mk_val_int(lhs.vint + rhs.vint)
      val_str -> mk_val_str(str_concat(lhs.vstr, rhs.vstr))
    op_sub -> mk_val_int(lhs.vint - rhs.vint)
    op_mul -> mk_val_int(lhs.vint * rhs.vint)
    op_div -> mk_val_int(lhs.vint / rhs.vint)
    op_eq  -> match lhs.vtag
      val_str -> mk_val_bool(str_eq(lhs.vstr, rhs.vstr))
      _       -> mk_val_bool(lhs.vint == rhs.vint)
    op_neq -> ... (inverse of eq)
    op_lt  -> mk_val_bool(lhs.vint < rhs.vint)
    op_gt  -> mk_val_bool(lhs.vint > rhs.vint)
    op_and -> mk_val_bool(lhs.vint && rhs.vint)
    op_or  -> mk_val_bool(lhs.vint || rhs.vint)
    op_mod -> mk_val_int(lhs.vint % rhs.vint)
```

The tagged value approach **eliminates the string-operator detection problem** that plagues the compiler (`is_str_op` heuristic). At runtime, we know the actual type.

---

## Coexistence Strategy

Both compiler and interpreter coexist as separate commands:

```
glyph build prog.glyph           # compile to native binary (existing)
glyph run prog.glyph             # compile + run native binary (existing)
glyph test prog.glyph            # compile + run tests as native binary (existing)

glyph interp prog.glyph          # interpret main function
glyph itest prog.glyph           # interpret test functions
glyph eval prog.glyph "expr"     # evaluate single expression (future)
```

### Shared Code Path

```
                    ┌──────────────────────┐
                    │  Read defs from DB   │
                    │  Tokenize + Parse    │
                    │  Lower to MIR        │
                    │  Fix field offsets    │
                    │  Fix extern calls    │
                    │  TCO optimize        │
                    └──────────┬───────────┘
                               │
                    ┌──────────┴───────────┐
                    │                      │
              ┌─────┴─────┐         ┌──────┴──────┐
              │  Compile  │         │  Interpret  │
              │           │         │             │
              │ cg_program│         │ execute_fn  │
              │ write .c  │         │ (walk MIR)  │
              │ invoke cc │         │             │
              │ run binary│         │ direct      │
              └───────────┘         └─────────────┘
```

The `build_program` function splits: `build_program` continues to generate C and compile; a new `interp_program` takes the same MIRs and executes them directly.

### Feature Parity

Both paths consume identical MIR, so language feature parity is automatic:
- Pattern matching: compiled to MIR branches, both paths execute same structure
- Closures: lowered to MIR aggregates + lifted functions, both paths handle same representation
- String interpolation: lowered to string builder calls, both paths use same MIR
- Enums: lowered to tagged aggregates, both paths access same tag/payload structure
- For-loops: desugared in MIR lowering, both paths see same loop structure

The **only** divergence is in runtime function behavior where the compiled version uses C runtime (pointer arithmetic, raw memory) and the interpreter uses tagged-value wrappers.

### Test Equivalence

The test suite (`test_comprehensive.glyph` + per-project tests) should produce identical results through both paths. A CI check could run:

```bash
./glyph test db.glyph          # compiled test
./glyph itest db.glyph         # interpreted test
# compare outputs
```

---

## Performance Analysis

### Current Compilation Cost

For a typical program (20-50 definitions):

| Phase | Time | Notes |
|-------|------|-------|
| Read defs from SQLite | ~5ms | Fast, indexed |
| Tokenize + Parse | ~10ms | Per-definition, fast |
| MIR lowering | ~15ms | Per-definition |
| Field offset fixing | ~5ms | One pass over all MIRs |
| C code generation | ~20ms | String concatenation |
| Write `/tmp/glyph_out.c` | ~2ms | File I/O |
| `cc` invocation | ~500-2000ms | **Dominant cost** |
| Execute binary | ~1ms | Native speed |
| **Total** | **~600-2100ms** | |

For the compiler itself (~750 definitions):

| Phase | Time |
|-------|------|
| Read + Tokenize + Parse + Lower | ~200ms |
| C codegen | ~100ms |
| `cc` invocation | ~3-5s |
| **Total** | **~3-5s** |

### Interpreter Cost Estimate

| Phase | Time | Notes |
|-------|------|-------|
| Read defs from SQLite | ~5ms | Same |
| Tokenize + Parse | ~10ms | Same |
| MIR lowering | ~15ms | Same |
| Field offset fixing | ~5ms | Same |
| **Interpret** | ~10-100ms | Depends on program complexity |
| **Total** | **~45-135ms** | |

**Speedup: 5-50x** for the feedback loop. The `cc` invocation (500-5000ms) is completely eliminated. Interpretation overhead is modest since MIR is already a low-level representation (no AST walking, no pattern matching at runtime — everything is flat blocks with jumps).

### Where Compiled Wins

- CPU-intensive computation (tight loops, number crunching): ~10-100x slower interpreted
- Large data processing: interpreter has boxing overhead per value
- Production deployment: compiled binary is self-contained, no interpreter needed

### Where Interpreter Wins

- Development iteration: sub-second feedback vs multi-second compile
- Error diagnostics: tagged values enable meaningful type error messages
- Safety: no segfaults from null pointer dereferences or buffer overflows
- Test execution: run tests without compilation pipeline

---

## Implementation Approach

### Option A: Self-Hosted in Glyph (Recommended)

Write the interpreter as ~120 new definitions in `glyph.glyph`. The interpreter is itself a Glyph program that:
1. Reads target program's definitions from a `.glyph` database
2. Tokenizes, parses, lowers to MIR (reusing existing functions)
3. Walks MIR blocks to execute

**Advantages:**
- Maximum code reuse (tokenizer, parser, MIR lowering already in `glyph.glyph`)
- Same language for compiler and interpreter — one codebase
- Dogfoods Glyph itself
- Interpreter benefits from future Glyph improvements
- Fits the LLM-native philosophy (LLM modifies interpreter via same MCP tools)

**Disadvantages:**
- Interpreter runs interpreted (meta-circular), could be slow for bootstrapping
- Must be compiled once via `glyph build` before it can interpret
- Glyph's lack of closures-as-values makes some patterns verbose

**Bootstrap path:**
```
glyph build glyph.glyph    → glyph binary (includes interpreter)
./glyph interp prog.glyph  → executes prog via interpreter
```

### Option B: Rust Crate

Add a new `glyph-interp` crate to the workspace that takes MIR (from existing Rust pipeline) and executes it.

**Advantages:**
- Native speed for the interpreter itself
- Access to Rust's type system for safe value representation
- Easy integration with existing Rust crates (rusqlite, etc.)
- Could serve as Stage 0 interpreter (no compilation needed)

**Disadvantages:**
- Duplicates effort (Rust MIR types vs Glyph MIR types)
- Two languages to maintain
- Doesn't exercise Glyph itself
- Adds ~1000 lines of Rust to maintain

### Option C: Hybrid

Rust crate for the execution engine (value representation, block walker, native functions), but reuse the self-hosted tokenizer/parser/lowerer by calling into compiled Glyph code.

**Advantages:**
- Best of both: native-speed execution engine, Glyph-native front end
- Minimal new Rust code (~500 lines for executor only)

**Disadvantages:**
- Complex FFI boundary between Rust executor and Glyph front end
- Two compilation models to maintain

**Recommendation: Option A** for the self-hosted version. It maximizes reuse, dogfoods Glyph, and the performance characteristics are acceptable since the interpreter is compiled to native code via `glyph build` before it interprets user programs. The meta-circular overhead only applies if you tried to interpret the interpreter — which isn't the use case.

---

## Challenges and Mitigations

### Challenge 1: Value Representation in Glyph

Glyph doesn't have sum types (tagged unions) as a first-class feature. Values must be represented as records with a tag field.

**Mitigation:** Use the existing record + tag pattern already used throughout the codebase (`AstNode.kind`, `TyNode.tag`, `Operand.okind`, etc.). A `Val` record:

```
Val = {vtag: I, vint: I, vstr: S, varr: [Val], vfields: [Val], vnames: [S]}
```

Unused fields are zero/empty for each variant. This is exactly how AST nodes, MIR operands, and type nodes already work.

### Challenge 2: Recursive Value Types

`Val` contains `[Val]` (arrays of values, record fields). Glyph arrays are homogeneous at the C level (`[i64]`), but the interpreter needs arrays of tagged values.

**Mitigation:** Since Glyph arrays are actually `{ptr, len, cap}` holding `i64` elements, and each `Val` is a heap-allocated record (pointer fits in `i64`), arrays of `Val` work naturally — each element is a pointer to a `Val` record. This is the same pattern used for `[AstNode]`, `[TyNode]`, etc.

### Challenge 3: Self-Referential Execution

The interpreter must be able to interpret programs that themselves use arrays, records, and strings. The interpreter's own execution uses these same primitives.

**Mitigation:** The interpreter's internal state (frames, value arrays) and the interpreted program's values exist at the same level — both are Glyph values in the running process. There's no aliasing conflict because the interpreter creates fresh `Val` records for the interpreted program's values, separate from its own MIR data structures.

### Challenge 4: Database Access from Interpreted Code

Interpreted programs that call `db_open`, `db_query_rows`, etc. need actual SQLite access.

**Mitigation:** The runtime functions `glyph_db_open` et al. are already available in the compiled interpreter binary. The interpreter's native function dispatch calls these directly, wrapping results in `Val` records:

```
rt_db_open path =
  handle = glyph_db_open(path.vstr)
  mk_val_ptr(handle)
```

### Challenge 5: Extern / FFI Functions

Interpreted programs may declare externs in the `extern_` table. The interpreter cannot generate C wrappers at runtime.

**Mitigation options:**
1. **Disallow externs in interpreter mode** — simplest, covers 90% of use cases (most programs use only built-in runtime functions)
2. **Pre-registered extern table** — interpreter ships with handlers for common externs (sqlite3, basic libc)
3. **dlopen/dlsym** — dynamically load shared libraries and call functions (complex, but possible via `glyph_system` + manual FFI)

**Recommendation:** Option 1 for MVP. Programs needing externs use `glyph run` (compiled mode). Add option 2 later for sqlite3 externs specifically.

### Challenge 6: Closures

Closures in compiled Glyph are `{fn_ptr, captures...}` where `fn_ptr` is a raw function pointer. The interpreter can't use raw pointers.

**Mitigation:** Represent closures as `{vtag: val_closure, vstr: "lifted_fn_name", varr: [captured_vals]}`. When calling a closure, look up the lifted function by name, prepend captured values as the first argument.

### Challenge 7: Performance of Self-Hosted Interpreter

The interpreter, being Glyph code compiled to native, executes at native speed. But it interprets MIR block-by-block with tagged value dispatch, adding overhead per operation.

**Estimated overhead:** ~20-50x slower than compiled code for compute-bound programs. For I/O-bound programs (which most development tasks are), the overhead is negligible.

**Mitigation:** This is acceptable because:
- The goal is fast feedback, not fast execution
- Test suites typically run in milliseconds even interpreted
- The compiled path remains available for performance-critical work

---

## Scope Estimate

### MVP (Minimal Viable Interpreter)

Execute programs with: integers, strings, booleans, arrays, records, pattern matching, function calls, basic I/O.

| Category | Definitions | Notes |
|----------|-------------|-------|
| Value type + constructors | 10 | `Val` record, `mk_val_int`, `mk_val_str`, etc. |
| Operand evaluation | 5 | `eval_operand` dispatches on `okind` |
| Statement execution | 15 | `exec_stmt` dispatches on `skind` (9 kinds) |
| Terminator execution | 5 | `exec_term` dispatches on `tkind` (5 kinds) |
| Binop/unop dispatch | 8 | `apply_binop`, `apply_unop` with operator tables |
| Frame management | 8 | `new_frame`, `push_frame`, `pop_frame`, call stack |
| Function dispatch | 10 | `call_fn`, `lookup_fn`, runtime function table |
| Runtime functions (core) | 25 | `rt_println`, `rt_str_concat`, `rt_array_push`, etc. |
| Entry point + CLI | 5 | `cmd_interp`, `interp_program`, `interp_main` |
| **MVP Total** | **~91 defs** | |

### Full Feature Set

| Category | Additional Defs | Notes |
|----------|----------------|-------|
| Closures | 5 | Closure creation, capture, calling |
| String interpolation | 3 | String builder integration |
| Runtime functions (DB) | 8 | SQLite operations via native calls |
| Runtime functions (IO) | 5 | File I/O, system calls |
| Test framework integration | 8 | `cmd_itest`, test dispatch, assertion wrappers |
| Error reporting | 5 | Stack traces, type mismatch messages |
| **Full Total** | **~125 defs** | |

### Estimated Effort

- **MVP:** ~91 definitions, achievable in 2-3 focused sessions
- **Full:** ~125 definitions, achievable in 4-5 focused sessions
- **Testing + polish:** 1-2 additional sessions

Each definition averages ~15-25 lines, so total new code is roughly 1,500-3,000 lines of Glyph.

---

## MCP Integration

The interpreter naturally integrates with the existing MCP server:

### New MCP Tools (Optional)

```
interp_eval(db_path, expr)     → evaluate expression, return result
interp_run(db_path)            → run main, return output
interp_test(db_path, name?)    → run tests, return results
```

These would give LLMs **instant feedback** on code changes without waiting for compilation:

```
1. LLM writes/modifies definition via put_def
2. LLM calls interp_test to verify
3. Sub-second feedback
4. LLM iterates
5. When satisfied, calls glyph build for final native binary
```

This is the core workflow improvement: the LLM development loop drops from ~3-5 seconds (compile) to ~100-200ms (interpret) per iteration.

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Semantic divergence between compiled and interpreted | Medium | High | Shared MIR ensures same lowering; test suite validates equivalence |
| Performance too slow for large programs | Low | Medium | Compiled path always available; interpreter for dev only |
| Value representation bugs (tag mismatches) | Medium | Medium | Comprehensive test suite; tagged values make errors visible |
| Extern FFI not available in interpreter | Certain (MVP) | Low | Compiled path handles extern-heavy programs |
| Recursive depth limits | Low | Low | TCO already handled at MIR level |
| Memory leaks in interpreter | Medium | Low | Dev tool, not production; acceptable for short-lived runs |

---

## Comparison to Alternatives

### Alternative 1: Faster C Compilation

Use `tcc` (Tiny C Compiler) instead of `cc` for development builds. TCC compiles ~10x faster than GCC.

- Pro: No new code, drop-in replacement
- Pro: Full feature parity (same C codegen)
- Con: Still pays codegen + file I/O + link cost (~100-200ms vs ~50ms interpreted)
- Con: Still produces native code with same memory safety issues
- Con: TCC is an additional dependency

**Verdict:** Good complementary optimization, but doesn't provide the safety or introspection benefits of an interpreter.

### Alternative 2: JIT Compilation

Compile MIR to machine code in-memory (via Cranelift or custom JIT), skip file I/O and external compiler.

- Pro: Native execution speed
- Pro: No external compiler dependency
- Con: Cranelift already available via Rust compiler (glyph0)
- Con: Complex to implement in self-hosted Glyph
- Con: Same memory safety issues as compiled code

**Verdict:** Essentially what `glyph0 build` already does. Marginal improvement over current flow.

### Alternative 3: Bytecode VM

Compile MIR to a custom bytecode format, execute on a stack-based VM.

- Pro: Faster than tree-walking interpreter (~5-10x)
- Pro: Bytecode can be cached and reloaded
- Con: Significantly more complex (bytecode format, compiler, VM loop)
- Con: Marginal benefit over MIR walking (MIR is already nearly bytecode-level)

**Verdict:** Over-engineered for the use case. MIR is already flat enough that walking it is efficient. A bytecode VM could be a future optimization if interpreter performance becomes a bottleneck.

---

## Conclusion

An interpreter is a **strong fit** for Glyph's architecture and development workflow:

1. **High code reuse** — ~70% of the compilation pipeline (tokenizer, parser, MIR lowering) is shared
2. **Clean insertion point** — MIR is an ideal intermediate representation for interpretation (flat blocks, explicit jumps, discriminated operands)
3. **Manageable scope** — ~120 new definitions for full feature set
4. **Clear value proposition** — 5-50x faster feedback loop for development
5. **Natural coexistence** — compiler and interpreter share front end, diverge only at execution
6. **LLM workflow improvement** — sub-second MCP feedback enables tighter development loops

The recommended approach is a **self-hosted interpreter in Glyph** (Option A), implemented as ~120 new definitions in `glyph.glyph`, using tagged values (Option B) for type-safe execution. MVP covers core language features in ~91 definitions; full feature set in ~125.
