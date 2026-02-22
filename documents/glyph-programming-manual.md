# Glyph Programming Manual

## 1. Introduction

Glyph is an LLM-native programming language. Programs are SQLite3 databases (`.glyph` files), not source files. The unit of storage is the *definition* — a named, typed, hashed source fragment stored as a row in SQL. SQL queries replace the traditional module/import system.

**Design goals:**
- Token-minimal syntax to reduce BPE token count for LLM generation and consumption
- SQL as the module system — read context via `SELECT`, write definitions via `INSERT`
- Incremental compilation via content hashing and dependency tracking
- No semicolons, no braces, no `return`/`let`/`import` keywords — indentation-sensitive

**Current status:** Working compiler v0.1. Two compilers exist:
- **Self-hosted compiler** (`glyph.glyph` → `./glyph`): 513 definitions, C code generation backend, full CLI with 16 commands. This is the primary compiler.
- **Rust compiler** (`cargo run -- ...`): Cranelift backend, bootstrap tool used to rebuild `glyph.glyph`.

## 2. Quick Start

### Create a program

```bash
./glyph init hello.glyph
```

### Add a definition

```bash
./glyph put hello.glyph fn -b 'main = println("Hello, world")'
```

### Build and run

```bash
./glyph build hello.glyph hello
./hello
# Output: Hello, world
```

Or build and run in one step:

```bash
./glyph run hello.glyph
# Output: Hello, world
```

### Multi-function program

```bash
./glyph put hello.glyph fn -b 'greet name = println("Hello, " + name)'
./glyph put hello.glyph fn -b 'main = greet("Glyph")'
./glyph run hello.glyph
# Output: Hello, Glyph
```

### From a file

```bash
echo 'factorial n = match n
  0 -> 1
  _ -> n * factorial(n - 1)' > /tmp/fact.gl

./glyph put hello.glyph fn -f /tmp/fact.gl
./glyph put hello.glyph fn -b 'main = println(int_to_str(factorial(10)))'
./glyph run hello.glyph
# Output: 3628800
```

## 3. Program Model: The Database

A `.glyph` file is a SQLite3 database. The program *is* its schema.

### Core Tables

#### `def` — All definitions

| Column | Type | Description |
|--------|------|-------------|
| `id` | INTEGER PRIMARY KEY | Unique definition ID |
| `name` | TEXT NOT NULL | Definition name |
| `kind` | TEXT NOT NULL | One of: `fn`, `type`, `trait`, `impl`, `const`, `fsm`, `srv`, `macro`, `test` |
| `sig` | TEXT | Optional type signature |
| `body` | TEXT NOT NULL | Source code |
| `hash` | BLOB NOT NULL | BLAKE3 hash of `kind \|\| sig \|\| body` (32 bytes) |
| `tokens` | INTEGER NOT NULL | BPE token count (cl100k_base) |
| `compiled` | INTEGER NOT NULL DEFAULT 0 | 1 = up-to-date, 0 = dirty |
| `created` | TEXT NOT NULL | Creation timestamp |
| `modified` | TEXT NOT NULL | Last modification timestamp |

Unique constraint: `(name, kind)`. Indexes on `kind` and `compiled WHERE compiled = 0`.

#### `dep` — Dependency graph

| Column | Type | Description |
|--------|------|-------------|
| `from_id` | INTEGER REFERENCES def(id) | Dependent definition |
| `to_id` | INTEGER REFERENCES def(id) | Dependency target |
| `edge` | TEXT NOT NULL | One of: `calls`, `uses_type`, `implements`, `field_of`, `variant_of` |

Primary key: `(from_id, to_id, edge)`. Cascading deletes from `def`.

#### `extern_` — Foreign function declarations

| Column | Type | Description |
|--------|------|-------------|
| `id` | INTEGER PRIMARY KEY | |
| `name` | TEXT NOT NULL UNIQUE | Glyph-side name |
| `symbol` | TEXT NOT NULL | C symbol name |
| `lib` | TEXT | Library name (e.g., `sqlite3`) |
| `sig` | TEXT NOT NULL | Type signature (e.g., `S -> I`) |
| `conv` | TEXT NOT NULL DEFAULT 'C' | Calling convention: `C`, `system`, `rust` |

#### `tag` — Metadata key-value pairs on definitions

| Column | Type | Description |
|--------|------|-------------|
| `def_id` | INTEGER REFERENCES def(id) | |
| `key` | TEXT NOT NULL | Tag key |
| `val` | TEXT | Tag value |

Primary key: `(def_id, key)`.

#### `module` / `module_member` — Logical grouping

`module`: `id`, `name` (unique), `doc`. `module_member`: `module_id`, `def_id`, `exported` (default 1).

#### `compiled` — Cached compilation artifacts

`def_id` (PK, references def), `ir` (BLOB), `target` (TEXT), `hash` (BLOB).

#### `meta` — Schema metadata

`key` (TEXT PK), `value` (TEXT). Contains `schema_version = '2'`. Present in databases created via `glyph init`; older databases (like `glyph.glyph`) may lack this table.

### Views

**`v_dirty`** — Dirty definitions and their transitive dependents (recursive CTE):
```sql
WITH RECURSIVE dirty(id) AS (
  SELECT id FROM def WHERE compiled = 0
  UNION
  SELECT d.from_id FROM dep d JOIN dirty ON d.to_id = dirty.id
)
SELECT DISTINCT def.* FROM def JOIN dirty ON def.id = dirty.id;
```

**`v_context`** — Definitions sorted by dependency depth (fewest deps first):
```sql
SELECT d.*, COUNT(dep.to_id) as dep_count
FROM def d LEFT JOIN dep ON d.id = dep.from_id
GROUP BY d.id ORDER BY dep_count ASC, d.tokens ASC;
```

**`v_callgraph`** — Full call graph:
```sql
SELECT f.name AS caller, t.name AS callee, d.edge
FROM dep d JOIN def f ON d.from_id = f.id JOIN def t ON d.to_id = t.id;
```

### Triggers (Rust compiler only)

The Rust compiler registers custom SQL functions `glyph_hash()` and `glyph_tokens()` that power two triggers:

- **`trg_def_dirty`**: On `UPDATE OF body, sig, kind ON def` — recomputes hash and tokens, sets `compiled = 0`.
- **`trg_dep_dirty`**: On `UPDATE OF compiled ON def WHEN compiled = 0` — cascades dirty flag to all dependents via `dep`.

