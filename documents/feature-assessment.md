# Glyph Feature Assessment & Recommendations

**Date:** 2026-03-24
**Scope:** Comprehensive evaluation of the Glyph language and self-hosted compiler

---

## 1. Executive Summary

Glyph is a working, self-hosted programming language with a 4-stage bootstrap chain, 1,287 function definitions, 287 tests, and 12 example programs spanning CLI tools, GUIs, games, and a REST API. The self-hosted compiler compiles itself through C and LLVM IR backends and can produce native binaries for real applications.

The language has matured well beyond "prototype" but has clear gaps between what was specified and what exists. The most impactful areas for improvement are: (1) a standard library, (2) memory management, (3) completing parsed-but-unimplemented features, and (4) error reporting quality. Below is a thorough audit followed by prioritized recommendations.

---

## 2. Current Feature Inventory

### 2.1 What Works Well

| Feature | Status | Evidence |
|---------|--------|----------|
| Core expressions (int, str, bool, float, array, record, map) | Complete | 23 expression types fully lowered |
| Pattern matching (wildcard, ident, int, str, bool, constructor, or-patterns) | Complete | 7 pattern kinds + guards |
| Closures & lambdas | Complete | Lambda lifting, heap-allocated captures, indirect calls |
| Pipe operator `\|>` | Complete | Desugars to function application in MIR |
| Error propagate `?` and unwrap `!` | Complete | Control-flow lowered to branch/panic |
| String interpolation `"text {expr}"` | Complete | StringBuilder-based O(n) codegen |
| Record literals & updates `rec{field: val}` | Complete | Functional update semantics |
| Let destructuring `{x, y} = expr` | Complete | Desugars to temp + field access |
| Hindley-Milner type inference | Complete | Let-polymorphism, unification, generalization |
| Monomorphization | Complete | 37 `mono_*` definitions, spec collection + compilation |
| Tail call optimization | Complete | 11 `tco_*` definitions |
| C codegen backend | Complete | Full pipeline to `cc` invocation |
| LLVM IR backend | Complete | `--emit=llvm` flag, ~66 `ll_*` definitions |
| 4-stage bootstrap | Complete | glyph0→glyph1→glyph2→glyph, all via `ninja` |
| MCP server (15 tools) | Complete | get/put/list/search/check/build/run/deps/rdeps/sql/coverage/link/libs/use/unuse |
| Test framework | Complete | 287 tests, `glyph test`, assertions, coverage |
| FFI (C ABI) | Complete | `extern_` table, dynamic wrapper generation |
| Incremental compilation | Partial | Content hashing exists; dep-table based dirty tracking |
| Import/export (file ↔ DB) | Complete | Namespace-aware, gen-aware |
| Library system (`glyph link`, `glyph use`) | Complete | Copy and registered-library modes |
| Gen=2 struct codegen | Complete | `typedef struct` for named types |
| Bitwise operators | Complete | `bitand`, `bitor`, `bitxor`, `shl`, `shr` |
| Maps (hash map) | Complete | `{{k: v}}` literals, linear-probe runtime |
| SQLite FFI | Complete | db_open/close/exec/query_rows/query_one |

### 2.2 Parsed but Not Lowered (Dead Syntax)

These features have parser support but produce no executable code:

| Feature | Parser | Type Checker | MIR Lowering | Codegen |
|---------|--------|--------------|--------------|---------|
| **Function composition `>>`** | `parse_compose` | `infer_compose` | **Missing** | **Missing** |
| **Field accessor `.field`** | `ex_field_accessor` | — | **Missing** | **Missing** |
| **Traits** | Spec only | — | — | — |
| **Impl blocks** | Spec only | — | — | — |
| **Const definitions** | `tk_const` token | — | — | — |
| **For-loops / ranges** | `tk_for` token | — | — | — |
| **FSM (state machines)** | Spec only | — | — | — |
| **Srv (server routes)** | Spec only | — | — | — |
| **Macro definitions** | `kind='macro'` in schema | — | — | — |
| **Byte literals `b"..."`** | Spec only | — | — | — |
| **Tuple types** | Spec only | — | — | — |

