### Glyph Monticello — Definition-Level Version Control

**What it does:** Definition-level version control native to Glyph's database model. Named after Squeak Smalltalk's Monticello system, which versions code at the method/class granularity rather than the file/line granularity. Glyph Monticello does the same for Glyph definitions — snapshot, diff, and share `.glyph` programs at the definition level.

**Why this is the right VCS for Glyph:** Git treats programs as bags of text files and diffs as line insertions/deletions. This is a fundamental mismatch for Glyph — a `.glyph` file is a SQLite database, and `git diff` shows `Binary files differ`. The `glyph export` workaround (exporting to text files for git tracking) fights the model instead of embracing it. Monticello showed that when your code lives in a structured store (Smalltalk image / SQLite database), version control should operate on the same structure — definitions, not lines.

**Status:** Core library complete (34 fn + 2 type + 19 tests). CLI integration pending.

---

## Architecture

### Library consumed by the compiler

Glyph Monticello is a **library** (`libraries/gmc.glyph`) consumed by the compiler (`glyph.glyph`). The compiler uses `glyph use glyph.glyph gmc.glyph` and exposes gmc's capabilities as `glyph` subcommands. User programs do **not** depend on gmc — the compiler operates on their `.glyph` databases from the outside:

```bash
glyph snapshot sheet.glyph "added formula engine"   # compiler calls gmc_snapshot on sheet's DB
glyph diff sheet.glyph                               # compiler calls gmc_diff on sheet's DB
```

This follows the Smalltalk model: Monticello lives in the development tools, not in the application. The schema tables (`snapshot`, `snapshot_def`, etc.) are created in target `.glyph` files by the compiler when needed, but the gmc code itself only lives in the compiler.

### Trunk-only model

Following Squeak Smalltalk's original Monticello, Glyph Monticello uses **trunk-only versioning** — no branches, no merge. Snapshots form a linear history on a single trunk.

**Why no branches?** Branching adds complexity that is already solved by `glyph export/import + git`. When a developer needs branches, they export to text files and use git. Glyph Monticello handles what git *can't* — definition-level snapshots, semantic diffs, and package-based sharing within the SQLite model.

### Schema lives in the target `.glyph` file

When the compiler first runs a gmc command on a `.glyph` file, it calls `gmc_migrate` to create the snapshot tables in that database. Empty tables cost nothing in SQLite — a few bytes of schema metadata. No separate init step needed.

This is the Smalltalk philosophy: the image contains its own history. A `.glyph` file carries both its definitions and its version history. The gmc *code* lives in the compiler, but the *data* lives in the target program.

### Dependencies

```
gmc.glyph → stdlib.glyph   (required — standard library)
           → sqlite3.glyph  (required — programmatic DB access)
           → diff.glyph     (required — body-level text diff)
           → json.glyph     (required — JSON serialization for .gmc format)
```

Cross-library type resolution requires `TypeName @ expr` annotations on record field accesses. json.glyph uses `JNode @` and `JToken @` to ensure correct field offsets when compiled from any consumer's context.

---

## Core Concepts

- **Snapshot** — a frozen copy of all definitions at a point in time. Stored as a row in a `snapshot` table: id, timestamp, description, parent snapshot id. Each snapshot records the content hash, body, and token count of every definition. Snapshots form a linear chain via parent pointers (trunk-only, no branching).
- **Diff** — compare working state against the latest snapshot, or compare two snapshots. Result: added definitions, removed definitions, modified definitions (same name, different hash). Uses the `DiffStatus` enum type (`Added | Removed | Modified`).
- **Package** — a named subset of definitions (by explicit membership or by namespace). Packages are the unit of sharing — you publish a package, not the whole database. Analogous to Monticello's MCZ packages. Two export formats: SQLite (ATTACH-based copy) and `.gmc` (JSON text format).

---

## Data Model

```sql
-- Snapshots (linear trunk history)
CREATE TABLE snapshot (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  description TEXT,
  timestamp INTEGER,          -- Unix epoch via strftime('%s','now')
  parent1 INTEGER             -- previous snapshot id
);

-- Snapshot contents (which definitions at which version)
CREATE TABLE snapshot_def (
  snapshot_id INTEGER,
  name TEXT,
  kind TEXT,
  hash BLOB,                  -- BLAKE3 hash of the body at snapshot time
  body TEXT,                   -- frozen body text
  tokens INTEGER,             -- token count
  PRIMARY KEY (snapshot_id, name, kind)
);

-- Packages (named definition subsets)
CREATE TABLE package (
  name TEXT PRIMARY KEY,
  description TEXT
);

CREATE TABLE package_member (
  package_name TEXT,
  def_name TEXT,
  def_kind TEXT,
  PRIMARY KEY (package_name, def_name, def_kind)
);
```

