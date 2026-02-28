# Glyph Database Schema: Unused & Incomplete Assessment

**Date:** 2026-02-25
**Scope:** All tables, columns, views, indexes, and def kinds in the `.glyph` schema

## Summary

The schema was designed with ambitious plans (modules, traits, incremental compilation, IR caching) but only the core Fn/Type/Test compilation pipeline was fully implemented. Roughly 40% of the schema surface area is dead or unpopulated.

---

## Dead Tables (never populated, no functional queries)

### `module` / `module_member`
- **Columns:** `module(id, name, doc)`, `module_member(module_id, def_id, exported)`
- **Intended:** Logical grouping of definitions with export flags
- **Status:** 0 rows. No query functions exist in Rust code. No self-hosted code references them. Model structs exist in `glyph-db/src/model.rs:145-151` but are never constructed.
- **Verdict:** Fully dead. Can be removed or kept as a placeholder for a future module system.

### `tag`
- **Columns:** `def_id, key, val`
- **Intended:** Arbitrary key-value metadata on definitions (e.g., `unsafe`, `doc`, `deprecated`)
- **Status:** 0 rows. Query functions exist (`set_tag`, `get_tags` in `queries.rs:256-275`) and are covered by unit tests, but nothing in the build pipeline or CLI ever calls them.
- **Verdict:** Dead code with working infrastructure. Revival cost is low.

### `compiled`
- **Columns:** `def_id, ir, target, hash`
- **Intended:** Cache serialized MIR/IR per definition per compilation target for incremental builds
- **Status:** 0 rows. Query methods exist (`cache_compiled`, `get_compiled` in `queries.rs:277-306`) but are never called from the build pipeline. The Rust compiler always recompiles from source.
- **Verdict:** Dead. The incremental compilation design assumed this cache; it was never wired up.

---

## Dead Columns (defined but never meaningfully used)

### `def.sig`
- **Intended:** Store the inferred type signature (e.g., `I -> I -> I`) after type checking
- **Status:** Always NULL. The Rust type checker infers types but never writes them back to the database. The self-hosted compiler doesn't populate it either.
- **Impact:** Blocks potential features like signature-based search, API documentation generation, and type-aware dependency analysis.

### `def.created` / `def.modified`
- **Intended:** Audit trail timestamps
- **Status:** Auto-populated via `DEFAULT datetime('now')` on INSERT. Never read by any query in either compiler.
- **Impact:** Minimal. They're free to maintain (SQLite handles it) but provide no value currently.

---

## Unpopulated Def Kinds

The `def.kind` CHECK constraint allows 10 values. Only 3 are used:

| Kind | Rows | Status |
|------|------|--------|
| `fn` | ~795 | Active |
| `type` | ~4 | Active |
| `test` | ~6 | Active |
| `trait` | 0 | Planned, never implemented |
| `impl` | 0 | Planned, never implemented |
| `const` | 0 | Planned, never implemented |
| `fsm` | 0 | Planned, never implemented |
| `srv` | 0 | Planned, never implemented |
| `macro` | 0 | Planned, never implemented |

The build pipeline explicitly ignores unknown kinds:
```rust
// crates/glyph-cli/src/build.rs
match def.kind {
    DefKind::Fn => { /* compile */ },
    DefKind::Type => { /* parse type def */ },
    _ => {}  // silently skip
}
```

**Notable:** `const` would be straightforward to implement (constant folding at compile time). `trait`/`impl` would require significant type system work. `fsm`/`srv` appear to be domain-specific ideas (finite state machines, servers) that were never fleshed out.

---

## Empty Dependency Graph

### `dep` table
- **Columns:** `from_id, to_id, edge`
- **Edge types:** `calls`, `uses_type`, `implements`, `field_of`, `variant_of`
- **Status:** 0 rows in all `.glyph` databases
- **Impact:** This is the most consequential gap. Without dependency edges:
  - `v_dirty` view only returns defs with `compiled=0`, never transitive dependents
  - `glyph deps` / `glyph rdeps` commands return empty results
  - Incremental compilation is effectively full-rebuild-only
  - The `compiled` cache table can't invalidate correctly

The Rust compiler would need to analyze parsed ASTs to extract call references and type usage, then INSERT edges into `dep`. The self-hosted compiler has the same gap.

---

## Unused Views

### `v_context`
- **Purpose:** Order definitions by dependency depth then token count, for token-budgeted context export
- **Status:** Defined in schema, never referenced in any code
- **Note:** The self-hosted `dump --budget` command exists but iterates definitions flat (doesn't use this view)

### `v_callgraph`
- **Purpose:** Human-readable call graph (caller name -> callee name with edge type)
- **Status:** Defined in schema, never referenced in any code
- **Note:** Would become useful once the `dep` table is populated

### `v_dirty` - USED but degraded
- Works correctly but returns only `compiled=0` defs (no transitive dependents, since `dep` is empty)

---

## Indexes on Empty Tables

These indexes are properly defined but index zero rows:
- `idx_dep_to` on `dep(to_id)` - would be critical once dep table is populated
- `idx_tag_key_val` on `tag(key, val)` - dead

---

## Meta Table

### `meta`
- **Columns:** `key, value`
- **Status:** 1 row (`schema_version = '4'`). Used by `init_schema` for migration checks.
- **Verdict:** Active and working.

---

## Interconnection Map

```
                        ACTIVE              DEAD/EMPTY
                    +----------+        +---------------+
                    |   def    |------->|     dep       |  (no edges populated)
                    | (fn,type |        +---------------+
                    |  test)   |------->|     tag       |  (no metadata)
                    +----------+        +---------------+
                         |          +-->|   module      |  (no grouping)
                         |          |   | module_member |
                    +----------+    |   +---------------+
                    | extern_  |    +-->|   compiled    |  (no IR cache)
                    +----------+        +---------------+
                    +----------+        +---------------+
                    |def_history|        |  v_context    |  (unused view)
                    +----------+        |  v_callgraph  |  (unused view)
                    +----------+        +---------------+
                    |   meta   |
                    +----------+
```

---

## Recommendations

### Low-effort, high-value
1. **Populate `dep` table** during build — analyze parsed ASTs for function calls and type references. This unblocks incremental compilation, `deps`/`rdeps` commands, and both dead views.
2. **Populate `def.sig`** after type inference — store the inferred signature string back to the database. Enables signature search and documentation.

### Keep as-is (low cost to maintain)
3. **`def.created`/`def.modified`** — free timestamps, may be useful for tooling later
4. **Unused def kinds in CHECK constraint** — no cost, documents the design intent

### Candidates for removal (if cleaning up)
5. **`module`/`module_member`** — no infrastructure exists, would need full design if ever implemented
6. **`tag`** — has query infrastructure but no callers; trivial to re-add later
7. **`compiled`** — depends on `dep` table being populated first; premature without it
8. **`v_context`/`v_callgraph`** — dead views that depend on empty `dep` table
