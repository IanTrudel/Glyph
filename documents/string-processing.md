# String Processing in Glyph

Comprehensive assessment of string representation, operations, and processing across the
Glyph compiler pipeline — from source syntax through tokenization, parsing, type checking,
MIR lowering, C/LLVM code generation, and the runtime.

## 1. Representation

### 1.1 Memory Layout

Strings are **fat pointers**: a 16-byte heap-allocated header containing a data pointer and
a byte length.

```
Offset 0:  const char*  ptr    → heap-allocated byte data
Offset 8:  long long    len    → byte count (no null terminator in logical length)
```

The data bytes are stored inline after the header when freshly allocated:

```
[ ptr | len | d0 d1 d2 ... ]   ← 16 + len bytes total
  ^-----------/                   ptr points to d0
```

All string runtime functions (`glyph_str_*`) follow this layout. The C type is `GVal`
(an `intptr_t` alias), and strings are passed/returned as opaque `GVal` pointers.

### 1.2 Encoding

Strings are **byte strings** — sequences of raw bytes with no enforced encoding. The runtime
operates on byte indices and byte lengths. Individual characters are accessed as unsigned byte
values (0–255) via `str_char_at`. There is no Unicode awareness: `str_len` returns byte count,
`str_slice` operates on byte offsets, and `str_to_upper`/`str_to_lower` only handle ASCII
(A–Z / a–z).

### 1.3 String Constants

**Cranelift backend (Rust compiler):** String literals are interned into a `HashMap<String, DataId>`,
placed in read-only data sections (`.str.0`, `.str.1`, ...), null-terminated for C compatibility,
and wrapped in heap-allocated 16-byte structs at each use site via `glyph_alloc(16)`.

**C codegen (self-hosted):** String literals are emitted as C string constants and wrapped at
runtime via `glyph_cstr_to_str()`, which copies the bytes into a `malloc(16+len)` allocation.

**LLVM IR backend:** String constants are emitted as named globals
(`@str_<fn_name>_<index> = private unnamed_addr constant [N x i8] c"..."`) and loaded via
`glyph_cstr_to_str` at use sites. Escape sequences are converted to LLVM hex escapes
(`\0A`, `\09`, `\0D`, `\22`, `\5C`, `\00`).

### 1.4 Allocation and GC

All string allocations use `malloc`, which is `#define`'d to `GC_malloc` (Boehm GC). Strings
are never explicitly freed — the garbage collector reclaims them. The `StringBuilder` functions
(`sb_new`, `sb_append`, `sb_build`) use `malloc`/`free` internally but produce GC-managed
final strings.

---

## 2. Syntax

### 2.1 String Literals

| Syntax | Description |
|--------|-------------|
| `"hello"` | Regular string with escape processing |
| `r"hello"` | Raw string — no escapes, no interpolation |
| `r"""..."""` | Triple-quoted raw string — multi-line, no escapes |

### 2.2 Escape Sequences

Processed by `process_esc_loop` (self-hosted) and `scan_string` (Rust lexer):

| Escape | Value | Code point |
|--------|-------|------------|
| `\n` | Newline | 10 |
| `\t` | Tab | 9 |
| `\r` | Carriage return | 13 |
| `\\` | Backslash | 92 |
| `\"` | Double quote | 34 |
| `\{` | Literal open brace | 123 |
| `\}` | Literal close brace | 125 |
| `\0` | Null byte | 0 (Rust lexer only) |
| `\xHH` | Hex byte | 0–255 |

**C codegen escaping** (`cg_escape_chars`): When emitting string literals into C source,
backslashes are doubled (`\\`), quotes are escaped (`\"`), newlines become `\n`, and `\xHH`
hex escapes are passed through verbatim. Other characters are emitted as-is.

**LLVM IR escaping** (`ll_escape_chars`): Characters are converted to LLVM hex notation —
non-printable and special characters use `\HH` format (e.g., `\0A` for newline, `\22` for
quote). Printable ASCII (32–126) passes through unchanged.

### 2.3 String Interpolation

Syntax: `"text {expr} more text"`

**Tokenization** (self-hosted `tok_str_interp_loop` / Rust `scan_string`):

