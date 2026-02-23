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
- **Self-hosted compiler** (glyph.glyph, ~647 definitions): C codegen backend, type system wired into `glyph check`, everything is `long long`
- The compiler IS a SQLite database — `glyph.glyph` contains all self-hosted definitions as rows

**Bootstrap chain (2-stage):**
```
cargo build → glyph0 (Rust/Cranelift)
glyph0 build glyph.glyph --gen=2 → glyph (Cranelift binary with gen=2 struct codegen)
```

## Which Compiler to Modify

| Task | Modify | Build/Test |
|------|--------|-----------|
| Language syntax/parsing | Rust: `glyph-parse` | `cargo test -p glyph-parse` |
| Type system | Rust: `glyph-typeck` | `cargo test -p glyph-typeck` |
| MIR lowering | Rust: `glyph-mir` AND/OR `glyph.glyph` | Both have MIR lowering |
| C codegen (gen=1/gen=2) | `glyph.glyph` only | `ninja` |
| Cranelift codegen | Rust: `glyph-codegen` | `cargo test -p glyph-codegen` |
| Runtime functions | Both: `runtime.rs` + `cg_runtime_*` | See [recipes](modification-recipes.md) |
| CLI commands | `glyph.glyph` | `ninja` |
| Schema changes | Rust: `glyph-db` + `glyph.glyph` | Both |

## Build & Test

```bash
# Rust compiler
cargo build && cargo test              # Build + 68 tests
cargo run -- build app.glyph           # Test against a program
cargo run -- build app.glyph --gen=2   # Build with gen=2 struct codegen

# Self-hosted compiler (full bootstrap)
ninja                                   # 2-stage: glyph0 → glyph (gen=2 via Cranelift)
ninja test                              # Rust tests + self-hosted regression

# Quick iteration on self-hosted changes
./glyph0 build glyph.glyph --full --gen=1    # Rebuild gen=1 only
./glyph0 build glyph.glyph --full --gen=2    # Rebuild with gen=2 overrides
./glyph build test_comprehensive.glyph test_out && ./test_out  # Regression

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
| MIR has no type info | Build pipeline skips type system (available via `glyph check`) | Runtime errors only; `local_types` tracks strings heuristically |
| Field offset ambiguity | Multiple record types share field names | Use unique field prefixes; `find_best_type` picks largest match |
| C keyword as fn name | `double`, `int`, `void` etc. collide in generated C | Avoid C reserved words in Glyph function names |
| `s2()` nesting ~7 limit | Stack overflow in Cranelift binary with deep nesting | Combine strings at same nesting level |
| Gen=2 parameter reads | Field-access tagging covers most cases; edge cases fall back | Use unique field prefixes for reliable struct detection |
| Extern headers | Wrappers only see stdlib includes | Heavy FFI: use separate C wrapper file instead of extern_ table |

## Debugging

**Segfaults:** The C runtime prints `segfault in function: <name>` on SIGSEGV. For deeper investigation: `cc -g -O0 -o debug /tmp/glyph_out.c && gdb ./debug`

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
  glyph-parse/    parser.rs (1693), lexer.rs (755), ast.rs (301),
                   token.rs (83), span.rs (105)
  glyph-typeck/   infer.rs (791), unify.rs (366), resolve.rs (288),
                   types.rs (224)
  glyph-mir/      lower.rs (1306), ir.rs (251)
  glyph-codegen/  cranelift.rs (893), runtime.rs (637), layout.rs (127),
                   linker.rs (74)
  glyph-cli/      build.rs (688), main.rs (121)
```

Dependency chain (strict, no cycles):
```
glyph-cli → glyph-codegen → glyph-mir → glyph-typeck → glyph-parse → glyph-db
```

## Supporting Files

- [rust-compiler.md](rust-compiler.md) — Crate-by-crate guide to the Rust compiler
- [self-hosted-compiler.md](self-hosted-compiler.md) — Subsystem guide for glyph.glyph (~647 definitions)
- [modification-recipes.md](modification-recipes.md) — Step-by-step recipes for common compiler changes
- [documents/glyph-compiler-reference.md](../../../documents/glyph-compiler-reference.md) — Exhaustive reference (~950 lines)
