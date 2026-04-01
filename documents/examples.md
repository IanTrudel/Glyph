# Example Program Ideas

Programs that would stress-test Glyph's capabilities and expose fundamental limitations. Ordered by how much they'd reveal about what the language needs.

## Programs That Expose Fundamental Gaps

### 1. PNG Decoder

**What it does:** Read a PNG file, decompress it, decode the pixel data, output as PPM (or display via X11/SDL).

**Why it's hard:** Glyph has no binary data type. `read_file` returns a string (`{ptr, len}` fat pointer of text). There are no byte arrays, no binary file I/O, no way to read a file as raw bytes and index into individual bytes. PNG requires:

- Reading a binary file header (8 magic bytes)
- Parsing chunk structures (4-byte length, 4-byte type, variable data, 4-byte CRC)
- Decompressing zlib/DEFLATE streams (Huffman trees, bit-level manipulation)
- Filtering scan lines (byte-level arithmetic on adjacent pixels)
- CRC32 checksum computation (byte-by-byte with lookup table)

You'd have to encode bytes as integer arrays, which is absurd overhead — each byte becomes a 64-bit GVal. A 1MB PNG would need 8MB of integer array just to hold the raw data.

**What it would reveal:** The need for a `Bytes` type — a compact, indexable byte buffer with binary I/O. This is a fundamental data type that's completely missing from Glyph. It affects any program that deals with: file formats (PNG, ZIP, WAV, PDF, ELF), network protocols (HTTP/2, WebSocket, TLS), serialization (MessagePack, Protocol Buffers), or hardware interaction.

**Note on workarounds:** Integer arrays `[I]` can represent bytes (each element 0-255), and Glyph strings are `{ptr, len}` fat pointers that preserve null bytes, so `read_file` + `str_char_at` can technically read binary files. But the 8x memory overhead (64-bit GVal per byte) compounds across decode pipeline buffers, and the byte manipulation boilerplate dominates over the actual decoding logic.

~~**Prerequisite language features:**~~ DONE

- `Y` (Bytes) type — single-char alias following `I`/`S`/`B` convention. Compact byte buffer, 1 byte per element, not 8. `[Y]` for byte arrays.
- `read_bytes` / `write_bytes` runtime functions
- Byte indexing and slicing
- Bitwise operations on bytes (already have `bitand`/`bitor`/`bitxor`/`shl`/`shr` on integers)

**Real-world example:** The existing `sheet.glyph` spreadsheet could support `.xlsx` files (Excel format), but `.xlsx` is a ZIP archive containing XML files. This requires ZIP decompression (DEFLATE algorithm, binary headers, CRC32 checksums) — the same class of binary processing as PNG. Without `Y`, sheet is limited to CSV import/export.

### 2. SQL Database Engine (Toy)

**What it does:** A minimal SQL database: CREATE TABLE, INSERT, SELECT with WHERE, single-file storage. Not competing with SQLite — demonstrating that Glyph can implement structured storage.

**Why it's hard:** Database engines need precise control over memory layout:

- **Page-based storage** — data lives in fixed-size pages (e.g., 4096 bytes). Pages are the unit of I/O. Glyph can't express "these 4096 bytes are a page" and overlay structure on them.
- **B-tree implementation** — nodes contain keys and child pointers packed into pages. Requires pointer arithmetic, packed structs, and byte-level serialization.
- **Buffer pool** — a fixed number of pages cached in memory, with LRU eviction. Requires pre-allocated memory that the GC doesn't touch.
- **Binary row format** — rows are serialized as byte sequences with fixed-width fields. No way to write/read structured binary data.
- **Memory-mapped files** — `mmap` for efficient I/O. Not available in Glyph.

**What it would reveal:** The gap between Glyph's high-level value model (everything is a GC'd 64-bit value) and systems programming requirements (fixed-size buffers, layout control, zero-copy I/O). This is the same class of gap as PNG decoding but more extreme — a database engine needs to manage memory as a resource, not just consume it.

**Prerequisite language features:**
- `Bytes` type with fixed-size allocation
- Binary serialization/deserialization
- `mmap` or equivalent memory-mapped I/O
- Pinned memory (excluded from GC)

### 3. Multiplayer Game Server

**What it does:** Accept TCP connections from multiple clients, maintain shared game state (player positions, scores), broadcast updates at a fixed tick rate (e.g., 20 ticks/second).

**Why it's hard:** Concurrent shared mutable state with real-time constraints:

