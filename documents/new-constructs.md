# New Language Constructs for Glyph

**Version:** 0.1 (2026-02-25)
**Scope:** Self-hosted compiler (glyph.glyph). Constructs prioritized by impact on the existing ~800 definitions.

---

## 1. Guards in Match Arms

**Priority: HIGH — biggest readability win, pure desugar**

The single biggest source of nesting in glyph.glyph. Nearly every `lower_*`, `cg_*`, and `infer_*` function has deeply nested match-within-match chains.

### Current

```
match x
  1 -> "one"
  _ -> match x > 0
    true -> "positive"
    _ -> "negative"
```

### Proposed

```
match x
  1 -> "one"
  n | n > 0 -> "positive"
  _ -> "negative"
```

### Syntax

```
pattern | condition -> body
```

The guard `| condition` is an optional boolean expression after the pattern. The arm matches only if both the pattern matches AND the guard evaluates to true. The pattern variable (`n`) is in scope within the guard.

### Implementation

Desugar in MIR lowering. When an arm has a guard:

1. Match the pattern as usual (bind variables)
2. Evaluate the guard expression
3. If guard is true, jump to the arm body
4. If guard is false, jump to the next arm (not the merge block)

This is a conditional branch inside the arm's basic block — a pattern already exists in `lower_match_bool`.

### Parser Changes

In `parse_pattern` or `parse_match_arm`: after parsing the pattern, check for `|` token. If present, parse the guard expression before `->`.

```
parse_match_arm tokens pos ast =
  pat = parse_pattern(tokens, pos, ast)
  guard = match cur_kind(tokens, pat.pos) == tk_pipe
    true -> parse_expr(tokens, pat.pos + 1, ast)
    _ -> mk_no_guard()
  expect_tok(tokens, guard.pos, tk_arrow)
  body = parse_expr(tokens, guard.pos + 1, ast)
  mk_arm(pat, guard, body)
```

Note: `|` is already used for or-patterns (`1 | 2 | 3 -> ...`). Disambiguation: `|` followed by a pattern literal/identifier that looks like a pattern continues or-pattern parsing; `|` followed by an expression with operators (`n > 0`, `f(x)`) is a guard. Alternatively, use a different token for guards (e.g., `when` or `if`):

```
match x
  n when n > 0 -> "positive"
  _ -> "negative"
```

### Impact

Functions with 4-8 levels of nested `match ... true ->` flatten to a single match with guards. Affects ~100+ definitions.

---

## 2. Record Update Syntax

**Priority: HIGH — reduces boilerplate for record-heavy code**

Records with many fields are common in glyph.glyph (MIR lowering state has 14 fields, engine has 8 fields, statements have 7 fields). Updating one or two fields requires reconstructing the entire record.

### Current

```
-- To change just the fn_name field of a 14-field record:
new_ctx = {block_stmts: ctx.block_stmts, block_terms: ctx.block_terms,
           local_names: ctx.local_names, local_types: ctx.local_types,
           cur_block: ctx.cur_block, nxt_local: ctx.nxt_local,
           nxt_block: ctx.nxt_block, var_names: ctx.var_names,
           var_locals: ctx.var_locals, var_marks: ctx.var_marks,
           fn_name: "new_name", fn_params: ctx.fn_params,
           fn_entry: ctx.fn_entry, za_fns: ctx.za_fns}
```

### Proposed

```
new_ctx = {ctx | fn_name: "new_name"}
```

### Syntax

```
{base_expr | field1: val1, field2: val2, ...}
```

Creates a new record with all fields from `base_expr`, except the listed fields which take the new values.

### Implementation

Parser desugar. `{ctx | fn_name: "new_name"}` expands to a full record construction where unlisted fields are copied from the base expression via field access:

```
_tmp = ctx
{block_stmts: _tmp.block_stmts, ..., fn_name: "new_name", ...}
```

The parser needs to know which fields exist on the record. Two approaches:

**A. Explicit fields only:** The update syntax only sets the listed fields. The type checker (with context-aware inference) resolves the full field set and the lowerer generates the complete record. Requires type info at lowering time.

**B. Runtime copy + overwrite:** Allocate a new record, `memcpy` from the base, then overwrite specific field offsets. This works without type info but needs the record size at codegen time.

