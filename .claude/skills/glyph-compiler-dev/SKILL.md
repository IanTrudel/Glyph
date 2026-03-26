---
name: glyph-compiler-dev
description: Working on the Glyph compiler itself (Rust crates or self-hosted glyph.glyph). Use when modifying compiler internals, adding language features, fixing codegen bugs, or working on the bootstrap chain.
---

# Glyph Compiler Development Guide

## Mental Model

Two compilers exist — both produce native executables from the same `.glyph` database input:

```
Rust Compiler (glyph0)           Self-Hosted Compiler (glyph)
DB → Parser → Resolver →         DB → tokenize → parse_fn_def →
  TypeInfer → MirLower →           lower_fn_def → cg_program →
  Cranelift → Linker → EXE          write_file → cc → EXE
```

- **Rust compiler** (6 crates, ~10k LOC): Cranelift backend, full type checking, incremental compilation
- **Self-hosted compiler** (glyph.glyph, ~1,651 definitions): C codegen backend, LLVM IR backend (`--emit=llvm`), MCP server (stdio transport, 18 tools), type system wired into `glyph check`, everything is `long long`, Boehm GC integrated
- The compiler IS a SQLite database — `glyph.glyph` contains all self-hosted definitions as rows

**Bootstrap chain (4-stage):**
```
cargo build → glyph0 (Rust/Cranelift)
glyph0 build glyph.glyph → glyph1 (Cranelift binary)
glyph1 build glyph.glyph → glyph2 (C-codegen binary)
glyph2 build glyph.glyph --emit=llvm → glyph (final LLVM-compiled binary)
```

## Which Compiler to Modify

**The Rust compiler (`glyph0`) is in maintenance mode** — it exists only to bootstrap glyph.glyph. New language features and capabilities go in `glyph.glyph` (self-hosted). Only modify the Rust crates when glyph.glyph uses a feature that glyph0 cannot parse or compile.

| Task | Modify | Build/Test |
|------|--------|-----------|
| Language syntax/parsing | `glyph.glyph` (self-hosted); Rust only if bootstrap needs it | `ninja` |
| Type system | `glyph.glyph` (self-hosted); Rust `glyph-typeck` only for bootstrap | `ninja` |
| MIR lowering | `glyph.glyph` primarily | `ninja` |
| C codegen (gen=1/gen=2) | `glyph.glyph` only | `ninja` |
| Cranelift codegen | Rust: `glyph-codegen` (maintenance only) | `cargo test -p glyph-codegen` |
| Runtime functions | Both: `runtime.rs` + `cg_runtime_*` (must stay in sync for bootstrap) | See [recipes](modification-recipes.md) |
| CLI commands | `glyph.glyph` | `ninja` |
| Schema changes | Rust: `glyph-db` + `glyph.glyph` | Both |
| LLVM IR emission | `glyph.glyph` only (`ll_*` defs) | `./glyph build --emit=llvm` |
| MCP server | `glyph.glyph` | `ninja` |
| Monomorphization | `glyph.glyph` only (`mono_*` defs) | `ninja` |

## Build & Test

```bash
# Rust compiler
cargo build && cargo test              # Build + 73 tests
cargo run -- build app.glyph           # Test against a program
cargo run -- build app.glyph --gen=2   # Build with gen=2 struct codegen

# Self-hosted compiler (full bootstrap)
ninja                                   # 4-stage: glyph0 → glyph1 → glyph2 → glyph
ninja test                              # Rust tests + self-hosted regression

# Quick iteration on self-hosted changes (MCP preferred — see recipes)
./glyph0 build glyph.glyph --full --gen=1    # Rebuild gen=1 only
./glyph0 build glyph.glyph --full --gen=2    # Rebuild with gen=2 overrides
./glyph test glyph.glyph                     # Run full self-hosted test suite (315 tests)

# MCP workflow (primary — no shell escaping, structured errors)
./glyph mcp glyph.glyph               # Start MCP server; use mcp__glyph__* tools in Claude

# Inspect generated C
cat /tmp/glyph_out.c                   # Last compiled C output
```

