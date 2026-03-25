# Type Checker Overhaul Plan

**Date:** 2026-03-25
**Status:** Investigation complete, Tier 1 fixes applied

## Motivation

BUG-007 revealed that the self-hosted HM type checker has fundamental consistency
gaps. The fix was small (add `subst_walk` to `inst_type`), but the fact that such
a basic invariant violation went undetected — and required monomorphic workarounds
instead — indicates systemic issues worth addressing.

## Issue Categories

### 1. Inconsistent Substitution Walking

Functions that read from `eng.ty_pool` must follow substitution chains to see
resolved types. Some do (`tc_collect_fv`, `unify`), some don't (`inst_type` before
fix). No systematic invariant enforcement exists.

**Audit needed:** Every function that calls `pool_get` or indexes `eng.ty_pool`
directly should be checked for whether it needs `subst_walk`.

**Known affected:**
- `inst_type` — FIXED (BUG-007)
- Functions relying on `pool_get` hint — fragile

### 2. Reversed Array Accumulation Pattern

Multiple functions use the recursive-push pattern that reverses arrays:
```
rest = f(items, i + 1)
glyph_array_push(rest, item)
rest
```

**Known instances to audit:**
- `parse_all_fns` — FIXED (BUG-007)
- `make_inst_map` — compensated by lookup scanning all pairs
- `extract_new_var_ids` — order may matter for forall bound var lists
- `infer_fn_params` — compensated by `build_fn_type` reverse iteration

### 3. Field Offset Disambiguation (BUG-005 class)

The gen=1 C codegen resolves `.field` offsets via heuristic (`find_best_type`).
`pool_get` exists solely to hint the codegen. 7+ functions need manual
`_ = node.tag` after `pool_get`. This is fragile and error-prone.

### 4. Pre-existing tc_err Warnings

4 `S vs I` type errors in `lower_call_args_acc` and `lower_expr2` during the
sigs pass of self-compilation. These don't prevent compilation but indicate
type inference inaccuracy.

### 5. String Operator Heuristic vs Polymorphism

The MIR lowering uses `local_types` to decide whether `==` should emit `str_eq`.
Polymorphic functions have unknown parameter types, so `==` falls back to integer
comparison. This is fundamentally incompatible with parametric polymorphism.

### 6. Bool/Int/Void Conflation

`int_like` in `unify_tags` treats Bool, Int, and Void as compatible. The Rust
compiler correctly reports 99 `B vs I` errors. The self-hosted compiler silently
unifies them.

### 7. Error Attribution

Type errors show pool indices (`@51252/317682`), not source locations. Impossible
to trace errors to specific expressions without manual debugging.

---

## Detailed Audit Results

### Substitution Walking Audit

Every function that calls `pool_get` or indexes `eng.ty_pool` was checked for
whether it follows substitution chains (via `subst_walk`) before reading the
node. Functions that receive a type index and immediately `pool_get` it without
`subst_walk` may read stale/unresolved type variables.

**Category A — Already correct (calls `subst_walk` or equivalent):**

| Function | Mechanism | Notes |
|----------|-----------|-------|
| `tc_collect_fv` | `subst_walk` at entry | Correct since original implementation |
| `subst_walk` | IS the walk function | By definition |
| `subst_resolve` | `subst_walk` at entry | Full recursive resolution |
| `unify` | `subst_walk` on both args | Correct |
| `ty_contains_var` | `subst_walk` at entry | Occurs check follows chains |
| `inst_type` | `subst_walk` at entry | FIXED in BUG-007 |
| `subst_bind` | `subst_find` on var root | Binds to root of union-find |
| `infer_index` | Uses `subst_resolve` | Resolves before reading |

**Category B — Safe (operates on structural sub-nodes, not top-level indices):**

These functions receive type indices from `node.ns` arrays (field lists, tuple
elements) where the parent function already walked the top-level type. The
sub-nodes are pool indices for fields/elements, not type variables that could
have substitution chains.

