# Glyph-Native SQLite3 Driver

## Motivation

Glyph programs are SQLite3 databases. The compiler reads definitions from `.glyph` files using the SQLite3 C library (`libsqlite3`), linked via FFI. This C dependency creates an architectural constraint: **any compilation target that cannot link C libraries cannot bootstrap the Glyph compiler.**

The WASM backend (Phase 3 of pluggable backends) demonstrated this constraint concretely. It compiles 1,820 definitions of the self-hosted compiler to a 2.4MB `.wasm` binary that runs in wasmtime — but the binary cannot compile a single definition because it has no SQLite access. The C and LLVM backends produce native binaries that link `libsqlite3` and bootstrap normally. The WASM backend produces a binary that can run algorithms but not the compiler.

A pure-Glyph SQLite3 driver would eliminate this dependency. Every backend that supports file I/O and computation could bootstrap. The compiler would be fully self-contained.

This document evaluates the feasibility, scope, and risks of implementing such a driver.

---

## SQLite3 File Format Overview

The SQLite3 file format is public and stable (backwards-compatible since 2004). The format is well-documented at [sqlite.org/fileformat2.html](https://www.sqlite.org/fileformat2.html). What follows is a complete technical reference for implementors.

### Database Header (100 bytes)

Every SQLite3 database begins with a 100-byte header. All multi-byte integers are **big-endian**.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 16 | Magic | `"SQLite format 3\000"` |
| 16 | 2 | Page size | Power of 2: 512–32768. Value `1` means 65536. |
| 18 | 1 | Write version | 1 = journal, 2 = WAL |
| 19 | 1 | Read version | 1 = journal, 2 = WAL |
| 20 | 1 | Reserved bytes/page | Space reserved at end of each page (usually 0) |
| 21 | 1 | Max embedded payload | Must be 64 |
| 22 | 1 | Min embedded payload | Must be 32 |
| 23 | 1 | Leaf payload fraction | Must be 32 |
| 24 | 4 | File change counter | Incremented on each DB modification |
| 28 | 4 | Database size (pages) | Valid when matching version-valid-for at offset 92 |
| 32 | 4 | First freelist trunk | Page number, or 0 if none |
| 36 | 4 | Total freelist pages | Count of all freelist pages |
| 40 | 4 | Schema cookie | Incremented on schema changes |
| 44 | 4 | Schema format | 1, 2, 3, or 4 |
| 48 | 4 | Page cache size | Suggested cache size |
| 52 | 4 | Largest root B-tree | Non-zero = auto-vacuum enabled |
| 56 | 4 | Text encoding | 1 = UTF-8, 2 = UTF-16le, 3 = UTF-16be |
| 60 | 4 | User version | `PRAGMA user_version` |
| 64 | 4 | Incremental vacuum | 0 = disabled |
| 68 | 4 | Application ID | `PRAGMA application_id` |
| 72 | 20 | Reserved | Must be zero |
| 92 | 4 | Version-valid-for | Change counter when version stored |
| 96 | 4 | SQLite version | `SQLITE_VERSION_NUMBER` of last writer |

**Derived value:**
```
usable_size = page_size - reserved_bytes_per_page
```
Constraint: `usable_size >= 480`.

**glyph.glyph header values** (actual, verified):
- Page size: 4096
- Journal mode: WAL (write/read version = 2)
- Database size: 360 pages (1,474,560 bytes)
- Text encoding: UTF-8
- Schema format: 4
- Freelist: empty (0 trunk pages)

### Page Structure

Pages are numbered 1 to N. Page 1 is special: its first 100 bytes are the database header, so its B-tree header starts at byte 100.

**Four B-tree page types** (identified by the first byte of the page header):

| Flag | Type | Description |
|------|------|-------------|
| `0x02` | Interior index | Keys + child pointers |
| `0x05` | Interior table | Rowid keys + child pointers |
| `0x0A` | Leaf index | Keys with payload |
| `0x0D` | Leaf table | Rowid keys with payload |

**Table B-trees** store data rows keyed by 64-bit integer rowids. One per table. **Index B-trees** store index entries keyed by record-format payloads.

### B-tree Page Header

Offsets relative to page start (or byte 100 for page 1).

**Leaf pages (8 bytes):**

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | Page type flag |
| 1 | 2 | First freeblock offset (0 = none) |
| 3 | 2 | Cell count (K) |
| 5 | 2 | Cell content area start (0 = 65536) |
| 7 | 1 | Fragmented free bytes |

**Interior pages (12 bytes):** same as above plus:

| Offset | Size | Field |
|--------|------|-------|
| 8 | 4 | Right-most child page number |

### Page Layout (top to bottom)

1. Database header (100 bytes, page 1 only)
2. B-tree page header (8 or 12 bytes)
3. Cell pointer array (K × 2 bytes, big-endian offsets into cell content area)
4. Unallocated space
5. Cell content area (cells packed from end of page backwards)
6. Reserved region (at very end, `reserved_bytes_per_page` bytes)

### Varint Encoding

SQLite3 varints encode 64-bit integers in 1–9 bytes:

- Bytes 1–8: high bit is continuation flag, lower 7 bits contribute to value
- Byte 9 (if present): all 8 bits contribute
- Bits assembled big-endian (first byte is most significant)

```
decode_varint(bytes):
    result = 0
    for i in 0..8:
        byte = next()
        if i < 8:
            result = (result << 7) | (byte & 0x7F)
            if (byte & 0x80) == 0: return result
        else:
            result = (result << 8) | byte
            return result
```

| Value range | Bytes |
|-------------|-------|
| 0 – 127 | 1 |
| 128 – 16,383 | 2 |
| 16,384 – 2,097,151 | 3 |
| Up to 2^63-1 | Up to 9 |

### Cell Formats

**Table B-tree leaf cell (`0x0D`):**
```
[varint: payload_size] [varint: rowid] [payload] [4-byte overflow ptr if overflow]
```

**Table B-tree interior cell (`0x05`):**
```
[4-byte: left_child_page] [varint: rowid]
```

**Index B-tree leaf cell (`0x0A`):**
```
[varint: payload_size] [payload] [4-byte overflow ptr if overflow]
```

**Index B-tree interior cell (`0x02`):**
```
[4-byte: left_child_page] [varint: payload_size] [payload] [4-byte overflow ptr if overflow]
```

### Record Format

All payloads use the record format:

```
[varint: header_size] [varint: type_1] ... [varint: type_N] [value_1] ... [value_N]
```

Header size includes itself. Column count is determined by parsing type varints until `header_size` bytes consumed.

**Serial type codes:**

| Serial Type | Size | Meaning |
|-------------|------|---------|
| 0 | 0 | NULL |
| 1 | 1 | 8-bit signed integer |
| 2 | 2 | 16-bit big-endian signed integer |
| 3 | 3 | 24-bit big-endian signed integer |
| 4 | 4 | 32-bit big-endian signed integer |
| 5 | 6 | 48-bit big-endian signed integer |
| 6 | 8 | 64-bit big-endian signed integer |
| 7 | 8 | IEEE 754 64-bit float (big-endian) |
| 8 | 0 | Integer constant 0 |
| 9 | 0 | Integer constant 1 |
| 10–11 | — | Reserved |
| N ≥ 12, even | (N-12)/2 | BLOB |
| N ≥ 13, odd | (N-13)/2 | TEXT (database encoding, no NUL) |

**Examples:** A 10-byte text string has serial type `(10×2)+13 = 33`. A 32-byte BLOB has serial type `(32×2)+12 = 76`.

### Overflow Pages

When a cell's payload exceeds the on-page maximum, excess spills to overflow pages.

**Thresholds** (U = usable page size):

For table B-tree leaf cells:
```
X = U - 35                          (max local payload)
M = ((U - 12) * 32 / 255) - 23     (min local payload)
```

For index B-tree cells:
```
X = ((U - 12) * 64 / 255) - 23     (max local payload)
M = ((U - 12) * 32 / 255) - 23     (min local payload)
```

Decision:
```
if P <= X:  all on page
if P > X:
    K = M + ((P - M) % (U - 4))
    if K <= X: K bytes on page, rest overflows
    else:      M bytes on page, rest overflows
```

**For glyph.glyph** (U = 4096): max local = 4061 bytes. A few large definitions (e.g., `cg_runtime_c` at ~6KB) use overflow.

**Overflow page format:**
```
[4-byte: next_overflow_page (0 if last)] [U-4 bytes of payload data]
```

### sqlite_schema Table

Page 1 is always the root of `sqlite_schema` (alias: `sqlite_master`):

```sql
CREATE TABLE sqlite_schema(
    type TEXT,        -- 'table', 'index', 'view', 'trigger'
    name TEXT,        -- object name
    tbl_name TEXT,    -- associated table name
    rootpage INTEGER, -- root page number
    sql TEXT          -- CREATE statement
);
```

To find the `def` table, scan `sqlite_schema` for `type='table' AND name='def'`, read `rootpage`, then traverse the B-tree at that page.

### Write-Ahead Log (WAL)

When WAL mode is active (header bytes 18–19 = 2), a `-wal` file may contain uncommitted pages.

**WAL header (32 bytes):**

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | Magic (`0x377f0682` or `0x377f0683`) |
| 4 | 4 | Format version (3007000) |
| 8 | 4 | Page size |
| 12 | 4 | Checkpoint sequence |
| 16 | 4 | Salt-1 |
| 20 | 4 | Salt-2 |
| 24 | 8 | Checksum |

**WAL frame (24-byte header + page data):**

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | Page number |
| 4 | 4 | DB size after commit (0 if non-commit) |
| 8 | 4 | Salt-1 (must match WAL header) |
| 12 | 4 | Salt-2 (must match WAL header) |
| 16 | 8 | Cumulative checksum |

**Reading with WAL:** For each page, scan WAL frames newest-to-oldest. If found, use the WAL version. Otherwise, read the main file. A checkpointed database has an empty WAL — the main file is complete.

### Rollback Journal

In journal mode (header bytes 18–19 = 1), a `-journal` file stores original pages before modification.

**Journal header:**

| Offset | Size | Field |
|--------|------|-------|
| 0 | 8 | Magic (`0xD9D505F920A163D7`) |
| 8 | 4 | Page count (-1 = all remaining) |
| 12 | 4 | Checksum nonce |
| 16 | 4 | Initial DB size (pages) |
| 20 | 4 | Sector size |
| 24 | 4 | Page size |

**Page records:** `[4-byte page_number] [page data] [4-byte checksum]`

### Freelist

Unused pages form a linked list. Trunk pages contain: `[4-byte next_trunk] [4-byte count] [4-byte page_num]...`. Leaf pages are just free space. Header offset 32 points to the first trunk page.

### Pointer Map Pages

Only present with auto-vacuum enabled (header offset 52 ≠ 0). **glyph.glyph uses auto_vacuum=0, so no ptrmap pages exist.**

---

## What Glyph Actually Needs from SQLite

The self-hosted compiler's SQL usage, counted across all definitions:

| Operation | Count | Examples |
|-----------|-------|---------|
| SELECT | 104 | Table scans, filtered queries, JOINs, subqueries, recursive CTEs |
| INSERT | 22 | New definitions, dependencies, tags |
| DELETE | 16 | Clear dependencies, remove definitions |
| UPDATE | 6 | Mark compiled, update body |
| PRAGMA | 4 | journal_mode, user_version |
| ATTACH/DETACH | 2 | Library linking |

**Critical queries that require SQL interpretation:**

```sql
-- Effective definitions (subquery with aggregation)
SELECT d.* FROM def d
INNER JOIN (
    SELECT name, kind, MAX(gen) as max_gen
    FROM def WHERE gen <= ?1
    GROUP BY name, kind
) latest ON d.name = latest.name AND d.kind = latest.kind AND d.gen = latest.max_gen

-- Dirty cascade (recursive CTE)
WITH RECURSIVE dirty(id) AS (
    SELECT id FROM effective WHERE compiled = 0
    UNION
    SELECT d.from_id FROM dep d JOIN dirty ON d.to_id = dirty.id
)
SELECT DISTINCT e.* FROM effective e JOIN dirty ON e.id = dirty.id

-- Simple reads
SELECT * FROM def WHERE name = ?1 AND kind = ?2
SELECT * FROM extern_
SELECT key, val FROM tag WHERE def_id = ?1
```

### glyph.glyph Data Profile

| Table | Rows | Notes |
|-------|------|-------|
| def | ~1,914 | 1,479 fn + 415 test + 13 type + 5 prop + 2 const |
| dep | ~1,857 | Dependency edges |
| extern_ | 23 | FFI declarations |
| tag | variable | Metadata tags |
| def_history | ~129 | Change tracking |
| compiled | variable | Cached compilation artifacts |
| lib_dep | 6 | Library dependencies |

9 tables, 7 indexes, 3 triggers, 3 views. Max definition body: ~6,261 bytes.

---

## Implementation Tiers

### Tier 1: Read-Only Table Scanner (~500–800 LOC)

Parse the file format and iterate rows of known tables. No SQL parser. No indexes. No writes.

| Component | LOC | Description |
|-----------|-----|-------------|
| Header parsing | ~50 | Validate magic, extract page size, encoding, DB size |
| Varint decode | ~30 | 1–9 byte variable-length integers |
| Page header parsing | ~40 | Type flag, cell count, content offset |
| Cell pointer array | ~20 | Read K × 2-byte offsets |
| Leaf cell parsing | ~80 | Payload size, rowid, record data |
| Interior cell parsing | ~30 | Child page pointer, rowid |
| Record format decode | ~100 | Serial type table, value extraction |
| B-tree traversal | ~100 | Recursive descent through interior → leaf pages |
| Overflow pages | ~40 | Follow linked list for large payloads |
| **Total** | **~490** | |

**Capabilities:** Read all rows of any table. The caller filters in application code (`if row.kind == "fn" ...`). Sufficient for loading definitions, dependencies, externs, and tags.

**Limitations:** No WHERE pushdown, no joins, no aggregation, no sorting — all done in application code. No WAL support — database must be checkpointed first. No write operations. No index lookups (full table scans only).

**WAL workaround:** Before building with a non-native backend, run `sqlite3 glyph.glyph "PRAGMA wal_checkpoint(TRUNCATE)"` or use `PRAGMA journal_mode=DELETE`. The build tool could do this automatically.

### Tier 2: Read-Only with Basic SQL (~2,500–3,500 LOC)

Add a SQL tokenizer, parser, and simple executor.

| Component | LOC | Description |
|-----------|-----|-------------|
| Tier 1 base | ~500 | Everything above |
| SQL tokenizer | ~300 | Keywords, identifiers, strings, numbers, operators |
| SQL parser | ~800 | SELECT, WHERE, ORDER BY, JOIN, GROUP BY, LIMIT |
| Expression evaluator | ~400 | Comparison, arithmetic, string ops, LIKE |
| Query executor | ~600 | Table scan with filter, sort, group, join |
| Subquery support | ~300 | Nested SELECTs, EXISTS, IN |
| **Total** | **~2,900** | |

**Capabilities:** Execute the SELECT queries the compiler uses. Still read-only.

**Limitations:** No writes, no CTEs, no ATTACH. The recursive CTE for dirty cascade would need special handling or reimplementation in application code.

### Tier 3: Read-Write with Known Schema (~4,000–6,000 LOC)

Add write operations for the known Glyph schema. No general SQL INSERT/UPDATE — hardcoded operations.

| Component | LOC | Description |
|-----------|-----|-------------|
| Tier 1 base | ~500 | Read operations |
| Varint encode | ~30 | Encode integers to 1–9 bytes |
| Record serialization | ~100 | Build record-format payloads |
| Cell construction | ~80 | Assemble cells with headers |
| Page allocation | ~100 | Freelist management, file extension |
| B-tree insert | ~500 | Insert cell, handle overflow |
| B-tree balance | ~800 | Page splitting (4 sub-algorithms) |
| B-tree delete | ~300 | Remove cell, rebalance |
| Transaction journal | ~400 | Rollback journal for crash safety |
| Overflow management | ~200 | Allocate/free overflow page chains |
| **Total** | **~3,010** | |

**Capabilities:** Full read-write access to `.glyph` databases. The compiler can load definitions, insert dependencies, mark definitions compiled, and update bodies.

**Limitations:** No general SQL — operations are hardcoded against the known schema. No triggers (dirty cascade must be reimplemented in application code). No views.

### Tier 4: Full SQL Engine (~10,000–17,000 LOC)

A general-purpose SQL database engine. This is what prsqlite (Rust, ~17,400 LOC) implements, with SELECT, INSERT, DELETE, CREATE TABLE, and CREATE INDEX.

**This tier is not recommended.** It reimplements the most complex parts of SQLite (query planner, optimizer, expression evaluation, type affinity) without the benefit of SQLite's 20+ years of testing.

---

## Existing Implementations in Other Languages

| Project | Language | LOC | Capabilities | Notes |
|---------|----------|-----|-------------|-------|
| [sqlittle](https://github.com/alicebob/sqlittle) | Go | ~5,700 | Read-only: table scan, index lookup, PK lookup | Most mature pure reader. No WAL. |
| [prsqlite](https://github.com/kawasin73/prsqlite) | Rust | ~17,400 | SELECT, INSERT, DELETE, CREATE TABLE/INDEX | Hobby project. Has SQL parser. |
| [Limbo](https://github.com/tursodatabase/limbo) | Rust | Very large | Full SQLite rewrite, async I/O, WASM | Company-backed, many contributors |
| sql.js | C→WASM | N/A | Full SQLite (Emscripten) | Not a reimplementation — transpiled C |
| modernc.org/sqlite | C→Go | Massive | Full SQLite (ccgo transpiler) | Machine-translated, not hand-written |

**Key observation:** No one has successfully hand-written a full SQLite replacement. The read-only implementations are small (~5K LOC) and practical. The read-write ones are either incomplete hobby projects or company-backed efforts with large teams.

---

## Risk Analysis

### Why SQLite Is Hard to Reimplement

SQLite's testing infrastructure is unmatched in open-source software:

- **Source:** ~155,800 lines of C
- **Test code:** ~92 million lines across 4 test harnesses (590:1 test-to-code ratio)
- **Branch coverage:** 100% (TH3 proprietary test suite)
- **MC/DC coverage:** 100%
- **Fuzzing:** ~1 billion mutations per day (dbsqlfuzz)
- **SQL Logic Test:** 7.2 million queries compared against PostgreSQL, MySQL, SQL Server, Oracle
- **Assert statements:** 6,754 in the source code

This testing has found and fixed thousands of corner cases over 20+ years: overflow page chains at page boundaries, concurrent reader/writer interactions, Unicode normalization, corrupt database recovery, integer overflow in arithmetic, and interactions between triggers, views, and foreign keys.

### Specific Risks for a Glyph Driver

| Risk | Severity | Mitigation |
|------|----------|------------|
| **Data corruption on write** | Critical | Glyph programs ARE their databases. A bug that corrupts a `.glyph` file destroys the program. |
| **Silent data loss** | Critical | Incorrect B-tree traversal could skip rows, causing missing definitions. |
| **Overflow page bugs** | High | Large definitions (~6KB) span multiple pages. Off-by-one errors in overflow chains corrupt the payload silently. |
| **WAL inconsistency** | High | Reading a database with an active WAL without WAL support returns stale data. |
| **B-tree balancing bugs** | High | Incorrect page splitting corrupts the tree structure, making all subsequent reads return wrong data. |
| **Encoding mismatches** | Medium | Incorrect serial type decoding produces garbage strings. |
| **Performance regression** | Low | Linear table scans instead of indexed lookups. Acceptable for compiler workloads (~2K rows). |

### The Fundamental Tradeoff

SQLite's robustness comes from its C implementation being battle-tested for two decades. A reimplementation trades that robustness for portability. This is not a good trade for a production compiler whose programs are databases. A subtle B-tree bug that manifests once in 10,000 builds would be nearly impossible to diagnose and could destroy user programs.

---

## Alternatives to a Full Driver

### Alternative 1: Pre-Export to Flat Format

Before targeting WASM, export the `.glyph` database to a flat format (JSON, binary, or the existing `src/` file tree) that the WASM-compiled compiler reads instead of SQLite.

```
glyph export program.glyph /tmp/flat/     # native binary, uses libsqlite3
glyph-wasm build /tmp/flat/ output.wasm    # WASM binary, reads flat files
```

**Pros:** Zero reimplementation risk. Works today with `glyph export`/`glyph import`.
**Cons:** Two-step workflow. Loses incrementality (no dirty tracking). Not really bootstrapping — the native compiler is still required for the export step.

### Alternative 2: Host-Provided SQLite Imports

The WASM module imports `db_open`, `db_exec`, etc. as host functions. A JavaScript host (Node.js/Deno) provides these via sql.js.

```javascript
const sqljs = await initSqlJs();
const wasmModule = await WebAssembly.instantiate(glyphWasm, {
    glyph_sqlite: {
        db_open: (pathPtr) => { /* open via sql.js */ },
        db_exec: (dbPtr, sqlPtr) => { /* execute via sql.js */ },
        db_query_rows: (dbPtr, sqlPtr) => { /* query via sql.js */ },
    }
});
```

**Pros:** Full SQLite compatibility via sql.js. No reimplementation risk.
**Cons:** Requires JavaScript host, not bare wasmtime. Not truly self-contained.

### Alternative 3: Compile-Time SQL Elimination

Redesign the compiler so that the compilation pipeline works on in-memory data structures, not SQL queries. The database is a storage format, not the execution model.

```
load_all_defs(file) -> [Def]       # one read pass, raw B-tree scan
build_dep_graph(defs) -> DepGraph   # computed in memory
find_dirty(graph) -> [Def]          # graph traversal, not recursive CTE
compile(dirty_defs) -> Result       # no SQL during compilation
write_results(file, results)        # one write pass
```

**Pros:** Eliminates SQL dependency entirely. The driver only needs raw B-tree read/write — Tier 1 + minimal Tier 3 (~2,000 LOC). All query logic (filtering, joining, dirty cascade) is pure Glyph code operating on arrays and maps. The recursive CTE becomes a graph traversal. The GROUP BY becomes a fold.

**Cons:** Significant refactoring of the compiler. 104 SELECT statements and 44 write statements would need to be replaced with in-memory equivalents.

### Alternative 4: WASI-Targeted SQLite via wasi-sdk

The SQLite project officially supports building with wasi-sdk (see [sqlite.org/wasm building docs, §6.7](https://sqlite.org/wasm/doc/trunk/building.md)). This compiles the real SQLite C implementation to WASM targeting WASI, producing a module that uses the same WASI syscalls (`fd_read`, `fd_write`, `path_open`) that Glyph's WASM backend already imports. The two modules are then merged into a single binary.

**Building SQLite with wasi-sdk (official method):**

```bash
# Install wasi-sdk (prebuilt binaries or build from source)
git clone --recursive https://github.com/WebAssembly/wasi-sdk.git
cd wasi-sdk && NINJA_FLAGS=-v make package
sudo ln -s $PWD/build/wasi-sdk-* /opt/wasi-sdk

# Build SQLite targeting WASI
cd sqlite
./configure --with-wasi-sdk=/opt/wasi-sdk
make
```

**Alternative: manual clang invocation with the amalgamation:**

```bash
/opt/wasi-sdk/bin/clang --target=wasm32-wasi -O2 \
  -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 \
  -o sqlite3.wasm sqlite3.c \
  -Wl,--export=sqlite3_open \
  -Wl,--export=sqlite3_close \
  -Wl,--export=sqlite3_exec \
  -Wl,--export=sqlite3_prepare_v2 \
  -Wl,--export=sqlite3_step \
  -Wl,--export=sqlite3_column_text \
  -Wl,--export=sqlite3_column_int64 \
  -Wl,--export=sqlite3_column_blob \
  -Wl,--export=sqlite3_column_bytes \
  -Wl,--export=sqlite3_finalize \
  -Wl,--export=sqlite3_errmsg \
  -Wl,--export=malloc \
  -Wl,--export=free
```

**Linking with Glyph's WASM output:**

```bash
# Compile Glyph's WAT output to WASM
wat2wasm glyph_out.wat -o glyph.wasm

# Merge both modules into one binary (Binaryen)
wasm-merge glyph.wasm main sqlite3.wasm sqlite \
  --rename-export-conflicts -o combined.wasm

# Run with WASI filesystem access
wasmtime --dir=. combined.wasm build program.glyph
```

The Glyph WASM backend would declare SQLite functions as imports:

```wat
(import "sqlite" "sqlite3_open" (func $sqlite3_open (param i32 i32) (result i32)))
(import "sqlite" "sqlite3_exec" (func $sqlite3_exec (param i32 i32 i32 i32 i32) (result i32)))
;; ... etc
```

After `wasm-merge`, these imports resolve to the real C SQLite functions compiled in the same binary. WASI provides the file I/O layer underneath. The result is the actual, battle-tested SQLite3 running inside WASM — no reimplementation, no JavaScript host, no custom driver.

**Pros:** Zero reimplementation risk — this is the real SQLite. Full read-write capability. Runs in bare wasmtime (no JS). Same file I/O layer (WASI) that Glyph already uses. The WASM compiler could genuinely bootstrap.

**Cons:** Requires `wasi-sdk` and `wasm-merge` (Binaryen) as build dependencies. Shared linear memory between Glyph's bump allocator and SQLite's `malloc` needs careful coordination (separate heap regions or use SQLite's exported `malloc`). The merged binary will be larger (~1-2MB for SQLite alone). String passing between Glyph's packed `{ptr, len}` representation and SQLite's null-terminated C strings requires conversion wrappers.

**Build dependencies:** [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) (clang + wasi-libc), [Binaryen](https://github.com/WebAssembly/binaryen) (`wasm-merge`), [wabt](https://github.com/WebAssembly/wabt) (`wat2wasm`), [wasmtime](https://wasmtime.dev/).

### Alternative 5: WASM Component Model (Future)

The WASM Component Model (in development) will allow composing multiple WASM modules with typed interfaces. A SQLite WASM component could be linked directly to a Glyph WASM component without a JavaScript host.

**Pros:** Clean architecture, real WASM-native SQLite.
**Cons:** The Component Model is not yet stable or widely supported. Not a solution today.

---

## Recommended Path

**If the goal is WASM bootstrap:**

Alternative 4 (WASI-targeted SQLite via wasi-sdk) is the strongest path. It uses the real, proven SQLite implementation compiled to the same WASI target as Glyph, merged into a single binary. Zero reimplementation risk, full read-write capability, runs in bare wasmtime. The engineering work is plumbing — import declarations, memory coordination, string conversion wrappers — not reimplementation.

**If the goal is a universal self-contained compiler (no C dependency at all):**

Alternative 3 (compile-time SQL elimination) combined with a Tier 1 read-only driver is the most sound approach. The driver is small (~500 LOC), the risk is contained (read-only — cannot corrupt data), and the compiler becomes genuinely portable. Write operations can use a separate, minimal serialization format for non-native targets. This is a larger architectural effort but eliminates all external dependencies.

**If the goal is practical WASM deployment of user programs:**

The WASM backend works today for programs that don't use SQLite. For programs that do, Alternative 2 (host-provided SQLite via sql.js) is pragmatic and carries zero reimplementation risk.

---

## Conclusion

The most practical path to WASM bootstrap is not reimplementing SQLite — it is compiling the real SQLite to the same WASI target using wasi-sdk and merging the modules. This approach carries zero correctness risk and requires only build tooling work.

A pure-Glyph SQLite3 driver is technically feasible but unnecessary for WASM bootstrap. The file format is well-documented and a read-only implementation is ~500 lines. A read-write implementation is ~4,000–6,000 lines but carries meaningful corruption risk. Such a driver would only be justified if the goal is eliminating all C dependencies entirely — a different and much larger ambition than WASM support.

SQLite3 remains one of the most robust and portable databases ever built. Any reimplementation should be approached with humility — not because the format is complex, but because the edge cases are subtle and the consequences of bugs are permanent.