These triggers are **not available** in the self-hosted compiler or direct `sqlite3` CLI usage. When modifying definitions via raw SQL, drop triggers first, then recreate after.

## 4. Lexical Grammar

### Identifiers

- **Lowercase identifier**: `[a-z_][a-zA-Z0-9_]*` — variables, function names
- **Type identifier**: `[A-Z][a-zA-Z0-9_]*` — type names, constructors

### Literals

**Integers**: Sequence of digits `0-9`. Underscores allowed for readability: `1_000_000`. Parsed as `i64`.

**Floats**: Integer part, `.`, at least one fractional digit. Underscores allowed: `3.14`, `1_000.5`. Parsed as `f64`.

**Strings**: Delimited by `"`. Escape sequences:

| Escape | Meaning |
|--------|---------|
| `\n` | Newline |
| `\t` | Tab |
| `\r` | Carriage return |
| `\\` | Backslash |
| `\"` | Double quote |
| `\{` | Literal open brace (inhibits interpolation) |
| `\0` | Null byte |
| `\xHH` | Hex byte |

**Raw strings**: `r"..."` — no escape processing, no interpolation. Contents taken literally.

**Byte strings**: `b"..."` — produces byte array.

### String Interpolation

Expressions inside `{...}` in a string literal are evaluated and converted to strings:

```
"hello {name}"
"result: {x + y}"
"nested: {f(a, b)}"
```

Use `\{` to escape a literal open brace. Closing brace `}` is safe outside interpolation. Nested braces inside interpolation expressions are matched by depth.

Interpolation compiles to string builder calls: `sb_new() → sb_append(literal) → sb_append(int_to_str(expr)) → ... → sb_build()`.

### Comments

Line comments: `--` to end of line.

```
-- this is a comment
x = 42  -- inline comment
```

### Operators and Punctuation

| Token | Symbol | Token | Symbol |
|-------|--------|-------|--------|
| `Plus` | `+` | `Minus` | `-` |
| `Star` | `*` | `Slash` | `/` |
| `Percent` | `%` | `EqEq` | `==` |
| `BangEq` | `!=` | `Lt` | `<` |
| `Gt` | `>` | `LtEq` | `<=` |
| `GtEq` | `>=` | `And` | `&&` |
| `Or` | `\|\|` | `Bang` | `!` |
| `Eq` | `=` | `ColonEq` | `:=` |
| `PipeGt` | `\|>` | `GtGt` | `>>` |
| `Ampersand` | `&` | `Question` | `?` |
| `Arrow` | `->` | `Backslash` | `\` |
| `DotDot` | `..` | `At` | `@` |
| `Pipe` | `\|` | `Dot` | `.` |
| `Colon` | `:` | `Comma` | `,` |
| `LParen`/`RParen` | `()`| `LBracket`/`RBracket` | `[]` |
| `LBrace`/`RBrace` | `{}` | | |

### Keywords

`match`, `trait`, `impl`, `const`, `extern`, `fsm`, `srv`, `test`, `as`

Note: `true` and `false` are boolean literals, not keywords.

### Indentation Rules

Glyph is indentation-sensitive. The lexer emits synthetic `Indent`, `Dedent`, and `Newline` tokens.

- **Tab = 4 spaces.** Indentation measured in spaces.
- **Indent**: Emitted when indentation increases from the current level.
- **Dedent**: Emitted when indentation decreases. Multiple `Dedent` tokens emitted for multi-level de-indentation.
- **Newline**: Emitted at line boundaries (not after `Indent`, no duplicates).
- **Bracket suppression**: Inside `()`, `[]`, or `{}`, no layout tokens are emitted. Multi-line expressions inside brackets are free-form.
- **Blank lines**: Ignored (consecutive newlines collapsed).
- At EOF, `Dedent` emitted for each remaining indentation level.

## 5. Expressions

### Operator Precedence (lowest to highest)

| Level | Operators | Associativity | Example |
|-------|-----------|---------------|---------|
| 1 | `\|>` (pipe) | Left | `x \|> f \|> g` = `g(f(x))` |
| 2 | `>>` (compose) | Left | `f >> g` = `\x -> g(f(x))` |
| 3 | `\|\|` | Left | `a \|\| b` |
| 4 | `&&` | Left | `a && b` |
| 5 | `==` `!=` `<` `>` `<=` `>=` | Non-associative | `a == b` |
| 6 | `+` `-` | Left | `a + b - c` |
| 7 | `*` `/` `%` | Left | `a * b / c` |
| 8 | `-` `!` `&` `*` (prefix) | Right (unary) | `-x`, `!cond` |
| 9 | `.` `()` `[]` `?` `!` (postfix) | Left | `x.f(a)[i]?` |

### Expression Forms

#### Literals

```
42                    -- Int (i64)
3.14                  -- Float (f64)
"hello"               -- String
r"raw\nstring"        -- Raw string (no escapes)
true                  -- Bool
false                 -- Bool
```

#### Identifiers

```
x                     -- variable or function
MyType                -- type constructor
```

#### Arithmetic

```
a + b                 -- addition (Int, Float, or String concatenation)
a - b                 -- subtraction
a * b                 -- multiplication
a / b                 -- division
a % b                 -- modulo
```

All arithmetic operators require matching types on both sides: `I*I->I`, `F*F->F`. String `+` concatenates: `S+S->S`.

#### Comparison

```
a == b                -- equality (any matching type -> Bool)
a != b                -- inequality
a < b                 -- less than
a > b                 -- greater than
a <= b                -- less or equal
a >= b                -- greater or equal
```

#### Logical

```
a && b                -- logical and (Bool -> Bool -> Bool)
a || b                -- logical or
!cond                 -- logical not (Bool -> Bool)
```

#### Unary

```
-x                    -- negate (Int -> Int, Float -> Float)
!x                    -- logical not (Bool -> Bool)
&x                    -- reference (T -> &T)
*x                    -- dereference (&T -> T, *T -> T)
```

#### Function Call

```
f(x)                  -- single argument
f(x, y, z)            -- multiple arguments
```

#### Field Access

```
record.field          -- access named field
```

#### Array Indexing

```
arr[i]                -- access element at index i
```

Bounds-checked at runtime. Panics on out-of-bounds.

#### Pipe

```
x |> f                -- equivalent to f(x)
x |> f |> g           -- equivalent to g(f(x))
data |> transform |> println
```

#### Compose

```
f >> g                -- equivalent to \x -> g(f(x))
transform = parse >> validate >> save
```

#### Error Propagate

```
expr?                 -- propagate error from ?T or !T
```

#### Unwrap

```
expr!                 -- unwrap ?T or !T, panic on error/None
```

Note: The type checker may report false positives on `!` unwrap. It works correctly at runtime.

#### Lambda

```
\x -> x + 1           -- single parameter
\x y -> x + y         -- multiple parameters
\x -> \y -> x + y     -- curried (equivalent to above)
```

Lambdas are heap-allocated closures: `{fn_ptr, captures...}`. The function pointer is the hidden first argument in the calling convention.

#### Match Expression

```
match expr
  pattern1 -> body1
  pattern2 -> body2
  _ -> default_body
