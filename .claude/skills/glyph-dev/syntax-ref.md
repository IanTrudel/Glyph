# Glyph Syntax Reference

## Type Aliases

| Alias | Full | Size | Notes |
|-------|------|------|-------|
| `I` | Int64 | 8B | Default numeric type |
| `U` | UInt64 | 8B | Unsigned |
| `F` | Float64 | 8B | |
| `S` | Str | 16B | Fat ptr `{ptr, len}` |
| `B` | Bool | 1B | i8 internally, i64 at ABI |
| `V` | Void | 0B | Unit |
| `N` | Never | 0B | Diverging |

## Compound Types

| Syntax | Meaning |
|--------|---------|
| `?T` | Optional |
| `!T` | Result |
| `[T]` | Array (24B header: `{ptr, len, cap}`) |
| `&T` | Reference |
| `*T` | Pointer |
| `{K: V}` | Map |
| `(A, B)` | Tuple |
| `A -> B` | Function (right-associative) |
| `{x: I, y: I}` | Record (fields alphabetical, 8B each) |

## Operator Precedence (low to high)

| Lvl | Operators | Assoc |
|-----|-----------|-------|
| 1 | `\|>` pipe | Left |
| 2 | `>>` compose | Left |
| 3 | `\|\|` | Left |
| 4 | `&&` | Left |
| 5 | `==` `!=` `<` `>` `<=` `>=` | None |
| 6 | `+` `-` | Left |
| 7 | `*` `/` `%` | Left |
| 8 | `-` `!` `&` `*` (prefix) | Right |
| 9 | `.` `()` `[]` `?` `!` (postfix) | Left |

## Expression Forms

```bnf
expr     = pipe
pipe     = compose ("|>" compose)*
compose  = or (">>" or)*
or       = and ("||" and)*
and      = cmp ("&&" cmp)*
cmp      = add (("==" | "!=" | "<" | ">" | "<=" | ">=") add)?
add      = mul (("+" | "-") mul)*
mul      = unary (("*" | "/" | "%") unary)*
unary    = ("-" | "!" | "&" | "*") unary | postfix
postfix  = atom ("." IDENT | "(" args ")" | "[" expr "]" | "?" | "!")*
atom     = INT | FLOAT | STRING | RAW_STRING | "true" | "false"
         | IDENT | UPPER_IDENT
         | "\" params "->" expr              -- lambda
         | "match" expr INDENT arms DEDENT   -- match
         | "(" expr ")"                      -- grouping
         | "[" exprs "]"                     -- array literal
         | "[" expr ".." expr "]"            -- range
         | "{" fields "}"                    -- record literal
         | "." IDENT                         -- field accessor shorthand
```

## Literals

```
42          -- Int (i64), underscores ok: 1_000_000
3.14        -- Float (f64)
"hello"     -- String, interpolation with {expr}
r"raw\n"    -- Raw string, no escapes
true false  -- Bool
```

### String Escapes

`\n` `\t` `\r` `\\` `\"` `\{` `\0` `\xHH`

### String Interpolation

`"hello {name}, you are {int_to_str(age)}"` compiles to sb_new/sb_append/sb_build.
Use `\{` for literal brace.

## Definition Forms

```bnf
fn_def   = IDENT params (":" type)? "=" (expr | INDENT block DEDENT)
params   = (IDENT (":" type)?)*
type_def = UPPER "=" record | enum | type
record   = "{" (IDENT ":" type ",")* "}"
enum     = "|" variant ("|" variant)*
variant  = UPPER | UPPER "(" types ")"
test_def = IDENT "=" (expr | INDENT block DEDENT)
```

## Statement Forms (inside blocks)

```bnf
stmt = IDENT "=" expr          -- let binding (new variable)
     | lvalue ":=" expr        -- assignment (mutate existing)
     | expr                    -- expression statement (value discarded)
```

Last expression in block is the return value. No `return` keyword.

## Match Expression

```
match expr
  pattern -> expr_or_block
  pattern -> expr_or_block
  _ -> default
```

Patterns: `_`, `x` (bind), `42` (int), `"s"` (string), `true`/`false`, `None` (nullary), `Some(x)` (constructor).

## Indentation Rules

- Tab = 4 spaces
- INDENT/DEDENT/NEWLINE tokens emitted by lexer
- Inside `()` `[]` `{}`: layout suppressed (free-form)
- Blank lines ignored
- `--` line comments

## Comments

```
-- line comment
x = 42  -- inline comment
```
