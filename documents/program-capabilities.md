# Glyph Program Capabilities

What kinds of programs are currently possible to write in Glyph, given the self-hosted compiler, runtime, and FFI system.

## Data Types Available

- **Integers** (`I`/`Int64`): 64-bit signed. All arithmetic ops (`+`, `-`, `*`, `/`, `%`), comparisons, bitwise
- **Unsigned** (`U`/`UInt64`): 64-bit unsigned
- **Floats** (`F`/`Float64`): 64-bit floating point
- **Booleans** (`B`/`Bool`): `true`/`false`, `&&`/`||`/`!`
- **Strings** (`S`/`Str`): Fat pointer `{ptr, len}`, heap-allocated. Concatenation (`+`), equality (`==`/`!=`), interpolation (`"hello {name}"`), raw strings (`r"no\escapes"`)
- **Arrays** (`[T]`): Dynamic, heap-allocated `{ptr, len, cap}`. Push, index, bounds-checked reads
- **Records/Structs**: Anonymous `{x:I, y:I}` or named (gen=2 `typedef struct`). Row polymorphism supported in type checker
- **Enums**: Tagged unions `{tag, payload}`. Heap-allocated variants. Pattern matching with `match`
- **Optionals** (`?T`): Sugar over enums — `Some(v)` / `None`
- **Results** (`!T`): `ok(v)` / `err(v)` with `?` propagation
- **Closures**: Heap-allocated `{fn_ptr, captures...}`, first-class. Uniform calling convention
- **Void** (`V`), **Never** (`N`)

## Control Flow

- **Match expressions**: Pattern matching on ints, strings, bools, enum variants, or-patterns (`1 | 2 | 3 -> ...`), wildcard `_`
- **If/else**: Standard conditional branching
- **For loops**: Desugar to index-based MIR loops; typically iterate over arrays
- **Recursion**: Full support, including mutual recursion
- **Pipe operator** (`|>`): Function chaining
- **Error propagation** (`?`): Early return on error results

## I/O & System

- **Console**: `print`, `println`, `eprintln` (stdout/stderr)
- **File I/O**: `read_file`, `write_file`, `try_read_file`, `try_write_file` (Result-returning variants)
- **Command-line args**: `args()` returns `[S]`
- **System calls**: `glyph_system(cmd)` — runs shell commands, returns exit code
- **Process**: `exit(code)`

## SQLite (Built-in)

- `db_open`, `db_close`, `db_exec`, `db_query_rows`, `db_query_one`
- Programs can read/write SQLite databases natively — this is a first-class capability since Glyph programs *are* SQLite databases

## FFI / C Interop

- **Extern table**: Declare C functions with signatures, compiler generates wrappers automatically. Supports `-l` library linking flags
- **Manual FFI**: Write a C wrapper file with `long long` ABI, concatenate with generated C, call wrappers directly from Glyph. Used for heavier FFI (ncurses, X11)
- **Limitation**: Extern wrappers only see `<stdlib.h>`, `<stdio.h>`, `<string.h>`, `<signal.h>`. Anything else needs a separate `.c` file

## String Operations

- Concatenation, equality, length, slicing, char-at, int-to-string, string-to-int, string-to-cstr (for FFI)
- **StringBuilder**: `sb_new`, `sb_append`, `sb_build` — O(n) string building
- **String interpolation**: `"value is {expr}"` with arbitrary expressions inside braces

## Proven Program Categories (Existing Examples)

| Program | Category | Key Features Used |
|---------|----------|-------------------|
| **calc.glyph** | Interactive REPL | Expression parsing, `getchar`/`fflush` externs, recursion, string ops |
| **life.glyph** | GUI application | X11 via FFI, arrays, loops, system interaction |
| **glint.glyph** | CLI tool / analyzer | SQLite queries on `.glyph` databases, formatted output, args |
| **gled.glyph** | Terminal text editor | ncurses via C wrappers, mutable state, file I/O, keyboard handling (38k binary) |
| **gstats.glyph** | Statistical tool | Gen=2 named structs, `getenv` extern, arithmetic, formatted output |
| **glyph.glyph** | The compiler itself | ~797 definitions — tokenizer, parser, type checker, MIR lowering, C codegen, CLI dispatch, MCP server, JSON subsystem |

## Feasible but Not Yet Demonstrated

1. **Network clients/servers** — via FFI to socket APIs or `system()` calls to curl
2. **Text processing / data pipelines** — file I/O + string ops + SQLite storage
3. **Build systems / task runners** — `system()` + file I/O + args
4. **Code generators / transpilers** — string building + file output (the compiler itself is one)
5. **Database-backed CLI apps** — SQLite is native; CRUD apps are straightforward
6. **Simple games** (terminal-based) — ncurses FFI proven with gled
7. **JSON processing** — full JSON subsystem exists (tokenizer, parser, accessors, builder) from MCP server work
8. **MCP tool servers** — already implemented; stdio transport + JSON-RPC

## Current Limitations

| Limitation | Impact |
|------------|--------|
| **No garbage collector** | Manual memory management via alloc/dealloc. Long-running programs leak unless careful |
| **No maps/hashmaps** | `{K:V}` type exists in syntax but no runtime implementation. Use SQLite tables as key-value stores instead |
| **No concurrency** | Single-threaded only. No threads, async, or channels |
| **No standard library** | Only the C runtime builtins. No regex, no HTTP, no date/time (without FFI) |
| **No dynamic dispatch / traits** | Trait syntax exists but no runtime support. Monomorphic only |
| **No generics** | Type inference handles polymorphism but no user-defined generic types/functions |
| **String type tracking gap** | Two untyped function params with `+`/`==` fall through to integer ops |
| **No floating-point literals in self-hosted** | Float support exists in Rust compiler but self-hosted C codegen may not handle all float operations |
| **C keyword collision** | Function names like `double`, `int`, `float` cause codegen errors |
| **No module system at runtime** | Modules exist in schema but no cross-database imports |

## Sweet Spot

Glyph is currently best suited for: **CLI tools, compilers/transpilers, database utilities, code generators, and LLM-facing tool servers** — programs that are text-heavy, use SQLite, don't need GC or concurrency, and benefit from the "program as database" model. The FFI escape hatch extends reach to GUI (X11, ncurses) and system programming when needed.