```

Each arm: `pattern -> expr` or `pattern -> INDENT block DEDENT`. Patterns tried in order; first match wins. Always include `_` wildcard as the last arm.

#### Block Expression

```
result =
  x = compute_something()
  y = compute_other()
  x + y
```

An indented block is a sequence of statements. The last expression is the block's value.

#### Field Accessor Shorthand

```
.name                 -- equivalent to \x -> x.name
users |> map(.name)   -- extract name field from each user
```

#### Array Literal

```
[]                    -- empty array
[1, 2, 3]            -- array of ints
[1..10]              -- range (produces [1,2,3,...,9])
```

#### Record Literal

```
{name: "Alice", age: 30}
{x: 1, y: 2}
```

Fields are stored in alphabetical order by name, 8 bytes per field.

#### Tuple

```
(1, "hello", true)    -- tuple of (Int, Str, Bool)
```

#### String Interpolation

```
"Hello, {name}!"
"Result: {compute(x)}"
"Braces: \{literal}"
```

## 6. Statements

Inside a function body (indented block):

### Let Binding

```
name = expr
```

Binds the value of `expr` to `name` in the current scope. There is no `let` keyword.

### Assignment

```
lvalue := expr
```

Mutates `lvalue` in place. Uses `:=` (not `=`).

### Expression Statement

```
println("hello")
f(x)
```

Any expression used as a statement. Side effects execute; value discarded.

### Return Value

The last expression in a block is the return value. There is no `return` keyword.

```
max a b =
  result = 0
  match a > b
    true -> result := a
    false -> result := b
  result              -- this is the return value
```

## 7. Definitions

### Function (`fn`)

```
name params = body
name params : ReturnType = body
```

Parameters are space-separated, with optional type annotations:

```
add a b = a + b
add a: I b: I : I = a + b
```

Body can be an inline expression or an indented block:

```
-- inline
double x = x * 2

-- block
factorial n =
  match n
    0 -> 1
    _ -> n * factorial(n - 1)
```

Flag parameters (prefixed with `--`) for CLI-style definitions:

```
greet name --loud: B = ...
```

### Type (`type`)

**Record type:**
```
Point = {x: I, y: I}
Person = {name: S, age: I}
```

**Enum type:**
```
Option = | None | Some(I)
Color = | Red | Green | Blue
Tree = | Leaf(I) | Node(Tree, Tree)
```

**Type alias:**
```
Name = S
```

### Test (`test`)

```
test_add = assert_eq(add(1, 2), 3)

test_strings =
  s = "hello"
  assert_eq(str_len(s), 5)
  assert_str_eq(s, "hello")
```

Test definitions have `kind='test'` in the database. They are excluded from normal builds and only compiled by `./glyph test`.

### Other Kinds

- **const**: `const name = expr` — compile-time constant
- **trait**: `trait Name ...` — method signatures (parsed, not fully implemented)
- **impl**: `impl Trait for Type ...` — method implementations (parsed, not fully implemented)
- **fsm**: Finite state machine (parsed, not fully implemented)
- **srv**: HTTP service (parsed, not fully implemented)
- **macro**: Macro definition (schema only, not implemented)

### Inserting Definitions

**Via CLI (recommended):**
```bash
./glyph put db.glyph fn -b 'add a b = a + b'
./glyph put db.glyph type -b 'Option = | None | Some(I)'
./glyph put db.glyph test -b 'test_add = assert_eq(add(1, 2), 3)'
./glyph put db.glyph fn -f /path/to/source.gl
```

The name is extracted from the body's first token. `put` uses DELETE+INSERT to avoid trigger issues.

**Via SQL:**
```sql
INSERT INTO def (name, kind, body, hash, tokens)
VALUES ('add', 'fn', 'add a b = a + b',
        X'0000000000000000000000000000000000000000000000000000000000000000', 0);
```

When using the Rust compiler API, `insert_def()` computes hash and tokens automatically. When inserting via raw SQL or the self-hosted compiler, provide zeroed hash and `tokens=0` — the Rust compiler's `build --full` will recompute correct values.

## 8. Type System

### Primitive Types

| Alias | Full Name | Size | Description |
|-------|-----------|------|-------------|
| `I` | Int64 | 8 bytes | 64-bit signed integer |
| `U` | UInt64 | 8 bytes | 64-bit unsigned integer |
| `F` | Float64 | 8 bytes | 64-bit floating point |
| `S` | Str | 16 bytes | String (fat pointer: `{ptr, len}`) |
| `B` | Bool | 1 byte | Boolean (i8, 0 or 1) |
| `V` | Void | 0 bytes | Unit type |
| `N` | Never | 0 bytes | Bottom type (diverging) |

Also available but less common: `Int32` (4 bytes), `Float32` (4 bytes).

### Compound Types

| Syntax | Meaning |
|--------|---------|
| `?T` | Optional (nullable) |
| `!T` | Result (error type) |
| `[T]` | Array |
| `&T` | Reference |
| `*T` | Pointer |
| `{K: V}` | Map |
| `(T1, T2)` | Tuple |

### Function Types

```
I -> I                -- Int to Int
I -> I -> I           -- curried: Int -> (Int -> Int)
(I -> I) -> I         -- function argument
S -> V                -- String to Void
```

Function types are right-associative: `A -> B -> C` means `A -> (B -> C)`.

### Record Types

Structural typing with row polymorphism:

```
{name: S, age: I}          -- closed record (exact fields)
{name: S ..}               -- open record (has name, may have more)
```

Fields are ordered alphabetically. Field offsets are computed at compile time (8 bytes per field, alphabetical order).

### Enum Types

Nominal typing:

```
Option = | None | Some(I)
```

Variants:
- **Nullary**: `None` — no payload
- **Positional**: `Some(I)` — tuple-like payload
- **Named**: `Variant{x: I, y: I}` — record-like payload (parsed but rarely used)

### Type Inference

Glyph uses Hindley-Milner type inference with row polymorphism. Type annotations are optional — types are inferred from usage.

```
-- all of these are equivalent:
add a b = a + b                    -- inferred as I -> I -> I
add a: I b: I : I = a + b         -- explicitly annotated
```

**Inference algorithm:**
1. Each function is pre-registered with its declared type signature (or fresh type variables)
2. Function bodies are type-checked with all signatures available
3. Unification resolves type variables via union-find with path compression and occurs check
4. Row unification extends open records with additional fields as needed

**Type display format:**
- `I` for Int, `U` for UInt, `F` for Float, `S` for Str, `B` for Bool, `V` for Void, `N` for Never
- `[T]` for arrays, `?T` for optional, `!T` for result
- `{name:S age:I}` for records (fields sorted, space-separated)
- `{name:S ..}` for open records
- `A -> B` for functions
- `forall t0 t1. Type` for polymorphic types

## 9. Pattern Matching

### Pattern Forms

| Pattern | Matches | Binds |
|---------|---------|-------|
| `_` | Anything | Nothing |
| `x` | Anything | `x` to matched value |
| `42` | Integer 42 | Nothing |
| `true` / `false` | Boolean | Nothing |
| `"hello"` | String "hello" | Nothing |
| `None` | Nullary constructor | Nothing |
| `Some(x)` | Constructor with payload | `x` to payload |
| `Point(x, y)` | Constructor with fields | `x`, `y` to fields |

### Match Expression

```
match value
  pattern1 -> expr1
  pattern2 -> expr2
  _ -> default