The composition operator `>>` is the most notable gap — it's parsed, type-checked, included in free-variable analysis (`walk_free_vars3`), but `lower_expr2`/`lower_expr3` have no arm for `ex_compose`. It silently produces `mk_op_unit()`.

### 2.3 Spec Features Not Started

From `glyph-spec.md` Section 8 (Standard Library) and Section 10 (Future Work):

- **Standard library functions**: `map`, `filter`, `fold`, `sort`, `zip`, `take`, `drop`, `find`, `any`, `all`, `rev`, `flat`, `split` (string), `join`, `trim`, `upper`, `lower`, `starts`, `ends`, `has`, `rep` — **none exist as Glyph-callable functions**
- **Concurrency**: `par`, `spawn`, `await` — not implemented
- **Effect system** — not implemented
- **Dependent types** — not implemented
- **WASM target** — not implemented
- **Garbage collection / ownership** — not implemented
- **Row polymorphism** in self-hosted type checker — implemented (unify_records, unify_row_vars, open record types)
- **Trait system** — parser tokens exist, no semantics
- **FFI safety (`@unsafe` annotations)** — not implemented
- **Multi-DB `ATTACH`** — not implemented (library linking uses copy, not attach)

---

## 3. Compiler Maturity Assessment

### 3.1 Quantitative Profile

| Metric | Value |
|--------|-------|
| Total definitions | 1,586 (1,287 fn + 287 test + 12 type) |
| Total source bytes | 524,787 (in def bodies) |
| Database size | 1.8 MB |
| Binary size (LLVM) | 431 KB |
| Rust compiler LOC | ~10,636 (across 6 crates) |
| Self-hosted namespaces | 20+ (codegen, parser, typeck, lower, llvm, mcp, json, ...) |
| Largest function | `cg_runtime_c` (5,353 chars — embedded C runtime) |
| Average function size | 339 chars |
| Example programs | 12 (hello, fibonacci, countdown, calculator, life, gled, glint, gstats, benchmark, api, asteroids, gtk×3) |
| Rust tests passing | 73/73 |
| Self-hosted tests | 287 |
| CLI commands | 28 |
| MCP tools | 15 |
| Codegen backends | 3 (Cranelift, C, LLVM IR) |

### 3.2 Maturity by Subsystem

#### Tokenizer — **Mature**
- 87 definitions in `tokenizer` namespace
- Handles indentation-sensitive layout (Python-style INDENT/DEDENT)
- String interpolation, raw strings, float literals, hex escapes
- Well-tested (13 tokenizer tests)

#### Parser — **Mature**
- 149 definitions in `parser` namespace
- Recursive descent, full expression grammar
- Good error messages with source context and caret
- 14 atom forms, postfix chaining, precedence climbing
- Well-tested (20+ parser tests)

