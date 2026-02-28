# Context-Aware Type Inference for Code Generation

**Version:** 0.1 (2026-02-25)
**Scope:** Self-hosted compiler only (glyph.glyph). Wiring type inference results into MIR lowering and C codegen.

---

## 1. Motivation

The self-hosted compiler has a type checker (`tc_*`, ~119 definitions) and a MIR lowerer (`lower_*`, ~108 definitions) that operate independently. The type checker runs in `glyph check` as advisory-only, while `glyph build` skips it entirely. This disconnect creates four known failure classes:

### 1.1 String Operator Heuristic

`lower_binary` calls `is_str_op(ctx, left, right)` which checks `op_type` — a simple lookup into `ctx.local_types`. If neither operand is a known string (e.g., both are function parameters), `+` generates integer addition instead of `str_concat`:

```
combine a b = a + b
-- type checker knows: a:S, b:S → S (from call site)
-- MIR lowering sees: a=unknown, b=unknown → integer add (WRONG)
```

### 1.2 Field Offset Guessing

`lower_field` emits `mir_emit_field(ctx, dest, base, 0, field_name)` with offset 0. The post-processing pass `fix_all_field_offsets` then uses `find_best_type` to guess which struct type a local belongs to based on field names accessed. This "largest match" heuristic broke for `.sval` (BUG-005: AstNode vs JNode) and required manual `_ = node.tag` workarounds.

### 1.3 Silent Non-Exhaustive Match

`lower_match_arms` falls through to the next basic block when all arms are exhausted without a wildcard. No warning, no trap. The type checker knows the scrutinee's type and could verify exhaustiveness.

### 1.4 Arity Mismatches

Calling a function with wrong number of arguments produces garbage at runtime. The type checker infers function types as `param1 -> param2 -> ... -> ret` and detects arity errors, but this information is discarded before codegen.

---

## 2. Current Architecture

### 2.1 Type Checker Pipeline (`glyph check`)

```
sources = read_fn_defs_gen(db, gen)
parsed = parse_all_fns(sources, 0)
eng = mk_engine()
register_builtins(eng)
tc_pre_register(eng, parsed, 0)     -- skeletal types for all fns
tc_infer_all(eng, parsed, 0)        -- infer each fn body
tc_report_errors(eng)               -- print warnings
```

After `tc_infer_all`, the engine contains:
- **`eng.ty_pool`** — arena of TyNode records, types referenced by index
- **`eng.bindings`** / `eng.parent` — union-find with resolved type variable bindings
- **`eng.env_names`** / `eng.env_types` — top-level function type environment
- **`eng.errors`** — accumulated error messages

Per-expression types exist in the pool but there is **no AST-node-to-type mapping**. `infer_expr` returns a type index for each expression, but the caller just unifies it without storing the association.

### 2.2 Build Pipeline (`glyph build`)

```
parsed = parse_all_fns(sources, 0)
za_fns = build_za_fns(parsed, 0, [])
mirs = compile_fns_parsed(parsed, 0, za_fns)
fix_all_field_offsets(mirs)
fix_extern_calls(mirs, externs)
tco_optimize(mirs)
c_code = cg_program(mirs, struct_map)
```

`compile_fn_parsed(pf, za_fns)` calls `lower_fn_def(pf.pf_ast, pf.pf_fn_idx, za_fns)` directly. No type inference. The lowerer uses `local_types` (parallel array of integers, 0=unknown, 1=int, 4=str, 5=bool) as a crude substitute — set from literals and `str_concat` results, propagated nowhere.

### 2.3 The Gap

```
                  ┌──────────────────────┐
                  │    Type Checker       │
                  │  (per-expression      │
  glyph check ──▶│   resolved types,     │──▶ print warnings
                  │   row polymorphism,   │    (discard engine)
                  │   function signatures)│
                  └──────────────────────┘

                  ┌──────────────────────┐
                  │    MIR Lowering       │
                  │  (heuristic string    │
  glyph build ──▶│   detection, guessed  │──▶ C codegen
                  │   field offsets, no   │
                  │   exhaustiveness)     │
                  └──────────────────────┘
```