```

Arms are tried top-to-bottom. First matching pattern wins. Always include a `_` wildcard as the last arm for exhaustiveness.

### Examples

```
-- integer patterns
match n
  0 -> "zero"
  1 -> "one"
  _ -> "other"

-- enum patterns
match opt
  None -> "nothing"
  Some(x) -> "got: " + int_to_str(x)

-- boolean patterns
match x > 0
  true -> "positive"
  false -> "non-positive"

-- multi-line arm bodies
match cmd
  "build" ->
    compile()
    link()
    0
  "test" ->
    run_tests()
  _ -> 1
```

## 10. Memory Model

### Strings

Fat pointer: `{ptr: *char, len: i64}` — 16 bytes total.
- Offset 0-7: pointer to heap-allocated character data
- Offset 8-15: length in bytes

String data is heap-allocated, immutable. Concatenation creates a new string.

### Arrays

Header: `{ptr: *i64, len: i64, cap: i64}` — 24 bytes total.
- Offset 0-7: pointer to heap-allocated data
- Offset 8-15: current element count
- Offset 16-23: allocated capacity

Each element is 8 bytes (`long long`). Arrays grow by doubling capacity on push.

### Records

Fields at fixed offsets, ordered alphabetically by name. Each field is 8 bytes. A record `{age: I, name: S}` has `age` at offset 0, `name` at offset 8 (alphabetical order).

### Enums

Heap-allocated via `glyph_alloc`: `{tag: i64, payload: i64...}`.
- Offset 0: tag (discriminant, i64)
- Offset 8+: payload fields (8 bytes each)

Tag values are assigned sequentially starting from 0, in declaration order.

### Closures

Heap-allocated: `{fn_ptr, capture1, capture2, ...}`.
- Offset 0: function pointer
- Offset 8+: captured values (8 bytes each)

Calling convention: closure pointer is the hidden first argument.

### No Garbage Collection

All heap allocations persist until process exit. There is no garbage collector. Programs that allocate heavily will use proportionally more memory.

## 11. Runtime Functions

All runtime functions are available in every compiled Glyph program. The C runtime is embedded in the generated code at compile time.

In the self-hosted compiler, a subset of runtime functions are declared in the `extern_` table for direct use in Glyph code. The compiler prefixes these with `glyph_` in generated C (e.g., `println` → `glyph_println`). Two exceptions: `println` and `eprintln` are stored without the prefix in `extern_` but still map to `glyph_println`/`glyph_eprintln`.

### Core

| Function | Signature | Description |
|----------|-----------|-------------|
| `panic` | `S -> N` | Print message to stderr and exit(1) |
| `exit` | `I -> V` | Exit process with code |
| `alloc` | `U -> *V` | Allocate bytes on heap (panics on OOM) |
| `dealloc` | `*V -> V` | Free heap pointer |
| `realloc` | `*V -> I -> *V` | Resize allocation (panics on OOM) |

### Print

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `S -> I` | Print string to stdout (no newline) |
| `println` | `S -> I` | Print string + newline to stdout |
| `eprintln` | `S -> I` | Print string + newline to stderr |

### String Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `str_concat` | `S -> S -> S` | Concatenate two strings |
| `str_eq` | `S -> S -> I` | String equality (1 = equal, 0 = not) |
| `str_len` | `S -> I` | Length in bytes |
| `str_slice` | `S -> I -> I -> S` | Substring `[start..end)` |
| `str_char_at` | `S -> I -> I` | Byte at index (-1 if out of bounds) |
| `int_to_str` | `I -> S` | Integer to string |
| `str_to_int` | `S -> I` | Parse integer (0 on failure) |
| `str_to_cstr` | `S -> *V` | Convert Glyph string to null-terminated C string |
| `cstr_to_str` | `*V -> S` | Convert C string to Glyph string |

Note: `str_eq` returns `I` (not `B`). Use `==` for string comparison in expressions — the compiler maps it to `str_eq` automatically.

### String Builder

O(n) string concatenation for interpolation:

| Function | Signature | Description |
|----------|-----------|-------------|
| `sb_new` | `-> *V` | Create new string builder |
| `sb_append` | `*V -> S -> *V` | Append string to builder |
| `sb_build` | `*V -> S` | Finalize builder, return string |

String interpolation (`"value: {x}"`) automatically compiles to builder calls.

### Array Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `array_new` | `I -> *V` | Create array with initial capacity |
| `array_push` | `[I] -> I -> I` | Push element, returns new data ptr |
| `array_len` | `[I] -> I` | Element count |
| `array_set` | `[I] -> I -> I -> V` | Set element at index |
| `array_pop` | `[I] -> I` | Remove and return last element |
| `array_bounds_check` | `I -> I -> V` | Check index in `[0..len)`, panic if not |

### I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `read_file` | `S -> S` | Read entire file to string |
| `write_file` | `S -> S -> I` | Write string to file (0 = success) |
| `args` | `-> [S]` | Command-line arguments as string array |
| `system` | `S -> I` | Execute shell command, return exit code |

Note: `read_file` uses `fseek`/`ftell` and does not work on stdin/pipes.

### SQLite Database

Available when program uses `glyph_db_*` functions. Links `-lsqlite3`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `glyph_db_open` | `S -> I` | Open database, return handle (0 = error) |
| `glyph_db_close` | `I -> V` | Close database handle |
| `glyph_db_exec` | `I -> S -> I` | Execute SQL (no result), 0 = success |
| `glyph_db_query_rows` | `I -> S -> [[S]]` | Query returning rows of string columns |
| `glyph_db_query_one` | `I -> S -> S` | Query returning single value |

Database handles are opaque integers (cast to `sqlite3*` internally).

### Test Assertions

Only available in test builds (`./glyph test`):

| Function | Signature | Description |
|----------|-----------|-------------|
| `assert` | `I -> I` | Fail if argument is 0 |
| `assert_eq` | `I -> I -> I` | Fail if integers not equal |
| `assert_str_eq` | `S -> S -> I` | Fail if strings not equal |

All assertions return 0 (not void), so the test suite continues after failures. A global `_test_failed` flag tracks whether any assertion failed.

## 12. CLI Reference

```
Usage: glyph <command> <db> [args...]
```

The `<db>` argument is always the path to a `.glyph` database file.

### Build Commands

**`init <db>`** — Create a new `.glyph` database with the full schema.

**`build <db> [output]`** — Compile all `fn` definitions to a native executable. Output defaults to `a.out` if not specified. Reads function definitions from the database, compiles each through tokenize → parse → MIR lower → C codegen, prepends the C runtime, writes to `/tmp/glyph_out.c`, and invokes `cc` to produce the binary.

**`run <db>`** — Build to `/tmp/glyph_run_tmp` and execute immediately.

**`test <db> [name...]`** — Compile and run test definitions. With no names, runs all tests. With names, filters to matching tests. Exit code 0 = all pass, 1 = any failure.

**`check <db>`** — Lightweight check: reads all fn definitions and reports count.

### Definition Management

**`get <db> <name> [--kind K]`** — Print a definition's source body. Optional `--kind` to disambiguate when multiple kinds share a name.

**`put <db> <kind> -b <body>`** — Insert or update a definition. The `<kind>` is `fn`, `type`, `test`, etc. The definition name is extracted from the body's first token. Uses DELETE+INSERT to avoid trigger issues.

**`put <db> <kind> -f <file>`** — Insert or update from a file.

**`rm <db> <name> [--force]`** — Remove a definition. Checks reverse dependencies first; use `--force` to skip the check.

### Query Commands

**`ls <db> [--kind K] [--sort S]`** — List definitions. Filter by `--kind`. Sort by `--sort tokens` (by token count), `--sort name` (alphabetical), or default (by kind then name).

**`find <db> <pattern> [--body]`** — Search definition names for pattern. With `--body`, also searches in definition bodies.

**`deps <db> <name>`** — Show forward dependencies (what this definition calls/uses). Requires a prior `build` to populate the dep table.

**`rdeps <db> <name>`** — Show reverse dependencies (what calls/uses this definition). Requires a prior `build`.

**`stat <db>`** — Show database overview: definition count by kind, extern count, total tokens, dirty count.

**`dump <db> [--budget N] [--root NAME] [--all] [--sigs]`** — Token-budgeted context export. `--budget N` limits total tokens. `--root` starts from a specific definition. `--all` includes all definitions. `--sigs` includes type signatures.

### Low-Level Commands

**`sql <db> <query>`** — Execute raw SQL. SELECT queries return formatted rows. Other statements execute and report success/failure.

**`extern <db> <name> <symbol> <sig> [--lib L]`** — Add a foreign function declaration to the `extern_` table. Optional `--lib` for library name (e.g., `sqlite3`).

## 13. Foreign Function Interface

### Declaring Externs

**Via CLI:**
```bash
./glyph extern db.glyph my_func my_c_func "I -> I -> S"
./glyph extern db.glyph sqlite_open sqlite3_open "S -> I" --lib sqlite3
```

**Via SQL:**
```sql
INSERT INTO extern_ (name, symbol, sig)
VALUES ('my_func', 'my_c_func', 'I -> I -> S');
```

### extern_ Table Schema

| Column | Description |
|--------|-------------|
| `name` | Glyph-side name used in source code |
| `symbol` | C symbol name in the linked library |
| `lib` | Library to link (e.g., `sqlite3` → `-lsqlite3`) |
| `sig` | Glyph type signature using standard type aliases |
| `conv` | Calling convention: `C` (default), `system`, `rust` |

### Type Mapping

| Glyph Type | C Type | Size |
|------------|--------|------|
| `I` (Int) | `long long` | 8 bytes |
| `U` (UInt) | `unsigned long long` | 8 bytes |
| `F` (Float) | `double` | 8 bytes |
| `B` (Bool) | `long long` (0/1) | 8 bytes at ABI boundary |
| `S` (Str) | `void*` (pointer to `{char*, long long}`) | 8 bytes (pointer) |
| `V` (Void) | `void` or `long long 0` | — |
| `[T]` (Array) | `void*` (pointer to `{long long*, long long, long long}`) | 8 bytes (pointer) |
| `*T` (Ptr) | `void*` | 8 bytes |

### Runtime Function Recognition

The self-hosted compiler recognizes runtime functions through a chain: `is_runtime_fn → is_runtime_fn2 → is_runtime_fn3 → is_runtime_fn4 → is_runtime_fn5`. Each checks a batch of names and returns 1 if the function is a known runtime function. Runtime functions are prefixed with `glyph_` in the generated C code (e.g., `println` becomes `glyph_println`).

## 14. Compilation Pipeline

### Overview

```
.glyph DB → SELECT fn defs → Tokenize → Parse → Type Infer → MIR Lower → Codegen → Linker → executable
```

### Step by Step

1. **Read**: Query `SELECT name, body FROM def WHERE kind='fn'` from the database
2. **Tokenize**: Indentation-sensitive lexer produces token stream per definition
3. **Parse**: Recursive-descent parser builds AST per definition
4. **Type Inference** (Rust compiler only): Hindley-Milner inference with row polymorphism, two-pass (pre-register signatures, then infer bodies)
5. **MIR Lowering**: AST → flat control-flow graph (basic blocks, SSA-like assignments, terminators)
6. **Code Generation**:
   - **Rust compiler**: Cranelift IR → native machine code
   - **Self-hosted compiler**: MIR → C source code → `cc` → native binary
7. **Linking**: Runtime library linked in, extern libraries linked via `-l` flags

### Incremental Compilation

The Rust compiler supports incremental compilation:
1. Each definition has a BLAKE3 content hash
2. On `build`, only `v_dirty` definitions (dirty + transitive dependents) are recompiled
3. After successful compilation, definitions are marked `compiled = 1`
4. `build --full` forces recompilation of everything

The self-hosted compiler always does full compilation.

### Self-Hosted Compiler Pipeline

```
compile_fn(source) = tokenize → parse_fn_def → lower_fn_def → MIR record

