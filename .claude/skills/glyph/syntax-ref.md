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
| `!T` | Result (`Ok(T)` / `Err(S)`) |
| `[T]` | Array (24B header: `{ptr, len, cap}`) |
| `&T` | Reference |
| `*T` | Pointer |
| `{K: V}` | Map type |
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

## Bitwise Operators

Bitwise operations use keyword syntax (not symbols):

| Expression | C equivalent |
|------------|-------------|
| `a bitand b` | `a & b` |
| `a bitor b` | `a \| b` |
| `a bitxor b` | `a ^ b` |
| `a shl n` | `a << n` |
| `a shr n` | `a >> n` |

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
         -- ? = error propagate, ! = unwrap
atom     = INT | FLOAT | STRING | RAW_STRING | "true" | "false"
         | IDENT | UPPER_IDENT
         | "\" params "->" expr              -- lambda / closure
         | "match" expr INDENT arms DEDENT   -- match
         | "(" expr ")"                      -- grouping
         | "[" exprs "]"                     -- array literal
         | "[" expr ".." expr "]"            -- range
         | "{" fields "}"                    -- record literal
         | "." IDENT                         -- field accessor shorthand (\x -> x.field)
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
Use `\{` for literal brace. Note: `{expr}` works on string-typed values directly (no `int_to_str` needed for strings).

## Definition Forms

```bnf
fn_def      = IDENT params (":" type)? "=" (expr | INDENT block DEDENT)
params      = (IDENT (":" type)?)*
type_def    = UPPER "=" record | enum | type | generic_record
record      = "{" (IDENT ":" type ",")* "}"
generic_record = "<" type_params ">" record    -- parameterized record
type_params = IDENT ("," IDENT)*
enum        = "|" variant ("|" variant)*
variant     = UPPER | UPPER "(" types ")"
test_def    = IDENT "=" (expr | INDENT block DEDENT)
```

### Generic Type Definitions (syntactic only)

```
Pair = <A,B>{fst:A, snd:B}
Stack = <T>{items:[T], size:I}
```

The `<T>` syntax is accepted by the parser. Type parameters are **not enforced** — all fields compile to `GVal` regardless of the declared type argument. No distinct code is generated per instantiation. Use for documentation intent only; the type checker will not catch mismatches at call sites.

## Statement Forms (inside blocks)

```bnf
stmt = IDENT "=" expr              -- let binding (new variable)
     | "{" IDENT ("," IDENT)* "}" "=" expr   -- let destructuring
     | lvalue ":=" expr            -- assignment (mutate existing)
     | expr                        -- expression statement (value discarded)
```

Last expression in block is the return value. No `return` keyword.

### Let Destructuring

```
{x, y} = make_point(3, 4)   -- binds x=3, y=4
{name, age} = user_record    -- field names must match binding names (no rename in v1)
```

## Match Expression

```
match expr
  pattern -> expr_or_block
  pattern ? guard_expr -> expr_or_block    -- guard (evaluated only if pattern matches)
  pat1 | pat2 | pat3 -> expr_or_block      -- or-pattern (any of these patterns)
  _ -> default
```

### Pattern Forms

| Pattern | Matches |
|---------|---------|
| `_` | anything (wildcard) |
| `x` | anything, binds to `x` |
| `42` | integer literal |
| `"s"` | string literal |
| `true` / `false` | boolean (use lowercase!) |
| `None` | nullary enum constructor |
| `Some(x)` | enum constructor with payload |
| `Ok(v)` / `Err(e)` | Result constructors |
| `1 \| 2 \| 3` | or-pattern (any match) |
| `n ? n > 0` | any value where guard is true |

### Match Guard Examples

```
-- classify a number
classify n =
  match n
    0 -> "zero"
    n ? n < 0 -> "negative"
    n ? n > 100 -> "huge"
    _ -> "normal"

-- guard with pattern binding
safe_head arr =
  match array_len(arr)
    n ? n > 0 -> arr[0]
    _ -> -1
```

### Or-Pattern Examples

```
match cmd
  "quit" | "exit" | "q" -> do_quit()
  "help" | "?" -> show_help()
  _ -> eprintln("unknown command")
```

## Lambda / Closures

```
\x -> x + 1             -- single param
\x y -> x + y           -- multiple params (curried)
\x -> x + captured_var  -- captures outer binding (heap-allocated)
.field_name             -- shorthand: equivalent to \x -> x.field_name
```

Closures are heap-allocated `{fn_ptr, captures...}`. The captured variable is copied by value at closure creation time. Closures are uniform: all callables use the same convention (closure ptr as hidden first arg).

## Indentation Rules

- Tab = 2 spaces
- INDENT/DEDENT/NEWLINE tokens emitted by lexer
- Inside `()` `[]` `{}`: layout suppressed (free-form)
- Blank lines ignored
- `--` line comments

## Comments

```
-- line comment
x = 42  -- inline comment
```