| Function | Called from | Why safe |
|----------|------------|----------|
| `tc_collect_fv_fields` | `tc_collect_fv` | Parent walked; field `n1` is structural |
| `ty_fields_contain_var` | `ty_contains_var` | Parent walked |
| `inst_type_fields` | `inst_type` | Parent walked |
| `inst_type_ns` | `inst_type` | Parent walked (tuple elements) |
| `find_field` | `unify_records` etc. | Scans field list; fields are structural |
| `fields_not_in` | `unify_records` | Set difference on field lists |
| `unify_fields_against` | `unify_records` | Unifies field types (calls `unify`, which walks) |
| `resolve_fields` | `subst_resolve`, `resolve_record2` | Calls `subst_resolve` on each field type |
| `resolve_record2` | `subst_resolve` | Calls `subst_find` on rest var, `subst_resolve` on binding |
| `extract_new_var_ids` | `instantiate` | Reads freshly-created vars from `make_inst_map` — no chains yet |

**Category C — Fragile (no `subst_walk`, correctness depends on calling context):**

| Function | Risk | Analysis |
|----------|------|----------|
| `instantiate` | **Low** | Reads `pool_get(eng, ti)` without walk. Safe IF `ti` always points directly to a `ty_forall` node (not a var that resolved to forall). Currently true because `env_insert` stores the forall index directly, but would break if env ever stored a var that was later unified to a forall. |
| `infer_ctor_pattern` | **Low** | Reads `pool_get(eng, ctor_ty_idx)` from `env_lookup`. Same assumption as `instantiate` — env stores resolved types. |
| `mono_is_poly` | **Low** | Reads `pool_get(eng, fn_ty)` from `mono_fn_ty_idx`. The fn_ty comes from tc_results, which stores resolved types from `subst_resolve`. Safe for now. |
| `mono_build_var_map` | **Low** | Reads `pool_get(eng, template_ti)` where template comes from a forall body. The body was created by `inst_type` (which now walks), so structural nodes are direct. |
| `mono_type_name` | **Low** | Reads `pool_get(eng, ti)` where `ti` comes from `subst_resolve` in callers. |
| `mono_coll_call` | **Low** | Reads `pool_get(eng, fn_ti)` from `mono_fn_ty_idx`, same as `mono_is_poly`. |
| `tc_type_detail` | **Medium** | Reads `pool_get(eng, node.n1)` and `node.n2` WITHOUT walking. Used for error messages — will show stale types if sub-types have been resolved through substitution. Produces misleading error output. |

**Summary:** No currently-triggerable bugs, but `instantiate` and
`infer_ctor_pattern` have a latent fragility: they assume `env_lookup` always
returns a direct pool index, never a variable. `tc_type_detail` produces
incorrect error messages when sub-types have substitution chains.

**Recommended defensive fix:** Add `subst_walk` to `instantiate` (1 line),
`infer_ctor_pattern` (1 line), and `tc_type_detail` (2 lines on `node.n1`/`node.n2`).
Low effort, eliminates the fragility.

---

### Reversed Array Audit

The recursive-push pattern `rest = f(xs, i+1); push(rest, x); rest` builds
arrays in reverse order (last element pushed first, becomes element 0). This
is a systematic Glyph idiom that silently reverses arrays.

**Category A — Order matters (potential bugs):**

| Function | What's reversed | Impact | Needs fix? |
|----------|----------------|--------|------------|
| `tc_infer_all` | Inferred type list | Types paired with wrong functions when zipped with `parsed` | **YES** — same class as `parse_all_fns` bug |
| `inst_type_fields` | Record field list | Fields in instantiated records have reversed order | **YES** — may cause field offset mismatch |
| `inst_type_ns` | Tuple element list | Tuple elements reversed in instantiated types | **YES** — element positions swapped |
| `resolve_tuple_elems` | Resolved tuple elements | Tuple elements reversed after resolution | **YES** — element positions swapped |
| `extract_new_var_ids` | Bound var ID list | Forall var IDs in wrong order; paired incorrectly in `make_inst_map` | **Probably safe** — `lookup_var_map` scans all pairs |
| `resolve_fields` | Resolved field list | Record fields reversed after resolution | **YES** — may cause field mismatch |

**Category B — Compensated (reversal canceled out):**

