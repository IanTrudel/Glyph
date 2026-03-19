---
name: glyph
description: Programming in the Glyph language. Use when creating, reading, or modifying Glyph programs (.glyph SQLite databases).
---

# Glyph Programming Guide

## Mental Model

Programs are SQLite databases (`.glyph` files). Each definition (function, type, test) is a row in the `def` table. There are no source files, no imports — SQL queries replace the module system.

```
Program = SQLite DB with schema (def, dep, extern_, module, compiled, def_history, meta)
Definition = Row in def table: {name, kind, body, hash, tokens, compiled, ns, gen}
Dependency = Row in dep table: {from_id, to_id, edge}
```

The compiler reads definitions via `SELECT`, compiles each independently, and links them into a single executable. Incremental compilation uses BLAKE3 content hashes and the dependency graph.

## Workflow

**Primary: MCP tools** (preferred — no shell, structured results)

```
mcp__glyph__init(db="app.glyph")
mcp__glyph__put_def(db="app.glyph", name="add", kind="fn", body="add a b = a + b")
mcp__glyph__put_def(db="app.glyph", name="main", kind="fn",
  body='main = println(int_to_str(add(1, 2)))')
mcp__glyph__run(db="app.glyph")
```

**Secondary: CLI** (for interactive shell use)

```bash
./glyph init app.glyph
./glyph put app.glyph fn -b 'add a b = a + b'
./glyph put app.glyph fn -b 'main = println(int_to_str(add(1, 2)))'
./glyph run app.glyph
```

## MCP Tools

Start the MCP server with `./glyph mcp app.glyph`, or use the pre-configured `mcp__glyph__*` tools directly.

| Tool | Key params | Description |
|------|-----------|-------------|
| `mcp__glyph__init` | `db` | Create new .glyph database |
| `mcp__glyph__put_def` | `name, body, kind, db, gen` | Insert/update definition |
| `mcp__glyph__get_def` | `name, kind, db` | Read definition body |
| `mcp__glyph__remove_def` | `name, kind, db` | Delete definition |
| `mcp__glyph__list_defs` | `pattern, kind, gen, db` | List definitions |
| `mcp__glyph__search_defs` | `pattern, kind, db` | Search bodies |
| `mcp__glyph__check_def` | `body, kind, db` | Validate without inserting |
| `mcp__glyph__deps` | `name, db` | Forward dependencies |
| `mcp__glyph__rdeps` | `name, db` | Reverse dependencies |
| `mcp__glyph__sql` | `query, db` | Raw SQL |
| `mcp__glyph__build` | `db, output, flags` | Compile to native binary |
| `mcp__glyph__run` | `db, flags, stdin` | Build + execute, returns stdout |
| `mcp__glyph__coverage` | `db` | Coverage report from last test run |
| `mcp__glyph__link` | `lib, target, ns, prefix` | Link library defs into app |
| `mcp__glyph__migrate` | `target, db` | Apply pending schema migrations |

## CLI Commands

All commands: `./glyph <command> <db.glyph> [args...]`

| Command | Usage | Description |
|---------|-------|-------------|
| `init` | `init <db>` | Create new `.glyph` database with schema |
| `put` | `put <db> <kind> -b '<body>' [--gen N]` | Insert/update definition. Name extracted from body's first token. Kind: `fn`, `type`, `test`. Also: `-f <file>` to read body from file |
| `get` | `get <db> <name> [--kind K]` | Print a definition's source body |
| `rm` | `rm <db> <name> [--force]` | Remove definition. Checks reverse deps unless `--force` |
| `build` | `build <db> [output] [--debug\|--release]` | Compile all `fn` defs to native executable |
| `run` | `run <db>` | Build to temp file and execute immediately |
| `test` | `test <db> [name...] [--cover]` | Compile and run `test` defs. Filter by name |
| `cover` | `cover <db>` | Show coverage report from last `test --cover` run |
| `check` | `check <db>` | Type-check all definitions, report errors and warnings |
| `ls` | `ls <db> [--kind K] [--sort S]` | List definitions. Sort: `tokens`, `name`, or default |
| `find` | `find <db> <pattern> [--body]` | Search names (and optionally bodies) for pattern |
| `deps` | `deps <db> <name>` | Forward dependencies (requires prior `build`) |
| `rdeps` | `rdeps <db> <name>` | Reverse dependencies (requires prior `build`) |
| `stat` | `stat <db>` | Overview: def counts by kind, extern count, total tokens |
| `dump` | `dump <db> [--budget N] [--all]` | Token-budgeted context export |
| `sql` | `sql <db> <query>` | Execute raw SQL. SELECT returns formatted rows |
| `extern` | `extern <db> <name> <sym> <sig> [--lib L]` | Add FFI declaration to `extern_` table |
| `export` | `export <db> <dir>` | Export defs to `src/<ns>/<name>.<kind>.gl` files |
| `import` | `import <db> <dir>` | Import defs from file tree (reads ns from dir position) |
| `migrate` | `migrate <target.glyph>` | Apply pending schema migrations |
| `link` | `link <lib.glyph> <app.glyph> [--ns=N] [--prefix=P]` | Link library defs into app. `--ns` filters by namespace; `--prefix` renames `old_` prefix to `new_` |
| `undo` | `undo <db> <name> [--kind K]` | Undo last change (reversible) |
| `history` | `history <db> <name> [--kind K]` | Show change history for a definition |
| `mcp` | `mcp <db>` | Start MCP server on stdin/stdout |

## Definition Patterns

### Function (`kind='fn'`)
```
name params = body
name params = INDENT block DEDENT
```

