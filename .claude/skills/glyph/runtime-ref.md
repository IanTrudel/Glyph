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

## Print / I/O

| Function | Sig | Description |
|----------|-----|-------------|
| `print` | `S -> I` | Print to stdout (no newline) |
| `println` | `S -> I` | Print + newline to stdout |
| `eprintln` | `S -> I` | Print + newline to stderr |
| `read_file` | `S -> S` | Read entire file (not stdin/pipes) |
| `write_file` | `S -> S -> I` | Write string to file (0=ok) |
| `read_line` | `I -> S` | Read a line from stdin (dummy arg required — zero-arg side effect) |
| `flush` | `I -> V` | Flush stdout (dummy arg required — zero-arg side effect) |
| `args` | `-> [S]` | Command-line arguments |
| `system` | `S -> I` | Execute shell command, return exit code |

## String

| Function | Sig | Description |
|----------|-----|-------------|
| `str_concat` | `S -> S -> S` | Concatenate (prefer `+` operator) |
| `str_eq` | `S -> S -> I` | Equality (1=eq, 0=neq; prefer `==`) |
| `str_len` | `S -> I` | Length in bytes |
| `str_slice` | `S -> I -> I -> S` | Substring `[start..end)` (clamps) |
| `str_char_at` | `S -> I -> I` | Byte at index (-1 if OOB) |
| `int_to_str` | `I -> S` | Integer to string |
| `str_to_int` | `S -> I` | Parse integer (0 on failure) |
| `str_to_cstr` | `S -> *V` | To null-terminated C string |
| `cstr_to_str` | `*V -> S` | From C string |
| `str_index_of` | `S -> S -> I` | Find substring index (-1 if not found) |
| `str_starts_with` | `S -> S -> I` | Prefix test (1=yes, 0=no) |
| `str_ends_with` | `S -> S -> I` | Suffix test (1=yes, 0=no) |
| `str_trim` | `S -> S` | Trim whitespace from both ends |
| `str_to_upper` | `S -> S` | Convert to uppercase |
| `str_split` | `S -> S -> [S]` | Split string by separator |
| `str_from_code` | `I -> S` | Single character from ASCII code |

## String Builder

O(n) concatenation. String interpolation compiles to these automatically.

| Function | Sig | Description |
|----------|-----|-------------|
| `sb_new` | `-> *V` | Create builder |
| `sb_append` | `*V -> S -> *V` | Append string |
| `sb_build` | `*V -> S` | Finalize, return string |

## Float

| Function | Sig | Description |
|----------|-----|-------------|
| `int_to_float` | `I -> F` | Integer to float |
| `float_to_int` | `F -> I` | Float to integer (truncate) |
| `float_to_str` | `F -> S` | Float to string |
| `str_to_float` | `S -> F` | Parse float (0.0 on failure) |

## Float Math

| Function | Sig | Description |
|----------|-----|-------------|
| `sqrt` | `F -> F` | Square root |
| `sin` | `F -> F` | Sine |
| `cos` | `F -> F` | Cosine |
| `atan2` | `F -> F -> F` | Arc tangent of y/x |
| `fabs` | `F -> F` | Absolute value |
| `pow` | `F -> F -> F` | Power: `pow(base, exp)` |
| `floor` | `F -> F` | Round down |
| `ceil` | `F -> F` | Round up |

## Array

| Function | Sig | Description |
|----------|-----|-------------|
| `array_new` | `I -> *V` | Create with initial capacity |
| `array_push` | `[I] -> I -> I` | Push element, returns new data ptr |
| `array_len` | `[I] -> I` | Element count |
| `array_set` | `[I] -> I -> I -> V` | Set element at index |
| `array_pop` | `[I] -> I` | Remove and return last (panics if empty) |
| `array_bounds_check` | `I -> I -> V` | Check `[0..len)`, panic if OOB |
| `raw_set` | `*V -> I -> I -> V` | Set value at raw pointer offset |
| `array_reverse` | `[I] -> [I]` | Reverse array in-place (panics if frozen) |
| `array_slice` | `[I] -> I -> I -> [I]` | New frozen array from `[start..end)` (clamps) |
| `array_index_of` | `[I] -> I -> I` | Find element index (-1 if not found) |
| `array_freeze` | `[I] -> [I]` | Mark array immutable (idempotent, returns same ptr) |
| `array_frozen` | `[I] -> I` | 1 if frozen, 0 if mutable |
| `array_thaw` | `[I] -> [I]` | Create a mutable deep copy of a frozen array |
| `generate` | `I -> (I -> I) -> [I]` | Create frozen array of N elements via `fn(index)` |