1. Scanner detects `{` (not preceded by `\`) in a string literal
2. Emits `tk_str_interp_start` token
3. Alternates between string-fragment tokens (`tk_str`) and expression tokens
4. Nested braces are tracked by depth counter
5. Emits `tk_str_interp_end` token

**Parsing** (self-hosted `psi_loop` / Rust parser):

The parser collects parts into a list: string literal fragments become `ex_str_lit` AST
nodes, expression fragments are parsed as full expressions. The whole thing becomes an
`ex_str_interp` node (self-hosted) or `ExprKind::StrInterp(Vec<StringPart>)` (Rust).

### 2.4 Type Alias

`S` is the single-character type alias for `Str`. In the type system, strings have tag
`ty_str = 4` (self-hosted) or `Type::Str` (Rust).

---

## 3. Core Runtime Functions

### 3.1 Primitive Operations (C runtime, always linked)

Defined in `cg_runtime_c`:

| Function | Signature | Description |
|----------|-----------|-------------|
| `glyph_str_eq` | `(S, S) → I` | Byte-wise equality via `memcmp`; null-safe |
| `glyph_str_len` | `(S) → I` | Returns byte length from header |
| `glyph_str_char_at` | `(S, I) → I` | Returns byte value at index; -1 if OOB |
| `glyph_str_slice` | `(S, I, I) → S` | Byte-range substring; clamps bounds |
| `glyph_str_concat` | `(S, S) → S` | Concatenation; null-safe (returns other if one is null) |
| `glyph_int_to_str` | `(I) → S` | Integer to decimal string via `snprintf` |
| `glyph_str_to_int` | `(S) → I` | Parse decimal integer (with sign) |
| `glyph_cstr_to_str` | `(ptr) → S` | Convert null-terminated C string to Glyph string |
| `glyph_str_to_cstr` | `(S) → ptr` | Convert Glyph string to null-terminated C string |

### 3.2 Extended String Operations (C runtime)

Defined in `cg_runtime_str2`:

| Function | Signature | Description |
|----------|-----------|-------------|
| `glyph_str_index_of` | `(S, S) → I` | First occurrence of needle; -1 if not found |
| `glyph_str_starts_with` | `(S, S) → B` | Prefix test via `memcmp` |
| `glyph_str_ends_with` | `(S, S) → B` | Suffix test via `memcmp` |
| `glyph_str_trim` | `(S) → S` | Strip leading/trailing whitespace (space, tab, newline, CR) |
| `glyph_str_to_upper` | `(S) → S` | ASCII-only uppercase conversion |
| `glyph_str_split` | `(S, S) → [S]` | Split by delimiter; empty delimiter splits into chars |
| `glyph_str_from_code` | `(I) → S` | Single byte value to 1-byte string |

### 3.3 Float ↔ String Conversion

Defined in `cg_runtime_float`:

| Function | Signature | Description |
|----------|-----------|-------------|
| `glyph_float_to_str` | `(F) → S` | Float to string via `snprintf("%g", ...)` |
| `glyph_str_to_float` | `(S) → F` | String to float via `atof` (max 63 chars) |

Float values are stored as bit-cast `GVal` integers (via `_glyph_i2f` / `_glyph_f2i`).

### 3.4 StringBuilder (O(n) string building)

Defined in `cg_runtime_sb`:

| Function | Signature | Description |
|----------|-----------|-------------|
| `glyph_sb_new` | `() → SB` | Allocate builder: `{buf_ptr, len=0, cap=64}` (24 bytes) |
| `glyph_sb_append` | `(SB, S) → SB` | Append string; doubles capacity when full |
| `glyph_sb_build` | `(SB) → S` | Finalize: copy buffer into new string, free builder |

StringBuilder is a 24-byte mutable struct `{buf: *char, len: i64, cap: i64}`. Growth is
exponential (capacity doubles). The `sb_build` call consumes the builder.

### 3.5 Output Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `glyph_println` | `(S) → I` | Print string + newline to stdout, flush; null-safe |
| `glyph_eprintln` | `(S) → I` | Print string + newline to stderr, flush |
| `glyph_print` | `(S) → I` | Print string to stdout (no newline), flush |

### 3.6 I/O Functions (string-based)

| Function | Signature | Description |
|----------|-----------|-------------|
| `glyph_read_file` | `(S) → S` | Read entire file; returns string with `len=-1` on failure |
| `glyph_write_file` | `(S, S) → I` | Write string to file path; returns 0 or -1 |
| `glyph_system` | `(S) → I` | Execute shell command string; returns exit code |

### 3.7 Hash Map (string-keyed)

Defined in `cg_runtime_map`. Uses FNV-1a hashing on the string's byte data. Key equality
delegates to `glyph_str_eq`. Open addressing with linear probing, 70% load factor threshold.

| Function | Signature | Description |
|----------|-----------|-------------|
| `glyph_hm_new` | `() → Map` | New map, initial capacity 8 |
| `glyph_hm_set` | `(Map, S, V) → Map` | Insert/update key-value pair |
| `glyph_hm_get` | `(Map, S) → V` | Lookup by string key |
| `glyph_hm_has` | `(Map, S) → B` | Check if key exists |
| `glyph_hm_del` | `(Map, S) → Map` | Delete key (tombstone) |
| `glyph_hm_keys` | `(Map) → [S]` | All keys as array |
| `glyph_hm_len` | `(Map) → I` | Number of entries |

---

## 4. Compiler Pipeline — String Processing by Phase

### 4.1 Tokenizer

**Self-hosted** (`tok_one3`, `tok_one4`):

- Regular strings: `scan_string_end` scans for unescaped `"`, skipping `\X` pairs
- Interpolation detection: `scan_str_has_interp` looks for unescaped `{` before closing `"`
- If interpolation found: `tok_str_interp_loop` emits interleaved string/expression tokens
- Raw strings: `scan_raw_string_end` (single-quote) and `scan_raw_triple_end` (triple-quote)
- String interp expression tokenization: `tok_interp_expr` recursively tokenizes `{...}` content with brace-depth tracking

**Rust** (`crates/glyph-parse/src/lexer.rs`):

- `scan_string`: Processes escapes inline, detects interpolation via `{`, emits `Str`,
  `StrInterpStart`/`StrInterpEnd` tokens
- `scan_raw_string`: No escape processing, supports single and triple-quoted forms
- Expression parts within interpolation are recursively lexed via a fresh `Lexer::new`

### 4.2 Parser

**Self-hosted** (`parse_atom`, `parse_str_interp`):

- Plain `tk_str` → `ex_str_lit` AST node with string value
- `tk_str_interp_start` → `psi_loop` collects parts → `ex_str_interp` node
- String patterns in match: `tk_str` in pattern position → `pat_str` pattern node

**Rust** (`crates/glyph-parse/src/parser.rs`):

- `TokenKind::Str(s)` → `ExprKind::StrLit(s)`
- `TokenKind::StrInterpStart` → collects `StringPart::Lit` / `StringPart::Expr` → `ExprKind::StrInterp(parts)`
- Pattern: `TokenKind::Str(s)` → `PatternKind::StrLit(s)`

### 4.3 Type Checker

**Self-hosted** (`register_builtins`, `infer_expr_core`):

- String literals and interpolations infer to type tag `ty_str = 4`
- Runtime function signatures are registered with string types:
  - `glyph_str_len : S → I`
  - `glyph_str_concat : S → S → S`
  - `glyph_str_eq : S → S → I`
  - `glyph_str_char_at : S → I → I`
  - `glyph_str_slice : S → I → I → S`
  - `glyph_int_to_str : I → S`
  - `glyph_str_to_int : S → I`

**Rust** (`crates/glyph-typeck/`):

- `Type::Str` enum variant, displayed as `S`
- Binary operations: `Str + Str → Str` (concat), `Str == Str → Bool`, `Str < Str → Bool`
- String indexing returns `Type::Str`
- Same runtime function signatures registered in `register_runtime_functions`

### 4.4 MIR Lowering

**String binary operations** (`lower_str_binop`, `lower_binary`):

The `is_str_op` function checks if either operand has type tag 4 (string). When a type
context map (`tctx`) is available, `tctx_is_str_bin` also queries inferred types. If
either operand is a string:

| Source operator | Lowered to |
|----------------|------------|
| `+` | `Rvalue::Call("str_concat", [left, right])` |
| `==` | `Rvalue::Call("str_eq", [left, right])` |
| `!=` | `Call("str_eq", ...) + BinOp(Eq, result, 0)` (negation) |
| other | Falls through to integer binop (known limitation) |

**String type tracking** (`build_local_types`):

MIR locals have a parallel `local_types` array. Type 4 (string) propagates through:
- String literal operands
- Return values of known string-returning functions (`is_str_ret_fn`: `str_concat`,
  `int_to_str`, `str_slice`, `sb_build`, `read_file`, `cstr_to_str`)
- Statement scanning (`blt_scan_stmts`) examines call targets and operand types

**String interpolation** (`lower_str_interp`, `lower_str_interp_parts`):

1. Each part is lowered to a MIR operand
2. String literal parts become `ok_const_str` operands
3. Expression parts are lowered; if result type is 4 (string), used directly;
   if type 3 (float), converted via `glyph_float_to_str`; otherwise via `glyph_int_to_str`
4. All parts collected into `sops` array on an `rv_str_interp` statement

**String pattern matching** (`lower_match_str`):

Lowered to `glyph_str_eq(scrutinee, pattern_literal)` call, result compared `!= 0`,
branching to match/next blocks. Falls through to next arm on mismatch.

### 4.5 C Code Generation

**String literals** (`cg_str_literal`):

```c
(GVal)glyph_cstr_to_str("escaped_content")
```

The source string passes through `process_escapes` (Glyph escape → actual chars) then
`cg_escape_str` (actual chars → C-safe escapes).

**String equality** (`cg_str_eq_stmt`):

```c
_local_N = glyph_str_eq(a, b);     // for ==
_local_N = !glyph_str_eq(a, b);    // for !=
```

**String concatenation** (`cg_binop_str` detects `rv_call` to `str_concat`):

```c
_local_N = glyph_str_concat(a, b);
```

**String interpolation** (`cg_str_interp_stmt`):

```c
{ GVal __sb = glyph_sb_new(); glyph_sb_append(__sb, part1); glyph_sb_append(__sb, part2); ... _local_N = (GVal)glyph_sb_build(__sb); }
```

Each interpolation is compiled to a StringBuilder block — O(n) concatenation.

### 4.6 LLVM IR Code Generation

**String globals** (`ll_str_globals`): Each unique string in a function gets a global constant:

```llvm
@str_main_0 = private unnamed_addr constant [6 x i8] c"hello\00"
```

**String loading** (`ll_load_operand`): String constants are loaded via `glyph_cstr_to_str`:

```llvm
%str_ptr = call i64 @glyph_cstr_to_str(ptr @str_main_0)
```

**String interpolation** (`ll_emit_str_interp`): Same StringBuilder pattern as C backend:

```llvm
%sb_0 = call i64 @glyph_sb_new()
%ap_0_0 = call i64 @glyph_sb_append(i64 %sb_0, i64 %part0)
; ...
%result = call i64 @glyph_sb_build(i64 %sb_0)
```

**Runtime declarations** (`ll_rt_decls1`, `ll_rt_decls2`, `ll_rt_decls3`): All string
functions are declared as `i64`-typed external functions matching the C runtime signatures.

---

## 5. Glyph-Level String Utilities

Functions written in Glyph (not C runtime) that operate on strings, used internally
by the compiler:

### 5.1 String Manipulation

| Function | Description |
|----------|-------------|
| `str_contains(haystack, needle)` | Substring search via sliding window + `str_eq` |
| `str_replace_all(s, from, to)` | Global replacement using StringBuilder |
| `str_to_lower(s)` | ASCII lowercase via char-by-char lookup |
| `str_lt(a, b)` | Lexicographic less-than via byte comparison |
| `sql_escape(s)` | Doubles single quotes for SQL string safety |
| `find_str_in(arr, target, i)` | Linear search in string array |
| `has_str_in(arr, target)` | Boolean presence check in string array |

### 5.2 String Splitting and Joining

| Function | Description |
|----------|-------------|
| `split_lines(s)` | Split on newlines |
| `split_comma(s)` | Split on commas |
| `split_arrow(s)` | Split on `->` (for type signatures) |
| `join_str_arr(arr, i)` | Join with spaces |
| `sort_str_arr(arr)` | Insertion sort using `str_lt` |

### 5.3 String Building Helpers

| Function | Description |
|----------|-------------|
| `s2(a, b)` | Concatenate 2 strings (alias for `str_concat`) |
| `s3(a, b, c)` | Concatenate 3 strings |
| `s4(a, b, c, d)` | Concatenate 4 strings |
| `s5(a, b, c, d, e)` | Concatenate 5 strings |
| `s7(a, b, c, d, e, f, g)` | Concatenate 7 strings |
| `itos(n)` | Integer to string (wraps `int_to_str`) |

These are used extensively throughout the compiler for C/LLVM code emission.

### 5.4 JSON String Processing

| Function | Description |
|----------|-------------|
| `json_gen_str(s)` | Escape and quote string for JSON output |
| `json_gen_str_loop(s, pos, len, sb)` | Per-char escaping: `"`, `\`, `\n`, `\t`, `\r` |
| `json_unescape(src, start, end)` | Unescape JSON string content |
| `json_unesc_loop(src, pos, end, sb)` | Per-char unescaping: `\n`, `\t`, `\r`, fallback passthrough |
| `json_parse_str(src, tokens, pos, pool)` | Parse JSON string token into AST node |

### 5.5 Diagnostic Formatting

| Function | Description |
|----------|-------------|
| `format_diagnostic(name, src, offset, line, msg)` | Error message with source context and caret |
| `format_parse_err(result, src, name)` | Format parser error with line/column info |
| `extract_source_line(src, offset)` | Extract single line from source for display |

---

## 6. Known Limitations and Edge Cases

### 6.1 No Unicode Support

All string operations are byte-oriented. `str_len` returns bytes, `str_char_at` returns
a single byte value, `str_slice` operates on byte indices. Multi-byte characters (UTF-8)
are not aware — slicing can split a codepoint. `str_to_upper`/`str_to_lower` handle only
ASCII A–Z / a–z.

### 6.2 String Operator Type Detection

The `+`/`==`/`!=` operators dispatch to string functions only when at least one operand
is **known to be a string** at MIR lowering time. Detection works through:

1. **Literal detection**: String literal operands have type tag 4
2. **Local type tracking**: `build_local_types` propagates string types through assignments
3. **Type context map**: `tctx_is_str_bin` queries the HM type inference results

**Failure case**: If both operands are untyped parameters (no type annotation, no type
context), the operator falls through to integer arithmetic. This is the primary string
correctness risk for user programs.

### 6.3 String Comparison

`glyph_str_eq` is null-safe (two nulls are equal, null vs non-null is false). However,
`str_lt` (lexicographic ordering) is implemented in Glyph (not the C runtime) and has no
null safety. The runtime `str_to_upper` allocates via `malloc` but doesn't route through
`glyph_alloc`, so its result header is GC-managed but its buffer is not — this is
inconsistent with other string functions.

### 6.4 StringBuilder Lifecycle

`sb_build` frees the internal buffer and the builder struct. Calling `sb_append` after
`sb_build` on the same builder is undefined behavior. There is no runtime check for this.

### 6.5 String Constant Overhead

Every string literal use in C and LLVM backends creates a fresh heap allocation via
`glyph_cstr_to_str`. In the Cranelift backend, string data is interned (deduplicated) in
data sections, but the 16-byte wrapper struct is freshly allocated at each use site.
Repeated use of the same literal in a loop allocates a new string object per iteration.

### 6.6 `str_to_int` Overflow

`glyph_str_to_int` does not check for integer overflow. Parsing a string representing a
number larger than `INT64_MAX` silently wraps.

### 6.7 `str_to_float` Truncation

`glyph_str_to_float` copies at most 63 bytes of the input string before parsing via
`atof`. Strings longer than 63 bytes are silently truncated.

---

## 7. Test Coverage

The compiler has 58 string-related tests covering:

- **Tokenizer**: `test_tok_string`, `test_tok_raw_string`, `test_tok_extras` (interpolation tokens)
- **Parser**: `test_parse_str_lit`
- **MIR lowering**: `test_mir_str_interp`, `test_mir_str_match`, `test_mir_str_return`
- **Type inference**: `test_ty_infer_str`
- **C codegen**: `test_cg_escape_str`, `test_cg_str_eq`, `test_cg_str2`, `test_cg_binop_str`, `test_cg_string_float`
- **LLVM codegen**: `test_ll_str_interp`, `test_ll_arrays_variants_str`
- **String operators**: `test_str_add_literal_param`, `test_str_add_two_params_sig`, `test_str_eq_literal_param`, `test_str_eq_two_params_sig`, `test_str_neq_two_params_sig`
- **Struct/enum string fields**: `test_str_struct_field_eq`, `test_str_struct_field_neq`, `test_str_enum_payload_concat`, `test_str_enum_payload_eq`, `test_str_enum_payload_neq`, `test_str_direct_struct_field`, `test_str_enum_struct_field`, `test_str_indirect_param_eq`, `test_str_recursive_param_eq`
- **Escape processing**: `test_esc_hex_cg`, `test_esc_hex_multi`, `test_esc_hex_preserve`, `test_esc_backslash`, `test_esc_brace`
- **Utility functions**: `test_string_helpers`, `test_string_utils`, `test_sort_str`, `test_util_find_str`, `test_util_str_lt`, `test_ns_string_helpers`
- **Interpolation correctness**: `test_str_interp_str_type`, `test_cg_variant_interp`, `test_float_interp`
- **Array/string interactions**: `test_str_arr_concat`, `test_str_arr_elem_eq`, `test_str_arr_elem_neq`, `test_str_arr_param_eq`, `test_str_arr_param_struct`

---

## 8. Function Inventory

### 8.1 C Runtime Functions (always available)

Core: `glyph_str_eq`, `glyph_str_len`, `glyph_str_char_at`, `glyph_str_slice`,
`glyph_str_concat`, `glyph_int_to_str`, `glyph_str_to_int`, `glyph_cstr_to_str`,
`glyph_str_to_cstr`, `glyph_println`, `glyph_eprintln`, `glyph_print`

Extended: `glyph_str_index_of`, `glyph_str_starts_with`, `glyph_str_ends_with`,
`glyph_str_trim`, `glyph_str_to_upper`, `glyph_str_split`, `glyph_str_from_code`

Float: `glyph_float_to_str`, `glyph_str_to_float`

Builder: `glyph_sb_new`, `glyph_sb_append`, `glyph_sb_build`

I/O: `glyph_read_file`, `glyph_write_file`, `glyph_system`

Map: `glyph_hm_new`, `glyph_hm_set`, `glyph_hm_get`, `glyph_hm_has`, `glyph_hm_del`,
`glyph_hm_keys`, `glyph_hm_len`

### 8.2 Compiler-Internal Glyph Functions

Manipulation: `str_contains`, `str_replace_all`, `str_to_lower`, `str_lt`,
`find_str_in`, `has_str_in`, `sql_escape`

Splitting: `split_lines`, `split_comma`, `split_arrow`

Building: `s2`–`s7`, `itos`, `join_str_arr`, `sort_str_arr`

JSON: `json_gen_str`, `json_unescape`, `json_parse_str`

Codegen: `cg_str_literal`, `cg_escape_str`, `cg_escape_chars`, `cg_str_eq_stmt`,
`cg_str_interp_stmt`, `cg_sb_appends`, `ll_escape_str`, `ll_escape_chars`,
`ll_str_globals`, `ll_emit_str_interp`, `ll_sb_appends`

Type detection: `is_str_op`, `tctx_is_str_bin`, `is_str_ret_fn`, `build_local_types`

MIR lowering: `lower_str_binop`, `lower_str_interp`, `lower_str_interp_parts`,
`lower_match_str`
