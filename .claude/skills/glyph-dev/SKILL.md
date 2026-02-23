---
name: glyph-dev
description: Programming in the Glyph language. Use when creating, reading, or modifying Glyph programs (.glyph SQLite databases).
---

# Glyph Programming Guide

## Mental Model

Programs are SQLite databases (`.glyph` files). Each definition (function, type, test) is a row in the `def` table. There are no source files, no imports — SQL queries replace the module system.

```
Program = SQLite DB with schema (def, dep, extern_, tag, module, compiled)
Definition = Row in def table: {name, kind, body, hash, tokens, compiled}
Dependency = Row in dep table: {from_id, to_id, edge}
```

The compiler reads definitions via `SELECT`, compiles each independently, and links them into a single executable. Incremental compilation uses BLAKE3 content hashes and the dependency graph.

## Workflow

```bash
./glyph init app.glyph
./glyph put app.glyph fn -b 'add a b = a + b'
./glyph put app.glyph fn -b 'main = println(int_to_str(add(1, 2)))'
./glyph build app.glyph myapp && ./myapp
```

## CLI Commands

All commands: `./glyph <command> <db.glyph> [args...]`

| Command | Usage | Description |
|---------|-------|-------------|
| `init` | `init <db>` | Create new `.glyph` database with schema |
| `put` | `put <db> <kind> -b '<body>'` | Insert/update definition. Name extracted from body's first token. Kind: `fn`, `type`, `test`. Also: `-f <file>` to read body from file |
| `get` | `get <db> <name> [--kind K]` | Print a definition's source body |
| `rm` | `rm <db> <name> [--force]` | Remove definition. Checks reverse deps unless `--force` |
| `build` | `build <db> [output]` | Compile all `fn` defs to native executable (default: `a.out`) |
| `run` | `run <db>` | Build to temp file and execute immediately |
| `test` | `test <db> [name...]` | Compile and run `test` defs. Filter by name. Exit 0=pass, 1=fail |
| `check` | `check <db>` | Lightweight check: read all fn defs, report count |
| `ls` | `ls <db> [--kind K] [--sort S]` | List definitions. Sort: `tokens`, `name`, or default (kind+name) |
| `find` | `find <db> <pattern> [--body]` | Search names (and optionally bodies) for pattern |
| `deps` | `deps <db> <name>` | Forward dependencies (requires prior `build`) |
| `rdeps` | `rdeps <db> <name>` | Reverse dependencies (requires prior `build`) |
| `stat` | `stat <db>` | Overview: def counts by kind, extern count, total tokens, dirty count |
| `dump` | `dump <db> [--budget N] [--all]` | Token-budgeted context export |
| `sql` | `sql <db> <query>` | Execute raw SQL. SELECT returns formatted rows |
| `extern` | `extern <db> <name> <sym> <sig> [--lib L]` | Add FFI declaration to `extern_` table |

## Two Compilers

| | Self-hosted (`./glyph`) | Rust (`cargo run --`) |
|---|---|---|
| Use for | App development | Compiler development |
| Backend | C codegen → cc | Cranelift → native |
| Build | `./glyph build app.glyph out` | `cargo run -- build app.glyph` |
| Run | `./glyph run app.glyph` | `cargo run -- run app.glyph` |
| Tests | `./glyph test app.glyph` | `cargo run -- test app.glyph` |
| Triggers | Not persisted (safe) | TEMP triggers (session-only) |

Use `./glyph` for all application development. Use `cargo run --` only when modifying the Rust compiler itself or rebuilding `glyph.glyph`.

## Definition Patterns

### Function (`kind='fn'`)
```
name params = body
name params = INDENT block DEDENT
```

```bash
./glyph put app.glyph fn -b 'double x = x * 2'
./glyph put app.glyph fn -f /tmp/complex_fn.gl
```

### Type (`kind='type'`)
```
# Record
Point = {x: I, y: I}

# Enum
Color = | Red | Green | Blue
Option = | None | Some(I)

# Alias
Name = S
```

```bash
./glyph put app.glyph type -b 'Point = {x: I, y: I}'
./glyph put app.glyph type -b 'Color = | Red | Green | Blue'
```

### Test (`kind='test'`)
```
test_name u = assert_eq(expr, expected)
test_name u = INDENT assertions... DEDENT
```

Note: test definitions need a dummy parameter (like all zero-arg side-effect functions).

```bash
./glyph put app.glyph test -b 'test_math u = assert_eq(1 + 1, 2)'
```

## Pitfalls