#### Type Checker — **Functional but Incomplete**
- 154 definitions in `typeck` namespace
- Hindley-Milner with let-polymorphism and generalization
- Pool-based type nodes, union-find substitution
- 16 registered builtins with polymorphic signatures
- Row polymorphism implemented (`unify_records`, `unify_row_vars`, `fields_not_in`, open record types with rest variables)
- **Gaps**: `?`/`!` not fully type-checked, no trait support, type errors are advisory (don't gate builds for small programs but do for <200 defs), generalization bug (BUG-007) prevents polymorphic utility functions

#### MIR Lowering — **Mature**
- 92 definitions in `lower` namespace + 104 in `mir` namespace
- All 21+ expression types lowered
- Pattern match compilation with guards, or-patterns, constructor binding
- Closure lifting with free-variable capture
- String/float binary operator dispatch via type tracking

#### C Codegen — **Mature**
- 107 definitions in `codegen` namespace
- Full C runtime (~5,353 chars) with memory, strings, arrays, maps, floats, math, I/O, SQLite, MCP, result types, bitsets
- Forward declarations, struct typedefs, extern wrappers
- Field offset resolution with type disambiguation
- Coverage instrumentation
- Debug/release build modes

#### LLVM IR Backend — **Complete**
- 66 definitions in `llvm` namespace
- Text-mode LLVM IR generation → `llc` → native
- Used for Stage 3 of bootstrap (final binary)

#### Monomorphization — **Complete**
- 37 definitions in `mono` namespace
- Collects polymorphic call sites, builds type variable maps, generates specialized copies
- Name mangling (`foo__int_str`) for specialized variants

#### TCO — **Complete but Limited**
- 11 definitions
- Transforms tail-recursive calls into MIR loops
- Only handles direct self-recursion (not mutual recursion)

#### MCP Server — **Mature**
- 55 definitions
- JSON subsystem (56 definitions in `json` namespace)
- Full stdio transport, 15 tools
- `check_def` runs type inference without inserting
- `build`/`run` shell out to avoid stdout corruption

#### CLI — **Mature**
- 49 definitions + 28 commands
- Dispatch chain architecture
- Comprehensive feature set (init, build, run, test, cover, get, put, rm, ls, find, deps, rdeps, stat, dump, sql, extern, undo, history, link, unlink, use, unuse, libs, migrate, mcp, export, import, version, update)

### 3.3 Maturity Ratings

| Subsystem | Rating | Notes |
|-----------|--------|-------|
| Tokenizer | A | Production-quality |
| Parser | A | Production-quality, good errors |
| Type Checker | B- | HM + row polymorphism working, no traits, generalization bug (BUG-007) |
| MIR Lowering | A- | Comprehensive, one dead arm (compose) |
| C Codegen | B+ | Works well, but embedded-C-in-strings is fragile |
| LLVM Codegen | B+ | Complete but text-mode IR (not LLVM C API) |
| TCO | B | Single-function only |
| Monomorphization | B+ | Works but limited to one level of specialization |
| Runtime | B | No GC, no bounds checking on some ops, O(n) hashmap |
| MCP Server | A | Excellent LLM integration |
| CLI | A | Comprehensive |
| Test Framework | B+ | Good coverage, no mocking/fixtures |
| Error Reporting | B- | Parse errors good, type errors basic, runtime errors limited to segfault handler |
| Documentation | B+ | Manual, spec, and type system docs exist |

### 3.4 Overall Maturity: **Late Alpha / Early Beta**

The compiler successfully compiles itself and real applications (including a GUI asteroids game, a text editor, GTK4 apps, and a REST API). The bootstrap chain works. The LLM workflow via MCP is polished. However, it lacks a standard library, has no memory management, has dead syntax, and the type checker has significant gaps.

---

## 4. Feature Recommendations

### Tier 1: High Impact, Moderate Effort

#### 4.1 Standard Library (as a linkable `stdlib.glyph`)

**Why:** Every example program re-implements basic operations. There are no `map`, `filter`, `fold`, `sort`, `zip`, `find`, `any`, `all`, `reverse`, `join`, `split`, `starts_with`, `ends_with`, `contains` functions available to user programs.

**What exists today:** Some string helpers exist in the C runtime (`glyph_str_split`, `glyph_str_trim`, `glyph_str_starts_with`, `glyph_str_ends_with`, `glyph_str_to_upper`, `glyph_str_index_of`, `glyph_array_reverse`, `glyph_array_slice`, `glyph_array_index_of`), but they are **not exposed as Glyph-callable functions** — they're only in the C preamble.

**Recommendation:**
1. Create `lib/stdlib.glyph` with thin wrappers around existing runtime functions plus pure-Glyph higher-order functions (`map`, `filter`, `fold`, etc.)
2. Use `glyph use` (registered library) so programs get them at build time
3. Priority functions: `map`, `filter`, `fold`, `sort`, `zip`, `reverse`, `join`, `split`, `trim`, `starts_with`, `ends_with`, `contains`, `find`, `any`, `all`, `len`, `take`, `drop`, `flat_map`, `range`
4. This requires closures to work as arguments (already supported) and monomorphization for generic functions (already supported)

**Estimated scope:** ~40-60 definitions, mostly straightforward recursive implementations

#### 4.2 Implement Function Composition `>>`

**Why:** Already parsed and type-checked. The only missing piece is MIR lowering. Currently silently returns unit, which is a correctness bug.

**Recommendation:** Add a `lower_compose` function that desugars `f >> g` to `\x -> g(f(x))` — literally a lambda with two calls. This is ~15 lines of Glyph code.

**Estimated scope:** 1 new definition, 1 modified (`lower_expr2`)

#### 4.3 Implement Field Accessor Lambdas `.field`

**Why:** The spec's signature feature for pipeline ergonomics: `users |> filter .active |> map .name`. Currently parsed to `ex_field_accessor` but not lowered.

**Recommendation:** Lower `.field` as `\x -> x.field` — a lambda capturing no variables, with a single field access in the body. This enables the idiomatic pipeline style from the spec.

**Estimated scope:** 1 new definition in lower, 1 modification to `lower_expr3`

#### 4.4 Expose Existing Runtime Functions

**Why:** The C runtime already has `glyph_str_split`, `glyph_str_trim`, `glyph_str_starts_with`, `glyph_str_ends_with`, `glyph_str_to_upper`, `glyph_str_index_of`, `glyph_array_reverse`, `glyph_array_slice`, `glyph_array_index_of`, `glyph_str_from_code` — but they're not callable from Glyph code. They need to be:
1. Added to `is_runtime_fn` chain so codegen maps them correctly
2. Registered in `register_builtins` for type checking
3. Documented

**Estimated scope:** Modifications to `is_runtime_fn` chain + `register_builtins`, no new runtime C code

### Tier 2: Medium Impact, Medium Effort

#### 4.5 HashMap Performance

**Why:** The current hashmap uses O(n) linear scan for every get/set/has/delete. This is acceptable for small maps (<100 entries) but will bottleneck any data-intensive program.

**Recommendation:** Replace the linear-scan implementation in `cg_runtime_map` with open addressing + FNV-1a hashing. The API (hm_new/get/set/has/del/keys/len) stays the same. Only the C runtime changes.

**Estimated scope:** Rewrite of `cg_runtime_map` (~60 lines of C)

#### 4.6 `--ffi` and `--pkg-config` Build Flags

**Why:** Every FFI-heavy example (life, gled, asteroids, api, gtk×3) needs a build script that manually `cat`s C wrapper files and runs `cc` with custom flags. Adding `--ffi <file.c>` and `--pkg-config <name>` to `glyph build` would eliminate all build scripts.

**Status:** Already partially implemented — `build_program` has `meta_prepend` and `cc_extra` parameters. The CLI just needs to thread `--ffi` and `--pkg-config` flags through.

**Estimated scope:** ~20 lines of modification to `cmd_build` and dispatch

#### 4.7 Type Checker: Generalization Bug (BUG-007)

**Why:** Row polymorphism is already implemented (`unify_records`, `unify_row_vars`, `fields_not_in`, open record types with rest variables). However, the type checker has a generalization bug (BUG-007) where `tc_collect_fv` fails to find free type variables in polymorphic functions, preventing generalization. This is likely caused by C codegen field offset disambiguation errors when reading TyNode `.tag` values from the type pool. See `documents/type-checker-generalization-bug.md` for full analysis.

**Recommendation:** Fix the field offset disambiguation in C codegen (Option A/C in BUG-007), or migrate the type checker to gen=2 struct codegen which eliminates the ambiguity entirely.

**Estimated scope:** Depends on approach — gen=2 migration is ~96 definitions

#### 4.8 For-Loops with Range

**Why:** Currently all iteration is via recursion or TCO. This works but is verbose. A `for x in range(0, n)` or `for x in arr` would be a significant ergonomic improvement. The tokenizer already has `tk_for`.

**Recommendation:** Implement `for x in expr : body` as sugar that desugars to an index-based loop in MIR. The Rust compiler's MIR lowering already has the `ForLoop` desugaring infrastructure.

**Estimated scope:** ~10 new definitions (parser + lowering), ~5 modified

#### 4.9 Const Definitions

**Why:** Currently, zero-arg functions are eagerly evaluated. There's no way to define a named constant without the zero-arg function gotcha (side effects at definition time). A `const` mechanism would fix this.

**Recommendation:** The simplest approach: `const` defs are compiled as global variables initialized once in a `__glyph_init` function called before `main`. Parser already has `tk_const`.

**Estimated scope:** ~10 new definitions

### Tier 3: Important for Production Use

#### 4.10 Memory Management

**Why:** There is currently **no memory management whatsoever**. Every allocation (`glyph_alloc`, `malloc`) leaks. This is fine for short-lived programs (compilers, CLIs) but fatal for long-running ones (servers, GUIs, games).

**Options (increasing complexity):**
1. **Arena allocator** — allocate from a per-function arena, free the whole arena on function return. Simple, handles most cases. Doesn't help with long-lived data.
2. **Reference counting** — add a refcount header to all heap allocations. Compiler inserts retain/release calls. Handles cycles poorly.
3. **Conservative GC (Boehm)** — link against `libgc`, replace `malloc` with `GC_malloc`. Zero compiler changes needed. The easiest path to correctness.
4. **Tracing GC** — custom mark-and-sweep or copying collector. Most work, best results.

**Recommendation:** Start with option 3 (Boehm GC). It's a one-line change: `#define malloc GC_malloc` in the runtime preamble, plus `-lgc` in the linker flags. This instantly eliminates all memory leaks with zero compiler changes. If performance matters later, move to option 2 or 4.

#### 4.11 Better Runtime Error Messages

**Why:** Runtime errors currently produce either a segfault handler message ("segfault in <fn>") or a generic panic. There are no source locations, no variable values, no stack traces in release mode.

**Recommendation:**
1. Emit function name tracking in release mode (already done: `_glyph_current_fn`)
2. Add source location metadata to panic calls (line:col from token spans)
3. Improve match exhaustiveness — currently emits `tm_unreachable` which traps silently

**Estimated scope:** ~10-15 modifications to codegen

#### 4.12 Trait System (Minimal)

**Why:** No ad-hoc polymorphism exists. You can't write `print` for custom types, can't define `==` for records, can't abstract over "things that have a `.len`". The spec defines traits but nothing is implemented.

**Recommendation:** A minimal trait system:
1. `trait Show`: `show : T -> S` (string conversion)
2. `trait Eq`: `eq : T -> T -> B`
3. `trait Ord`: `cmp : T -> T -> I`
4. Auto-derive for records and enums based on field types
5. Dictionary-passing implementation (no virtual dispatch)

This is a large feature but would unlock generic `sort`, `==` on records, and `println` for arbitrary types.

**Estimated scope:** 50-100 new definitions across parser, type checker, and codegen

### Tier 4: Nice to Have

#### 4.13 WASM Target

**Why:** Would allow Glyph programs to run in browsers and sandboxed environments. The C codegen backend could target WASM via `emcc` with minimal changes.

#### 4.14 Incremental Compilation (True)

**Why:** The dep table and dirty tracking exist but aren't used by the self-hosted compiler — it always recompiles everything. The Rust compiler uses it for incremental builds. For the self-hosted compiler, each `build` re-parses, re-typechecks, and re-codegens all definitions.

#### 4.15 Mutual TCO

**Why:** Current TCO only handles direct self-recursion. Mutual recursion between two functions (common in parsers, interpreters) still grows the stack.

#### 4.16 Module/Namespace Visibility

**Why:** All definitions are globally visible. No `pub`/`private` distinction. The `module`/`module_member` tables exist in the schema but aren't used by the compiler.

---

## 5. Risk Assessment

### Critical Risks

1. **Memory leaks in long-running programs.** Every allocation leaks. The asteroids game, text editor, and any server will eventually OOM. Mitigation: Boehm GC (Tier 3.1).

2. **Composition operator silently returns unit.** Code using `>>` compiles but produces wrong results. This is a correctness bug. Mitigation: Tier 1.2 (trivial fix).

3. **No standard library means every program reinvents basics.** LLMs will generate incompatible utility functions across programs. Mitigation: Tier 1.1.

### Moderate Risks

4. **Type checker gaps.** Missing row polymorphism means the type checker rejects valid code. Missing trait support means no polymorphic `==`/`show`/`sort`. Type errors are advisory for large programs (>200 defs don't get checked during build).

5. **Runtime is hand-written C embedded in Glyph strings.** The `cg_runtime_c` function is 5,353 characters of C code inside a Glyph string literal. It's hard to read, hard to debug, and hard to modify. Escape sequences like `\\n`, `\\{`, `\\\"` make it error-prone.

6. **O(n) hashmap.** Will bottleneck data-intensive programs.

### Low Risks

7. **Text-mode LLVM IR.** Works but is slower to compile than using the LLVM C API directly. Acceptable for current scale.

8. **No garbage collection.** OK for CLI tools and compilers (the primary use case). Only critical for long-running programs.

---

## 6. Recommended Implementation Order

| Priority | Feature | Effort | Impact |
|----------|---------|--------|--------|
| **P0** | Fix `>>` composition (correctness bug) | 1 hour | High — silent wrong results |
| **P0** | Implement `.field` accessor lambdas | 2 hours | High — enables idiomatic pipelines |
| **P1** | Expose existing runtime functions | 1 day | High — unlocks split/trim/starts_with/etc. |
| **P1** | Standard library (`stdlib.glyph`) | 2-3 days | Very high — foundation for all programs |
| **P1** | `--ffi` / `--pkg-config` flags | Half day | Medium — eliminates all build scripts |
| **P2** | Boehm GC integration | Half day | High for long-running programs |
| **P2** | HashMap performance | 1 day | Medium — only matters for data-heavy programs |
| **P2** | For-loops with range | 2 days | Medium — ergonomic improvement |
| **P2** | Const definitions | 1 day | Medium — eliminates zero-arg gotcha |
| **P3** | Fix generalization bug (BUG-007) | 3-5 days | Medium — enables polymorphic utility functions |
| **P3** | Better runtime error messages | 2 days | Medium — developer experience |
| **P4** | Minimal trait system | 1-2 weeks | High but very expensive |
| **P4** | True incremental compilation | 1 week | Medium — build speed at scale |
| **P4** | WASM target | 2-3 days | Low — niche use case |

---

## 7. Conclusion

Glyph has achieved something remarkable: a self-hosting compiler with three codegen backends, an MCP-powered LLM workflow, and 12 working example programs — all in ~525 KB of source code stored in a SQLite database. The core language (expressions, pattern matching, closures, type inference, monomorphization, TCO) is solid.

The most impactful next steps are not new language features but **filling gaps in what's already there**: fixing the composition operator, implementing field accessor lambdas, exposing runtime functions that already exist in C but aren't callable from Glyph, and creating a standard library. These changes would transform Glyph from "a compiler that works" to "a language you can write programs in without reinventing the wheel."

After that foundation is solid, the priorities shift to production-readiness: memory management (Boehm GC), HashMap performance, and for-loops. The trait system and WASM target are longer-term investments that depend on having the basics right first.
