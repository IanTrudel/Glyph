# Glyph Compiler Maturity Reassessment (2026-03-28)

**Compared against:** Chapter 3 of `documents/feature-assessment.md` (dated 2026-03-24)

---

## 1. Updated Quantitative Profile

| Metric | Original (Mar 24) | Current (Mar 28) | Delta |
|--------|-------------------|-------------------|-------|
| Total definitions | 1,586 | **1,720** | +134 |
| Function definitions | 1,287 | **1,351** | +64 |
| Test definitions | 287 | **356** | +69 (+24%) |
| Type definitions | 12 | **13** | +1 |
| Total source bytes (fn bodies) | 524,787 | **468,080** | -56,707 (refactoring) |
| Average function size | 339 chars | **346 chars** | +7 |
| Largest function | 5,353 (cg_runtime_c) | **5,759** (cg_runtime_c) | +406 |
| Database size | 1.8 MB | **2.7 MB** | +0.9 MB |
| Binary size (LLVM) | 431 KB | **465 KB** | +34 KB |
| Rust compiler LOC | ~10,636 | **~10,795** | +159 |
| Self-hosted namespaces | 20+ | **139 distinct** | significant growth |
| Example programs | 12 | **19** | +7 |
| Rust tests passing | 73/73 | **76/76** | +3 |
| Self-hosted tests | 287 | **356** | +69 |
| CLI commands | 28 | **33** | +5 |
| MCP tools | 15 | **22** | +7 |
| Codegen backends | 3 | 3 (unchanged) |
| Dependency graph edges | — | **3,529** | (new metric) |
| Extern declarations | — | **23** | (new metric) |
| C runtime modules | — | **19** (`cg_runtime_*`) | (new metric) |
| C runtime total bytes | — | **26,122** | (new metric) |
| Library ecosystem | 0 | **9 libraries** | entirely new |

---

## 2. Subsystem-by-Subsystem Reassessment

### Tokenizer — **Mature** (unchanged: A)
- 85 definitions in `tokenizer` namespace
- No significant changes since original assessment. Stable and production-quality.

### Parser — **Mature** (unchanged: A)
- 134 definitions in `parser` namespace (was 149 — likely refactoring/consolidation)
- Composition parsing (`parse_compose`, `parse_compose_loop`) still present and working
- Production-quality with good error messages

### Type Checker — **Functional, Improved** (B- → **B+**)
- **145 definitions** in `typeck` namespace (was 154 — refactored)
- **BUG-007 FIXED**: `inst_type` now uses `subst_walk` + `parse_all_fns` reversed order. Generalization works correctly.
- Row polymorphism working: `unify_records`, `unify_row_vars`, `unify_fields_against`
- Full generalize/instantiate pipeline: `generalize`, `generalize_raw`, `inst_type`, `inst_type_fields`, `inst_type_ns`
- 88 core type-checking functions (unify/env/pool/inst/tc/infer/subst/generalize)
- 21 type checker tests (up from an unspecified number)
- Registered builtins function is now 3,583 chars — significantly expanded
- **Remaining gaps**: No trait support, `?`/`!` not fully type-checked. But the generalization fix eliminates the biggest practical barrier.

### MIR Lowering — **Mature, Extended** (A- → **A**)
- 87 definitions in `lower` namespace + 102 in `mir` namespace (189 total)
- **Composition operator `>>` now lowered**: `lower_compose` exists and produces real closures (not silent unit). This was flagged as a P0 correctness bug — now fixed.
- **Field accessor `.field` now lowered**: `lower_field_accessor` exists and generates synthetic lambdas via `mk_synth_setup`. This was flagged as P0 — now fixed.
- 21+22 tests covering MIR and lowering
- The one dead arm from the original assessment (compose) is no longer dead.

### C Codegen — **Mature, Significantly Expanded** (B+ → **A-**)
- **109 definitions** in `codegen` namespace (was 107)
- **19 runtime modules** totaling **26,122 chars** of C runtime (was a single 5,353-char function). The runtime has been decomposed into:
  - `cg_runtime_c` (5,759) — core: includes, GC macros, GVal, strings, arrays
  - `cg_runtime_map` (3,556) — hashmap implementation
  - `cg_runtime_str2` (2,537) — extended string operations
  - `cg_runtime_sqlite` (2,510) — SQLite integration
  - `cg_runtime_result` (1,468) — Result/Option types
  - `cg_runtime_freeze` (1,341) — immutable data support
  - `cg_runtime_io` (1,296) — I/O operations
  - `cg_runtime_arr_util` (1,293) — array utilities
  - `cg_runtime_sb` (966) — string builder
  - `cg_runtime_coverage` (931) — coverage instrumentation
  - `cg_runtime_math` (852) — math functions
  - `cg_runtime_float` — float operations
  - `cg_runtime_bitset` — bitset operations
  - `cg_runtime_mcp` — MCP support
  - `cg_runtime_ref` — reference support
  - `cg_runtime_raw` — raw memory operations
  - `cg_runtime_args` — argc/argv handling
  - `cg_runtime_extra` — additional utilities
  - `cg_runtime_full` — full runtime assembly
