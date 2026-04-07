# A History of Glyph

*A programming language written by an LLM, for LLMs.*

---

## Genesis (February 21, 2026)

Glyph began with a single commit on the morning of February 21, 2026. Ian Trudel pushed 9,923 lines of Rust across 47 files — six crates forming a complete compiler pipeline: database layer, parser, type checker, MIR lowering, Cranelift codegen, and a CLI. The language specification came bundled in that same commit. Glyph was not sketched incrementally; it arrived nearly whole.

The founding premise was radical: **programs are SQLite databases, not source files.** A `.glyph` file is a database. The unit of source code is the *definition* — a named, hashed row in a SQL table. There are no source files to open, no imports to resolve, no directories to organize. An LLM reads context with `SELECT` and writes code with `INSERT`. The entire module system is replaced by queries.

The second premise was equally unusual: **no humans write Glyph.** The syntax was designed from first principles to minimize BPE token count — the currency of LLM computation. Single-character type aliases (`I` for Int64, `S` for Str, `B` for Bool), no semicolons, no braces, no `return` or `let` or `import` keywords. Every construct chosen to be cheap to generate and cheap to read — by a machine.

## The Self-Hosting Sprint (February 21-22)

What happened next was extraordinary. Within 15 hours of the initial commit, the compiler had bootstrapped itself.

- **4:46 AM** — Initial Rust compiler committed (6 crates, ~10k lines)
- **7:41 PM** — Self-hosted compiler implemented (Phases A-C: tokenizer, parser, type system, MIR, C codegen — all written *in Glyph*, stored in `glyph.glyph`)
- **8:27 PM** — Error diagnostics, raw strings, enum fixes, string builder added
- **9:12 PM** — Self-hosted CLI with 16 commands operational
- **9:52 PM** — Unit testing framework running
- **3:52 AM (Feb 22)** — Three-stage bootstrap chain complete and building with Ninja

By dawn on February 22, Glyph could compile itself. The bootstrap chain — Rust compiler builds the Glyph compiler, which rebuilds itself, which rebuilds itself again — was working. The first example programs appeared that same day: a calculator REPL and Conway's Game of Life with X11 graphics.

## The Feature Torrent (February 22 - March 3)

The weeks that followed were a sustained burst of capability expansion. The pace was relentless — 10 commits on February 23 alone, covering string interpolation, null pointer safety, MIR debugging, Result types with `?` propagation, tail-call optimization, definition history/undo, and zero-arg function auto-calling. Most features were implemented in both the Rust bootstrap compiler and the self-hosted compiler simultaneously.

Key additions in this period:

- **Closures** — Heap-allocated closure environments with lambda lifting and free-variable capture analysis. A uniform calling convention passes the closure pointer as a hidden first argument.
- **Match guards** (`pat ? guard -> body`) — Pattern matching with boolean guard expressions, falling through to the next arm on failure.
- **Or-patterns** (`1 | 2 | 3 -> body`) — Multiple patterns sharing a single arm body.
- **Let destructuring** (`{x, y} = expr`) — Record field extraction at the binding level.
- **Bitwise operators** — `bitand`, `bitor`, `bitxor`, `shl`, `shr` as keywords (avoiding symbol collision with existing operators).
- **Float support** — Full floating-point arithmetic with `int_to_float`/`float_to_int` coercion and bitcast helpers.
- **Let-polymorphism** — Standard Hindley-Milner generalization with `forall` quantification in the self-hosted type checker.
- **C runtime hardening** — Bounds checks, SIGSEGV/SIGFPE signal handlers, null protection, stack traces with function names.
- **Code coverage** — Function-level instrumentation via `#ifdef GLYPH_COVERAGE`, TSV output, and a `glyph cover` report command.
- **MCP server** — A Model Context Protocol server (`glyph mcp`) giving Claude structured tool access: read, write, search, type-check, build, and run — all through JSON-RPC over stdio.

The example programs grew alongside: `gled` (a terminal text editor with ncurses), `glint` (a project analyzer using SQLite FFI), and `gstats` (a statistical tool — the first program to use gen=2 named record types with C `typedef struct`).

## The Database as Architecture

Perhaps the most distinctive feature of Glyph is what it *doesn't* have. There are no files to organize, no directories to navigate, no import graphs to untangle. The program *is* the database schema:

- **`def`** — Every definition (function, type, test, const) lives as a row, with its source body, type signature, content hash, and token count.
- **`dep`** — A dependency graph tracked as edges between definitions.
- **`extern_`** — Foreign function declarations for C ABI interop.
- **`def_history`** — Automatic change tracking via SQLite triggers.

Incremental compilation falls out naturally: hash each definition (BLAKE3), recompile only dirty defs and their transitive dependents. The `v_dirty` view is a single SQL query. The entire dependency analysis that other compilers implement as complex graph algorithms is just a recursive CTE.

The compiler itself — all 1,774 definitions of it — lives in `glyph.glyph`, a 3.5 MB SQLite database containing approximately 192,000 tokens of source code.

## The LLVM Backend and Namespace System (March 13-18)

After a quiet period in early March, a new wave of infrastructure work landed. March 13 was the single most active day in the project's history (22 commits), focused on making Glyph presentable to the outside world: export/import for git-friendly representation, README writing, the hello/fibonacci/countdown examples, and MCP tooling polish.

On March 18, two major features landed in a single commit:

- **LLVM IR backend** — A text-based LLVM IR emitter (`--emit=llvm`) with ~77 dedicated `ll_*` definitions. This gave Glyph access to LLVM's optimization passes, producing significantly faster binaries. The bootstrap chain grew to four stages: Rust/Cranelift -> Cranelift binary -> C codegen binary -> LLVM-compiled final binary.

- **Namespace system** — A `ns` column on the `def` table, auto-derived from name prefixes (`cg_` -> "codegen", `tc_` -> "typeck", `parse_` -> "parser", etc.). Not a module system in the traditional sense — more like a database index over naming conventions.

The same day saw the `glyph link` command for library composition and the map type `{K:V}` with an open-addressing hashmap runtime.

## Libraries and the Ecosystem (March 18-26)

With the compiler stable and the toolchain mature, attention turned to building an ecosystem. Nine libraries emerged:

| Library | Purpose |
|---------|---------|
| **stdlib.glyph** | Higher-order functions (`map`, `filter`, `fold`, `zip`), string utilities, array manipulation |
| **json.glyph** | JSON parsing and generation |
| **network.glyph** | TCP/UDP sockets, HTTP client/server |
| **web.glyph** | Web framework with routing and middleware |
| **gtk.glyph** | GTK4 bindings (windows, forms, rich widgets, dialogs) |
| **x11.glyph** | X11 window management and graphics |
| **async.glyph** | Coroutines, channels, and epoll-based event loop |
| **thread.glyph** | POSIX threading with mutexes and condition variables |
| **scan.glyph** | Icon-style string scanning with combinator API |

Each library is itself a `.glyph` database, linked into applications via `glyph use`. The FFI system generates C wrappers automatically from extern declarations — programs needing GTK4 or X11 just declare their foreign functions and the compiler handles the rest.

The example portfolio exploded. By late March, 21 programs demonstrated the language's range:

- **Asteroids** — A full arcade game with X11 graphics
- **Vulkan triangle** — GPU rendering via the Vulkan API
- **GTK calculator** — Desktop GUI application
- **Window manager** — An X11 tiling window manager
- **Web API** — REST server with CRUD operations
- **Prolog interpreter** — A logic programming language implementation
- **Sheet** — A command-line spreadsheet with a bytecode VM
- **Pipeline** — Producer-consumer threading example

## Performance Work (March 24-28)

As the compiler grew to handle larger programs, performance became a concern. Three targeted optimizations landed:

1. **3.3x faster type checking** — Bitset-based free variable collection replaced naive list traversal in the type checker.
2. **O(1) hashmap** — Open addressing with FNV-1a hashing replaced linear-scan dispatch tables in the compiler itself.
3. **Frozen hashmap dispatch** — String-based dispatch chains (matching on definition kinds, command names, etc.) were replaced with compile-time frozen hashmaps, eliminating sequential string comparisons.

On March 24, **Boehm GC integration** landed — all `malloc`/`realloc`/`free` calls redirected to the garbage collector via preprocessor macros, eliminating manual memory management from Glyph programs entirely.

## The Monomorphization Milestone (March 28 — v0.5.0)

The largest single feature effort culminated on March 28 with the release of v0.5.0. Monomorphization — specializing polymorphic functions for their concrete type arguments — was implemented across both the C codegen and LLVM IR backends.