build_program(db_path, output):
  1. Read all fn defs from database
  2. compile_fn each → list of MIR records
  3. cg_program(mirs) → C source code string
  4. Prepend cg_runtime_c (embedded C runtime)
  5. Write to /tmp/glyph_out.c
  6. glyph_system("cc -O2 -o <output> /tmp/glyph_out.c")
```

### Main Wrapper Generation

The generated C code wraps the Glyph `main` function:

```c
long long glyph_main(void);  // forward declaration of compiled main

int main(int argc, char** argv) {
    glyph_set_args(argc, argv);
    return (int)glyph_main();
}
```

## 15. Testing

### Writing Tests

Test definitions use `kind='test'` in the database. A test is a zero-argument function that calls assertion functions:

```bash
./glyph put db.glyph test -b 'test_add = assert_eq(add(1, 2), 3)'

./glyph put db.glyph test -b 'test_strings =
  s = "hello world"
  assert_eq(str_len(s), 11)
  assert_str_eq(str_slice(s, 0, 5), "hello")'

./glyph put db.glyph test -b 'test_factorial =
  assert_eq(factorial(0), 1)
  assert_eq(factorial(1), 1)
  assert_eq(factorial(5), 120)
  assert_eq(factorial(10), 3628800)'
```

Tests can call any `kind='fn'` definition in the same database. They cannot call other tests.

### Running Tests

```bash
# Run all tests
./glyph test db.glyph

