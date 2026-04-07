# Pluggable Backends

This document describes the design for pluggable code generation backends in the Glyph compiler. The goal is to allow new backends (WebAssembly, ELF, ARM, etc.) to live in separate `.glyph` library files that the compiler loads at build time, rather than requiring all backends to be compiled into the compiler itself.

## Table of Contents

1. [Motivation](#motivation)
2. [Current Architecture](#current-architecture)
3. [The MIR Contract](#the-mir-contract)
4. [Backend Interface](#backend-interface)
5. [Runtime Contract](#runtime-contract)
6. [Toolchain Contract](#toolchain-contract)
7. [Library Packaging](#library-packaging)
8. [Discovery by Convention](#discovery-by-convention)
9. [Backend Dispatch](#backend-dispatch)
10. [Worked Example: WASM Backend](#worked-example-wasm-backend)
11. [Worked Example: Extracting LLVM](#worked-example-extracting-llvm)
12. [Constraints and Limitations](#constraints-and-limitations)
13. [Migration Path](#migration-path)

---

## Motivation

The Glyph compiler currently has two backends compiled directly into `glyph.glyph`:

| Backend | Functions | Tokens | Prefix | Output |
|---------|-----------|--------|--------|--------|
| C codegen | ~142 | ~25,700 | `cg_` | C source → `cc` → native ELF |
| LLVM IR | ~93 | ~13,500 | `ll_` | LLVM IR text → `clang` → native ELF |

Both backends produce native x86-64 executables via different paths. Adding a new target (e.g., WebAssembly, ARM, RISC-V) currently requires adding ~60–140 new definitions directly into the compiler database. This scales poorly: every backend inflates the compiler, slows self-compilation, and couples target-specific code to the core.

A pluggable backend architecture would allow:

- **Separation of concerns** — each backend is an independent `.glyph` library with its own development and test cycle
- **Selective inclusion** — only the backend(s) needed for a given build are loaded
- **Third-party backends** — anyone can write a backend library without modifying the compiler
- **Smaller compiler** — `glyph.glyph` contains only the C backend (needed for self-compilation) and the shared frontend/MIR infrastructure

---

## Current Architecture

The compilation pipeline splits cleanly into a **shared frontend** and a **target-specific backend**:

```
.glyph DB
  │
  ▼
Read definitions (SQL)
  │
  ▼
Parse (tok_*, parse_*)
  │
  ▼
Type check (tc_*, infer_*, unify_*)
  │
  ▼
Monomorphize (mono_*)
  │
  ▼
Lower to MIR (lower_*, compile_fn_*)
  │
  ▼
Post-MIR passes:
  ├── fix_all_field_offsets    (resolve record field byte offsets)
  ├── fix_extern_calls         (rewrite extern calls for runtime set)
  ├── tco_optimize             (tail call → loop rewriting)
  └── array_freeze             (freeze MIR arrays for safety)
  │
  ▼
┌─────────────────────────────────────────────────────────┐
│ BACKEND BOUNDARY — everything above is shared           │
├─────────────────────┬───────────────────────────────────┤
│ C Backend (cg_*)    │ LLVM Backend (ll_*)               │
│ MIR → C text        │ MIR → LLVM IR text                │
│ cc invocation       │ clang invocation                  │
│ Embedded C runtime  │ C runtime compiled separately     │
└─────────────────────┴───────────────────────────────────┘
```

Backend selection currently happens in `compile_db` via a `mode` integer:

```
mode < 10   → build_program      (C backend)
mode >= 10  → build_program_llvm (LLVM backend)
mode == 20  → emit C source only (no compilation)
```

Both `build_program` and `build_program_llvm` share identical code up to the MIR stage — the same parse, type check, monomorphization, and MIR post-processing. They diverge only at the code emission step.

---

## The MIR Contract

MIR is the interface between the shared frontend and any backend. A backend receives an array of MIR functions and supporting metadata. The MIR format is stable and well-defined.

### MIR Function Record

Each function in the `[MIR]` array is a record:

```
MirFunction =
  fn_name         : S           -- function name (e.g., "factorial")
  fn_params       : [I]         -- local IDs for parameters
  fn_locals       : [S]         -- local variable names (parallel array)
  fn_blocks_stmts : [[Statement]]  -- statements per basic block
  fn_blocks_terms : [Terminator]   -- terminator per basic block
  fn_entry        : I           -- entry block ID (typically 0)
  fn_subs         : [MIR]       -- nested lifted lambdas
  fn_types        : [I]         -- per-local MIR type tags
```

### Statements (Rvalues)

Each statement assigns to a local variable. The `skind` field selects the operation:

| Kind | Code | Description |
|------|------|-------------|
| `rv_use` | 1 | Copy: `_dest = _src` |
| `rv_binop` | 2 | Binary operation: `_dest = _a op _b` |
| `rv_unop` | 3 | Unary operation: `_dest = op _a` |
| `rv_call` | 4 | Function call: `_dest = f(args...)` |
| `rv_field` | 5 | Field access: `_dest = _rec.field` |
| `rv_index` | 6 | Array index: `_dest = _arr[_idx]` |
| `rv_aggregate` | 7 | Construct record/array/enum: `_dest = {fields...}` |
| `rv_str_interp` | 8 | String interpolation: `_dest = "...{_x}..."` |
| `rv_make_closure` | 9 | Create closure: `_dest = closure(fn, captures...)` |

Statement record:

```
Statement = {sdest:I, skind:I, sival:I, sstr:S, sop1:Op, sop2:Op, sops:[Op]}
```

### Terminators

Each block ends with exactly one terminator:

| Kind | Code | Description |
|------|------|-------------|
| `tm_goto` | 1 | Unconditional jump to `tgt1` |
| `tm_return` | 2 | Return `top` from function |
| `tm_branch` | 3 | Conditional: if `top` then `tgt1` else `tgt2` |
| `tm_switch` | 4 | Multi-way branch (enum tag dispatch) |
| `tm_unreachable` | 5 | Trap (non-exhaustive match) |

Terminator record:

```
Terminator = {tkind:I, top:Op, tgt1:I, tgt2:I}
```

### Operands

Leaf values in statements and terminators:

```
Operand = {okind:I, oval:I, ostr:S}
```

| Kind | Code | Meaning |
|------|------|---------|
| `ok_local` | 1 | Reference to local variable `_N` |
| `ok_const_int` | 2 | Integer literal |
| `ok_const_bool` | 3 | Boolean literal (0 or 1) |
| `ok_const_str` | 4 | String literal |
| `ok_const_unit` | 5 | Unit value `()` |
| `ok_func_ref` | 6 | Function reference by name |
| `ok_const_float` | 7 | Float literal (bitcast in `oval`) |

### MIR Invariants

After post-processing, the MIR array satisfies these invariants that backends can rely on:

1. **Field offsets resolved** — `fix_all_field_offsets` has replaced symbolic field names with concrete byte offsets in all `rv_field` statements
2. **Extern calls normalized** — `fix_extern_calls` has rewritten runtime function calls to use `glyph_`-prefixed names
3. **Tail calls optimized** — `tco_optimize` has converted self-recursive tail calls into loops (goto back to entry block with parameter reassignment)
4. **Arrays frozen** — MIR arrays are immutable; backends can traverse without concern for mutation

---

## Backend Interface

A pluggable backend must provide three capabilities:

### 1. Code Emission

The primary entry point. Takes post-processed MIR and supporting metadata, returns target text.

**Required signature:**

```
backend_emit mirs struct_map rt_set externs ret_map → S
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `mirs` | `[MIR]` | Array of post-processed MIR functions |
| `struct_map` | `Map` | Named type → field layout mapping |
| `rt_set` | `Map` | Runtime function name set (for name prefixing) |
| `externs` | `[Extern]` | FFI declarations from `extern_` table |
| `ret_map` | `Record` | Function → return type mapping (for typed returns) |

The return value is a string containing the target code (C source, LLVM IR, WAT, assembly, etc.).

### 2. Data Function Emission (optional)

If the target needs special handling for `data` definitions (algebraic data type constructors):

```
backend_emit_data data_defs → S
```

### 3. Toolchain Invocation

The backend must specify how to turn emitted text into an executable:

```
backend_link emitted_path output_path mode lib_flags cc_extra → I
```

Returns 0 on success, nonzero on failure. This wraps the target-specific toolchain (e.g., `cc`, `clang`, `wat2wasm`, `llc`).

### Supporting Metadata

Backends also receive these pre-computed structures:

| Structure | Builder | Purpose |
|-----------|---------|---------|
| `struct_map` | `build_full_struct_map` | Maps named types to ordered field lists with byte offsets. Used for typed returns, aggregate construction, field access. |
| `rt_set` | `mk_runtime_set` | Hash set of ~90 runtime function names. Backends use this to distinguish user functions from runtime calls and apply the `glyph_` prefix. |
| `ret_map` | `build_ret_map` | Maps function names to their return types (struct pointer vs GVal). Enables typed calling conventions. |
| `enum_map` | `build_enum_map` | Maps enum type names to their variant lists. Used for typedef generation. |

---

## Runtime Contract

Every backend needs a runtime implementation. The runtime provides the ~90 functions in `mk_runtime_set`, covering:

| Category | Functions | Description |
|----------|-----------|-------------|
| Memory | `alloc`, `dealloc`, `realloc` | Heap allocation (via Boehm GC) |
| Strings | `str_concat`, `str_eq`, `str_len`, `str_slice`, `str_char_at`, `int_to_str`, `str_to_int`, ... | Fat-pointer strings `{ptr, len}` |
| StringBuilder | `sb_new`, `sb_append`, `sb_build` | O(n) string construction |
| Arrays | `array_new`, `array_push`, `array_pop`, `array_set`, `array_len`, ... | Dynamic arrays `{ptr, len, cap}` |
| I/O | `println`, `eprintln`, `print`, `read_file`, `write_file`, `read_line`, `flush` | Standard I/O |
| Float | `float_to_str`, `str_to_float`, `int_to_float`, `float_to_int`, math functions | IEEE 754 via bitcast |
| Maps | `hm_new`, `hm_set`, `hm_get`, `hm_del`, `hm_keys`, `hm_len`, `hm_has` | Hash map operations |
| Result | `ok`, `err`, `try_read_file`, `try_write_file` | Result type construction |
| Bytes | `bytes_new`, `bytes_len`, `bytes_get`, `bytes_set`, `bytes_push`, `bytes_slice` | Raw byte buffers |
| Refs | `ref`, `deref`, `set_ref` | Mutable reference cells |
| Safety | `panic`, `_glyph_current_fn`, signal handlers | Runtime error reporting |

### Runtime Strategies

Different backends will implement runtimes differently:

| Backend | Runtime Strategy |
|---------|-----------------|
| C | Embedded C source (current: `cg_runtime_full`) — concatenated into output |
| LLVM IR | Separate C file compiled alongside IR (`/tmp/glyph_runtime.c`) |
| WASM | Runtime as WAT/WASM module, imported by generated code |
| Native ELF | Runtime as static `.a` library, linked at final step |

A backend library should include its runtime as string constants (like `cg_runtime_c` does for the C backend) or reference external runtime files via `cc_prepend` metadata.

### Data Representation

All backends must agree on the core data layout:

| Type | Representation |
|------|---------------|
| `GVal` | 64-bit integer (`intptr_t`) — the universal value type |
| Strings | Heap-allocated `{ptr: *u8, len: i64}` (16 bytes) |
| Arrays | Heap-allocated `{ptr: *GVal, len: i64, cap: i64}` (24 bytes) |
| Records | Heap-allocated struct, fields in alphabetical order, each field is a GVal |
| Enums | Heap-allocated `{tag: i64, payload...}` (tag at offset 0) |
| Closures | Heap-allocated `{fn_ptr, captures...}` |
| Booleans | 0 or 1 in a GVal |
| Floats | Bitcast into GVal via `_glyph_i2f` / `_glyph_f2i` |

---

## Toolchain Contract

After code emission, the backend must invoke an external toolchain to produce the final artifact. The contract:

1. **Write emitted code** to a temp file (e.g., `/tmp/glyph_out.wat`)
2. **Compile/assemble** using the target toolchain
3. **Link runtime** — either embedded, compiled alongside, or linked from a prebuilt library
4. **Handle build modes** — respect the `mode` parameter for debug/release flags
5. **Return exit code** — 0 for success, nonzero for failure

Example toolchain configurations:

| Backend | Emit | Toolchain | Link |
|---------|------|-----------|------|
| C | `/tmp/glyph_out.c` | `cc` | Runtime embedded in source |
| LLVM | `/tmp/glyph_out.ll` | `clang` | `clang runtime.c out.ll -o output` |
| WASM | `/tmp/glyph_out.wat` | `wat2wasm` + `wasm-ld` | Runtime as WASM imports |
| ARM | `/tmp/glyph_out.s` | `aarch64-linux-gnu-gcc` | Cross-compile with runtime |

---

## Library Packaging

A backend library is a `.glyph` database following a naming convention. The convention is the discovery mechanism — no flags, no registry tables, no special commands.

### Naming Convention

For a backend named `<target>` (e.g., `wasm`, `arm64`, `qbe`), the library must export:

```
# Required — these exact names are what the compiler looks for
<target>_emit_program : fn    -- mirs struct_map rt_set externs ret_map → S
<target>_link         : fn    -- emitted_path output_path mode lib_flags cc_extra → I
```

For example, `wasm.glyph` exports `wasm_emit_program` and `wasm_link`. That's it. When the user passes `--emit=wasm`, the compiler looks for `wasm_emit_program` in the compilation unit. If the function exists (because the library was `use`'d), it gets called. If not, the compiler reports an error.

### Example: wasm.glyph

```
# Entry points (convention-required names)
wasm_emit_program : fn   -- mirs struct_map rt_set externs ret_map → S
wasm_link         : fn   -- emitted_path output_path mode lib_flags cc_extra → I

# Internal codegen functions (any naming, wa_ by convention)
wa_emit_function    wa_emit_block      wa_emit_stmt
wa_emit_term        wa_emit_call       wa_emit_binop
wa_emit_operand     wa_emit_aggregate  wa_emit_field
wa_local            wa_block_label     wa_fn_name
wa_runtime_imports  wa_type_section    wa_memory_section
...

# Runtime (if embedded as string constants)
wa_runtime : fn   -- returns runtime WAT as string

# Optional extensions
wasm_emit_data     : fn   -- data definition codegen
wasm_emit_test     : fn   -- test harness generation
```

### Library Metadata

The `meta` table in the backend `.glyph` can specify build-time requirements:

```sql
INSERT INTO meta (key, val) VALUES ('cc_prepend', 'wasm_runtime.c');
INSERT INTO meta (key, val) VALUES ('cc_args', '-lwasm');
```

---

## Discovery by Convention

The central design challenge for pluggable backends is: **how does the compiler find and call a backend it wasn't originally compiled with?**

Glyph has no dynamic dispatch — no vtables, no trait objects, no `eval`, no runtime function lookup by string name. Every function call must be statically known at compile time. A registry or `--backend` flag would be needless ceremony — and would mean nothing on a non-compiler program.

The solution is **convention-based discovery**: the `--emit` flag value directly determines the function name to call.

### How It Works

```
glyph build program.glyph --emit=wasm
```

The compiler:

1. Builds the emit target string from the flag: `"wasm"`
2. Looks for a function named `wasm_emit_program` in the current compilation unit
3. If found → calls it with the standard backend arguments (MIR, struct_map, etc.)
4. If not found → reports a clear error:

```
Error: no backend for --emit=wasm
  Expected function 'wasm_emit_program' not found.
  To add the WASM backend: glyph use glyph.glyph wasm.glyph
```

The name lookup happens at **compile time**, not runtime. When the compiler is built with `glyph use glyph.glyph wasm.glyph`, the library's definitions (including `wasm_emit_program`) are compiled into the binary. The match dispatch in the compiler references the function directly — but the match arms are determined by which backends are linked in.

### The Dispatch Match

The compiler maintains a `dispatch_backend` function. When `--emit=<target>` is used for something other than the built-in `c` or `llvm` backends, it dispatches through this match:

```
dispatch_backend target mirs struct_map rt_set externs ret_map =
  match target
    "wasm" -> wasm_emit_program(mirs, struct_map, rt_set, externs, ret_map)
    "qbe"  -> qbe_emit_program(mirs, struct_map, rt_set, externs, ret_map)
    _ -> err("unknown backend: " + target)
```

The key question is: **who writes this match?**

---

## Backend Dispatch

Two approaches solve this, depending on whether the backend lives inside the compiler binary or outside it.

### Approach A: Linked Backends (Code-Generating Registrar)

When `glyph use glyph.glyph wasm.glyph` is run, the library's definitions become part of the compiler's compilation unit. The `use` command can go one step further: **detect that the library contains a backend entry point and auto-update the dispatcher**.

#### Registration Flow

```bash
glyph use glyph.glyph wasm.glyph
```

The `use` command:

1. Registers `wasm.glyph` in `lib_dep` as usual
2. Probes the library: does it contain a function matching `*_emit_program`?
3. If yes, extracts the target name (e.g., `wasm` from `wasm_emit_program`)
4. Auto-regenerates the `dispatch_backend` definition in `glyph.glyph` to include the new arm

Step 4 is **metaprogramming via the database** — the `use` command writes Glyph source code into the `def` table. Since definitions are SQL rows, this is just an INSERT/UPDATE. The next compiler rebuild compiles the new dispatch arm.

```
-- Auto-generated by `glyph use`:
dispatch_backend target mirs struct_map rt_set externs ret_map =
  match target
    "wasm" -> wasm_emit_program(mirs, struct_map, rt_set, externs, ret_map)
    _ -> err("unknown backend: " + target)
```

`glyph unuse glyph.glyph wasm.glyph` removes the library and regenerates the dispatcher without the arm.

This is invisible to the user. They just `use` the library and rebuild the compiler — no `--backend` flag, no registry commands, no manual edits. The convention (`<target>_emit_program`) is the entire protocol.

#### How It Fails Gracefully

If someone writes `--emit=snoopy`, the compiler's dispatch_backend hits the wildcard arm and reports:

```
Error: no backend for --emit=snoopy
  No function 'snoopy_emit_program' found.
  Available backends: c, llvm, wasm
```

The "Available backends" list comes from the arms in the generated match — always accurate, always in sync.

#### Tradeoffs

| Pro | Con |
|-----|-----|
| Zero runtime overhead (static dispatch) | Requires rebuilding the compiler after `use` |
| No special flags — `use` is enough | Metaprogramming is unusual (auto-modifying source) |
| Convention is discoverable (naming pattern) | |
| Error messages are precise | |

### Approach B: External Backends (Subprocess Dispatch)

For backends that should not require rebuilding the compiler — or that aren't even written in Glyph — the compiler can discover standalone executables by convention.

#### Backend Executables

A backend library is compiled into a standalone tool:

```bash
glyph build wasm.glyph glyph-wasm
cp glyph-wasm ~/.local/bin/
```

The backend executable accepts a standard CLI:

```bash
glyph-wasm emit /tmp/glyph_mir.json -o /tmp/glyph_out.wat
glyph-wasm link /tmp/glyph_out.wat -o program.wasm --mode=2
glyph-wasm info
```

#### Discovery

```
glyph build program.glyph --emit=wasm
```

If `wasm_emit_program` is not found as a linked function, the compiler falls back to looking for `glyph-wasm` on PATH. Same naming convention (`glyph-<target>`), same principle as Git subcommand discovery (`git-lfs`, `git-credential-store`).

Install the binary → it's available. Remove it → it's gone. No registration.

#### MIR Serialization

The compiler serializes MIR + metadata to JSON for the subprocess:

```json
{
  "mirs": [
    {
      "fn_name": "factorial",
      "fn_params": [0],
      "fn_locals": ["n", "_1", "_2", "_3"],
      "fn_entry": 0,
      "fn_types": [0, 0, 0, 0],
      "fn_blocks_stmts": [
        [{"sdest": 1, "skind": 2, "sival": 5,
          "sop1": {"okind": 1, "oval": 0},
          "sop2": {"okind": 2, "oval": 1}}]
      ],
      "fn_blocks_terms": [
        {"tkind": 3, "top": {"okind": 1, "oval": 1}, "tgt1": 1, "tgt2": 2}
      ]
    }
  ],
  "struct_map": { ... },
  "externs": [ ... ],
  "ret_map": { ... }
}
```

The existing JSON subsystem (`json_*`, `jb_*`) used by the MCP server can serialize MIR.

#### Invocation Flow

```
compile_db:
  1. Shared frontend: parse → typecheck → mono → MIR → post-process
  2. Check linked backends (dispatch_backend match)
  3. If not found, look for glyph-wasm on PATH
  4. Serialize MIR to /tmp/glyph_mir.json
  5. glyph_system("glyph-wasm emit /tmp/glyph_mir.json -o /tmp/glyph_out.wat")
  6. glyph_system("glyph-wasm link /tmp/glyph_out.wat -o out.wasm --mode=2")
```

#### Tradeoffs

| Pro | Con |
|-----|-----|
| No compiler rebuild needed | Serialization overhead (MIR → JSON → parse) |
| Works with any language | Two processes instead of one |
| Process isolation | Must maintain serialization format |
| Independent versioning | Subprocess spawn latency |

### Combined Dispatch (Recommended)

The compiler tries linked backends first, then falls back to external:

```
compile_db with --emit=<target>:
  1. Shared frontend → MIR
  2. match target
       "c"    -> build_program(...)              -- always built-in
       "llvm" -> build_program_llvm(...)         -- built-in (or linked)
       _      -> dispatch_backend(target, ...)   -- auto-generated match
  3. If dispatch_backend returns "unknown backend":
       bin = find_on_path("glyph-" + target)
       if found -> serialize MIR, invoke subprocess
       else     -> error with suggestions
```

This gives a clean upgrade path: start with an external backend for rapid iteration, promote to linked once stable.

| Scenario | Mechanism | Overhead |
|----------|-----------|----------|
| Built-in C backend | Direct call | Zero |
| Linked WASM backend (`glyph use`) | Direct call | Zero |
| External WASM backend (`glyph-wasm` on PATH) | Subprocess | Serialization + spawn |
| Unknown `--emit=snoopy` | Error | — |

---

## Worked Example: WASM Backend

A WebAssembly backend (`wasm.glyph`) that emits WAT (WebAssembly Text Format).

### Structure

```
wasm.glyph (~80-100 definitions)
├── wa_emit_program      -- top-level: module header + functions + memory + exports
├── wa_emit_function     -- per-function: params, locals, body
├── wa_emit_block        -- per-block: label + statements + terminator
├── wa_emit_stmt         -- statement dispatch (match on skind)
│   ├── wa_emit_use      -- local.get / local.set
│   ├── wa_emit_binop    -- i64.add, i64.sub, i64.mul, ...
│   ├── wa_emit_unop     -- i64.eqz, ...
│   ├── wa_emit_call     -- call $function_name
│   ├── wa_emit_field    -- i64.load offset=N
│   ├── wa_emit_index    -- array element access (call to runtime)
│   ├── wa_emit_aggregate -- struct/array construction (call to runtime alloc)
│   ├── wa_emit_closure  -- closure creation
│   └── wa_emit_interp   -- string interpolation (sb_* calls)
├── wa_emit_term         -- terminator dispatch (match on tkind)
│   ├── wa_emit_goto     -- br $label
│   ├── wa_emit_return   -- return
│   ├── wa_emit_branch   -- br_if $label
│   └── wa_emit_switch   -- br_table
├── wa_emit_operand      -- operand rendering (local.get, i64.const, ...)
├── wa_fn_name           -- name mangling (glyph_ prefix for runtime)
├── wa_runtime_imports   -- (import "env" "glyph_println" (func ...))
├── wa_memory_section    -- (memory (export "memory") 256)
├── wa_type_section      -- function type declarations
├── wa_link              -- wat2wasm + wasm-ld invocation
└── wa_runtime           -- runtime as WAT or reference to wasm_runtime.c
```

### Sample Output

For a simple Glyph function:

```
factorial n =
  match n <= 1
    true -> 1
    _ -> n * factorial(n - 1)
```

The WASM backend would emit:

```wat
(func $factorial (param $p0 i64) (result i64)
  (local $l1 i64) (local $l2 i64) (local $l3 i64)
  ;; bb_0
  (local.set $l1
    (i64.le_s (local.get $p0) (i64.const 1)))
  (br_if $bb_1 (local.get $l1))
  (br $bb_2)
  ;; bb_1
  (return (i64.const 1))
  ;; bb_2
  (local.set $l2
    (i64.sub (local.get $p0) (i64.const 1)))
  (local.set $l3
    (call $factorial (local.get $l2)))
  (return
    (i64.mul (local.get $p0) (local.get $l3)))
)
```

### Build Flow (External — Subprocess)

The simplest path. No compiler modification needed:

```bash
# Build the WASM backend into a standalone tool
glyph build wasm.glyph glyph-wasm
cp glyph-wasm ~/.local/bin/

# Now any glyph compiler can use it — no registration:
glyph build program.glyph program.wasm --emit=wasm

# Pipeline:
#   parse → typecheck → mono → MIR
#   → wasm_emit_program not found (not linked)
#   → glyph-wasm found on PATH
#   → serialize MIR to /tmp/glyph_mir.json
#   → glyph-wasm emit /tmp/glyph_mir.json -o /tmp/glyph_out.wat
#   → glyph-wasm link /tmp/glyph_out.wat -o program.wasm
```

### Build Flow (Linked — Zero Overhead)

For zero-overhead dispatch after linking into the compiler:

```bash
# Link library into compiler
glyph use glyph.glyph wasm.glyph

# Rebuild compiler — glyph use auto-detected wasm_emit_program
# and regenerated dispatch_backend with a "wasm" arm
ninja

# Use directly — no subprocess, no serialization:
./glyph build program.glyph program.wasm --emit=wasm

# Pipeline:
#   parse → typecheck → mono → MIR → wasm_emit_program() → /tmp/glyph_out.wat
#   wasm_link() → wat2wasm /tmp/glyph_out.wat -o program.wasm
```

---

## Worked Example: Extracting LLVM

The existing LLVM backend (`ll_*` functions) could be extracted into `llvm.glyph` as a proof of concept.

### Steps

1. **Export** the ~93 `ll_*` functions from `glyph.glyph`:
   ```bash
   glyph export glyph.glyph src/ --ns=llvm
   ```

2. **Create** `llvm.glyph` and import:
   ```bash
   glyph init llvm.glyph
   glyph import llvm.glyph src/
   ```

3. **Add convention entry points** — rename/alias to match the naming convention:
   ```
   llvm_emit_program  (wraps cg_llvm_program)
   llvm_link          (new: wraps clang invocation)
   ```

4. **Remove** `ll_*` from `glyph.glyph` and link the library:
   ```bash
   glyph use glyph.glyph llvm.glyph
   ```
   The `use` command detects `llvm_emit_program` and auto-regenerates `dispatch_backend` with a `"llvm"` arm.

5. **Rebuild the compiler** — `ninja`. The `--emit=llvm` flag now routes through the library's `llvm_emit_program`. No manual edits needed.

### Shared Utilities

Both the C and LLVM backends currently share some functions:

| Function | Used By | Action |
|----------|---------|--------|
| `cg_fn_name` | C + LLVM | Keep in compiler (shared utility) |
| `cg_lbrace` / `cg_rbrace` | C + LLVM | Keep in compiler (trivial) |
| `build_local_types2` | C + LLVM | Keep in compiler (pre-backend analysis) |
| `build_ret_map` | C + LLVM | Keep in compiler (pre-backend analysis) |
| `cg_escape_str` | C + LLVM | Keep in compiler or duplicate |

These shared functions stay in `glyph.glyph` and are available to library backends via the `use` mechanism (libraries can see the host's definitions during compilation).

---

## Constraints and Limitations

### No Dynamic Dispatch

Glyph has no trait objects or vtables. You cannot call a function by string name at runtime. The naming convention (`<target>_emit_program`) bridges this gap: `glyph use` detects the entry point at registration time and auto-generates the dispatch match at compile time. For external backends, the subprocess name (`glyph-<target>`) provides discovery without any in-process dispatch. See [Discovery by Convention](#discovery-by-convention) for the full design.

### C Backend Must Stay In-Tree

The C backend (`cg_*`) is required for self-compilation. The compiler builds itself by emitting C, so the C backend cannot be extracted into a library (circular dependency: you'd need the compiler to build the library that the compiler needs to build itself). The C backend is the bootstrap path and remains in `glyph.glyph`.

### Runtime Duplication

Each backend needs its own runtime implementation. The C runtime (`cg_runtime_full`, ~8,000 tokens across 16 functions) is substantial. A WASM runtime would need equivalent functionality in WAT or compiled C→WASM. This is inherent to cross-target compilation — there's no avoiding it.

### Single GVal Representation

All backends must preserve the `GVal = intptr_t` (64-bit) representation. This simplifies the backend interface (no type-directed code generation) but means backends targeting 32-bit platforms need adaptation layers, and WASM's i64 operations map directly but memory management differs.

### Library Loading at Build Time (Linked Backends)

Linked backend libraries are loaded via `glyph use` + `load_libs_for_build`. This means backend definitions are compiled into the compiler binary — adding or removing a linked backend requires rebuilding the compiler. External backends (subprocess dispatch) have no such limitation: install or remove the binary and the compiler discovers the change immediately.

### Test Harness

The test program builder (`build_test_program`) currently only emits via the C backend. To support `glyph test` with alternative backends, each backend library would also need a test harness emitter, or the C backend would remain the default for testing.

---

## Migration Path

### Phase 1: Infrastructure

Build the foundation for convention-based discovery.

**1a. Convention detection in `glyph use`**

Extend `cmd_use` to probe newly registered libraries for backend entry points. After the normal `lib_dep` insert, scan the library:

```sql
SELECT name FROM def WHERE kind='fn' AND name LIKE '%\_emit\_program' ESCAPE '\'
```

If found, extract the target name (strip `_emit_program` suffix), verify a matching `<target>_link` exists, and regenerate the dispatcher. No new flags needed — the convention is the signal.

New definitions: `probe_backend_entry`, `extract_backend_name`, `regenerate_dispatch`.

**1b. Code-generating dispatcher**

`regenerate_dispatch` scans all `lib_dep` libraries for `*_emit_program` functions, builds a Glyph match expression as a string, and writes it into the `def` table as `dispatch_backend`:

```
regenerate_dispatch db lib_deps =
  backends = scan_for_backends(db, lib_deps)
  -- backends = [{name: "wasm", emit: "wasm_emit_program"}, ...]
  arms = build_match_arms(backends, 0, "")
  body = "dispatch_backend target mirs sm rs ex rm =\n  match target\n"
       + arms
       + "    _ -> err(\"unknown backend: \" + target)"
  put_def(db, "dispatch_backend", "fn", body)
```

The generated definition is visible (`glyph get glyph.glyph dispatch_backend`), debuggable, and compiles like any other function. The user just runs `glyph use` — the rest is automatic.

**1c. Extract shared frontend**

Refactor `build_program` and `build_program_llvm` to share `build_frontend`:

```
build_frontend sources sigs externs struct_map type_rows const_defs data_defs n_app =
  -- parse, type-check, monomorphize, lower, post-process
  -- returns: {bf_mirs, bf_rt_set, bf_ret_map, bf_enum_map}
```

Then each backend function becomes a thin wrapper:

```
build_program ... =
  bf = build_frontend(...)
  code = cg_program(bf.bf_mirs, struct_map, bf.bf_rt_set, bf.bf_enum_map)
  -- write, compile, link
```

This isolates the backend boundary cleanly.

**1d. MIR serialization for subprocess fallback**

Add `mir_to_json` using the existing JSON subsystem (`jb_*` builders). Covers MIR function records, struct_map, rt_set, externs, ret_map. The counterpart `mir_from_json` lives in the external backend executable.

New definitions: `mir_to_json`, `mir_fn_to_json`, `mir_stmt_to_json`, `mir_term_to_json`, `mir_op_to_json`, `mir_metadata_to_json`.

**1e. External backend discovery (PATH fallback)**

Add `find_backend_binary` that looks for `glyph-<target>` on PATH when no linked backend matches.

New definitions: `find_backend_binary`, `invoke_external_backend`.

### Phase 2: Extract LLVM as Proof of Concept

Move the 93 `ll_*` functions into `llvm.glyph`. This is the lowest-risk extraction because:

- The LLVM backend is not used for self-compilation
- It has a clean `ll_` namespace prefix
- The shared utility set is small and well-understood
- It validates the full convention-based workflow end-to-end

Steps:

1. Export `ll_*` into `llvm.glyph`
2. Add convention entry points: `llvm_emit_program`, `llvm_link`
3. Remove `ll_*` from `glyph.glyph`
4. `glyph use glyph.glyph llvm.glyph` — auto-detects `llvm_emit_program`, regenerates dispatcher
5. Rebuild compiler, verify `--emit=llvm` still works
6. Also build `glyph-llvm` standalone binary, verify subprocess dispatch

### Phase 3: New Backends

With the interface proven, new backends can be developed independently:

| Backend | Library | Start As | Toolchain | Notes |
|---------|---------|----------|-----------|-------|
| WASM | `wasm.glyph` | External | `wat2wasm` or Binaryen | High priority — web target |
| QBE | `qbe.glyph` | External | `qbe` + `cc` | Simple SSA IR, near-mechanical MIR translation |
| ARM64 | `arm64.glyph` | External | `aarch64-linux-gnu-gcc` | Cross-compilation |
| RISC-V | `riscv.glyph` | External | `riscv64-linux-gnu-gcc` | Cross-compilation |
| x86 ASM | `x86.glyph` | External | `nasm` + `ld` | Educational / minimal |

New backends start as external (subprocess) for rapid iteration — no compiler rebuilds during development. Once stable, they can be promoted to linked via `glyph use` for zero-overhead dispatch.

QBE is a particularly interesting target — it's a lightweight compiler backend (similar to LLVM but ~10,000 LOC) that accepts a simple SSA-based IR very similar to Glyph's MIR. The translation would be nearly mechanical.

### Phase 4: Ecosystem

Once multiple backends exist:

- **Backend test suite** — a reference `.glyph` program that exercises all MIR features, used to validate new backends against the C backend's output
- **Cross-compilation** — `glyph build program.glyph --target=wasm` as sugar for `--emit=wasm` with appropriate defaults
- **Backend SDK** — a `backend-sdk.glyph` library with helpers for the common patterns (operand rendering, block traversal, name mangling, StringBuilder-based emission)
- **`glyph install-backend wasm`** — download and install a backend from a registry (future)