The six-phase plan executed over a single day:
1. Typed local variables in generated C
2. Typed function parameters
3. Typed return values
4. Enum typedef generation
5. The monomorphization pass itself
6. Removal of the `-w` (suppress warnings) flag — the true test

That last phase proved the hardest. Removing `-w` surfaced every latent type cast boundary in the generated C code. All 23 example programs had to build clean, and all 463 tests had to pass. They did.

## The Language Today (March 29, 2026)

Thirty-seven days after its first commit, Glyph stands at:

- **1,774 definitions** in the self-hosted compiler (1,399 functions, 360 tests, 13 types, 2 constants)
- **~192,000 tokens** of source code in `glyph.glyph`
- **~10,659 lines** of Rust across 6 bootstrap crates
- **9 libraries**, **21 example programs**
- **23 foreign function declarations** in the compiler
- **18 MCP tools** for LLM interaction
- **200 commits** across 24 active development days
- **4-stage bootstrap chain**: Rust/Cranelift -> Cranelift binary -> C codegen -> LLVM final
- Releases from v0.2.0 through v0.5.1

The compiler pipeline flows: `.glyph` database -> SQL query -> tokenizer -> parser -> Hindley-Milner type inference with row polymorphism -> MIR lowering -> C codegen (or LLVM IR) -> native executable.

## Libraries, Regex, and Property Testing (March 29)

March 29 marked a turn from infrastructure to expressiveness. The standard library gained higher-order utilities, the scan library acquired combinator parsing and float support, and a new **regex library** appeared — a Thompson NFA engine with Pike's VM execution, supporting character classes, quantifiers, alternation, and grouping across 44 functions and 15 tests. All pure Glyph, no FFI.

The same day brought a more radical addition: **property-based testing as a first-class definition kind.** A `prop` definition takes a seed integer, returns a boolean, and the test harness runs it for N trials with xorshift64-advancing seeds. The stdlib provided generators and shrinkers; migration #10 (`add_prop_kind`) was applied to every `.glyph` database in the repository. With `kind='prop'` alongside `kind='fn'` and `kind='test'`, Glyph now had three categories of executable definition, all stored as rows in the same table.

## The Seventeen-Commit Day (March 30)

March 30 rivaled March 13 for sheer density — 17 commits touching nearly every layer of the system. The work fell into three currents.

**Language refinements.** Pattern match **exhaustiveness checking** landed: 11 new `exhaust_*` definitions that warn at compile time when a match expression fails to cover all constructors, booleans, or lacks a wildcard catch-all. Guarded arms are correctly excluded (guards can fail). **Const expression evaluation** followed — `kind='const'` definitions are now evaluated as expressions, not just stored as strings. **Negative number literals** replaced the `0 - N` idiom that had been used throughout the codebase. **Char literals** (`#x` syntax: `#a` = 97, `#\n` = 10) gave the language a way to express ASCII values without magic numbers; a follow-up commit replaced every magic integer in the compiler with the new syntax.

**Type system deepening.** A **two-phase type inference** pass was introduced to handle polymorphic functions correctly. The first "warm" pass pre-infers all functions with aggressive generalization (all free variables become universal), then resets the union-find substitution to prevent cross-function type contamination. The real pass re-infers with clean substitution and correct callee types. This eliminated cascading type errors in programs like the Vie vector editor (10 `tc_err` warnings to zero).

**Tooling and applications.** The **Cairo library** (2D graphics bindings) and 18 new GTK4 FFI wrappers enabled the **Vie vector illustration editor** — a GTK4 application with path and shape editing, selection, pan/zoom, fill/stroke properties, and undo/redo. The **XML and SVG libraries** followed the same day, with SVG export integrated into Vie. Two new MCP tools rounded out the day: **bulk `get_defs`** for fetching multiple definitions in a single call, and **per-definition `emit`** for inspecting generated C or LLVM IR for individual functions.

The library count rose to 15. The example count, with the new GTK applications, reached 25.

## The Type System Crisis (March 31)

March 31 was a day of reckoning with the type checker. Four commits, each fixing a deeper manifestation of the same underlying problem: **cyclic type graphs.**

