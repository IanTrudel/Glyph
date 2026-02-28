# Glyph Type System Specification

**Version:** 0.1 (2026-02-25)
**Status:** Partially implemented. The Rust compiler has a complete type checker; the self-hosted compiler has a partial reimplementation. Type checking is currently advisory (`glyph check`) and not enforced during `glyph build`.

---

## 1. Overview

Glyph uses a **Hindley-Milner type system** extended with **row polymorphism** for records. Types are inferred — annotations are optional. Every value at runtime is a 64-bit `long long`, but the type system reasons about what those 64 bits represent.

Key properties:
- **Complete type inference** — no annotations required (though they serve as documentation and constraints)
- **Let-polymorphism** — bindings are generalized, uses are instantiated with fresh type variables
- **Structural typing for records** — records are typed by their fields, not by name
- **Nominal typing for enums** — enum variants are identified by their type name
- **No subtyping** — types must match exactly (row polymorphism handles the "has at least these fields" pattern)
- **No typeclasses or traits** — overloading is not supported at the type level (traits exist in the parser but are not type-checked)

---

## 2. Primitive Types

| Type | Alias | Description | Runtime representation |
|------|-------|-------------|----------------------|
| `Int64` | `I` | 64-bit signed integer | `long long` |
| `UInt64` | `U` | 64-bit unsigned integer | `long long` (unsigned semantics) |
| `Float64` | `F` | 64-bit IEEE 754 float | `long long` (bit-cast) |
| `Int32` | `I32` | 32-bit signed integer | lower 32 bits of `long long` |
| `Float32` | `F32` | 32-bit IEEE 754 float | lower 32 bits |
| `Str` | `S` | UTF-8 string | Pointer to `{ptr:*u8, len:I}` (16 bytes, heap) |
| `Bool` | `B` | Boolean | `long long` 0 or 1 (i8 in Cranelift IR) |
| `Void` | `V` | Unit / no meaningful value | `long long` 0 |
| `Never` | `N` | Diverging computation | No value (function never returns) |

**Notes:**
- `Int32` and `Float32` are parsed and type-checked but not fully supported in codegen. In practice, everything is 64-bit.
- Numeric literals default to `Int64`. Float literals (containing `.`) default to `Float64`.
- There are no unsigned literals — `U` values come from casts or extern functions.

---

## 3. Compound Types

### 3.1 Function Types

```
A -> B              -- function from A to B
A -> B -> C         -- curried: A -> (B -> C), right-associative
```

All functions are curried. A multi-parameter function `f x y = x + y` has type `I -> I -> I`, meaning `I -> (I -> I)`. Function application peels one argument at a time.

Closures have the same type as regular functions. At runtime, closures are heap-allocated `{fn_ptr, captures...}` structs called via a uniform convention.

### 3.2 Array Types

```
[T]                 -- array of T
```

Runtime: `{data_ptr:*T, len:I, cap:I}` (24 bytes, heap-allocated header + heap data). All elements must have the same type. Empty arrays `[]` get a polymorphic element type that unifies with the first element pushed.

### 3.3 Optional Types

```
?T                  -- None | Some(T)
```

Runtime: `{tag:I, payload:I}` (16+ bytes, heap-allocated). `tag=0` means None, `tag=1` means Some. Constructors: `None`, `Some(value)`. The `?` postfix operator propagates None (early return). The `!` postfix operator unwraps (panics on None).

### 3.4 Result Types

```
!T                  -- Ok(T) | Err(S)
```

Runtime: same variant structure as Optional. `tag=0` means Ok, `tag=1` means Err. Error payload is always `Str`. The `?` operator propagates errors; `!` unwraps.

**Limitation:** The type checker currently treats `?` and `!` postfix operators as working on Optional only, not Result. Result propagation works at runtime but isn't type-checked correctly.

### 3.5 Tuple Types

```
(A, B)              -- pair
(A, B, C)           -- triple
```

Runtime: heap-allocated array-like structure. Fields accessed by index. Tuples are positionally typed — `(I, S)` is different from `(S, I)`.

### 3.6 Record Types

