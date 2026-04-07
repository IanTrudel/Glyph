# Glyph Type System — Solutions

**Date:** 2026-04-03
**Companion to:** `glyph-type-system-revisited.md` (assessment)

This document presents concrete, implementable solutions organized into three tiers: immediate bug fixes, targeted improvements, and architectural enhancements. Each solution includes the specific definitions to modify, implementation sketches in Glyph, estimated effort, and what it catches.

---

## Tier 1 — Bug Fixes (High Priority, Low Effort)

These are implementation bugs where the type checker has the right logic but fails to report or propagate errors. Fixing them requires minimal code changes and zero architectural redesign.

### 1.1 Report Occurs Check Failures

**Problem:** `subst_bind` returns -1 when the occurs check fires (infinite type like `t = t → t`), but never pushes an error to `eng.errors`. Every caller that ignores the -1 silently accepts infinite types.

**What it catches:** `x(x)`, `f(f(f))`, self-application, any recursive type construction.

**Definition to modify:** `subst_bind`

**Current code:**
```
match ty_contains_var(eng, resolved, root)
  true -> -1
  _ ->
    glyph_array_set(eng.bindings, root, resolved)
    0
```

**Fixed code:**
```
match ty_contains_var(eng, resolved, root)
  true ->
    glyph_array_push(eng.errors, "infinite type (occurs check)")
    -1
  _ ->
    glyph_array_set(eng.bindings, root, resolved)
    0
```

**Effort:** 1 definition, +1 line. Tokens: ~5 additional.

**Risk:** Near zero. The -1 return path already exists and callers already handle (or ignore) it. Adding the error push only makes the failure visible — it doesn't change control flow.

---

### 1.2 Propagate Unification Failures in apply_args

**Problem:** `apply_args` calls `unify(eng, fn_ty, expected_fn)` but discards the return value. When unification fails (type mismatch at a call site), the error is already pushed to `eng.errors` by `unify_tags`, so this is mostly harmless for type mismatches — but for occurs check failures (§1.1) the error is only returned, not pushed. After fixing §1.1, this becomes less critical but is still bad practice.

More importantly, when unification fails, `apply_args` continues with `subst_walk(eng, ret)` where `ret` is a fresh variable that was never successfully unified. This produces an unresolved type variable that propagates garbage downstream.

**Definition to modify:** `apply_args`

**Current code:**
```
apply_args eng ast fn_ty args i =
  match i >= glyph_array_len(args)
    true -> fn_ty
    _ ->
      arg_ty = infer_expr(eng, ast, args[i])
      ret = subst_fresh(eng)
      expected_fn = mk_tfn(arg_ty, ret, eng.ty_pool)
      unify(eng, fn_ty, expected_fn)
      resolved_ret = subst_walk(eng, ret)
      apply_args(eng, ast, resolved_ret, args, i + 1)
```

**Fixed code:**
```
apply_args eng ast fn_ty args i =
  match i >= glyph_array_len(args)
    true -> fn_ty
    _ ->
      arg_ty = infer_expr(eng, ast, args[i])
      ret = subst_fresh(eng)
      expected_fn = mk_tfn(arg_ty, ret, eng.ty_pool)
      r = unify(eng, fn_ty, expected_fn)
      match r < 0
        true -> mk_terror(eng.ty_pool)
        _ ->
          resolved_ret = subst_walk(eng, ret)
          apply_args(eng, ast, resolved_ret, args, i + 1)
```

**Effort:** 1 definition, +3 lines.

**Impact:** When a call site has a type mismatch, inference stops propagating garbage types and returns Error instead. Error absorption (`ty_error` unifies with anything) prevents cascading downstream errors.

---

### 1.3 Propagate Unification Failures in Remaining Call Sites

**Problem:** 11 more definitions call `unify` without checking the return. Most of these are in expression inference where the unification failure is already reported via `eng.errors` by `unify_tags`. The fix here is defense-in-depth: stop the inference from continuing with inconsistent state after a failure.

**Definitions to modify and their fix patterns:**

#### Pattern A: "Unify then return a fresh/existing type" — add error guard

