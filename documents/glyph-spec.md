# Glyph Language Specification v0.1

**An LLM-native programming language where programs are databases.**

---

## 1. Overview

Glyph is a programming language designed for generation and consumption by large language
models. Its two foundational design decisions are:

1. **Token-minimal syntax** — every construct is chosen to minimize BPE token count
   while preserving unambiguous semantics.
2. **Programs are SQLite3 databases** — there are no source files. The unit of storage
   is the *definition*, and the module system is replaced by SQL queries.

A Glyph program is a `.glyph` file, which is a SQLite3 database. The compiler, `glyphc`,
reads and writes this database directly. An LLM interacts with a Glyph program by issuing
SQL to read context and write definitions.

### 1.1 Design Goals

| Priority | Goal                                           |
|----------|-------------------------------------------------|
| P0       | Minimize token count for LLM generation         |
| P0       | Unambiguous — no construct has context-free ambiguity |
| P0       | SQLite-native storage with queryable structure   |
| P1       | FFI to C ABI for bootstrapping and system access |
| P1       | Incremental compilation via content hashing      |
| P2       | Self-hosting (bootstrappable)                    |
| P2       | Formal categorical semantics                     |

### 1.2 Non-Goals (for v0.1)

- Human-optimized readability (humans can use tooling to pretty-print)
- IDE integration (the DB *is* the IDE backend)
- Garbage collection (region-based or Rust-style ownership)

---

## 2. Program Model: The Database Schema

A Glyph program is a SQLite3 database with the following schema. This is the
**canonical** representation — there is no other source format.

### 2.1 Core Schema

```sql
-- Glyph program schema v0.1
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

---------------------------------------------------------------------
-- DEFINITIONS: the atoms of a Glyph program
---------------------------------------------------------------------
CREATE TABLE def (
  id        INTEGER PRIMARY KEY,
  name      TEXT    NOT NULL,
  kind      TEXT    NOT NULL CHECK(kind IN (
              'fn','type','trait','impl','const','fsm','srv','macro','test'
            )),
  sig       TEXT,               -- type signature (NULL = inferred)
  body      TEXT    NOT NULL,    -- Glyph source for this definition
  hash      BLOB    NOT NULL,    -- BLAKE3 hash of (kind || sig || body)
  tokens    INTEGER NOT NULL,    -- pre-computed token count (cl200k)
  compiled  INTEGER NOT NULL DEFAULT 0,  -- 0=dirty, 1=compiled
  created   TEXT    NOT NULL DEFAULT (datetime('now')),
  modified  TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE UNIQUE INDEX idx_def_name_kind ON def(name, kind);
CREATE INDEX idx_def_kind ON def(kind);
CREATE INDEX idx_def_compiled ON def(compiled) WHERE compiled = 0;

---------------------------------------------------------------------
-- DEPENDENCIES: edges in the definition graph
---------------------------------------------------------------------
CREATE TABLE dep (
  from_id   INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  to_id     INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  edge      TEXT    NOT NULL CHECK(edge IN (
              'calls','uses_type','implements','field_of','variant_of'
            )),
  PRIMARY KEY (from_id, to_id, edge)
);

CREATE INDEX idx_dep_to ON dep(to_id);  -- "who depends on me?"

---------------------------------------------------------------------
-- EXTERN: foreign function declarations (C ABI)
---------------------------------------------------------------------
CREATE TABLE extern (
  id        INTEGER PRIMARY KEY,
  name      TEXT    NOT NULL,    -- Glyph-side name
  symbol    TEXT    NOT NULL,    -- C symbol name
  lib       TEXT,                -- library (NULL = libc)
  sig       TEXT    NOT NULL,    -- Glyph type signature
  conv      TEXT    NOT NULL DEFAULT 'C' CHECK(conv IN ('C','system','rust')),
  UNIQUE(name)
);

---------------------------------------------------------------------
-- TAGS: arbitrary metadata on definitions
---------------------------------------------------------------------
CREATE TABLE tag (
  def_id    INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  key       TEXT    NOT NULL,
  val       TEXT,
  PRIMARY KEY (def_id, key)
);

CREATE INDEX idx_tag_key_val ON tag(key, val);

---------------------------------------------------------------------
-- MODULES: logical grouping (optional, for organization only)
---------------------------------------------------------------------
CREATE TABLE module (
  id        INTEGER PRIMARY KEY,
  name      TEXT    NOT NULL UNIQUE,
  doc       TEXT
);

CREATE TABLE module_member (
  module_id INTEGER NOT NULL REFERENCES module(id) ON DELETE CASCADE,
  def_id    INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  exported  INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY (module_id, def_id)
);

---------------------------------------------------------------------
-- COMPILATION CACHE
---------------------------------------------------------------------
CREATE TABLE compiled (
  def_id    INTEGER PRIMARY KEY REFERENCES def(id) ON DELETE CASCADE,
  ir        BLOB    NOT NULL,    -- serialized MIR/LIR
  target    TEXT    NOT NULL,    -- e.g. "x86_64-linux", "wasm32"
  hash      BLOB    NOT NULL     -- hash at compilation time
);

---------------------------------------------------------------------
-- VIEWS: convenience queries
---------------------------------------------------------------------

-- All dirty definitions and their transitive dependents
CREATE VIEW v_dirty AS
  WITH RECURSIVE dirty(id) AS (
    SELECT id FROM def WHERE compiled = 0
    UNION
    SELECT d.from_id FROM dep d JOIN dirty ON d.to_id = dirty.id
  )
  SELECT DISTINCT def.* FROM def JOIN dirty ON def.id = dirty.id;

-- Token budget: definitions sorted by dependency depth
CREATE VIEW v_context AS
  SELECT d.*, COUNT(dep.to_id) as dep_count
  FROM def d
  LEFT JOIN dep ON d.id = dep.from_id
  GROUP BY d.id
  ORDER BY dep_count ASC, d.tokens ASC;

-- Full call graph
CREATE VIEW v_callgraph AS
  SELECT
    f.name  AS caller,
    t.name  AS callee,
    d.edge
  FROM dep d
  JOIN def f ON d.from_id = f.id
  JOIN def t ON d.to_id   = t.id;
```