---

## API

### Types

```
DiffStatus = Added | Removed | Modified

GmcDiff = {kind: S, name: S, new_body: S, old_body: S, status: I}
```

### Snapshots

```
gmc_migrate db              → !I        (create tables if not exist)
gmc_latest db               → !I        (id of most recent snapshot, 0 if none)
gmc_snapshot db desc         → !I        (create snapshot, return id)
gmc_snap_defs db snap_id    → ![[S]]    (definitions in a snapshot)
gmc_snap_info db snap_id    → ![S]      (id, description, timestamp, parent)
gmc_show db snap_id         → !S        (formatted snapshot display)
gmc_restore db snap_id      → !I        (restore working state from snapshot)
gmc_log db                  → ![[S]]    (all snapshots, newest first)
gmc_log_n db n              → ![[S]]    (last n snapshots)
gmc_prune db keep           → !I        (delete all but last `keep` snapshots)
```

### Diff

```
gmc_diff db                 → ![GmcDiff] (working state vs latest snapshot)
gmc_diff_snaps db s1 s2     → ![GmcDiff] (between two snapshots)
gmc_to_diffs rows           → [GmcDiff]  (convert raw rows to diff records)
```

### Packages — CRUD

```
gmc_pkg_create db name desc → !I
gmc_pkg_del db name         → !I
gmc_pkg_list db             → ![[S]]
gmc_pkg_add db pkg name kind → !I
gmc_pkg_add_ns db pkg ns    → !I        (add all defs in namespace)
gmc_pkg_remove db pkg name kind → !I
gmc_pkg_defs db pkg         → ![[S]]
```

### Packages — SQLite Export/Import

```
gmc_pkg_export db pkg path  → !I        (ATTACH + copy defs to new .glyph file)
gmc_pkg_import db path      → !I        (ATTACH + copy defs from .glyph file)
```

### Packages — JSON Export/Import (.gmc format)

```
gmc_pkg_to_json db pkg      → !S        (serialize package to JSON string)
gmc_pkg_from_json db json   → !I        (parse JSON and INSERT defs)
gmc_pkg_save db pkg path    → !I        (serialize + write to .gmc file)
gmc_pkg_load db path        → !I        (read .gmc file + parse + INSERT)
```

### JSON Helpers (internal)

```
gmc_json_esc s              → S         (escape string for JSON output)
gmc_je_loop s i sb          → S         (escape loop)
gmc_defs_to_json sb defs i  → I         (serialize defs array)
gmc_json_imp_loop db pool arr i n → !I  (import loop using json.glyph)
```

Serialization uses a manual string builder for efficiency. Deserialization uses json.glyph's `json_decode`/`json_get_str`/`json_arr_get` with `JNode @` annotations for cross-library type safety.

---

## .gmc Package Format

A `.gmc` file is a JSON text file containing a package's metadata and definitions:

```json
{
  "package": "mylib",
  "description": "A reusable library",
  "defs": [
    {"name": "foo", "kind": "fn", "body": "foo x = x + 1"},
    {"name": "bar", "kind": "fn", "body": "bar = foo(42)"}
  ]
}
```

Serialization uses a manual string builder with proper escaping of `"`, `\`, `\n`, `\r`, `\t`. Deserialization uses json.glyph (`json_decode` + `json_get_str` + `json_arr_get`).

Imported definitions get `zeroblob(32)` as their hash, forcing recompilation on first build.

---

## CLI Commands (Planned)

Integrated as `glyph` subcommands, not a separate binary:

```bash
glyph snapshot app.glyph "description"          # create a snapshot of current state
glyph log app.glyph                             # show snapshot history
glyph diff app.glyph                            # diff working state vs last snapshot
glyph diff app.glyph snap1 snap2                # diff two snapshots
glyph restore app.glyph 5                       # restore from snapshot
glyph package app.glyph create pkg-name "desc"  # create a package
glyph package app.glyph add pkg-name fn_name fn # add def to package
glyph package app.glyph export pkg-name out.gmc # export as .gmc file
glyph package app.glyph import pkg.gmc          # import from .gmc file
```

---

## Examples

**Diff example:**

```
Working state vs snapshot #12 (2 modified, 1 added, 1 removed)

  modified: fn parse_atom
  modified: fn tok_one

  added: fn parse_char_lit

  removed: fn old_helper