These two systems need to be connected:

```
                  ┌──────────────────────┐
                  │    Type Checker       │
                  │  (per-expression      │
                  │   resolved types,     │──┐
                  │   row polymorphism,   │  │  TypeContext
                  │   function signatures)│  │  (per-fn type map)
                  └──────────────────────┘  │
                                            ▼
                  ┌──────────────────────┐
                  │    MIR Lowering       │
  glyph build ──▶│  (exact string ops,   │──▶ C codegen
                  │   typed field offsets,│
                  │   exhaustiveness,     │
                  │   arity checking)     │
                  └──────────────────────┘
```

---

## 3. Design: Per-Expression Type Map

### 3.1 The Core Idea

During type inference, build a **node-to-type map**: an array indexed by AST node index, storing the resolved type pool index for each expression.

```
infer_expr_mapped eng ast ni tmap =
  node = ast[ni]
  ty = <...existing inference logic...>
  glyph_array_set(tmap, ni, ty)
  ty
```

After inference, `tmap[i]` gives the type of AST node `i`. The MIR lowerer can query this map when making codegen decisions.

### 3.2 TypeContext Record

```
TypeContext = {tc_pool: [TyNode], tc_map: [I], tc_env: Engine}
```

Fields:
- **`tc_pool`** — the engine's `ty_pool` (shared, not copied)
- **`tc_map`** — array of length `len(ast)`, maps AST node index → type pool index. `-1` means no type info (inference failed or node not visited).
- **`tc_env`** — the full engine, for `subst_resolve` and variable lookups

### 3.3 Null Context

When type inference fails or is unavailable, the lowerer uses a **null context** — `tc_map` is empty, all lookups return `-1`, and the lowerer falls back to current heuristics. This ensures backward compatibility.

```
mk_null_tctx =
  {tc_pool: [], tc_map: [], tc_env: mk_engine()}
```

---

## 4. Inference Changes

### 4.1 Modified `infer_fn_def`

Current:
```
infer_fn_def eng ast fi =
  node = ast[fi]
  env_push(eng)
  param_types = infer_fn_params(eng, ast, node.ns, 0)
  body_ty = infer_expr(eng, ast, node.n1)
  env_pop(eng)
  build_fn_type(eng, param_types, body_ty, 0)
```

New — takes and populates a type map:
```
infer_fn_def_mapped eng ast fi tmap =
  node = ast[fi]
  env_push(eng)
  param_types = infer_fn_params_mapped(eng, ast, node.ns, 0, tmap)
  body_ty = infer_expr_mapped(eng, ast, node.n1, tmap)
  env_pop(eng)
  fn_ty = build_fn_type(eng, param_types, body_ty, 0)
  glyph_array_set(tmap, fi, fn_ty)
  fn_ty
```

### 4.2 Modified `infer_expr` → `infer_expr_mapped`

The key change: every `infer_expr` call stores its result in `tmap` before returning. The mapped version wraps the original:

```
infer_expr_mapped eng ast ni tmap =
  ty = infer_expr(eng, ast, ni)
  resolved = subst_resolve(eng, ty)
  match ni >= 0
    true ->
      match ni < glyph_array_len(tmap)
        true -> glyph_array_set(tmap, ni, resolved)
        _ -> 0
    _ -> 0
  ty
```

This is minimally invasive — `infer_expr` itself doesn't change, just the entry point. The resolved type is stored, not the raw type variable, so subsequent lookups don't need to walk the substitution.

### 4.3 `tc_infer_all_mapped`

New version that produces a TypeContext per function:

```
tc_infer_all_mapped eng parsed i =
  match i >= glyph_array_len(parsed)
    true -> []
    _ ->
      pf = parsed[i]
      tctx = match pf.pf_fn_idx >= 0
        true ->
          tmap = init_tmap(glyph_array_len(pf.pf_ast))
          ty = infer_fn_def_mapped(eng, pf.pf_ast, pf.pf_fn_idx, tmap)
          {tc_pool: eng.ty_pool, tc_map: tmap, tc_env: eng}
        _ -> mk_null_tctx()
      rest = tc_infer_all_mapped(eng, parsed, i + 1)
      glyph_array_push(rest, tctx)
      rest
```

Returns an array of TypeContext records, one per parsed function, parallel to the `parsed` array.

### 4.4 Helper: `init_tmap`

```
init_tmap n =
  arr = []
  fill_tmap(arr, n, 0)
  arr

fill_tmap arr n i =
  match i >= n
    true -> 0
    _ ->
      glyph_array_push(arr, 0 - 1)
      fill_tmap(arr, n, i + 1)
```

Creates an array of `-1` values (no type assigned yet).

---

## 5. MIR Lowering Changes

### 5.1 Extended Lowering State

Add `tctx` to the lowering context:

```
mk_mir_lower_typed fn_name za_fns tctx =
  {block_stmts: [],
   block_terms: [],
   local_names: [],
   local_types: [],
   cur_block: [0],
   nxt_local: [0],
   nxt_block: [0],
   var_names: [],
   var_locals: [],
   var_marks: [],
   fn_name: fn_name,
   fn_params: [],
   fn_entry: [0],
   za_fns: za_fns,
   tctx: tctx}
```

The only addition is the `tctx` field. When no type info is available, pass `mk_null_tctx()`.

### 5.2 Type Query Function

```
tctx_type_of ctx ast_idx =
  tctx = ctx.tctx
  tmap = tctx.tc_map
  match glyph_array_len(tmap) == 0
    true -> 0 - 1
    _ -> match ast_idx < 0
      true -> 0 - 1
      _ -> match ast_idx >= glyph_array_len(tmap)
        true -> 0 - 1
        _ -> tmap[ast_idx]

tctx_is_str ctx ast_idx =
  ti = tctx_type_of(ctx, ast_idx)
  match ti < 0
    true -> 0
    _ ->
      pool = ctx.tctx.tc_pool
      match glyph_array_len(pool) == 0
        true -> 0
        _ ->
          tag = pool[ti].tag
          match tag == ty_str()
            true -> 1
            _ -> 0
```

### 5.3 Fix: String Operator Detection

Current `is_str_op` checks `op_type(ctx, left, right)` — only knows about string literals and propagated types.

New approach in `lower_binary`: check the type map first, fall back to current heuristic.

```
lower_binary ctx ast node =
  left = lower_expr(ctx, ast, node.n1)
  right = lower_expr(ctx, ast, node.n2)
  is_str = match tctx_is_str(ctx, node.n1) == 1
    true -> 1
    _ -> match tctx_is_str(ctx, node.n2) == 1
      true -> 1
      _ -> is_str_op(ctx, left, right)
  match is_str == 1
    true -> lower_str_binop(ctx, node.ival, left, right)
    _ ->
      mop = lower_binop(node.ival)
      dest = mir_alloc_local(ctx, "")
      mir_emit_binop(ctx, dest, mop, left, right)
      mk_op_local(dest)
```

The type map check is **before** `is_str_op` (current heuristic). If the inferencer resolved either operand as `ty_str`, we know it's a string operation — even when both are function parameters.

### 5.4 Fix: Field Offset Resolution

Current `lower_field` emits offset 0 and relies on `fix_all_field_offsets` to guess the type. With the type map:

```
lower_field ctx ast node =
  base = lower_expr(ctx, ast, node.n1)
  dest = mir_alloc_local(ctx, "")
  base_type_name = tctx_record_name(ctx, node.n1)
  mir_emit_field_typed(ctx, dest, base, 0, node.sval, base_type_name)
  mk_op_local(dest)
```

Where `tctx_record_name` resolves the base expression's type to a named struct (if available):

