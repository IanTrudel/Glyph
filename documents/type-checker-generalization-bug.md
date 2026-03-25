# Type Checker Generalization Bug (BUG-007)

**Status:** Workaround in place. Root cause unresolved.
**Severity:** High — prevents polymorphic functions in the self-hosted compiler
**Affected component:** Self-hosted type checker (`tc_collect_fv`, `generalize`, `pool_get`)
**Workaround:** Monomorphic specializations of `arr_extend` (commit 2855685, March 2026)

## Summary

The self-hosted Hindley-Milner type checker fails to generalize polymorphic
functions. A function like `arr_extend dst src i = ...` that should infer as
`∀a. [a] -> [a] -> I -> I` instead gets monomorphized to the concrete type of
its first call site (e.g., `[S] -> [S] -> I -> I`). Subsequent call sites with
different element types produce spurious `type mismatch` errors.

The bug appeared after row polymorphism was added to the self-hosted type
checker. The root cause is unconfirmed but has two leading theories:

1. **Row polymorphism implementation bug** — The newly added `unify_records`,
   `unify_row_vars`, and `fields_not_in` functions may be creating type pool
   entries (rest variables, open record types) that confuse `tc_collect_fv`'s
   walk, or polluting the substitution environment so that free type variables
   appear already bound during generalization.

2. **C codegen field offset disambiguation bug** — The compiled type checker may
   misread `TyNode.tag` values in the type pool due to record type ambiguity,
   leading `tc_collect_fv` to misidentify type variables as concrete types and
   report 0 free variables.

Theory 1 is the more likely cause given the temporal correlation: the bug did
not exist before row polymorphism was added.

## Reproduction

A minimal 4-definition program confirms the bug:

```
-- /tmp/tc_repro.glyph (4 defs)

arr_extend dst src i =
  match i >= glyph_array_len(src)
    true -> 0
    _ ->
      array_push(dst, src[i])
      arr_extend(dst, src, i + 1)

use_strings u =
  a = ["hello", "world"]
  b = []
  arr_extend(b, a, 0)
  b

use_arrays u =
  a = [[1, 2], [3, 4]]
  b = []
  arr_extend(b, a, 0)
  b

main = use_strings(0)
```

Building this with the self-hosted compiler produces:
```
type mismatch: [S] vs S
```
or similar, because `arr_extend` is inferred as `[S] -> [S] -> I -> I` from its
first use (`use_strings`), and the second use (`use_arrays` with `[[I]]`)
conflicts.

## Technical Analysis

### How HM Generalization Works in the Self-Hosted Compiler

The self-hosted type checker uses a standard two-pass approach:

1. **Pre-registration** (`tc_pre_register`): Before inference, every function
   gets a monomorphic type variable `t_i -> t_j -> ... -> t_k` registered in
   the environment. This allows mutual recursion.

2. **Inference** (`infer_fn_def`): Each function body is inferred. The
   pre-registered type unifies with the inferred type.

3. **Generalization** (`generalize`): After inference, free type variables in the
   function's type are universally quantified to produce a polymorphic type
   `∀a b. [a] -> [a] -> I -> I`.

4. **Instantiation** (`instantiate`): At each call site, the `∀` type is
   instantiated with fresh type variables.

### The Generalization Pipeline

```
generalize(eng, ty):
  1. free  = tc_collect_fv(eng, ty, [], seen1)    -- collect free vars in ty
  2. efv   = env_free_vars(eng, seen2)             -- collect free vars in env
  3. to_bind = subtract_vars_bs(free, seen2, 0)    -- free \ efv = vars to generalize
  4. if to_bind is empty → return ty (monomorphic)
  5. else → wrap in ∀ via mk_tforall
```

### Where It Breaks

For `arr_extend`, step 1 returns `free = []` (no free variables found), even
though the inferred type contains type variables for the array element type.
With no free variables, `to_bind` is empty, and the function stays monomorphic.

The `tc_collect_fv` function walks the type pool starting from the function's
inferred type index. It calls `pool_get` to read each `TyNode`, then checks
`node.tag == ty_var()` to identify type variables:

```
tc_collect_fv eng ti acc seen =
  w = subst_walk(eng, ti)
  ...
  node = pool_get(eng, w)
  tag = node.tag
  match tag == ty_var()       -- ← THIS CHECK FAILS SPURIOUSLY
    true -> ...collect variable...
    _ -> match tag == ty_fn()
      true -> ...recurse into param/return...
      ...
```

### The `pool_get` Hint Workaround

The `pool_get` function exists specifically to work around field offset
ambiguity (see BUG-005):

```
pool_get eng idx =
  node = eng.ty_pool[idx]
  _ = node.tag              -- force .tag access for codegen disambiguation
  node
```

The `_ = node.tag` line hints to the C codegen which record type this is,
helping `find_best_type` choose the right field layout. However, this hint
may not be sufficient in all contexts.

### The Field Offset Disambiguation Problem

The C codegen resolves record field offsets using a heuristic:

1. **`build_type_reg`** scans all MIR to collect known record types (sets of
   field names).
2. **`coll_local_acc`** pre-scans each function to find all field names accessed
   on each local variable.
3. **`find_best_type`** picks the record type with the most fields that contains
   all the accessed field names.

The problem is **type ambiguity**. Multiple record types share field names:

| Type     | Fields (alphabetical)                        | Size |
|----------|----------------------------------------------|------|
| TyNode   | `{n1, n2, ns, sval, tag}`                    | 5    |
| AstNode  | `{ival, kind, n1, n2, n3, ns, sval}`         | 7    |
| JNode    | `{items, keys, nval, sval, tag}`             | 5    |

TyNode and AstNode share fields `{n1, n2, ns, sval}`. TyNode and JNode share
`{sval, tag}`. The "prefer largest" heuristic picks AstNode (7 fields) when a
local accesses `{n1, ns, sval, tag}` — but TyNode is the correct type.

Field offsets are computed from alphabetical order. For `.tag`:
- **TyNode**: fields are `{n1, n2, ns, sval, tag}` → `.tag` is at offset 4 (index 4 × 8 = byte 32)
- **AstNode**: fields are `{ival, kind, n1, n2, n3, ns, sval}` → no `.tag` field
- **JNode**: fields are `{items, keys, nval, sval, tag}` → `.tag` is at offset 4 (byte 32)

If the codegen picks the wrong type for a local holding a TyNode, it reads the
wrong memory offset for `.tag`. A type variable node (`tag = 15`) could be read
as a concrete type (e.g., `tag = 1` for Int), causing `tc_collect_fv` to skip
it entirely.

### Why `pool_get` Might Not Be Enough

The `_ = node.tag` hint in `pool_get` tells the codegen that the returned node
has a `.tag` field. But the codegen's `coll_local_acc` scans field accesses
**per local variable**, not per function call chain. If the caller assigns
`pool_get`'s return value to a local that also has other field accesses from
different record types, the disambiguation can still fail.

Additionally, the hint only works within `pool_get`'s own codegen scope. The
**caller** must independently disambiguate its own locals. The memory notes for
BUG-005 document that 7 specific functions needed explicit `_ = node.tag` hints
after calling `pool_get`.

`tc_collect_fv` does access `.tag` on its `node` local, so in theory it should
disambiguate correctly. The failure may involve:

- An intermediate function in the call chain that doesn't preserve the hint
- `subst_walk` or `subst_find` returning a value through a code path that
  confuses the field offset scanner
- A subtlety in how `coll_local_acc` handles reused local names across match
  arms

### Evidence

**For Theory 1 (row polymorphism bug):**

1. **Temporal correlation** — The generalization failures appeared after row
   polymorphism was added to the self-hosted type checker. Before that change,
   the type checker did not produce these errors.

2. **Row variables create implicit bindings** — `unify_row_vars` calls
   `subst_bind` on rest variables, creating record types that may accidentally
   bind type variables that should remain free. If a record type is involved
   anywhere in the inference chain (e.g., the type checker engine record itself),
   rest variables could capture element type variables.

3. **`fields_not_in` walks the pool** — This function reads TyNodes to compare
   field names, potentially triggering substitution side effects or pool growth
   that shifts indices.

**For Theory 2 (field offset bug):**

4. **The bug reproduces with zero string literals** — `arr_extend` uses only
   `glyph_array_len`, `array_push`, `>=`, and recursion. There is no reason for
   the element type variable to be bound to `S` or any concrete type.

5. **The bug reproduces in a minimal 4-function program** — ruling out
   environmental pollution from the compiler's 1300+ other functions.

6. **`env_nullify` doesn't help** — Neutralizing pre-registered environment
   entries before generalization has no effect. The pre-registered type variable
   is not the problem; the problem is in `tc_collect_fv`'s walk.

7. **The Rust compiler generalizes correctly** — The same HM algorithm, when
   compiled by Cranelift (which uses proper type-safe IR), generalizes
   `arr_extend` as polymorphic. Only the C-codegen-compiled version fails.

**Distinguishing the theories:** Point 7 suggests Theory 2, since the same
algorithm works when compiled differently. However, the Rust and self-hosted
type checkers are not identical — the self-hosted version has row polymorphism
code that the Rust version implements differently. A definitive test would be to
revert the row polymorphism additions and check if generalization works again.

## Current Workaround

Three monomorphic specializations replace the polymorphic `arr_extend`:

```
arr_extend_s dst src i =     -- for [S] arrays
  match i >= glyph_array_len(src)
    true -> 0
    _ ->
      s = src[i]
      _ = glyph_str_len(s)      -- type hint: element is S
      glyph_array_push(dst, s)
      arr_extend_s(dst, src, i + 1)

arr_extend_row dst src i =   -- for [[S]] arrays
  match i >= glyph_array_len(src)
    true -> 0
    _ ->
      row = src[i]
      _ = glyph_array_len(row)  -- type hint: element is [_]
      glyph_array_push(dst, row)
      arr_extend_row(dst, src, i + 1)

arr_extend_gtd dst src i =   -- for [{gtd_name:S ...}] record arrays
  match i >= glyph_array_len(src)
    true -> 0
    _ ->
      gtd = src[i]
      _ = gtd.gtd_name          -- type hint: element is record
      glyph_array_push(dst, gtd)
      arr_extend_gtd(dst, src, i + 1)
```

