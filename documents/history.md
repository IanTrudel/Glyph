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

## A Note on Authorship

Every commit in the repository is authored by Ian Trudel. But the code itself — the 1,774 definitions in `glyph.glyph`, the libraries, the examples — was written by Claude, Anthropic's LLM. The CLAUDE.md file in the repository root, at over 15,000 words, serves as the institutional memory between sessions: what has been built, what broke and was fixed, what conventions to follow.

This is the premise made real. Glyph is not a language that happens to be usable by LLMs. It is a language that was *designed* for LLMs, *written* by an LLM, compiling *programs written by LLMs*, through a toolchain that speaks the LLM's native protocol. The human provides direction, taste, and judgment. The machine writes the code.

Whether this is a curiosity or a glimpse of something larger remains to be seen. But as a proof of concept — a self-hosting compiler with garbage collection, closures, pattern matching, polymorphic types, two codegen backends, a standard library, and 21 working programs, all built in 37 days — it is, at minimum, a remarkable artifact of its moment.

---

*Document compiled from 200 git commits, database queries against `glyph.glyph`, and project documentation. March 29, 2026.*