## Pipeline Side-by-Side

```
           Rust Compiler                      Self-Hosted Compiler
           ─────────────                      ────────────────────
Input:     Database::open()                   glyph_db_open()
           dirty_defs_gen()/effective_defs()  read_fn_defs() via SQL SELECT
                ↓                                  ↓
Parse:     Lexer::tokenize() → Parser         tokenize() → parse_fn_def()
           → AST (Expr, Stmt, Pattern)        → AstNode pool (integer indices)
                ↓                                  ↓
Types:     Resolver → InferEngine             tc_pre_register → tc_infer_all
           → typed AST + deps                  (available via `glyph check`)
                ↓                                  ↓
MIR:       MirLower::lower_fn()               lower_fn_def()
           → MirFunction (Rust structs)       → parallel arrays (stmts, terms)
                ↓                                  ↓
Post:      (field offsets in codegen)          fix_all_field_offsets()
                                              fix_extern_calls()
                ↓                                  ↓
Codegen:   CodegenContext::compile_function()  cg_program() / cg_function()
           → Cranelift IR → object file       → C source string
                ↓                                  ↓
Link:      link_with_extras() → cc            glyph_system("cc ...") → EXE
```

## Key Conventions (Self-Hosted)

| Convention | Explanation |
|-----------|-------------|
| Recursive `*_loop` suffix | No loops — all iteration is tail-recursive |
| `match true/_ ->` | No `if/else` — match on bool for branching |
| `counter = [0]` + `raw_set` | Single-element arrays for mutable state |
| `tk_int = 1`, `rv_use = 1` | Integer constant functions for enum-like values |
| Chain functions | Split at ~30 match arms: `dispatch_cmd` → `dispatch_cmd2` → ... |
| Unique field prefixes | `okind/oval/ostr`, `sdest/skind`, `tkind/top` — avoids offset ambiguity |
| `s2()`–`s7()` | String concat helpers for C codegen output |
| `cg_lbrace()`/`cg_rbrace()` | C braces in strings (avoids interpolation) |

## Pitfalls

| Pitfall | Why | Workaround |
|---------|-----|-----------|
| `{` in string literals | Triggers Rust compiler's string interpolation | Use `cg_lbrace()`/`cg_rbrace()` or `"\{"` in self-hosted |
| Zero-arg side-effect fn | Evaluated eagerly (treated as constant) | Add dummy param: `usage u = ...` |
| Self-hosted can't self-build gen=2 | Sees both gen=1 and gen=2 overrides with same names | Use `./glyph0 build --gen=2` |
| `tokens=0` from self-hosted | Self-hosted doesn't compute BLAKE3 hash or BPE tokens | Run `cargo run -- build --full` for correct values |
| Runtime fn names in Cranelift | Runtime functions (e.g., `int_to_str`) are resolved with or without `glyph_` prefix | User code can call `int_to_str(x)` directly; Cranelift resolves to `glyph_int_to_str` |
| MIR has no type info | Build pipeline skips type system (available via `glyph check`) | Runtime errors only; `local_types` tracks strings heuristically |
| Field offset ambiguity | Multiple record types share field names | Use unique field prefixes; `find_best_type` picks largest match |
| C keyword as fn name | `double`, `int`, `void` etc. collide in generated C | Avoid C reserved words in Glyph function names |
| `s2()` nesting ~7 limit | Stack overflow in Cranelift binary with deep nesting | Combine strings at same nesting level |
| Gen=2 parameter reads | Field-access tagging covers most cases; edge cases fall back | Use unique field prefixes for reliable struct detection |
| Extern headers | Wrappers only see stdlib includes | Heavy FFI: use separate C wrapper file via `cc_prepend` meta key or manual concatenation |
| String pattern matching | `parse_single_pattern` double-stripping: extracted token text is already unquoted, so `glyph_str_slice(s, 1, len-1)` gives wrong result | Fixed 2026-03-19; watch for similar bugs when adding new pattern kinds |
| JNode `.sval` field access | `find_best_type` picks AstNode (7 fields) over JNode (3 fields) — wrong offset | Add `_ = node.tag` hint in JSON functions that access JNode `.sval` without dispatching on `.tag` |
| Closure calling convention | Raw fn refs (`&fn_name`) in arrays/records crash — closure convention dereferences `f[0]` for fn ptr | Wrap in lambdas: `\x -> fn_name(x)` creates proper closure struct |
| Multi-line lambdas | Only single-line `\x -> expr` parses | Use helper function pattern: `helper f x = ...` + `\x -> helper(f, x)` |
| String interp on record fields | Type checker may infer record fields as int; `"{r.field}"` calls `int_to_str` on a string | Use explicit `+` concatenation: `r.field + " " + r.other` |