The generic `arr_extend` remains in the codebase but has zero callers.

## Proper Fix Options

### Option A: Debug and Fix the Row Polymorphism Implementation

The most likely fix. The row polymorphism code (`unify_records`, `unify_row_vars`,
`fields_not_in`, `unify_fields_against`) was recently added and is the probable
cause. Potential issues to investigate:

- **Rest variable leakage**: `unify_row_vars` creates fresh variables and binds
  rest variables via `subst_bind`. If these bindings accidentally capture type
  variables from unrelated types (e.g., array element variables), generalization
  sees them as bound rather than free.
- **Eager record unification**: When the type checker engine record (with 8+
  fields) is passed through functions, `unify_records` may fire on it and bind
  variables that should remain parametric.
- **`fields_not_in` pool walks**: This function reads TyNodes to compare field
  names. If it triggers during inference of `arr_extend`, it could create
  spurious substitution bindings.

**Diagnostic approach**: Temporarily disable row polymorphism (make
`unify_records` a no-op that returns 0) and test whether `arr_extend`
generalizes correctly. If it does, bisect the row poly code to find the
specific interaction.

**Estimated scope:** 1-3 definitions to fix once the specific issue is identified

### Option B: Fix the Field Offset Disambiguation

Make `find_best_type` / `resolve_fld_off` type-aware instead of relying on the
"prefer largest" heuristic. Possible approaches:

- **Propagate MIR type info** into the field offset resolution phase. If the MIR
  knows a local is of type `TyNode`, use that directly instead of heuristic
  matching.
- **Unique field prefixes**: TyNode fields already partially use this (the type
  checker engine uses `env_names`, `env_types`, etc.). Renaming TyNode fields to
  `ty_tag`, `ty_n1`, `ty_n2` would eliminate ambiguity with AstNode. This is
  invasive (~96 type checker defs reference these fields).
- **Struct codegen (gen=2)**: The gen=2 C codegen uses `typedef struct` with
  proper type information. Moving the type checker to gen=2 would eliminate field
  offset ambiguity entirely, but gen=2 is currently used only for a subset of
  definitions.

### Option C: Fix `tc_collect_fv` Specifically

Add aggressive disambiguation hints throughout the type checker walk functions.
Every function that reads from `eng.ty_pool` must ensure that its local holding
a TyNode is properly disambiguated. This is fragile and has been attempted
(BUG-005's `pool_get` hint), but may need more comprehensive application.

### Option D: Move Type Checker to gen=2

Migrate all ~96 type checker definitions to gen=2 struct codegen. This gives
each record type a proper C struct with correct field offsets, eliminating the
entire class of disambiguation bugs. This is the most robust fix but requires
significant migration effort.

### Option E: Named Type Annotation System

Add optional type annotations to the language that the C codegen can use for
field offset resolution, rather than relying on inference and heuristics. This
is a language-level change and the most invasive option.

## Scope of Impact

Currently, only `arr_extend` is known to be affected. Other polymorphic
functions in the compiler either:
- Don't need generalization (they're used at only one type)
- Use type-specific operations that constrain inference enough to avoid the bug
- Happen to have the right field access patterns for disambiguation

Any future polymorphic utility function added to the compiler risks hitting the
same bug. The workaround pattern (create monomorphic specializations with type
hints) is reliable but defeats the purpose of parametric polymorphism.

## Related Issues

- **BUG-005** (`find_best_type` JNode/AstNode ambiguity): Same root cause,
  different symptom. Fixed with `_ = node.tag` hints in 7 functions.
- **Bool/Int unification**: The Rust compiler reports 99 `B vs I` type errors
  because it distinguishes Bool and Int. The self-hosted compiler treats them as
  compatible via `int_like` in `unify_tags`. These are separate from BUG-007.
- **Enum variant unification**: `color_tag` and `light_tag` produce variant
  mismatch errors in the Rust compiler. Also separate.

## Files and Definitions

Key definitions involved:
- `tc_collect_fv` — walks type pool to find free variables
- `pool_get` — type pool accessor with `.tag` disambiguation hint
- `generalize` — wraps free variables in `∀` quantifier
- `subst_walk` — follows substitution chain to find representative type
- `find_best_type` — heuristic record type matcher (the likely culprit)
- `mk_ty` — constructs TyNode records `{tag, n1, n2, ns, sval}`
- `arr_extend_s`, `arr_extend_row`, `arr_extend_gtd` — workaround specializations
- `load_attached_loop`, `read_lib_generics_loop` — callers updated to use specializations