## Map

Hash map with string keys. All values are `I` (GVal) internally.

| Function | Sig | Description |
|----------|-----|-------------|
| `hm_new` | `-> *V` | Create empty map |
| `hm_set` | `*V -> S -> I -> V` | Insert or update entry |
| `hm_get` | `*V -> S -> I` | Lookup value (0 if absent) |
| `hm_has` | `*V -> S -> I` | Membership test (1=present, 0=absent) |
| `hm_del` | `*V -> S -> V` | Remove entry |
| `hm_keys` | `*V -> [S]` | Extract all keys as frozen array |
| `hm_freeze` | `*V -> *V` | Mark map immutable |
| `hm_frozen` | `*V -> I` | 1 if frozen, 0 if mutable |
| `hm_len` | `*V -> I` | Number of entries |
| `hm_get_float` | `*V -> S -> F` | Lookup float value (0.0 if absent) |
| `hm_set_float` | `*V -> S -> F -> V` | Insert or update float entry |

Note: Current implementation uses string keys only (`hm_keq` does string equality). Values are stored as `I` (GVal/intptr_t) — store pointers or integers. Use `hm_get_float`/`hm_set_float` for float values (avoids bitcast issues).

```
-- Example: word frequency counter
count_words words =
  m = hm_new()
  count_loop(m, words, 0)
  m

count_loop m words i =
  match i >= array_len(words)
    true -> 0
    _ ->
      w = words[i]
      cur = hm_get(m, w)
      hm_set(m, w, cur + 1)
      count_loop(m, words, i + 1)
```

## Ref (Mutable Cell)

Explicit mutable cell — 8 bytes, always mutable. Use instead of `[0]` array hack.

| Function | Sig | Description |
|----------|-----|-------------|
| `ref` | `I -> &I` | Create mutable cell holding value |
| `deref` | `&I -> I` | Read cell value |
| `set_ref` | `&I -> I -> V` | Write new value to cell |

```
counter = ref(0)
_ = set_ref(counter, deref(counter) + 1)
x = deref(counter)    -- x = 1
```

Closures capture the ref pointer by value — mutations visible to both sides.

## Bitset

Fixed-size bit array for efficient boolean flag storage.

| Function | Sig | Description |
|----------|-----|-------------|
| `bitset_new` | `I -> *V` | Create bitset with N bits (all clear) |
| `bitset_set` | `*V -> I -> V` | Set bit at index |
| `bitset_test` | `*V -> I -> I` | Test bit at index (1=set, 0=clear) |

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

Declare functions in the `extern_` table; the compiler generates C wrappers and links `-lsqlite3`. Do not use `glyph_` prefix in your code — that's the generated C name.

```bash
./glyph extern app.glyph db_open sqlite3_open "S -> I" --lib sqlite3
```

Common pattern using `glyph_db_*` helpers (declare via extern):

| Name | Sig | Description |
|------|-----|-------------|
| `db_open` | `S -> I` | Open database, returns handle (0=error) |
| `db_close` | `I -> V` | Close handle |
| `db_exec` | `I -> S -> I` | Execute SQL, no result (0=ok) |
| `db_query_rows` | `I -> S -> [[S]]` | Query → rows of string columns |
| `db_query_one` | `I -> S -> S` | Query → single value |

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
| Map | opaque pointer to hash map | 8B (ptr) |
| Record (named) | `typedef struct { long long f1; ... } Glyph_Name;` | 8B/field |
| Record (anonymous) | Fields at fixed offsets, alphabetical | 8B/field |
| Enum | `{tag: i64, payload...}` heap-allocated | 8B + 8B/field |
| Closure | `{fn_ptr, captures...}` heap-allocated | 8B + 8B/capture |

All values are `long long` (8 bytes) at ABI boundary. Boehm GC integrated — `malloc`/`realloc`/`free` redirected to `GC_malloc`/`GC_realloc`/`GC_free` via preprocessor macros. Programs link with `-lgc`.

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