These call sites unify a constraint then return a pre-computed result type. If unification fails, the result type is still valid (it's just unconstrained). The fix is to return `mk_terror` on failure so error absorption takes over.

**`infer_field_access`** — unifies record type constraint:
```
-- Current:  unify(eng, rec_ty, record_ty)
-- Fixed:
r = unify(eng, rec_ty, record_ty)
match r < 0
  true -> mk_terror(eng.ty_pool)
  _ ->
    tc_constrain_field(eng, node.sval, result_ty)
    result_ty
```

**`infer_pipe`** — unifies function constraint:
```
-- Current:  unify(eng, right_ty, fn_ty)
-- Fixed:
r = unify(eng, right_ty, fn_ty)
match r < 0
  true -> mk_terror(eng.ty_pool)
  _ -> ret
```

**`infer_compose`** — two unifications:
```
-- Current:
unify(eng, left_ty, mk_tfn(a, b, eng.ty_pool))
unify(eng, right_ty, mk_tfn(b, c, eng.ty_pool))
mk_tfn(a, c, eng.ty_pool)
-- Fixed:
r1 = unify(eng, left_ty, mk_tfn(a, b, eng.ty_pool))
match r1 < 0
  true -> mk_terror(eng.ty_pool)
  _ ->
    r2 = unify(eng, right_ty, mk_tfn(b, c, eng.ty_pool))
    match r2 < 0
      true -> mk_terror(eng.ty_pool)
      _ -> mk_tfn(a, c, eng.ty_pool)
```

**`infer_propagate`** and **`infer_unwrap`** — unify Optional constraint:
```
-- These are identical functions. In each:
-- Current:  unify(eng, inner, mk_topt(elem, eng.ty_pool))
-- Fixed:
r = unify(eng, inner, mk_topt(elem, eng.ty_pool))
match r < 0
  true -> mk_terror(eng.ty_pool)
  _ -> elem
```

**`infer_index`** — unifies array constraint:
```
-- Current:
unify(eng, container, arr_ty)
unify(eng, idx, mk_tint(eng.ty_pool))
elem
-- Fixed:
r = unify(eng, container, arr_ty)
match r < 0
  true -> mk_terror(eng.ty_pool)
  _ ->
    unify(eng, idx, mk_tint(eng.ty_pool))
    elem
```

#### Pattern B: "Unify in a loop" — early-return on failure

**`infer_match_arms_ty`** — unifies pattern and body types per arm:
```
-- Current:
unify(eng, scrutinee_ty, pat_ty)
body_ty = infer_expr(eng, ast, arms[i + 1])
unify(eng, result_ty, body_ty)
-- Fixed:
r1 = unify(eng, scrutinee_ty, pat_ty)
body_ty = infer_expr(eng, ast, arms[i + 1])
r2 = unify(eng, result_ty, body_ty)
-- (continue regardless — errors already in eng.errors, and we want to check ALL arms)
```

For match arms, continuing on failure is actually correct — we want to check all arms and report all errors, not stop at the first mismatch.

**`unify_array_elems`** and **`infer_map_pairs`** — same reasoning, continue to check all elements.

**`infer_binary`** — the fallback `unify(eng, lt, rt)`:
```
-- Current:
match r < 0
  true ->
    unify(eng, lt, rt)
    lt
-- Fixed:
match r < 0
  true ->
    r2 = unify(eng, lt, rt)
    match r2 < 0
      true -> mk_terror(eng.ty_pool)
      _ -> lt
```

**Effort:** ~11 definitions, +2-5 lines each. Total: ~35-50 additional tokens.

---

### 1.4 Fix Bool/Int Coercion in unify_tags

**Problem:** `unify_tags` contains a deliberate coercion:
```
_ ? (at == ty_int() || at == ty_bool()) && (bt == ty_int() || bt == ty_bool()) -> 0
```

This makes `42 + true` pass. The spec says "no implicit coercion" (Section 10.2). The Rust checker has the same coercion, so both compilers are wrong relative to the spec.

**Decision needed:** Is this intentional? At the C ABI level Bool is `long long` 0/1, same as Int. If this coercion is desired for pragmatic reasons, document it in the spec. If not, remove it.

**To remove — modify `unify_tags`:**
```
-- Remove this line:
_ ? (at == ty_int() || at == ty_bool()) && (bt == ty_int() || bt == ty_bool()) -> 0
-- The default mismatch arm will catch Bool vs Int
```

**To keep — update the spec:** Change Section 10.2 to note that Bool and Int are interchangeable at the type level.

**Impact of removal:** May cause new type warnings in existing code that uses boolean expressions in arithmetic (e.g., `count + (x > 0)` where comparison result is added as 0/1). Grep for patterns like this before removing.

**Effort:** 1 definition, -1 line (to remove) or 0 code changes (to document).

---

## Tier 2 — Targeted Improvements (Medium Priority, Medium Effort)

These improve the checker's coverage without architectural changes. Each is a self-contained enhancement.

### 2.1 Constructor Arity Checking

**Problem:** `Some(1, 2, 3)` passes because `infer_call` special-cases `Some` only for arity 1 vs 0, and for other constructors via `is_known_ctor` returns a fresh type variable without checking arity at all.

**Definition to modify:** `infer_call` (the `Some` branch and the `is_known_ctor` branch)

**Fix for Some:**
```
-- Current:
match glyph_str_eq(cname, "Some")
  true ->
    match glyph_array_len(node.ns) == 1
      true ->
        arg_ty = infer_expr(eng, ast, node.ns[0])
        mk_topt(arg_ty, eng.ty_pool)
      _ -> mk_topt(subst_fresh(eng), eng.ty_pool)
-- Fixed:
match glyph_str_eq(cname, "Some")
  true ->
    match glyph_array_len(node.ns) == 1
      true ->
        arg_ty = infer_expr(eng, ast, node.ns[0])
        mk_topt(arg_ty, eng.ty_pool)
      _ ->
        glyph_array_push(eng.errors, "Some expects 1 argument, got " + itos(glyph_array_len(node.ns)))
        mk_topt(subst_fresh(eng), eng.ty_pool)
```

**Fix for known constructors:** Requires looking up the constructor's type from the environment (registered via `tc_register_ctors`) and checking arity against the function type's parameter count. This is more involved — the constructor type `Variant : A → B → EnumType` has a known arity from its type.

**New helper function:** `fn_type_arity`
```
fn_type_arity eng ti =
  w = subst_walk(eng, ti)
  match w < 0
    true -> 0
    _ ->
      node = pool_get(eng, w)
      match node.tag == ty_fn()
        true -> 1 + fn_type_arity(eng, node.n2)
        _ -> 0
```

Then in the `is_known_ctor` branch of `infer_call`:
```
match is_known_ctor(cname)
  true ->
    ctor_ty = env_lookup(eng, cname)
    match ctor_ty >= 0
      true ->
        expected = fn_type_arity(eng, ctor_ty)
        actual = glyph_array_len(node.ns)
        match expected > 0 && actual != expected
          true ->
            glyph_array_push(eng.errors, cname + " expects " + itos(expected) + " argument(s), got " + itos(actual))
            mk_terror(eng.ty_pool)
          _ -> apply_args(eng, ast, instantiate(eng, ctor_ty), node.ns, 0)
      _ -> subst_fresh(eng)
  _ -> ...
```

**Effort:** 1 new definition (`fn_type_arity`, ~40 tokens), 1 modified definition (`infer_call`, ~+60 tokens).

**What it catches:** `Some(1, 2, 3)`, `Ok(a, b)`, wrong-arity enum variant construction.

---

### 2.2 Local Let-Polymorphism

**Problem:** `infer_stmt` for `st_let` doesn't call `generalize`, so local bindings are monomorphic:
```
f =
  id = \x -> x     -- id : t0 → t0 (monomorphic)
  id(42)            -- t0 = I, id locked to I → I
  id("hello")       -- ERROR: can't unify I with S
```

The Rust checker does generalize at let bindings.

**Definition to modify:** `infer_stmt`

**Current code:**
```
_ ? k == st_let() ->
  val_ty = infer_expr(eng, ast, node.n1)
  env_insert(eng, node.sval, val_ty)
  val_ty
```

**Fixed code (full generalization):**
```
_ ? k == st_let() ->
  val_ty = infer_expr(eng, ast, node.n1)
  gen_ty = generalize(eng, val_ty)
  env_insert(eng, node.sval, gen_ty)
  val_ty
```

**Performance concern:** `generalize` calls `tc_collect_fv` (walks the type) and `env_free_vars` (walks the entire environment). For functions with many let bindings, this could be slow.

**Compromise — only generalize lambda bindings:**
```
_ ? k == st_let() ->
  val_ty = infer_expr(eng, ast, node.n1)
  rhs_node = ast[node.n1]
  gen_ty = match rhs_node.kind == ex_lambda()
    true -> generalize(eng, val_ty)
    _ -> val_ty
  env_insert(eng, node.sval, gen_ty)
  val_ty
```

This only generalizes `id = \x -> x` (where polymorphism matters), not `x = 42` (where it doesn't). Avoids the performance cost for the common case.

**Effort:** 1 definition, +3-5 lines.

**What it catches:** Local polymorphic helper functions used at multiple types.

---

### 2.3 Assignment (:=) Type Checking

**Problem:** Assignment `:=` is parsed and lowered to MIR but not type-checked. The type checker's `infer_stmt` doesn't have a branch for it.

**Investigation:** The self-hosted parser handles `:=` in `parse_stmt_expr` (which falls through from `parse_stmt`). Looking at the MIR lowering, `lower_stmt` dispatches `st_let` and `st_let_destr` but has no `st_assign` case — it falls to `_ -> mk_op_unit()`. So assignment isn't even lowered as a statement in the current type checker path.

Looking at how assignment works in the actual compiler: it's parsed as part of expression handling (parsed as `ex_assign` or similar), not as a statement kind. So `infer_stmt`'s `st_expr` branch handles it by calling `infer_expr`, which likely doesn't have an assignment case either.

**What's needed:** When the parser emits an assignment node, the type checker should:
1. Look up the LHS variable's current type in the environment
2. Infer the RHS type
3. Unify them

This requires understanding how assignment is represented in the AST. Since it's not in the statement dispatch, it's likely handled as an expression with `ex_assign` kind or similar.

**New function:** `infer_assign`
```
infer_assign eng ast node =
  lhs_name = node.sval
  lhs_ty = env_lookup(eng, lhs_name)
  rhs_ty = infer_expr(eng, ast, node.n1)
  match lhs_ty >= 0
    true ->
      unify(eng, lhs_ty, rhs_ty)
      mk_tvoid(eng.ty_pool)
    _ ->
      env_insert(eng, lhs_name, rhs_ty)
      mk_tvoid(eng.ty_pool)
```

Then add it to the appropriate dispatch in `infer_expr_core` or `infer_expr2`.

**Effort:** 1 new definition (~50 tokens), 1 modified dispatch definition (+1 arm).

**Risk:** Need to verify the AST node kind for assignments. May require parser investigation.

---

### 2.4 Remove Int/Float Implicit Coercion

**Problem:** `binop_arith` accepts Int+Float→Float and Float+Int→Float, contradicting the spec's "no implicit coercion" rule.

**Definition to modify:** `binop_arith`

**Current code (Int branch):**
```
match lt == ty_int()
  true -> match rt == ty_int()
    true -> mk_tint(eng.ty_pool)
    _ -> match rt == ty_float()
      true -> mk_ty_prim(ty_float(), eng.ty_pool)   ← remove
      _ -> -1
```

**Fixed code:**
```
match lt == ty_int()
  true -> match rt == ty_int()
    true -> mk_tint(eng.ty_pool)
    _ -> -1
```

Same change for the Float+Int path. Remove the `rt == ty_int()` sub-branch from the `lt == ty_float()` branch.

**Also modify:** `binop_arith_num` — same pattern, same fix.

**Impact analysis needed:** Before making this change, run:
```sql
-- Find functions that might mix int and float arithmetic
SELECT name FROM def WHERE kind='fn' AND gen=1
AND body LIKE '%int_to_float%' OR body LIKE '%float_to_int%'
```

Code that already uses explicit conversion functions (`int_to_float`, `float_to_int`) is fine. Code that relies on implicit mixing will break.

**Effort:** 2 definitions, -2 lines each.

---

### 2.5 Negative Test Suite

**Problem:** All 28 existing type checker tests are positive (verify well-typed programs produce correct types). No tests verify that ill-typed programs are rejected. This means regressions that make the checker more permissive go undetected.

**New test definitions to add:**

```
test_ty_reject_occurs =
  eng = mk_engine()
  src = "f x = x(x)"
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  infer_fn_def(eng, ast, r.node)
  assert(glyph_array_len(eng.errors) > 0)

test_ty_reject_arr_plus_int =
  eng = mk_engine()
  register_builtins(eng)
  src = "f = [1, 2] + 5"
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  infer_fn_def(eng, ast, r.node)
  assert(glyph_array_len(eng.errors) > 0)

test_ty_reject_fn_int_mismatch =
  eng = mk_engine()
  register_builtins(eng)
  src = "f x = x(42) + x"
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  infer_fn_def(eng, ast, r.node)
  assert(glyph_array_len(eng.errors) > 0)

test_ty_reject_str_len_int =
  eng = mk_engine()
  register_builtins(eng)
  src = "f = glyph_str_len(42)"
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  infer_fn_def(eng, ast, r.node)
  assert(glyph_array_len(eng.errors) > 0)

test_ty_reject_compose_mismatch =
  eng = mk_engine()
  register_builtins(eng)
  src = "f = glyph_str_len >> glyph_str_len"
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  infer_fn_def(eng, ast, r.node)
  assert(glyph_array_len(eng.errors) > 0)

test_ty_reject_pipe_mismatch =
  eng = mk_engine()
  register_builtins(eng)
  src = r"""f x = x |> glyph_str_len |> glyph_str_len"""
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  infer_fn_def(eng, ast, r.node)
  assert(glyph_array_len(eng.errors) > 0)
```

**Effort:** 6 new test definitions, ~80-100 tokens each.

**Critical point:** `test_ty_reject_occurs` will FAIL until §1.1 is implemented. This is the canary test — it should be added first, observe it fail, then fix §1.1 and watch it pass.

---

### 2.6 Guard Expression Type Constraint

**Problem:** `infer_match_arms_ty` infers the guard expression but doesn't constrain it to Bool:
```
match guard_idx >= 0
  true ->
    guard_ty = infer_expr(eng, ast, guard_idx)
    0                                               ← should unify with Bool
  _ -> 0
```

**Fix:**
```
match guard_idx >= 0
  true ->
    guard_ty = infer_expr(eng, ast, guard_idx)
    unify(eng, guard_ty, mk_tbool(eng.ty_pool))
    0
  _ -> 0
```

**Effort:** 1 definition, +1 line.

**What it catches:** `x ? x + 1 -> body` where the guard is an integer expression, not a boolean.

---

## Tier 3 — Architectural Enhancements (Lower Priority, Higher Effort)

These are larger changes that improve the checker's architecture. They're worth doing but require more planning and testing.

### 3.1 Default Unresolved Types Post-Inference

**Idea borrowed from the micropass paper's "default_numbers" pass.** After all inference is complete, sweep unresolved type variables and default them to Int. This provides deterministic behavior instead of leaving unresolved variables as garbage.

**New function:** `default_unresolved_vars`
```
default_unresolved_vars eng =
  n = deref(eng.next_var)
  int_ty = mk_tint(eng.ty_pool)
  dur_loop(eng, 0, n, int_ty)

dur_loop eng i n int_ty =
  match i >= n
    true -> 0
    _ ->
      root = subst_find(eng.parent, i)
      b = eng.bindings[root]
      match b < 0
        true ->
          glyph_array_set(eng.bindings, root, int_ty)
          dur_loop(eng, i + 1, n, int_ty)
        _ -> dur_loop(eng, i + 1, n, int_ty)
```

**Call site:** After `tc_infer_loop_sigs` returns, before `resolve_tmap`:
```
-- In build_program, after tc_infer_loop_sigs:
default_unresolved_vars(eng)
```

**Impact:** Unresolved operand types in `+`, `-`, etc. will default to Int, producing correct integer code generation instead of undefined behavior. This is the same behavior the C codegen already assumes (everything is `long long`), so it's making an implicit assumption explicit.

**Effort:** 2 new definitions (~60 tokens), 1 modified call site.

---

### 3.2 Exhaustiveness Checking

Pattern exhaustiveness is the largest missing feature. It prevents the "non-exhaustive match → silent fallthrough" class of bugs.

**Architecture:**

1. After type inference (when scrutinee types are known), walk each match expression
2. Collect the set of patterns present
3. Compare against the scrutinee type's value space
4. Report missing cases

**Required knowledge:**
- For enums: all variants (available from `build_variant_map`)
- For Bool: `{true, false}`
- For Int/Str: infinite — require wildcard or variable pattern
- For Optional: `{Some(_), None}`
- For Result: `{Ok(_), Err(_)}`

**Implementation sketch:**

```
-- New type tag constants for pattern coverage
pc_total = 0     -- wildcard/variable covers everything
pc_bool = 1      -- set of {true, false}
pc_int_set = 2   -- set of specific ints (needs wildcard)
pc_ctor_set = 3  -- set of constructor names

-- Check one match expression
check_exhaust eng ast match_node scrut_ty =
  arms = match_node.ns
  pats = collect_match_pats(ast, arms, 0, [])
  w = subst_walk(eng, scrut_ty)
  snode = pool_get(eng, w)
  match has_wildcard_pat(pats, 0)
    true -> 0                    -- wildcard covers all
    _ ->
      match snode.tag == ty_bool()
        true -> check_bool_exhaust(pats)
        _ -> match snode.tag == ty_named()
          true -> check_enum_exhaust(eng, snode.sval, pats)
          _ -> match snode.tag == ty_opt()
            true -> check_opt_exhaust(pats)
            _ -> 0               -- Int/Str: can't check without wildcard requirement

check_bool_exhaust pats =
  has_true = pats_contain_bool(pats, true, 0)
  has_false = pats_contain_bool(pats, false, 0)
  match has_true && has_false
    true -> 0
    _ -> -1   -- "non-exhaustive: missing true/false"

check_enum_exhaust eng type_name pats =
  -- look up all variants for type_name
  -- check each variant appears in pats
  -- report missing ones
  ...
```

**Effort:** ~8-12 new definitions (~400-600 tokens). Requires access to the variant map at inference time, which is already passed through `tc_register_ctors`.

**What it catches:** Missing match arms for booleans, enum variants, optionals, results.

---

### 3.3 Better Error Messages

Current error messages are cryptic:
```
type mismatch: I vs S @309/310
```

The `@309/310` are pool indices, meaningless to users. Improving this requires threading source location information into the type checker.

**Approach A: Pool index → AST node → source position**

The tmap already maps AST node indices to type pool indices. If we reverse this (or tag error messages with AST node indices instead of pool indices), we can map back to source positions via the AST node's `pos` field.

**Modify `unify_tags` error reporting:**
```
-- Current:
err = "type mismatch: " + tc_type_detail(eng, na) + " vs " + tc_type_detail(eng, nb) + " @" + itos(a1) + "/" + itos(b1)
-- Improved:
err = "type mismatch: expected " + tc_type_detail(eng, na) + ", got " + tc_type_detail(eng, nb)
```

Drop the pool indices entirely (they were for debugging). The function name prefix from `tc_prefix_errors` already provides location context.

**Approach B: Thread source positions through inference**

More invasive: pass source line/column through the inference functions. Each `infer_*` function would need access to the AST node's position. This is a larger refactor but produces errors like:
```
type mismatch in foo at line 5: expected S, got I
```

**Effort:** Approach A: 1 definition, minimal. Approach B: ~20+ definitions modified to thread position.

---

### 3.4 Stricter Constructor Inference

**Problem:** `infer_call` for type identifiers (constructors) has weak type inference. Known constructors like `Ok`, `Err`, `Left`, `Right` get fresh type variables (no constraint at all). Unknown constructors get an error.

**Better approach:** When constructors are registered via `tc_register_ctors`, they get function types in the environment (e.g., `Ok : ∀a. a → !a`). The `infer_call` path for type identifiers should use these registered types, not special-case each constructor.

**Modified `infer_call` — unified constructor path:**
```
match callee_node.kind == ex_type_ident()
  true ->
    cname = callee_node.sval
    ctor_ty = env_lookup(eng, cname)
    match ctor_ty >= 0
      true ->
        inst_ty = instantiate(eng, ctor_ty)
        apply_args(eng, ast, inst_ty, node.ns, 0)
      _ ->
        match glyph_str_eq(cname, "None")
          true -> mk_topt(subst_fresh(eng), eng.ty_pool)
          _ ->
            glyph_array_push(eng.errors, "unknown constructor: " + cname)
            mk_terror(eng.ty_pool)
  _ -> ...
```

This removes the `is_known_ctor` special case and the `Some` special case. ALL constructors go through environment lookup → instantiate → apply_args, which automatically handles arity (apply_args will fail if wrong number of arguments).

**Prerequisite:** `tc_register_ctors` must register `Some` as `∀a. a → ?a` and `None` as a nullary type.

**Effort:** 1 modified definition (`infer_call`, rewrite constructor branch ~150 tokens), verification that `tc_register_ctors` covers all needed constructors.

---

### 3.5 Selective Micropass Borrowing: Struct Field Resolution

The micropass paper's separation of "known_record_fields" (propagate known field types forward) and "resolve_records" (infer record type from field names backward) maps well onto Glyph's existing `tc_constrain_field` / `tc_constrain_struct` system.

Currently these constraints are applied inline during inference. A cleaner architecture would be:

1. **During inference:** Record field accesses create row-polymorphic constraints normally (already done)
2. **After inference, before tmap resolution:** Run a dedicated "struct resolution" pass that matches inferred record types against the struct map and refines field types

This is already partially implemented via the struct map. The improvement would be making it a distinct, re-runnable pass that can catch more cases (currently it only fires during `infer_field_access` and `infer_record`, not for records that arrive through unification).

**Effort:** ~3-4 new definitions, ~150 tokens. Low risk since it's additive.

---

## Implementation Order

The recommended implementation order balances impact against risk:

### Phase 1: Safety Net (Day 1)
1. **§1.1** — Occurs check error reporting (1 def, ~5 tokens)
2. **§2.5** — Add `test_ty_reject_occurs` (verify it fails, then passes after §1.1)
3. **§1.2** — Fix `apply_args` (1 def, ~15 tokens)
4. **§2.6** — Guard type constraint (1 def, +1 line)

After Phase 1: the checker catches infinite types, function call mismatches propagate Error types, and guards must be boolean. Run full test suite to verify no regressions.

### Phase 2: Error Propagation (Day 2)
5. **§1.3** — Fix remaining 11 unify call sites (~11 defs, ~50 tokens)
6. **§2.5** — Add remaining negative tests (~5 tests, ~500 tokens)
7. **§3.3A** — Remove pool indices from error messages (1 def, minimal)

After Phase 2: all unification failures are propagated or reported. Negative tests guard against future regressions. Error messages are human-readable.

### Phase 3: Coverage Expansion (Week 1)
8. **§2.1** — Constructor arity checking (1 new + 1 modified def)
9. **§2.2** — Local let-polymorphism for lambdas (1 def)
10. **§3.1** — Default unresolved vars post-inference (2 new defs)
11. **§3.4** — Unified constructor inference path (1 def rewrite)

After Phase 3: constructors are arity-checked, local lambdas are polymorphic, unresolved types default to Int, and all constructors go through the type system uniformly.

### Phase 4: Spec Alignment (Week 2)
12. **§1.4** — Decide on Bool/Int coercion (document or remove)
13. **§2.4** — Remove Int/Float coercion (2 defs)
14. **§2.3** — Assignment type checking (1 new + 1 modified def)

### Phase 5: Major Features (Future)
15. **§3.2** — Exhaustiveness checking (~10 new defs)
16. **§3.3B** — Source position threading (~20 defs modified)
17. **§3.5** — Post-inference struct resolution pass (~4 new defs)

---

## Estimated Total Effort

| Phase | New Defs | Modified Defs | New Tokens | Risk |
|-------|----------|---------------|------------|------|
| 1 | 1 test | 3 | ~25 | Very low |
| 2 | 5 tests | 12 | ~550 | Low |
| 3 | 4 | 3 | ~300 | Low-medium |
| 4 | 1 | 3 | ~60 | Medium (may break code) |
| 5 | ~12 | ~20 | ~1000 | Medium-high |
| **Total** | **~23** | **~41** | **~1,935** | — |

The first two phases (~15 definitions, ~575 tokens) fix the critical bugs and establish the negative test suite. This is the minimum viable improvement. Everything beyond is incremental value.

---

## What This Does NOT Address

- **Typeclass/trait system** — The parser accepts trait definitions but they're not type-checked. This is a major feature, not a fix.
- **Higher-kinded types** — Not in scope for a semi-overhaul.
- **Dependent types / refinement types** — Academic interest only for Glyph's use case.
- **Full micropass architecture** — As discussed, the ROI doesn't justify the rewrite for an LLM-oriented language. The selective borrowing in §3.1 and §3.5 captures the useful ideas.
- **Making type errors block compilation** — This requires all of the above to be implemented first, plus a thorough pass over all example programs to fix genuine type errors. It's a policy decision, not a technical one.
