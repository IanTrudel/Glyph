# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Glyph is an LLM-native programming language where **programs are SQLite3 databases** (`.glyph` files), not source files. The unit of storage is the *definition*, and SQL queries replace the traditional module/import system. The language is designed for token-minimal syntax to minimize BPE token count for LLM generation and consumption.

**Current status:** Working compiler (v0.2). Two compilers exist:
- **Self-hosted compiler** (`glyph.glyph` → `./glyph`): ~1,019 definitions, C codegen backend, 19 CLI commands, MCP server. This is the primary compiler.
- **Rust compiler** (`cargo run -- ...`): Cranelift backend, used as bootstrap tool (`glyph0`) to rebuild `glyph.glyph`.

## Build Commands

### Bootstrap chain (build the self-hosted compiler)

```bash
ninja                          # full bootstrap: glyph0 → glyph
ninja test                     # run all tests (Rust + self-hosted)
```

### Rust compiler (bootstrap tool)

```bash
cargo build                    # build the Rust compiler
cargo test                     # run all Rust tests (73 tests across 6 crates)
cargo run -- <subcommand>      # run the Rust compiler CLI
```

## Self-Hosted CLI (`./glyph`) — Primary Compiler

```bash
./glyph init program.glyph                    # create a new .glyph database
./glyph put program.glyph fn -b 'main = 42'   # insert/update a definition
./glyph put program.glyph fn -f /tmp/code.gl   # insert from file
./glyph put program.glyph type -b 'P = {x: I, y: I}'  # insert a type
./glyph get program.glyph main                 # print a definition's body
./glyph rm program.glyph old_fn                # remove a definition
./glyph build program.glyph                    # compile to native executable
./glyph build program.glyph out --debug        # debug build (O0 -g)
./glyph build program.glyph out --release      # release build (O2)
./glyph run program.glyph                      # build + execute main
./glyph test program.glyph                     # run test definitions
./glyph test program.glyph test_foo test_bar   # run specific tests
./glyph check program.glyph                    # type-check only
./glyph ls program.glyph                       # list definitions
./glyph find program.glyph pattern             # search definitions
./glyph deps program.glyph fn_name             # forward dependencies
./glyph rdeps program.glyph fn_name            # reverse dependencies
./glyph stat program.glyph                     # overview statistics
./glyph dump program.glyph --budget 1000       # token-budgeted export
./glyph sql program.glyph "SELECT ..."         # raw SQL
./glyph extern program.glyph name sym sig      # add FFI declaration
./glyph undo program.glyph fn_name             # undo last change
./glyph history program.glyph fn_name          # show change history
./glyph mcp program.glyph                      # start MCP server
```

## Rust Compiler CLI (`cargo run --` / `glyph0`) — Bootstrap Only

```bash
glyph0 init program.glyph                     # create a new .glyph database
glyph0 build program.glyph                    # compile dirty definitions
glyph0 build program.glyph --full             # recompile everything
glyph0 build program.glyph --emit-mir         # show MIR for debugging
glyph0 run program.glyph                      # build + execute main
glyph0 check program.glyph                    # type-check only
glyph0 test program.glyph                     # run test definitions
```

## Workspace Structure

```
Cargo.toml (workspace)
glyph.glyph             # self-hosted compiler source (SQLite database, ~1,019 definitions)
build.ninja              # bootstrap build rules
crates/
  glyph-db/              # SQLite schema, custom functions (glyph_hash, glyph_tokens), DB access
  glyph-parse/           # Indentation-sensitive lexer, recursive-descent parser, full AST
  glyph-typeck/          # Name resolution, HM inference + row polymorphism, unification
  glyph-mir/             # MIR (flat CFG), lowering, pattern match compilation, serde cache
  glyph-codegen/         # Cranelift codegen, ABI, system linker invocation, minimal C runtime
  glyph-cli/             # `glyph` binary — init/build/run/check/test commands
examples/
  calculator/            # Expression calculator REPL
  glint/                 # Project analyzer (SQLite FFI)
  gstats/                # Statistical analyzer (named record types)
  gled/                  # Terminal text editor (ncurses FFI)
  life/                  # Conway's Game of Life (X11 GUI)
  benchmark/             # Performance comparison (Glyph vs C)
documents/
  glyph-self-hosted-programming-manual.md  # comprehensive language manual
  glyph-spec.md          # formal language specification
```