```
tctx_record_name ctx ast_idx =
  ti = tctx_type_of(ctx, ast_idx)
  match ti < 0
    true -> ""
    _ ->
      pool = ctx.tctx.tc_pool
      match glyph_array_len(pool) == 0
        true -> ""
        _ ->
          node = pool[ti]
          match node.tag == ty_record()
            true -> resolve_record_name(pool, node)
            _ -> ""
```

When the type map provides a record type name, `fix_all_field_offsets` can use it directly instead of guessing. The existing heuristic remains as fallback when `base_type_name` is empty.

### 5.5 Fix: Non-Exhaustive Match Warning

At the end of `lower_match_arms`, when `i >= array_len(arms)` and no wildcard was matched, emit a trap instead of falling through:

```
lower_match_arms ctx ast arms scrutinee result merge i =
  match i >= glyph_array_len(arms)
    true ->
      mir_terminate(ctx, mk_term_unreachable())
      0
    _ ->
      <...existing arm dispatch...>
```

This doesn't require the type checker — it's a pure control-flow fix. But with the type context available, we can also emit a diagnostic:

```
-- Optional: warn at compile time about non-exhaustive match
warn_nonexhaustive ctx ast scrutinee_idx =
  type_name = tctx_type_name(ctx, scrutinee_idx)
  eprintln(s3("warning: non-exhaustive match on ", type_name, " (no wildcard '_')"))
```

### 5.6 Fix: Arity Checking

In `lower_call`, before emitting the call, check the callee's type:

```
tctx_arity ctx callee_name =
  eng = ctx.tctx.tc_env
  match glyph_array_len(eng.env_names) == 0
    true -> 0 - 1
    _ ->
      ty = env_lookup(eng, callee_name)
      match ty < 0
        true -> 0 - 1
        _ -> count_fn_params(eng.ty_pool, subst_resolve(eng, ty))

count_fn_params pool ti =
  match ti < 0
    true -> 0
    _ ->
      node = pool[ti]
      match node.tag == ty_fn()
        true -> 1 + count_fn_params(pool, node.n2)
        _ -> 0
```

When the inferencer provides arity info, `lower_call` can emit a compile-time error instead of generating wrong code:

```
lower_call_checked ctx ast node =
  callee_name = get_callee_name(ast, node)
  n_args = glyph_array_len(node.ns)
  expected = tctx_arity(ctx, callee_name)
  match expected >= 0
    true -> match n_args == expected
      true -> lower_call(ctx, ast, node)
      _ ->
        eprintln(s5("error: ", callee_name, " takes ", itos(expected), s2(" args, given ", itos(n_args))))
        lower_call(ctx, ast, node)
    _ -> lower_call(ctx, ast, node)
```

Note: this only warns, it doesn't abort. The existing call lowering proceeds regardless. Partial application (currying) means fewer args is valid, so this check would need refinement for curried calls.

---

## 6. Pipeline Integration

### 6.1 Modified `build_program`

```
build_program sources externs output_path struct_map mode =
  parsed = parse_all_fns(sources, 0)
  za_fns = build_za_fns(parsed, 0, [])
  -- NEW: run type inference
  eng = mk_engine()
  register_builtins(eng)
  tc_pre_register(eng, parsed, 0)
  tctxs = tc_infer_all_mapped(eng, parsed, 0)
  tc_report_errors(eng)
  -- Pass type contexts to lowering
  mirs = compile_fns_parsed_typed(parsed, 0, za_fns, tctxs)
  fix_all_field_offsets(mirs)
  fix_extern_calls(mirs, externs)
  tco_optimize(mirs)
  c_code = cg_program(mirs, struct_map)
  <...rest unchanged...>
```

### 6.2 Modified `compile_fns_parsed_typed`