# Run specific tests by name
./glyph test db.glyph test_add test_strings

# Output example:
# Compiling 3 tests...
# PASS test_add
# PASS test_factorial
#   FAIL test_strings: strings differ
# 2/3 passed
```

Exit codes: `0` = all tests passed, `1` = any test failed.

### Test Framework Architecture

The testing framework is implemented entirely in the self-hosted compiler (12 definitions). It follows a single-binary architecture with a dispatch table.

#### Compilation Pipeline

```
cmd_test(argv, argc)
  ├── read_test_names(db) → [S]         -- SELECT name FROM def WHERE kind='test'
  ├── read_test_defs(db) → [S]          -- SELECT body FROM def WHERE kind='test'
  ├── read_fn_defs(db) → [S]            -- SELECT body FROM def WHERE kind='fn'
  └── build_test_program(fn_sources, test_sources, test_names, output_path)
        ├── compile_fns(fn_sources)      -- compile all fn defs to MIR
        ├── compile_fns(test_sources)    -- compile all test defs to MIR
        ├── cg_test_program(fn_mirs, test_mirs, test_names) → C source
        │     ├── cg_preamble()          -- standard C includes + runtime decls
        │     ├── cg_test_preamble_extra()  -- assert function declarations
        │     ├── cg_forward_decls()     -- forward declarations for fn + test defs
        │     ├── cg_test_forward_decls()   -- forward decls for test names
        │     ├── cg_functions()         -- compiled fn + test function bodies
        │     └── cg_test_dispatch()     -- main() with dispatch table
        ├── Prepend cg_runtime_full() + cg_test_runtime()
        ├── Write to /tmp/glyph_test.c
        └── cc -O1 -o <output> /tmp/glyph_test.c
```

#### Separation from Normal Builds

Test definitions (`kind='test'`) are **excluded from normal builds**. The `build` command queries `WHERE kind='fn'`, so tests are never compiled into the production binary. The `test` command uses separate `read_test_defs` + `read_fn_defs` queries to compile both function definitions (needed as dependencies) and test definitions into a single test binary.

#### Generated Test Binary Structure

The generated C code for a test binary has this structure:

```c
// 1. C runtime (glyph_runtime_full + glyph_test_runtime)
// ... memory, strings, arrays, I/O functions ...

// 2. Test runtime globals and assertions
static int _test_failed = 0;
static const char* _test_name = "";

long long glyph_assert(long long cond) {
  if (!cond) { fprintf(stderr, "  FAIL %s: assertion failed\n", _test_name); _test_failed = 1; }
  return 0;
}

long long glyph_assert_eq(long long a, long long b) {
  if (a != b) { fprintf(stderr, "  FAIL %s: %lld != %lld\n", _test_name, a, b); _test_failed = 1; }
  return 0;
}

long long glyph_assert_str_eq(void* a, void* b) {
  if (!glyph_str_eq(a, b)) { fprintf(stderr, "  FAIL %s: strings differ\n", _test_name); _test_failed = 1; }
  return 0;
}

// 3. Forward declarations for all fn + test functions
long long add(long long a, long long b);
long long test_add(void);
// ...

// 4. Compiled function bodies (fn defs)
long long add(long long a, long long b) { return a + b; }

// 5. Compiled test bodies
long long test_add(void) { return glyph_assert_eq(add(1, 2), 3); }

// 6. Test dispatch with filtering
static int should_run(const char* name, int argc, char** argv) {
  if (argc <= 1) return 1;  // no filter = run all
  for (int i = 1; i < argc; i++)
    if (strcmp(argv[i], name) == 0) return 1;
  return 0;
}

