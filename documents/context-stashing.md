# Context Stashing: Ephemeral Scratch Storage for Glyph MCP

**Status:** Speculative / Under Consideration

## Problem

When an LLM works on a complex task across multiple turns (refactoring, multi-step analysis, large codegen), intermediate results have no natural home:

- **Stuffing into context:** Re-sending large intermediate data every turn wastes tokens and hits context limits.
- **Polluting `def` table:** Temporary working data doesn't belong alongside program definitions.
- **Lost between turns:** Without persistence, the LLM must re-derive intermediate results each turn.

The Glyph MCP already solves the "don't paste the whole program" problem — definitions live in SQLite, queried on demand. Context stashing extends this pattern to **temporary working data** produced during multi-turn tasks.

## Use Cases

1. **Large refactors:** Stash a dependency analysis or type map, reference it across subsequent edit turns without re-querying.
2. **Multi-step codegen:** Stash partial C output or MIR dumps while iterating on a function, discard when done.
3. **Cross-definition analysis:** Stash call graph subsets, field usage maps, or error lists that span many definitions.
4. **Shared sessions:** Multiple LLM sessions (or tools) working on the same `.glyph` file can share intermediate state through the stash.

## Design

### Schema

Single new table, added to the Glyph database schema:

```sql
CREATE TABLE stash (
    key        TEXT PRIMARY KEY,
    value      TEXT NOT NULL,
    created_at INTEGER NOT NULL DEFAULT (unixepoch()),
    expires_at INTEGER  -- NULL = no expiry
);
```

Lives in the program's `.glyph` file alongside `def`, `dep`, etc. No separate database.

### MCP Tools

Two new tools added to the existing MCP server dispatch:

#### `stash_put`

Store or overwrite a value under a key.

| Parameter | Type   | Required | Description |
|-----------|--------|----------|-------------|
| `key`     | string | yes      | Identifier for the stashed value |
| `value`   | string | yes      | Content to store (text, JSON, code, etc.) |
| `ttl`     | int    | no       | Time-to-live in seconds. NULL = permanent until explicit delete. |

Behavior:
- Upserts (INSERT OR REPLACE) the key.
- If `ttl` is provided, sets `expires_at = unixepoch() + ttl`.
- Before inserting, deletes all rows where `expires_at < unixepoch()` (lazy cleanup).
- Returns `{"ok": true}`.

#### `stash_get`

Retrieve a stashed value, optionally with projection.

| Parameter    | Type   | Required | Description |
|--------------|--------|----------|-------------|
| `key`        | string | yes      | Key to look up |
| `projection` | string | no       | Line range (`"10-20"`), JSON path (`".results[0].name"`), or omit for full value |

Behavior:
- Returns `{"key": "...", "value": "..."}` or `{"error": "not found"}`.
- Checks expiry: if `expires_at < unixepoch()`, deletes and returns not found.
- Projection (if provided):
  - **Line range** (`"5-15"`): returns lines 5 through 15 of the value.
  - **JSON path** (`".foo.bar"`): if value is valid JSON, extracts the path. Uses existing JSON subsystem.
  - **No projection**: returns full value.

#### `stash_del`

Delete a stashed value.

| Parameter | Type   | Required | Description |
|-----------|--------|----------|-------------|
| `key`     | string | yes      | Key to delete |

Returns `{"ok": true}` regardless of whether key existed.

#### `stash_list`

List all active (non-expired) stash keys.

| Parameter | Type   | Required | Description |
|-----------|--------|----------|-------------|
| `pattern` | string | no       | SQL LIKE pattern to filter keys. Default `%`. |

Returns `{"keys": [{"key": "...", "size": 1234, "expires_at": ...}, ...]}`.

### Cleanup Strategy

- **Lazy:** Every `stash_put` call deletes expired rows first.
- **No background process:** Glyph MCP is request-driven, no daemon. Expired rows persist until the next write operation touches them.
- **Manual:** `stash_del` for explicit removal. The `sql` tool can also `DELETE FROM stash` directly.

## Implementation Estimate

~20 new definitions in `glyph.glyph`, following existing MCP tool patterns:

| Category | Definitions | Description |
|----------|-------------|-------------|
| Schema   | 1           | `init_schema` modification (add `stash` table) |
| Cleanup  | 1           | `stash_cleanup` — delete expired rows |
| Put      | 2           | `mcp_tool_stash_put`, `stash_do_put` |
| Get      | 4           | `mcp_tool_stash_get`, `stash_do_get`, `stash_project_lines`, `stash_project_json` |
| Del      | 1           | `mcp_tool_stash_del` |
| List     | 2           | `mcp_tool_stash_list`, `stash_list_loop` |
| Dispatch | 1           | Modify `mcp_dispatch` to route new tool names |
| JSON     | 0           | Reuses existing JSON subsystem for path projection |

Total: ~12 new definitions + 2 modifications.

## What This Is Not

- **Not a general-purpose cache.** No eviction policy beyond TTL. Not optimized for high-throughput access.
- **Not a replacement for `def`.** Program definitions belong in `def`. Stash is for ephemeral working data.
- **Not a novel idea.** This is a key-value store with TTL. The value is integration with Glyph's existing SQLite-as-program model and MCP tool surface.

## Alternative: Just Use `sql`

The existing `sql` MCP tool already allows:

```sql
CREATE TABLE IF NOT EXISTS stash (key TEXT PRIMARY KEY, value TEXT);
INSERT OR REPLACE INTO stash VALUES ('my_analysis', '...');
SELECT value FROM stash WHERE key = 'my_analysis';
```

Dedicated tools save ~20 tokens per operation and provide TTL/projection convenience. Whether that justifies the implementation cost depends on how frequently multi-turn stashing comes up in practice.

## Open Questions

1. **Per-database or global?** Current design puts `stash` in each `.glyph` file. A global stash (separate SQLite file) would allow cross-project sharing but adds complexity.
2. **Size limits?** Should `stash_put` reject values over some threshold (e.g., 1MB)? SQLite handles large TEXT fine, but it could bloat the program database.
3. **Binary data?** Current design is text-only. BLOB support would require base64 encoding in the JSON MCP protocol.
4. **Schema migration:** Adding `stash` table to existing databases requires a migration path (schema_version bump, CREATE IF NOT EXISTS in init).