| Function | Compensation | Safe? |
|----------|-------------|-------|
| `infer_fn_params` | `build_fn_type` iterates `len-1-i` (reverse) | **YES** — double reversal = correct order |
| `infer_lambda_params_ty` | Used with `build_fn_type` or `build_lambda_fn_type` | **Needs check** — must confirm callers also reverse |
| `make_inst_map` | `lookup_var_map` scans all pairs linearly | **YES** — order irrelevant for pair lookup |

**Category C — Order doesn't matter:**

| Function | Why |
|----------|-----|
| `fields_not_in` | Set difference — order of output doesn't affect unification |
| `subtract_vars` | Set difference on variable lists |
| `subtract_vars_bs` | Set difference using bitset |

**Key finding:** `tc_infer_all` is the most dangerous — it produces a reversed
list of inferred types that gets zipped with `parsed` (which is now in correct
forward order after the BUG-007 fix). This means type `i` in tc_results
corresponds to function `N-1-i` in parsed. This hasn't caused visible failures
because `tc_infer_all` is only used in the warm pass (for generalization), and
the sigs pass re-infers everything. But it's a latent correctness bug.

`inst_type_fields` and `resolve_fields` reversing record fields is concerning
because field order determines struct layout in the C codegen. Currently safe
because gen=1 C codegen resolves fields by name (heuristic), not by position.
But would break with positional field access.

**Recommended fixes (by priority):**
1. `tc_infer_all` → forward-accumulating loop (same pattern as `paf_loop`)
2. `inst_type_fields`, `inst_type_ns`, `resolve_fields`, `resolve_tuple_elems` → forward-accumulating
3. `infer_lambda_params_ty` → verify compensation or convert to forward

---

### tc_err Root Cause

The 4 `S vs I` type errors during `tc_infer_loop_sigs` are in
`lower_call_args_acc` and `lower_expr2`.

**Investigation findings:**

`tc_infer_loop_sigs` re-infers every function with optional type signatures
(`infer_fn_def_sig`). When a function calls a polymorphic function like
`s2(a, b)` (string concat), the sigs pass:
1. Looks up `s2` in the environment → gets its generalized type `∀a. a → a → S`
2. Instantiates with fresh vars
3. Unifies the fresh vars with the call arguments

The `S vs I` errors happen because:
- In the warm pass, `s2`'s type is inferred as `S → S → S` (monomorphic, from
  actual usage). After generalization, it becomes `∀a. a → a → S`.
