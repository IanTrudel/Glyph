# The closure limitation, explained

There are actually **two distinct limitations**:

## 1. Single-expression lambda body (parser limitation)

Both compilers parse `\params -> body` where `body` is a single expression via `parse_expr`. This means:

**Works:**
```
\x -> x + 1
\x -> match x / true -> 1 / _ -> 0
\x -> f(g(x))
```

**Doesn't work:**
```
\x ->
  y = transform(x)
  validate(y)
  y
```

The workaround is a helper function:
```
helper x =
  y = transform(x)
  validate(y)
  y

result = map(\x -> helper(x), items)
```

**How hard to fix:** Relatively easy. The parser already has `parse_body()` (line 128 of parser.rs) which checks for `Indent` → parse block, else → parse expression. The lambda parser just needs to call `parse_body()` instead of `parse_expr()` after `->`. The self-hosted parser needs the same change in `parse_lambda_params`. MIR lowering already handles `ExprKind::Block` as a lambda body — `lower_lambda` calls `lower_expr` which dispatches to block lowering. The AST change is minimal: lambda body goes from `Expr` to `Body` (or just use `ExprKind::Block` which already exists).

## 2. Closure calling convention (runtime limitation)

Raw function references (`&fn_name`) crash when stored in arrays/records because the closure calling convention always dereferences `f[0]` to get the function pointer. This means:

**Crashes:**
```
fns = [&transform, &validate]    -- raw fn refs in array
fns[0](x)                        -- boom: tries f[0] on a raw pointer
```

**Works:**
```
fns = [\x -> transform(x), \x -> validate(x)]   -- proper closures
fns[0](x)                        -- fine: closure struct has fn_ptr at [0]
```

For stdlib, this means you can't do `map(transform, items)` — you must write `map(\x -> transform(x), items)`. The extra lambda wrapper is required.

## Is it worth fixing?

### Multi-line lambdas: **Yes, high value, low cost**

| Factor | Assessment |
|--------|-----------|
| Parser change | ~20 lines in each compiler |
| MIR/codegen change | Zero — already handles blocks |
| Test coverage | Add 1-2 tests |
| Risk | Low — additive, doesn't break existing code |
| Benefit for stdlib | Enables complex `fold`, `filter`, `flat_map` bodies inline |
| Benefit for users | Major quality-of-life for all Glyph programs |

This would unlock patterns like:
```
filter(\x ->
  score = compute_score(x)
  score > threshold
, items)

fold(\acc item ->
  key = extract_key(item)
  _ = hm_set(acc, key, item)
  acc
, hm_new(), items)
```

### Raw function reference fix: **Lower priority, higher cost**

Would require changing the calling convention or adding a "function type" tag to distinguish raw fn pointers from closure structs at call sites. This is a deeper change across MIR lowering and codegen. The `\x -> f(x)` wrapper is a workable (if verbose) pattern.

## Recommendation

Fix multi-line lambdas. It's a small parser change with outsized benefit — it unblocks stdlib adoption for more complex patterns and improves the language for all users. The closure calling convention issue is a separate, harder problem that the lambda-wrapper pattern already mitigates.
