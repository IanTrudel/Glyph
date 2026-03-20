# Library Linking

## Current Behaviour

`glyph link <lib.glyph> <app.glyph>` is a **database-level copy**: it INSERTs rows from `lib.glyph`'s `def` table directly into `app.glyph`'s `def` table (plus externs). There is no C or LLVM IR merging.

Once linked, imported definitions are indistinguishable from definitions written directly in the app. They persist permanently, show up in `glyph ls`, accumulate in `def_history`, and are subject to `glyph put`/`glyph rm` like any other def. There is no record of which defs came from which library, and no way to unlink them as a batch.

At build time, `glyph build` compiles everything — app defs and formerly-library defs alike — into a single C file and a single executable. No separate compilation, no ABI boundary.

## Problems

- **No provenance** — after link, no record of which defs came from a library or which version
- **No unlinking** — removing a library requires manually `rm`-ing each def
- **No upgrade path** — updating a library version requires manual collision resolution
- **Namespace pollution** — library defs permanently join the app's def table

---

## Improvement Options

### Level 1: Provenance tags (low effort, high value)

The `tag` table already exists (`tag(def_id, key, val)`). On link, stamp each imported def:

```sql
INSERT INTO tag (def_id, key, val) VALUES (?, 'lib', 'mylib@v1.2')
```

New commands this enables:

- `glyph unlink lib.glyph app.glyph` — deletes all defs tagged with that lib
- `glyph ls --lib mylib` — filter by provenance
- `glyph upgrade-lib new-lib.glyph app.glyph` — unlink old version, re-link new, report conflicts

One new column written in `link_insert_loop`, two new commands. Backwards-compatible with existing linked databases (they just have no tags).

### Level 2: `lib_dep` table — libraries stay in their own DB (medium effort, architecturally clean)

Instead of copying defs, record the dependency and read library defs at build time via SQLite ATTACH:

```sql
CREATE TABLE lib_dep (
  lib_path TEXT NOT NULL,
  ns       TEXT,
  prefix   TEXT,
  hash     BLOB
)
```

`glyph use lib.glyph app.glyph` adds a row. `glyph build` ATTACHes each declared library and unions their defs into the compilation query:

```sql
ATTACH DATABASE 'lib.glyph' AS lib;
SELECT * FROM lib.def UNION ALL SELECT * FROM def WHERE ...
```

Library defs never land in `app.glyph` — they are read-only at compile time. `glyph unuse lib.glyph app.glyph` just deletes the `lib_dep` row. Upgrading a library is updating the file on disk.

This matches how the language already works: no imports, SQL is the module system — libraries are just other databases queried at build time.

### Level 3: Separate compilation (high effort)

Compile `lib.glyph` to a `.glya` archive (compiled C or LLVM bitcode), link at the C/linker level. True separate compilation with an ABI boundary. Enables distributing precompiled libraries without shipping source. Only relevant once there is a use case for binary-only library distribution.

---

## Recommendation

**Level 1** is worth doing immediately — a one-session change that fixes the unlinking problem and adds provenance with no architectural disruption.

**Level 2** is the right long-term design. SQLite ATTACH is a natural fit: the "program = database" model extends cleanly to "library = another database, queried at build time." It eliminates the copy problem entirely rather than papering over it.

**Level 3** only makes sense if library authors need to distribute binaries without source, which is not a current use case.