Approach A is cleaner and integrates with the context-aware inference pipeline. Approach B is a pragmatic fallback.

### Impact

Any function that needs to "modify" a record (which is immutable) benefits. Currently rare in glyph.glyph because the pattern is so verbose that code avoids it — using mutable single-element arrays instead. Record update would make immutable record patterns practical.

---

## 3. Partial Application Sugar

**Priority: HIGH — enables point-free style and combinators**

### Current

```
-- To pass a partially applied function:
add_one x = x + 1
result = map(nums, add_one)

-- Or with a full lambda (requires closures):
result = map(nums, \x -> x + 1)
```

### Proposed

```
result = map(nums, _ + 1)
result = filter(people, _.age > 18)
result = map(names, str_len(_))
```

### Syntax

`_` in an expression context creates an anonymous single-parameter lambda. Multiple `_` create multi-parameter lambdas (left to right):

```
_ + _        -- \a b -> a + b
_.name       -- \x -> x.name  (already exists as .name shorthand)
f(_, 10)     -- \x -> f(x, 10)
```

### Implementation

Parser transform. When `_` appears in an expression position (not a pattern), wrap the enclosing expression in a lambda:

```
_ + 1        →  \x -> x + 1
f(_, 10)     →  \x -> f(x, 10)
_ + _        →  \x y -> x + y
```

The parser collects `_` placeholders, assigns them parameter names, and wraps in a lambda node.

### Dependency

Requires closures in C codegen for full usefulness. Without closures, partial application can still work for named top-level functions via eta-expansion.

### Impact

Combined with closures, this makes functional combinators extremely concise. `map(arr, _ * 2)` is more token-efficient than both the manual loop and the full lambda.

---

## 4. If-Then-Else

**Priority: DEFERRED — imperative construct, doesn't fit Glyph's functional model**

*Included for completeness. Glyph uses `match` for all conditional logic, keeping the language purely expression-based with a single conditional construct. Guards (section 1) address the verbosity of boolean conditions within match without introducing an imperative `if/else` statement.*

Every conditional in Glyph is currently:

```
match condition
  true -> then_branch
  _ -> else_branch
```

This is 3 lines for a simple boolean branch.

### Proposed

```
if condition then_expr else_expr
```

Or with indentation:

```
if condition
  then_expr
else
  else_expr
```

### Implementation

Pure parser desugar — rewrite to `match condition | true -> then_expr | _ -> else_expr`. Zero MIR or codegen changes.

```
parse_if tokens pos ast =
  cond = parse_expr(tokens, pos + 1, ast)
  then_body = parse_expr(tokens, cond.pos, ast)
  expect_tok(tokens, then_body.pos, tk_else)
  else_body = parse_expr(tokens, then_body.pos + 1, ast)
  mk_match(cond, [mk_arm(pat_bool(true), then_body), mk_arm(pat_wildcard(), else_body)])
```

### Impact

Simplifies hundreds of `match condition | true -> ... | _ -> ...` patterns. The most common pattern in the codebase.

---

## 5. Let Destructuring

**Priority: HIGH — parser-level, helps everywhere records are used**

### Current

```
result = get_person()
name = result.name
age = result.age
```

### Proposed

```
{name, age} = get_person()
```

### Syntax

```
{field1, field2, ...} = expr
```

Each `field` becomes a local variable bound to `expr.field`. Optionally with renaming:

```
{name: n, age: a} = get_person()
-- n = result.name, a = result.age
```

### Implementation

Parser desugar. `{name, age} = expr` expands to:

```
_tmp = expr
name = _tmp.name
age = _tmp.age
```

The parser generates a fresh temporary name, a let binding for the whole expression, and individual let bindings for each field access.

### Impact

The compiler constantly unpacks records — MIR statements (`stmt.sdest`, `stmt.skind`, `stmt.sop1`), AST nodes (`node.kind`, `node.n1`, `node.sval`), parse results (`r.node`, `r.pos`). Destructuring reduces 3-4 lines to 1.

---

## 6. Closures in C Codegen

**Priority: HIGH — unlocks the entire functional programming model**

