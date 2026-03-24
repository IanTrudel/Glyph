# Glyph Self-Hosted Programming Manual

A comprehensive reference for the Glyph programming language and its self-hosted compiler.

**Version:** 0.2 (February 2026)

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Quick Start](#2-quick-start)
3. [The Database Program Model](#3-the-database-program-model)
4. [CLI Reference](#4-cli-reference)
5. [Lexical Grammar](#5-lexical-grammar)
6. [Expressions](#6-expressions)
7. [Statements and Blocks](#7-statements-and-blocks)
8. [Definitions](#8-definitions)
9. [Type System](#9-type-system)
10. [Pattern Matching](#10-pattern-matching)
11. [Closures and Lambdas](#11-closures-and-lambdas)
12. [Strings](#12-strings)
13. [Arrays](#13-arrays)
14. [Records and Structs](#14-records-and-structs)
15. [Enums and Algebraic Types](#15-enums-and-algebraic-types)
16. [Float Support](#16-float-support)
17. [Error Handling](#17-error-handling)
18. [Foreign Function Interface](#18-foreign-function-interface)
19. [Testing Framework](#19-testing-framework)
20. [Runtime Function Reference](#20-runtime-function-reference)
21. [Generational Versioning (Gen 2)](#21-generational-versioning-gen-2)
22. [Named Record Types (Gen 2 Struct Codegen)](#22-named-record-types-gen-2-struct-codegen)
23. [C-Layout Structs](#23-c-layout-structs)
24. [MCP Server](#24-mcp-server)
25. [Build Modes](#25-build-modes)
26. [Compilation Pipeline](#26-compilation-pipeline)
27. [Bootstrap Chain](#27-bootstrap-chain)
28. [Example Programs](#28-example-programs)
29. [Pitfalls and Limitations](#29-pitfalls-and-limitations)
30. [Appendix: Operator Precedence](#30-appendix-operator-precedence)
31. [Appendix: Memory Layout](#31-appendix-memory-layout)
32. [Appendix: Schema Reference](#32-appendix-schema-reference)

---

## 1. Introduction

Glyph is an LLM-native programming language where **programs are SQLite3 databases** (`.glyph` files), not source files. The unit of storage is the *definition* — a named, typed, hashed source fragment stored as a row in SQL. SQL queries replace the traditional module/import system.

### Design Goals

- **Token-minimal syntax** — reduce BPE token count for LLM generation and consumption
- **SQL as module system** — read context via `SELECT`, write definitions via `INSERT`
- **Incremental compilation** — content hashing and dependency tracking
- **No ceremony** — no semicolons, no braces, no `return`/`let`/`import` keywords

### Two Compilers

| | Self-Hosted (`./glyph`) | Rust (`cargo run --`) |
|---|---|---|
| **Use for** | Application development | Compiler development |
| **Backend** | C codegen → cc | Cranelift → native |
| **Build** | `./glyph build app.glyph out` | `cargo run -- build app.glyph` |
| **Run** | `./glyph run app.glyph` | `cargo run -- run app.glyph` |
| **Tests** | `./glyph test app.glyph` | `cargo run -- test app.glyph` |

The self-hosted compiler is the primary compiler. Use it for all application development.

### Statistics

- ~1,019 definitions in the self-hosted compiler (942 gen=1, 77 gen=2)
- 95 test definitions
- 6 type definitions
- C codegen backend producing ~30k binaries
- Full 3-stage bootstrap chain

---

## 2. Quick Start

### Create a Program

```bash
./glyph init hello.glyph
```

### Add a Definition

```bash
./glyph put hello.glyph fn -b 'main = println("Hello, world")'
```

### Build and Run

```bash
./glyph build hello.glyph hello
./hello
# Output: Hello, world
```

Or in one step:

```bash
./glyph run hello.glyph
# Output: Hello, world
```

### Multi-Function Program

```bash
./glyph put hello.glyph fn -b 'greet name = println("Hello, " + name)'
./glyph put hello.glyph fn -b 'main = greet("Glyph")'
./glyph run hello.glyph
# Output: Hello, Glyph
```

### From a File

For definitions with special characters, quotes, or multiple lines, write to a temp file first:

```bash
cat > /tmp/main.gl << 'EOF'
main =
  println("factorial(10) = " + int_to_str(factorial(10)))
  println("fib(10) = " + int_to_str(fib(10)))
EOF
./glyph put hello.glyph fn -f /tmp/main.gl
```

---

## 3. The Database Program Model

A Glyph program is a `.glyph` file — a SQLite3 database. There are no source files.

### Core Tables

| Table | Purpose |
|-------|---------|
| `def` | All definitions: functions, types, tests |
| `dep` | Dependency graph edges between definitions |
| `extern_` | C ABI foreign function declarations |
| `tag` | Key-value metadata tags on definitions |
| `module` / `module_member` | Logical grouping with export flags |
| `compiled` | Cached compilation artifacts |
| `def_history` | Automatic change history (via triggers) |
| `meta` | Schema version and metadata |

### The `def` Table

```sql
CREATE TABLE def (
  id        INTEGER PRIMARY KEY,
  name      TEXT NOT NULL,
  kind      TEXT NOT NULL,  -- fn, type, test, trait, impl, const, fsm, srv, macro
  sig       TEXT DEFAULT '',
  body      TEXT NOT NULL,
  hash      BLOB NOT NULL,
  tokens    INTEGER DEFAULT 0,
  compiled  INTEGER DEFAULT 0,
  gen       INTEGER DEFAULT 1,
  created   TEXT DEFAULT (datetime('now')),
  modified  TEXT DEFAULT (datetime('now'))
);
```

### Key Views

| View | Description |
|------|-------------|
| `v_dirty` | Dirty definitions + transitive dependents |
| `v_context` | Definitions sorted by dependency depth |
| `v_callgraph` | Caller/callee/edge triples |

### Interacting via SQL

You can query any `.glyph` database directly with `sqlite3`:

```bash
sqlite3 app.glyph "SELECT name, kind FROM def ORDER BY name"
sqlite3 app.glyph "SELECT body FROM def WHERE name='main'"
```

Or use the built-in `sql` command:

```bash
./glyph sql app.glyph "SELECT name, kind FROM def ORDER BY name"
```

---

## 4. CLI Reference

All commands follow the pattern: `./glyph <command> <db.glyph> [args...]`

### Definition Management

| Command | Usage | Description |
|---------|-------|-------------|
| `init` | `init <db>` | Create a new `.glyph` database with full schema |
| `put` | `put <db> <kind> -b '<body>'` | Insert or update a definition |
| `put` | `put <db> <kind> -f <file>` | Insert definition from file |
| `put` | `put <db> <kind> -b '...' --gen N` | Insert at specific generation |
| `get` | `get <db> <name> [--kind K]` | Print a definition's source body |
| `rm` | `rm <db> <name> [--force]` | Remove definition (checks reverse deps) |
| `undo` | `undo <db> <name> [--kind K]` | Undo last change (reversible, run again to swap back) |
| `history` | `history <db> <name> [--kind K]` | Show change history |

### Building and Running

| Command | Usage | Description |
|---------|-------|-------------|
| `build` | `build <db> [output]` | Compile to native executable (default: `a.out`) |
| `build` | `build <db> out --debug` | Compile with debug symbols (O0 -g) |
| `build` | `build <db> out --release` | Compile optimized (O2) |
| `run` | `run <db>` | Build to temp file and execute |
| `test` | `test <db> [name...]` | Compile and run test definitions |
| `check` | `check <db>` | Type-check only, report counts |

### Query and Analysis

| Command | Usage | Description |
|---------|-------|-------------|
| `ls` | `ls <db> [--kind K] [--sort S]` | List definitions (sort: tokens, name, default) |
| `find` | `find <db> <pattern> [--body]` | Search names and optionally bodies |
| `deps` | `deps <db> <name>` | Forward dependencies (requires prior build) |
| `rdeps` | `rdeps <db> <name>` | Reverse dependencies (requires prior build) |
| `stat` | `stat <db>` | Overview: counts, tokens, dirty defs |
| `dump` | `dump <db> [--budget N] [--all]` | Token-budgeted context export |

### Advanced

| Command | Usage | Description |
|---------|-------|-------------|
| `sql` | `sql <db> <query>` | Execute raw SQL, formatted output |
| `extern` | `extern <db> <name> <sym> <sig> [--lib L]` | Add FFI declaration |

---

## 5. Lexical Grammar

### Identifiers and Keywords

```
identifier    = [a-z_][a-zA-Z0-9_]*     -- lowercase start
type_ident    = [A-Z][a-zA-Z0-9_]*       -- uppercase start (types, constructors)
```

Reserved words: `match`, `true`, `false`, `impl`, `for`

### Literals

```
42              -- integer (i64)
1_000_000       -- integer with underscores
3.14            -- float (f64)
"hello"         -- string
"x = {expr}"    -- string with interpolation
r"raw\n"        -- raw string (no escape processing)
true false      -- boolean
```

### Operators and Punctuation

```
+  -  *  /  %          -- arithmetic
== != < > <= >=        -- comparison
&& ||                  -- logical
|> >>                  -- pipe, compose
? !                    -- error propagate, unwrap
& *                    -- ref, deref (prefix)
- !                    -- negate, not (prefix)
.                      -- field access
= :=                   -- let binding, assignment
->                     -- arrow (match arms, function types)
\                      -- lambda
bitand bitor bitxor    -- bitwise AND, OR, XOR
shl shr                -- bitwise shift left, shift right
```

### Comments

```
-- this is a line comment
x = 42  -- inline comment
```

### Indentation Rules

- Tab width: 2 spaces
- The lexer emits INDENT, DEDENT, and NEWLINE tokens
- Inside `()`, `[]`, `{}`: layout is suppressed (free-form)
- Blank lines are ignored

---

## 6. Expressions

### Operator Precedence (Low to High)

| Level | Operators | Associativity |
|-------|-----------|---------------|
| 1 | `\|>` (pipe) | Left |
| 2 | `>>` (compose) | Left |
| 3 | `\|\|` (logical or) | Left |
| 4 | `&&` (logical and) | Left |
| 5 | `== != < > <= >=` (comparison) | None |
| 6 | `+ -` (additive) | Left |
| 7 | `* / %` (multiplicative) | Left |
| 8 | `- ! & *` (prefix unary) | Right |
| 9 | `. () [] ? !` (postfix) | Left |

### Atoms

```
42                      -- integer literal
3.14                    -- float literal
"hello"                 -- string literal
r"raw\n"                -- raw string (no escapes)
true                    -- boolean true
false                   -- boolean false
name                    -- identifier (variable/function reference)
Name                    -- type identifier / constructor
(expr)                  -- grouping
```

### Arithmetic

```
a + b       -- addition (also string concatenation)
a - b       -- subtraction
a * b       -- multiplication
a / b       -- integer or float division
a % b       -- modulo
-a          -- unary negation
```

### Comparison and Logical

```
a == b      -- equality (also string equality)
a != b      -- inequality
a < b       -- less than
a > b       -- greater than
a <= b      -- less or equal
a >= b      -- greater or equal
a && b      -- logical AND (short-circuit)
a || b      -- logical OR (short-circuit)
!a          -- logical NOT
```

### Bitwise Operators

Glyph uses keyword-style bitwise operators to avoid ambiguity with other symbols:

```
a bitand b   -- bitwise AND
a bitor b    -- bitwise OR
a bitxor b   -- bitwise XOR
a shl n      -- left shift
a shr n      -- right shift
```

Examples:

```
5 bitand 3    -- 1
5 bitor 3     -- 7
5 bitxor 3    -- 6
1 shl 3       -- 8
16 shr 2      -- 4
255 bitand 15 -- 15
```

### Function Calls

```
f(x)            -- single argument
f(x, y, z)     -- multiple arguments
f(g(x))         -- nested calls
```

### Field Access

```
record.field        -- access field on a record
record.field.sub    -- chained field access
.field              -- field accessor shorthand (creates a lambda)
```

The `.field` shorthand creates a lambda: `.name` is equivalent to `\x -> x.name`. Useful with pipe:

```
people |> .name     -- equivalent to: \x -> x.name
```

### Array Indexing

```
arr[0]          -- index into array
arr[i + 1]      -- computed index
```

### Pipe and Compose

```
x |> f          -- pipe: equivalent to f(x)
x |> f |> g     -- chain: equivalent to g(f(x))
f >> g          -- compose: creates \x -> g(f(x))
```

### Error Propagation and Unwrap

```
expr?           -- if Err, return it; if Ok, extract payload
expr!           -- if Err, panic; if Ok, extract payload
```

### Lambda (Anonymous Function)

```
\x -> x + 1                    -- single parameter
\x y -> x + y                  -- multiple parameters
\x -> match x
  0 -> "zero"
  _ -> "other"
```

### Match Expression

```
match expr
  pattern1 -> result1
  pattern2 -> result2
  _ -> default
```

See [Pattern Matching](#10-pattern-matching) for full details.

### Block Expression

A block is an indented sequence of statements. The value of the last expression is the block's value:

```
result =
  x = compute()
  y = transform(x)
  x + y
```

### Array Literal and Range

```
[1, 2, 3, 4]           -- array literal
[1..10]                 -- range (1 through 9)
[]                      -- empty array
```

### Record Literal

```
{x: 10, y: 20}                 -- anonymous record
{name: "Alice", age: 30}       -- string and integer fields
```

Fields are stored in alphabetical order internally.

### Tuple

```
(a, b)          -- pair
(a, b, c)       -- triple
```

---

## 7. Statements and Blocks

Inside a block (indented region), there are three statement forms:

### Let Binding

```
name = expr
```

Introduces a new local variable. There is no `let` keyword.

```
x = 42
name = "Alice"
result = compute(x)
```

### Assignment (Mutation)

```
lvalue := expr
```

Mutates an existing variable. Uses `:=` (not `=`).

```
count := count + 1
arr[i] := new_value
```

### Expression Statement

Any expression can be a statement. Its value is discarded.

```
println("hello")
array_push(arr, 42)
```

### Return Value

The last expression in a block is its return value. There is no `return` keyword:

```
max a b =
  result = match a > b
    true -> a
    _ -> b
  result
```

---

## 8. Definitions

### Function Definition (`kind='fn'`)

```
name params = body
name params =
  block
```

Examples:

```bash
# Simple one-liner
./glyph put app.glyph fn -b 'double x = x * 2'

# Multi-line with block
cat > /tmp/fact.gl << 'EOF'
factorial n =
  match n
    0 -> 1
    _ -> n * factorial(n - 1)
EOF
./glyph put app.glyph fn -f /tmp/fact.gl
```

**Zero-argument functions**: Functions with no parameters and side effects are treated as constants (evaluated eagerly). Add a dummy parameter:

```
-- BAD: runs immediately at definition time
print_usage = println("Usage: ...")

-- GOOD: deferred until called
print_usage u = println("Usage: ...")
```

### Type Definition (`kind='type'`)

#### Record Types

```
Point = {x: I, y: I}
Config = {filename: S, verbose: I}
```

#### Enum Types

```
Color = | Red | Green | Blue
Shape = | Circle(I) | Rect(I, I)
Option = | None | Some(I)
```

#### Type Aliases

```
Name = S
Count = I
```

```bash
./glyph put app.glyph type -b 'Point = {x: I, y: I}'
./glyph put app.glyph type -b 'Color = | Red | Green | Blue'
```

### Test Definition (`kind='test'`)

```
test_name u = assertions...
```

Tests need a dummy parameter (like all zero-arg functions with side effects):

```bash
./glyph put app.glyph test -b 'test_add u =
  assert_eq(1 + 1, 2)
  assert_eq(3 + 4, 7)'
```

See [Testing Framework](#19-testing-framework) for details.

---

## 9. Type System

Glyph uses Hindley-Milner type inference with row polymorphism. Types are inferred automatically — no annotations are required.

### Primitive Types

| Alias | Full Name | Size | Description |
|-------|-----------|------|-------------|
| `I` | Int64 | 8 bytes | Default integer type |
| `U` | UInt64 | 8 bytes | Unsigned integer |
| `F` | Float64 | 8 bytes | Double-precision float |
| `S` | Str | 16 bytes | Fat pointer `{ptr, len}` |
| `B` | Bool | 1 byte | i8 internally, i64 at ABI boundary |
| `V` | Void | 0 bytes | Unit type |
| `N` | Never | 0 bytes | Diverging (e.g., `panic`, `exit`) |

### Compound Types

| Syntax | Meaning | Example |
|--------|---------|---------|
| `?T` | Optional | `?I` = optional integer |
| `!T` | Result | `!S` = result with string error |
| `[T]` | Array | `[I]` = array of integers |
| `&T` | Reference | `&I` = reference to integer |
| `*T` | Pointer | `*V` = void pointer |
| `{K: V}` | Map | `{S: I}` = string-to-int map |
| `(A, B)` | Tuple | `(I, S)` = int-string pair |
| `A -> B` | Function | `I -> I` = int to int (right-associative) |
| `{x: I, y: I}` | Record | Named fields, 8 bytes each |

### Type Inference

The compiler infers types for all expressions. The type checker runs automatically during `glyph build` for programs under 200 definitions.

```
-- Types are inferred:
add a b = a + b         -- inferred: I -> I -> I
greet name = "Hi, " + name  -- inferred: S -> S
```

### Let-Polymorphism

Functions are generalized with `∀` (forall) quantification. Each use site instantiates fresh type variables:

```
identity x = x          -- ∀a. a -> a
-- Can be used at different types:
n = identity(42)         -- I -> I
s = identity("hello")   -- S -> S
```

### Row Polymorphism

Record types support row polymorphism — functions can accept records with additional fields:

```
get_name r = r.name      -- works on any record with a .name field
```

---

## 10. Pattern Matching

The `match` expression is Glyph's primary conditional construct. There is no `if/else`.

### Basic Syntax

```
match expr
  pattern -> result
  pattern -> result
  _ -> default
```

### Pattern Kinds

#### Wildcard

```
match x
  _ -> "anything"
```

#### Variable Binding

```
match x
  n -> n + 1       -- binds x to n
```

#### Integer Literal

```
match n
  0 -> "zero"
  1 -> "one"
  _ -> "other"
```

#### String Literal

```
match cmd
  "quit" -> exit(0)
  "help" -> show_help(0)
  _ -> eprintln("unknown command")
```

#### Boolean Literal

Use lowercase `true`/`false` (not `True`/`False`):

```
match condition
  true -> "yes"
  _ -> "no"
```

#### Constructor Pattern

```
match opt
  None -> "empty"
  Some(x) -> "has " + int_to_str(x)

match shape
  Circle(r) -> r * r * 3
  Rect(w, h) -> w * h
```

#### Record Pattern

```
match point
  {x: 0, y: 0} -> "origin"
  {x: x, y: y} -> int_to_str(x) + "," + int_to_str(y)
```

#### Tuple Pattern

```
match pair
  (0, _) -> "first is zero"
  (_, 0) -> "second is zero"
  (a, b) -> a + b
```

### Or-Patterns

Match multiple patterns with the same body using `|`:

```
match n
  1 | 2 | 3 -> "small"
  4 | 5 | 6 -> "medium"
  _ -> "large"
```

Or-patterns work with any pattern kind:

```
match cmd
  "quit" | "exit" | "q" -> exit(0)
  _ -> continue(0)
```

### Match Guards

Add a boolean condition after a pattern with `?`:

```
match n
  n ? n > 100 -> "huge"
  n ? n > 10 -> "big"
  n ? n > 0 -> "positive"
  _ -> "non-positive"
```

Guards are checked after the pattern matches. If the guard is false, execution falls through to the next arm:

```
match value
  0 -> "zero"                    -- exact match
  n ? n > 0 -> "positive"       -- variable bind + guard
  _ -> "negative"                -- fallback
```

Guards work with wildcards too:

```
flag = 1
result = match 42
  _ ? flag == 1 -> "flagged"
  _ -> "default"
```

### Match with Blocks

Arms can have block bodies:

```
match command
  "run" ->
    result = execute(0)
    println("done: " + int_to_str(result))
    result
  _ -> 0
```

### Boolean Dispatch Pattern

Since there's no `if/else`, use `match` on boolean expressions:

```
abs n =
  match n >= 0
    true -> n
    _ -> 0 - n
```

---

## 11. Closures and Lambdas

### Lambda Syntax

```
\x -> x + 1                -- single parameter
\x y -> x + y              -- multiple parameters
\_ -> 42                   -- ignore parameter
```

### Closures Capture Variables

Lambdas capture variables from their enclosing scope:

```
base = 100
add_base = \x -> base + x
add_base(42)    -- 142
```

Multiple captures:

```
a = 10
b = 20
f = \x -> a + b + x
f(3)    -- 33
```

### Nested Closures

```
x = 5
outer = \y -> \z -> x + y + z
inner = outer(10)
inner(20)    -- 35
```

### Passing Functions as Arguments

```
apply_fn f x = f(x)
result = apply_fn(\x -> x * 3, 7)    -- 21
```

### Field Accessor Shorthand

The `.field` syntax creates a lambda that extracts a field:

```
.name           -- equivalent to \x -> x.name
.age            -- equivalent to \x -> x.age
```

### Implementation Details

Closures are heap-allocated as `{fn_ptr, capture1, capture2, ...}`. Every function receives a hidden first parameter (the closure pointer). Non-closure functions ignore it. The calling convention is uniform — both regular functions and closures use the same ABI.

---

## 12. Strings

### String Literals

```
"hello, world"
"line 1\nline 2"
""                  -- empty string
```

### Escape Sequences

| Escape | Character |
|--------|-----------|
| `\n` | Newline |
| `\t` | Tab |
| `\r` | Carriage return |
| `\\` | Backslash |
| `\"` | Double quote |
| `\{` | Literal `{` (prevents interpolation) |
| `\0` | Null byte |
| `\xHH` | Hex byte |

### Raw Strings

No escape processing:

```
r"hello\nworld"     -- contains literal backslash-n, not a newline
r"C:\Users\file"    -- no escaping needed
```

### String Interpolation

Embed expressions inside strings with `{expr}`:

```
name = "Alice"
age = 30
println("Hello, {name}! Age: {int_to_str(age)}")
```

Interpolation compiles to efficient string builder calls (`sb_new` → `sb_append` × N → `sb_build`).

**Important:** Use `\{` for a literal `{` inside a string. Unescaped `{` always starts an interpolation.

### String Operators

```
"hello" + " world"      -- concatenation (compiles to str_concat)
"abc" == "abc"           -- equality (compiles to str_eq)
"abc" != "xyz"           -- inequality
```

### String Functions

```
str_len("hello")                -- 5
str_slice("hello", 0, 3)       -- "hel"
str_char_at("hello", 0)        -- 104 (ASCII 'h')
int_to_str(42)                  -- "42"
str_to_int("42")                -- 42
str_concat("a", "b")           -- "ab" (prefer + operator)
str_eq("a", "b")               -- 0 (prefer == operator)
```

### Extended String Functions

```
str_index_of("hello world", "world")  -- 6 (position of first match)
str_index_of("hello", "xyz")          -- -1 (not found)

str_starts_with("hello", "hel")       -- 1
str_ends_with("hello", "llo")         -- 1

str_trim("  hello  ")                 -- "hello"
str_trim("\n text \t")                -- "text"

str_to_upper("hello")                 -- "HELLO"

str_split("a,b,c", ",")              -- ["a", "b", "c"]
str_split("hello", "")               -- ["h", "e", "l", "l", "o"]
```

**Note:** `str_contains` and `str_to_lower` are not runtime builtins (they conflict with compiler-internal names). Use `str_index_of(s, needle) >= 0` for containment checks. For lowercase conversion, iterate bytes with `str_char_at` and `str_from_code`.

### Character Handling

Glyph has no character type. Characters are handled as integer byte codes:

```
code = str_char_at("A", 0)           -- 65
ch = str_from_code(65)                -- "A"
ch = str_from_code(10)                -- newline character

-- Character classification by byte range:
is_digit c = c >= 48 && c <= 57      -- '0'=48, '9'=57
is_upper c = c >= 65 && c <= 90      -- 'A'=65, 'Z'=90
is_lower c = c >= 97 && c <= 122     -- 'a'=97, 'z'=122
```

---

## 13. Arrays

### Array Literals

```
[1, 2, 3, 4]           -- array of integers
["a", "b", "c"]        -- array of strings
[]                      -- empty array
[1..10]                 -- range: [1, 2, 3, ..., 9]
```

### Array Operations

```
arr[0]                  -- index (0-based)
arr[i]                  -- computed index
array_len(arr)          -- length
array_push(arr, val)    -- push element (mutates, returns new data ptr)
array_set(arr, i, val)  -- set element at index (bounds-checked)
array_pop(arr)          -- remove and return last element
array_new(cap)          -- create empty array with initial capacity
```

### Extended Array Operations

```
-- Reverse array in place (mutates, returns same array)
arr = [1, 2, 3, 4]
array_reverse(arr)            -- arr is now [4, 3, 2, 1]

-- Extract subarray [start, end) — returns new array
sub = array_slice(arr, 1, 3)  -- [3, 2]

-- Find index of integer element (-1 if not found)
array_index_of(arr, 3)        -- 1
array_index_of(arr, 99)       -- -1
```

**Note:** `array_index_of` uses GVal (integer/pointer) equality. For string arrays, it compares pointers, not string content. Use a manual loop with `str_eq` for string search.

### Iterating Arrays

Glyph uses recursive functions for iteration (no `for` loop):

```
sum_arr arr i =
  match i >= array_len(arr)
    true -> 0
    _ -> arr[i] + sum_arr(arr, i + 1)

-- Usage:
total = sum_arr(nums, 0)
```

### Building Arrays

```
make_squares n i acc =
  match i >= n
    true -> acc
    _ ->
      array_push(acc, i * i)
      make_squares(n, i + 1, acc)

squares = make_squares(10, 0, [])
```

### Memory Layout

Arrays are heap-allocated with a 24-byte header: `{ptr: *i64, len: i64, cap: i64}`. The data is stored on the heap. `array_push` automatically grows the capacity when needed.

---

## 14. Records and Structs

### Anonymous Records

Records are unordered collections of named fields:

```
point = {x: 10, y: 20}
person = {name: "Alice", age: 30}
```

Fields are stored in **alphabetical order** internally (BTreeMap ordering).

### Field Access

```
point.x                 -- 10
person.name             -- "Alice"
```

### Record Updates (Functional)

Create a new record from an existing one with modified fields:

```
p = {x: 10, y: 20}
p2 = p{x: 30}             -- {x: 30, y: 20}
p3 = p{x: 5, y: 50}       -- {x: 5, y: 50}

-- Original is unchanged (functional update):
p.x                        -- still 10

-- Useful in recursive helpers:
move_right p = p{x: p.x + 1}
```

### Records as Return Values

```
make_point x y = {x: x, y: y}

p = make_point(10, 20)
println(int_to_str(p.x))   -- 10
```

### Named Record Types (Gen 2)

Define record types in the `def` table for better C codegen:

```bash
./glyph put app.glyph type -b 'Point = {x: I, y: I}'
./glyph put app.glyph fn -b 'make_point x y = {x: x, y: y}'
```

The compiler matches record aggregates against type definitions by their sorted field set. When matched, generated C uses `typedef struct` instead of offset-based indexing. See [Named Record Types](#22-named-record-types-gen-2-struct-codegen).

### Memory Layout

Each field occupies 8 bytes (one `GVal`). Fields are laid out alphabetically. A record `{x: I, y: I}` has:
- offset 0: field `x`
- offset 8: field `y`

Records are heap-allocated via `glyph_alloc`.

---

## 15. Enums and Algebraic Types

### Defining Enums

Two syntax forms are supported:

```bash
# Leading pipe (recommended for multi-line):
./glyph put app.glyph type -b 'Color = | Red | Green | Blue'

# No leading pipe (also valid):
./glyph put app.glyph type -b 'Color = Red | Green | Blue'
```

Both forms are equivalent. Examples with payloads:

```bash
./glyph put app.glyph type -b 'Shape = | Circle(I) | Rect(I, I)'
./glyph put app.glyph type -b 'Option = | None | Some(I)'
```

### Constructing Enum Values

```
c = Red                 -- nullary constructor
s = Circle(5)           -- single payload
r = Rect(3, 4)          -- multiple payloads
```

### Destructuring with Match

```
area s =
  match s
    Circle(r) -> r * r * 3
    Rect(w, h) -> w * h
```

### Built-in Enum Types

#### Option

```
-- Constructors: None, Some(val)
match find_item(key)
  None -> "not found"
  Some(x) -> "found: " + int_to_str(x)
```

#### Result

```
-- Constructors: Ok(val), Err(msg)
match safe_div(10, 0)
  Ok(v) -> println(int_to_str(v))
  Err(e) -> eprintln(e)
```

### Memory Layout

Enums are heap-allocated: `{tag: i64, payload1: i64, payload2: i64, ...}`. The tag is at offset 0, payloads start at offset 8.

---

## 16. Float Support

### Float Literals

```
3.14
2.0
0.5
-1.5
```

### Float Arithmetic

All standard arithmetic operators work on floats:

```
x = 3.14
y = 2.0
z = x + y       -- 5.14
w = x * y       -- 6.28
q = 10.0 / 3.0  -- 3.333...
```

### Implicit Int-to-Float Coercion

When one operand is float and the other is int, the int is automatically promoted:

```
x = 3.14 + 1       -- 4.14 (int 1 promoted to float)
y = 2 + 1.5         -- 3.5
z = 10.0 - 3        -- 7.0
w = 2 * 3.5          -- 7.0
```

### Float Comparisons

```
a = 1.5
b = 2.5
a < b       -- 1 (true)
a > b       -- 0 (false)
a == a      -- 1 (true)
a == b      -- 0 (false)
```

### Conversion Functions

```
int_to_float(42)        -- 42.0
float_to_int(3.14)      -- 3 (truncates)
float_to_str(3.14)      -- "3.14"
str_to_float("3.14")    -- 3.14
```

### Printing Floats

Use `float_to_str` for conversion before printing:

```
x = 3.14
println(float_to_str(x))   -- "3.14"
println("value: {float_to_str(x)}")
```

### Implementation

Floats are stored as `GVal` (64-bit integer) via `memcpy` bitcast. The C runtime provides `_glyph_i2f` and `_glyph_f2i` helper functions for GVal↔double conversion. Float operations are type-directed — the MIR lowering tracks local types to emit correct float instructions.

---

## 17. Error Handling

### Result Type

Results represent operations that can fail:

```
-- Construct
ok_val = Ok(42)
err_val = Err("something went wrong")
```

### Pattern Matching on Results

```
match try_read_file("data.txt")
  Ok(content) -> println(content)
  Err(msg) -> eprintln("Error: " + msg)
```

### Error Propagation (`?`)

The `?` operator propagates errors up the call stack:

```
process_file path =
  content = try_read_file(path)?    -- returns Err if read fails
  Ok(str_len(content))
```

If the expression is an `Err`, `?` immediately returns that `Err` from the current function. If it's `Ok`, it extracts the payload.

### Unwrap (`!`)

The `!` operator extracts the `Ok` payload or panics:

```
content = try_read_file("data.txt")!    -- panics if Err
```

### Result Functions

```
ok(val)                 -- wrap value in Ok
err(msg)                -- wrap message in Err
try_read_file(path)     -- read file, returns !S
try_write_file(p, s)    -- write file, returns !I
```

---

## 18. Foreign Function Interface

### Declaring Externs

Use the `extern` command to declare C functions:

```bash
./glyph extern app.glyph getenv getenv "I -> I" --lib c
```

This adds a row to the `extern_` table:

| Column | Description |
|--------|-------------|
| `name` | Glyph-side name |
| `symbol` | C symbol name |
| `sig` | Type signature (arrow-separated) |
| `lib` | Library to link (e.g., `sqlite3`, `c`) |
| `conv` | Calling convention (default: `C`) |

### Signature Format

Signatures use arrow-separated types (curried style):

```
I -> I          -- one int param, returns int
I -> S -> I     -- two params (int, string), returns int
[S]             -- no params, returns string array
```

### Using Externs in Code

Once declared, call the extern like any other function:

```
val = getenv(str_to_cstr("HOME"))
match val == 0
  true -> println("HOME not set")
  _ -> println(cstr_to_str(val))
```

### How It Works

The compiler generates C wrapper functions:

```c
GVal glyph_getenv(GVal p0) { return (GVal)(getenv)((char*)p0); }
```

Parentheses around the symbol suppress C macro expansion. The wrapper converts between `GVal` (the universal 64-bit type) and C types.

### Library Linking

Libraries specified in `--lib` are automatically passed to the linker:

```bash
./glyph extern app.glyph db_open glyph_db_open "S -> I" --lib sqlite3
# Linker will receive -lsqlite3
```

### FFI Without Externs (Advanced)

For heavy FFI, write C wrapper functions directly and concatenate them with the generated C:

```bash
# 1. Write C wrappers
cat > wrapper.c << 'EOF'
#include <ncurses.h>
long long nc_initscr(void) { return (long long)initscr(); }
long long nc_getch(void) { return getch(); }
EOF

# 2. Build and concatenate
./glyph build app.glyph || true
cat wrapper.c /tmp/glyph_out.c > combined.c
cc combined.c -o app -lncurses
```

Unknown function names in Glyph code pass through as-is in generated C, so calls like `nc_initscr()` work when the wrapper is linked.

### Runtime Functions Already Available

Functions prefixed `glyph_` in the extern table that match built-in runtime names are automatically provided — no wrapper generation needed. See [Runtime Function Reference](#20-runtime-function-reference).

---

## 19. Testing Framework

### Writing Tests

Test definitions have `kind='test'`:

```bash
./glyph put app.glyph test -b 'test_addition u =
  assert_eq(1 + 1, 2)
  assert_eq(3 + 4, 7)
  assert_eq(0 + 0, 0)'
```

Tests need a dummy parameter (`u`) because zero-arg functions with side effects are evaluated eagerly.

### Assertion Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `assert` | `I -> I` | Fail if 0 (false) |
| `assert_eq` | `I -> I -> I` | Fail if integers differ |
| `assert_str_eq` | `S -> S -> I` | Fail if strings differ |

All assertions return 0 and set a global `_test_failed` flag on failure, allowing the test suite to continue.

### Running Tests

```bash
# Run all tests
./glyph test app.glyph

# Run specific tests by name
./glyph test app.glyph test_addition test_subtraction
```

### Test Output

```
PASS test_addition
PASS test_subtraction
FAIL test_buggy
  assertion failed: expected 5, got 4
2/3 passed
```

Exit code: 0 if all pass, 1 if any fail.

### Architecture

All `kind='test'` definitions are compiled into a single test binary with a dispatch-table `main()`. Test definitions are excluded from regular `build` (which only compiles `kind='fn'`). The test binary includes all `fn` definitions plus a test runtime with assertion functions.

---

## 20. Runtime Function Reference

All functions are available in every compiled program. The C runtime is embedded at compile time. Runtime functions are prefixed `glyph_` in generated C (e.g., `println` → `glyph_println`).

### Core

| Function | Signature | Description |
|----------|-----------|-------------|
| `panic` | `S -> N` | Print to stderr, exit(1) |
| `exit` | `I -> V` | Exit with code |
| `alloc` | `U -> *V` | Heap allocate (panics on OOM) |
| `dealloc` | `*V -> V` | Free heap pointer |
| `realloc` | `*V -> I -> *V` | Resize allocation |

### Print

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `S -> I` | Print to stdout (no newline) |
| `println` | `S -> I` | Print with newline to stdout |
| `eprintln` | `S -> I` | Print with newline to stderr |

### String

| Function | Signature | Description |
|----------|-----------|-------------|
| `str_concat` | `S -> S -> S` | Concatenate (prefer `+` operator) |
| `str_eq` | `S -> S -> I` | Equality, 1=eq, 0=neq (prefer `==`) |
| `str_len` | `S -> I` | Length in bytes |
| `str_slice` | `S -> I -> I -> S` | Substring `[start..end)` |
| `str_char_at` | `S -> I -> I` | Byte at index (-1 if out of bounds) |
| `int_to_str` | `I -> S` | Integer to string |
| `str_to_int` | `S -> I` | Parse integer (0 on failure) |
| `str_to_cstr` | `S -> *V` | To null-terminated C string |
| `cstr_to_str` | `*V -> S` | From null-terminated C string |
| `str_index_of` | `S -> S -> I` | Position of first occurrence (-1 if not found) |
| `str_starts_with` | `S -> S -> B` | 1 if string starts with prefix |
| `str_ends_with` | `S -> S -> B` | 1 if string ends with suffix |
| `str_trim` | `S -> S` | Remove leading/trailing whitespace |
| `str_to_upper` | `S -> S` | ASCII uppercase conversion |
| `str_split` | `S -> S -> [S]` | Split by separator (empty sep splits chars) |
| `str_from_code` | `I -> S` | Single-character string from byte code |

### String Builder

O(n) string concatenation. String interpolation compiles to these automatically.

| Function | Signature | Description |
|----------|-----------|-------------|
| `sb_new` | `-> *V` | Create builder (zero-arg, call as `sb_new()`) |
| `sb_append` | `*V -> S -> *V` | Append string to builder |
| `sb_build` | `*V -> S` | Finalize, return assembled string |

### Float

| Function | Signature | Description |
|----------|-----------|-------------|
| `float_to_str` | `F -> S` | Float to string |
| `str_to_float` | `S -> F` | Parse float |
| `int_to_float` | `I -> F` | Integer to float |
| `float_to_int` | `F -> I` | Float to integer (truncates) |

### Math

| Function | Signature | Description |
|----------|-----------|-------------|
| `sqrt` | `F -> F` | Square root |
| `sin` | `F -> F` | Sine (radians) |
| `cos` | `F -> F` | Cosine (radians) |
| `atan2` | `F -> F -> F` | Two-argument arctangent |
| `fabs` | `F -> F` | Absolute value |
| `pow` | `F -> F -> F` | Exponentiation |
| `floor` | `F -> F` | Floor (round down) |
| `ceil` | `F -> F` | Ceiling (round up) |

### Array

| Function | Signature | Description |
|----------|-----------|-------------|
| `array_new` | `I -> *V` | Create empty array with initial capacity |
| `array_push` | `[I] -> I -> I` | Push element, returns new data ptr |
| `array_len` | `[I] -> I` | Element count |
| `array_set` | `[I] -> I -> I -> V` | Set element at index (bounds-checked) |
| `array_pop` | `[I] -> I` | Remove and return last element |
| `array_reverse` | `[T] -> [T]` | Reverse in place, returns array |
| `array_slice` | `[T] -> I -> I -> [T]` | Extract subarray `[start, end)` |
| `array_index_of` | `[I] -> I -> I` | Find integer element index (-1 if not found) |

### I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `read_file` | `S -> S` | Read entire file to string (no stdin/pipes) |
| `write_file` | `S -> S -> I` | Write string to file (0 = success) |
| `args` | `-> [S]` | Command-line arguments |
| `system` | `S -> I` | Execute shell command, return exit code |
| `read_line` | `I -> S` | Read line from stdin (takes dummy arg, strips newline, empty on EOF) |
| `flush` | `I -> V` | Flush stdout (takes dummy arg) |

### HashMap

String-keyed hash map with integer/pointer values.

| Function | Signature | Description |
|----------|-----------|-------------|
| `hm_new` | `-> {K:V}` | Create empty map (zero-arg, call as `hm_new()`) |
| `hm_set` | `{K:V} -> S -> I -> {K:V}` | Set key to value, returns map |
| `hm_get` | `{K:V} -> S -> I` | Get value by key (0 if missing) |
| `hm_del` | `{K:V} -> S -> {K:V}` | Delete key, returns map |
| `hm_has` | `{K:V} -> S -> B` | 1 if key exists |
| `hm_keys` | `{K:V} -> [S]` | All keys as array |
| `hm_len` | `{K:V} -> I` | Number of entries |

### Result

| Function | Signature | Description |
|----------|-----------|-------------|
| `ok` | `I -> !I` | Wrap value in Ok (heap-allocated) |
| `err` | `S -> !I` | Wrap error message in Err |
| `try_read_file` | `S -> !S` | Read file, Err on failure |
| `try_write_file` | `S -> S -> !I` | Write file, Err on failure |

### SQLite

Available when program uses `glyph_db_*` functions. Automatically links `-lsqlite3`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `glyph_db_open` | `S -> I` | Open database (0 = error) |
| `glyph_db_close` | `I -> V` | Close database handle |
| `glyph_db_exec` | `I -> S -> I` | Execute SQL, no result (0 = ok) |
| `glyph_db_query_rows` | `I -> S -> [[S]]` | Query → rows of string columns |
| `glyph_db_query_one` | `I -> S -> S` | Query → single string value |

### Test Assertions

Only available in test builds (`./glyph test`).

| Function | Signature | Description |
|----------|-----------|-------------|
| `assert` | `I -> I` | Fail if 0 |
| `assert_eq` | `I -> I -> I` | Fail if integers differ |
| `assert_str_eq` | `S -> S -> I` | Fail if strings differ |

### Low-Level

| Function | Signature | Description |
|----------|-----------|-------------|
| `raw_set` | `I -> I -> I -> I` | Write value at pointer+offset: `((long long*)ptr)[idx] = val` |

---

## 21. Generational Versioning (Gen 2)

The `def` table has a `gen` column (default 1) that enables generational overrides — multiple versions of the same definition, selected at build time.

### How It Works

- `--gen=N` flag selects the highest `gen <= N` per (name, kind) pair
- A gen=2 definition with the same name/kind as gen=1 **replaces** it when building with `--gen=2`
- Definitions without overrides at a given gen inherit from lower generations

### Creating Gen 2 Definitions

```bash
# Default gen=1 definition
./glyph put app.glyph fn -b 'helper x = x + 1'

# Gen=2 override with different implementation
./glyph put app.glyph fn -b 'helper x = x * 2' --gen 2
```

### Building with Generations

```bash
# Build using only gen=1 definitions
./glyph0 build app.glyph --full --gen=1

# Build with gen=2 overrides active
./glyph0 build app.glyph --full --gen=2
```

### Typical Usage

Generational versioning is primarily used for compiler evolution. The self-hosted compiler (glyph.glyph) has:
- 942 gen=1 definitions (original implementation)
- 77 gen=2 definitions (struct codegen, type system enhancements)

Application programs typically don't need generations — all definitions default to gen=1.

---

## 22. Named Record Types (Gen 2 Struct Codegen)

Gen=2 adds named record types that generate C `typedef struct` definitions.

### Defining Named Types

```bash
./glyph put app.glyph type -b 'Point = {x: I, y: I}'
./glyph put app.glyph type -b 'Stats = {count: I, max_val: I, min_val: I, sum: I}'
```

### How Matching Works

The compiler matches record aggregates in code against type definitions by their **sorted field set**:

```
make_point x y = {x: x, y: y}  -- matches Point (fields: {x, y})
```

The sorted field set `{x, y}` matches the `Point` type definition.

### Generated C

Without named types (gen=1):
```c
GVal* p = (GVal*)glyph_alloc(16);
((GVal*)p)[0] = x;  // offset 0 = x (alphabetical)
((GVal*)p)[1] = y;  // offset 8 = y
```

With named types (gen=2):
```c
typedef struct { GVal x; GVal y; } Glyph_Point;
Glyph_Point* p = (Glyph_Point*)glyph_alloc(sizeof(Glyph_Point));
p->x = x;
p->y = y;
```

### Field Access

```
p = make_point(10, 20)
p.x     -- gen=1: ((GVal*)p)[0], gen=2: ((Glyph_Point*)p)->x
p.y     -- gen=1: ((GVal*)p)[1], gen=2: ((Glyph_Point*)p)->y
```

### Anonymous Records

Records without a matching type definition fall back to offset-based access:

```
-- No matching type def: uses gen=1 offset-based access
temp = {a: 1, b: 2, c: 3}
temp.b   -- ((GVal*)temp)[1]
```

---

## 23. C-Layout Structs

For FFI interoperability, gen=2 supports C-compatible struct layouts using C type specifiers.

### Defining C-Layout Structs

Use lowercase C type specifiers instead of Glyph type aliases:

```bash
./glyph put app.glyph type -b 'Point2D = {x: i32, y: i32}'
./glyph put app.glyph type -b 'FPoint = {fx: f64, fy: f64}'
```

### Supported Type Specifiers

| Specifier | C Type | Size |
|-----------|--------|------|
| `i8` | `int8_t` | 1 byte |
| `u8` | `uint8_t` | 1 byte |
| `i16` | `int16_t` | 2 bytes |
| `u16` | `uint16_t` | 2 bytes |
| `i32` | `int32_t` | 4 bytes |
| `u32` | `uint32_t` | 4 bytes |
| `i64` | `int64_t` | 8 bytes |
| `u64` | `uint64_t` | 8 bytes |
| `f32` | `float` | 4 bytes |
| `f64` | `double` | 8 bytes |
| `ptr` | `void*` | 8 bytes |

### Generated C

```
Point2D = {x: i32, y: i32}
```

Generates:

```c
typedef struct { int32_t x; int32_t y; } Glyph_Point2D;
```

### Auto-Detection

Any field with a C type specifier triggers C-compatible layout for the entire struct. Standard `I`/`S`/etc. fields generate `GVal` as usual.

### Read/Write Behavior

- **Reads** use implicit widening (e.g., `int32_t` → `GVal`)
- **Writes** emit explicit narrowing casts (e.g., `(int32_t)val`)

### Float Fields

Float fields (`f32`, `f64`) use bitcast for GVal conversion:

```
p = {fx: 1.5, fy: 2.5}
sum = p.fx + p.fy           -- float addition
println(float_to_str(sum))  -- "4.0"
```

### Use Cases

C-layout structs are essential for:
- Passing structs to C libraries via FFI
- Matching exact binary layouts expected by system APIs
- Memory-efficient data storage (sub-word fields)

---

## 24. MCP Server

The self-hosted compiler includes a built-in MCP (Model Context Protocol) server for IDE integration.

### Starting the MCP Server

```bash
./glyph mcp app.glyph
```

This starts a JSON-RPC server on stdin/stdout using the MCP protocol.

### Available Tools

| Tool | Description |
|------|-------------|
| `get_def` | Read a definition's source body |
| `put_def` | Insert or update a definition (validates before inserting) |
| `list_defs` | List definitions with optional filters |
| `search_defs` | Search definition bodies for a pattern |
| `remove_def` | Remove a definition |
| `deps` | Get forward dependencies of a definition |
| `rdeps` | Get reverse dependencies |
| `sql` | Execute raw SQL |
| `check_def` | Validate a definition without inserting |
| `schema` | Get the database schema |

### Usage with Claude Code

The MCP server is designed for use as a Claude Code MCP tool, allowing Claude to read and modify Glyph programs through the standard MCP protocol.

---

## 25. Build Modes

### Default Mode

```bash
./glyph build app.glyph out
```

Optimization level O1, debug instrumentation included.

### Debug Mode

```bash
./glyph build app.glyph out --debug
```

- Optimization: O0
- Debug symbols: `-g`
- Full stack traces on crashes
- SIGSEGV/SIGFPE signal handlers
- Function tracking (`_glyph_current_fn`)

### Release Mode

```bash
./glyph build app.glyph out --release
```

- Optimization: O2
- No debug instrumentation
- Smaller binaries

### Safety Features (All Modes)

All build modes include:
- SIGSEGV and SIGFPE signal handlers
- Null pointer protection on string operations
- Array bounds checking
- Non-exhaustive match detection (trap)
- Function name tracking for crash reports

---

## 26. Compilation Pipeline

```
.glyph DB (SQLite)
  │
  ├─ SELECT fn defs ──→ Tokenizer ──→ Parser ──→ AST
  │                                                │
  │                                        Type Inference (HM)
  │                                                │
  ├─ Type map (per-expression types) ──────────────┤
  │                                                │
  │                                          MIR Lowering
  │                                                │
  │                                        ┌───────┴───────┐
  │                                        │ MIR (flat CFG) │
  │                                        └───────┬───────┘
  │                                                │
  │                                          C Code Generation
  │                                                │
  │                                        /tmp/glyph_out.c
  │                                                │
  │                                            cc (linker)
  │                                                │
  │                                         Native Executable
```

### Stages

1. **Read definitions**: `SELECT` from `def` table
2. **Tokenize**: Indentation-sensitive lexer emits INDENT/DEDENT/NEWLINE tokens
3. **Parse**: Recursive-descent parser produces AST per definition
4. **Type inference**: Hindley-Milner inference with let-polymorphism produces per-expression type map
5. **MIR lowering**: AST → flat CFG (basic blocks, statements, terminators). Type map guides string/float operation detection
6. **C codegen**: MIR → C source code with embedded runtime
7. **Link**: Invoke `cc` to produce native executable

### Incremental Compilation

Each definition is content-hashed (BLAKE3). Only dirty definitions and their transitive dependents (via the `dep` table) are recompiled.

---

## 27. Bootstrap Chain

The Glyph compiler bootstraps in 3 stages:

```
Stage 0: glyph0 (Rust compiler, cargo build --release)
    │
    │  Compiles glyph.glyph via Cranelift
    ▼
Stage 1: glyph (gen=2 binary, ~307k)
    │
    │  Self-hosted C codegen compiler
    ▼
Programs: Compiles user .glyph programs to C → native
```

### Building the Bootstrap Chain

```bash
ninja              # Full bootstrap: glyph0 → glyph
ninja bootstrap    # Same as default
ninja test         # Run all tests
```

Or manually:

```bash
cargo build --release                           # Build glyph0
cp target/release/glyph glyph0
./glyph0 build glyph.glyph --full --gen=2      # Build self-hosted compiler
```

---

## 28. Example Programs

### Calculator (Expression REPL)

A command-line expression calculator with `+`, `-`, `*`, `/`, `%`, and parentheses:

```
-- Recursive descent parser on char arrays
eval_line chars =
  r = parse_expr(chars, 0)
  r.val

parse_expr chars pos =
  t = parse_term(chars, pos)
  parse_expr_loop(chars, t.pos, t.val)
```

Build: `./glyph build examples/calculator/calc.glyph calc`

### Statistical Analyzer (Named Record Types)

Uses gen=2 named record types for structured data:

```
Stats = {count: I, max_val: I, min_val: I, sum: I}
Config = {filename: S, verbose: I}

compute_stats nums =
  len = array_len(nums)
  match len == 0
    true -> {count: 0, max_val: 0, min_val: 0, sum: 0}
    _ ->
      first = nums[0]
      init = {count: 1, max_val: first, min_val: first, sum: first}
      stats_loop(nums, 1, init)
```

Build: `./glyph build examples/gstats/gstats.glyph gstats`

### Project Analyzer (SQLite FFI)

Reads `.glyph` databases and reports statistics:

```
main =
  a = args()
  n = array_len(a)
  match n
    ...
  db = glyph_db_open(a[1])
  rows = glyph_db_query_rows(db, "SELECT name, kind, body FROM def ORDER BY name")
  ...
```

Build: `./glyph build examples/glint/glint.glyph glint`

### Terminal Text Editor (ncurses FFI)

A full text editor with ncurses:

```bash
# Build with C wrapper for ncurses
./glyph build examples/gled/gled.glyph || true
cat examples/gled/nc_wrapper.c /tmp/glyph_out.c > /tmp/combined.c
cc /tmp/combined.c -o gled -lncurses
```

Features: cursor movement, insert/delete, line splitting/joining, scroll, save (Ctrl-S), quit (Ctrl-Q).

### Conway's Game of Life (X11 GUI)

Graphical Life simulation using X11:

```bash
./glyph build examples/life/life.glyph || true
cat examples/life/x11_wrapper.c /tmp/glyph_out.c > /tmp/combined.c
cc /tmp/combined.c -o life -lX11
```

### Benchmark Suite

Performance comparison between Glyph and equivalent C:

```bash
cd examples/benchmark && ./build.sh
./benchmark         # Run Glyph version
./benchmark_c       # Run C version
```

---

## 29. Pitfalls and Limitations

### Language Pitfalls

| Pitfall | Cause | Fix |
|---------|-------|-----|
| `{` in strings triggers interpolation | String interpolation syntax | Use `\{` for literal `{` |
| Zero-arg fn runs eagerly | Treated as constant | Add dummy param: `usage u = ...` |
| C keyword as fn name | C codegen conflict | Avoid: `double`, `int`, `float`, `void`, `return`, `if`, `while`, `for`, `struct`, `enum`, `const` |
| `=` vs `:=` confusion | Different operations | `=` creates new binding, `:=` mutates existing |
| `True`/`False` vs `true`/`false` | Uppercase = enum constructors | Always use lowercase for boolean patterns |
| No `return` keyword | Block-based return | Last expression in block is the return value |
| No `if/else` | Match-only conditionals | Use `match condition` with `true -> ... _ -> ...` |

### Runtime Limitations

| Limitation | Details |
|------------|---------|
| No garbage collection | All heap allocations persist until exit. Short-lived programs only. |
| No stdin via `read_file` | Uses `fseek/ftell` (doesn't work on pipes). Use `-b` or `-f` flags. |
| String operators need type info | `+`/`==`/`!=` on two untyped params may use integer ops instead of string ops |
| No nested `{` in gen=1 strings | In Glyph strings compiled by Rust compiler, `{` triggers interpolation |

### Build System Pitfalls

| Pitfall | Fix |
|---------|-----|
| `deps`/`rdeps` show empty | Run `./glyph build` first to populate dep table |
| `tokens=0` from self-hosted | Run `cargo run -- build --full` to compute token counts |
| Gen=2 only in self-hosted | Rust compiler ignores gen column. Use `./glyph0 build --gen=2` |
| Extern header limitation | Extern wrappers only see stdlib headers. Use separate C wrapper file for other headers. |
| `sb_new()` is zero-arg | Call as `sb_new()` not `sb_new(0)`. Runtime declares `GVal glyph_sb_new(void)`. |

### Field Offset Ambiguity

When multiple record types share field names, the compiler may pick the wrong type for field access. Mitigated by:
- Accessing a type-unique field on the same variable
- The compiler preferring the largest matching type (most fields)

---

## 30. Appendix: Operator Precedence

| Level | Operators | Associativity | Notes |
|-------|-----------|---------------|-------|
| 1 | `\|>` | Left | Pipe |
| 2 | `>>` | Left | Compose |
| 3 | `\|\|` | Left | Logical OR (short-circuit) |
| 4 | `&&` | Left | Logical AND (short-circuit) |
| 5 | `== != < > <= >=` | None | Comparison |
| 6 | `+ -` | Left | Additive |
| 7 | `* / %` | Left | Multiplicative |
| 8 | `- ! & *` | Right | Prefix unary |
| 9 | `. () [] ? !` | Left | Postfix |

Bitwise operators (`bitand`, `bitor`, `bitxor`, `shl`, `shr`) are parsed as keyword-style infix operators.

---

## 31. Appendix: Memory Layout

### Value Representation

All values at ABI boundary are `GVal` = `typedef intptr_t GVal;` (64-bit integer).

### Type Layouts

| Type | Layout | Size | Heap? |
|------|--------|------|-------|
| Integer | Raw `i64` | 8 bytes | No |
| Float | Bitcast `double` ↔ `i64` via memcpy | 8 bytes | No |
| Bool | `i8` internally, `i64` at ABI | 1/8 bytes | No |
| String | `{ptr: *char, len: i64}` | 16 bytes | Yes |
| Array | `{ptr: *i64, len: i64, cap: i64}` | 24 bytes | Yes |
| Record (named) | `typedef struct { GVal f1; ... } Glyph_Name;` | 8 bytes/field | Yes |
| Record (anonymous) | Fields at offsets, alphabetical | 8 bytes/field | Yes |
| Record (C-layout) | `typedef struct { type1 f1; ... } Glyph_Name;` | varies | Yes |
| Enum | `{tag: i64, payload...}` | 8 + 8/field | Yes |
| Closure | `{fn_ptr, captures...}` | 8 + 8/capture | Yes |
| Void | `0` | 0 bytes | No |

### C ABI Mapping

| Glyph | C Type | Notes |
|-------|--------|-------|
| `I` | `long long` | |
| `U` | `unsigned long long` | |
| `F` | `double` | Bitcast through GVal |
| `B` | `long long` | 0/1 at ABI boundary |
| `S` | `void*` | Pointer to `{char*, long long}` |
| `[T]` | `void*` | Pointer to `{long long*, long long, long long}` |
| `*T` | `void*` | |
| `V` | `long long 0` | Not C `void` — returns 0 |

---

## 32. Appendix: Schema Reference

### Full Schema

```sql
PRAGMA journal_mode = WAL;

CREATE TABLE def (
  id        INTEGER PRIMARY KEY,
  name      TEXT NOT NULL,
  kind      TEXT NOT NULL,
  sig       TEXT DEFAULT '',
  body      TEXT NOT NULL,
  hash      BLOB NOT NULL,
  tokens    INTEGER DEFAULT 0,
  compiled  INTEGER DEFAULT 0,
  gen       INTEGER DEFAULT 1,
  created   TEXT DEFAULT (datetime('now')),
  modified  TEXT DEFAULT (datetime('now'))
);

CREATE TABLE dep (
  from_id   INTEGER REFERENCES def(id),
  to_id     INTEGER REFERENCES def(id),
  edge      TEXT NOT NULL,
  PRIMARY KEY (from_id, to_id, edge)
);

CREATE TABLE extern_ (
  id        INTEGER PRIMARY KEY,
  name      TEXT NOT NULL UNIQUE,
  symbol    TEXT NOT NULL,
  lib       TEXT DEFAULT '',
  sig       TEXT DEFAULT '',
  conv      TEXT DEFAULT 'C'
);

CREATE TABLE tag (
  def_id    INTEGER REFERENCES def(id),
  key       TEXT NOT NULL,
  val       TEXT DEFAULT '',
  PRIMARY KEY (def_id, key)
);

CREATE TABLE module (
  id        INTEGER PRIMARY KEY,
  name      TEXT NOT NULL UNIQUE,
  doc       TEXT DEFAULT ''
);

CREATE TABLE module_member (
  module_id INTEGER REFERENCES module(id),
  def_id    INTEGER REFERENCES def(id),
  exported  INTEGER DEFAULT 1,
  PRIMARY KEY (module_id, def_id)
);

CREATE TABLE compiled (
  def_id    INTEGER PRIMARY KEY REFERENCES def(id),
  ir        BLOB,
  target    TEXT DEFAULT 'native',
  hash      BLOB
);

CREATE TABLE def_history (
  id        INTEGER PRIMARY KEY,
  def_id    INTEGER,
  name      TEXT,
  kind      TEXT,
  sig       TEXT,
  body      TEXT,
  hash      BLOB,
  tokens    INTEGER,
  gen       INTEGER,
  changed_at TEXT DEFAULT (datetime('now'))
);

CREATE TABLE meta (
  key       TEXT PRIMARY KEY,
  value     TEXT
);
```

### Edge Types in `dep`

| Edge | Meaning |
|------|---------|
| `calls` | Function A calls function B |
| `uses_type` | Function uses a type definition |
| `implements` | Impl block implements a trait |
| `field_of` | Field belongs to a record type |
| `variant_of` | Variant belongs to an enum type |

### Kind Values

`fn`, `type`, `test`, `trait`, `impl`, `const`, `fsm`, `srv`, `macro`

### Views

```sql
-- Dirty definitions + transitive dependents
CREATE VIEW v_dirty AS ...;

-- Definitions sorted by dependency depth
CREATE VIEW v_context AS ...;

-- Caller/callee/edge triples
CREATE VIEW v_callgraph AS ...;
```