```
mcp__glyph__put_def(db="app.glyph", name="double", kind="fn", body="double x = x * 2")
mcp__glyph__put_def(db="app.glyph", name="clamp", kind="fn",
  body="clamp lo hi x =
  match x < lo
    true -> lo
    _ -> match x > hi
      true -> hi
      _ -> x")
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

# Generic (parameterized)
Pair = <A,B>{fst:A, snd:B}
Stack = <T>{items:[T], size:I}
```

### Test (`kind='test'`)
```
test_name u = assert_eq(expr, expected)
test_name u = INDENT assertions... DEDENT
```

Note: test definitions take a dummy parameter (like all zero-arg side-effect functions).

## Language Features

### Closures
```
\x -> x + 1             -- single param
\x y -> x + y           -- multiple params
\x -> x + captured_var  -- closes over outer bindings (heap-allocated capture)
.name                   -- field accessor shorthand: equivalent to \x -> x.name
```

### Match Guards
```
match expr
  pattern -> body
  pattern ? guard_expr -> body    -- guard evaluated only if pattern matches
  _ -> default
```
Example: `n ? n > 100 -> "huge"` — matches any `n`, but only if `n > 100`.

### Or-Patterns
```
match cmd
  "quit" | "exit" | "q" -> do_quit()
  1 | 2 | 3 -> "small"
  _ -> "other"
```

### Let Destructuring
```
{x, y} = point_expr     -- binds x and y from record fields
```
No rename syntax in v1; field names must match binding names.

### Bitwise Operators
```
a bitand b   -- a & b
a bitor b    -- a | b
a bitxor b   -- a ^ b
a shl n      -- a << n
a shr n      -- a >> n
```

### Generic Type Definitions (syntactic only)
```
Pair = <A,B>{fst:A, snd:B}
Stack = <T>{items:[T], size:I}
```
The `<T>` syntax is accepted by the parser but type parameters are not enforced — all fields are stored as `GVal` at runtime. There is no true monomorphization or distinct code generation per type argument. Use generic type defs for documentation/intent only; the compiler will not catch type mismatches at instantiation sites.

### Loops
No for-loop syntax. Write loops as tail-recursive helper functions:
```
loop_helper arr i =
  match i >= array_len(arr)
    true -> 0
    _ -> arr[i] + loop_helper(arr, i + 1)
```

## Pitfalls

| Pitfall | Cause | Fix |
|---------|-------|-----|
| `{` in strings triggers interpolation | String interpolation syntax | Use `\{` for literal `{` |
| Map literals need double braces | `{k: v}` is a record literal | Use `hm_new()` + `hm_set` for maps |
| Zero-arg fn with side effects runs eagerly | Treated as constant | Add dummy param: `usage u = println(...)` |
| C keyword as fn name | C codegen conflict | Avoid: `double`, `int`, `float`, `void`, `return`, `if`, `while`, `for`, `struct`, `enum`, `const`, `static`, `extern` |
| `=` vs `:=` | `=` is let binding, `:=` is mutation | Use `:=` only for reassigning existing variables |
| No stdin | `read_file` uses fseek | Use `-b` flag or temp files |
| No GC | All heap allocs persist | Short-lived programs only |
| `deps`/`rdeps` empty | Dep table populated at build time | Run `./glyph build` first |
| Type checker warns on `!` | Unwrap not fully typed | Ignore warning; runtime is correct |
| `r[0]` on Result segfaults | Array indexing treats Result as array header | Use `match r` with `Ok(v)`/`Err(e)`, or `?`/`!` operators |
| Field offset ambiguity | Shared field names across record types | Access a type-unique field on the same variable |
| Gen mismatch on `put` | Default auto-detects gen | Use `--gen N` to target a specific generation explicitly |
| `True`/`False` segfaults | These are enum constructors, not bool literals | Use lowercase `true`/`false` in match patterns |

## Schema Quick Reference

```sql
-- Core tables
def(id, name, kind, sig, body, hash, tokens, compiled, gen, ns, created, modified)
dep(from_id, to_id, edge)         -- edge: calls|uses_type|implements|field_of|variant_of
extern_(id, name, symbol, lib, sig, conv)
module(id, name, doc)
module_member(module_id, def_id, exported)
compiled(def_id, ir, target, hash)
def_history(id, def_id, name, kind, sig, body, hash, tokens, gen, changed_at)
meta(key, value)                  -- schema_version, etc.

-- Useful views
v_dirty     -- dirty defs + transitive dependents
v_context   -- defs sorted by dependency depth
v_callgraph -- caller/callee/edge triples

-- kind values: fn, type, trait, impl, const, fsm, srv, macro, test
-- ns: auto-derived from name prefix (e.g., cg_→"codegen", ll_→"llvm", tok_→"tokenizer")
```

## Generational Versioning

The `def` table has a `gen` column (default 1):

- `--gen=N` selects the highest `gen <= N` per (name, kind) pair
- Gen=2 definitions override gen=1 when building with `--gen=2`
- Application programs typically use gen=1 (the default)

**Rollback**: Every `put`, `rm`, and `undo` saves the previous version to `def_history`. Use `glyph undo <db> <name>` to restore. Undo is reversible (running again swaps back).

## Two Compilers

| | Self-hosted (`./glyph`) | Rust (`cargo run --`) |
|---|---|---|
| Use for | App development | Compiler development |
| Backend | C codegen → cc | Cranelift → native |
| Build | `./glyph build app.glyph out` | `cargo run -- build app.glyph` |

Use `./glyph` for all application development. Use `cargo run --` only when modifying the Rust compiler or rebuilding `glyph.glyph`.

## Supporting Files

- [syntax-ref.md](syntax-ref.md) — Type aliases, operators, BNF, indentation rules
- [runtime-ref.md](runtime-ref.md) — All runtime functions with signatures
- [examples.md](examples.md) — Complete programs with MCP + CLI workflow