Currently `rv_make_closure` in `cg_stmt` is unimplemented. Programs using closures must be compiled by the Cranelift backend. This blocks all functional combinators.

### Current (manual recursion)

```
filter_positive arr i acc =
  match i >= glyph_array_len(arr)
    true -> acc
    _ ->
      x = arr[i]
      match x > 0
        true ->
          glyph_array_push(acc, x)
          filter_positive(arr, i + 1, acc)
        _ -> filter_positive(arr, i + 1, acc)
```

### With Closures

```
positives = filter(nums, \x -> x > 0)
names = map(people, .name)
total = fold(nums, 0, \acc x -> acc + x)
```

### Implementation

The Rust/Cranelift backend already implements closures:

1. **Heap-allocate closure record**: `{fn_ptr, capture1, capture2, ...}`
2. **Calling convention**: every function receives a hidden first parameter (closure pointer). Non-closure functions ignore it.
3. **Capture analysis**: identify free variables in the lambda body
4. **Indirect call**: call through the function pointer in the closure record

For C codegen, emit:

```c
// Closure creation
long long* _closure = (long long*)glyph_alloc(N * 8);
_closure[0] = (long long)&lambda_123;
_closure[1] = captured_var1;
_closure[2] = captured_var2;

// Closure call
long long result = ((long long(*)(long long, long long))_closure[0])(_closure, arg1);
```

### New Definitions Needed

- `cg_make_closure` — emit closure allocation and capture storage
- `cg_closure_call` — emit indirect call through closure pointer
- `lower_lambda` — capture analysis, generate closure MIR (already exists for Cranelift)
- `cg_lambda_fn` — emit the lambda as a static C function with closure-pointer parameter

### Impact

Eliminates the most common boilerplate pattern: recursive loops with index + accumulator that are just `map`, `filter`, or `fold`. Also enables `.field` shorthand lambdas and pipe-friendly code.

---

## 7. Where Clauses (Local Definitions)

**Priority: MEDIUM — reduces namespace pollution**

Every helper function is top-level. Functions like `fill_tmap`, `find_line_start`, `extract_until_newline` are only used by one caller. The global namespace has ~800 entries.

### Proposed

```
pos_to_linecol src pos =
  count(src, pos, 0, 1, 1)
  where
    count src pos i line col =
      match i >= pos
        true -> {line: line, col: col}
        _ -> match str_char_at(src, i) == 10
          true -> count(src, pos, i + 1, line + 1, 1)
          _ -> count(src, pos, i + 1, line, col + 1)
```

### Syntax

```
main_expr
  where
    helper1 params = body1
    helper2 params = body2
```

The `where` block introduces local function definitions visible only within the enclosing function body.

### Implementation

Two approaches:

**A. Lift to top-level (simple):** Prefix local names with the enclosing function name (`pos_to_linecol$count`). All free variables must be passed explicitly — no implicit capture. This is purely a parser/name-resolution transform.

**B. Closure-based (requires closures):** Local functions capture free variables from the enclosing scope. This requires closure support in C codegen.

Approach A is simpler and works without closures. The compiler already handles top-level functions, so lifting is straightforward.

### Impact

Groups related definitions, reduces global namespace clutter. Moderate benefit — the database model already provides per-definition isolation, so namespace pollution is less visible than in file-based languages.

---

## 8. Multi-Way Cond

**Priority: MEDIUM — sugar for dispatch chains**

For chains of conditions that don't share a scrutinee:

### Current

```
classify x y =
  match x > 0
    true -> match y > 0
      true -> "Q1"
      _ -> "Q4"
    _ -> match y > 0
      true -> "Q2"
      _ -> "Q3"
```

### Proposed

```
classify x y =
  cond
    x > 0 && y > 0 -> "Q1"
    x < 0 && y > 0 -> "Q2"
    x < 0 && y < 0 -> "Q3"
    _ -> "Q4"
```

### Implementation

Desugar `cond` to nested `match ... true ->` chains:

```
cond
  c1 -> e1
  c2 -> e2
  _ -> e3
```

Becomes:

```
match c1
  true -> e1
  _ -> match c2
    true -> e2
    _ -> e3
```

Pure parser transform. Alternatively, `cond` arms desugar to `match true | c1 -> e1 | c2 -> e2 | _ -> e3` if guards are available.