```
compile_fns_parsed_typed parsed i za_fns tctxs =
  match i >= glyph_array_len(parsed)
    true -> []
    _ ->
      pf = parsed[i]
      tctx = tctxs[i]
      mir = match pf.pf_fn_idx >= 0
        true -> lower_fn_def_typed(pf.pf_ast, pf.pf_fn_idx, za_fns, tctx)
        _ -> compile_fn(pf.pf_src, za_fns)
      rest = compile_fns_parsed_typed(parsed, i + 1, za_fns, tctxs)
      glyph_array_push(rest, mir)
      rest
```

### 6.3 Modified `lower_fn_def_typed`

```
lower_fn_def_typed ast fn_idx za_fns tctx =
  node = ast[fn_idx]
  ctx = mk_mir_lower_typed(node.sval, za_fns, tctx)
  entry = mir_new_block(ctx)
  glyph_array_set(ctx.fn_entry, 0, entry)
  lower_fn_params(ctx, ast, node.ns, 0)
  body_op = lower_expr(ctx, ast, node.n1)
  mir_terminate(ctx, mk_term_return(body_op))
  {fn_name: node.sval, fn_params: ctx.fn_params, fn_locals: ctx.local_names,
   fn_blocks_stmts: ctx.block_stmts, fn_blocks_terms: ctx.block_terms,
   fn_entry: entry}
```

Same as `lower_fn_def` but creates context with `mk_mir_lower_typed`. The `lower_expr` dispatch chain remains unchanged — individual cases like `lower_binary` access `ctx.tctx` when they need type info.

---

## 7. Fallback Strategy

Every type query has a fallback:

| Query | Type Info Available | Fallback (No Type Info) |
|-------|-------------------|------------------------|
| Is operand a string? | `tctx_is_str(ctx, ast_idx)` | `is_str_op(ctx, left, right)` (current heuristic) |
| What record type? | `tctx_record_name(ctx, ast_idx)` | `find_best_type` field-name matching |
| Callee arity? | `tctx_arity(ctx, name)` | No check (current behavior) |
| Match exhaustive? | Type-aware variant counting | Emit `unreachable` trap (no type needed) |

This means:
- If inference fails for a function, `mk_null_tctx()` is used and behavior is identical to today
- If inference succeeds but the type is ambiguous (unresolved variable), queries return `-1` and heuristics take over
- The compiler never crashes due to missing type info — it degrades gracefully

---

## 8. Implementation Plan

### Phase 1: Infrastructure (~6 new definitions)

| Definition | Description |
|---|---|
| `init_tmap` | Create `-1`-filled type map array |
| `fill_tmap` | Recursive helper for init_tmap |
| `mk_null_tctx` | Create empty TypeContext for fallback |
| `tctx_type_of` | Query type map by AST node index |
| `tctx_is_str` | Check if AST node has string type |
| `tctx_record_name` | Resolve record type name from type map |

### Phase 2: Inference with Mapping (~4 new/modified definitions)

| Definition | Action |
|---|---|
| `infer_expr_mapped` | NEW — wraps `infer_expr`, stores result in tmap |
| `infer_fn_def_mapped` | NEW — wraps `infer_fn_def`, passes tmap |
| `infer_fn_params_mapped` | NEW — wraps `infer_fn_params`, stores param types in tmap |
| `tc_infer_all_mapped` | NEW — produces array of TypeContext records |

### Phase 3: Lowering Integration (~5 new/modified definitions)

| Definition | Action |
|---|---|
| `mk_mir_lower_typed` | NEW — extends `mk_mir_lower` with `tctx` field |
| `lower_fn_def_typed` | NEW — wraps `lower_fn_def` with typed context |
| `compile_fns_parsed_typed` | NEW — wraps `compile_fns_parsed` with type contexts |
| `lower_binary` | MODIFY — check `tctx_is_str` before heuristic |
| `lower_match_arms` | MODIFY — emit `unreachable` trap on exhaustion |

### Phase 4: Pipeline Wiring (~2 modified definitions)

| Definition | Action |
|---|---|
| `build_program` | MODIFY — run inference, pass tctxs to lowering |
| `cmd_check` | MODIFY — unify with build pipeline (shared engine) |

