# Remote Database Build: Compiling Glyph Programs from External Databases

## Rationale

Glyph programs are SQLite databases. The compiler reads definitions from SQL tables, compiles them through its pipeline (tokenize, parse, type-check, lower, codegen), and produces a native binary. Nothing about the compilation pipeline requires SQLite specifically — it requires **rows of definitions**. This observation opens a powerful extension: compile programs stored in any SQL-compatible database.

### Why this matters

- **Collaborative development.** Multiple LLMs can write definitions concurrently to a shared PostgreSQL database. Any machine with Glyph installed pulls and builds a native binary.
- **Portability.** The same program source on a remote server produces native binaries for every target platform. The remote database is the source of truth; local machines are build nodes.
- **Deployment.** A CI/CD system or edge node runs `glyph build postgres://prod-db/myapp` and gets a native binary without checking anything out.
- **Scale separation.** A 20-million-line program lives on a beefy database server. Developer machines only fetch and compile the definitions they need.

### Why not just copy the .glyph file?

For small programs (< 10k definitions), copying the `.glyph` file works fine. But for large programs:

- A full dump of 500k definitions into a temp SQLite file is a bottleneck — you're transferring and rewriting the entire program before you can compile anything.
- The incremental compilation machinery (content hashes, dirty tracking) already exists in the schema. Running it on the remote database means you only transfer **changed definitions and their dependents** over the network.
- Concurrent writes to a SQLite file require file-level locking. PostgreSQL provides row-level locking and true multi-writer concurrency.

---

## Architecture

### What the compiler reads

The compilation pipeline queries a small, well-defined set of tables:

| Table | Purpose | When read |
|-------|---------|-----------|
| `def` | All definitions (name, kind, body, hash, tokens, gen, compiled) | Every build — filtered by dirty/gen |
| `dep` | Dependency graph edges (from_id, to_id, edge) | Dirty computation (transitive closure) |
| `extern_` | C ABI foreign function declarations (name, symbol, sig) | Linking phase |
| `meta` | Schema version, cc_args, cc_prepend | Build configuration |
| `compiled` | Cached compilation artifacts (MIR per target) | Incremental builds |

The compiler does **not** write to these tables during compilation (it marks definitions as `compiled=1` afterward, but that is a post-build step). The build itself is a **read pipeline**.

### What the compiler does NOT need during build

- `def_history` — audit trail, not needed for compilation
- `module` / `module_member` — logical grouping, not used by codegen
- TEMP triggers — these exist only for interactive editing (hash/token recomputation)

### The query interface

The Rust compiler accesses data through these query functions in `glyph-db`:

```
effective_defs(target_gen)     → Vec<DefRow>    -- all defs for a generation
dirty_defs_gen(target_gen)     → Vec<DefRow>    -- only dirty + dependents
defs_by_kind_gen(kind, gen)    → Vec<DefRow>    -- e.g., all test defs
```

The self-hosted compiler uses equivalent SQL through `db_query_rows`:

```sql
-- Dirty definitions (the v_dirty view, inlined with generation filtering)
WITH RECURSIVE effective AS (
    SELECT d.* FROM def d
    INNER JOIN (
        SELECT name, kind, MAX(gen) as max_gen
        FROM def WHERE gen <= ?1
        GROUP BY name, kind
    ) latest ON d.name = latest.name AND d.kind = latest.kind AND d.gen = latest.max_gen
),
dirty(id) AS (
    SELECT id FROM effective WHERE compiled = 0
    UNION
    SELECT d.from_id FROM dep d JOIN dirty ON d.to_id = dirty.id
)
SELECT DISTINCT e.* FROM effective e JOIN dirty ON e.id = dirty.id
```

These queries are standard SQL. They run identically on PostgreSQL, MySQL, or any SQL database with recursive CTE support.

---

## Design: Streaming Query Interface

The key design principle is that the compiler should **stream definitions from the remote database** rather than copying the entire database locally. The remote database runs the dirty computation and dependency resolution server-side, and only the relevant rows are transferred.

### Connection abstraction

The compiler currently opens a database with a file path:

```
db_open("program.glyph")   -- SQLite file
```

This generalizes to a URI scheme:

```
db_open("program.glyph")                     -- local SQLite (default, unchanged)
db_open("postgres://host:5432/myapp")         -- PostgreSQL
db_open("mysql://host:3306/myapp")            -- MySQL
db_open("https://api.example.com/myapp")      -- HTTP API (optional)
```

The connection handle returned supports the same operations regardless of backend:

```
query_rows(conn, sql, params) → [Row]
query_one(conn, sql, params)  → Row
exec(conn, sql, params)       → int
```

### What runs where