- **Multiple connections** — `async.glyph` provides an event loop with epoll, but it's single-threaded. True concurrency requires `thread.glyph`, but there are no atomic operations, no lock-free data structures, and no memory ordering guarantees.
- **Shared game state** — multiple threads reading/writing player positions. Without atomics or channels with proper semantics, this requires mutex-based synchronization that Glyph's thread library provides but with no deadlock prevention.
- **Binary protocol** — efficient client-server communication needs compact binary messages, not JSON strings. Back to the `Bytes` gap.
- **Tick scheduling** — process all inputs, update state, broadcast results within 50ms. GC pauses are unpredictable and can blow the deadline.
- **Connection lifecycle** — handling disconnects, timeouts, reconnects. Error propagation across thread boundaries.

**What it would reveal:** Glyph's concurrency story is thin. `thread.glyph` provides mutexes and thread spawning, but there's no channel/message-passing primitive, no atomic operations, no structured concurrency (spawn-and-join patterns). A real concurrent system exposes all of these gaps.

**Prerequisite language features:**
- Channels (typed, bounded, multi-producer/multi-consumer)
- Atomic integer operations
- Binary protocol support (`Bytes` type again)
- Timer/scheduling primitives

### 4. Audio Synthesizer

**What it does:** Generate audio waveforms (sine, square, sawtooth), mix them, apply effects (reverb, delay), output to speakers via ALSA/PulseAudio. Real-time audio processing.

**Why it's hard:** The hardest real-time constraint in userspace programming:

- **44,100 samples per second** — each sample must be computed and delivered on time. A single GC pause of >23ms causes an audible glitch (one buffer underrun).
- **Tight inner loops** — generating a buffer of 1024 samples means 1024 iterations of: read oscillator phase, compute sine/cosine, apply envelope, mix, write to output buffer. Every allocation in this loop risks a GC pause.
- **Float-heavy computation** — Glyph's float support works but every operation goes through `_glyph_i2f`/`_glyph_f2i` bitcast helpers. In a sample-generation loop running millions of times per second, this overhead matters.
- **Pre-allocated buffers** — audio engines allocate their buffers once and reuse them. Glyph's arrays grow dynamically via `array_push`. There's no "allocate a fixed-size array and fill it by index" without triggering GC.
- **No SIMD** — computing 4 or 8 samples in parallel via SSE/AVX is standard in audio. Glyph has no access to vector instructions.

**What it would reveal:** Glyph can't do real-time work. The GC is the fundamental barrier — you can't guarantee latency when the runtime can pause at any allocation. This isn't necessarily a gap to fill (most languages can't do real-time either), but it defines Glyph's ceiling.

**Prerequisite language features:**
- Pre-allocated fixed-size arrays (no GC interaction)
- Efficient float math (eliminate bitcast overhead)
- `Bytes`/buffer type for audio output
- FFI to ALSA/PulseAudio

## Programs Within Reach (Would Stress But Not Break Glyph)

### 5. HTTP/1.1 Server (From Scratch)

**What it does:** Accept TCP connections, parse HTTP requests, route to handlers, serve responses. Not using `network.glyph`/`web.glyph` — implementing the protocol from scratch.

**Why it's interesting:** HTTP/1.1 is a text-based protocol, so the `Bytes` gap is less critical (headers are ASCII, bodies can be treated as strings for text content). Would stress:

- **Concurrent connections** — `async.glyph` event loop or `thread.glyph` per-connection
- **Parser robustness** — HTTP parsing has many edge cases (chunked encoding, keep-alive, malformed requests)
- **Streaming responses** — serving large files without loading them entirely into memory (no streaming I/O in Glyph)
- **Timeout handling** — idle connections need cleanup

**Current feasibility:** Largely implementable. `network.glyph` already does most of this — reimplementing from scratch would validate the language primitives rather than the library.

### 6. Prolog Interpreter (Extended)

**What it does:** A fuller Prolog — unification, backtracking search, cut, assert/retract, arithmetic evaluation, list operations, and DCG (Definite Clause Grammars).

**Why it's interesting:** `examples/prolog/prolog.glyph` already exists (basic unification and backtracking). Extending it to a complete Prolog would stress:

- **Complex recursive data structures** — terms are deeply nested trees. Unification walks them recursively. Occurs-check prevents infinite types.
- **Backtracking/choice points** — need to save and restore state efficiently. Currently done via recursion (implicit stack). A trail-based approach needs mutable arrays of undo operations.
- **Dynamic predicates** — `assert`/`retract` modify the program at runtime. The database model is actually perfect for this — predicates could be stored as rows.
- **Performance** — Prolog search spaces grow exponentially. Naive interpretation is slow. WAM (Warren Abstract Machine) compilation is the standard optimization, but that's a compiler project inside a compiler project.

**Current feasibility:** Fully implementable. The basic interpreter exists. Extension to full Prolog is a matter of adding features, not hitting language limitations. Good candidate for demonstrating Glyph's strengths (recursive data, pattern matching, database storage of clauses).