### 2.2 Schema Invariants

- Every definition has a BLAKE3 hash of `kind || sig || body`. When the hash changes,
  `compiled` is set to 0 (via trigger).
- The `dep` table is populated by the compiler during analysis, not by the user/LLM.
- `tokens` is computed on INSERT/UPDATE using the target LLM's tokenizer.

### 2.3 Triggers

```sql
-- Auto-dirty on content change
CREATE TRIGGER trg_def_dirty AFTER UPDATE OF body, sig, kind ON def
BEGIN
  UPDATE def SET
    compiled = 0,
    hash = glyph_hash(NEW.kind, NEW.sig, NEW.body),
    tokens = glyph_tokens(NEW.body),
    modified = datetime('now')
  WHERE id = NEW.id;
END;

-- Cascade dirty to dependents
CREATE TRIGGER trg_dep_dirty AFTER UPDATE OF compiled ON def
  WHEN NEW.compiled = 0
BEGIN
  UPDATE def SET compiled = 0
  WHERE id IN (SELECT from_id FROM dep WHERE to_id = NEW.id);
END;
```

(`glyph_hash` and `glyph_tokens` are application-defined SQLite functions
registered by `glyphc`.)

---

## 3. Syntax

### 3.1 Lexical Grammar

```
WHITESPACE  = [ \t]+
NEWLINE     = '\n'
INDENT      = increase in leading whitespace (significant)
DEDENT      = decrease in leading whitespace (significant)
COMMENT     = '--' [^\n]*

IDENT       = [a-z_][a-zA-Z0-9_]*
TYPE_IDENT  = [A-Z][a-zA-Z0-9_]*
OP          = [+\-*/%<>=!&|^~@#]+
INT_LIT     = [0-9][0-9_]*
FLOAT_LIT   = [0-9][0-9_]* '.' [0-9][0-9_]*
STR_LIT     = '"' ( [^"\\] | '\\' . | '{' expr '}' )* '"'
BYTE_LIT    = 'b"' ( [^"\\] | '\\' . )* '"'
```

### 3.2 Primitive Types

Single-character type names minimize tokens. Longer aliases exist for clarity.