```
{name:S, age:I}                 -- closed record (exactly these fields)
{name:S, age:I ..}              -- open record (at least these fields)
{name:S, age:I, email:S ..}    -- open record with more known fields
```

Runtime: heap-allocated struct. Fields are stored in **alphabetical order** by name, 8 bytes each. A record `{age:I, name:S}` has `age` at offset 0 and `name` at offset 8.

Records use **structural typing with row polymorphism** (see Section 6). Two records with the same fields and field types are the same type, regardless of where they're defined.

**Named record types** (type definitions) associate a name with a record structure:
```
Person = {name:S age:I email:S}
```
These are used by the gen=2 struct codegen to produce C `typedef struct` declarations. At the type level, they're still structural.

### 3.7 Map Types

```
{K:V}               -- map from K to V (uppercase first token)
```

Parsed and type-checked but **not implemented in codegen**. The parser disambiguates `{K:V}` (map, uppercase key) from `{k:T}` (record, lowercase field name).

### 3.8 Reference and Pointer Types

```
&T                  -- reference to T
*T                  -- raw pointer to T
```

Parsed and present in the type system. `&` creates a reference (prefix operator), `*` dereferences. In practice, since everything is a `long long` at runtime, these don't add indirection — they're type-level markers.

### 3.9 Named Types

```
TypeName            -- named type (must start with uppercase)
TypeName[A, B]      -- parameterized type (type application)
```

Named types are used for:
- Enum types and their variants
- Record type aliases (struct definitions)
- Type parameters in generic definitions (aspirational)

---

## 4. Type Definitions

### 4.1 Record Types

```
TypeName = {field1:Type1 field2:Type2 ...}
```

Example:
```
Token = {kind:I start:I end:I line:I}
AstNode = {kind:I ival:I sval:S n1:I n2:I n3:I ns:[I]}
```

Fields are separated by whitespace (commas optional). Fields are stored and accessed in alphabetical order at runtime.

### 4.2 Enum Types

```
TypeName =
  | Variant1
  | Variant2(Type)
  | Variant3(Type1, Type2)
  | Variant4 {field1:Type1 field2:Type2}
```

Three variant forms:
- **Nullary**: `| None` — no payload, just a tag
- **Positional**: `| Some(I)` — tagged with positional payload fields
- **Named**: `| Person {name:S age:I}` — tagged with named payload fields

Runtime: `{tag:I, payload1:I, payload2:I, ...}` heap-allocated. Tag is at offset 0, payload fields at offsets 8, 16, etc. Discriminants are assigned sequentially (0, 1, 2, ...) in declaration order.

Example:
```
Expr =
  | IntLit(I)
  | StrLit(S)
  | BinOp(Expr, S, Expr)
  | Call(Expr, [Expr])
```

**Limitations:**
- Recursive enum types (like `Expr` above) parse but may cause issues in type inference (occurs check)
- Type parameters on enums (e.g., `Option[T] = | None | Some(T)`) parse but are not fully supported in inference or codegen
- The self-hosted compiler treats all enum payloads as `I` (Int) at the MIR level

### 4.3 Type Aliases

```
TypeName = ExistingType
```

Example:
```
Name = S
Callback = I -> I -> I
```

---

## 5. Type Annotations

Annotations are optional everywhere. When present, they constrain inference.

### 5.1 Function Parameters

```
fn_name param1:Type1 param2:Type2 = body
```

Example:
```
add x:I y:I = x + y
greet name:S = println("Hello, {name}")
```

### 5.2 Return Types

```
fn_name params : ReturnType = body
```

Example:
```
factorial n:I : I =
  match n <= 1
    true -> 1
    _ -> n * factorial(n - 1)
```

### 5.3 Lambda Parameters

```
\param1:Type1 param2:Type2 -> body
```

### 5.4 Flag Parameters

```
fn_name --flag:Type=default = body
```

Flag parameters have optional type annotations and optional default values.

### 5.5 Let Bindings

Let bindings do **not** have type annotations. The type is always inferred from the right-hand side.

---

## 6. Row Polymorphism

Row polymorphism allows functions to work with any record that has at least certain fields, without specifying the complete record type.