## Build Modes

| Mode | Flag | cc flags | Debug instrumentation |
|------|------|----------|-----------------------|
| Default | (none) | `-DGLYPH_DEBUG -O1` | ON (stack trace, null checks, segfault handler) |
| Debug | `--debug` | `-DGLYPH_DEBUG -O0 -g` | ON + debug symbols for gdb |
| Release | `--release` | `-O2` | Compiled out by preprocessor |

All debug instrumentation is wrapped in `#ifdef GLYPH_DEBUG` in the generated C. The `--release` flag omits `-DGLYPH_DEBUG` so the preprocessor strips all debug code. Array bounds checks remain in all modes.

**Stack traces:** On segfault, shows current function + full call chain:
```
segfault in count_nl
--- stack trace ---
  count_lines
  sum_lines
  report_db
  run_report
  main
```

## Debugging

**Segfaults:** Default builds show full stack traces. For deeper investigation with gdb: `./glyph build app.glyph --debug && gdb ./app`

**Generated C:** Always inspect `/tmp/glyph_out.c` after a build. Search for the function name to see what codegen produced.

**Parse errors:** Use the Rust compiler (`cargo run -- build app.glyph`) for better diagnostics — it has line:col with source context and caret.

**MIR:** `cargo run -- build app.glyph --emit-mir` dumps MIR from the Rust compiler. The self-hosted compiler has no MIR dump.

**Field offsets:** If struct access gives wrong values, check alphabetical field order (fields are sorted A-Z, 0-indexed). Verify gen=2 `build_local_types` tagged the local.

**Type system:** `./glyph check app.glyph` runs inference. Known issue: crashes on some record patterns.

**Compiler crashes:** If the self-hosted compiler itself segfaults, the SIGSEGV handler shows which *compiler* function crashed. Read it with `./glyph get glyph.glyph <fn_name>`.

See [modification-recipes.md](modification-recipes.md) Recipe 12 for the full debugging guide.

## Crate & File Map

```
crates/
  glyph-db/       connection.rs (229), queries.rs (338), model.rs (170),
                   schema.rs (150), functions.rs (48)
  glyph-parse/    parser.rs (1760), lexer.rs (827), ast.rs (301),
                   token.rs (83), span.rs (105)
  glyph-typeck/   infer.rs (815), unify.rs (366), resolve.rs (288),
                   types.rs (224)
  glyph-mir/      lower.rs (1466), ir.rs (251)
  glyph-codegen/  cranelift.rs (908), runtime.rs (637), layout.rs (127),
                   linker.rs (74)
  glyph-cli/      build.rs (677), main.rs (121)
```

Dependency chain (strict, no cycles):
```
glyph-cli → glyph-codegen → glyph-mir → glyph-typeck → glyph-parse → glyph-db
```

## Supporting Files

- [rust-compiler.md](rust-compiler.md) — Crate-by-crate guide to the Rust compiler
- [self-hosted-compiler.md](self-hosted-compiler.md) — Subsystem guide for glyph.glyph (~1,651 definitions)
- [modification-recipes.md](modification-recipes.md) — Step-by-step recipes for common compiler changes
- [documents/glyph-compiler-reference.md](../../../documents/glyph-compiler-reference.md) — Exhaustive reference (~950 lines)
