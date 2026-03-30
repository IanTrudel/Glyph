# Glyph Compiler Bugs

## BUG-008: Float comparison codegen uses `int_to_float` conversion instead of bitcast

**Status:** Open (workaround available)
**Severity:** High — silently produces wrong results for float `<`, `<=`, `>`, `>=` comparisons
**Found:** 2026-03-30, while debugging vie vector editor anchor hit testing
**Workaround:** Use `float_to_int(d) < 8` instead of `d < 8.0` (truncate to int, then integer compare)

### Symptom

Float comparisons like `d < 8.0` always evaluate to `false` when `d` is the return value of a user-defined function (e.g., `pt_dist`). This happens even when `d` clearly holds a value less than 8.0 (verified via `println(ftoa(d))`).

### Generated C (wrong)

For Glyph source `match d < 8.0`:

```c
_46 = _glyph_f2i(8.0);            // right: bitcast 8.0 double to GVal bits  (CORRECT)
_47 = glyph_int_to_float(_24);     // left:  interpret float bits as integer,
                                    //        convert that integer to double   (WRONG)
_48 = _glyph_i2f(_47) < _glyph_i2f(_46);  // compare the two doubles
```

When `_24` holds the double bit pattern of 4.07 (~`0x4010462...`), `glyph_int_to_float` treats it as the integer `4614256656552045000` and converts to `4.614e18`. So the comparison becomes `4.614e18 < 8.0` which is always `false`.

### Expected C (correct)

```c
_46 = _glyph_f2i(8.0);            // right: bitcast 8.0 to GVal
_48 = _glyph_i2f(_24) < _glyph_i2f(_46);  // just bitcast both back to double
```

No `glyph_int_to_float` call is needed — `_24` already holds a double bit pattern.

### Root Cause Chain

The bug spans three compiler subsystems: type checker output, MIR lowering type tracking, and float coercion codegen.

#### 1. `coerce_to_float` (immediate cause) — `glyph.glyph`

```
coerce_to_float ctx op =
  ot = op_type(ctx, op)
  match ot == 3          ← only recognizes type tag 3 (ty_float)
    true -> op           ← correct: already float, return as-is
    _ ->                 ← WRONG for unknown types (0) or type vars (15)
      dest = mir_alloc_local(ctx, "")
      mir_set_lt(ctx, dest, 3)
      mir_emit_call(ctx, dest, mk_op_func("glyph_int_to_float"), [op])
      mk_op_local(dest)
```

Any operand whose tracked type is not exactly 3 gets `int_to_float` conversion. This includes:
- Type 0 (unknown/untracked) — common for function return values
- Type 15 (ty_var, unresolved type variable)
- Any other non-float type

But in a float comparison context (the caller already determined this IS a float operation because the other operand is `8.0`), an unknown-type operand is much more likely to already hold float bits than to be an integer needing conversion.

#### 2. `track_call_type` (type tracking gap) — `glyph.glyph`

```
track_call_type ctx dest callee =
  match callee.okind == ok_func_ref()
    true -> match is_str_ret_fn(callee.ostr)
      true -> mir_set_lt(ctx, dest, 4)
      _ -> match is_float_ret_fn(callee.ostr)
        true -> mir_set_lt(ctx, dest, 3)
        _ -> 0
    _ -> 0
```

After emitting a call, `track_call_type` sets the result local's type — but only for hardcoded function names:

```
is_float_ret_fn name =
  match name
    _ ? str_eq(name, "str_to_float") -> true
    _ ? str_eq(name, "int_to_float") -> true
    _ ? str_eq(name, "hm_get_float") -> true
    _ -> false
```

Only 3 functions are recognized. User-defined float-returning functions (like `pt_dist`, `math_sqrt`, `ff`, `canvas_zoom`, etc.) are not tracked. Their return locals stay at type 0.

#### 3. `lower_expr` / `lower_let` (tctx fallback) — `glyph.glyph`

`lower_expr` has a fallback that queries the type checker's per-expression type map (tctx):

```
tt = tctx_query(ctx.tctx, ni)
_ = match (tt == 3 || tt == 4) && op.okind == ok_local()
    true -> mir_set_lt(ctx, op.oval, tt)
    _ -> 0
```

And `lower_let` also tries:

```
ot = op_type(ctx, val)
tt = tctx_query(ctx.tctx, node.n1)
ty = match ot > 0
    true -> ot
    _ -> match tt > 0
        true -> tt
        _ -> 0
```

This should work IF the type checker successfully resolved the return type to `ty_float` (tag 3). But for programs with type errors (vie has 35 `tc_err` warnings), type variables may remain unresolved. The `resolve_tmap` step converts these to tag 15 (`ty_var`) or -1, neither of which is 3.

### Affected Patterns

Any float comparison where the left operand is a call result or variable bound to one:

```
d = some_float_fn(args)
match d < 8.0        -- BROKEN: d gets int_to_float'd
  true -> ...

match some_float_fn(x) < threshold   -- BROKEN for same reason
  true -> ...
```

Float arithmetic (`+`, `-`, `*`, `/`) is NOT affected — only comparisons, because comparisons go through `coerce_to_float` while arithmetic uses a different path.

### Fix Options (in order of preference)

#### Option A: Fix `coerce_to_float` to bitcast unknown types

Most surgical fix. In a float comparison context, unknown-type operands should be bitcast (not converted):

```
coerce_to_float ctx op =
  ot = op_type(ctx, op)
  match ot
    _ ? ot == 3 -> op                           -- already float
    _ ? ot == 1 ->                              -- known integer: convert
      dest = mir_alloc_local(ctx, "")
      mir_set_lt(ctx, dest, 3)
      mir_emit_call(ctx, dest, mk_op_func("glyph_int_to_float"), [op])
      mk_op_local(dest)
    _ -> op                                     -- unknown/other: assume float bits, pass through
```

The `_glyph_i2f` bitcast in the comparison expression handles the rest. This is safe because `coerce_to_float` is only called when `tctx_is_float_bin` or `is_float_op` already confirmed this is a float operation — so an unknown-type operand in that context is overwhelmingly likely to already hold float bits.

**Risk:** If a genuinely integer value reaches a float comparison (e.g., `my_int < 1.5`), it would be bitcast instead of converted. This would give wrong results. But this case should be caught by `op_type` returning 1 (int) and taking the conversion path.

#### Option B: Expand `track_call_type` with tctx fallback

Pass the call's AST node index to `track_call_type` and query tctx:

```
track_call_type ctx dest callee call_ni =
  tt = tctx_query(ctx.tctx, call_ni)
  match tt == 3
    true -> mir_set_lt(ctx, dest, 3)
    _ -> match tt == 4
      true -> mir_set_lt(ctx, dest, 4)
      _ -> match callee.okind == ok_func_ref()
        true -> match is_str_ret_fn(callee.ostr) ...
        ...
```

**Risk:** Requires signature change to `track_call_type` and `lower_call`. Only works when the type checker succeeds for that expression.

#### Option C: Remove hardcoded name list, use only tctx

Replace `is_float_ret_fn` / `is_str_ret_fn` entirely with tctx queries. This is the cleanest long-term solution but depends on the type checker always providing complete information.

#### Option D: Improve type checker resilience

Ensure that type inference for individual expressions succeeds even when other functions have type errors. Currently, 35 `tc_err` warnings during vie compilation suggest cascading failures that leave type variables unresolved.

### Recommended Approach

**Option A** for immediate fix (minimal risk, surgical change to one function), followed by **Option B** or **C** for robustness.

### Test Case

Minimal reproduction:

```
-- In a .glyph database:
my_sqrt x = 0.0 + x    -- returns float
my_dist a b = my_sqrt((a - b) * (a - b))

test_float_cmp =
  d = my_dist(3.0, 7.0)
  -- d should be 4.0
  -- with bug: d < 8.0 is false (glyph_int_to_float corrupts the value)
  assert_eq(float_to_int(d), 4)  -- sanity check d is ~4.0
  -- This is what we want to work:
  -- assert(d < 8.0)  -- currently broken
```

### References

- **Discovered in:** `examples/gtk/vie/vie.glyph`, `hit_anchor_loop` function
- **Compiler functions:** `coerce_to_float`, `track_call_type`, `is_float_ret_fn`, `lower_binary`, `tctx_is_float_bin`, `lower_let`, `lower_expr`
- **Workaround pattern:** `float_to_int(expr) < int_threshold` instead of `expr < float_threshold`