It began with float types silently losing their identity when passed through monomorphized call chains. The fix — propagating float types through monomorphization — was straightforward. But achieving zero `tc_err` warnings across the compiler, all examples, and all libraries exposed a harder issue. The Prolog interpreter, with its deeply recursive data structures, sent the type checker into infinite loops.

The root cause was `tc_collect_fv` and `inst_type` — functions that walk type graphs to collect free variables or instantiate polymorphic types. When the type graph contained cycles (a type variable unified with a structure containing itself), these walks never terminated. The fix required **cycle detection via visited-set bitsets** — first in `tc_collect_fv` and `inst_type`, then extended to `subst_resolve` and `ty_contains_var` when the Prolog interpreter found new cycle paths. The cycle detection itself had to be optimized (pool-length snapshots, variable-exempt visited sets) to avoid quadratic overhead on the 1,500+ function compiler. BUG-008, as it was catalogued, was the deepest type system bug to date and would resurface twice more.

The same day saw the LLVM backend's hex escape handling fixed and its auto-declaration system completed — finally enabling a full four-stage bootstrap through LLVM without manual intervention.

## Hardening and New Frontiers (April 1)

April 1 opened with the **compiler smoke test suite** — 69 white-box tests using `glyph use glyph.glyph` to import the compiler itself as a library and stress-test every stage: tokenizer, parser, type checker, MIR lowering, C codegen, LLVM backend, and end-to-end pipelines. The suite immediately found three new bugs (BUG-009 through BUG-011), which were fixed in the same session.

Three **monomorphization performance optimizations** followed in quick succession: hash map lookups replaced linear scans in `mono_fn_ty_idx`, `is_za_fn`, and `mono_find_spec_caller`. These were the kind of fixes that only mattered at scale — and the compiler, now at 1,900+ definitions, was at scale.

The day's feature work was ambitious. The **Bytes type** (`Y`) added compact 1-byte-per-element storage — the same `{ptr, len, cap}` header as arrays, but with `unsigned char` elements instead of 8-byte `GVal` words. An 8x memory savings for binary data, with a full runtime (`bytes_new`, `bytes_get`, `bytes_push`, `bytes_slice`, `read_bytes`, `write_bytes`). This immediately enabled the **zip library** — 72 definitions implementing DEFLATE decompression from first principles: bitstream reading, Huffman tree construction, fixed and dynamic block types, length/distance back-references, and CRC32 verification. Pure Glyph, no zlib dependency.