- **Boehm GC integrated**: `cg_runtime_c` now includes `<gc/gc.h>` and `#define malloc(sz) GC_malloc(sz)`. This was the Tier 3 P2 recommendation — done.
- **`cc_prepend`/`cc_args` meta keys**: `build_program` accepts `meta_prepend` and `cc_extra` parameters, threaded through `cmd_build`, `cmd_run`, `cmd_test`, `compile_db`, and `load_libs_for_build`. The `--ffi`/`--pkg-config` flags recommendation is effectively addressed through the meta key system.
- 13 codegen tests

### LLVM IR Backend — **Complete** (unchanged: B+)
- 67 definitions in `llvm` namespace
- Stable, used for Stage 3 of bootstrap

### Monomorphization — **Complete** (unchanged: B+)
- 37 definitions in `mono` namespace
- Includes `mono_var_suffix`, `type_arg_to_suffix` for name mangling

### TCO — **Complete but Limited** (unchanged: B)
- 11 definitions
- Still only handles direct self-recursion

### MCP Server — **Mature, Significantly Expanded** (A → **A+**)
- **67 definitions** in `mcp` namespace (was 55)
- **22 tools** (was 15), with these additions since assessment:
  - `mcp_tool_put_defs` — batch definition insertion
  - `mcp_tool_check_all` — full type-check
  - `mcp_tool_init` — create new databases
  - `mcp_tool_migrate` — schema migrations
  - `mcp_tool_schema` — schema introspection
  - `mcp_tool_libs` — library dependency listing
  - `mcp_tool_unuse` — remove library dependency
- Multi-level dispatch chain (`mcp_tools_call` → `_call2` → `_call3` → `_call4`)
- JSON subsystem: 56 definitions in `json` namespace (unchanged)

### CLI — **Mature, Expanded** (A → **A+**)
- **33 commands** (was 28), new additions:
  - `cmd_link_do` — enhanced library linking
  - `cmd_unlink` — unlink libraries
  - `cmd_cover` — coverage report
  - `cmd_update` — update mechanism
  - `cmd_version` — version info
- 3-level dispatch chain (`dispatch_cmd` → `dispatch_cmd2` → `dispatch_cmd3`)
- 35 definitions in `cli` namespace

### Test Framework — **Improved** (B+ → **A-**)
- **356 tests** (was 287, +24%)
- Test distribution by subsystem:
  - Generic test namespace: 208
  - Parser: 22, MIR: 22, Type checker: 21, Lowering: 21
  - Utilities: 18, Codegen: 13, Tokenizer: 11, Build: 10
  - JSON: 7, MCP: 2, TCO: 1
- Coverage instrumentation (`--cover` flag, `cg_runtime_coverage`)

### Error Reporting — **Improved** (B- → **B+**)
- Parse errors with source context and caret positioning
- Type errors with structured reporting (`tc_report_errors`, `tc_report_loop`, `tc_prefix_errors`)
- Type detail function (`tc_type_detail`, `tc_tag_name`) for human-readable type names
- Runtime: SIGSEGV/SIGFPE handlers with full stack traces, `_glyph_current_fn` tracking

### Documentation — **Expanded** (B+ → **A-**)
- Thread safety design documents (5 files)
- Feature assessment document
- Self-hosted programming manual
- Formal language specification

---

## 3. New Capabilities Not in Original Assessment

### Library Ecosystem (entirely new)
9 linkable libraries with 204+ fn definitions and 82+ tests:

| Library | Functions | Tests | Purpose |
|---------|-----------|-------|---------|
| `stdlib.glyph` | 34 | 22 | map, filter, fold, sort, zip, range, all, any, find, join, etc. |
| `json.glyph` | 59 | 22 | JSON parsing and generation |
| `web.glyph` | 51 | 9 | HTTP server framework |
| `thread.glyph` | 25 | 10 | Threads, mutexes, atomics, channels, par_map |
| `scan.glyph` | 20 | 19 | Parser combinator library |
| `network.glyph` | 15 | 0 | Network primitives |
| `gtk.glyph` | — | — | GTK4 bindings |
| `x11.glyph` | — | — | X11 bindings |
| `async.glyph` | — | — | Async primitives |

The **stdlib** was the #1 Tier 1 recommendation. It now exists with: `map`, `filter`, `fold`, `sort`, `zip`, `range`, `all`, `any`, `find_index`, `flat_map`, `join`, `take`, `drop`, `each`, `contains`, `clamp`, `min`, `max`, `sum`, `product`, `concat_into`, `iabs`.

