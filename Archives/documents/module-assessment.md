# Module System Assessment

## What exists in the schema

Two tables are defined in `init_schema` but never populated:

```sql
CREATE TABLE module (
  id   INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  doc  TEXT
);

CREATE TABLE module_member (
  module_id INTEGER NOT NULL REFERENCES module(id) ON DELETE CASCADE,
  def_id    INTEGER NOT NULL REFERENCES def(id)    ON DELETE CASCADE,
  exported  INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY (module_id, def_id)
);
```

`SELECT COUNT(*) FROM module` → **0** in glyph.glyph itself.

The only compiler code that touches these tables is `export_module_row` /
`export_module_loop`, called by `export_schema` (the `glyph dump` SQL export)
purely to round-trip existing rows back to SQL. No build, type-check, or
codegen path queries them.

The `tag` table (`def_id, key, val`) is in the same situation — defined,
empty, unused by the compiler.

## What the design intended

`module` provides logical grouping and a docstring. `module_member.exported`
is a visibility flag: `1` = public API, `0` = internal helper. The intent was
to control what crosses database boundaries when linking libraries together.

## What is NOT implemented

- No `glyph module` CLI command
- No compiler enforcement of `exported=0` visibility
- No cross-database linking / `glyph import` tool
- No namespace syntax in the language (`Module.foo` does not exist)

## Code change implications

**Within a single database**: none. Definitions reference each other by bare
name (`my_helper(x)`), with no namespace prefix in call expressions. Moving
definitions into or out of a module group has zero effect on call sites — the
`def` table is flat and the compiler resolves names globally.

**Across databases**: the `exported` flag would be enforced only at the
tooling layer (a hypothetical merge/link command that skips non-exported
definitions). Call sites in the consuming database still use bare names — no
`Module.foo(x)` syntax is needed or possible.

## The name collision problem

This is the fundamental gap the current design does not address.

`def` has a `UNIQUE INDEX ON (name, kind, gen)`. If two databases both define
`foo` and you attempt to merge them, the database rejects the second insert.
The `exported` flag controls *what crosses the boundary* but provides no
*disambiguation* when collisions occur on the other side.

Options and their costs:

| Approach | Code change at call sites? | Notes |
|---|---|---|
| Rename on merge (`lib_foo`) | Yes — every call site | Breaks the "no change" promise |
| Qualified names in `def.name` (`Mod.foo`) | Yes — requires new call syntax | Needs language change |
| Copy-only, manual rename | Manual per conflict | What programs do today |
| Last-write-wins policy | No | Silent correctness risk |

## What Glyph programs actually do

Manual namespace prefixes by convention: `cg_`, `blt_`, `mcp_`, `tc_`,
`psf_`, `cmd_`, etc. This is the C approach. It works well for Glyph's
use case because:

1. An LLM reading definitions treats the prefix as a semantic signal.
2. SQL queries give "module" grouping for free: `SELECT * FROM def WHERE name LIKE 'cg_%'`.
3. No runtime or compile-time overhead.

## Structural enforcement at scale

The naming convention already handles discovery reasonably well today. But as a
database grows past ~1,000–2,000 definitions, two gaps emerge:

1. **Context window efficiency**: an LLM needs to include exactly the right
   subset of definitions in its prompt — "give me the public surface of the
   codegen subsystem" should be a single query, not pattern knowledge.
2. **Visibility**: there is no machine-readable distinction between an entry
   point (`cmd_build`) and an internal helper (`cg_block2`). Both are `kind='fn'`,
   both are equally visible to queries.

The `module` / `module_member` design addresses (2) via `exported`, but at the
cost of a join table and the unsolved name-collision problem. For an LLM-native
system, a relational design fits better than a syntactic one.

### Option A — `ns` column on `def` (recommended)

Add `ns TEXT` to the `def` table, populated from the name prefix (`cg`, `mcp`,
`tc`, etc.). This can be set automatically on insert (split on first `_`) or
explicitly overridden.

```sql
-- Public surface of the codegen subsystem
SELECT name FROM def WHERE ns = 'cg' AND visibility = 'public'

-- List all subsystems
SELECT DISTINCT ns FROM def ORDER BY ns

-- Everything an LLM needs to bootstrap a task in a given subsystem
SELECT name, body FROM def WHERE ns = 'cg'
```

Combined with a `visibility TEXT NOT NULL DEFAULT 'public'` column (marking
internal helpers as `'private'`), this gives a full module-equivalent picture:

| Module concept | Relational equivalent |
|---|---|
| Module name | `def.ns` |
| Public API | `WHERE visibility = 'public'` |
| Internal helper | `WHERE visibility = 'private'` |
| "Import module X" | `SELECT ... WHERE ns = 'X'` |
| Cross-module dep check | dep table + cross-ns filter |

**Why this fits LLMs better than the `module` table:**

- No join required — every definition carries its own namespace.
- SQL-as-import already works; this makes the grouping *queryable without
  knowing the naming convention*.
- `glyph dump --budget` can use `ns` + `visibility` to prioritise which
  definitions to include in a token-budgeted export.
- A linter pass (`glyph check`) can warn on calls from a `public` function in
  one namespace into a `private` function in another.
- No language syntax changes needed — call sites stay as bare names.

### Option B — Sub-databases (federation)

A `glyph link <src.glyph> <dst.glyph>` command copies exported definitions
from `src` into `dst`, erroring on name collisions. Each logical subsystem
lives in its own `.glyph` file. Strongest isolation, highest friction. Worth
revisiting if subsystems need independent authorship or versioning, but
premature for a single growing database.

### Option C — SQL views as packages

Define named views (`CREATE VIEW v_codegen AS SELECT * FROM def WHERE name
LIKE 'cg_%'`). No schema changes; LLMs query views instead of `def`. Downside:
views don't carry visibility metadata and are invisible to `glyph dump`.

## Conclusion

The `module` / `module_member` tables should be **removed**. They add schema
confusion without providing value, and the `export_module_row` /
`export_module_loop` compiler code that round-trips them can go with them.

The current naming-convention approach is correct for Glyph's LLM-native use
case, but it will need formalisation as the database grows. The recommended
path is **Option A**: add `ns` and `visibility` columns to `def`. This promotes
the *idea* of modules — grouping, public API, internal helpers — while keeping
the implementation relational and SQL-queryable, which is exactly what LLMs
work with natively.

If cross-database composition becomes a need, the `glyph link` command
(Option B) can be layered on top without requiring any language changes.