| Short | Long      | Description                 | Size     |
|-------|-----------|-----------------------------|----------|
| `I`   | `Int`     | 64-bit signed integer       | 8 bytes  |
| `I32` | `Int32`   | 32-bit signed integer       | 4 bytes  |
| `U`   | `UInt`    | 64-bit unsigned integer     | 8 bytes  |
| `F`   | `Float`   | 64-bit IEEE float           | 8 bytes  |
| `F32` | `Float32` | 32-bit IEEE float           | 4 bytes  |
| `S`   | `Str`     | UTF-8 string (owned)        | variable |
| `B`   | `Bool`    | Boolean                     | 1 byte   |
| `V`   | `Void`    | Unit / no value             | 0 bytes  |
| `N`   | `Never`   | Bottom type (diverges)      | 0 bytes  |
| `&T`  | `Ref T`   | Borrowed reference          | ptr      |
| `*T`  | `Ptr T`   | Raw pointer (FFI)           | ptr      |
| `?T`  | `Opt T`   | Optional (None \| Some T)   | varies   |
| `!T`  | `Res T`   | Result (Err E \| Ok T)      | varies   |
| `[T]` | `Arr T`   | Dynamic array               | varies   |
| `{K:V}`|`Map K V` | Hash map                    | varies   |

### 3.3 Definition Grammar (EBNF)

```ebnf
program     = { definition } ;

definition  = fn_def | type_def | trait_def | impl_def
            | const_def | fsm_def | srv_def | extern_def
            | test_def ;

(* --- Functions --- *)
fn_def      = IDENT { param } [ ':' type ] '=' body ;
param       = IDENT [ ':' type ] ;
body        = expr | INDENT { stmt } DEDENT ;
stmt        = expr | let_bind | assign ;
let_bind    = IDENT '=' expr ;
assign      = lvalue ':=' expr ;

(* --- Types --- *)
type_def    = TYPE_IDENT [ type_params ] '=' type_body ;
type_params = '[' IDENT { ',' IDENT } ']' ;
type_body   = record | enum_body | alias ;
record      = '{' { field } '}' ;
field       = IDENT ':' type [ '=' expr ] ;   (* default value *)
enum_body   = '|' variant { '|' variant } ;
variant     = TYPE_IDENT [ record | '(' type { ',' type } ')' ] ;

(* --- Traits --- *)
trait_def   = 'trait' TYPE_IDENT [ type_params ]
              INDENT { sig_decl } DEDENT ;
sig_decl    = IDENT ':' type ;

(* --- Implementations --- *)
impl_def    = 'impl' TYPE_IDENT 'for' TYPE_IDENT
              INDENT { fn_def } DEDENT ;

(* --- Constants --- *)
const_def   = 'const' IDENT [ ':' type ] '=' expr ;

(* --- Extern (FFI) --- *)
extern_def  = 'extern' [ STR_LIT ] INDENT { extern_fn } DEDENT ;
extern_fn   = IDENT ':' fn_type [ 'as' STR_LIT ] ;

(* --- State machines --- *)
fsm_def     = 'fsm' TYPE_IDENT
              INDENT { state_def } DEDENT ;
state_def   = TYPE_IDENT
              INDENT { transition } DEDENT ;
transition  = IDENT [ '(' { param } ')' ] '->' TYPE_IDENT [ '/' expr ] ;

(* --- Server routes --- *)
srv_def     = 'srv' expr
              INDENT { route } DEDENT ;
route       = STR_LIT
              INDENT { handler } DEDENT ;
handler     = HTTP_METHOD { param } '->' body ;
HTTP_METHOD = 'G' | 'P' | 'U' | 'X' | 'H' ;   (* GET POST PUT DELETE HEAD *)

(* --- Tests --- *)
test_def    = 'test' STR_LIT '=' body ;

(* --- Expressions --- *)
expr        = pipe_expr ;
pipe_expr   = comp_expr { '|>' comp_expr } ;
comp_expr   = or_expr { '>>' or_expr } ;
or_expr     = and_expr { '||' and_expr } ;
and_expr    = cmp_expr { '&&' cmp_expr } ;
cmp_expr    = add_expr { cmp_op add_expr } ;
cmp_op      = '==' | '!=' | '<' | '>' | '<=' | '>=' ;
add_expr    = mul_expr { ('+' | '-') mul_expr } ;
mul_expr    = unary_expr { ('*' | '/' | '%') unary_expr } ;
unary_expr  = [ '-' | '!' | '&' | '*' ] postfix_expr ;
postfix_expr= atom { '.' IDENT | '(' args ')' | '[' expr ']' | '?' | '!' } ;
args        = [ expr { ',' expr } ] ;

atom        = INT_LIT | FLOAT_LIT | STR_LIT | BYTE_LIT
            | IDENT | TYPE_IDENT
            | '(' expr ')'
            | '[' [ expr { ',' expr } ] ']'          (* array literal *)
            | '{' [ field_init { ',' field_init } ] '}'  (* record literal *)
            | lambda
            | if_expr | match_expr | for_expr ;

lambda      = '\' { param } '->' expr ;
field_init  = IDENT ':' expr ;
if_expr     = 'if' expr ':' expr [ 'else' ':' expr ] ;
match_expr  = 'match' expr INDENT { pattern '->' expr } DEDENT ;
for_expr    = 'for' pattern 'in' expr [ 'if' expr ] ':' expr ;
pattern     = '_' | IDENT | INT_LIT | STR_LIT
            | TYPE_IDENT [ '(' { pattern } ')' ]
            | '{' { IDENT [ ':' pattern ] } '}' ;
```