**Rust crate dependency flow (strict, no cycles):**
```
glyph-cli → glyph-codegen → glyph-mir → glyph-typeck → glyph-parse → glyph-db
```

## Compilation Pipeline

**Self-hosted compiler (primary):**
```
.glyph DB → SELECT defs → Tokenizer → Parser → Type Infer (HM) → MIR → C codegen → cc → executable
```

**Rust compiler (bootstrap):**
```
.glyph DB → SELECT dirty defs → Parser → Resolver → Type Infer (HM + row polymorphism) → MIR → Cranelift → Linker → executable
```

Incremental compilation: content-hash (BLAKE3) each definition, recompile only dirty defs and their transitive dependents via the `dep` table dependency graph.

## Bootstrap Chain

```
Stage 0: glyph0 (Rust compiler, cargo build --release)
    │  Compiles glyph.glyph via Cranelift
    ▼
Stage 1: glyph (self-hosted, C codegen, ~307k binary)
    │  Compiles user .glyph programs to C → native
    ▼
Programs: user applications
```

Build with `ninja` (or manually: `cargo build --release && cp target/release/glyph glyph0 && ./glyph0 build glyph.glyph --full`).

## Core Database Schema

The program *is* its schema. Key tables:
- **`def`** — all definitions (fn, type, test, trait, impl, const, fsm, srv, macro) with source body, type sig, hash, token count, compiled flag, generation
- **`dep`** — dependency graph edges (calls, uses_type, implements, field_of, variant_of)
- **`extern_`** — C ABI foreign function declarations (note: underscore suffix to avoid SQL keyword)
- **`module` / `module_member`** — logical grouping with export flags
- **`compiled`** — cached compilation artifacts (serialized MIR per target)
- **`def_history`** — automatic change history (triggers track DELETE/UPDATE on def)
- **`meta`** — schema version and metadata

Key views: `v_dirty` (dirty + transitive dependents), `v_context` (defs sorted by dep depth), `v_callgraph`.

## Inserting Definitions

**Preferred method** — use the self-hosted CLI:

```bash
./glyph put program.glyph fn -b 'main = println("hello")'
./glyph put program.glyph fn -f /tmp/complex_fn.gl          # from file
./glyph put program.glyph type -b 'Point = {x: I, y: I}'    # type definition
./glyph put program.glyph test -b 'test_add u = assert_eq(1+1, 2)'
```

**Via SQL** (raw interface). Hash and tokens must be provided on INSERT:

```sql
INSERT INTO def (name, kind, body, hash, tokens)
VALUES ('main', 'fn', 'main = 42',
        X'0000000000000000000000000000000000000000000000000000000000000000', 3);
```

When using the glyph-db Rust API, `insert_def()` computes hash and tokens automatically.

## Generational Versioning

The `def` table has a `gen` column (default 1). The `--gen=N` flag selects the highest `gen <= N` per (name, kind) pair, allowing multiple versions of a definition to coexist. All compiler definitions are gen=1. Application programs use gen=1 (the default).

## Language Key Conventions

- Single-char type aliases: `I`=Int64, `U`=UInt64, `F`=Float64, `S`=Str, `B`=Bool, `V`=Void, `N`=Never
- Shorthand type constructors: `?T`=Optional, `!T`=Result, `[T]`=Array, `{K:V}`=Map, `&T`=Ref, `*T`=Ptr
- No `return`/`let`/`import` keywords; no semicolons or braces (indentation-sensitive)
- Pipe `|>`, compose `>>`, error propagate `?`, unwrap `!`
- `.field` without a preceding value creates a lambda: `.name` ≡ `\x -> x.name`
- Bitwise operators use keywords: `bitand`, `bitor`, `bitxor`, `shl`, `shr`
- Match guards: `pat ? guard_expr -> body`
- Or-patterns: `1 | 2 | 3 -> body`
- LLM interaction: read context via SQL SELECT, write definitions via `./glyph put` or SQL INSERT/UPDATE
