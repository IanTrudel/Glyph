# Glyph Compiler Architecture

This document describes the internal architecture of the Glyph self-hosted compiler — the primary compiler that lives in `glyph.glyph`. The compiler is written entirely in Glyph itself (1,516 functions, 172,395 tokens) and compiles Glyph programs stored as SQLite databases into native executables via C or LLVM IR backends.

## Table of Contents

1. [Overview](#overview)
2. [Compiler Features](#compiler-features)
3. [Compilation Pipeline](#compilation-pipeline)
4. [Tokenizer](#tokenizer)
5. [Parser](#parser)
6. [Type Checker](#type-checker)
7. [MIR Lowering](#mir-lowering)
8. [Monomorphization](#monomorphization)
9. [Tail Call Optimization](#tail-call-optimization)
10. [C Code Generation](#c-code-generation)
11. [LLVM IR Code Generation](#llvm-ir-code-generation)
12. [Build Orchestration](#build-orchestration)
13. [C Runtime](#c-runtime)
14. [Namespace Map](#namespace-map)
15. [Data Structures](#data-structures)
16. [Dependency Graph](#dependency-graph)

---

## Overview

The Glyph compiler is a **self-hosted, whole-program compiler** that reads definitions from a `.glyph` SQLite database, runs them through a classical compilation pipeline (tokenize → parse → type-check → lower → optimize → codegen), and produces a native executable by emitting either C source code or LLVM IR text, then invoking an external compiler (`cc` or `clang`) to produce the final binary.

```
.glyph SQLite DB
  │
  ├── SELECT name, body, kind FROM def WHERE kind='fn'
  ├── SELECT name, body FROM def WHERE kind='type'
  ├── SELECT name, symbol, signature FROM extern_
  │
  ▼
Tokenizer (84 fn, 5.5k tokens)
  │  indentation-sensitive lexer
  ▼
Parser (126 fn, 12k tokens)
  │  recursive-descent, flat AST pool
  ▼
Type Checker (146 fn, 13.5k tokens)
  │  Hindley-Milner + row polymorphism
  ▼
MIR Lowering (74 fn, 9.6k tokens)
  │  AST → flat CFG (basic blocks, statements, terminators)
  ▼
Monomorphization (53 fn, 6k tokens)
  │  specialize generic functions with concrete types
  ▼
Tail Call Optimization (11 fn, 1k tokens)
  │  rewrite self-recursive tail calls into loops
  ▼
┌─────────────────────┬──────────────────────────┐
│ C Codegen           │ LLVM IR Codegen          │
│ (142 fn, 25.7k tok) │ (93 fn, 13.5k tok)       │
│ MIR → C source      │ MIR → LLVM IR text       │
│ /tmp/glyph_out.c    │ /tmp/glyph_out.ll        │
│       │             │        │                  │
│   cc -o output      │   clang runtime.c out.ll  │
└───────┼─────────────┴────────┼──────────────────┘
        ▼                      ▼
     Native executable      Native executable
```

**Key design choices:**

- **Everything is a `GVal`** (`typedef intptr_t GVal`) — a 64-bit value that holds integers, booleans, pointers to heap-allocated data (strings, arrays, records, closures), and floats (via bitcast). This uniform representation eliminates type-directed dispatch at the codegen level.
- **Flat arena-based data structures** — the AST is a flat array of `AstNode` records (not a pointer-based tree). The type pool is a flat array of `TyNode` records. All references are integer indices. This makes the compiler expressible in Glyph itself, which has no pointer types.
- **Two backends, shared front-end** — the C and LLVM backends share everything up to and including TCO. The LLVM backend reuses the same C runtime (compiled separately and linked in).
- **Embedded C runtime** — the runtime (memory management, strings, arrays, I/O, hash maps, signal handlers) is stored as string constants inside the compiler and concatenated into the generated C output. No external runtime library is needed.

---

## Compiler Features

The Glyph self-hosted compiler provides the following language and compiler features:

### Language Features

| Feature | Description |
|---------|-------------|
| **Hindley-Milner type inference** | Full HM inference with let-polymorphism — no type annotations required |
| **Row polymorphism** | Records support structural subtyping via row variables (`{x: I, ..r}`) |
| **Pattern matching** | `match` expressions with wildcard, literal, variable, constructor, or-pattern (`p1 \| p2`), and range (`a..b`) patterns |
| **Match guards** | `pat ? guard_expr -> body` syntax for conditional pattern arms |
| **Exhaustiveness checking** | Compile-time warnings for non-exhaustive match expressions |
| **Closures / lambdas** | `\x y -> body` with automatic free-variable capture, heap-allocated environments |
| **Multi-line lambdas** | Lambda bodies can contain indented blocks with let-bindings and statements |
| **String interpolation** | `"hello {name}, you are {age} years old"` with arbitrary expressions |
| **Pipe operator** | `x \|> f` desugars to `f(x)` for left-to-right data flow |
| **Function composition** | `f >> g` creates `\x -> g(f(x))` via synthetic closure |
| **Error propagation** | `expr?` unwraps `Result`/`Optional`, propagating errors to caller |
| **Unwrap operator** | `expr!` unwraps or panics with error message |
| **Let destructuring** | `{x, y} = record_expr` for record field extraction |
| **Named record types** | `type Point = {x: I, y: I}` with C struct codegen |
| **Enum / variant types** | `type Color = Red(I) \| Green(I) \| Blue(I)` with discriminated unions |
| **Char literals** | `#a`, `#\n`, `#\\` compile to integer code points |
| **Negative literals** | `-42` in expressions and patterns |
| **Raw strings** | `r"no \escapes here"` and `r"""triple-quoted"""` |
| **For-loops** | `for x in arr` desugared to index-based MIR loops |
| **Tail call optimization** | Self-recursive tail calls rewritten to loops automatically |
| **Bitwise operators** | `bitand`, `bitor`, `bitxor`, `shl`, `shr` as keywords |
| **Map literals** | `{{key: val, ...}}` with hash-map runtime |
| **Optional type** | `?T` shorthand, `Some(x)` / `None` constructors |
| **Result type** | `!T` shorthand, `ok(x)` / `err(msg)` constructors |
| **Array type** | `[T]` shorthand, `[1, 2, 3]` literals, bounds-checked indexing |

### Compiler Features

| Feature | Description |
|---------|-------------|
| **Monomorphization** | Generic functions specialized with concrete types at call sites |
| **Dual backend** | C codegen (`--emit=c`, default) and LLVM IR codegen (`--emit=llvm`) |
| **Build modes** | `--debug` (O0 -g), `--release` (O2/LTO), default (O1) |
| **Incremental compilation** | Content-hash (BLAKE3) per definition; only dirty defs + dependents recompile |
| **Named struct codegen** | Record types generate `typedef struct` in C for proper field access |
| **Type-directed operations** | String `+`/`==`/`!=` and float arithmetic dispatch based on inferred types |
| **Foreign function interface** | `extern_` table declares C functions; compiler generates ABI wrapper code |
| **Generational versioning** | `--gen=N` flag selects definition versions for staged language evolution |
| **Per-definition emit** | `glyph emit fn_name` shows generated C/LLVM/MIR for a single function |
| **Code coverage** | `glyph test --cover` instruments function entry; `glyph cover` reports |
| **Property-based testing** | `prop` definition kind with seed/trials for randomized testing |
| **Parse error messages** | Descriptive errors with source context, line number, and caret |
| **Test framework** | `glyph test` with `assert`, `assert_eq`, `assert_str_eq`, pass/fail tracking |
| **MCP server** | `glyph mcp` exposes 26 tools via JSON-RPC for LLM interaction |
| **Library linking** | `glyph link`/`use`/`unuse`/`libs` for cross-database definition sharing |
| **Namespace system** | Auto-derived `ns` column from function name prefixes |
| **Definition history** | `glyph undo`/`glyph history` via automatic triggers on the `def` table |
| **Bootstrap chain** | 4-stage: glyph0 (Rust) → glyph1 (Cranelift) → glyph2 (C) → glyph (LLVM) |

### Runtime Features

| Feature | Description |
|---------|-------------|
| **Boehm GC** | All `malloc`/`realloc`/`free` redirected to garbage collector |
| **Signal handlers** | SIGSEGV and SIGFPE handlers with function name + stack trace |
| **Bounds checking** | Array index and string `char_at` bounds-checked at runtime |
| **Immutable collections** | `array_freeze`/`array_thaw`/`hm_freeze` for frozen arrays and maps |
| **Mutable references** | `ref(val)`/`deref(r)`/`set_ref(r, val)` single-cell mutable state |
| **Hash maps** | Open-addressing FNV-1a maps: `hm_new`/`hm_set`/`hm_get`/`hm_del` |
| **String builder** | `sb_new`/`sb_append`/`sb_build` for O(n) string construction |
| **Result type runtime** | `ok(val)`/`err(msg)` constructors, `try_read_file`/`try_write_file` |
| **SQLite access** | `db_open`/`db_close`/`db_exec`/`db_query_rows` (conditional linking) |
| **Bitset** | `bitset_new`/`bitset_set`/`bitset_test` for type checker cycle detection |

---

## Compilation Pipeline

The full pipeline is orchestrated by `compile_db` (the entry point for `glyph build`) and `build_program` / `build_program_llvm` (the backend-specific drivers).

### Step-by-step (`build_program`)

```
 1. consts_to_fn_sources      Convert const defs to zero-arg function sources
 2. parse_all_fns             Tokenize + parse all function sources → [AST]
 3. check_parse_errors        Abort if any parse errors
 4. mk_runtime_set            Build hash set of ~85 runtime function names
 5. build_variant_map         Map constructor names → {type, discriminant, payload_fields}
 6. build_ctor_siblings       Map each constructor → its sibling constructors
 7. build_enum_map            Map enum type names → typedef info
 8. mk_engine                 Create type inference engine (pool, env, substitution)
 9. register_builtins         Register ~90 built-in function types (println, str_len, etc.)
10. tc_register_externs       Register extern function signatures
11. tc_register_ctors         Register enum constructor types
12. tc_set_struct_map         Register named struct types (for float field inference)
13. tc_pre_register           Pre-register all fn names with fresh types (mutual recursion)
14. tc_infer_loop_warm        Warm-up inference pass (generalize all free vars)
15. eng_reset_subst/errors    Reset inference state
16. tc_infer_loop_sigs        Full inference pass → per-function {tmap, type, raw_type}
17. tc_report_errors          Report type warnings to stderr
18. build_za_fns              Build zero-arg function set (for call-on-reference)
19. mono_pass                 Monomorphization: collect → propagate → dedup → compile specs
20. compile_fns_parsed_mono   Lower all functions to MIR (with mono call maps)
21. mono_concat_mirs          Append monomorphized MIR functions to main list
22. fix_all_field_offsets     Resolve record field names → integer offsets
23. fix_extern_calls          Rewrite extern call references
24. tco_optimize              Tail call optimization on all MIR functions
25. array_freeze              Freeze MIR arrays (immutability)
26. cg_program                Generate C: preamble + typedefs + forward decls + functions
27. cg_extern_wrappers        Generate C wrappers for extern declarations
28. cg_runtime_full           Concatenate all C runtime modules
29. write_file                Write /tmp/glyph_out.c
30. glyph_system("cc ...")    Invoke C compiler to produce executable
```

The LLVM backend (`build_program_llvm`) shares steps 1–25, then emits LLVM IR text to `/tmp/glyph_out.ll` and the C runtime to `/tmp/glyph_runtime.c`, and invokes `clang` to link them.

### Build modes

| Mode | C flags | LLVM flags |
|------|---------|------------|
| Default | `-DGLYPH_DEBUG -O1` | `-O1 -flto` |
| Debug (`--debug`) | `-DGLYPH_DEBUG -O0 -g` | `-O0 -g` |
| Release (`--release`) | `-O2` | `-O2 -flto` |

---

## Tokenizer

**84 functions, 5,476 tokens.** Namespace: `tokenizer`.

The tokenizer is an indentation-sensitive lexer that produces a flat array of `Token` records from a source string. It handles Glyph's significant whitespace (like Python) and bracket-depth tracking (indentation is suppressed inside `()`, `[]`, `{}`).

### Token structure

```
Token = {kind: I, start: I, end: I, line: I}
```

All tokens reference positions in the original source string. `tok_text src t` extracts the text via `str_slice(src, t.start, t.end)`.

### Token kinds (67 constants)

**Literals:** `tk_int`(1), `tk_float`(2), `tk_str`(3), `tk_raw_str`(6), `tk_char`(7)

**String interpolation:** `tk_str_interp_start`(4), `tk_str_interp_end`(5)

**Identifiers:** `tk_ident`(10) (lowercase), `tk_type_ident`(11) (uppercase/constructor)

**Keywords:** `tk_match`(22), `tk_for`(23), `tk_in`(24), `tk_else`(21), `tk_const`(27), `tk_extern`(28), `tk_test`(31), `tk_as`(32)

**Operators:**

| Token | Kind | Token | Kind |
|-------|------|-------|------|
| `+` | 40 | `-` | 41 |
| `*` | 42 | `/` | 43 |
| `%` | 44 | `=` | 45 |
| `==` | 47 | `!=` | 48 |
| `<` | 49 | `>` | 50 |
| `<=` | 51 | `>=` | 52 |
| `&&` | 53 | `\|\|` | 54 |
| `!` | 55 | `\|>` | 58 |
| `>>` | 59 | `?` | 60 |
| `->` | 61 | `\` | 62 |
| `..` | 63 | `\|` | 64 |

**Bitwise keywords:** `bitand`(56), `bitor`(57), `bitxor`(65), `shl`(66), `shr`(67)

**Delimiters:** `(`(70), `)`(71), `[`(72), `]`(73), `{`(74), `}`(75), `:`(80), `,`(81), `.`(82)

**Structure:** `tk_indent`(90), `tk_dedent`(91), `tk_newline`(92), `tk_eof`(99), `tk_error`(100)

### Core tokenizer functions

**`tokenize src`** — Entry point. Measures initial indentation, initializes indent stack at `[0]`, calls `tok_loop`, freezes the token array.

**`tok_loop src len pos line bdepth indent_stack tokens`** — Main loop. At each position:
- **Newline:** Calls `skip_blanks` to find the next non-blank line. Compares the new indentation against the indent stack top. Emits `tk_indent` (push new level) or `tk_dedent`(s) (pop levels) as needed. Bracket depth (`bdepth`) suppresses indent/dedent/newline inside `()`, `[]`, `{}`.
- **Other characters:** Delegates to the `tok_one` dispatch chain, which returns a packed integer encoding the new position, line number, and bracket depth.
- **EOF:** Flushes remaining dedents and appends `tk_eof`.

**`tok_one` / `tok_one2` / `tok_one3` / `tok_one4`** — Four-part character dispatch chain, split to avoid Cranelift parse failure on deeply nested match expressions:

| Function | Characters handled |
|----------|-------------------|
| `tok_one` | `-` (arrow/minus/negative), `(`, `)`, `[`, `]`, `{`, `}`, `,`, `\`, `:`, `.`/`..` |
| `tok_one2` | `=`/`==`, `!`/`!=`, `<`/`<=`, `>`/`>=`/`>>`, `\|`/`\|>`/`\|\|` |
| `tok_one3` | `#` (char literals), `&&`, `+`, `*`, `/`, `%`, `?`, `"` (strings) |
| `tok_one4` | Digits (int/float), `r"` (raw strings), identifiers/keywords |

**`tok_pack pos line bd`** — Packs three return values into a single integer: `pos * 100000000 + line * 1000000 + bd`. This avoids returning a record from `tok_one` (which would require heap allocation). The caller unpacks with division and modulo.

### Indentation handling

The tokenizer maintains an indent stack (array of integers). On each non-blank line:

1. `measure_indent` counts leading spaces (tabs count as 4).
2. If the new indent > stack top: push new level, emit `tk_indent`.
3. If the new indent < stack top: pop levels and emit `tk_dedent` for each, until the stack matches.
4. If equal: emit `tk_newline` (same-level continuation).

Indentation tokens are suppressed when `bdepth > 0` (inside brackets). `bdepth` increments on `(`, `[`, `{` and decrements on `)`, `]`, `}`.

### Negative number disambiguation

When `-` is followed by a digit, the tokenizer checks if the previous token was a "value kind" (int, float, char, ident, type_ident, str, raw_str, rparen, rbracket, rbrace, bang). If so, `-` is emitted as `tk_minus` (subtraction). Otherwise, it scans a negative number literal. This distinguishes `x - 1` from `f(-1)`.

### String interpolation

When `"` is encountered, `scan_str_has_interp` looks ahead for an unescaped `{` before the closing `"`. If found, the tokenizer emits `tk_str_interp_start`, then `tok_str_interp_loop` alternates between string segments (`tk_str`) and expression tokens (recursing into `tok_one` for expressions between `{` and `}`), ending with `tk_str_interp_end`.

---

## Parser

**126 functions, 12,039 tokens.** Namespace: `parser`.

The parser is a **recursive-descent parser** that builds a flat AST pool — an array of `AstNode` records. All parse functions take `(src, tokens, pos, pool)` and return a `ParseResult = {node: I, pos: I}` where `node` is the index into the pool (or negative on error).

### AST node structure

```
AstNode = {kind: I, ival: I, sval: S, n1: I, n2: I, n3: I, ns: [I]}
```

| Field | Purpose |
|-------|---------|
| `kind` | Node type tag (expression, statement, or pattern kind) |
| `ival` | Integer payload (literal value, operator code, boolean 0/1) |
| `sval` | String payload (identifier name, string literal text) |
| `n1`, `n2`, `n3` | Child node indices into the AST pool (-1 if unused) |
| `ns` | Variable-length child array (params, args, match arms, fields) |

### Expression kinds (22)

| Kind | Value | Description | Key fields |
|------|-------|-------------|------------|
| `ex_int_lit` | 1 | Integer literal | `ival`=value |
| `ex_str_lit` | 3 | String literal | `sval`=text |
| `ex_bool_lit` | 4 | Boolean literal | `ival`=0/1 |
| `ex_float_lit` | 54 | Float literal | `sval`=text |
| `ex_ident` | 10 | Identifier | `sval`=name |
| `ex_type_ident` | 60 | Constructor/type | `sval`=name |
| `ex_binary` | 20 | Binary operation | `ival`=op, `n1`=lhs, `n2`=rhs |
| `ex_unary` | 21 | Unary operation | `ival`=op, `n1`=operand |
| `ex_call` | 22 | Function call | `n1`=callee, `ns`=args |
| `ex_field_access` | 23 | `expr.field` | `n1`=object, `sval`=field |
| `ex_index` | 24 | `expr[idx]` | `n1`=array, `n2`=index |
| `ex_field_accessor` | 25 | `.field` shorthand | `sval`=field |
| `ex_pipe` | 30 | `a \|> f` | `n1`=lhs, `n2`=rhs |
| `ex_compose` | 31 | `f >> g` | `n1`=lhs, `n2`=rhs |
| `ex_propagate` | 32 | `expr?` | `n1`=inner |
| `ex_unwrap` | 33 | `expr!` | `n1`=inner |
| `ex_lambda` | 40 | `\x -> body` | `ns`=params, `n1`=body |
| `ex_match` | 42 | `match` | `n1`=scrutinee, `ns`=arms (stride-3) |
| `ex_block` | 44 | Indented block | `ns`=statements |
| `ex_array` | 50 | `[a, b, c]` | `ns`=elements |
| `ex_record` | 51 | `{f: v}` | `ns`=field pairs, `n1`=base (update) |
| `ex_map` | 52 | `{{k: v}}` | `ns`=key-value pairs |
| `ex_str_interp` | 53 | `"...{e}..."` | `ns`=parts |

### Statement kinds (3)

| Kind | Value | Description |
|------|-------|-------------|
| `st_expr` | 200 | Expression statement (`n1`=expr) |
| `st_let` | 201 | Let binding (`sval`=name, `n1`=rhs) |
| `st_let_destr` | 203 | Destructuring let (`ns`=field name nodes, `n1`=rhs) |

### Pattern kinds (7)

| Kind | Value | Description |
|------|-------|-------------|
| `pat_wildcard` | 100 | `_` |
| `pat_ident` | 101 | Variable binding (`sval`=name) |
| `pat_int` | 102 | Integer literal (`ival`=value) |
| `pat_bool` | 103 | Boolean (`ival`=0/1) |
| `pat_str` | 104 | String literal (`sval`=text) |
| `pat_ctor` | 105 | Constructor (`sval`=name, `ns`=sub-patterns) |
| `pat_or` | 106 | Or-pattern (`ns`=sub-patterns) |

### Operator precedence (lowest to highest)

The parser implements a standard precedence tower via recursive descent. Each level calls the next tighter level for its operands:

| Level | Function | Operators | Associativity |
|-------|----------|-----------|---------------|
| 1 | `parse_pipe_expr` | `\|>` | Left |
| 2 | `parse_compose` | `>>` | Left |
| 3 | `parse_logic_or` | `\|\|` | Left |
| 4 | `parse_logic_and` | `&&` | Left |
| 5 | `parse_cmp` | `==` `!=` `<` `>` `<=` `>=` | Left |
| 6 | `parse_bitwise` | `bitand` `bitor` `bitxor` `shl` `shr` | Left |
| 7 | `parse_add` | `+` `-` | Left |
| 8 | `parse_mul` | `*` `/` `%` | Left |
| 9 | `parse_unary` | prefix `-` `!` | Right |
| 10 | `parse_postfix` | `.f` `(args)` `[idx]` `{f:v}` `?` `!` | Left |
| 11 | `parse_atom` | Literals, identifiers, `(e)`, `[a]`, `{r}`, `\x->e`, `match` | — |

`parse_expr` is an alias for `parse_pipe_expr`.

### Operator constants (21)

| Name | Value | Name | Value | Name | Value |
|------|-------|------|-------|------|-------|
| `op_add` | 1 | `op_sub` | 2 | `op_mul` | 3 |
| `op_div` | 4 | `op_mod` | 5 | `op_eq` | 6 |
| `op_neq` | 7 | `op_lt` | 8 | `op_gt` | 9 |
| `op_lt_eq` | 10 | `op_gt_eq` | 11 | `op_and` | 12 |
| `op_or` | 13 | `op_bitand` | 14 | `op_bitor` | 15 |
| `op_bitxor` | 16 | `op_shl` | 17 | `op_shr` | 18 |
| `op_neg` | 20 | `op_not` | 21 | | |

### Key parse functions

**`parse_fn_def`** — Entry point for a function definition. Parses: name, parameters (zero or more identifiers), `=`, body. Returns a `df_fn` node (kind=300) with `sval`=name, `ns`=param indices, `n1`=body index.

**`parse_body`** — Skips newlines, checks for `tk_indent`. If indented, delegates to `parse_block` (returns `ex_block`); otherwise parses a single expression.

**`parse_block` / `parse_block_stmts`** — Parses statements until `tk_dedent` or `tk_eof`. Returns `ex_block` with `ns` containing all statement indices.

**`parse_stmt`** — Checks for destructuring let (`{x, y} = expr` via lookahead: `{` + ident + `,`/`}`). Otherwise delegates to `parse_stmt_expr`, which parses an expression and converts to `st_let` if followed by `=`.

**`parse_atom`** — The largest parser function (1,080 tokens). Handles all primary expressions: int/char/str/raw_str/float literals, string interpolation, identifiers (`true`/`false` as special cases), type identifiers, parenthesized expressions, arrays, records, maps, lambdas, `match`, and `.field` accessor syntax.

**`parse_match_arms`** — Parses arms in **stride-3** format: `[pattern, body, guard]` repeated. Guard is `-1` if absent. Checks for `?` after a pattern to parse match guards (`pat ? guard_expr -> body`).

**`parse_pattern` / `parse_single_pattern`** — Parses a single pattern, then checks for `|` to build `pat_or` nodes. Single patterns: `_`, `true`/`false`, identifiers, integers (including negative), strings, char literals, type identifiers with optional constructor payloads `Ctor(p1, p2)`.

**`parse_postfix_loop`** — Chains postfix operations: `.field`, `(args)`, `[index]`, `{field: val}` (record update), `?`, `!`.

### Parser infrastructure

- `mk_node kind ival sval n1 n2 n3 ns` — creates an AstNode record
- `pool_push pool node` — appends node to AST pool, returns its index
- `push_simple pool kind ival sval` — shorthand for leaf nodes
- `mk_result node pos` — success result
- `mk_err_msg pos msg` — error result with message
- `is_err r` — checks `r.node < 0`
- `expect_tok tokens pos kind` — returns `pos + 1` if token matches, `-1` otherwise

### Entry points

**`compile_fn src za_fns`** — Full pipeline for a single function: tokenize → parse → lower.

**`parse_all_fns sources i`** — Batch parse: tokenizes and parses an array of source strings. Returns array of `{pf_src, pf_ast, pf_fn_idx, pf_name, pf_tokens, pf_err_pos, pf_err_msg}` records.

**`validate_def body`** — Tokenizes and parses a definition body, returning `{vr_ok, vr_msg, vr_pos, vr_tokens}` for the MCP `put_def`/`check_def` tools.

---

## Type Checker

**146 functions, 13,472 tokens.** Namespace: `typeck`.

The type checker implements **Hindley-Milner type inference with let-polymorphism and row polymorphism**. It uses a pool-based type representation with union-find substitution and path compression.

### Type representation

Types are nodes in a flat pool (`eng.ty_pool`), referenced by integer index:

```
TyNode = {tag: I, n1: I, n2: I, ns: [I], sval: S}
```

| Field | Purpose |
|-------|---------|
| `tag` | Type kind (see tag constants below) |
| `n1` | First child type index (e.g., parameter type for functions, element type for arrays) |
| `n2` | Second child type index (e.g., return type for functions) |
| `ns` | Array of type indices (e.g., record fields, tuple elements, forall bound vars) |
| `sval` | String (e.g., field names, type names) |

### Type tag constants (19)

| Tag | Value | Description | Pool node usage |
|-----|-------|-------------|-----------------|
| `ty_int` | 1 | Int64 | — |
| `ty_uint` | 2 | UInt64 | — |
| `ty_float` | 3 | Float64 | — |
| `ty_str` | 4 | String | — |
| `ty_bool` | 5 | Bool | — |
| `ty_void` | 6 | Void | — |
| `ty_never` | 7 | Never (bottom) | — |
| `ty_fn` | 10 | Function | `n1`=param, `n2`=return |
| `ty_array` | 11 | Array | `n1`=element |
| `ty_opt` | 12 | Optional | `n1`=inner |
| `ty_res` | 13 | Result | `n1`=inner |
| `ty_record` | 14 | Record | `ns`=field indices, `n1`=row variable or -1 |
| `ty_var` | 15 | Type variable | `n1`=variable ID |
| `ty_forall` | 16 | Forall (∀) | `n1`=body, `ns`=bound var IDs |
| `ty_named` | 17 | Named type | `sval`=name, `ns`=type args |
| `ty_tuple` | 18 | Tuple | `ns`=element types |
| `ty_map` | 19 | Map | `n1`=key, `n2`=value |
| `ty_field` | 20 | Record field | `sval`=name, `n1`=type |
| `ty_error` | 99 | Error type | — |

### Engine (inference state)

The engine record holds all mutable state for type inference:

```
mk_engine =
  {ty_pool: [],       -- arena of TyNode, indexed by integer
   parent: [],        -- union-find parent array for type variables
   bindings: [],      -- substitution: bindings[var_id] = type index or -1
   next_var: ref(0),  -- mutable counter for fresh variable IDs
   env_names: [],     -- parallel array: environment name → type mapping
   env_types: [],     -- parallel array: environment name → type mapping
   env_marks: [],     -- scope stack marks (indices into env_names)
   errors: [],        -- accumulated error messages
   tmap: ref([]),     -- per-expression type map (AST index → resolved type tag)
   z_smap: []}        -- struct map for C float field constraints
```

### Inference pipeline

1. **Engine creation:** `mk_engine()` allocates empty state.
2. **Builtins:** `register_builtins(eng)` registers ~40 runtime functions (println, str_len, array_push, etc.) with their types. `register_rt_aliases(eng)` adds ~50 short-name aliases.
3. **Struct map:** `tc_set_struct_map(eng, smap)` loads named struct definitions so the type checker can constrain `double` fields.
4. **Constructor registration:** `tc_register_ctors(eng, vmap, struct_map)` registers enum/data constructor types.
5. **Extern registration:** `tc_register_externs(eng, externs, 0)` parses extern signature strings and registers them.
6. **Pre-registration:** `tc_pre_register(eng, parsed, 0)` gives each function a fresh function type (based on parameter count) so mutually recursive functions can reference each other.
7. **Warm-up pass:** `tc_infer_loop_warm` runs inference where all free variables are generalized (no environment subtraction). This builds up increasingly accurate types for multi-pass convergence.
8. **Reset:** `eng_reset_subst` and error clearing.
9. **Full inference:** `tc_infer_loop_sigs` processes each function:
   - Initialize tmap for AST size
   - `infer_fn_def`: push scope → create fresh param types → infer body → pop scope → build curried function type
   - `generalize_raw`: collect free vars, subtract env free vars, wrap in forall
   - Insert generalized type into env
   - Resolve tmap to final type tags
   - Produce `{fn_tmap, fn_ty, fn_ty_raw}` result
10. **Error reporting:** `tc_report_errors` prints warnings to stderr.

### Expression inference

`infer_expr` dispatches on expression kind through a three-level chain (`infer_expr_core` → `infer_expr2` → `infer_expr3`):

| Expression | Inference rule |
|------------|---------------|
| Int literal | → `ty_int` |
| Float literal | → `ty_float` |
| String/interp | → `ty_str` |
| Bool literal | → `ty_bool` |
| Identifier | → env lookup + instantiate forall types |
| Binary op | Infer both sides, dispatch on operator for result type |
| Unary op | Infer operand, `-` preserves type, `!` → `ty_bool` |
| Call | Infer callee as `arg → ret`, unify `arg` with actual argument, return `ret` |
| Field access | Create `{field: ?a, ..?b}`, unify with subject, return `?a` |
| Index | If map → value type; if array → element type |
| Array | Infer first element, unify all elements, return `[T]` |
| Record | Infer field values, build `ty_record` with `ty_field` entries |
| Map | Fresh key/value types, unify all pairs |
| Lambda | Like `infer_fn_def` (push scope, params, body, pop scope) |
| Match | Infer scrutinee, unify all arm body types, check patterns |
| Block | Push scope, infer statements, last expression is block's type |
| Pipe `\|>` | `left \|> right` means `right(left)`: unify `right` with `left_ty → ?ret` |
| Compose `>>` | `f >> g`: unify `f: a→b`, `g: b→c`, return `a→c` |
| Propagate `?` | Unwrap Result or Optional |
| Unwrap `!` | Unwrap Result or Optional |

### Unification

**`unify eng t1 t2`** — The core operation. Walks both types via `subst_walk`, handles:

- **Variable vs. anything:** Bind the variable to the other type (with occurs check via `ty_contains_var`).
- **Same primitive tags:** OK (int=int, str=str, etc.).
- **Int ↔ Bool:** Allowed (both represented as i64 at runtime).
- **Never:** Unifies with anything (bottom type).
- **Function:** Recursively unify parameter and return types.
- **Array/Optional/Result:** Recursively unify element types.
- **Map:** Recursively unify key and value types.
- **Record:** Row-polymorphic unification via `unify_records`.
- **Tuple:** Element-wise unification.
- **Mismatch:** Push error message.

### Row polymorphism

Records carry a row variable (`n1` field of `ty_record`). When unifying two records:

1. `unify_fields_against` finds matching fields by name and unifies their types.
2. Extra fields from either side are collected.
3. `unify_row_vars` handles open records: if a record has a row variable, bind it to a new record containing the other side's extra fields (with its own fresh row variable for further extension).

This enables structural subtyping: a function expecting `{x: I}` can receive `{x: I, y: S}`.

### Substitution (union-find)

- **`subst_fresh_var`** — Allocates a fresh variable ID, extends `parent` and `bindings` arrays.
- **`subst_find`** — Path-compressed union-find: follows `parent` pointers to the root, compresses the path.
- **`subst_walk`** — One-level resolution: if `ty_var`, finds root, returns binding or self.
- **`subst_bind`** — Binds a variable to a type. If target is also a variable, merges via union-find. Otherwise, performs occurs check and sets binding.
- **`subst_resolve` / `sr_inner`** — Deep resolution: recursively walks and rebuilds the type tree, following all bindings. Depth-limited to 30 to prevent infinite loops on cyclic types.

### Generalization and instantiation (let-polymorphism)

**Generalize:** After inferring a function's type, collect all free type variables (`tc_collect_fv`), subtract the environment's free variables (`efv_loop` + `subtract_vars_bs`), and wrap the remaining variables in a `ty_forall` node. This makes the function polymorphic.

**Instantiate:** At each use site of a polymorphic function, `instantiate` creates fresh type variables for all bound variables in the forall and substitutes them throughout the type body (`inst_type`). This gives each call site independent type variables.

Both `tc_collect_fv` and `inst_type` use **bitset-based cycle detection** to avoid infinite loops on cyclic type graphs.

### Environment

The environment is a scope stack implemented as parallel arrays:

- **`env_insert name type`** — Appends name/type pair.
- **`env_lookup name`** — Linear scan from end (most recent first) for shadowing.
- **`env_push`** — Saves current length as a mark.
- **`env_pop`** — Truncates back to the saved mark.
- **`env_nullify name`** — Sets all entries with given name to -1 (used during warm inference to avoid self-reference).

### Struct map constraints

For C codegen, the struct map knows which record fields are `double` (float). When the type checker encounters a record literal or field access, it checks the struct map and constrains the field's type variable to `ty_float` if it maps to a `double` C type. This ensures the codegen emits correct float bitcast operations.

---

## MIR Lowering

**74 functions, 9,648 tokens.** Namespace: `lower`.

MIR (Mid-level Intermediate Representation) is a **flat control-flow graph** of basic blocks, where each block contains a sequence of statements and ends with a terminator. The lowering phase translates the AST into this representation.

### MIR structure

**Operand** — A value reference:

```
{okind: I, oval: I, ostr: S}
```

| Kind | Value | Description |
|------|-------|-------------|
| `ok_local` | 1 | Local variable `_N` |
| `ok_const_int` | 2 | Integer literal |
| `ok_const_bool` | 3 | Boolean literal |
| `ok_const_str` | 4 | String literal |
| `ok_const_unit` | 5 | Unit/void |
| `ok_func_ref` | 6 | Named function reference |
| `ok_const_float` | 7 | Float literal (value as string in `ostr`) |

**Statement** — An operation that assigns to a local:

```
{sdest: I, skind: I, sival: I, sstr: S, sop1: Op, sop2: Op, sops: [Op]}
```

| Kind | Value | Description |
|------|-------|-------------|
| `rv_use` | 1 | Copy: `_dest = src` |
| `rv_binop` | 2 | Binary op: `_dest = lhs OP rhs` |
| `rv_unop` | 3 | Unary op: `_dest = OP src` |
| `rv_call` | 4 | Function call: `_dest = f(args...)` |
| `rv_aggregate` | 5 | Construct array/record/variant |
| `rv_field` | 6 | Field access: `_dest = base.field` |
| `rv_index` | 7 | Array index: `_dest = arr[idx]` |
| `rv_str_interp` | 8 | String interpolation |
| `rv_make_closure` | 9 | Closure construction |

Aggregate sub-kinds (in `sival`): `ag_array`(2), `ag_record`(3), `ag_variant`(4), `ag_record_update`(5).

**Terminator** — Ends a basic block:

```
{tkind: I, top: Op, tgt1: I, tgt2: I}
```

| Kind | Value | Description |
|------|-------|-------------|
| `tm_goto` | 1 | Unconditional jump to `tgt1` |
| `tm_branch` | 2 | If `top` then `tgt1` else `tgt2` |
| `tm_return` | 4 | Return `top` from function |
| `tm_unreachable` | 5 | Trap (non-exhaustive match, etc.) |

### Lowering state

The lowering context (`mk_mir_lower`) contains 19 fields:

```
{block_stmts: [],      -- parallel array of blocks, each a list of statements
 block_terms: [],      -- parallel array of block terminators
 cur_block: ref(0),    -- current block being emitted into
 fn_entry: ref(0),     -- entry block index
 fn_name: S,           -- function name
 fn_params: [],        -- parameter local IDs
 lambda_ctr: ref(0),   -- counter for unique lambda names
 lifted_fns: [],       -- accumulated lifted lambda MIR functions
 local_names: [],      -- local variable names (parallel array)
 local_types: [],      -- local variable types (0=unknown, 1=int, 3=float, 4=str, 5=bool, 6=map)
 match_sibs: ...,      -- variant sibling info for exhaustiveness
 nxt_block: ref(0),    -- next block ID to allocate
 nxt_local: ref(0),    -- next local ID to allocate
 outer_fn_name: S,     -- outermost function name (for lambda naming)
 tctx: ...,            -- type context from HM inference (type map)
 var_locals: [],       -- variable-to-local mapping
 var_marks: [],        -- scope boundary markers
 var_names: [],        -- variable name scope stack
 vmap: ...,            -- variant discriminant map
 za_fns: []}           -- zero-argument function names
```

Infrastructure: `mir_alloc_local` allocates locals, `mir_new_block` allocates blocks, `mir_emit` appends statements, `mir_terminate` sets terminators, `mir_switch_block` changes the current emission target, `mir_push_scope`/`mir_pop_scope`/`mir_bind_var`/`mir_lookup_var` manage the variable scope stack.

### Expression lowering

`lower_expr` dispatches through three functions (`lower_expr`, `lower_expr2`, `lower_expr3`) to handle all 22 expression kinds:

**Literals:** Int, string, bool, and unit produce zero MIR instructions — they're just operands (`mk_op_int`, `mk_op_str`, etc.). Float literals allocate a local and emit `rv_use`.

**Identifiers (`lower_ident`):** Three cases: (1) local variable found → `mk_op_local(id)`, (2) zero-arg function → emit a call, (3) named function → `mk_op_func(name)`.

**Binary ops:** Type-directed dispatch. If either operand is string (type 4) → `lower_str_binop` (emits `str_concat`/`str_eq`). If either is float (type 3) → `lower_float_binop` (emits bitcast wrappers). Otherwise → `rv_binop`.

**Calls (`lower_call`):** Three paths: (1) constructor call → `rv_aggregate` with `ag_variant`, (2) field accessor call → `rv_field`, (3) normal call → `rv_call` (with monomorphization lookup for specialization).

**Pipe (`|>`):** `a |> f` lowers to `call f(a)`.

**Compose (`>>`):** `f >> g` creates a synthetic closure `\x -> g(f(x))` with both `f` and `g` captured.

**Blocks:** Push scope, lower all statements, pop scope. Last statement's result is the block's value.

**Records:** New records → `rv_aggregate` with `ag_record` and field names in `sstr`. Updates (`rec{f:v}`) → copy existing fields from base, overwrite updated ones.

**Error propagation (`?`):** Extract tag (field 0), branch: if error → early return, if ok → extract payload (field 1).

**Unwrap (`!`):** Extract tag, branch: if error → panic, if ok → extract payload.

### Pattern match compilation

`lower_match` allocates a result local and merge block, then dispatches to specialized handlers per pattern kind:

**Wildcard:** No conditional — lower body directly, goto merge.

**Identifier:** Bind scrutinee to a new local, then like wildcard.

**Integer/Boolean:** Compare scrutinee to literal, branch to body or next arm.

**String:** Call `glyph_str_eq(scrutinee, pattern)`, branch on result.

**Constructor:** Extract tag via field 0, compare to expected discriminant, bind sub-pattern variables to payload fields (`__payload0`, `__payload1`, ...).

**Or-pattern:** Test each sub-pattern's condition; success from any jumps to the body block.

**Guards:** All pattern handlers support guards. If a guard is present: after the pattern matches, evaluate the guard expression. If true → run body. If false → fall through to next arm.

### Closure compilation (lambda lifting)

`lower_lambda` performs lambda lifting:

1. **Free variable analysis:** `walk_free_vars` recursively walks the lambda body to find identifiers bound in the enclosing scope but not by the lambda's own parameters.
2. **Fresh context:** A new lowering context is created for the lambda with a unique name (`outer_fn_name + "_lam_" + counter`).
3. **Environment parameter:** The lambda receives `__env` as its first parameter (the closure environment pointer).
4. **Capture loads:** For each captured variable, emit `rv_field` to extract from `__env` at positions `__cap0`, `__cap1`, etc.
5. **Body lowering:** Lower the lambda body in the new context.
6. **Closure construction:** In the parent context, emit `rv_make_closure` with the lambda function name and capture operands.
7. **Lifted functions:** The lambda's MIR is added to the parent's `lifted_fns` list. Nested lambdas are collected transitively.

### MIR result

`lower_fn_def` returns a record with 8 fields:

```
{fn_name: S,              -- function name
 fn_params: [I],           -- parameter local IDs
 fn_locals: [S],           -- all local names
 fn_blocks_stmts: [[[S]]], -- per-block statement lists
 fn_blocks_terms: [[T]],   -- per-block terminators
 fn_entry: I,              -- entry block index
 fn_subs: [MIR],           -- lifted lambda functions
 fn_types: [I]}            -- local type information
```

---

## Monomorphization

**53 functions, 6,060 tokens.** Namespace: `mono`.

The monomorphization pass specializes generic (polymorphic) functions with concrete types at each call site. A function like `map : (a → b) → [a] → [b]` called with integers becomes `map__int_str : (I → S) → [I] → [S]`.

### Four-phase process

**Phase 1: Collection (`mono_collect`)**

Walks all parsed function ASTs looking for calls to polymorphic functions (those with `ty_forall` types). For each call site:

1. Get the callee's template forall type from the type pool.
2. Get the concrete type at the call site (after inference).
3. `mono_build_var_map` structurally walks both type trees, pairing each type variable with its concrete type.
4. `mono_spec_name` generates a name like `map__int_str` from the original name and the concrete type suffixes.
5. Add to the specs list.

**Phase 2: Propagation (`mono_propagate`)**

Iterative fixed-point (max 20 rounds). For each existing specialization, walks its AST looking for calls to other polymorphic functions. The concrete type bindings from the outer specialization flow into inner calls, potentially discovering new specializations. Continues until no new specs are found.

**Phase 3: Deduplication (`mono_dedup_specs`)**

Removes duplicate specializations with the same original function + spec name. Merges caller information from duplicates.

**Phase 4: Compilation (`mono_compile_specs`)**

For each specialization:

1. Find the original parsed function.
2. Build a specialized type map by substituting concrete types for variables.
3. Build a call map redirecting generic call sites to specialized callee names.
4. Re-lower the function via `lower_fn_def` with the spec name and specialized type context.

### Specialization naming

`mono_type_name` maps type indices to name suffixes:

| Type | Suffix |
|------|--------|
| `ty_int` | `int` |
| `ty_str` | `str` |
| `ty_bool` | `bool` |
| `ty_float` | `float` |
| `ty_void` | `void` |
| `ty_array(T)` | `arr_T` |
| `ty_opt(T)` | `opt_T` |
| `ty_fn(A,B)` | `fn_A_B` |
| `ty_record` | `rec` |
| `ty_map(K,V)` | `map_K_V` |

Example: `fold : (b → a → b) → b → [a] → b` called with `(I → S → I) → I → [S] → I` becomes `fold__fn_int_fn_str_int_int_str`.

---

## Tail Call Optimization

**11 functions, 983 tokens.** Namespace: `tco`.

TCO is a **MIR-to-MIR transformation** that converts self-recursive tail calls into loops. It operates on completed MIR functions, after all lowering is done.

### Detection

`tco_opt_blks` scans each block for this exact MIR pattern:

```
bb_N:
  ...                                    -- arbitrary preceding statements
  _X = call fn:<self_name>(args...)      -- self-recursive call
  _Y = use(_X)                           -- copy result
  goto bb_M                              -- unconditional jump
```

where `bb_M` is a **return trampoline** (0 statements, terminator is `return _Y`).

Eight nested conditions are checked: block has ≥2 statements, second-to-last is `rv_call`, callee is `ok_func_ref` matching the function name, last is `rv_use` of the call result, terminator is `tm_goto` to a return block.

### Transformation

When detected, `tco_transform`:

1. **Allocates temporary locals:** N new temporaries (`tco_tmp_0`, `tco_tmp_1`, ...) at the end of `fn_locals`.
2. **Copies preceding statements:** All statements except the last 2 (the call and the use).
3. **Copies arguments to temporaries:** `tco_tmp_i = use(arg_i)` — prevents aliasing when arguments reference parameters that will be overwritten.
4. **Copies temporaries to parameters:** `param_i = use(tco_tmp_i)` — overwrites function parameters with new values.
5. **Replaces terminator:** `goto fn_entry` — jumps back to the entry block, creating a loop.

### Example

Before:
```
fn factorial(n, acc):
  bb0:  _2 = eq(n, 0)
        branch _2 → bb1, bb2
  bb1:  _3 = use(acc)
        goto bb3
  bb2:  _4 = sub(n, 1)
        _5 = mul(acc, n)
        _6 = call fn:factorial(_4, _5)   ← tail call
        _7 = use(_6)
        goto bb3
  bb3:  return _7
```

After:
```
fn factorial(n, acc):
  bb0:  _2 = eq(n, 0)
        branch _2 → bb1, bb2
  bb1:  _3 = use(acc)
        goto bb3
  bb2:  _4 = sub(n, 1)
        _5 = mul(acc, n)
        _8 = use(_4)          ← copy to temp
        _9 = use(_5)          ← copy to temp
        _0 = use(_8)          ← overwrite n
        _1 = use(_9)          ← overwrite acc
        goto bb0               ← loop back
  bb3:  return _7
```

### Limitations

- Only handles **direct self-recursion** (callee must match function name exactly).
- Only catches the specific MIR pattern: call + use + goto-to-return-trampoline.
- No mutual recursion (A calls B calls A).
- No indirect/closure tail calls.

---

## C Code Generation

**142 functions, 25,695 tokens.** Namespace: `codegen`.

The C backend translates MIR into C source code that compiles with any C compiler (gcc, clang, tcc). The universal value type is `GVal` (`typedef intptr_t GVal`).

### Function generation (`cg_function2`)

Each MIR function becomes a C function:

```c
GVal function_name(GVal _0, GVal _1) {
    _glyph_current_fn = "function_name";    // function tracking
    GVal _2 = 0; GVal _3 = 0;              // local declarations
    bb_0:                                    // entry block label
        _2 = _0 + _1;                       // statements
        return _2;                           // terminator
}
```

Named struct types use typed returns: `Glyph_Point* make_point(GVal _0, GVal _1)`.

### Statement emission (`cg_stmt` / `cg_stmt2`)

`cg_stmt2` handles type-aware dispatch first (float binops → bitcast wrappers, string equality → `glyph_str_eq` calls), then falls through to `cg_stmt` for the base cases:

| MIR Statement | Generated C |
|---------------|-------------|
| `rv_use` | `_N = src;` (with casts for struct types) |
| `rv_binop` | `_N = op1 + op2;` (or float: `_glyph_f2i(_glyph_i2f(a) + _glyph_i2f(b))`) |
| `rv_unop` | `_N = -op;` or `_N = !op;` |
| `rv_call` (direct) | `_N = fname(args);` |
| `rv_call` (indirect) | Extract fn_ptr from closure, cast, call with closure as first arg |
| `rv_field` | `_N = ((GVal*)base)[offset];` (with tag/null checks in debug) |
| `rv_index` | Bounds check + `_N = ((GVal*)hdr[0])[idx];` |
| `rv_aggregate(array)` | Heap alloc header (24B) + data, store elements |
| `rv_aggregate(record)` | Heap alloc `GVal*` array, store fields alphabetically |
| `rv_aggregate(variant)` | Zero payload: `disc * 2 + 1` (tagged immediate). With payload: heap alloc `{disc, payload...}` |
| `rv_aggregate(update)` | Alloc new struct, memcpy old, overwrite updated fields |
| `rv_str_interp` | `sb_new(); sb_append()...; sb_build();` |
| `rv_make_closure` | Heap alloc `{fn_ptr, cap1, cap2, ...}` |

### Terminator emission (`cg_term`)

| MIR Terminator | Generated C |
|----------------|-------------|
| `tm_goto` | `goto bb_N;` |
| `tm_return` | `return operand;` |
| `tm_branch` | `if (cond) goto bb_T; else goto bb_F;` |
| `tm_unreachable` | `glyph_panic("non-exhaustive match");` |

### Operand rendering (`cg_operand`)

| Operand | Generated C |
|---------|-------------|
| `ok_local` | `_N` |
| `ok_const_int` | `(GVal)42` |
| `ok_const_bool` | `0` or `1` |
| `ok_const_str` | `(GVal)glyph_cstr_to_str((GVal)"escaped")` |
| `ok_func_ref` | `(GVal)&function_name` |
| `ok_const_float` | `_glyph_f2i(3.14)` |
| `ok_const_unit` | `0` |

### Closure codegen

**Construction:** Heap-allocate `{fn_ptr, cap1, cap2, ...}`, store function pointer at slot 0 and captured values at subsequent slots.

```c
{ GVal* __c = (GVal*)glyph_alloc(N*8);
  __c[0] = (GVal)&lifted_lambda;
  __c[1] = captured_x;
  __c[2] = captured_y;
  _dest = (GVal)__c; }
```

**Indirect call:** Extract fn_ptr, cast to function pointer type, call with closure as hidden first argument.

```c
{ GVal __fp = ((GVal*)closure)[0];
  _dest = ((GVal(*)(GVal,GVal,GVal))__fp)(closure, arg1, arg2); }
```

### Runtime name mapping

`mk_runtime_set` builds a frozen hash map of ~85 runtime function names. `cg_fn_name` checks if a name is in this set; if so, prefixes with `glyph_` (e.g., `println` → `glyph_println`). This prevents user-defined functions from colliding with runtime functions.

### Extern wrappers

For user-declared externs (in `extern_` table), `cg_extern_wrapper` generates C wrapper functions bridging `GVal` calling convention to C ABI. Void-returning externs wrap with `return 0;`. Zero-arg externs get a dummy parameter.

### Program assembly (`cg_program`)

The full C output is assembled as:

1. `cg_preamble` — `#include` directives, `GVal` typedef, extern declarations for all runtime functions
2. `cg_all_typedefs` — `typedef struct` for named types + enum variant typedefs
3. `cg_forward_decls` — Forward declarations for all compiled functions
4. All function bodies
5. `cg_main_wrapper` — `int main()` calling `GC_INIT()`, signal handlers, `glyph_set_args()`, `glyph_main()`

The build pipeline wraps this with: `[cc_prepend file] + [full runtime C] + [extern wrappers] + [program C code]`, writes to `/tmp/glyph_out.c`, and invokes `cc`.

---

## LLVM IR Code Generation

**93 functions, 13,494 tokens.** Namespace: `llvm`.

The LLVM backend translates MIR into LLVM IR text (`.ll` files). It shares the same front-end pipeline as the C backend through TCO, then diverges to emit LLVM IR instead of C.

### Key differences from C codegen

1. **Memory model:** All locals are `alloca`'d and accessed via `load`/`store` (memory-form SSA, not register SSA). Locals are named `%v0`, `%v1`, etc.
2. **String constants:** Extracted per-function into module-level globals (`@str_funcname_0 = private unnamed_addr constant [N x i8] c"..."`). References use `getelementptr` + `ptrtoint` + `call @glyph_cstr_to_str`.
3. **Typed struct access:** Named structs use `getelementptr %Glyph_T, ptr %base, i32 0, i32 field_idx` for proper LLVM field indexing. Anonymous records fall back to raw offset GEP.
4. **Intermediate SSA names:** Block-unique names using block ID encoding: `%btmp_123`, `%ftmp1_123`, `%cloptr_123`.

### Function structure

```llvm
@str_fname_0 = private unnamed_addr constant [6 x i8] c"hello\00"

define i64 @fname(i64 %p0, i64 %p1) {
  %v0 = alloca i64, align 8         ; local declarations
  %v1 = alloca i64, align 8
  store i64 %p0, ptr %v0            ; store params
  store i64 %p1, ptr %v1
  br label %bb_0                     ; jump to entry block

bb_0:
  %load_0 = load i64, ptr %v0       ; statements
  %btmp_0 = add i64 %load_0, 42
  store i64 %btmp_0, ptr %v1
  ret i64 %btmp_0                   ; terminator
}
```

### Statement emission

**Integer arithmetic:**
```llvm
%btmp_N = add i64 %op1, %op2
store i64 %btmp_N, ptr %v_dest
```

**Float arithmetic** (bitcast wrappers):
```llvm
%ftmp1_N = bitcast i64 %op1 to double
%ftmp2_N = bitcast i64 %op2 to double
%btmp_N = fadd double %ftmp1_N, %ftmp2_N
%fbc_N = bitcast double %btmp_N to i64
store i64 %fbc_N, ptr %v_dest
```

**Comparisons** (i1 → i64 zero-extension):
```llvm
%btmp_N = icmp slt i64 %op1, %op2
%zext_N = zext i1 %btmp_N to i64
store i64 %zext_N, ptr %v_dest
```

**Direct calls:**
```llvm
%res_N = call i64 @fname(i64 %arg0, i64 %arg1)
store i64 %res_N, ptr %v_dest
```

**Indirect (closure) calls:**
```llvm
%cloptr_N = inttoptr i64 %closure_val to ptr
%fpslot_N = getelementptr i64, ptr %cloptr_N, i64 0
%fpi64_N = load i64, ptr %fpslot_N
%fpptr_N = inttoptr i64 %fpi64_N to ptr
%res_N = call i64 (i64, i64, ...) %fpptr_N(i64 %closure_val, i64 %arg1)
```

**Named struct field access:**
```llvm
%fptr_N = getelementptr %Glyph_Point, ptr %base, i32 0, i32 1
%fval_N = load i64, ptr %fptr_N
```

### LLVM instruction mapping

| MIR Op | LLVM Integer | LLVM Float |
|--------|-------------|------------|
| add | `add` | `fadd` |
| sub | `sub` | `fsub` |
| mul | `mul` | `fmul` |
| div | `sdiv` | `fdiv` |
| mod | `srem` | `frem` |
| eq | `icmp eq` | `fcmp oeq` |
| lt | `icmp slt` | `fcmp olt` |
| bitand | `and` | — |
| shl | `shl` | — |
| shr | `ashr` | — |

### Program assembly

```llvm
target triple = "x86_64-pc-linux-gnu"
target datalayout = "e-m:e-p270:32:32-..."

%Glyph_Point = type { i64, i64 }                    ; struct declarations

declare i64 @glyph_alloc(i64)                        ; runtime declarations
declare i64 @glyph_println(i64)

define i64 @func1(i64 %p0) { ... }                   ; compiled functions

define i32 @main(i32 %argc, ptr %argv) {              ; main wrapper
  call void @glyph_set_args(i32 %argc, ptr %argv)
  %ret = call i64 @glyph_main()
  %ret32 = trunc i64 %ret to i32
  ret i32 %ret32
}
```

`ll_auto_declares` collects all function calls, subtracts defined + runtime + extern names, and emits `declare` for any remaining (catches monomorphized specializations, data functions, etc.).

### LLVM build pipeline

The LLVM backend writes two files:
- `/tmp/glyph_out.ll` — LLVM IR text (all compiled functions)
- `/tmp/glyph_runtime.c` — C runtime + extern wrappers

Then invokes: `clang /tmp/glyph_runtime.c /tmp/glyph_out.ll -o output -no-pie -lm -lgc [lib_flags]`

---

## Build Orchestration

**43 functions, 6,027 tokens.** Namespace: `build`.

### Top-level entry (`compile_db`)

```
compile_db db_path output_path mode gen_str cli_cc_args =
  1. Open database, read lib dependencies
  2. Read fn sources, signatures, externs, types, consts, data definitions
  3. Read cc_prepend and cc_args from meta table
  4. Load library dependencies (fn/type/extern defs from linked .glyph files)
  5. Build full struct map from type definitions
  6. Close database
  7. Check for main function
  8. Dispatch to backend:
     - mode 20    → emit C source only (no compile)
     - mode >= 10 → build_program_llvm (LLVM IR backend)
     - otherwise  → build_program (C codegen backend)
  9. Collect dependency pairs, insert into dep table
```

Mode encoding: `0`=default C, `1`=debug C, `2`=release C, `10`=default LLVM, `11`=debug LLVM, `12`=release LLVM, `20`=emit C source only.

### Test building (`build_test_program`)

Similar to `build_program` but:
- Parses fn sources and test sources separately
- Infers types for both
- Combines for monomorphization
- Generates a test dispatch main that iterates test names, calls each, tracks pass/fail
- Optional `--cover` flag: adds `#ifdef GLYPH_COVERAGE` instrumentation
- Optional `--seed`/`--trials` for property-based tests

### CLI commands

Entry point: `main` reads `argv`, dispatches through `dispatch_cmd` → `dispatch_cmd2` → `dispatch_cmd3` (three chained match functions handling 33 commands total).

Key commands: `cmd_build` (parses flags, calls `compile_db`), `cmd_run` (builds to temp, executes), `cmd_test` (builds test binary, runs it), `cmd_put` (insert/update definition with parse validation), `cmd_check` (type-check all definitions).

---

## C Runtime

The C runtime is embedded as string constants inside the compiler and concatenated by `cg_runtime_full`. It consists of ~15 modules:

```
cg_runtime_full = cg_runtime_c + cg_runtime_args + cg_runtime_sb + cg_runtime_raw +
                  cg_runtime_io + cg_runtime_extra + cg_runtime_float + cg_runtime_math +
                  cg_runtime_result + cg_runtime_mcp + cg_runtime_bitset +
                  cg_runtime_arr_util + cg_runtime_map + cg_runtime_str2 +
                  cg_runtime_freeze + cg_runtime_ref + [cg_runtime_sqlite if needed]
```

### Memory management

All allocation goes through Boehm GC via preprocessor redirects:
```c
#define malloc(sz) GC_malloc(sz)
#define realloc(p,sz) GC_realloc(p,sz)
#define free(p) (void)0   // GC handles deallocation
```

`glyph_alloc(size)` wraps malloc with OOM panic. `glyph_realloc(ptr, size)` wraps realloc.

### Signal handlers

Always installed (not debug-only):
- `_glyph_sigsegv` — SIGSEGV: prints `"segfault in <fn>"` + stack trace (debug mode shows all frames)
- `_glyph_sigfpe` — SIGFPE: prints `"division by zero in <fn>"`
- `_glyph_current_fn` — always tracks the currently executing function name
- `_glyph_call_stack[256]` + `_glyph_call_depth` — debug-only call stack for stack traces

### String representation

Fat pointer: `{char* ptr, int64_t len}` (16 bytes, heap-allocated).

| Function | Description |
|----------|-------------|
| `glyph_str_eq` | Length check + memcmp, null-safe |
| `glyph_str_len` | Returns len, null returns 0 |
| `glyph_str_char_at` | Bounds-checked, returns -1 on OOB |
| `glyph_str_slice` | Clamped bounds, allocates new string |
| `glyph_str_concat` | Allocates new, memcpy both halves |
| `glyph_int_to_str` | snprintf to buffer |
| `glyph_cstr_to_str` | strlen + memcpy |
| `glyph_str_to_int` | Manual digit parsing with sign |
| `glyph_str_to_cstr` | Allocates null-terminated copy |
| `glyph_str_split` | Split by delimiter string |
| `glyph_str_index_of` | Find substring position |
| `glyph_str_starts_with` / `_ends_with` | Prefix/suffix check |
| `glyph_str_trim` | Trim whitespace |
| `glyph_str_to_upper` | Uppercase conversion |
| `glyph_str_from_code` | Code point to single-char string |

### Array representation

Header: `{int64_t* data, int64_t len, int64_t cap}` (24 bytes, heap-allocated). Data is a separate heap allocation.

| Function | Description |
|----------|-------------|
| `glyph_array_push` | Doubling growth, frozen check |
| `glyph_array_len` | Direct header read |
| `glyph_array_set` | Bounds-checked, frozen check |
| `glyph_array_pop` | Empty check, frozen check |
| `glyph_array_bounds_check` | Panic with index/len on failure |
| `glyph_array_reverse` | In-place reverse |
| `glyph_array_slice` | Create sub-array |
| `glyph_array_index_of` | Linear search |
| `glyph_array_freeze` / `_thaw` | Immutability via sign bit in cap |
| `glyph_generate` | Build frozen array from function: `generate(n, f)` |

### StringBuilder

Buffer `{char* buf, int64_t len, int64_t cap}`:

| Function | Description |
|----------|-------------|
| `glyph_sb_new` | 64-byte initial buffer |
| `glyph_sb_append` | Doubling growth, memcpy |
| `glyph_sb_build` | Allocate final string, free buffer |

Used for string interpolation: `"hello {name}"` → `sb_new(); sb_append("hello "); sb_append(name); sb_build()`.

### Float support

Floats stored as i64 via bitcast:
```c
static inline double _glyph_i2f(int64_t i) { double d; memcpy(&d, &i, 8); return d; }
static inline int64_t _glyph_f2i(double d) { int64_t i; memcpy(&i, &d, 8); return i; }
```

Math functions (sin, cos, sqrt, atan2, pow, floor, ceil, etc.) take/return GVal with bitcast wrappers.

### Hash map

Open-addressing with FNV-1a hashing. Layout: header `{GVal* data, int64_t len, int64_t cap}`, data array of `{key, value, state}` triples. States: 0=empty, 1=occupied, 2=tombstone. Resize at 70% load.

| Function | Description |
|----------|-------------|
| `hm_new` | Create empty map |
| `hm_set` / `hm_get` / `hm_del` / `hm_has` | CRUD operations |
| `hm_keys` / `hm_len` | Enumeration |
| `hm_freeze` / `hm_frozen` | Immutability |

### Result type

`Ok(val)` and `Err(msg)` are 2-slot heap allocations: `{tag, payload}` where tag=0 for Ok, tag=1 for Err.

### Other modules

| Module | Contents |
|--------|----------|
| `cg_runtime_args` | `glyph_set_args` / `glyph_args` — argc/argv handling |
| `cg_runtime_io` | File I/O, stdin reading, `glyph_system` |
| `cg_runtime_ref` | `ref(val)` / `deref(r)` / `set_ref(r, val)` — mutable cells |
| `cg_runtime_bitset` | `bitset_new` / `bitset_set` / `bitset_test` — for cycle detection |
| `cg_runtime_sqlite` | SQLite bindings (conditional) |
| `cg_test_runtime` | `assert` / `assert_eq` / `assert_str_eq` |
| `cg_runtime_coverage` | Function-level instrumentation for `--cover` |

---

## Namespace Map

The compiler is organized into namespaces derived from function name prefixes:

| Namespace | Functions | Tokens | Role |
|-----------|-----------|--------|------|
| `codegen` | 142 | 25,695 | C code generation |
| `typeck` | 146 | 13,472 | HM type inference |
| `llvm` | 93 | 13,494 | LLVM IR backend |
| `parser` | 126 | 12,039 | Recursive-descent parser |
| `mcp` | 72 | 11,558 | MCP server (26 JSON-RPC tools) |
| `lower` | 74 | 9,648 | AST → MIR lowering |
| `cli` | 40 | 9,541 | 33 CLI commands |
| `mono` | 53 | 6,060 | Monomorphization |
| `build` | 43 | 6,027 | Build orchestration |
| `tokenizer` | 84 | 5,476 | Indentation-sensitive lexer |
| `json` | 56 | 3,195 | JSON parser/builder (for MCP) |
| `register` | 3 | 3,045 | Builtin type registration |
| `mir` | 100 | 2,836 | MIR data structures + constructors |
| `mk` | 24 | 2,431 | Factory functions |
| `read` | 24 | 2,080 | Database reading |
| `builtin` | 19 | 1,969 | Builtin type analysis |
| `compile` | 8 | 1,545 | Compilation driver |
| `find` | 16 | 1,405 | Search/lookup |
| `exhaust` | 12 | 1,397 | Match exhaustiveness checking |
| `util` | 32 | 1,231 | String helpers, SQL escaping |
| `emit` | 6 | 1,196 | Code emission |
| `link` | 9 | 1,132 | Library linking |
| `tco` | 11 | 983 | Tail call optimization |

### Test distribution (403 tests)

| Namespace | Tests |
|-----------|-------|
| general (unnamespaced) | 270 |
| parser | 22 |
| mir | 22 |
| typeck | 19 |
| util | 17 |
| lower | 17 |
| tokenizer | 10 |
| codegen | 10 |
| build | 9 |
| json | 5 |
| tco | 1 |
| mcp | 1 |

---

## Data Structures

### Types (13 definitions)

| Name | Definition | Used by |
|------|-----------|---------|
| `AstNode` | `{kind:I, ival:I, sval:S, n1:I, n2:I, n3:I, ns:[I]}` | Parser, type checker, lowering |
| `Token` | `{kind:I, start:I, end:I, line:I}` | Tokenizer, parser |
| `ParseResult` | `{node:I, pos:I}` | Parser (extended with `pr_err` at runtime) |
| `JNode` | `{items:[I], keys:[S], nval:I, sval:S, tag:I}` | JSON subsystem (MCP) |
| `Color` | `Red(I) \| Green(I) \| Blue(I)` | Test enum |
| `Dir` | `North \| South \| East \| West` | Test enum |
| `Light` | `LOff \| LOn(I) \| LDim(I)` | Test enum |
| `FPoint` | `{fx: f64, fy: f64}` | Float struct test |
| `FPoint32` | `{fx: f32, fy: f32}` | 32-bit float struct test |
| `Point2D` | `{x: i32, y: i32}` | 32-bit int struct test |
| `StrBox` | `{val: S}` | Wrapped string test |
| `StrComp` | `SComp(StrBox)` | Nested struct test |
| `StrTag` | `SVal(S)` | Tagged string test |

### Anonymous record types (used pervasively)

Many compiler-internal data structures are anonymous records, not named types:

- **TyNode:** `{tag:I, n1:I, n2:I, ns:[I], sval:S}` — type pool nodes
- **Engine:** `{ty_pool:[], parent:[], bindings:[], next_var:ref(I), env_names:[], env_types:[], env_marks:[], errors:[], tmap:ref([]), z_smap:[]}` — type inference state
- **MIR lowering context:** 19-field record (see [MIR Lowering](#lowering-state))
- **MIR function result:** `{fn_name:S, fn_params:[I], fn_locals:[S], fn_blocks_stmts:[], fn_blocks_terms:[], fn_entry:I, fn_subs:[], fn_types:[I]}`
- **Mono spec:** `{ms_caller_fns:[S], ms_caller_nis:[I], ms_orig:S, ms_spec:S, ms_var_map:[I]}`
- **Operand:** `{okind:I, oval:I, ostr:S}`
- **Statement:** `{sdest:I, skind:I, sival:I, sstr:S, sop1:Op, sop2:Op, sops:[Op]}`
- **Terminator:** `{tkind:I, top:Op, tgt1:I, tgt2:I}`

### Constants (2 definitions)

| Name | Value |
|------|-------|
| `glyph_version` | `dev` |
| `glyph_repo` | `IanTrudel/Glyph` |

---

## Dependency Graph

The compiler has **2,066 dependency edges** (all of type `calls` — no `uses_type`, `implements`, or `field_of` edges are populated).

### Most complex functions (by outgoing dependencies)

| Function | Dependencies | Role |
|----------|-------------|------|
| `parse_atom` | 35 | Core atom parser |
| `parse_postfix_loop` | 21 | Postfix chain parser |
| `lower_expr2` | 19 | Expression lowering (part 2) |
| `cg_runtime_full` | 18 | Runtime module concatenation |
| `cg_stmt` | 18 | C statement emission |
| `ll_binop_instr` | 18 | LLVM binary op mapping |
| `build_program` | 16 | Build orchestration |

### Most depended-on functions (by reverse dependencies)

| Function | Reverse deps | Role |
|----------|-------------|------|
| `gval_t` | 44 | Core value type helper |
| `is_err` | 40 | Error checking |
| `cur_kind` | 39 | Token kind accessor |
| `jb_obj` | 39 | JSON object builder |
| `cg_lbrace` | 30 | C brace helper |
| `jb_arr` | 30 | JSON array builder |
| `sql_escape` | 30 | SQL string escaping |
| `cg_rbrace` | 29 | C brace helper |
| `cg_operand` | 28 | C operand rendering |
| `json_get_str` | 25 | JSON string accessor |
| `lower_expr` | 24 | Expression lowering entry |
| `mcp_text_result` | 24 | MCP response helper |
| `ok_local` | 24 | MIR local operand |
| `infer_expr` | 20 | Type inference entry |
| `unify` | 17 | Unification |

### Extern declarations (23)

The compiler declares 23 external C functions for its own use:

| Category | Functions |
|----------|-----------|
| String | `str_concat`, `str_eq`, `str_len`, `str_slice`, `str_char_at`, `int_to_str`, `str_to_int` |
| Array | `array_push`, `array_set`, `array_pop`, `array_len` |
| I/O | `println`, `read_file`, `write_file`, `system` |
| Database | `db_open`, `db_close`, `db_exec`, `db_query_rows`, `db_query_one` |
| Other | `exit`, `args`, `raw_set` |