- In the sigs pass, when re-inferring callers, `s2` is instantiated to
  `?a → ?a → S`. If the caller passes an integer argument (e.g., from `itos()`
  return value that hasn't been resolved yet), unification binds `?a = I`.
  Then when the same call also passes a string argument, unification fails
  with `S vs I`.

This is a fundamental interaction between:
1. The **two-pass inference** design (warm generalizes, sigs re-infers with signatures)
2. **Missing type signatures** on helper functions — without signatures,
   the sigs pass re-infers from scratch, losing context from the warm pass
3. The `int_like` unification leniency (Issue #6) — during warm pass, `I` and
   `S` might both unify successfully where they shouldn't

**The errors are not in `lower_call_args_acc` or `lower_expr2` themselves** —
those function names appear in `tc_err` output because they are the functions
being *type-checked* when the error occurs, not because they contain bugs.

**Why they don't prevent compilation:** `eng_reset_errors` clears the error
list between warm and sigs passes. The sigs pass tc_err messages are printed
but the types are used as-is (partially resolved). The C codegen doesn't
depend on exact types for most operations.

**Root cause found and FIXED:** `st_let_destr` was the only AST node kind
where the `ns` field stored raw strings (field names) instead of AST node
indices. `lld_loop` treated `node.ns` elements as strings, while every other
lowering function treated them as int indices. The HM type checker unified
the AstNode record type across all usages and hit `[S] vs [I]` on the `ns`
field's element type.

**Fix:** `pdf_loop` now creates `ex_ident` AST nodes for each field name and
stores their pool indices in `ns`. `lld_loop` reads `ast[names[i]].sval` to
get the field name. This makes `st_let_destr` consistent with all other AST
node kinds.

---

## Recommended Fix Order

### Tier 1 — Low-effort, high-value (do now)

**1a. Reversed array: `tc_infer_all`** — Same bug class as `parse_all_fns`
(BUG-007). Convert to forward-accumulating loop. ~10 lines changed. Risk:
minimal (warm pass only, sigs re-infers). Eliminates a latent correctness
bug that would surface if warm-pass results were ever used directly.

**1b. Defensive `subst_walk` in `instantiate`** — 1 line: add
`w = subst_walk(eng, ti)` at entry, use `w` for `pool_get`. Eliminates
fragile assumption about env contents. Same for `infer_ctor_pattern`.

**1c. Reversed array: `inst_type_fields`, `inst_type_ns`, `resolve_fields`,
`resolve_tuple_elems`** — Convert to forward-accumulating loops. ~40 lines
total. Eliminates silent field/element reordering in type instantiation and
resolution.

### Tier 2 — Medium effort, high value (next session)

**2a. Fix `tc_type_detail`** — Add `subst_walk` on `node.n1`/`node.n2` so
error messages show resolved types instead of stale variables. Also consider
recursive detail for nested types.

**2b. ~~Eliminate tc_err warnings~~** — DONE. Root cause was `st_let_destr`
storing strings in `ns` field instead of AST node indices. Fixed in
`pdf_loop` and `lld_loop`. All `S vs I` errors eliminated.

### Tier 3 — Significant effort, architectural (planned)

**3a. Bool/Int/Void separation** — Remove `int_like` from `unify_tags`.
Expect ~99 new type errors in self-compilation (matching Rust compiler).
Each error is a real type inconsistency that needs a code fix. High effort
but fundamentally improves type safety.

**3b. Gen=2 migration for type checker** — Move 20+ type checker functions
to gen=2 struct codegen. Eliminates BUG-005 class (field offset
disambiguation). Mechanical migration (body unchanged, only `gen` column).

**3c. String operator / tmap integration** — Thread `tmap` type information
into polymorphic function bodies so `==` on strings emits `str_eq` even
inside generic functions. Requires extending MIR lowering's `local_types`
to consult `tmap` for parameter types.

### Tier 4 — Large effort, quality-of-life (future)

**4a. Source-level error attribution** — Replace pool indices in error
messages with source locations. Requires threading AST node indices through
the type checker to map type errors back to expressions.

**4b. Sigs pass type inheritance** — Instead of re-inferring from scratch,
have the sigs pass inherit resolved substitutions from the warm pass. Would
eliminate tc_err entirely and improve inference accuracy for complex
programs. Significant architectural change to the two-pass design.

---

## Change Log

**2026-03-25 — Tier 1 fixes applied (all pass 314/314 tests + 4-stage bootstrap)**

Definitions added:
- `tc_infer_all_loop` — forward-accumulating replacement for `tc_infer_all` recursion
- `itf_loop` — forward-accumulating helper for `inst_type_fields`
- `itn_loop` — forward-accumulating helper for `inst_type_ns`
- `rf_loop` — forward-accumulating helper for `resolve_fields`
- `rte_loop` — forward-accumulating helper for `resolve_tuple_elems`

Definitions modified:
- `tc_infer_all` — now delegates to `tc_infer_all_loop` (fixes reversed type list)
- `inst_type_fields` — now delegates to `itf_loop` (fixes reversed field list)
- `inst_type_ns` — now delegates to `itn_loop` (fixes reversed element list)
- `resolve_fields` — now delegates to `rf_loop` (fixes reversed field list)
- `resolve_tuple_elems` — now delegates to `rte_loop` (fixes reversed element list)
- `instantiate` — added `subst_walk` at entry (defensive, eliminates fragile assumption)
- `infer_ctor_pattern` — added `subst_walk` on `ctor_ty_idx` (defensive)
- `infer_fn_params` — forward-accumulating via `ifp_loop`
- `infer_lambda_params_ty` — forward-accumulating via `ilpt_loop`
- `build_fn_type` — forward iteration (matching new forward-order param arrays)
- `pdf_loop` — stores `ex_ident` AST node indices instead of raw strings in `ns`
- `lld_loop` — reads `ast[names[i]].sval` instead of treating `names[i]` as string
- `lower_let_destr` — passes `ast` to `lld_loop`

tc_err `S vs I` warnings: **ELIMINATED** (0 errors during build and test)