int main(int argc, char** argv) {
  int passed = 0, failed = 0;
  if (should_run("test_add", argc, argv)) {
    _test_name = "test_add"; _test_failed = 0;
    test_add();
    if (_test_failed) { failed++; } else { printf("PASS %s\n", "test_add"); passed++; }
  }
  // ... repeat for each test ...
  printf("%d/%d passed\n", passed, passed + failed);
  return failed > 0 ? 1 : 0;
}
```

#### Assertion Functions

Three assertion functions are provided by `cg_test_runtime`:

| Function | Signature | On Failure |
|----------|-----------|------------|
| `assert(cond)` | `I -> I` | `FAIL test_name: assertion failed` |
| `assert_eq(a, b)` | `I -> I -> I` | `FAIL test_name: 42 != 99` (shows values) |
| `assert_str_eq(a, b)` | `S -> S -> I` | `FAIL test_name: strings differ` |

All assertions return `long long 0` (not void). This is important because:
1. Glyph's type system expects expressions to have values
2. Tests can chain multiple assertions as expression statements
3. The test suite continues after failures — assertions set `_test_failed = 1` but don't abort

#### Test Filtering

When command-line arguments are provided after the database path, only matching test names run:

```bash
./glyph test db.glyph test_add          # run only test_add
./glyph test db.glyph test_add test_mul # run test_add and test_mul
./glyph test db.glyph                   # run all tests
```

The filtering is passed through to the test binary. The `cmd_test` function constructs the command: if `argc >= 4`, it joins the remaining arguments and appends them to the binary path. The binary's `should_run()` function checks each test name against the argument list.

#### Self-Hosted Compiler Definitions

The test framework adds 12 new definitions to `glyph.glyph`:

| Definition | Role |
|------------|------|
| `cg_test_runtime` | C source for assertion functions + globals |
| `cg_test_preamble_extra` | `extern` declarations for assert/assert_eq/assert_str_eq |
| `cg_test_forward_decls` | Forward declarations for test function names |
| `cg_test_dispatch_case` | Recursive generator for per-test dispatch blocks |
| `cg_test_dispatch` | Complete `main()` with `should_run` + dispatch loop |
| `cg_test_program` | Assembles full C source (preamble + decls + fns + tests + dispatch) |
| `build_test_program` | Top-level: compile MIR, generate C, invoke cc |
| `cmd_test` | CLI handler: read defs, compile, execute |
| `read_test_defs` | Query test bodies from database |
| `read_test_names` | Query test names from database |

Plus 2 modified definitions: `is_runtime_fn5` (recognizes assert functions) and `dispatch_cmd4` (routes `test` command).

#### Runtime Function Chain

Assertion functions are recognized by the compiler's runtime function chain. `is_runtime_fn5` handles `assert`, `assert_eq`, and `assert_str_eq`, ensuring they are prefixed with `glyph_` in the generated C code and not treated as user-defined functions.

### Test Naming Convention

Test names should be descriptive identifiers. By convention, prefix with `test_`:

```
test_factorial = assert_eq(factorial(5), 120)
test_empty_array = assert_eq(array_len([]), 0)
test_string_concat =
  s = "hello" + " " + "world"
  assert_str_eq(s, "hello world")
```

### Complete Test Example

```bash
# Create a program with functions and tests
./glyph init math.glyph

./glyph put math.glyph fn -b 'add a b = a + b'
./glyph put math.glyph fn -b 'mul a b = a * b'
./glyph put math.glyph fn -b 'factorial n =
  match n
    0 -> 1
    _ -> n * factorial(n - 1)'

./glyph put math.glyph test -b 'test_add =
  assert_eq(add(1, 2), 3)
  assert_eq(add(0, 0), 0)
  assert_eq(add(-1, 1), 0)'

./glyph put math.glyph test -b 'test_mul =
  assert_eq(mul(3, 4), 12)
  assert_eq(mul(0, 100), 0)'

./glyph put math.glyph test -b 'test_factorial =
  assert_eq(factorial(0), 1)
  assert_eq(factorial(1), 1)
  assert_eq(factorial(5), 120)
  assert_eq(factorial(10), 3628800)'

# Run all tests
./glyph test math.glyph
# Output:
# Compiling 3 tests...
# PASS test_add
# PASS test_factorial
# PASS test_mul
# 3/3 passed

# Run specific test
./glyph test math.glyph test_factorial
# Output:
# Compiling 3 tests...
# PASS test_factorial
# 1/1 passed
```

### Limitations

- **C keyword collision**: Test function names that are C reserved words will cause codegen errors.
- **No test isolation**: All tests run in the same process. A crash in one test aborts the entire suite.
- **No setup/teardown**: No before/after hooks. Each test must set up its own state.
- **Integer assertions only**: `assert_eq` compares `long long` values. For strings, use `assert_str_eq`. There is no `assert_neq` or `assert_gt`.
- **No test output capture**: Test output (from `println`) goes to stdout mixed with PASS/FAIL messages.
- **Compilation includes all fns**: Even when filtering tests, all `kind='fn'` definitions are compiled into the test binary (needed because any test might call any function).

## 16. Practical Guide and Examples

### Hello World

```
main = println("Hello, world")
```

### Arithmetic

```
add a b = a + b
mul a b = a * b
square x = x * x

main = println(int_to_str(square(add(3, 4))))
-- Output: 49
```

### Fibonacci (recursive)

```
fib n =
  match n
    0 -> 0
    1 -> 1
    _ -> fib(n - 1) + fib(n - 2)

main = println(int_to_str(fib(10)))
-- Output: 55
```

### Factorial (recursive)

```
factorial n =
  match n
    0 -> 1
    _ -> n * factorial(n - 1)

main = println(int_to_str(factorial(10)))
-- Output: 3628800
```

### String Operations

```
main =
  s = "Hello, Glyph"
  println(int_to_str(str_len(s)))
  println(str_slice(s, 0, 5))
  println(str_slice(s, 7, 12))
-- Output:
-- 12
-- Hello
-- Glyph
```

### String Interpolation

```
greet name age =
  println("Hello, {name}! You are {int_to_str(age)} years old.")

main = greet("Alice", 30)
-- Output: Hello, Alice! You are 30 years old.
```

### Array Processing with For-Loop Pattern

Glyph for-loops desugar to index-based MIR loops with array push. The idiomatic pattern for accumulation uses a mutable variable:

```
sum_array arr =
  total = [0]
  i = [0]
  match arr
    _ ->
      -- manual loop using recursion
      0
  -- For actual array iteration, use index + recursion:

sum_recursive arr i =
  match i >= array_len(arr)
    true -> 0
    false -> arr[i] + sum_recursive(arr, i + 1)

main = println(int_to_str(sum_recursive([10, 20, 30], 0)))
-- Output: 60
```

### Pattern Matching

```
classify n =
  match n
    0 -> "zero"
    1 -> "one"
    _ -> "other"

