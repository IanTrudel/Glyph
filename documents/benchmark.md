Great question. There are several axes worth proving, each with different stress tests:

## 1. Query-powered code intelligence (the killer feature)

File-based tooling needs external indexers (ctags, LSP servers, tree-sitter). With SQLite, it's native:

```sql
-- "What functions does nobody call?" (dead code)
SELECT name FROM def WHERE kind='fn'
  AND name NOT IN (SELECT DISTINCT callee FROM dep);

-- "What's the transitive closure of main's dependencies?"
WITH RECURSIVE tc(name) AS (
  SELECT 'main'
  UNION
  SELECT d.callee FROM dep d JOIN tc ON tc.name = d.caller
) SELECT * FROM tc;

-- "Show me the 10 largest functions by token count"
SELECT name, tokens FROM def WHERE kind='fn' ORDER BY tokens DESC LIMIT 10;

-- "What changed in the last hour?" (built-in history)
SELECT * FROM def_history WHERE timestamp > datetime('now', '-1 hour');
```

**Stress test**: Generate a program with 10k+ definitions and a realistic dependency graph. Time these queries vs. equivalent `grep`/`ctags`/`ripgrep` operations on an exported file tree. SQLite should win by orders of magnitude on graph queries.

## 2. Incremental compilation at scale

The `v_dirty` view (content hash + transitive dependents via `dep` table) is a built-in incremental build system with zero external tooling.

**Stress test**:
- Create a 5k-definition program with a deep dependency graph
- Change one leaf function -> measure rebuild (should recompile only its dependents)
- Change a core utility used by 500 functions -> measure rebuild
- Compare wall-clock time vs. full rebuild
- Compare with `make` or `ninja` doing the same job on equivalent `.c` files -- the point being that Glyph needs no Makefile at all, the schema *is* the build system

## 3. Atomic operations / crash safety

Files can be half-written. SQLite gives you ACID for free.

**Stress test**:
- Write a script that inserts/updates definitions in a tight loop
- `kill -9` the process at random points
- Verify the database is never corrupt (`PRAGMA integrity_check`)
- Try the same with a directory of `.c` files being written by a script -- you'll find truncated files

## 4. Concurrent tooling access

Multiple tools can read the same program simultaneously (SQLite WAL mode).

**Stress test**:
- Run `glyph build` in one process while `glyph test` reads in another, while an MCP server handles queries in a third
- Verify no lock contention errors, no stale reads
- This is something file-based builds struggle with (lock files, race conditions)

## 5. Space efficiency and portability

One file vs. a directory tree.

**Stress test**:
- Export glyph.glyph (~1,774 defs) to files with `glyph export`, compare total size
- The `.glyph` file is a single portable artifact -- `scp` one file and you have the entire program + history + dependency graph + compilation cache
- Measure: `du -sh glyph.glyph` vs. `du -sh exported_src/`

## 6. LLM-native workflow (the design thesis)

The real argument is that LLMs work better with structured data than with file trees.

**Stress test**:
- Give an LLM a task requiring cross-cutting changes (rename a type used in 50 places)
- Compare: (a) LLM with SQL access to a `.glyph` DB, (b) LLM with file-tree access
- Measure: token cost, accuracy, number of tool calls, time to completion
- The SQL approach should require fewer tokens (targeted queries vs. reading entire files) and fewer errors (atomic multi-row UPDATE vs. find-and-replace across files)

## What I'd actually build

A benchmark program that generates synthetic programs at various scales and runs timed comparisons:

| Operation | .glyph (SQL) | File tree + make |
|-----------|-------------|-----------------|
| Find all callers of `foo` | `SELECT` on dep | `rg 'foo(' src/` |
| Dead code detection | 1 SQL query | Build full call graph from scratch |
| Incremental rebuild (1 leaf change) | `v_dirty` -> cc | `make` with `.d` files |
| Incremental rebuild (core change) | Same mechanism | Same mechanism |
| "What changed?" | `SELECT` on def_history | `git log` (external tool) |
| Crash recovery | Automatic (ACID) | Hope for the best |
| Move entire project | `cp a.glyph b.glyph` | `cp -r src/ dst/` |

The most compelling demonstrations are the ones where the file-based approach requires *external tooling* (LSP, ctags, make, git) to do what SQL gives you for free. The database isn't just a different container -- it's a queryable, transactional, self-indexing program representation.