### 6.1 Open vs Closed Records

- **Closed record**: `{name:S, age:I}` — exactly these two fields
- **Open record**: `{name:S, age:I ..}` — has at least `name` and `age`, may have more

In type annotations, `..` at the end of a record type makes it open.

### 6.2 Field Access Constraint

When you write `x.name`, the type checker creates the constraint:

```
x : {name:t ..}     -- x must be a record with at least a 'name' field of type t
```

This means the function:
```
get_name x = x.name
```
gets the inferred type:
```
get_name : {name:t ..} -> t
```
It works on ANY record that has a `name` field, regardless of what other fields exist.

### 6.3 Field Accessor Sugar

The `.field` syntax (dot without a preceding expression) creates a lambda:
```
.name           -- equivalent to \x -> x.name
                -- type: {name:t ..} -> t
```

This composes naturally with pipes:
```
people |> map(.name)    -- extract names from a list of records
```

### 6.4 Record Patterns

Record patterns in match expressions are always **open**:
```
match person
  {name, age} -> "..."    -- matches any record with name and age fields
```

### 6.5 Row Variable Unification

When two open records unify, a fresh row variable connects them:

```
-- f : {x:I ..a} -> {y:I ..b} -> ???
-- If applied to the same record r, then:
--   r : {x:I ..a}  AND  r : {y:I ..b}
-- Unification produces: r : {x:I, y:I ..c}
```

This is the standard Rémy-style row polymorphism algorithm. The Rust implementation fully implements this; the self-hosted version uses a simpler bidirectional field-matching approach.

---

## 7. Type Inference Algorithm

### 7.1 Core Algorithm: Hindley-Milner

The inference engine uses Algorithm W (constraint-based variant):

1. **Fresh variables**: Each unknown type gets a fresh type variable `t0`, `t1`, etc.
2. **Constraint generation**: Expressions generate equality constraints between type variables
3. **Unification**: Constraints are solved by unifying types, binding type variables to concrete types
4. **Generalization**: At let-bindings, free type variables are universally quantified (`forall`)
5. **Instantiation**: At use sites, `forall`-bound variables get fresh copies

### 7.2 Unification

Unification follows the standard algorithm with union-find:

```
unify eng a b =
  a_ = subst_walk(eng, a)
  b_ = subst_walk(eng, b)
  match a_ == b_
    true -> 0
    _ ->
      ta = pool[a_].tag
      tb = pool[b_].tag
      match ta == ty_error() | tb == ty_error()
        true -> 0                                       -- error absorbs
        _ -> match ta == ty_var()
          true -> subst_bind(eng, pool[a_].n1, b_)      -- bind variable
          _ -> match tb == ty_var()
            true -> subst_bind(eng, pool[b_].n1, a_)
            _ -> unify_tags(eng, pool[a_], pool[b_])    -- structural match

unify_tags eng na nb =
  match na.tag == ty_fn() && nb.tag == ty_fn()
    true ->
      unify(eng, na.n1, nb.n1)                          -- unify params
      unify(eng, na.n2, nb.n2)                          -- unify returns
    _ -> match na.tag == ty_array() && nb.tag == ty_array()
      true -> unify(eng, na.n1, nb.n1)                  -- unify elements
      _ -> match na.tag == ty_record() && nb.tag == ty_record()
        true -> unify_records(eng, na, nb)               -- row unification
        _ -> match na.tag == nb.tag
          true -> 0                                      -- same primitive
          _ -> 0 - 1                                     -- type error
```

**Occurs check**: Before binding a variable to a type, check that the variable doesn't appear in the type (prevents infinite types like `t0 = [t0]`).

**Error absorption**: `Error` types unify with anything, preventing cascading error messages.

### 7.3 Row Unification

For two record types `{f1:T1, f2:T2 ..r1}` and `{f2:U2, f3:U3 ..r2}`:

```
unify_records eng ra rb =
  -- Bidirectional: check each side's fields against the other
  r1 = unify_fields_against(eng, ra.ns, rb.ns, rb.n1, 0)
  match r1 < 0
    true -> r1
    _ -> unify_fields_against(eng, rb.ns, ra.ns, ra.n1, 0)

unify_fields_against eng fs1 fs2 rest_var i =
  match i >= array_len(fs1)
    true -> 0
    _ ->
      f = pool[fs1[i]]                        -- field node: {sval=name, n1=type}
      match_idx = find_field(eng, fs2, f.sval) -- look for same-named field
      match match_idx >= 0
        true ->
          other = pool[fs2[match_idx]]
          unify(eng, f.n1, other.n1)           -- unify field types
          unify_fields_against(eng, fs1, fs2, rest_var, i + 1)
        _ -> match rest_var >= 0
          true ->                              -- open record absorbs extra field
            unify_fields_against(eng, fs1, fs2, rest_var, i + 1)
          _ -> 0 - 1                           -- closed record, field missing: error
```

The five cases:
1. **Common fields** (`f2`): unify `T2` with `U2`
2. **Left-only fields** (`f1`): must be absorbed by `r2` (its row variable)
3. **Right-only fields** (`f3`): must be absorbed by `r1` (its row variable)
4. **Both open**: Create fresh `r3`. Bind `r1 = {f3:U3 ..r3}` and `r2 = {f1:T1 ..r3}`
5. **One open, one closed**: The open side's variable gets bound to the missing fields
6. **Both closed**: All fields must match exactly

### 7.4 Let-Polymorphism (Generalization)

At a `let` binding:
```
x = \a -> a     -- inferred as t0 -> t0
```

The type `t0 -> t0` is generalized: `t0` doesn't appear in the environment, so it becomes `forall t0. t0 -> t0`. Later uses of `x` each get a fresh instance:

```
x(42)       -- instantiated as I -> I
x("hello")  -- instantiated as S -> S
```

### 7.5 Mutual Recursion

Functions are pre-registered with fresh type skeletons before inference begins:
```
-- Pre-registration phase:
is_even : t0 -> t1    -- fresh vars
is_odd  : t2 -> t3    -- fresh vars

-- Inference phase (processes bodies):
is_even n = match n == 0 -> true, _ -> is_odd(n - 1)
-- Constrains: t0 = I, t1 = B, t2 = I, t3 = B
```

### 7.6 Binary Operator Typing

| Operator | Left | Right | Result |
|----------|------|-------|--------|
| `+` | `I` | `I` | `I` |
| `+` | `F` | `F` | `F` |
| `+` | `S` | `S` | `S` |
| `-` `*` `/` `%` | `I` | `I` | `I` |
| `-` `*` `/` `%` | `F` | `F` | `F` |
| `==` `!=` | `T` | `T` | `B` |
| `<` `>` `<=` `>=` | `I`/`F`/`S` | same | `B` |
| `&&` `\|\|` | `B` | `B` | `B` |

No implicit coercion between numeric types. `I + F` is a type error.

### 7.7 Pipeline Operator Typing

```
x |> f      -- typed as f(x)
             -- f must unify with typeof(x) -> t
```

```
f >> g      -- typed as \x -> g(f(x))
             -- f: a -> b, g: b -> c, result: a -> c
```

---

## 8. Pattern Matching and Exhaustiveness

### 8.1 Pattern Types

| Pattern | Syntax | Constrains scrutinee to |
|---------|--------|------------------------|
| Wildcard | `_` | any type (fresh var) |
| Variable | `x` | any type (binds `x` in scope) |
| Integer | `42` | `I` |
| String | `"hello"` | `S` |
| Boolean | `true` / `false` | `B` |
| Constructor | `Some(x)` | the enum type containing `Some` |
| Record | `{name, age}` | open record with those fields |
| Tuple | `(a, b)` | tuple of matching arity |
| Or-pattern | `1 \| 2 \| 3` | same type as each alternative |

### 8.2 Exhaustiveness

**Not currently checked.** The type system does not verify that match expressions cover all possible values. A non-exhaustive match is:
- **Rust compiler**: Emits `Unreachable` terminator → hardware trap at runtime
- **Self-hosted compiler**: Falls through silently to the next basic block (a known bug — see `c-runtime-assessment.md`)

### 8.3 Or-Patterns

