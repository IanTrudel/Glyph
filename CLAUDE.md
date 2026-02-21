# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Glyph is an LLM-native programming language where **programs are SQLite3 databases** (`.glyph` files), not source files. The unit of storage is the *definition*, and SQL queries replace the traditional module/import system. The language is designed for token-minimal syntax to minimize BPE token count for LLM generation and consumption.

**Current status:** Working compiler (v0.1). Can parse, type-check, lower to MIR, codegen via Cranelift, and link native executables.

## Build Commands

```bash
cargo build                    # build the compiler
cargo test                     # run all tests (60 tests across 5 crates)
cargo run -- <subcommand>      # run the glyph CLI
```

## Compiler CLI (`glyph`)

```bash
glyph init program.glyph               # create a new .glyph database
glyph build program.glyph              # compile dirty definitions
glyph build program.glyph --full       # recompile everything
glyph build program.glyph --emit-mir   # show MIR for debugging
glyph run program.glyph                # build + execute main
glyph check program.glyph              # type-check only
glyph test program.glyph               # run test definitions
```

## Workspace Structure

```
Cargo.toml (workspace)
crates/
  glyph-db/        # SQLite schema, custom functions (glyph_hash, glyph_tokens), DB access
  glyph-parse/     # Indentation-sensitive lexer, recursive-descent parser, full AST
  glyph-typeck/    # Name resolution, HM inference + row polymorphism, unification
  glyph-mir/       # MIR (flat CFG), lowering, pattern match compilation, serde cache
  glyph-codegen/   # Cranelift codegen, ABI, system linker invocation, minimal C runtime
  glyph-cli/       # `glyph` binary — init/build/run/check/test commands
```

**Dependency flow (strict, no cycles):**
```
glyph-cli → glyph-codegen → glyph-mir → glyph-typeck → glyph-parse → glyph-db
```

## Compilation Pipeline

```
.glyph DB (SQLite) → SELECT dirty defs → Parser (per-definition) → Resolver (queries DB)
→ Type Infer (HM + row polymorphism) → MIR (monomorphized) → Cranelift → Linker → executable
```

Incremental compilation: content-hash (BLAKE3) each definition, recompile only dirty defs and their transitive dependents via the `dep` table dependency graph.

## Core Database Schema

The program *is* its schema. Key tables:
- **`def`** — all definitions (fn, type, trait, impl, const, fsm, srv, macro, test) with source body, type sig, hash, token count, compiled flag
- **`dep`** — dependency graph edges (calls, uses_type, implements, field_of, variant_of)
- **`extern_`** — C ABI foreign function declarations (note: underscore suffix to avoid SQL keyword)
- **`module` / `module_member`** — logical grouping with export flags
- **`compiled`** — cached compilation artifacts (serialized MIR per target)

Key views: `v_dirty` (dirty + transitive dependents), `v_context` (defs sorted by dep depth), `v_callgraph`.

## Inserting Definitions

Definitions are inserted via SQL (the LLM interface). Hash and tokens must be provided on INSERT:

```sql
INSERT INTO def (name, kind, body, hash, tokens)
VALUES ('main', 'fn', 'main = 42',
        X'0000000000000000000000000000000000000000000000000000000000000000', 3);
```

When using the glyph-db Rust API, `insert_def()` computes hash and tokens automatically.

## Language Key Conventions

- Single-char type aliases: `I`=Int64, `U`=UInt64, `F`=Float64, `S`=Str, `B`=Bool, `V`=Void, `N`=Never
- Shorthand type constructors: `?T`=Optional, `!T`=Result, `[T]`=Array, `{K:V}`=Map, `&T`=Ref, `*T`=Ptr
- No `return`/`let`/`import` keywords; no semicolons or braces (indentation-sensitive)
- Pipe `|>`, compose `>>`, error propagate `?`, unwrap `!`
- `.field` without a preceding value creates a lambda: `.name` ≡ `\x -> x.name`
- LLM interaction: read context via SQL SELECT, write definitions via SQL INSERT/UPDATE