| Pitfall | Cause | Fix |
|---------|-------|-----|
| `{` in strings triggers interpolation | String interpolation syntax | Use `\{` for literal `{` |
| Zero-arg fn with side effects runs eagerly | Treated as constant | Add dummy param: `usage u = println(...)` |
| C keyword as fn name | C codegen conflict | Avoid: `double`, `int`, `float`, `void`, `return`, `if`, `while`, `for`, `struct`, `enum`, `const`, `static`, `extern`, etc. |
| `=` vs `:=` | `=` is let binding, `:=` is mutation | Use `:=` only for reassigning existing variables |
| No stdin | `read_file` uses fseek | Use `-b` flag or temp files |
| No GC | All heap allocs persist | Short-lived programs only; no long-running servers |
| `deps`/`rdeps` empty | Dep table populated at build time | Run `./glyph build` first |
| Type checker warns on `!` | Unwrap not fully typed | Ignore warning; runtime is correct |
| `r[0]` on Result segfaults | Array indexing treats Result as array header | Use `match r` with `Ok(v)`/`Err(e)` patterns, or `?`/`!` operators |
| Field offset ambiguity | Shared field names across record types | Access a type-unique field on the same variable |
| tokens=0 from self-hosted | Self-hosted doesn't compute tokens | Run `cargo run -- build app.glyph --full` to fix |
| Gen=2 only in self-hosted | Rust compiler ignores `gen` column | Build gen=2 defs with `./glyph0 build --gen=2` |
| Self-hosted can't self-build | Sees both gen=1 and gen=2 overrides | Use `./glyph0 build glyph.glyph --gen=N` to build compiler |

## Schema Quick Reference

```sql
-- Core tables
def(id, name, kind, sig, body, hash, tokens, compiled, gen, created, modified)
dep(from_id, to_id, edge)         -- edge: calls|uses_type|implements|field_of|variant_of
extern_(id, name, symbol, lib, sig, conv)
tag(def_id, key, val)
module(id, name, doc)
module_member(module_id, def_id, exported)
compiled(def_id, ir, target, hash)

-- Useful views
v_dirty     -- dirty defs + transitive dependents
v_context   -- defs sorted by dependency depth
v_callgraph -- caller/callee/edge triples

-- kind values: fn, type, trait, impl, const, fsm, srv, macro, test
```

## Generational Versioning

The `def` table has a `gen` column (default 1) for generational overrides:

- `--gen=N` flag selects the highest `gen <= N` per (name, kind) pair
- Gen=2 definitions override gen=1 when building with `--gen=2`
- A gen=2 definition with the same name/kind as a gen=1 definition replaces it
- Definitions without overrides at a given gen are inherited from lower generations

```bash
./glyph0 build glyph.glyph --full --gen=1   # Build with gen=1 defs only
./glyph0 build glyph.glyph --full --gen=2   # Build with gen=2 overrides
```

This is used internally for compiler evolution. Application programs typically don't need generational versioning — all definitions are gen=1 by default.

## Struct Codegen (Gen=2)

Gen=2 adds named record types to C codegen. When a program defines record types via `kind='type'`:

```
Stats = {count: I, max_val: I, min_val: I, sum: I}
```

The compiler generates a C typedef:
```c
typedef struct { long long count; long long max_val; long long min_val; long long sum; } Glyph_Stats;
```

**How it works**:
- Record fields are sorted alphabetically (BTreeMap ordering)
- MIR record aggregates are matched against type definitions by sorted field set equality
- Matched records use `((Glyph_Stats*)p)->field` access
- Anonymous records (no matching type def) fall back to offset-based `((long long*)p)[N]` access
- Type definitions are inserted as `kind='type'` in the `def` table

**Using named types in programs**:
```bash
./glyph put app.glyph type -b 'Point = {x: I, y: I}'
./glyph put app.glyph fn -b 'make_point x y = {x: x, y: y}'
./glyph put app.glyph fn -b 'get_x p = p.x'
```

Named types improve generated C readability and enable struct-based field access, but are functionally equivalent to anonymous records at the language level.

## Supporting Files

- [syntax-ref.md](syntax-ref.md) — Type aliases, operators, BNF, indentation rules
- [runtime-ref.md](runtime-ref.md) — All runtime functions with signatures
- [examples.md](examples.md) — 9 complete programs with CLI workflow

## Build System (Compiler Development)

The compiler itself uses a 3-stage bootstrap managed by `ninja`:

```bash
ninja          # Full 3-stage bootstrap: glyph0 → glyph → glyph_new (must match)
ninja test     # Run test_comprehensive.glyph through self-hosted compiler
ninja dump     # Regenerate glyph.sql from glyph.glyph database
```

Stage 0 (`glyph0`): Rust compiler via `cargo build --release`
Stage 1 (`glyph`): Cranelift-compiled from glyph.glyph
Stage 2 (`glyph_new`): Self-hosted C-codegen from glyph.glyph (must match stage 1 output)