### Impact

Functions like `dispatch_cmd`, `is_runtime_fn`, `op_type` — long chains of equality checks on different values — become more readable.

---

## 9. Type Annotations

**Priority: MEDIUM — documentation and inference guidance**

### Proposed

```
factorial (n : I) : I =
  match n <= 1
    true -> 1
    _ -> n * factorial(n - 1)
```

### Syntax

```
fn_name (param : Type) ... : ReturnType = body
```

Annotations are optional. When present, the type checker unifies the annotation with the inferred type, producing an error on mismatch.

### Implementation

- **Parser**: recognize `: Type` after parameter names and before `=`
- **Type checker**: parse type annotation into a TyNode, unify with inferred type
- **MIR lowering**: annotations could feed into `local_types` for better codegen decisions

### Impact

Useful for documentation and catching bugs. The LLM generating Glyph code could include annotations as self-documentation. Not strictly needed (HM infers everything), but helps when inference produces unexpected results.

---

## 10. String Formatting

**Priority: LOW — current approach works**

### Current

```
s3("got ", itos(n), " results")
-- or
"got {itos(n)} results"
```

### Proposed

Auto-coercion in string interpolation:

```
"got {n} results"    -- n:I auto-converts via itos
```

### Implementation

In MIR lowering for `ex_str_interp`, when the interpolated expression has a known non-string type (from context-aware inference), automatically wrap it in the appropriate conversion (`itos` for int, `btos` for bool, etc.).

### Impact

Minor convenience. The explicit `itos()` call is clear and LLM-friendly.

---

## 11. Array Comprehensions

**Priority: LOW — requires closures**

### Proposed

```
squares = [x * x | x <- range(10)]
evens = [x | x <- nums, x % 2 == 0]
```

### Implementation

Desugar to loop + filter + push:

```
squares = for_map(range(10), \x -> x * x)
evens = for_filter(nums, \x -> x % 2 == 0)
```

Requires closures in C codegen.

### Impact

Nice for data processing, less relevant for compiler internals.

---

## 12. Prioritized Implementation Order

| # | Construct | Effort | Impact | Dependencies |
|---|-----------|--------|--------|-------------|
| 1 | **Guards** | Low-Medium | Very High | Parser + MIR lowering |
| 2 | **Record update** | Medium | High | Parser + type info or runtime copy |
| 3 | **Partial application (`_`)** | Low-Medium | High | Parser (full use needs closures) |
| 4 | **Let destructuring** | Low | High | Parser only |
| 5 | **Closures in C codegen** | Medium-High | Very High | C codegen + MIR |
| 6 | **Where clauses** | Medium | Medium | Parser + name resolution |
| 7 | **Multi-way cond** | Very Low | Medium | Parser only (or guards) |
| 8 | **Type annotations** | Low-Medium | Medium | Parser + type checker |
| 9 | **String formatting** | Low | Low | Context-aware inference |
| 10 | **Array comprehensions** | Medium | Low | Closures |
| — | ~~If-then-else~~ | — | — | Deferred (imperative, use guards instead) |

Guards alone would transform the readability of nearly every function in glyph.glyph. Record update and partial application sugar would eliminate the two most common sources of boilerplate. Closures would unlock the functional programming model that the language's design implies but can't currently deliver in the self-hosted compiler.

---

## 13. Token Impact

Glyph is designed for minimal BPE token count. Each construct should reduce token count, not increase it:

| Construct | Before (tokens) | After (tokens) | Savings |
|-----------|-----------------|----------------|---------|
| Guard | `_ -> match x > 0 \n true -> e` (~10) | `n \| n > 0 -> e` (~6) | ~40% |
| Record update | 14-field reconstruction (~50) | `{ctx \| fn_name: "x"}` (~6) | ~88% |
| Partial app | `\x -> x + 1` (~6) | `_ + 1` (~3) | ~50% |
| Destructure | `r = e \n x = r.x \n y = r.y` (~12) | `{x, y} = e` (~5) | ~58% |
| Closure | 6-line recursive loop (~30) | `filter(arr, \x -> x > 0)` (~8) | ~73% |

All proposed constructs reduce token count, aligning with Glyph's LLM-native design goal.