### 3.4 Operator Semantics

| Operator | Meaning                          | Token cost |
|----------|----------------------------------|------------|
| `\|>`    | Pipeline: `x \|> f` = `f(x)`    | 2          |
| `>>`     | Compose: `f >> g` = `\x -> g(f(x))` | 1      |
| `?`      | Propagate error / None           | 1          |
| `!`      | Unwrap or panic                  | 1          |
| `.`      | Field access / method call       | 1          |
| `:=`     | Mutating assignment              | 1          |
| `=`      | Binding (immutable by default)   | 1          |

### 3.5 Points of Interest

**No `return` keyword.** The last expression in a body is the return value.
This saves 1 token per function.

**No `import` statement.** All definitions in the database are in scope. Name
collisions are resolved by module qualifier: `mod.name`. The compiler resolves
references by querying the `def` table.

**No `let` keyword.** Bindings use bare `name = expr`. Inside a body block,
this is unambiguous because top-level definitions cannot appear inside indented
blocks.

**No semicolons or braces for blocks.** Indentation-sensitive, like Python/Haskell.
Saves 1-2 tokens per line.

**String interpolation is `{expr}` not `${expr}`.** Saves 1 token per interpolation.

---

## 4. Type System

### 4.1 Overview

- **Structural typing** for records: `{name:S age:I}` is a type, not just a value.
- **Nominal typing** for enums and newtypes.
- **Full Hindley-Milner inference** with bidirectional checking at extern boundaries.
- **Row polymorphism** for extensible records: `fn greet r:{name:S ..} = "Hi {r.name}"`.

### 4.2 Type Syntax

```ebnf
type        = prim_type | named_type | fn_type | tuple_type
            | record_type | enum_ref | type_app ;
prim_type   = 'I' | 'I32' | 'U' | 'F' | 'F32' | 'S' | 'B' | 'V' | 'N' ;
named_type  = TYPE_IDENT ;
fn_type     = type '->' type ;        (* right-associative *)
tuple_type  = '(' type ',' type { ',' type } ')' ;
record_type = '{' { IDENT ':' type } [ '..' ] '}' ;
type_app    = TYPE_IDENT '[' type { ',' type } ']' ;
ref_type    = '&' type ;
ptr_type    = '*' type ;
opt_type    = '?' type ;
res_type    = '!' type ;
arr_type    = '[' type ']' ;
map_type    = '{' type ':' type '}' ;
```

### 4.3 Traits

Minimal trait system for polymorphism and FFI abstraction:

```
trait Num[T]
  add : T -> T -> T
  mul : T -> T -> T
  zero : T
  one  : T

impl Num for I
  add a b = a + b
  mul a b = a * b
  zero = 0
  one  = 1
```

### 4.4 Inference Rules

1. Literal `42` infers to `I` (default), `42:I32` for explicit.
2. Literal `3.14` infers to `F`.
3. Literal `"x"` infers to `S`.
4. `?` on expression of type `T` wraps context in `?T` return.
5. `!` on `?T` yields `T` (panic on None); on `!T` yields `T` (panic on Err).
6. Record literals infer structurally.
7. Lambda params infer from usage context.

---

## 5. FFI (Foreign Function Interface)

### 5.1 Extern Blocks

```
extern "libc"
  malloc : U -> *V as "malloc"
  free   : *V -> V as "free"
  write  : I -> *V -> U -> I as "write"

extern "libsqlite3"
  db_open : S -> *V -> I as "sqlite3_open"
```