### Thread Safety / Concurrency (entirely new)
The `thread.glyph` library provides: `spawn`, `await`, `mutex_new/lock/unlock`, `with_lock`, `atomic_new/load/store/add/cas`, `chan_new/bounded/send/recv/close/closed/len`, `par_map`. Backed by C FFI (`thread_ffi.c`). This addresses what was listed as "not implemented" under spec Section 10.

### Memory Management (was "no management whatsoever")
Boehm GC is now integrated directly into `cg_runtime_c` via preprocessor macros. Every `malloc` redirects to `GC_malloc`. This was the recommended approach (option 3, Tier 3) and is done.

### Build System Meta Keys
`build_program` now accepts `meta_prepend` (for FFI C files) and `cc_extra` (for linker flags), loaded automatically from library metadata via `load_libs_for_build` / `load_lib_meta_loop`. This partially addresses the `--ffi`/`--pkg-config` recommendation through a library-metadata-driven approach.

### Immutable/Frozen Data
`cg_runtime_freeze` (1,341 chars) — support for immutable data structures, with design documents in `documents/thread-safety/`.

---

## 4. Recommendation Status from Original Assessment

| Priority | Feature | Original Status | Current Status |
|----------|---------|----------------|----------------|
| **P0** | Fix `>>` composition | Silent wrong results | **FIXED** — `lower_compose` implemented |
| **P0** | Implement `.field` accessor lambdas | Not lowered | **FIXED** — `lower_field_accessor` implemented |
| **P1** | Expose existing runtime functions | Not callable | **DONE** — 19 runtime modules, `register_builtins` expanded to 3,583 chars |
| **P1** | Standard library | Did not exist | **DONE** — `stdlib.glyph` with 34 functions |
| **P1** | `--ffi`/`--pkg-config` flags | Partially implemented | **DONE** — via `cc_prepend`/`cc_args` meta keys |
| **P2** | Boehm GC | Not integrated | **DONE** — `#define malloc(sz) GC_malloc(sz)` |
| **P2** | HashMap performance | O(n) linear scan | **Unchanged** — still `cg_runtime_map` (3,556 chars, linear probe) |
| **P2** | For-loops with range | Not implemented | **Unchanged** — `range` exists in stdlib but no `for` syntax |
| **P2** | Const definitions | Not implemented | **Unchanged** |
| **P3** | Fix generalization bug (BUG-007) | Broken | **FIXED** — `inst_type` uses `subst_walk` |
| **P3** | Better runtime error messages | Basic | **Improved** — full stack traces, SIGSEGV/SIGFPE handlers, structured type errors |
| **P4** | Minimal trait system | Not started | **Unchanged** |
| **P4** | True incremental compilation | Not used | **Unchanged** |
| **P4** | WASM target | Not started | **Unchanged** |

**7 of 14 recommendations completed. 1 partially improved. 6 unchanged.**

---

## 5. Updated Maturity Ratings

| Subsystem | Original | Current | Change |
|-----------|----------|---------|--------|
| Tokenizer | A | A | — |
| Parser | A | A | — |
| Type Checker | B- | **B+** | +2 (BUG-007 fixed, generalization working) |
| MIR Lowering | A- | **A** | +1 (compose + field accessor lowered) |
| C Codegen | B+ | **A-** | +1 (modular runtime, GC, meta keys) |
| LLVM Codegen | B+ | B+ | — |
| TCO | B | B | — |
| Monomorphization | B+ | B+ | — |
| Runtime | B | **B+** | +1 (Boehm GC, freeze, extended runtime) |
| MCP Server | A | **A+** | +1 (22 tools, batch ops, init/migrate) |
| CLI | A | **A+** | +1 (33 commands) |
| Test Framework | B+ | **A-** | +1 (356 tests, coverage instrumentation) |
| Error Reporting | B- | **B+** | +2 (structured type errors, stack traces) |
| Documentation | B+ | **A-** | +1 (thread-safety docs, feature assessment) |
| **Library Ecosystem** | — | **B+** | entirely new (9 libraries, 204+ fns) |
| **Concurrency** | — | **B** | entirely new (threads, channels, atomics) |

---

## 6. Revised Overall Maturity: **Beta**

The original assessment rated Glyph as "Late Alpha / Early Beta." Four days later, it has crossed into **Beta** territory:

- All P0 correctness bugs are fixed (composition, field accessors)
- The biggest practical gap (no standard library) is filled
- Memory management exists (Boehm GC)
- The library ecosystem went from 0 to 9 libraries
- Concurrency primitives now exist
- 22 MCP tools make LLM-driven development comprehensive
- 19 example programs demonstrate real-world viability
- 356 self-hosted tests provide reasonable regression coverage

**Remaining gaps for "Release Candidate"**: HashMap performance (still O(n)), no `for`-loop syntax, no const definitions, no trait system, no WASM target. Of these, only the HashMap and trait system are likely to be encountered as practical blockers.