main =
  println(classify(0))
  println(classify(1))
  println(classify(42))
-- Output:
-- zero
-- one
-- other
```

### Enum Variants

First define the type, then use constructors in code:

```
-- Type definition (kind='type'):
Option = | None | Some(I)

-- Functions (kind='fn'):
test_some =
  x = Some(42)
  match x
    None -> println("none")
    Some(v) -> println("got: " + int_to_str(v))

test_none =
  x = None
  match x
    None -> println("none")
    Some(v) -> println("got: " + int_to_str(v))

main =
  test_some()
  test_none()
-- Output:
-- got: 42
-- none
```

### Record Creation and Field Access

```
make_point x y = {x: x, y: y}

distance p =
  p.x * p.x + p.y * p.y

main =
  p = make_point(3, 4)
  println(int_to_str(distance(p)))
-- Output: 25
```

### Closures and Higher-Order Functions

```
apply f x = f(x)
compose f g x = g(f(x))

main =
  double = \x -> x * 2
  inc = \x -> x + 1
  println(int_to_str(apply(double, 5)))
  println(int_to_str(compose(double, inc, 3)))
-- Output:
-- 10
-- 7
```

### Pipe Operator

```
main =
  result = 5 |> (\x -> x * 2) |> (\x -> x + 1) |> int_to_str
  println(result)
-- Output: 11
```

### File I/O

```
main =
  write_file("/tmp/hello.txt", "Hello from Glyph!")
  content = read_file("/tmp/hello.txt")
  println(content)
-- Output: Hello from Glyph!
```

### Database Access

```
main =
  db = glyph_db_open("/tmp/test.db")
  glyph_db_exec(db, "CREATE TABLE IF NOT EXISTS kv (key TEXT, val TEXT)")
  glyph_db_exec(db, "INSERT INTO kv VALUES ('name', 'Glyph')")
  result = glyph_db_query_one(db, "SELECT val FROM kv WHERE key='name'")
  println(result)
  glyph_db_close(db)
-- Output: Glyph
```

### Multi-Function Program

```
-- helper functions
is_even n = n % 2 == 0

fizzbuzz n =
  match is_even(n)
    true -> "even"
    false -> "odd"

process_range n max =
  match n > max
    true -> 0
    false ->
      println(int_to_str(n) + " is " + fizzbuzz(n))
      process_range(n + 1, max)

main = process_range(1, 5)
-- Output:
-- 1 is odd
-- 2 is even
-- 3 is odd
-- 4 is even
-- 5 is odd
```

### Raw Strings

```
main =
  s = r"no escapes: \n \t \\"
  println(s)
-- Output: no escapes: \n \t \\
```

### Mutable State Pattern (Single-Element Array)

Glyph has no mutable local variables. The idiomatic workaround uses a single-element array:

```
counter =
  c = [0]
  array_set(c, 0, c[0] + 1)
  array_set(c, 0, c[0] + 1)
  array_set(c, 0, c[0] + 1)
  c[0]

main = println(int_to_str(counter()))
-- Output: 3
```

### Boolean Operations

```
check x =
  match x > 0 && x < 100
    true -> "in range"
    false -> "out of range"

main =
  println(check(50))
  println(check(200))
-- Output:
-- in range
-- out of range
```

### Command-Line Arguments

```
main =
  a = args()
  i = [0]
  -- print argument count
  println("argc: " + int_to_str(array_len(a)))
```

## 17. Known Limitations

### C Keyword Collision
Glyph function names that are C reserved words (`double`, `int`, `float`, `void`, `return`, `if`, `else`, `while`, `for`, `switch`, `case`, `break`, `continue`, `struct`, `typedef`, `enum`, `union`, `const`, `static`, `extern`, `register`, `volatile`, `sizeof`, `goto`, `default`, `do`, `long`, `short`, `signed`, `unsigned`, `char`, `auto`, `inline`, `restrict`) will cause C codegen errors. Avoid these as definition names.

### Field Offset Ambiguity
When multiple record types share the same field names and the only fields accessed on a variable are the shared ones, the codegen may resolve to the wrong record type. Mitigated when a type-unique field (like `.tag` or `.kind`) is also accessed on the same variable.

### Zero-Argument Function Eager Evaluation
Glyph zero-argument functions with side effects (e.g., `println`) may be evaluated eagerly at definition site. Workaround: add a dummy parameter: `print_usage u = println("Usage: ...")`.

### String Brace Escaping
In string literals, `{` triggers interpolation. Use `\{` for a literal open brace. Closing brace `}` is safe outside interpolation. In generated C code within the self-hosted compiler, use `cg_lbrace()` and `cg_rbrace()` helpers.

### No Stdin Support
`read_file` uses `fseek`/`ftell` and cannot read from stdin or pipes. Use temporary files or the `-b` flag for CLI input.

### Trigger Management
The Rust compiler's SQLite triggers use custom functions (`glyph_hash`, `glyph_tokens`) not available in plain `sqlite3`. When modifying `glyph.glyph` via external tools:
1. `DROP TRIGGER trg_def_dirty; DROP TRIGGER trg_dep_dirty;`
2. Make changes
3. Recreate triggers
4. Run `cargo run -- build glyph.glyph --full` to recompute hashes/tokens

### Type Checker False Positives
The `!` unwrap operator may produce type mismatch warnings from the type checker. This is a type system limitation — the runtime behavior is correct.

### No Garbage Collection
All heap allocations persist until process exit. Long-running programs or programs with heavy allocation will consume proportionally more memory.

### Self-Hosted Compiler Nesting Limit
The self-hosted compiler's string builder (`cg_runtime_c`) cannot have more than ~7 nested `s2()` calls without stack overflow. Combine strings at the same nesting level instead.

### Token Counts in Self-Hosted Compiler
The self-hosted compiler inserts definitions with `tokens=0` and `hash=zeroblob(32)`. Correct values are computed when the Rust compiler runs `build --full` on the database.

### Boolean I8/I64 Mismatch
Internally, `Bool` maps to Cranelift `I8` but most values are `I64`. The codegen inserts `uextend`/`ireduce` coercion at variable assignment and function return boundaries. This is handled automatically but can cause subtle issues in FFI.

### Dependency Table Population
The `deps` and `rdeps` CLI commands only work after a `build` that populates the `dep` table. Freshly inserted definitions have no dependency edges until compiled.