```
match x
  1 | 2 | 3 -> "small"
  _ -> "large"
```

Or-patterns desugar to repeated tests in MIR. Type inference uses the first alternative's type for the whole pattern (limitation — does not verify alternatives have consistent types).

---

## 9. Implementation Status

### 9.1 Rust Type Checker (`glyph-typeck`)

**Fully implemented:**
- All primitive types including `Int32`, `Float32`
- All compound types: `Fn`, `Array`, `Tuple`, `Record`, `Opt`, `Res`, `Ref`, `Ptr`, `Map`, `Named`
- Complete HM inference with let-polymorphism
- Row polymorphism with full open-open unification
- Occurs check
- Name resolution against the database with dependency tracking
- Pre-registration for mutual recursion
- All expression forms, statement forms, and pattern forms
- Error accumulation (continues after errors)
- ~800 lines across 7 modules, 15 tests

**Not implemented:**
- Type definitions (resolve is a stub — type bodies are not processed)
- Trait/typeclass constraints
- Higher-kinded types
- Exhaustiveness checking
- GADTs or dependent types
- `Assign` (:=) type checking (does not verify LHS matches RHS)

### 9.2 Self-Hosted Type Checker (glyph.glyph)

**Implemented:**
- Primitive types: `I`, `U`, `F`, `S`, `B`, `V`, `N`
- Compound types: `Fn`, `Array`, `Opt`, `Res`, `Record`, `Tuple`, `Named`
- Union-find substitution with path compression and occurs check
- Unification for all supported types
- Record row polymorphism (simplified bidirectional matching)
- Environment with scope push/pop
- Expression inference for all forms
- Pre-registration for forward references
- ~119 definitions, ~27k characters

**Not implemented:**
- `Ref`, `Ptr`, `Map` types (not in the type tag set)
- `Int32`, `Float32`
- Let-polymorphism generalization (the `mk_tforall` constructor exists but no `generalize` function)
- Full open-open row unification (uses simpler bidirectional check)
- `subst_resolve` doesn't recurse into `Res`, `Tuple`, `Named`, `ForAll`
- `!` unwrap on Result types (treats as Optional)
- Type annotations on parameters (not parsed/used)
- Constructor pattern decomposition (just env lookup)

### 9.3 Integration Status

The type checker is available via `glyph check <db>` but is **not integrated into the build pipeline**. Known issues:
- Record unification bugs with dangling pool indices in the self-hosted version
- Advisory warnings only — does not block compilation
- Missing registrations for many runtime functions (only 11 of ~40+ registered)

---

## 10. Type System Design Decisions

### 10.1 Everything is `long long`

At the C ABI level, every value is `long long` (64-bit integer). Pointers are cast to/from integers. This simplifies FFI and codegen enormously but means:
- Type errors become segfaults, not compile errors
- No tagged unions — the type system must track what things are
- Bool is i8 in Cranelift IR but i64 at function boundaries (coercion required)

### 10.2 No Implicit Coercion

`I + F` is a type error. You must explicitly convert: `float_to_int(f) + i` or `i + int_to_float(f)`. This keeps the type system simple and predictable.

### 10.3 Structural Records, Nominal Enums

Records are structural: `{x:I, y:I}` is the same type regardless of where it appears. This enables row polymorphism.

Enums are nominal: `Some(42)` belongs to `Option`, not to any enum with a `Some` variant. Variant names are globally unique (no two enum types can share a variant name).

### 10.4 Curried Functions

All functions are conceptually single-argument. `f : I -> S -> B` means `f` takes an `I` and returns a function `S -> B`. This enables partial application:

```
add x y = x + y
add3 = add(3)      -- add3 : I -> I
add3(4)             -- 7
```

### 10.5 No Garbage Collection

Types like `Array`, `Str`, `Record`, and enum variants are heap-allocated and never freed. The type system does not track ownership or lifetimes. This is acceptable for short-lived programs but requires manual arena management for long-running ones.

---

## 11. Future Directions

### 11.1 Enforce Type Checking in Build

