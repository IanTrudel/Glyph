# Glyph Migration System

## Motivation

Glyph programs are SQLite databases. Schema evolves as the language grows (already seen: schema_version 1→4, ad-hoc `migrate_gen` and `migrate_history` functions). Currently each migration is a hand-written check-then-alter function — this doesn't scale.

Two distinct migration needs exist:

1. **Compiler schema migrations** — the `.glyph` format itself evolves (new tables, columns, indexes). Every `.glyph` file needs upgrading when the compiler changes.
2. **Application data migrations** — user programs that use SQLite for their own data need a migration mechanism too.

This design addresses both with the same machinery.

## Design

### New Table: `migration`

```sql
CREATE TABLE IF NOT EXISTS migration (
  version   INTEGER PRIMARY KEY,   -- sequential, no gaps
  name      TEXT NOT NULL,          -- human-readable label
  sql_up    TEXT NOT NULL,          -- SQL to apply
  applied   INTEGER NOT NULL DEFAULT 0,  -- 0=pending, 1=applied
  applied_at TEXT                   -- datetime when applied
);
```

Migrations are **data in the database itself**. A `.glyph` file carries its own migration history. This is the Glyph-native approach — no external files, no filesystem dependency.

### How It Works

**Schema version** stays in `meta` as a single integer — the highest applied migration version. This is the fast-path check: open database, read version, compare against compiler's expected version.

```
meta.schema_version = max(migration.version WHERE applied = 1)
```

**Compiler-shipped migrations** are embedded in the compiler as a list of `(version, name, sql)` tuples. On `glyph init`, all migrations are inserted as already-applied (the schema starts fresh at the latest version). On `glyph build`/`glyph run`/any command that opens a database, the compiler checks the current version and applies any missing migrations.

**User-defined migrations** are inserted by the user (or LLM) via `glyph migrate add`. They occupy a separate version namespace (application versions start at 10000) or, simpler, they share the same sequence but the compiler only ships migrations up to its known max — anything beyond is user-defined.

### Migration Rules

1. **Forward-only**. No down migrations. Rolling back means restoring from `def_history` or a backup. Down migrations are a complexity trap — they're rarely tested and often wrong.
2. **Idempotent SQL**. Use `IF NOT EXISTS`, `IF EXISTS`, column-presence checks. A migration that's safe to re-run is a migration that won't break on edge cases.
3. **Atomic**. Each migration runs in a transaction. If it fails, nothing is applied.
4. **Sequential**. Migrations apply in version order. No cherry-picking.

### CLI Commands

#### `glyph migrate <db>`

Apply all pending migrations. Output:

```
migration 5: add_tag_table ... ok
migration 6: add_module_system ... ok
2 migrations applied (schema version: 6)
```

If already up to date:

```
schema version 6: up to date
```

#### `glyph migrate add <db> -n "description" -b "SQL"`

Insert a new pending migration. Version is auto-assigned as `max(version) + 1`.

```
glyph migrate add app.glyph -n "add_users_table" -b "CREATE TABLE users (...)"
glyph migrate add app.glyph -n "add_email_index" -f /tmp/migration.sql
```

#### `glyph migrate status <db>`

Show migration state:

```
version  name                 status      applied_at
1        initial_schema       applied     2026-01-15 10:00:00
2        add_gen_column       applied     2026-01-20 14:30:00
3        add_def_history      applied     2026-02-01 09:00:00
4        add_migration_table  applied     2026-02-26 12:00:00
5        add_users_table      pending     -
```

#### `glyph migrate squash <db>`

Collapse all applied migrations into a single "initial" migration. Useful when the migration list grows long. Rewrites the `migration` table but doesn't touch the actual schema.

### Compiler Integration

On database open (every command), the compiler runs:

```
fn ensure_migrated(db):
  current = read meta.schema_version (default 0 if missing)
  for each compiler_migration where version > current:
    BEGIN
    exec migration.sql_up
    INSERT INTO migration (version, name, sql_up, applied, applied_at)
      VALUES (?, ?, ?, 1, datetime('now'))
    UPDATE meta SET val = ? WHERE key = 'schema_version'
    COMMIT
```