For a remote build of a large program:

```
                        ┌──────────────────────────┐
                        │   Remote Database         │
                        │   (PostgreSQL / MySQL)    │
                        │                           │
  glyph build ─────────►│  1. Run v_dirty query     │
  postgres://host/app   │     (server-side CTE)     │
                        │  2. Stream dirty def rows  │──── only changed defs
                        │  3. Stream dep edges       │──── only for dirty defs
                        │  4. Stream extern_ rows    │──── small, ~20 rows
                        │  5. Stream meta values     │──── 2-3 rows
                        └──────────────────────────┘
                                    │
                                    ▼
                        ┌──────────────────────────┐
                        │   Local Machine           │
                        │                           │
                        │  6. Tokenize + parse      │
                        │  7. Type-check            │
                        │  8. Lower to MIR          │
                        │  9. Codegen (C / LLVM IR) │
                        │  10. cc / llc → binary    │
                        └──────────────────────────┘
```

The heavy work (SQL filtering, dirty computation) runs on the database server. The heavy work (compilation) runs on the local machine. Only definition bodies cross the network.

### Local hash cache

For incremental remote builds, a local cache avoids retransmitting unchanged definitions:

```
~/.glyph/cache/<db-uri-hash>/
  hashes.db          -- SQLite: (name, kind, gen, hash, compiled_artifact)
```

Build flow with cache:

1. Query remote for `SELECT name, kind, gen, hash FROM def` (lightweight — no bodies)
2. Compare against local `hashes.db`
3. Fetch full rows only for definitions whose hash differs
4. Compile only changed definitions
5. Update local cache with new hashes + artifacts

For a 500k-definition program where 12 definitions changed, this transfers ~12 definition bodies instead of 500k.

---

## Schema Compatibility

The remote database must implement the Glyph schema. The core tables are:

```sql
CREATE TABLE def (
    id        INTEGER PRIMARY KEY,
    name      TEXT    NOT NULL,
    kind      TEXT    NOT NULL CHECK(kind IN ('fn','type','test','const','data')),
    sig       TEXT,
    body      TEXT    NOT NULL,
    hash      BLOB    NOT NULL,
    tokens    INTEGER NOT NULL,
    compiled  INTEGER NOT NULL DEFAULT 0,
    gen       INTEGER NOT NULL DEFAULT 1,
    ns        TEXT,
    created   TEXT,
    modified  TEXT
);

CREATE TABLE dep (
    from_id   INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
    to_id     INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
    edge      TEXT    NOT NULL CHECK(edge IN ('calls','uses_type','implements','field_of','variant_of')),
    PRIMARY KEY (from_id, to_id, edge)
);

CREATE TABLE extern_ (
    id        INTEGER PRIMARY KEY,
    name      TEXT    NOT NULL UNIQUE,
    symbol    TEXT    NOT NULL,
    lib       TEXT,
    sig       TEXT    NOT NULL,
    conv      TEXT    NOT NULL DEFAULT 'C' CHECK(conv IN ('C','system','rust'))
);

CREATE TABLE meta (
    key       TEXT    PRIMARY KEY,
    value     TEXT
);

CREATE TABLE compiled (
    def_id    INTEGER PRIMARY KEY REFERENCES def(id) ON DELETE CASCADE,
    ir        BLOB    NOT NULL,
    target    TEXT    NOT NULL,
    hash      BLOB    NOT NULL
);

CREATE TABLE def_history (
    id         INTEGER PRIMARY KEY,
    def_id     INTEGER NOT NULL,
    name       TEXT    NOT NULL,
    kind       TEXT    NOT NULL,
    sig        TEXT,
    body       TEXT    NOT NULL,
    hash       BLOB    NOT NULL,
    tokens     INTEGER NOT NULL,
    gen        INTEGER NOT NULL DEFAULT 1,
    changed_at TEXT    NOT NULL DEFAULT (datetime('now'))
);
```

### SQL dialect differences

The schema above is valid in both SQLite and PostgreSQL with minor adjustments:

| Feature | SQLite | PostgreSQL | Resolution |
|---------|--------|------------|------------|
| `INTEGER PRIMARY KEY` auto-increment | Implicit ROWID | Use `SERIAL` or `GENERATED ALWAYS AS IDENTITY` | Adapter translates DDL |
| `BLOB` type | Native | `BYTEA` | Type alias in adapter |
| `datetime('now')` | SQLite function | `NOW()` or `CURRENT_TIMESTAMP` | Adapter translates |
| Recursive CTEs | Supported (3.8.3+) | Supported | No change needed |
| Custom functions (`glyph_hash`, `glyph_tokens`) | Registered via C API | Must be PL/pgSQL or C extension | See below |

