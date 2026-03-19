# New Language Constructs for Glyph

**Version:** 0.2 (2026-03-02)
**Scope:** Self-hosted compiler (glyph.glyph). Constructs prioritized by impact on the existing ~1,000+ definitions.

---

## 1. Guards in Match Arms — IMPLEMENTED

**Status: COMPLETE** (both Rust and self-hosted compilers)

Uses `?` token instead of `|` (avoids ambiguity with or-patterns).

### Syntax

```
match x
  1 -> "one"
  n ? n > 0 -> "positive"
  _ -> "negative"
```

```
pattern ? guard_expr -> body
```

The guard `? guard_expr` is an optional boolean expression after the pattern. The arm matches only if both the pattern matches AND the guard evaluates to true. Pattern variables are in scope within the guard.

### Implementation Details

- **Rust compiler**: `guard: Option<Expr>` on `MatchArm`, `lower_arm_body_guarded` helper in MIR lowering
- **Self-hosted compiler**: stride-3 arms arrays `[pat, body, guard]` (guard=-1 for no guard), `lower_guard_body` helper. 1 new def + 8 modified defs
- MIR lowering: guard evaluated after pattern match; if false, falls through to next arm
- Or-patterns (`1 | 2 | 3 -> body`) also implemented — `|` is the or-pattern separator, `?` is the guard separator

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

## 5. Let Destructuring — IMPLEMENTED

**Status: COMPLETE** (both Rust and self-hosted compilers)

### Syntax

```
{name, age} = get_person()
```

```
{field1, field2, ...} = expr
```

Each `field` becomes a local variable bound to `expr.field`.

### Implementation Details

- **Parser lookahead**: `{` + ident + (`,`|`}`) disambiguates from record literals (which require `field: value` with colon)
- **Rust compiler**: `StmtKind::LetDestructure(Vec<String>, Expr)` in ast.rs, `parse_let_destructure` in parser.rs, arms in `lower_stmt` and `walk_free_vars` in lower.rs, plus arms in infer.rs and resolve.rs
- **Self-hosted compiler**: `st_let_destr=203`, new `parse_destr_fields`/`pdf_loop` for field parsing, `lower_let_destr`/`lld_loop` for MIR emission, `parse_stmt_expr` (refactored original parse_stmt logic)
- **MIR lowering**: evaluates RHS once into temp local `_d`, then emits `Rvalue::Field` for each field name + binds to scope
- Rename syntax (`{name: n, age: a} = expr`) not yet implemented (v1 is shorthand only)

---

## 6. Closures in C Codegen — IMPLEMENTED

**Status: COMPLETE** (both Rust/Cranelift and self-hosted C codegen)

### Syntax

```
positives = filter(nums, \x -> x > 0)
names = map(people, .name)
total = fold(nums, 0, \acc x -> acc + x)
```

### Implementation Details

- **Lambda lifting**: free-variable capture analysis identifies variables from enclosing scope
- **Heap-allocated closure environments**: `{fn_ptr, capture1, capture2, ...}` allocated via `glyph_alloc`
- **Uniform calling convention**: closure pointer as hidden first argument; non-closure functions ignore it
- **Indirect calling**: call through function pointer in closure record
- **C codegen**: `cg_make_closure` emits allocation + capture storage, `cg_closure_call` emits indirect call cast
- **6 self-hosted test definitions**: test_closure_basic, test_closure_capture, test_closure_as_arg, test_closure_nested, test_closure_multi_cap, test_closure_mir
- See `memory/closures.md` for detailed implementation notes

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

## 10. String Formatting — PARTIALLY IMPLEMENTED

**Status: PARTIAL** — type-aware auto-coercion for int and float in string interpolation

### Syntax

```
"got {n} results"    -- n:I auto-converts via int_to_str
"pi is {x}"         -- x:F auto-converts via float_to_str
```

### Implementation Details

- MIR lowering for `ex_str_interp` checks the inferred type of each interpolated expression
- Integer expressions automatically wrapped in `int_to_str` call
- Float expressions automatically wrapped in `float_to_str` call
- Implicit int-to-float coercion also added for mixed arithmetic (`3.14 + 1` works)
- Explicit `int_to_str()`/`float_to_str()` calls still work and are still needed when type inference can't determine the type

### Remaining

- Bool auto-coercion not yet implemented
- Custom `to_str` for user types not yet supported

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

## 12. Implementation Status

| # | Construct | Status | Notes |
|---|-----------|--------|-------|
| 1 | **Guards** | DONE | Uses `?` token (not `|`). Or-patterns also implemented. |
| 2 | **Record update** | TODO | Medium effort. Needs type info at lowering time. |
| 3 | **Partial application (`_`)** | TODO | Low-medium effort. Closures now available. |
| 4 | **Let destructuring** | DONE | `{x, y} = expr`. Rename syntax not yet implemented. |
| 5 | **Closures in C codegen** | DONE | Full lambda lifting, heap-allocated environments, indirect calls. |
| 6 | **Where clauses** | TODO | Medium effort. Can use closure-based approach now. |
| 7 | **Multi-way cond** | TODO | Very low effort. Pure parser desugar. |
| 8 | **Type annotations** | TODO | Low-medium effort. Parser + type checker. |
| 9 | **String formatting** | PARTIAL | Int and float auto-coercion in interpolation. Bool not yet. |
| 10 | **Array comprehensions** | TODO | Medium effort. Closures now available. |
| — | ~~If-then-else~~ | DEFERRED | Use guards instead. |

### Remaining priorities

Record update and partial application sugar would eliminate the two most common sources of boilerplate. Multi-way cond is very low effort and would clean up dispatch chains. With closures now working, array comprehensions and partial application are unblocked.

---

## 13. Token Impact

Glyph is designed for minimal BPE token count. Each construct should reduce token count, not increase it:

| Construct | Before (tokens) | After (tokens) | Savings | Status |
|-----------|-----------------|----------------|---------|--------|
| Guard | `_ -> match x > 0 \n true -> e` (~10) | `n ? n > 0 -> e` (~6) | ~40% | DONE |
| Record update | 14-field reconstruction (~50) | `{ctx \| fn_name: "x"}` (~6) | ~88% | TODO |
| Partial app | `\x -> x + 1` (~6) | `_ + 1` (~3) | ~50% | TODO |
| Destructure | `r = e \n x = r.x \n y = r.y` (~12) | `{x, y} = e` (~5) | ~58% | DONE |
| Closure | 6-line recursive loop (~30) | `filter(arr, \x -> x > 0)` (~8) | ~73% | DONE |

All proposed constructs reduce token count, aligning with Glyph's LLM-native design goal.