### Phase 5: Enhanced Diagnostics (optional, ~4 new definitions)

| Definition | Action |
|---|---|
| `tctx_arity` | NEW — count function params from type |
| `count_fn_params` | NEW — walk ty_fn chain |
| `warn_nonexhaustive` | NEW — compile-time match warning |
| `lower_call_checked` | NEW — arity check before call lowering |

**Total: ~17 new definitions + ~4 modified definitions.**

---

## 9. What This Does NOT Change

- **C codegen** (`cg_stmt`, `cg_function2`, etc.) — unchanged. It consumes MIR, not types.
- **Post-processing passes** (`fix_all_field_offsets`, `fix_extern_calls`, `tco_optimize`) — unchanged, still run. Field offset pass becomes less critical but remains as safety net.
- **`glyph check`** — can optionally share the same engine as build, but the command itself stays.
- **MIR data structures** — no new statement kinds or operand types. The improvement is in which MIR operations are emitted, not in the MIR IR itself.
- **gen=2 struct codegen** — `build_local_types` continues working. With type info, it could become more accurate, but the existing scan+propagate+field-access tagging is already good for gen=2.

---

## 10. Risk Assessment

| Risk | Mitigation |
|---|---|
| Inference slows build | Inference is O(n) in AST size. For ~800 definitions, this is negligible compared to C compilation |
| Inference crash aborts build | Wrap `infer_fn_def_mapped` in error handling — on failure, return `mk_null_tctx()` |
| Wrong type info generates wrong code | Every type query has fallback to current behavior. Wrong type info can only improve or match status quo |
| `ty_pool` grows large | Pool is shared across all functions via the engine. One pool for the entire program, ~O(total AST nodes) |
| `tmap` per function wastes memory | Each tmap is `len(ast)` integers. For a 200-token function with ~50 AST nodes, that's 400 bytes. Entire program: ~32 KB |

---

## 11. Example: Before and After

### String Operation on Parameters

```
combine a b = a + b
main = println(combine("hello " "world"))
```

**Before (current):**
```
-- lower_binary: is_str_op checks op_type(ctx, left, right)
-- left = ok_local(0), right = ok_local(1)
-- op_type: local_types[0] = 0 (unknown), local_types[1] = 0 (unknown)
-- is_str_op returns 0 → integer addition
-- Result: garbage (pointer added to pointer)
```

**After (with type context):**
```
-- lower_binary: tctx_is_str(ctx, node.n1) checks tmap
-- tmap[n1] → ty_str (inferencer unified param `a` with "hello " at call site)
-- tctx_is_str returns 1 → str_concat
-- Result: "hello world"
```

### Field Access Without Hint

```
get_name node =
  _ = node.tag    -- BUG-005 workaround (currently needed)
  node.sval
```

**Before:** Requires `_ = node.tag` hint to disambiguate JNode from AstNode.

**After:** `tctx_record_name(ctx, node_ast_idx)` returns the inferred record type. If the call site passes a JNode, the inferencer resolves `node : {tag:I, sval:S, ...}` → JNode, and the field offset is correct without the hint.

---

## 12. Future Extensions

### 12.1 Type-Directed Match Compilation

With the scrutinee type known, match compilation can:
- Verify exhaustiveness for enum types (all variants covered)
- Generate jump tables for dense integer ranges
- Optimize string matches with hash comparison

### 12.2 Monomorphization

With full type info, generic functions like `combine a b = a + b` could be monomorphized — generating separate `combine_str` and `combine_int` versions. This eliminates runtime type dispatch entirely.

### 12.3 Type Annotations in MIR

Store resolved types on MIR locals (not just the crude `local_types` integers). This enables gen=2 C codegen to use exact struct types for ALL locals, not just those matched by the field-access heuristic.

### 12.4 Typed Error Messages

With the type map, error messages can include type context:
```
error[T005]: wrong number of arguments
 --> combine:1:5
  |
1 | combine a b = a + b
  |         ^ combine : S -> S -> S (2 params), called with 3
```
