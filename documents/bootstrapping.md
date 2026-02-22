# Bootstrapping the Glyph Compiler

## Overview

Glyph has a self-hosted compiler: a compiler written in Glyph that can compile itself. The compiler source lives in `glyph.glyph` (a SQLite database with 553 definitions), and it produces a 153k native binary via C code generation.

Bootstrapping is the process of building this self-hosted compiler from source. Since you need a Glyph compiler to compile Glyph, a Rust-based bootstrap compiler bridges the gap.

## The Bootstrap Chain

```
Stage 0:  cargo build --release  →  glyph0  (12M, Rust/Cranelift)
Stage 1:  glyph0 build glyph.glyph --full  →  glyph1  (186k, Cranelift-linked)
Stage 2:  glyph1 build glyph.glyph glyph   →  glyph   (153k, C-codegen)
```

**Stage 0** compiles the Rust codebase into `glyph0`. This is a full compiler with a Cranelift backend — it can parse, type-check, lower to MIR, and generate native code via Cranelift JIT.

**Stage 1** uses `glyph0` to compile all 553 definitions in `glyph.glyph`. The `--full` flag recomputes content hashes, token counts, and dependency edges, then compiles everything via Cranelift and links a native binary. We rename this to `glyph1` to avoid clobbering the final binary.

**Stage 2** uses `glyph1` (now a working Glyph compiler) to rebuild itself. `glyph1` reads the definition bodies from `glyph.glyph`, compiles each one through the self-hosted pipeline (tokenize → parse → type-check → MIR lower → C codegen), concatenates the output into a single C file, and invokes `cc` to produce the final `glyph` binary.

The result is a fixed point: `glyph` can rebuild itself from `glyph.glyph` and produce an identical binary.

## Build System

The bootstrap chain is encoded in `build.ninja`:

```bash
ninja              # default: full 3-stage bootstrap
ninja bootstrap    # alias for default
ninja test         # run Rust tests (68) + self-hosted tests (14)
ninja dump         # regenerate glyph.sql from glyph.glyph
ninja -t clean     # remove glyph0, glyph1, glyph, glyph.sql
```

After `ninja -t clean`, a bare `ninja` rebuilds everything from scratch.

## Binary Sizes

| Binary | Size | Backend | Description |
|--------|------|---------|-------------|
| `glyph0` | 12M | Rust + Cranelift runtime | Full Rust compiler, large due to Cranelift codegen framework |
| `glyph1` | 186k | Cranelift native code | All 553 Glyph definitions compiled to native x86-64 |
| `glyph` | 153k | GCC-compiled C | Same definitions, but GCC's optimizer produces 15% less code |
| `glyph.glyph` | 332k | — | The source database (SQLite, 553 definitions) |

`glyph` is smaller than `glyph1` despite identical functionality because GCC (with `-O`) optimizes better than Cranelift (which prioritizes compilation speed over output quality).

## Two Codegen Backends

The Rust compiler has a **Cranelift backend** (in `crates/glyph-codegen/`). The self-hosted compiler has a **C codegen backend** (definitions in `glyph.glyph`). They produce equivalent results but differ in implementation:

| Aspect | Cranelift (Rust) | C Codegen (Self-hosted) |
|--------|-----------------|------------------------|
| String escape processing | Rust lexer converts `\n` → byte 0x0A at scan time | Raw text preserved; `process_escapes` converts before C emission |
| Record field ordering | BTreeMap — always alphabetical | Explicit `alpha_rank` sort at aggregate construction |
| Field offset resolution | Type system provides offsets via `Rvalue::Field` | Post-processing pass: type registry + per-local pre-scan + disambiguation |
| Linking | Cranelift object file + system linker | Single `.c` file + `cc -O` |

## The Self-Hosted Compilation Pipeline

When `glyph build db.glyph output` runs:

1. **Read definitions** — `read_fn_defs` queries `SELECT name, body FROM def WHERE kind='fn'`
2. **Compile each function** — `compile_fn(source)`:
   - Tokenize (indentation-sensitive lexer)
   - Parse (recursive descent → AST pool)
   - Type-check (HM inference + row polymorphism)
   - Lower to MIR (flat CFG with basic blocks)
3. **Fix field offsets** — `fix_all_field_offsets(mirs)`:
   - Build type registry from record aggregate statements
   - Pre-scan field accesses per local variable
   - Resolve correct alphabetical offset for each `rv_field` statement
4. **Generate C** — `cg_program(mirs)`:
   - Emit preamble (`#include`, extern declarations)
   - Emit runtime C code (memory, strings, arrays, I/O, SQLite)
   - Emit forward declarations for all functions
   - Emit function bodies (locals, basic blocks, terminators)
   - Emit `main()` wrapper
5. **Compile and link** — `cc -O -w -o output /tmp/glyph_out.c -lsqlite3`

## C Runtime

The self-hosted compiler embeds a C runtime (~8.5k chars across 6 string constants) compiled into every binary:

| Runtime | Functions | Purpose |
|---------|-----------|---------|
| `cg_runtime_c` | panic, alloc, realloc, dealloc, str_eq/len/char_at/slice/concat, int_to_str, cstr_to_str, println, eprintln, array_bounds_check, array_push/len/set/pop, exit, str_to_int | Core language support |
| `cg_runtime_args` | set_args, args | Command-line argument access |
| `cg_runtime_sb` | sb_new, sb_append, sb_build | String builder for O(n) interpolation |
| `cg_runtime_io` | str_to_cstr, read_file, write_file, system | File I/O and shell commands |
| `cg_runtime_sqlite` | db_open, db_close, db_exec, db_query_rows, db_query_one | SQLite database access |
| `cg_runtime_raw` | raw_set | Direct memory write for MIR mutation |

