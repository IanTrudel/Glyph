# Glyph Runtime Reference

All functions available in every compiled program. C runtime embedded at compile time.
Runtime functions are prefixed `glyph_` in generated C (e.g., `println` → `glyph_println`).

## Core

| Function | Sig | Description |
|----------|-----|-------------|
| `panic` | `S -> N` | Print to stderr, exit(1) |
| `exit` | `I -> V` | Exit with code |
| `alloc` | `U -> *V` | Heap allocate (panics OOM) |
| `dealloc` | `*V -> V` | Free heap pointer |
| `realloc` | `*V -> I -> *V` | Resize allocation |

## Print

| Function | Sig | Description |
|----------|-----|-------------|
| `print` | `S -> I` | Print to stdout (no newline) |
| `println` | `S -> I` | Print + newline to stdout |
| `eprintln` | `S -> I` | Print + newline to stderr |

## String

| Function | Sig | Description |
|----------|-----|-------------|
| `str_concat` | `S -> S -> S` | Concatenate (prefer `+` operator) |
| `str_eq` | `S -> S -> I` | Equality (1=eq, 0=neq; prefer `==`) |
| `str_len` | `S -> I` | Length in bytes |
| `str_slice` | `S -> I -> I -> S` | Substring `[start..end)` |
| `str_char_at` | `S -> I -> I` | Byte at index (-1 if OOB) |
| `int_to_str` | `I -> S` | Integer to string |
| `str_to_int` | `S -> I` | Parse integer (0 on failure) |
| `str_to_cstr` | `S -> *V` | To null-terminated C string |
| `cstr_to_str` | `*V -> S` | From C string |

## String Builder

O(n) concatenation. String interpolation compiles to these automatically.

| Function | Sig | Description |
|----------|-----|-------------|
| `sb_new` | `-> *V` | Create builder |
| `sb_append` | `*V -> S -> *V` | Append string |
| `sb_build` | `*V -> S` | Finalize, return string |

## Array

| Function | Sig | Description |
|----------|-----|-------------|
| `array_new` | `I -> *V` | Create with initial capacity |
| `array_push` | `[I] -> I -> I` | Push element, returns new data ptr |
| `array_len` | `[I] -> I` | Element count |
| `array_set` | `[I] -> I -> I -> V` | Set element at index |
| `array_pop` | `[I] -> I` | Remove and return last |
| `array_bounds_check` | `I -> I -> V` | Check `[0..len)`, panic if OOB |

## I/O

| Function | Sig | Description |
|----------|-----|-------------|
| `read_file` | `S -> S` | Read entire file (no stdin/pipes) |
| `write_file` | `S -> S -> I` | Write string to file (0=ok) |
| `args` | `-> [S]` | Command-line arguments |
| `system` | `S -> I` | Execute shell command, return exit code |

## Result

Result values are `{tag: i64, payload: i64}` enums. Tag 0 = Ok, 1 = Err.
Construct with `Ok(val)` / `Err(msg)`. Destructure with `match`, `?` (propagate), or `!` (unwrap).

| Function | Sig | Description |
|----------|-----|-------------|
| `ok` | `I -> !I` | Wrap value in Ok (heap-allocated) |
| `err` | `S -> !I` | Wrap error message in Err |
| `try_read_file` | `S -> !S` | Read file, Err on failure |
| `try_write_file` | `S -> S -> !I` | Write file, Err on failure |

### Operators

| Op | Syntax | Behavior |
|----|--------|----------|
| `?` | `expr?` | If Err, return it (propagate). If Ok, extract payload. |
| `!` | `expr!` | If Err, panic. If Ok, extract payload. |

### Pattern Matching

```
match result
  Ok(val) -> use(val)
  Err(msg) -> eprintln(msg)
```

## SQLite

Available when program uses `glyph_db_*`. Links `-lsqlite3`.

| Function | Sig | Description |
|----------|-----|-------------|
| `glyph_db_open` | `S -> I` | Open database (0=error) |
| `glyph_db_close` | `I -> V` | Close handle |
| `glyph_db_exec` | `I -> S -> I` | Execute SQL, no result (0=ok) |
| `glyph_db_query_rows` | `I -> S -> [[S]]` | Query → rows of string columns |
| `glyph_db_query_one` | `I -> S -> S` | Query → single value |

## Test Assertions

Only in test builds (`./glyph test`). All return 0, set `_test_failed` flag on failure.

| Function | Sig | Description |
|----------|-----|-------------|
| `assert` | `I -> I` | Fail if 0 |
| `assert_eq` | `I -> I -> I` | Fail if integers differ |
| `assert_str_eq` | `S -> S -> I` | Fail if strings differ |

## Memory Layout

| Type | Layout | Size |
|------|--------|------|
| String | `{ptr: *char, len: i64}` | 16B |
| Array | `{ptr: *i64, len: i64, cap: i64}` | 24B |
| Record (named) | `typedef struct { long long f1; ... } Glyph_Name;` | 8B/field |
| Record (anonymous) | Fields at fixed offsets, alphabetical | 8B/field |
| Enum | `{tag: i64, payload...}` heap-allocated | 8B + 8B/field |
| Closure | `{fn_ptr, captures...}` heap-allocated | 8B + 8B/capture |

All values are `long long` (8 bytes) at ABI boundary. No GC — allocations persist until exit.

## FFI Type Mapping

| Glyph | C | Notes |
|-------|---|-------|
| `I` | `long long` | |
| `U` | `unsigned long long` | |
| `F` | `double` | |
| `B` | `long long` | 0/1 at ABI |
| `S` | `void*` | Pointer to `{char*, long long}` |
| `[T]` | `void*` | Pointer to `{long long*, long long, long long}` |
| `*T` | `void*` | |
| `V` | `long long 0` | Not void in C |