### Custom functions

The `glyph_hash` (BLAKE3 content hash) and `glyph_tokens` (BPE token count) functions are currently registered as SQLite C extensions inside the compiler process. For PostgreSQL:

**Option A: Application-side computation.** The compiler computes hash and token count before INSERT. The `glyph put` command and MCP `put_def` tool already do this — the custom functions are a convenience for raw SQL inserts, not a compilation requirement.

**Option B: PostgreSQL extension.** A small C extension implementing `glyph_hash(kind, sig, body) → bytea` and `glyph_tokens(body) → integer`. This preserves the ability to use raw SQL inserts with automatic hash/token computation.

Option A is sufficient. The custom functions are only needed for **writing** definitions, and writes go through `glyph put` / MCP tools, not raw SQL.

---

## Implementation Plan

### Phase 1: Connection abstraction (self-hosted compiler)

**What:** Abstract `db_open` / `db_query_rows` / `db_exec` behind a connection type that dispatches on URI scheme.

**Where:** New definitions in `glyph.glyph`, plus a `libpq` FFI binding.

**New extern declarations:**
```
pq_connect    PQconnectdb     libpq  (S) -> *V
pq_status     PQstatus        libpq  (*V) -> I
pq_exec       PQexec          libpq  (*V, S) -> *V
pq_ntuples    PQntuples       libpq  (*V) -> I
pq_nfields    PQnfields       libpq  (*V) -> I
pq_getvalue   PQgetvalue      libpq  (*V, I, I) -> S
pq_clear      PQclear         libpq  (*V) -> V
pq_finish     PQfinish        libpq  (*V) -> V
```

**New definitions (~15-20):**
```
-- Connection type dispatch
db_open_uri uri         -- parse URI, dispatch to sqlite3_open or pq_connect
db_is_remote uri        -- true if postgres:// or mysql://
db_query_remote conn sql -- execute query, return rows as arrays of strings

-- PostgreSQL adapter
pq_connect_checked uri  -- connect + check status
pq_query_rows conn sql  -- execute, iterate ntuples * nfields, return [[S]]
pq_query_one conn sql   -- single row variant
pq_exec_checked conn sql -- execute non-query

-- Row conversion (remote rows come as strings, need parsing)
row_to_def_remote row   -- [S] -> same shape as db_query_rows result
parse_hash_hex hex      -- hex string -> blob (PostgreSQL returns bytea as hex)
```

**Modified definitions (~5):**
- `cmd_build` — accept URI argument, dispatch to `db_open_uri`
- `read_fn_defs` / `read_fn_defs_gen` — use connection abstraction
- `read_externs` — use connection abstraction
- `build_program` — pass connection handle instead of path

**Build flag:** `-lpq` added automatically when a `postgres://` URI is detected (via `cc_args` mechanism or dynamic flag).

### Phase 2: Local hash cache

**What:** Cache definition hashes locally so incremental remote builds skip unchanged definitions.

**Where:** `~/.glyph/cache/` directory, one SQLite file per remote URI.

**Cache schema:**
```sql
CREATE TABLE cached_def (
    name      TEXT NOT NULL,
    kind      TEXT NOT NULL,
    gen       INTEGER NOT NULL,
    hash      BLOB NOT NULL,
    PRIMARY KEY (name, kind, gen)
);
CREATE TABLE cached_meta (
    key       TEXT PRIMARY KEY,
    value     TEXT
);
```

**New definitions (~10):**
```
cache_path uri          -- hash URI to ~/.glyph/cache/<hash>.db
cache_open uri          -- open or create cache DB
cache_lookup name kind gen -- return cached hash or nil
cache_update name kind gen hash -- upsert hash
cache_diff_remote conn cache  -- query remote hashes, diff against cache, return changed names
```

**Build flow:**
1. `cache_diff_remote` fetches `SELECT name, kind, gen, hash FROM def` from remote
2. Compares each row against `cached_def`
3. Returns list of changed (name, kind) pairs
4. Compiler fetches full bodies only for changed definitions
5. After successful build, `cache_update` writes new hashes

### Phase 3: CLI integration

**What:** The `glyph build` command accepts URIs alongside file paths.

**Syntax:**
```bash
glyph build postgres://host/myapp                    # build from remote, output to ./myapp
glyph build postgres://host/myapp myapp --release     # release build
glyph build mysql://host:3306/myapp myapp             # MySQL source
glyph build myapp.glyph                               # local file (unchanged)
```

**New CLI definitions (~5):**
```
parse_build_uri arg     -- detect URI vs file path
cmd_build_remote uri out flags  -- orchestrate remote build
print_remote_stats conn -- print "Connected to postgres://... (N definitions)"
```