This replaces the current ad-hoc `migrate_gen()` / `migrate_history()` pattern. Those existing migrations become versions 1-3 in the compiler's migration list.

### Compiler Migration Registry

Embedded in the compiler (both Rust and self-hosted):

```
MIGRATIONS = [
  (1, "initial_schema",    "<full CREATE TABLE/INDEX/VIEW DDL>"),
  (2, "add_gen_column",    "ALTER TABLE def ADD COLUMN gen ..."),
  (3, "add_def_history",   "CREATE TABLE def_history ...; CREATE TRIGGER ..."),
  (4, "add_migration_table", "CREATE TABLE migration ..."),
]
```

Version 1 is special — on `glyph init`, it creates everything from scratch. On existing databases, version 1 is marked as applied (the schema already exists), and only newer migrations run.

### Bootstrapping Existing Databases

Existing `.glyph` files have `meta.schema_version = '4'` but no `migration` table. The upgrade path:

1. Compiler detects missing `migration` table
2. Creates it
3. Inserts migrations 1-4 as already applied (since the schema is already at version 4)
4. Applies any migrations > 4

This is itself the last ad-hoc migration. After this, everything goes through the migration system.

### Application Migrations

User programs that maintain their own databases can use the same pattern. The `migration` table is part of the standard schema. User code:

```
-- In a Glyph program that manages its own database:
add_migration db 5 "add_users" "CREATE TABLE IF NOT EXISTS users (...)"
apply_migrations db
```

Or from the CLI:

```bash
glyph migrate add myapp.glyph -n "add_users" -b "CREATE TABLE users (...)"
glyph migrate myapp.glyph
```

## Implementation Scope

### New Definitions (~15-20)

| Definition | Purpose |
|-----------|---------|
| `migration_table_sql` | DDL for the migration table |
| `ensure_migration_table` | Bootstrap: create table if missing, backfill existing versions |
| `read_schema_version` | Read current version from meta |
| `read_pending_migrations` | Query unapplied migrations |
| `apply_migration` | Execute one migration in a transaction |
| `apply_all_migrations` | Loop over pending, apply sequentially |
| `compiler_migrations` | Return list of compiler-shipped migrations |
| `register_compiler_migrations` | Insert compiler migrations that aren't in the table yet |
| `cmd_migrate` | CLI dispatch for `glyph migrate` |
| `cmd_migrate_add` | CLI: insert new migration |
| `cmd_migrate_status` | CLI: show migration state |
| `cmd_migrate_squash` | CLI: collapse applied migrations |
| `format_migration_row` | Pretty-print a migration status line |

### Modified Definitions

| Definition | Change |
|-----------|--------|
| `init_schema` | Add `migration` table to DDL, insert initial migrations as applied |
| `dispatch_cmd` chain | Add `migrate` command routing |
| `print_usage` | Add migrate to help text |

### Rust Compiler Changes

| File | Change |
|------|--------|
| `schema.rs` | Add `MIGRATION_TABLE_SQL`, `COMPILER_MIGRATIONS` array |
| `connection.rs` | Replace `migrate_gen`/`migrate_history` with `ensure_migrated()` |

## What This Replaces

- `migrate_gen()` — becomes migration version 2
- `migrate_history()` — becomes migration version 3
- Ad-hoc `schema_version` bumps — automated by migration application
- The pattern of "check if column/table exists, then ALTER" — still used inside individual migrations but no longer scattered across the codebase

## What This Doesn't Do

- **Down migrations**: Intentionally omitted. Use `def_history` + `glyph undo` for rollback.
- **Cross-database migrations**: No support for migrating data between `.glyph` files. That's a different tool.
- **Automatic migration generation**: No schema diffing. Migrations are hand-written SQL. LLMs are good at writing SQL — this is fine.