### 5.2 Type Mapping

| Glyph  | C equivalent       |
|--------|--------------------|
| `I`    | `int64_t`          |
| `I32`  | `int32_t`          |
| `U`    | `uint64_t`         |
| `F`    | `double`           |
| `F32`  | `float`            |
| `B`    | `_Bool`            |
| `*T`   | `T*`               |
| `*V`   | `void*`            |
| `S`    | `const char*` (NUL-terminated on FFI boundary) |
| `V`    | `void`             |

### 5.3 Calling Conventions

- `C` (default): standard C ABI
- `system`: platform-specific (stdcall on Windows, C elsewhere)
- `rust`: Rust ABI (for bootstrapping, unstable)

### 5.4 FFI Safety

All extern calls are implicitly `unsafe`. A function that calls an extern is
tainted and must be called within an `unsafe` expression or annotated with
a `@unsafe` tag:

```
@unsafe
alloc n:U =
  p = malloc(n)
  if p == null: panic "OOM"
  p
```

---

## 6. LLM Interface Protocol

This is the key innovation: how an LLM reads and writes Glyph programs.

### 6.1 Reading Context (Token-Budgeted)

An LLM building or modifying a Glyph program queries for context:

```sql
-- "I need to understand function `process`"
SELECT body, sig FROM def WHERE name = 'process' AND kind = 'fn';

-- "What functions does `process` call?"
SELECT d.name, d.sig, d.body
FROM def d
JOIN dep ON d.id = dep.to_id
WHERE dep.from_id = (SELECT id FROM def WHERE name = 'process')
  AND dep.edge = 'calls';

-- "Give me everything within a 2000-token budget, prioritized by dependency"
WITH RECURSIVE ctx(id, depth) AS (
  SELECT id, 0 FROM def WHERE name = 'main'
  UNION ALL
  SELECT dep.to_id, ctx.depth + 1
  FROM dep JOIN ctx ON dep.from_id = ctx.id
  WHERE ctx.depth < 5
)
SELECT d.name, d.kind, d.sig, d.body
FROM def d JOIN ctx ON d.id = ctx.id
ORDER BY ctx.depth ASC
LIMIT (
  SELECT MAX(rownum) FROM (
    SELECT ROW_NUMBER() OVER (ORDER BY ctx.depth) as rownum,
           SUM(d.tokens) OVER (ORDER BY ctx.depth ROWS UNBOUNDED PRECEDING) as cum_tokens
    FROM def d JOIN ctx ON d.id = ctx.id
  ) WHERE cum_tokens <= 2000
);

-- "What types exist?"
SELECT name, body FROM def WHERE kind = 'type';

-- "What's the full API surface?"  (exported signatures only)
SELECT d.name, d.sig FROM def d
JOIN module_member mm ON d.id = mm.def_id
WHERE mm.exported = 1
ORDER BY d.name;

-- "Find anything related to 'parse'"
SELECT name, kind, sig, body FROM def
WHERE name LIKE '%parse%' OR body LIKE '%parse%'
ORDER BY tokens ASC;
```

### 6.2 Writing Definitions

An LLM writes code by inserting/updating rows:

```sql
-- Create a new function
INSERT INTO def (name, kind, body, hash, tokens)
VALUES (
  'process',
  'fn',
  'process items = items |> filter .valid |> map transform |> collect',
  glyph_hash('fn', NULL, 'process items = ...'),
  12
);

-- Modify an existing function
UPDATE def SET body = 'process items =
  items
    |> filter .valid
    |> par map transform
    |> collect'
WHERE name = 'process' AND kind = 'fn';

-- Add a type
INSERT INTO def (name, kind, body, hash, tokens)
VALUES (
  'Item',
  'type',
  'Item = {id:U name:S valid:B data:[U]}',
  glyph_hash('type', NULL, '...'),
  14
);

-- Declare an extern
INSERT INTO extern (name, symbol, lib, sig)
VALUES ('clock', 'clock_gettime', 'librt', 'I -> *V -> I');
```

### 6.3 Compilation Commands

After writing, the LLM (or toolchain) triggers compilation:

```sql
-- "What needs recompiling?"
SELECT id, name, kind FROM v_dirty;

-- Compilation is invoked via glyphc, not SQL:
-- $ glyphc build program.glyph          # compile dirty defs
-- $ glyphc build program.glyph --full   # recompile everything
-- $ glyphc run program.glyph            # build + execute main
-- $ glyphc check program.glyph          # type-check only
-- $ glyphc test program.glyph           # run all test defs
```

### 6.4 LLM Workflow Example

A complete interaction for "add a CSV parser":

```
LLM:  SELECT name, sig FROM def WHERE kind = 'type';
DB:   → Record = {name:S ..}, Row = [S], ...

LLM:  SELECT name, sig FROM extern;
DB:   → read_file: S -> !S, ...

LLM:  INSERT INTO def (name, kind, body, hash, tokens) VALUES
      ('parse_csv', 'fn',
       'parse_csv path:S =
          read_file(path)?
            |> split "\n"
            |> filter .len > 0
            |> map (split ",")
            |> \rows -> {header: rows[0] data: rows[1:]}',
       ..., 28);

LLM:  INSERT INTO def (name, kind, body, hash, tokens) VALUES
      ('CsvDoc', 'type',
       'CsvDoc = {header:[S] data:[[S]]}',
       ..., 8);

LLM:  $ glyphc check program.glyph
      → OK, parse_csv : S -> !CsvDoc (inferred)
```

---

## 7. Compilation Pipeline

```
┌─────────────┐
│  .glyph DB  │  SQLite3 database
└──────┬──────┘
       │ SELECT dirty defs
       ▼
┌─────────────┐
│   Parser    │  Per-definition, not per-file
└──────┬──────┘
       │ AST nodes
       ▼
┌─────────────┐
│  Resolver   │  Queries DB for referenced names
└──────┬──────┘
       │ Resolved AST
       ▼
┌─────────────┐
│ Type Infer  │  Hindley-Milner + row polymorphism
└──────┬──────┘
       │ Typed AST
       ▼
┌─────────────┐
│    MIR      │  Mid-level IR (desugared, monomorphized)
└──────┬──────┘
       │
       ├──────────────────┐
       ▼                  ▼
┌─────────────┐   ┌─────────────┐
│  Cranelift  │   │    LLVM     │  (optional, for release builds)
│  (default)  │   │  (opt-in)   │
└──────┬──────┘   └──────┬──────┘
       │                  │
       ▼                  ▼
   native obj         native obj
       │                  │
       └────────┬─────────┘
                ▼
         ┌────────────┐
         │   Linker   │  links with extern libs
         └─────┬──────┘
               ▼
          executable

Compilation results cached back to DB:
  INSERT INTO compiled (def_id, ir, target, hash) ...
```

### 7.1 Incremental Compilation

1. Query `v_dirty` for definitions whose hash changed or whose dependencies changed.
2. Parse and type-check only those definitions.
3. Re-lower to MIR, re-codegen.
4. Link incrementally (where the linker supports it).
5. Update `compiled` table and set `def.compiled = 1`.

Because each definition is independent and dependencies are explicit in the
`dep` table, incremental compilation is trivially correct.

### 7.2 Bootstrap Path

Phase 1 (current): `glyphc` is written in Rust, using `rusqlite` for DB
access and `cranelift` for codegen.

Phase 2: Core library functions are rewritten in Glyph, calling Rust via
FFI where needed.

Phase 3: `glyphc` is rewritten in Glyph. The parser, type checker, and
codegen become Glyph definitions in a compiler database. The Rust
implementation becomes the bootstrap compiler.

---

## 8. Standard Library

The standard library is a set of pre-populated definitions in a
`stdlib.glyph` database that is attached at compile time:

```sql
ATTACH 'stdlib.glyph' AS std;
```

### 8.1 Core Functions