**Typed MIR** was the most architecturally significant change of the day. Previously, the MIR (mid-level intermediate representation) carried no struct type information — the C codegen guessed which record type a value belonged to by counting field accesses and picking the best match. This heuristic broke when multiple types shared field names. Typed MIR threaded struct type names from two sources — the MIR lowering pass (which knows the record literal's type) and the type checker (which knows the inferred type of every expression) — into the codegen, eliminating an entire class of field-offset ambiguity bugs. A companion feature, **type annotation syntax** (`TypeName @ expr`), gave programmers an explicit escape hatch for the remaining edge cases.

The day closed with three new libraries — **diff** (Myers algorithm), **sqlite3** (bindings for the database engine), and **gmc** (Glyph Monticello, a definition-level version control system) — setting the stage for the next day's integration.

## Monticello and the Compiler as Platform (April 2)

The most conceptually striking work of the entire project landed on April 2. **GMC — Glyph Monticello** — was integrated into the self-hosted compiler itself, giving Glyph a built-in version control system for definitions.

The name was borrowed from Monticello, the version control system for Smalltalk's Squeak environment, and the parallels were intentional. In Smalltalk, the unit of versioning is the method. In Glyph, it is the definition. GMC snapshots capture the complete state of every definition in a `.glyph` database — body, kind, type signature, hash — and store them in dedicated SQLite tables. Diffs are computed at the definition level, not the line level: a function was added, modified, or removed.

The integration required **transitive dependency resolution** — a new `expand_lib_deps` system that follows the `use` table across libraries, so a program depending on `glyph.glyph` automatically inherits its chain of library dependencies (`json.glyph`, `diff.glyph`, `sqlite3.glyph`, `gmc.glyph`). Cross-library deduplication prevents duplicate symbols when the same function appears in multiple libraries.

Five new **MCP tools** exposed GMC to LLMs: `snapshot` (create a named checkpoint), `log` (list snapshots), `diff` (show changes between working copy and any snapshot), `show` (inspect a snapshot's metadata), and `restore` (roll back to a previous state). An LLM working on a Glyph program could now checkpoint its work, examine what it had changed, and roll back mistakes — without touching git, without leaving the MCP protocol.

A **zlib compression library** (C FFI wrappers) joined the pure-Glyph zip library from the previous day, completing the binary data story.

## The Memory Wall (April 2)

The same day brought two critical performance fixes that together resolved Glyph's most pressing scaling problem.

The **type checker optimization** delivered a 36% faster bootstrap (5.8 seconds to 3.7 seconds). Full path compression with union-by-rank in the substitution map, early returns when resolved types have no free variables, and direct `HashSet` walks in `collect_env_free_vars` — each individually modest, collectively transformative. A purge of 2,775 accumulated `def_history` rows from `glyph.glyph` (1.9 MB of dead weight) brought the database from 4.3 MB back to 1.4 MB.

The **C codegen StringBuilder conversion** was more dramatic. The code generator had been building its output — 182,543 lines of C — via recursive string concatenation: each function's code appended to the next with `fn + rest`. Across 2,670 functions, this created an O(n²) allocation pattern that produced approximately 14.9 GB of intermediate string objects, overwhelming the Boehm garbage collector. The fix was to switch to the same `sb_new` / `sb_append` / `sb_build` pattern already used by the LLVM codegen path. Peak memory dropped from 14.9 GB to 1.8 GB. The generated C output was byte-identical — a zero-diff transformation.

This fix was satisfying for a deeper reason. The StringBuilder had been one of Glyph's earliest features, added on the evening of February 21 during the initial self-hosting sprint. It was designed precisely for this use case — O(n) string assembly — but the C codegen, written in the heat of Phase C, had never adopted it. Six weeks later, the tool the language provided for itself finally caught up with the code that needed it.

## The Language at Forty-Two Days (April 3, 2026)

Forty-two days and 265 commits after its first line of code, Glyph stands at:

- **1,955 definitions** in the self-hosted compiler (1,528 functions, 407 tests, 5 property tests, 13 types, 2 constants)
- **~232,000 tokens** of source code in `glyph.glyph`
- **~10,980 lines** of Rust across 6 bootstrap crates
- **19 libraries**: stdlib, scan, regex, json, network, web, gtk, x11, cairo, xml, svg, async, thread, diff, sqlite3, gmc, zip, zlib, math
- **25 example programs** (including 5 GTK applications)
- **23 foreign function declarations** in the compiler
- **23 MCP tools** for LLM interaction (including 5 GMC version control tools)
- **4-stage bootstrap chain**: Rust/Cranelift → Cranelift binary → C codegen → LLVM final
- **69 smoke tests** plus 407 unit tests and 5 property tests
- Releases from v0.2.0 through v0.6.1 across 28 active development days

The compiler pipeline remains: `.glyph` database → SQL query → tokenizer → parser → Hindley-Milner type inference → MIR lowering → monomorphization → C codegen (or LLVM IR) → native executable. But the compiler is no longer just a compiler. With GMC integration, it is a versioned development environment. With the MCP server's 23 tools, it is a platform that speaks the LLM's native protocol. With the smoke test suite importing the compiler as a library, it tests itself with itself.

## A Note on Authorship

Every commit in the repository is authored by Ian Trudel. But the code itself — the 1,955 definitions in `glyph.glyph`, the 19 libraries, the examples — was written by Claude, Anthropic's LLM. The CLAUDE.md file in the repository root, at over 15,000 words, serves as the institutional memory between sessions: what has been built, what broke and was fixed, what conventions to follow.

This is the premise made real. Glyph is not a language that happens to be usable by LLMs. It is a language that was *designed* for LLMs, *written* by an LLM, compiling *programs written by LLMs*, through a toolchain that speaks the LLM's native protocol. The human provides direction, taste, and judgment. The machine writes the code.

Whether this is a curiosity or a glimpse of something larger remains to be seen. But as a proof of concept — a self-hosting compiler with garbage collection, closures, pattern matching, polymorphic types, two codegen backends, 19 libraries, a built-in version control system, and 25 working programs, all built in 42 days — it is, at minimum, a remarkable artifact of its moment.

---

*Document compiled from 265 git commits, database queries against `glyph.glyph`, and project documentation. April 3, 2026.*
