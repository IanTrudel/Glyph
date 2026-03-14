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

## Conclusion

The `module` / `module_member` tables are schema scaffolding for a grouping
and export-visibility system that was never built. They solve the *API surface
declaration* problem (which definitions are public?) but not the *name
collision* problem (what happens when two libraries define the same name?).

For Glyph's actual use case — LLMs as the sole authors and consumers,
SQL-as-import — naming conventions are likely sufficient. A formal module
system would add schema complexity and potentially require language changes
(namespace syntax) without clear benefit over the current prefix convention.

If a module system were to be added, the minimal useful increment would be:
a `glyph link <src.glyph> <dst.glyph>` command that copies exported
definitions from `src` into `dst`, erroring on name collisions and asking the
caller to resolve them. No language syntax changes required.
