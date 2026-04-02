### sqlite3.glyph — Centralized SQLite Library

**Location:** `libraries/sqlite3.glyph`
**Dependencies:** `stdlib.glyph`
**Status:** Phases 1–3 complete (33 fn + 18 tests). Phase 4 (named results) deferred. Phase 5 (compiler migration) future.

---

## Purpose

Centralize all SQLite functionality into a single library. The Glyph compiler (`glyph.glyph`) and all programs that use SQLite (`gmc.glyph`, `glint.glyph`, etc.) should depend on this library rather than using raw runtime functions directly.

One place to maintain, test, and ensure stability.

---

## Current State

### Raw Runtime Functions (C FFI)

Five primitives in the C runtime, always available:

| Function | Signature | Returns |
|----------|-----------|---------|
| `db_open` | `S -> I` | Database handle (0 on failure) |
| `db_close` | `I -> V` | Void |
| `db_exec` | `I -> S -> I` | 0 on success, -1 on error (prints to stderr) |
| `db_query_rows` | `I -> S -> [[S]]` | Array of row arrays (all values as strings) |
| `db_query_one` | `I -> S -> S` | First column of first row (empty string on error) |

### Pain Points

1. **No error propagation** — `db_open` returns 0 on failure, callers don't check
2. **String-only results** — all column values are strings, manual parsing needed for integers/floats
3. **No parameterized queries** — SQL strings built via concatenation, manual escaping with `sql_escape()`
4. **No transactions** — no BEGIN/COMMIT/ROLLBACK patterns in Glyph code
5. **No prepared statements** — SQL recompiled every call
6. **Full buffering** — `db_query_rows` loads entire result set into memory
7. **No PRAGMAs** — `db_open` calls raw `sqlite3_open` with no configuration (no WAL, no foreign keys, no busy timeout)
8. **No thread safety** — no `SQLITE_THREADSAFE` configuration, no busy timeout for lock contention
9. **No error messages** — `db_exec` prints errors to stderr but the error string is not returned to Glyph code
10. **ATTACH via raw SQL only** — `attach_lib` builds ATTACH SQL strings manually through `db_exec`, no dedicated API

### Compiler Usage Patterns

The compiler uses SQLite extensively:

- **Definition CRUD** — read/write/delete defs in the `def` table
- **Dependency queries** — traverse `dep` table for dirty tracking, call graphs
- **Schema migrations** — `migrate_*` functions run DDL via `db_exec`
- **Multi-database** — `ATTACH DATABASE` for library linking (`lib0.def`, `lib1.def`)
- **Metadata** — `meta` table for schema version, `extern_` for FFI declarations
- **History** — `def_history` queries for undo/history commands

Common pattern across all commands:
```
db = db_open(path)
rows = db_query_rows(db, sql)
... process rows in loop ...
db_close(db)
```

---

## Decisions

### Error Handling — Result Types

Use Glyph's `!T` (Result) types and `?` (error propagation) throughout. No silent failures.

```
sql_open path         → !I             (Result with db handle, or error)
sql_exec db sql       → !I             (Result with 0, or error message)
sql_rows db sql       → ![[S]]         (Result with rows, or error)
```

Callers use `?` to propagate or `!` to unwrap-or-panic:
```
db = sql_open("app.glyph")?
rows = sql_rows(db, "SELECT name FROM def")?
sql_close(db)
```

### PRAGMAs — Automatic on Open

`sql_open` sets sensible defaults automatically:
```sql
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;
PRAGMA busy_timeout = 5000;
```

No separate configuration step. These are the right defaults for Glyph's use case (concurrent access, referential integrity, contention tolerance).

### Result Format — Both String Arrays and Named Maps

Two layers, both available:

**String arrays** (base layer, fast, no overhead):
```
sql_rows db sql       → ![[S]]         (array of row arrays)
sql_one db sql        → !S             (single value)
sql_row db sql        → ![S]           (single row)
sql_col db sql        → ![S]           (single column)
sql_int db sql        → !I             (single integer)
sql_exists db sql     → !B             (any rows?)
```

**Named maps** (convenience layer, column names as keys):
```
sql_named db sql      → ![{S:S}]       (array of column→value maps)
```

Requires a new C runtime function (`db_query_named`) that uses `sqlite3_column_name()` to return column metadata alongside values. The string-array layer works with the existing runtime; the named layer needs a runtime addition.

### Thread Safety

SQLite is compiled in serialized mode by default (safe for multi-threaded use). The library should:

- Set `PRAGMA busy_timeout` on open (handles lock contention from concurrent writers)
- WAL mode enables concurrent readers with a single writer
- Document that each thread should open its own connection (connection-per-thread pattern)
- No connection pooling initially — Glyph's `thread.glyph` library can manage connections per-thread

### ATTACH Support — Required

Multi-database operations are essential for library linking and `gmc`. The library provides:

```
sql_attach db path alias   → !I
sql_detach db alias        → !I
sql_prefixed alias table   → S         ("lib0.def")
```

### Prepared Statements — Deferred

String-based SQL with escaping is sufficient for now. Prepared statements would require new C runtime functions (`db_prepare`, `db_bind_*`, `db_step`, `db_finalize`) and a statement handle type. Worth revisiting if performance profiling shows SQL compilation as a bottleneck.

---

## C Runtime Prerequisites

The current `cg_runtime_sqlite` needs additions before the library can provide proper error handling, thread safety, and ATTACH support.

### New Runtime Functions Needed (Phase 4)

| Function | C Signature | Purpose |
|----------|-------------|---------|
| `db_error` | `I -> S` | `sqlite3_errmsg(db)` — return last error message as Glyph string |
| `db_query_named` | `I -> S -> [[S]]` | Like `db_query_rows` but first row is column names from `sqlite3_column_name()` |

