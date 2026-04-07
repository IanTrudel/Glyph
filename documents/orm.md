# ORM Considerations for Glyph

## Why a Traditional ORM Doesn't Fit

- **LLMs write SQL fluently.** The main reason ORMs exist — humans finding SQL tedious or error-prone — doesn't apply when the only users are LLMs. SQL is arguably more token-efficient than an ORM's method-chaining API.
- **Programs are already databases.** Glyph's entire model is "data lives in SQLite." Adding an abstraction layer on top of SQLite when you're already inside SQLite is circular.
- **Token economy.** `db_exec(db, "INSERT INTO users VALUES(?, ?)")` is cheaper in tokens than `user.save()` plus all the ORM config/schema/model definitions that make `.save()` work.
- **No impedance mismatch.** ORMs bridge the gap between object graphs (inheritance, identity, lazy loading) and flat tables. Glyph records are already flat `{name: S, age: I}` — there's no mismatch to bridge.

## Current State of DB in Glyph

### Runtime API (C FFI in `cg_runtime_sqlite`)

Five functions, all operating on raw strings:

| Function | Signature | Returns |
|----------|-----------|---------|
| `db_open` | `S -> Handle` | SQLite3 handle (opaque pointer) |
| `db_close` | `Handle -> V` | void |
| `db_exec` | `Handle, S -> I` | 0=ok, -1=error |
| `db_query_rows` | `Handle, S -> [[S]]` | Array of arrays of strings |
| `db_query_one` | `Handle, S -> S` | First column of first row |

### Problems Identified

1. **No parameterized queries.** SQL is built by string concatenation:
   ```
   "SELECT ... WHERE kind = '" + sql_escape(kind) + "'"
   ```
   The `sql_escape` helper exists but is easy to forget. No bind-parameter API at the C level.

2. **Everything returns strings.** `db_query_rows` converts all column values (including integers and NULLs) to strings in C. Callers must `str_to_int` manually. NULLs become empty strings, indistinguishable from actual empty strings.

3. **Positional column access.** `row[0]`, `row[1]` — fragile, opaque, no compiler help if the query changes column order.

4. **No bridge to record types.** Glyph has `type User = {name: S, age: I}` but query results are `[[S]]`. Every caller manually destructures:
   ```
   row = rows[i]
   name = row[0]    -- hope this is still column 0
   kind = row[1]    -- hope this is still column 1
   ```

5. **The Rust side already solved this.** `glyph-db/src/model.rs` has typed models (`DefRow`, `DepRow`, `ExternRow`) with `row_to_def` mapping functions. The self-hosted Glyph side has no equivalent.

### Usage Patterns in Existing Code

- **The compiler itself** is the heaviest DB user (~50+ functions do `db_query_rows`/`db_exec`). All use string concat for SQL, positional column access.
- **glint.glyph** (project analyzer): `report_db` queries `SELECT name, kind, body FROM def`, accesses `row[1]` for kind.
- **API example**: Uses in-memory arrays, not SQLite — sidesteps the problem entirely.

## What the Type System Enables

Glyph has named record types that can be constructed and deconstructed:

```
type User = {name: S, age: I, email: S}
```

The compiler already knows the shape. A mapping layer can leverage that — no manual schema definition needed because the record type *is* the schema. Construction (`{name: "x", age: 25, email: "y"}`) and field access (`.name`, `.age`) are already native.

The design question becomes: **how much can the compiler infer from the record type to eliminate boilerplate?** Destructuring query rows into typed records, generating parameterized inserts from record fields, validating column names against type definitions at compile time — all possible without runtime reflection or configuration.

This is closer to Rust's `sqlx` (compile-time checked queries against known types) than Django/ActiveRecord (runtime schema introspection + method generation). It fits Glyph's philosophy: the type *is* the definition, the database *is* the program, no redundant layers.

## What Would Actually Help

### Layer 1: Parameterized Queries (Safety)

Extend the C runtime with bind-parameter support:

```
-- current (unsafe)
db_exec(db, "INSERT INTO users VALUES('" + sql_escape(name) + "', " + int_to_str(age) + ")")

-- proposed (safe)
db_exec_p(db, "INSERT INTO users VALUES(?, ?)", [name, int_to_str(age)])
```

This requires changes to `cg_runtime_sqlite` to use `sqlite3_prepare_v2` + `sqlite3_bind_*` instead of `sqlite3_exec`. Minimal surface area, high safety impact.

### Layer 2: Typed Row Mapping (Convenience)

A library that maps `[[S]]` results to typed records:

```
type User = {name: S, age: I, email: S}

-- maps columns by position to record fields (alphabetical order matches SELECT order)
users = db_query_as(db, "SELECT age, email, name FROM users", row_to_user)

row_to_user row = {age: str_to_int(row[0]), email: row[1], name: row[2]}
```

Or with compiler support, the mapping function could be auto-generated from the type definition when the compiler sees a `query_as` call with a known record type.

### Layer 3: Record-to-SQL Generation (Productivity)

```
type User = {name: S, age: I, email: S}

-- generates "INSERT INTO users (age, email, name) VALUES (?, ?, ?)" + binds
db_insert(db, "users", {name: "x", age: 25, email: "y"})

-- generates "UPDATE users SET age = ?, email = ?, name = ? WHERE id = ?"
db_update(db, "users", "id", 1, {name: "x", age: 26, email: "y"})
```

### What NOT to Build

- **Schema migrations.** The program *is* the database. Schema changes are definition changes.
- **Relationship mapping.** No foreign key traversal, lazy loading, or join abstractions. Just write the SQL.
- **Query DSL replacing SQL.** LLMs write SQL natively. A `.where().orderBy()` chain would be strictly more tokens for no gain.
- **Identity maps / unit of work.** No object lifecycle management. Records are values, not entities.
- **Connection pooling.** Single-process SQLite. One handle is enough.

## Implementation Options

### Option A: Pure Library (`db.glyph`)

Runtime helpers written in Glyph. Wraps existing `db_query_rows`/`db_exec` with convenience functions. Row mapping is manual (caller writes `row_to_user`). No compiler changes.

**Pro:** Ship immediately, no compiler work.
**Con:** Can't auto-generate row mappers, can't add bind params without C changes.

### Option B: Runtime Extension (C FFI)

Add `db_exec_p` / `db_query_p` to `cg_runtime_sqlite` with bind-parameter support. Library layer on top for record mapping.

**Pro:** Real parameterized queries, foundation for everything else.
**Con:** Requires C runtime changes + compiler changes to emit the new runtime.

### Option C: Compiler-Assisted (sqlx-style)

Compiler recognizes `query_as(db, sql, RecordType)` patterns. At compile time, it generates the row-mapping code from the record type's field names and types. Could even parse the SQL string at compile time to validate column count.

**Pro:** Zero-boilerplate, type-safe, LLM-optimal (minimal tokens).
**Con:** Significant compiler investment. The type checker would need to reason about SQL↔record correspondence.

## Recommendation

**Start with Option B** (runtime extension for parameterized queries), then **Option A** (pure library for convenience helpers). Option C is appealing but premature — the type checker has higher-priority work.

The key insight: Glyph doesn't need an ORM. It needs a **typed query toolkit** — parameterized queries for safety, record mapping for ergonomics, and SQL stays in the driver's seat.