```

**Typical workflow (CLI):**

```bash
# work on sheet.glyph...
glyph snapshot sheet.glyph "added formula engine"
glyph snapshot sheet.glyph "fixed cell reference parsing"
glyph log sheet.glyph
glyph diff sheet.glyph              # working state vs last snapshot
glyph restore sheet.glyph 3         # revert to snapshot #3

# package sharing
glyph package sheet.glyph create formulas "formula engine"
glyph package sheet.glyph add formulas eval_formula fn
glyph package sheet.glyph export formulas /tmp/formulas.gmc
glyph package sheet.glyph import /tmp/formulas.gmc  # into another .glyph file
```

---

## Why This Plays to Glyph's Strengths

1. **The `hash` column already exists** — every definition has a BLAKE3 content hash. Snapshot creation is a single `INSERT INTO snapshot_def SELECT ... FROM def` — the database already knows the state.
2. **The `dep` table already exists** — future diff analysis can check whether a changed definition breaks its callers. No other VCS can do this because no other VCS knows the call graph.
3. **The `def_history` table already exists** — automatic change tracking via triggers. Glyph Monticello extends this from per-definition history to whole-program snapshots.
4. **SQLite is the storage layer** — no need for a custom object store, packfile format, or index. Everything is SQL queries on tables in the same database.
5. **Packages map to namespaces** — the `ns` column on `def` naturally partitions definitions into shareable units.

## What Monticello Got Right That Git Doesn't (For This Model)

- **Semantic units** — Monticello versions methods and classes, not files. Glyph Monticello versions definitions, not lines. The diff is always meaningful.
- **Image-centric** — the version history lives inside the image (database), not in a parallel directory. The program carries its own history.
- **Trunk-only** — Squeak Monticello uses trunk. Branching is the wrong abstraction when your code lives in a structured store. For branch-level workflows, export to text and use git.
- **Package-centric sharing** — you share a curated subset (a package), not the whole repository. Libraries are packages extracted from one database and imported into another — which is exactly what `glyph link` already does, minus the version tracking.

## What Glyph Monticello Adds Over Monticello

- **SQL-powered queries** — "show me all definitions that changed between snapshots 5 and 12 in the `parser` namespace" is a single SQL query joining `snapshot_def` with itself.
- **Two export formats** — SQLite ATTACH (fast, preserves types) and `.gmc` JSON (portable, human-readable, diffable with git).
- **Content-addressed deduplication** — definitions with the same hash across snapshots share logical identity. A thousand snapshots where 90% of definitions didn't change stores only the 10% that did (via `snapshot_def` recording hash-identical bodies).

---

## Implementation Status

### Complete

**34 fn + 2 type + 19 tests** in `libraries/gmc.glyph`:

- Schema migration (`gmc_migrate`)
- Trunk-only snapshots (create, restore, log, prune, show, info)
- Definition-level diff (working vs HEAD, snapshot vs snapshot)
- Package CRUD (create, delete, list, add, add by namespace, remove)
- Package SQLite export/import (ATTACH-based)
- Package JSON export/import (`.gmc` format via json.glyph with `JNode @` annotations)
- 19/19 tests passing

**Dependencies:** stdlib.glyph, sqlite3.glyph (33 fn + 18 tests), diff.glyph (DiffKind enum + LCS algorithm), json.glyph (JSON parsing/generation with JNode + JToken types)

### Remaining

- **CLI integration** — wire up `glyph snapshot/log/diff/restore/package` subcommands in `glyph.glyph`
- **Body-level diff display** — use diff.glyph to show line-level changes within modified definitions (currently reports *which* defs changed, not *what* changed)
- **Dependency-aware diff** — use `dep` table to warn when a changed definition breaks callers

---

## Naming Convention

`gmc_` prefix for all public functions. Internal helpers use `gmc_` prefix with descriptive suffixes. Loop helpers use `_loop` suffix. JSON serialization helpers use `gmc_json_`/`gmc_je_` prefix.

---

## Connection to next-steps.md

This subsumes or enables several proposed features:

- #27 (definition-level diffing) — `glyph diff` is this, with full snapshot history
- #11 (package manager) — packages are Glyph Monticello's sharing unit