Neither has been added yet. Error handling is done at the Glyph level — `sql_exec` includes the SQL string in the error message. Named results are deferred until a consumer needs them.

### PRAGMA Configuration — Implemented in Glyph

PRAGMAs are set in `sql_open` at the Glyph level rather than modifying `db_open` in the C runtime:

```
sql_open path =
  db = glyph_db_open(path)
  match db == 0
    true -> err("failed to open: " + path)
    _ ->
      glyph_db_exec(db, "PRAGMA journal_mode=WAL")
      glyph_db_exec(db, "PRAGMA foreign_keys=ON")
      glyph_db_exec(db, "PRAGMA busy_timeout=5000")
      ok(db)
```

This approach keeps the C runtime unchanged and lets the library control configuration.

### Thread Safety

SQLite's default compile is `SQLITE_THREADSAFE=1` (serialized mode) on most systems. The library provides:

- `busy_timeout=5000` set on every connection via `sql_open`
- WAL mode enables concurrent readers + single writer
- Connection-per-thread pattern (each thread opens its own handle)

---

## API Design

### Core — Open/Close

```
sql_open path              → !I        (open + set PRAGMAs, Result with handle)
sql_close db               → V
```

### Execute — No Result Set

```
sql_exec db sql            → !I        (0 = success)
sql_exec_many db stmts     → !I        (execute array of statements in transaction)
```

### Query — String Arrays

```
sql_rows db sql            → ![[S]]    (all rows)
sql_one db sql             → !S        (single value)
sql_row db sql             → ![S]     (first row)
sql_col db sql             → ![S]     (first column of all rows)
sql_int db sql             → !I        (single integer value)
sql_exists db sql          → !B        (any rows returned?)
```

### Query — Named Results

```
sql_named db sql           → ![{S:S}]  (rows as column→value maps)
```

Requires runtime addition: `db_query_named`.

### Value Conversion

```
sql_to_int val             → I         (string → integer)
sql_to_float val           → F         (string → float)
sql_is_null val            → B         (empty string = NULL)
```

### Escaping and Query Building

```
sql_escape s               → S         (escape single quotes)
sql_quote s                → S         (escape + wrap in quotes)
sql_in values              → S         ("'a','b','c'" for IN clauses)
```

### Transactions

```
sql_begin db               → !I
sql_commit db              → !I
sql_rollback db            → !I
sql_transaction db f       → !I        (begin, call f(db), commit or rollback)
```

### Multi-Database

```
sql_attach db path alias   → !I
sql_detach db alias        → !I
sql_prefixed alias table   → S
```

### Schema Introspection

```
sql_tables db              → ![S]      (table names)
sql_columns db table       → ![S]      (column names)
sql_has_table db name      → !B
sql_has_column db tbl col  → !B
sql_version db             → !S        (meta.schema_version)
```

---

## Implementation Plan

### Phase 1 — Core Wrapping ✓

Wrap the 5 runtime functions with Result-based error handling and convenience helpers.

Definitions (19 fn + 12 tests):
- `sql_open`, `sql_close` with PRAGMA setup (WAL, foreign_keys, busy_timeout)
- All query variants (`sql_rows`, `sql_one`, `sql_row`, `sql_col`, `sql_int`, `sql_exists`)
- Value conversion (`sql_to_int`, `sql_to_float`, `sql_is_null`)
- Escaping (`sql_escape`, `sql_escape_loop`, `sql_quote`, `sql_in`, `sql_in_loop`)
- Loop helpers (`sql_col_loop`)

### Phase 2 — Transactions and Multi-DB ✓

Definitions (8 fn + 3 tests):
- `sql_begin`, `sql_commit`, `sql_rollback`, `sql_transaction`
- `sql_attach`, `sql_detach`, `sql_prefixed`
- `sql_exec_many`, `sql_exec_many_loop`

### Phase 3 — Schema Introspection ✓

Definitions (6 fn + 3 tests):
- `sql_tables`, `sql_columns`, `sql_has_table`, `sql_has_column`, `sql_version`

### Phase 4 — Named Results (Runtime Addition) — Deferred

- New C runtime function `db_query_named` using `sqlite3_column_name()`
- `sql_named` wrapper in library
- Tests

Not needed yet — string array results are sufficient for all current consumers.

### Phase 5 — Compiler Migration — Future

Gradually replace raw `db_*` calls in `glyph.glyph` with `sql_*` library functions. Dozens of compiler functions touched, done incrementally.

---

## Naming Convention

`sql_` prefix for all public functions. Internal helpers use `sql_` prefix with descriptive suffixes. Loop helpers use `_loop` suffix.

Not `db_` (conflicts with runtime function names) or `sqlite_` (too long, wastes tokens).

---

## Resolved Questions

- **`sql_exec_many`**: Wraps statements in a transaction (BEGIN, exec each, COMMIT; ROLLBACK on error).
- **Error information**: Result errors carry the SQL that failed (e.g., `err("exec failed: " + s)`). No `db_error` runtime function yet — the SQLite error message from `sqlite3_errmsg` is not currently propagated.
- **`sql_named`**: Deferred. String arrays sufficient for all current use cases.
- **PRAGMA location**: Set in Glyph-level `sql_open`, not in C runtime `db_open`. Keeps runtime unchanged.

## Open Questions

- Should `db_error` be added to the C runtime? Would allow `sql_exec` to include the actual SQLite error message.
- When should compiler migration (Phase 5) begin? After sqlite3.glyph has been stable for a while.