```
-- I/O
read  : S -> !S                    -- read file to string
write : S -> S -> !V               -- write string to file
say   : S -> V                     -- print to stdout
ask   : S -> !S                    -- prompt + read line

-- Collections
map    : (a -> b) -> [a] -> [b]
filter : (a -> B) -> [a] -> [a]
fold   : (b -> a -> b) -> b -> [a] -> b
sort   : [a] -> [a]               -- requires Ord
sort   : (a -> k) -> [a] -> [a]   -- sort by key extractor (overload)
group  : (a -> k) -> [a] -> {k:[a]}
zip    : [a] -> [b] -> [(a,b)]
flat   : [[a]] -> [a]
take   : I -> [a] -> [a]
drop   : I -> [a] -> [a]
find   : (a -> B) -> [a] -> ?a
any    : (a -> B) -> [a] -> B
all    : (a -> B) -> [a] -> B
len    : [a] -> I
rev    : [a] -> [a]

-- String
split  : S -> S -> [S]
join   : S -> [S] -> S
trim   : S -> S
upper  : S -> S
lower  : S -> S
starts : S -> S -> B
ends   : S -> S -> B
has    : S -> S -> B
rep    : S -> I -> S

-- Math
abs : I -> I
min : I -> I -> I
max : I -> I -> I
clamp : I -> I -> I -> I
pow : I -> I -> I

-- Concurrency
par  : (a -> b) -> [a] -> [b]     -- parallel map
spawn : (V -> a) -> Task[a]
await : Task[a] -> !a
```

### 8.2 Field-Accessor Shorthand

The `.field` syntax without a preceding value creates a lambda:

```
.name        ≡  \x -> x.name
.age > 18    ≡  \x -> x.age > 18
.len         ≡  \x -> x.len
```

This is what makes pipelines so compact:

```
users |> filter .age > 18 |> sort .name |> map .email
```

---

## 9. Complete Example: A CLI Grep Tool

Stored as three definitions in a `.glyph` database:

**def: `Match` (kind: type)**
```
Match = {path:S line:I text:S}
```

**def: `search` (kind: fn)**
```
search pat:S path:S =
  read(path)?
    |> split "\n"
    |> zip [1..]
    |> filter \(t _) -> has pat t
    |> map \(t n) -> {path line:n text:t}
```

**def: `main` (kind: fn)**
```
@cli
main pat:S paths:[S] --color:B=true =
  paths
    |> flat map (search pat)
    |> map \m ->
      c = if color: "\x1b[31m{m.text}\x1b[0m" else: m.text
      "{m.path}:{m.line}: {c}"
    |> map say
```

**Total token count: ~65 tokens.** An equivalent Rust program would be ~300+ tokens.

---

## 10. Future Work

- **Categorical semantics**: Formal mapping of `|>` to morphism composition, `map` to
  functorial lifting, `?` to Kleisli arrows in the `Result` monad. This would allow
  Glyph programs to be formally verified using categorical semantics.
- **Dependent types**: For compile-time array length checking and more expressive FFI.
- **Effect system**: Track I/O, mutation, and concurrency as type-level effects.
- **WASM target**: For running Glyph in browsers and sandboxed environments.
- **Multi-DB programs**: A program can `ATTACH` library databases, enabling a
  package manager that is literally "download a .glyph file."
- **Bidirectional sync**: A `glyphc fmt` command that can dump the DB to
  human-readable `.gly` text files and re-import them, for version control
  with git.

---

## Appendix A: Token Analysis

Measured against cl200k (Claude/GPT-4 tokenizer):

| Construct              | Glyph tokens | Rust tokens | Python tokens |
|------------------------|:------------:|:-----------:|:-------------:|
| Function definition    | 4-6          | 8-15        | 6-10          |
| Type/struct definition | 3-8          | 8-20        | 8-15          |
| Pipeline (5 stages)    | 12-15        | 30-50       | 25-40         |
| Error propagation      | 1 (`?`)      | 1 (`?`)     | 8-15 (try/except) |
| Pattern match (3 arms) | 8-12         | 15-25       | 12-20         |
| HTTP handler           | 4-6          | 20-40       | 15-25         |
| Extern declaration     | 5-8          | 8-12        | 10-15 (ctypes)|

## Appendix B: Comparison with Existing Approaches

| Feature             | Glyph         | Rust    | Python  | APL/J  |
|---------------------|---------------|---------|---------|--------|
| Storage format      | SQLite DB     | Files   | Files   | Files  |
| Module system       | SQL queries   | mod/use | import  | ns     |
| Incremental compile | Per-def hash  | Per-crate| N/A    | N/A    |
| LLM context loading | SQL + budget  | Parse files | Parse | Parse |
| Token efficiency    | ★★★★★        | ★★      | ★★★     | ★★★★★ |
| Human readability   | ★★★           | ★★★★    | ★★★★★  | ★      |
| Searchability       | ★★★★★ (SQL)  | ★★ (grep)| ★★ (grep)| ★   |
| FFI                 | C ABI native  | Native  | ctypes  | limited|