The most impactful improvement: make `glyph build` run the type checker and refuse to compile on type errors. This would catch the entire class of "wrong type → segfault" bugs at compile time.

**Blockers:** Record unification bugs in the self-hosted type checker, incomplete runtime function registrations.

### 11.2 Exhaustiveness Checking

Verify that match expressions cover all possible values of the scrutinee type. Requires:
- Pattern matrix analysis
- Knowledge of all enum variants
- Integration with the type inference results

### 11.3 Typeclass / Trait System

The parser already accepts trait definitions. Implementing typeclasses would enable:
- Overloaded operators (e.g., `+` for both `I` and custom types)
- Generic algorithms (e.g., `sort : [a] -> [a] where a: Ord`)
- `Show`/`Display`-like automatic string conversion

### 11.4 Effect System

Track which functions can panic, perform I/O, or diverge. Would enable:
- Pure function guarantees
- Safe error handling without `?` everywhere
- Better optimization in the compiler

### 11.5 Lifetime / Ownership Tracking

For long-running programs, some form of ownership tracking would enable safe memory management without GC. Could be as simple as:
- Arena-scoped allocations (tied to request/response cycles)
- Linear types (values used exactly once)
- Region-based memory management

---

## Appendix A: Type Grammar (BNF)

```bnf
type        = type_atom ("->" type)?          -- function type (right-associative)

type_atom   = UPPER_IDENT                     -- named type (I, S, MyType, etc.)
            | LOWER_IDENT                     -- type variable (a, b, t, etc.)
            | UPPER_IDENT "[" types "]"       -- type application: List[I]
            | "?" type_atom                   -- optional: ?I
            | "!" type_atom                   -- result: !I
            | "&" type_atom                   -- reference: &I
            | "*" type_atom                   -- pointer: *I
            | "[" type "]"                    -- array: [I]
            | "{" UPPER_IDENT ":" type "}"    -- map: {S:I}
            | "{" record_fields "}"           -- record: {x:I, y:I}
            | "(" type "," types ")"          -- tuple: (I, S)
            | "(" type ")"                    -- grouping

record_fields = (LOWER_IDENT ":" type ","?)*
                ("..")?                       -- optional row variable extension

types       = type ("," type)*
```

## Appendix B: Type Alias Table

| Alias | Full Name | Runtime Size |
|-------|-----------|-------------|
| `I` | `Int64` | 8 bytes |
| `U` | `UInt64` | 8 bytes |
| `F` | `Float64` | 8 bytes |
| `I32` | `Int32` | 4 bytes (8 at ABI) |
| `F32` | `Float32` | 4 bytes (8 at ABI) |
| `S` | `Str` | 16 bytes (ptr+len) |
| `B` | `Bool` | 1 byte (8 at ABI) |
| `V` | `Void` | 0 bytes (8 at ABI) |
| `N` | `Never` | 0 bytes (unreachable) |

## Appendix C: Type Tags (Self-Hosted)

The self-hosted type checker represents types as `TyNode` records in a pool. Each node has a `tag` field:

| Tag | Value | Structure | Description |
|-----|-------|-----------|-------------|
| `ty_int` | 1 | — | Int64 |
| `ty_uint` | 2 | — | UInt64 |
| `ty_float` | 3 | — | Float64 |
| `ty_str` | 4 | — | String |
| `ty_bool` | 5 | — | Bool |
| `ty_void` | 6 | — | Void |
| `ty_never` | 7 | — | Never |
| `ty_fn` | 10 | n1=param, n2=ret | Function |
| `ty_array` | 11 | n1=elem | Array |
| `ty_opt` | 12 | n1=inner | Optional |
| `ty_res` | 13 | n1=inner | Result |
| `ty_record` | 14 | ns=field indices, n1=rest var/-1 | Record |
| `ty_var` | 15 | n1=var id | Type variable |
| `ty_forall` | 16 | n1=body, ns=bound vars | Universal quantification |
| `ty_named` | 17 | sval=name, ns=type args | Named type |
| `ty_tuple` | 18 | ns=element types | Tuple |
| `ty_field` | 20 | sval=name, n1=type | Record field |
| `ty_error` | 99 | — | Error sentinel |