### 7. Terminal Spreadsheet (Extended)

**What it does:** Extend `examples/sheet/sheet.glyph` with: formula evaluation (cell references, arithmetic, functions like SUM/AVG/COUNT), dependency tracking between cells, circular reference detection, CSV import/export, multi-sheet support.

**Why it's interesting:** The existing sheet has a bytecode VM for expression evaluation. Extending it tests:

- **Graph algorithms** — cell dependency tracking is a DAG. Circular reference detection is cycle detection. Topological sort determines evaluation order. All pure Glyph.
- **Incremental recomputation** — when a cell changes, only recompute cells that depend on it (transitive forward dependencies). Same pattern as Glyph's own incremental compilation.
- **String formatting** — column alignment, number formatting, truncation for display. Tests string manipulation at scale.

**Current feasibility:** Fully implementable. Probably the best "next example" — builds on existing code, tests real algorithmic patterns, and the result is a useful tool.

### 8. Static Site Generator

**What it does:** Read Markdown files from a directory, apply HTML templates, generate a static website. Includes: template engine with variable substitution, Markdown parsing (#5), file system traversal, asset copying.

**Why it's interesting:** End-to-end file processing pipeline:

- **Directory traversal** — needs `readdir` (proposed in #43.3 os.glyph)
- **Template engine** — string interpolation on steroids. Parse `{{variable}}` and `{% for item in list %}` constructs. Tests parser writing and string manipulation.
- **File processing pipeline** — read → parse → transform → write for each file. Functional pipeline pattern at the file level.
- **Configuration** — read a TOML/JSON config file for site metadata

**Current feasibility:** ~80% implementable. Needs `readdir` for directory traversal (requires os.glyph or a small FFI addition). Everything else works today.

### 9. Monticello-Style Version Control (`gmc`)

**What it does:** Definition-level version control native to Glyph's database model. Named after Squeak Smalltalk's Monticello system, which versions code at the method/class granularity rather than the file/line granularity. `gmc` would do the same for Glyph definitions — snapshot, diff, merge, branch, and share `.glyph` programs at the definition level.

**Why this is the right VCS for Glyph:** Git treats programs as bags of text files and diffs as line insertions/deletions. This is a fundamental mismatch for Glyph — a `.glyph` file is a SQLite database, and `git diff` shows `Binary files differ`. The `glyph export` workaround (exporting to text files for git tracking) fights the model instead of embracing it. Monticello showed that when your code lives in a structured store (Smalltalk image / SQLite database), version control should operate on the same structure — definitions, not lines.

**Core concepts:**

- **Snapshot** — a frozen copy of a set of definitions at a point in time. Stored as a row in a `snapshot` table: id, timestamp, description, parent snapshot id(s). Each snapshot records the content hash of every included definition (the `hash` column already exists on `def`).
- **Ancestry** — snapshots form a DAG via parent pointers, exactly like git commits. A branch is just a named pointer to a snapshot.
- **Diff** — compare two snapshots by diffing their definition hash sets. Result: added definitions, removed definitions, modified definitions (same name, different hash). For modified definitions, diff the bodies at the text level. This is far more meaningful than line-based diff — you see "function `parse_atom` changed" not "lines 4027-4053 of glyph_out.c changed."
- **Merge** — combine two snapshots that diverged from a common ancestor. Definitions touched by only one side apply cleanly. Definitions touched by both sides are conflicts — presented at the definition level (ancestor body vs. side A body vs. side B body), not at the line level. The dependency graph (`dep` table) can warn when a merge would break callers.
- **Package** — a named subset of definitions (by namespace, by explicit list, or by dependency closure from a root function). Packages are the unit of sharing — you publish a package, not the whole database. Analogous to Monticello's MCZ packages.

**Data model:**

```sql
-- Snapshots (like git commits)
CREATE TABLE snapshot (
  id INTEGER PRIMARY KEY,
  description TEXT,
  timestamp INTEGER,          -- Unix epoch
  parent1 INTEGER,            -- first parent snapshot
  parent2 INTEGER             -- second parent (for merges), NULL otherwise
);

-- Snapshot contents (which definitions at which version)
CREATE TABLE snapshot_def (
  snapshot_id INTEGER,
  name TEXT,
  kind TEXT,
  hash BLOB,                  -- BLAKE3 hash of the body at snapshot time
  body TEXT,                   -- frozen body text
  PRIMARY KEY (snapshot_id, name, kind)
);

-- Branches (named pointers to snapshots)
CREATE TABLE branch (
  name TEXT PRIMARY KEY,
  snapshot_id INTEGER
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

All of this lives inside the `.glyph` file itself — the version history is part of the program, not in a separate `.git` directory. This is the Smalltalk philosophy: the image contains its own history.

**Commands:**

```bash
gmc snapshot app.glyph "description"          # create a snapshot of current state
gmc log app.glyph                             # show snapshot history
gmc diff app.glyph                            # diff working state vs last snapshot
gmc diff app.glyph snap1 snap2                # diff two snapshots
gmc branch app.glyph feature-x                # create a branch
gmc checkout app.glyph feature-x              # switch to a branch (restore its snapshot)
gmc merge app.glyph feature-x                 # merge branch into current
gmc package app.glyph create pkg-name ns      # create a package from a namespace
gmc package app.glyph export pkg-name out.mcz # export package as standalone file
gmc package app.glyph import pkg.mcz          # import package definitions
```

**Diff example:**

```
Snapshot 12 → 13 (2 modified, 1 added, 1 removed)

  modified: fn parse_atom (tokens: 84 → 91)
    - match tok_kind(t)
    -   1 -> parse_int(t)
    + match tok_kind(t)
    +   1 -> parse_int(t)
    +   7 -> parse_char_lit(t)

  modified: fn tok_one (tokens: 203 → 210)
    + 39 -> mk_tok(tk_char, pos, pos + 2)

  added: fn parse_char_lit (tokens: 24)

  removed: fn old_helper (was 15 tokens)
```

**Merge conflict example:**

```
Conflict in fn dispatch_cmd (both sides modified):

  ancestor:
    dispatch_cmd args = match args[0]
      "build" -> cmd_build(args)
      "test" -> cmd_test(args)

  branch main:
    dispatch_cmd args = match args[0]
      "build" -> cmd_build(args)
      "test" -> cmd_test(args)
      "run" -> cmd_run(args)

  branch feature-x:
    dispatch_cmd args = match args[0]
      "build" -> cmd_build(args)
      "test" -> cmd_test(args)
      "check" -> cmd_check(args)

  Resolution: accept main / accept feature-x / edit manually
```

**Why this plays to Glyph's strengths:**

1. **The `hash` column already exists** — every definition has a BLAKE3 content hash. Snapshot creation is a single `SELECT name, kind, hash, body FROM def` — the database already knows the state.
2. **The `dep` table already exists** — merge conflict analysis can check whether a changed definition breaks its callers. No other VCS can do this because no other VCS knows the call graph.
3. **The `def_history` table already exists** — automatic change tracking via triggers. `gmc` extends this from per-definition history to whole-program snapshots.
4. **SQLite is the storage layer** — no need for a custom object store, packfile format, or index. Everything is SQL queries on tables in the same database.
5. **Packages map to namespaces** — the `ns` column on `def` naturally partitions definitions into shareable units.

**What Monticello got right that git doesn't (for this model):**

- **Semantic units** — Monticello versions methods and classes, not files. `gmc` versions definitions, not lines. The diff is always meaningful.
- **Image-centric** — the version history lives inside the image (database), not in a parallel directory. The program carries its own history.
- **Package-centric sharing** — you share a curated subset (a package), not the whole repository. Libraries are packages extracted from one database and imported into another — which is exactly what `glyph link` already does, minus the version tracking.

**What `gmc` adds over Monticello:**

- **SQL-powered queries** — "show me all definitions that changed between snapshots 5 and 12 in the `parser` namespace" is a single SQL query joining `snapshot_def` with itself.
- **Dependency-aware merging** — the `dep` table lets the merge algorithm warn when a definition change breaks a caller that the other branch didn't update.
- **Content-addressed deduplication** — definitions with the same hash across snapshots share storage (the body is identical). A thousand snapshots where 90% of definitions didn't change stores only the 10% that did (via `snapshot_def` referencing hash-identical bodies).

**Implementation as a Glyph program:**

`gmc` itself would be a Glyph program (`examples/gmc/gmc.glyph`) — a definition-level VCS written in the language it versions. It uses SQLite externs to read/write the snapshot tables in the target `.glyph` file. Estimated ~80-120 definitions:

- ~15 for snapshot creation/restore
- ~15 for diff computation and display
- ~20 for merge (three-way diff, conflict detection, resolution)
- ~15 for branch management
- ~15 for package creation/export/import
- ~10 for CLI dispatch and formatting
- ~10 for history display and queries

**Current feasibility:** Fully implementable today. All required primitives exist: SQLite access (via externs), string comparison, hash-based diffing, array manipulation for definition sets. No new language features needed — this is a pure application-level project that exercises Glyph's database-native strengths.

**Connection to next-steps.md:** This subsumes or enables several proposed features:
- #27 (definition-level diffing) — `gmc diff` is this, with full snapshot history
- #11 (package manager) — packages are `gmc`'s sharing unit
- #42 (multi-agent concurrency) — `gmc merge` provides the conflict resolution protocol for concurrent agents working on the same database