### Phase 4: Rust compiler support (optional)

**What:** Extend the Rust bootstrap compiler (`glyph0`) with the same remote build capability.

**Where:** `crates/glyph-db/src/connection.rs` — add a `RemoteDatabase` enum variant.

This phase is optional because the self-hosted compiler is the primary compiler. The Rust compiler is maintenance-mode only.

---

## What this does NOT change

- **Local development workflow.** `glyph build program.glyph` continues to work exactly as it does today. The remote build feature is additive.
- **The compilation pipeline.** Tokenizer, parser, type checker, MIR lowering, and codegen are completely unchanged. They receive definition rows; they don't care where those rows came from.
- **The MCP server.** It continues to operate on local `.glyph` files. A future extension could proxy MCP operations to a remote database, but that is a separate feature.
- **The bootstrap chain.** `ninja` still builds from the local `glyph.glyph` file.

---

## Security Considerations

- **Connection strings.** Database credentials in URIs (`postgres://user:pass@host/db`) should not be logged or stored in build artifacts. The compiler should read credentials from environment variables or a config file, not from command-line arguments visible in `ps`.
- **SQL injection.** The compiler constructs SQL queries with parameterized values (never string concatenation). This is already the case for SQLite and must remain so for PostgreSQL.
- **Network trust.** Definitions fetched from a remote database are compiled and executed. A compromised database could inject malicious code. This is equivalent to trusting a git remote — the user must trust the database they point the compiler at.
- **TLS.** PostgreSQL connections should use `sslmode=require` by default. The `libpq` connection string supports this natively.

---

## Performance Characteristics

### Small program (< 1k definitions, typical application)

Remote build overhead is negligible. A single `SELECT * FROM def WHERE kind='fn'` returns all definitions in one round-trip. Total network transfer: ~100 KB. Local SQLite is still faster (no network latency), but the difference is imperceptible.

### Medium program (1k-50k definitions)

The hash cache becomes valuable. After the first full build, subsequent builds transfer only changed definitions. With 10 changes out of 10k definitions, the transfer is ~10 definition bodies (~5-50 KB) plus one lightweight hash comparison query.

### Large program (50k+ definitions)

Server-side dirty computation is critical. The recursive CTE in `v_dirty` runs on the database server, which has the full `dep` table indexed. Only the dirty subgraph crosses the network. For a 500k-definition program with a localized change, this might be 50-200 definitions.

The local hash cache further reduces this: if the developer built recently and only a few definitions changed, the diff query returns only new hashes, and only genuinely new bodies are fetched.

### Comparison with file-based approaches

| Operation | Remote .glyph (SQL) | `git pull` + file build |
|-----------|---------------------|------------------------|
| Determine what changed | 1 SQL query (server-side) | `git fetch` + `git diff` + parse changed files |
| Transfer changed code | Stream N rows (~KB) | Transfer full file diffs (~KB, similar) |
| Determine what to recompile | `v_dirty` CTE (server-side) | Build system dependency scan (local) |
| Incremental recompile | Identical | Identical |
| First build (cold cache) | Stream all definitions | Clone entire repo |

The SQL approach wins on "determine what changed" and "determine what to recompile" because the database already has the dependency graph indexed. The file-based approach must reconstruct this information from the file system and build rules.

---

## Future Extensions

### HTTP API adapter

For environments where direct database connections are impractical (firewalls, serverless), an HTTP API could wrap the database:

```
GET  /api/v1/program/defs?dirty=true&gen=1    → JSON array of definitions
GET  /api/v1/program/defs/hashes               → lightweight hash list
GET  /api/v1/program/externs                    → extern declarations
GET  /api/v1/program/meta                       → build configuration
```

This would be a thin REST wrapper around the same SQL queries.

### `glyph push` / `glyph pull`

Bidirectional sync between local `.glyph` files and remote databases:

```bash
glyph pull postgres://host/myapp local.glyph   # sync remote → local
glyph push local.glyph postgres://host/myapp   # sync local → remote
```

Uses content hashes for conflict-free merge of non-overlapping changes. Conflicting definitions (same name, different hash on both sides) require explicit resolution.

### Multi-target builds

A single remote program compiled on multiple machines simultaneously:

```
postgres://host/myapp  ──►  linux-x86_64 build node  ──►  myapp-linux
                       ──►  linux-aarch64 build node  ──►  myapp-arm
                       ──►  macos-x86_64 build node   ──►  myapp-macos
                       ──►  windows-x86_64 build node ──►  myapp-windows
```

The database is the single source of truth. Each build node fetches definitions, compiles for its target, and optionally uploads the artifact back to a release table.