## Pitfalls and Lessons Learned

### String interpolation in definitions

Glyph uses `{expr}` for string interpolation. A string literal `"{"` causes the lexer to enter interpolation mode, consuming source code until it finds a matching `}`. This is catastrophic when it happens inside a function definition.

**Rule**: Always use `"\{"` for a literal `{` in string constants. The `cg_lbrace` helper exists for this reason.

This was the hardest bootstrap bug to diagnose — the symptom was `125 ->` (a match arm pattern) appearing in generated C code, because the lexer consumed source from one string literal through to a `}` in a different match arm.

### Escape sequence processing

The Rust lexer processes escape sequences (`\n` → newline byte) at scan time. The self-hosted lexer does not — it extracts raw text between quotes. This means the same string literal has different runtime values depending on which compiler built the binary.

**Fix**: `process_escapes` converts Glyph escapes to actual bytes, called in `cg_str_literal` before `cg_escape_str` re-escapes for C output. The round-trip is:

```
Glyph source: \n  →  process_escapes: byte 0x0A  →  cg_escape_str: \n  →  C compiler: byte 0x0A
Glyph source: \\n →  process_escapes: bytes 0x5C,0x6E → cg_escape_str: \\n → C compiler: bytes 0x5C,0x6E
```

### Record field offset resolution

Cranelift and the C backend both store record fields in alphabetical order (the Rust type system uses BTreeMap). But MIR `rv_field` statements store a field name and offset. The self-hosted compiler's `lower_field` doesn't have type information, so it stores `sival=0` (offset 0) for all fields.

**Fix**: A post-processing pass (`fix_all_field_offsets`) runs after compilation:
1. Scan all `rv_aggregate` statements to build a global type registry (sorted field name lists)
2. For each local variable, collect all field names accessed on it
3. Find the matching type in the registry (prefer the type with the most fields for disambiguation)
4. Compute the correct alphabetical offset and patch `sival` in-place via `glyph_raw_set`

### Type disambiguation

When multiple record types share field names (e.g., TyNode has `{n1, n2, ns, sval, tag}` and AstNode has `{ival, kind, n1, n2, n3, ns, sval}`), the field offset resolver must pick the right type. The first-match strategy picked TyNode for locals that only accessed `{n1, ns, sval}`, giving wrong offsets.

**Fix**: `find_best_type` prefers the type with the most fields. AstNode (7 fields) wins over TyNode (5 fields) when both contain the accessed fields. This works because the larger type is more specific — a variable accessing 3 fields is more likely to be the 7-field type than the 5-field type.

### void return from runtime functions

C codegen generates `_N = function_call(args)` for every call, assigning the result to a local. But some runtime functions (originally `glyph_array_set`) returned `void`, causing C compilation errors.

**Fix**: All runtime functions return `long long` (even if just `return 0`). The MIR always assigns call results to locals, so every function must return a value.

### Modifying definitions in the database

The `glyph.glyph` database has triggers that call custom SQLite functions (`glyph_hash`, `glyph_tokens`) on INSERT/UPDATE. These functions only exist inside the Rust compiler process.

**Rule**: When modifying definitions via Python scripts or the `sqlite3` CLI, always drop triggers first:
```python
for t in ["trg_def_dirty", "trg_dep_dirty", "trg_def_hash_insert", "trg_def_hash_update"]:
    conn.execute(f"DROP TRIGGER IF EXISTS {t}")
```
Then run `glyph0 build glyph.glyph --full` afterward to recompute hashes and tokens.

### Zero-argument functions with side effects

Glyph evaluates zero-argument functions eagerly (as constants). A function like `print_usage = eprintln("Usage: ...")` would print at definition-evaluation time, not at call time.

**Fix**: Add a dummy parameter: `print_usage u = eprintln("Usage: ...")`.

### Generated C file location

Both `build` and `run` commands write to `/tmp/glyph_out.c`. This is a useful debugging artifact — when something goes wrong, inspect this file to see the generated C code.

## Verifying the Bootstrap

After any changes to `glyph.glyph` definitions:

```bash
ninja                                          # rebuild everything
ninja test                                     # run all tests
./glyph build glyph.glyph glyph_verify         # self-build
diff <(md5sum glyph) <(md5sum glyph_verify)    # should differ (non-deterministic C)
./glyph_verify run test_comprehensive.glyph    # should produce same 14 lines
```

The binaries won't be byte-identical (string addresses differ between compilations), but they should produce identical behavior. The test suite verifies: arithmetic, strings, arrays, match expressions, recursion, boolean operators, raw strings, and enum variants.

## Adding New Definitions

The recommended workflow for adding new definitions to the self-hosted compiler:

1. Write a Python script that inserts/replaces definitions via `sqlite3` (avoids escaping issues)
2. Run `ninja` to rebuild the full chain
3. Run `ninja test` to verify nothing broke
4. Run `ninja dump` to update `glyph.sql`

Alternatively, use the self-hosted CLI directly:

```bash
./glyph put glyph.glyph fn -b 'my_fn x = x + 1'
ninja   # rebuilds with the new definition
```

## File Inventory

```
glyph.glyph          Source database (553 definitions, 332k)
glyph.sql            SQL dump of glyph.glyph (for version control)
glyph0               Rust bootstrap compiler (stage 0)
glyph1               Cranelift intermediate binary (stage 1)
glyph                Self-hosted compiler (stage 2, final)
build.ninja           Build system encoding the bootstrap chain
test_comprehensive.glyph   Regression test database (14 tests)
crates/               Rust compiler source (6 crates, ~10k lines)
```
