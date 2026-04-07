# Glyph Type System — Revisited Assessment

**Date:** 2026-04-03
**Scope:** Full audit of both compilers' type checkers via code reading, MCP probing, and cross-compiler comparison.

---

## Executive Summary

Glyph has two type checker implementations — a Rust one (~2,056 lines across 8 modules) and a self-hosted one (~212 definitions, ~63k characters, ~19.7k tokens). Both implement Hindley-Milner inference with row polymorphism for records. Both are functional. Neither is sound.

The Rust checker is more rigorous: it catches 16 type errors in `glyph.glyph` that the self-hosted checker silently accepts. The self-hosted checker reports 0 errors on every database tested (`glyph.glyph`, `documentation.glyph`, `stdlib.glyph`, all examples). It achieves this zero-error count partly through genuine correctness and partly through pervasive error suppression — silent occurs check failures, Bool/Int coercion, error-type absorption, and unchecked unification return values.

The type system's primary real-world value today is not enforcement but *type-directed code generation*: the tmap (per-expression type map) produced during inference feeds into MIR lowering to resolve operator overloading (`+` as `str_concat` vs integer add vs float add). This works well and has fixed real bugs. But as a safety net against type errors, the system has fundamental gaps.

---

## 1. Architecture Overview

### 1.1 Self-Hosted Type Checker

```
mk_engine()                          ← allocate pool, env, bindings, errors
  │
register_builtins(eng)               ← ~55 runtime functions with monomorphic/polymorphic types
tc_register_externs(eng, externs)     ← extern_ table entries parsed from signature strings
tc_register_ctors(eng, vmap, smap)   ← enum constructors as functions (Variant → EnumType)
tc_set_struct_map(eng, smap)         ← named record type → C type mapping for float inference
tc_pre_register(eng, parsed)         ← all fn defs get fresh type skeletons (a₁→a₂→…→aₙ→r)
  │
tc_infer_loop_warm(eng, parsed)      ← PASS 1: infer all bodies, generalize, populate env
eng_reset_subst(eng)                 ← wipe substitution (but env types survive)
eng_reset_errors(eng)                ← discard all Pass 1 errors
  │
tc_infer_loop_sigs(eng, parsed)      ← PASS 2: infer again with sig constraints; produce tmaps
tc_report_errors(eng)                ← print Pass 2 errors as warnings
```

**Two-pass design:** The warm-up pass establishes function types in the environment so that the real pass can resolve cross-function calls. The substitution is reset between passes, so pass-1 type bindings don't leak into pass-2 — only the generalized ForAll types in the environment survive.

**Pool-based representation:** Types are nodes in a flat array (`ty_pool`), referenced by index. Each `TyNode = {tag:I, n1:I, n2:I, ns:[I], sval:S}`. Fields stored alphabetically (BTreeMap convention), tag at offset 32. Type constructors (`mk_tint`, `mk_tfn`, `mk_trecord`, etc.) push nodes and return indices.

**Union-find substitution:** Type variables have entries in parallel `parent` and `bindings` arrays. `subst_find` does path-compressed root finding. `subst_walk` resolves a type index through the substitution — if it's a variable, check its binding. `subst_resolve` (via `sr_inner`) recursively resolves compound types with a depth limit of 30.

### 1.2 Rust Type Checker

Clean algebraic data type representation (`Type` enum with `Fn`, `Array`, `Record(RowType)`, `Var`, `ForAll`, etc.). Union-find with rank-based merging. Full occurs check. Proper error accumulation with source spans. ~2,056 lines.

Key structural differences from self-hosted:
- Uses `BTreeMap<String, Type>` for record fields (vs flat pool arrays)
- Proper `RowType { fields, rest: Option<TypeVarId> }` (vs n1=rest_var, ns=field_indices)
- Error types carry `Span` for location reporting
- `generalize` in `TypeEnv` checks environment free vars correctly
- `is_ground` optimization skips cloning for fully-resolved types

### 1.3 Integration Into Build Pipeline

The type checker runs during `glyph build` for all programs. The build pipeline (`build_program`) runs the full two-pass inference and produces `tc_results` — an array of `{fn_tmap, fn_ty, fn_ty_raw, fn_ty_snames}` records per function. The `fn_tmap` is a per-AST-node array of resolved type tags (integers: 1=int, 3=float, 4=str, etc.).

MIR lowering consults the tmap via `tctx_is_float_bin` and `tctx_is_str_bin` to resolve operator overloading. If the tmap says either operand of `+` is float (tag 3), it emits float add. If either is string (tag 4), it emits `str_concat`. This replaced fragile name-based heuristics and fixed real bugs (string parameters not being recognized as strings).

The `glyph check` command uses the same pipeline but without codegen — just parse, infer, report. The MCP `check_all` tool runs the same path.

---

## 2. What Works

### 2.1 Core HM Inference ✓

Basic Hindley-Milner inference is correct for the common cases:

- **Literals:** `42` → Int, `3.14` → Float, `"hello"` → Str, `true` → Bool
- **Function types:** `f x = x + 1` infers `I → I`
- **Curried application:** `f(1)(2)` correctly peels arguments
- **Let bindings:** `x = 42; x + 1` correctly propagates Int through the binding

Tested: `test_ty_infer_int`, `test_ty_infer_str`, `test_ty_infer_binary`, `test_ty_deep`, `test_ty_block_call` — all pass.

### 2.2 Let-Polymorphism (Generalization) ✓

Implemented and working at the function level. After inferring each function's type, `generalize` / `generalize_raw` collects free type variables not appearing in the environment, creates fresh copies, and wraps in `ForAll`. At call sites, `instantiate` unwraps the `ForAll` with fresh variables.

```
-- generalize: inferred t0 → t0, t0 not in env → ∀t0. t0 → t0
-- instantiate: ∀t0. t0 → t0 → t1 → t1 (fresh t1)
```

**Limitation:** Let bindings within function bodies do NOT generalize. `infer_stmt` for `st_let` just does `env_insert(eng, name, val_ty)` without calling `generalize`. This means local `id = \a -> a; id(42) + glyph_str_len(id("x"))` will fail because `id` is monomorphic within the body. This is a known simplification — standard ML does generalize at let, but the implementation cost (running `env_free_vars` at every let binding) was deemed too high.

Tested: `test_ty_forall` passes, verifying that `generalize` produces a ForAll node and `instantiate` creates independent copies.

### 2.3 Row Polymorphism ✓

Record row polymorphism works correctly for field access and cross-record unification:

- `r.name` creates constraint `r : {name:t ..}` (open record with fresh rest variable)
- Two open records unify by matching common fields, then creating a fresh row variable for the remainder
- Closed records (literal constructors) require exact field matches

The implementation follows Rémy-style row polymorphism with the five cases handled in `unify_records` → `unify_fields_against` → `unify_row_vars`:
1. Common fields: unify field types
2. Left-only fields absorbed by right's row variable
3. Right-only fields absorbed by left's row variable
4. Both open: fresh row variable connects remainders
5. Both closed: extra fields → error

Tested: `test_ty_record_field` verifies that field access on a record with `{pos:I, val:I}` correctly resolves `.val` to Int.

### 2.4 Operator Type-Directed Codegen ✓

The tmap system correctly resolves operator overloading:
- `binop_type` / `binop_arith` recognizes Int+Int→Int, Float+Float→Float, Str+Str→Str
- When both operands are unresolved type variables, falls back to `unify(lt, rt)` and returns the left type
- `binop_arith` also handles mixed Int+Float→Float (though the spec says this is a type error — the self-hosted checker is more permissive than the spec)
- The resolved type tags flow into MIR lowering via tmap for correct code generation

### 2.5 Pipe and Composition ✓

`x |> f` correctly infers as `f(x)` — creates `fn_ty = mk_tfn(left_ty, ret)`, unifies with `right_ty`, returns `ret`.

`f >> g` correctly infers as `\x -> g(f(x))` — creates fresh `a`, `b`, `c`, unifies `f : a → b` and `g : b → c`, returns `a → c`.

Tested: `test_ty_pipeline`, and probing confirmed `glyph_str_len >> glyph_str_len` correctly reports mismatch (S→I then S→I: output I doesn't match input S).

### 2.6 Optional/Result Types (Partial ✓)

- `Some(x)` correctly wraps in `?T`
- `None` gets `?fresh_var`
- `!` unwrap on Optional extracts inner type
- `?` propagation extracts inner from both Optional and Result
- Error propagation (`?`) checks for Result type first, falls back to Optional

### 2.7 Struct Map Constraint System ✓

A novel feature: `tc_constrain_field` and `tc_constrain_struct` use a "struct map" (built from named type definitions) to propagate C-level type information into inference. When a field access like `r.x` resolves and the struct map indicates that field `x` has C type `double`, the type checker unifies the result with Float. This solves the problem where struct fields of type `double` would otherwise be inferred as Int (since everything is `long long` at the ABI level).

### 2.8 Extern Type Registration ✓

External functions from the `extern_` table have their signature strings parsed (`split_arrow`) and converted to function types. `sig_part_ty_flex` uses `subst_fresh(eng)` for unrecognized type names, making extern types partially polymorphic. This is correct for the common case where most externs have concrete types.

`register_builtins` registers ~55 runtime functions with correct types, including polymorphic functions (`glyph_array_len : ∀a. [a] → I`, `glyph_array_push : ∀a. [a] → a → [a]`, `ok : ∀a. a → !a`, etc.).

---

## 3. What Doesn't Work

### 3.1 Silent Occurs Check Failure — CRITICAL

**Bug:** `subst_bind` returns -1 on occurs check failure but does NOT push an error to `eng.errors`. Callers of `unify` (which propagates the -1) also don't check the return value.

**Proof:**
```
test_occurs x = x(x)          → valid: true   (WRONG: should be occurs check violation)
test_self_apply x = x(x) + 1  → valid: true   (WRONG: infinite type t = t → t)
test_infinite f = f(f(f))      → valid: true   (WRONG)
```

**Root cause chain:**
1. `subst_bind` detects occurs check violation → returns -1, no error pushed
2. `unify` propagates the -1 return value
3. `apply_args` calls `unify(eng, fn_ty, expected_fn)` but discards the return (bare expression)
4. Inference continues with unresolved type variables, producing garbage types

**Impact:** Any self-application (`x(x)`, omega combinator, recursive type construction) silently passes. The resulting types are internally inconsistent but never crash the checker — they just produce wrong tmaps. At codegen, the wrong types may lead to incorrect operator selection or, more commonly, the heuristic fallback catches it anyway.

**Same pattern in:** `infer_field_access`, `infer_pipe`, `infer_compose`, `infer_match_arms_ty`, `unify_array_elems`, `infer_map_pairs`, `tc_apply_field_types`. At least 12 call sites that ignore `unify`'s return value.

### 3.2 Bool/Int Coercion — DELIBERATE BUT UNSOUND

**Behavior:** `unify_tags` contains:
```
_ ? (at == ty_int() || at == ty_bool()) && (bt == ty_int() || bt == ty_bool()) -> 0
```

This treats Bool and Int as interchangeable. `42 + true` passes the type checker.

**Rationale:** At runtime, Bool is just 0 or 1 in a `long long`. The Rust checker has the same coercion. This is a pragmatic choice but violates the "no implicit coercion" principle stated in the type system spec (Section 10.2).

**Impact:** Low in practice — Bool+Int rarely appears in real code. But it means the type checker can't catch logic errors like accidentally adding a comparison result to an integer.

### 3.3 Int/Float Implicit Coercion — UNSOUND

**Behavior:** `binop_arith` accepts Int+Float→Float and Float+Int→Float:
```
match lt == ty_int()
  true -> match rt == ty_float()
    true -> mk_ty_prim(ty_float(), ...)    ← Int+Float → Float
```

**The spec says:** "No implicit coercion between numeric types. `I + F` is a type error." (Section 10.2)

**The Rust checker does NOT have this** — it rejects Int+Float.

**Impact:** Medium. Real Glyph code frequently mixes int and float arithmetic (especially in scientific/game examples). The self-hosted checker's permissiveness here actually matches what the runtime does (bit-cast + float add), but it prevents catching accidental mixing.

### 3.4 No Let-Polymorphism in Local Bindings — LIMITATION

As described in §2.2, `infer_stmt` for `st_let` does not call `generalize`. This means:
```
f =
  id = \x -> x
  a = id(42)       -- id : t→t, unifies t=I, id now I→I
  b = id("hello")  -- ERROR: can't unify I with S
```

Standard ML and Haskell generalize at every let binding. The Rust checker (`infer.rs:infer_stmt`) DOES call `env.generalize(subst, val_ty)` for let bindings. So the Rust checker is strictly more powerful here.

**Impact:** Low-medium. Most Glyph code defines polymorphic functions at the top level (where generalization works), not as local lets. But it does prevent some patterns like local helper polymorphism.

### 3.5 Fallback-to-Unify Masks Operator Type Errors — UNSOUND

When `binop_type` returns -1 (can't determine types — e.g., both operands are unresolved type variables), `infer_binary` falls back to:
```
unify(eng, lt, rt)
lt
```

This means `x + 1` where `x` is a parameter of unknown type will silently unify `x` with Int. That's correct. But `x + "hello"` where `x` is unknown will unify `x` with Str and return Str — even though `+` on strings should only work via `str_concat`, not the raw `+` operator in C codegen. More problematically, if `binop_type` can't resolve the types (both vars), it allows ANY combination:

```
test_str_int_add x = x + 1   → valid: true  (x unified with Int, correct)
```

The issue is that the fallback doesn't constrain the operand types to be numeric or string — it just unifies them with each other, accepting any pair of same-typed operands.

### 3.6 Constructor Pattern Matching — INCOMPLETE

`infer_ctor_pattern` has special cases for `Some` and generic constructors, but:
- `Some(1, 2, 3)` is accepted (arity not checked)
- Unknown constructors get fresh type variables (no error)
- Variant binding (`infer_ctor_bind_typed`) gives all sub-patterns the SAME type (the constructor's single parameter type), not positional types

The Rust checker correctly reports type mismatches for enum variant matching (e.g., `Red` vs `Green` vs `Blue` in `color_tag`), while the self-hosted checker silently accepts all variant patterns.

### 3.7 No Exhaustiveness Checking — KNOWN LIMITATION

Neither checker verifies that match expressions cover all possible values. This is documented in the spec (Section 8.2). Non-exhaustive matches silently fall through in the self-hosted compiler (the MIR emits `tm_unreachable` which traps at runtime).

### 3.8 Assignment (:=) Not Type-Checked — KNOWN LIMITATION

The spec notes this in Section 9.1: "Assign (:=) type checking does not verify LHS matches RHS." The self-hosted checker doesn't even parse `:=` as a statement (it's parsed as part of expression handling, not in `infer_stmt`). This means:
```
x = 42
x := "hello"   -- silently accepted, no type error
```

### 3.9 Error Type Absorption — DESIGN TRADEOFF

Both checkers treat `ty_error` as a universal sink: `unify(Error, T) → success` for any T. This prevents cascading errors (one unknown name doesn't cause 50 downstream mismatches) but also means that ANY function call to an unregistered name produces Error, which then silently poisons all downstream inference.

In `check_def`, only builtins are registered — user-defined functions are unknown, producing Error types. This makes `check_def` useful only for single-definition validation with builtin-only dependencies. `check_all` registers all functions via pre-registration, so cross-function errors are caught there.

### 3.10 Never Type Handling — PARTIALLY BROKEN

`unify_tags` treats `ty_never` as universally compatible:
```
_ ? at == ty_never() || bt == ty_never() -> 0
```

This is correct for `glyph_exit(1) + 42` (Never unifies with Int because the exit diverges). But it also means `ty_never` unifies with anything without constraining the other side, which could mask errors if Never types leak into non-diverging code.

The Rust checker has a more nuanced approach: it checks for Never before type variables, preventing Never from being bound to a variable when the variable should be constrained.

---

## 4. Divergence Between Checkers

### 4.1 Error Counts

| Database | Self-hosted errors | Rust errors | Gap |
|----------|-------------------|-------------|-----|
| `glyph.glyph` | 0 | 16 | 16 |
| `documentation.glyph` | 0 | 60 | 60 |
| `stdlib.glyph` | 0 | not tested | — |

### 4.2 Specific Divergences on glyph.glyph

The Rust checker catches 16 errors in the compiler's own source that the self-hosted checker misses:

1. **Result type mismatches (3):** `cmd_build`, `cmd_run`, `cmd_test` — functions that return `Ok(x)` in one branch and `Err(y)` in another. The Rust checker treats these as distinct nominal types; the self-hosted checker doesn't fully resolve Result variants.

2. **Enum variant mismatches (7):** `color_tag`, `light_tag`, `mcp_diffs_to_json`, `print_diffs` — match arms returning different enum variants (Red/Green/Blue, Added/Removed/Modified). The Rust checker's nominal enum typing catches these; the self-hosted checker accepts all variant constructors.

3. **Array/scalar confusion (2):** `load_libs_for_build` — expected `S` but got `[S]`. The self-hosted checker's error absorption prevents this from propagating.

4. **Record/scalar confusion (3):** `read_lib_generics_loop` — expected record, got string and vice versa.

5. **Missing record field (1):** `mono_pass` — record missing `pf_fn_idx` field.

### 4.3 Structural Differences

| Feature | Self-hosted | Rust |
|---------|-------------|------|
| Pool representation | Flat array of TyNode records | Algebraic `Type` enum |
| Union-find | Path compression only | Path compression + rank |
| Occurs check | Returns -1, no error pushed | Returns `TypeError::OccursCheck` |
| Let-polymorphism | Function-level only | Let-binding level |
| Bool/Int coercion | Yes | Yes |
| Int/Float coercion | Yes (in binop_arith) | No |
| Error absorption | Yes (ty_error) | Yes (Type::Error) |
| Row unification | Bidirectional field matching + row var binding | Same algorithm, `|| true` quirk |
| Source spans | No (pool indices only) | Yes (Span on all errors) |
| Error messages | `"type mismatch: I vs S @309/310"` | `"expected Int, found Str" at 1:5` |
| Named types/enums | Partial (constructors as functions) | Partial (same limitation) |
| Map types | Parsed, inferred, not codegen'd | Same |
| Ref/Ptr types | Not in self-hosted tag set | In Rust Type enum |

---

## 5. Scale and Complexity

### 5.1 Self-Hosted Type Checker

- **212 definitions** in the `typeck` namespace and related prefixes
- **~19,700 tokens** / **~63,450 characters** total
- **28 test definitions** covering: engine init, environment scoping, fresh variables, primitives, unification (same/var/fn/array), binary ops, unary ops, inference (int/str/complex/block/call/deep), lambdas, pipelines, ForAll generalization, record fields, type annotations, type builders/constructors/helpers
- **Key subsystems:**
  - Pool + constructors: ~30 defs
  - Substitution + union-find: ~10 defs
  - Environment: ~12 defs
  - Unification: ~10 defs
  - Inference (expressions): ~35 defs
  - Inference (orchestration): ~25 defs
  - Generalization + instantiation: ~20 defs
  - Struct constraints + tmap: ~25 defs
  - Builtin registration: ~5 defs (but `register_builtins` alone is 1,821 tokens)
  - Error reporting + helpers: ~20 defs
  - Monomorphization integration: ~20 defs

### 5.2 Rust Type Checker

- **8 source files**, ~2,056 lines
- **~18 unit tests** across key modules
- **Well-structured** with clear module boundaries (types, unify, infer, env, resolve, builtins, error)

### 5.3 Comparison

The self-hosted checker is roughly 3× larger by character count (63k vs ~20k) due to Glyph's verbose low-level style (no pattern matching on types, manual pool management, explicit recursion for everything). But it covers approximately the same feature surface — the extra volume is implementation overhead, not additional capability.

---

## 6. The Type Checker's Actual Role

Despite the gaps catalogued above, the type checker serves a real and important purpose in the compiler. Its primary value is **not** catching type errors — it's **informing code generation**.

### 6.1 Operator Disambiguation (Primary Value)

Without type information, the MIR lowering must guess whether `+` means integer addition, float addition, or `str_concat`. The old heuristic (`is_str_op`/`is_float_op`) used name-based guessing — if one operand was a known string variable, use `str_concat`. This failed on:
- String parameters (no type prefix in the name)
- Float fields in records (field access returns opaque type)
- Intermediate values from function calls

The tmap solves this. After inference, every AST node has a resolved type tag. MIR lowering queries `tctx_is_float_bin(tctx, n1, n2, ...)` and `tctx_is_str_bin(tctx, n1, n2, ...)`. If the tmap has an answer, it's used. Otherwise, the old heuristic is tried as a fallback. This two-tier system is robust — the tmap catches what inference can resolve, and the heuristic catches the rest.

### 6.2 Float Field Inference (Secondary Value)

The struct map constraint system (`tc_constrain_field`, `tc_constrain_struct`) propagates C type information from named record definitions into inference. When a field is declared as `double` in the struct map, accessing it forces the result type to Float. This solved bugs where float fields were being treated as integers in arithmetic.

### 6.3 Advisory Diagnostics (Tertiary Value)

The type checker's error reporting catches some obvious mismatches:
- `glyph_str_len(42)` — caught
- `[1,2,3] + 5` — caught
- `f(42) + f(f)` — caught (function/int confusion)
- `glyph_str_len >> glyph_str_len` — caught (composition mismatch)

But it misses too many cases (§3) to be relied upon as a safety net.

---

## 7. Recommendations

### 7.1 Fix the Occurs Check (High Priority, Low Effort)

Add error reporting to the occurs check failure path in `subst_bind`:
```
match ty_contains_var(eng, resolved, root)
  true ->
    glyph_array_push(eng.errors, "infinite type: occurs check failed")
    -1
```

This single change would catch all self-application bugs. Estimated effort: 1 definition modified.

### 7.2 Check `unify` Return Values (High Priority, Medium Effort)

The ~12 call sites that ignore `unify`'s return value should at minimum not suppress -1 returns. The most impactful sites:

- `apply_args` — function call argument unification
- `infer_field_access` — record field constraint
- `infer_pipe` — pipe operator
- `infer_compose` — composition operator
- `infer_match_arms_ty` — match arm pattern/body unification

This doesn't require changing the return type — just adding error collection when `unify` returns -1. Estimated effort: ~12 definitions modified.

### 7.3 Add Let-Binding Generalization (Medium Priority, Medium Effort)

Change `infer_stmt` for `st_let` to call `generalize(eng, val_ty)` before `env_insert`. This aligns with the Rust checker and enables local polymorphism. The main concern is performance — `generalize` calls `env_free_vars` which scans the entire environment. For functions with many let bindings, this could be slow.

A compromise: only generalize let bindings whose RHS is a lambda (where polymorphism is useful). Simple value bindings (`x = 42`) don't benefit from generalization.

### 7.4 Remove Int/Float Coercion (Low Priority)

Remove the Int+Float→Float path from `binop_arith` to match the spec. This would require updating some existing code that relies on implicit mixing, but would make the checker more rigorous.

### 7.5 Enforce Type Checking in Build (Long-Term)

The type checker currently reports errors as warnings that don't block compilation. Making it block on type errors would require:
1. Fixing all false positives (currently 0 reported, but that's because real errors are suppressed)
2. Implementing the fixes in §7.1–7.4 first
3. Running the checker on all example programs and fixing genuine type errors
4. A flag to opt-out for code that legitimately uses dynamic typing patterns

### 7.6 Exhaustiveness Checking (Long-Term)

Implementing pattern exhaustiveness checking requires:
- Knowledge of all enum variants (available from `build_variant_map`)
- Pattern matrix analysis
- Integration with the inferred scrutinee type

This is a significant feature (~200-400 tokens of new code, several new definitions) but would catch an entire class of "non-exhaustive match → silent fallthrough" bugs that currently only manifest at runtime.

---

## 8. Test Coverage Assessment

The 28 type checker tests cover:
- ✅ Engine initialization and pool management
- ✅ Environment scoping (push/pop)
- ✅ Fresh variable allocation
- ✅ Primitive type construction
- ✅ Unification (same type, variable binding, function types, arrays)
- ✅ Binary and unary operator inference
- ✅ Expression inference (int, string, complex, block, call, deep)
- ✅ Lambda inference
- ✅ Pipeline inference
- ✅ ForAll generalization and instantiation
- ✅ Record field access with row polymorphism
- ✅ Type annotation handling
- ✅ Type builders and constructors

**NOT tested:**
- ❌ Occurs check (would pass because the check silently fails)
- ❌ Cross-function inference in check_all pipeline
- ❌ Enum variant typing
- ❌ Result type inference
- ❌ Map type inference
- ❌ Let-destructuring type propagation
- ❌ Match exhaustiveness (not implemented)
- ❌ Assignment (:=) type checking (not implemented)
- ❌ Negative test cases (deliberate type errors that SHOULD be caught)
- ❌ Error message quality/content
- ❌ Two-pass warm-up correctness

The most notable gap is the absence of **negative tests** — tests that verify the checker REJECTS ill-typed programs. All existing tests verify that well-typed programs produce expected types. Without negative tests, regressions that make the checker more permissive (accepting bad programs) go undetected.

---

## Appendix A: Probing Results Summary

| Test Case | Expected | Self-hosted | Rust |
|-----------|----------|-------------|------|
| `42 + true` | error | ✅ valid | ✅ valid |
| `42 + 3.14` | error (per spec) | ✅ valid | ❌ error |
| `x(x)` | occurs check error | ✅ valid (BUG) | — |
| `x(x) + 1` | occurs check error | ✅ valid (BUG) | — |
| `f(f(f))` | occurs check error | ✅ valid (BUG) | — |
| `glyph_str_len(42)` | error | ❌ error | ❌ error |
| `[1,2,3] + 5` | error | ❌ error | ❌ error |
| `f(42) + f(f)` | error | ❌ error | — |
| `x |> str_len |> str_len` | error | ❌ error | ❌ error |
| `str_len >> str_len` | error | ❌ error | ❌ error |
| `Some(1,2,3)` | arity error | ✅ valid (BUG) | — |
| `f(1,2) + f(1,2,3)` | arity error | ✅ valid (BUG) | — |
| `match x: 1→42, true→99` | error (Int≠Bool) | ✅ valid (coercion) | — |
| `match x: 1→42, _→str_len(x)` | error | ❌ error | — |
| `r.x + r.y + r.z` then `r={x:1}` | error | N/A (cross-fn) | — |
| `{name:42, age:50}.nonexistent` | error | ❌ error | ❌ error |
| `Some(42)!` then `str_len(result)` | error | ❌ error | — |
| `r2 = r{name:42}; str_len(r2.name)` | error | ❌ error | — |

Legend: ✅ valid = accepted (green if correct), ❌ error = rejected (green if correct), (BUG) = should have been rejected

## Appendix B: Definition Census

```sql
SELECT ns, COUNT(*) as n, SUM(tokens) as tokens
FROM def WHERE kind='fn' AND gen=1
AND ns IN ('typeck')
GROUP BY ns;
-- typeck: 162 defs, 16,503 tokens

-- Additional typeck-related defs outside the namespace:
-- tc_* (not ns=typeck): ~50 defs
-- Total typeck surface: ~212 defs, ~19,721 tokens
```

## Appendix C: `tests/smoke/melting.glyph`

A white-box smoke test database that exercises the type checker's known failure points. The name refers to the "melting point" — where the type system breaks down under heat.

**Setup:** Uses `glyph use glyph.glyph` to access all compiler internals. Each test constructs a type checker engine, parses a source snippet, runs inference, and asserts whether errors were produced.

**Running:** `./glyph test tests/smoke/melting.glyph`

### Bug Exposure (7 tests — FAIL today, PASS when fixed)

| Test | Bug |
|------|-----|
| `melt_occurs_self_apply` | `x(x)` — occurs check returns -1, no error pushed |
| `melt_occurs_nested` | `x(x(x))` — same silent occurs check |
| `melt_occurs_with_use` | `x(x) + 1` — occurs check + ignored return |
| `melt_fn_used_as_int` | `x(42) + x` — occurs check via binop fallback |
| `melt_int_float_coerce` | `42 + 3.14` — Int+Float accepted, spec says reject |
| `melt_ctor_arity_some` | `Some(1,2,3)` — arity not checked |
| `melt_guard_not_bool` | `_ ? x+1 -> ...` — guard not constrained to Bool |

These assert `assert(n > 0)` — the type checker SHOULD produce errors but doesn't. Each failure pinpoints a specific defect.

### Correctness (11 tests — PASS today, regression guards)

| Test | What it verifies |
|------|-----------------|
| `melt_arr_int_mismatch_caught` | `[1,2,3] + 5` rejected (array vs int) |
| `melt_pipe_mismatch_caught` | `x \|> str_len \|> str_len` rejected (I fed to S) |
| `melt_compose_mismatch_caught` | `str_len >> str_len` rejected (output I != input S) |
| `melt_direct_call_mismatch_caught` | `str_len(42)` rejected (I fed to S) |
| `melt_match_arm_mismatch_caught` | Int pattern then `str_len(x)` rejected |
| `melt_unify_ret_ignored_apply` | `str_len(x) + x(42)` rejected (S vs Fn) |
| `melt_ctor_unknown_fresh` | `Bogus(x, 42)` rejected (unknown constructor) |
| `melt_let_no_polymorphism` | Monomorphic local let catches type conflict |
| `melt_cross_fn_warm_cold` | Two-pass design catches cross-function mismatch |
| `melt_opt_unwrap_type` | `Some(42)!` correctly infers Int return |
| `melt_record_field_row_poly` | `r.name` + `r.age` row polymorphism works |

### Design Documentation (5 tests — PASS today, deliberate behavior)

| Test | What it documents |
|------|-------------------|
| `melt_bool_int_coerce` | Bool and Int unify — matches runtime (`long long` 0/1) |
| `melt_match_bool_int_pattern` | Int and Bool patterns coexist in same match |
| `melt_error_absorbs_all` | `ty_error` unifies with any type (cascading error prevention) |
| `melt_never_absorbs_all` | `ty_never` unifies with any type (bottom/divergence) |
| `melt_binop_fallback_unify` | `x + y` with unknown types unifies x==y, no numeric constraint |

These are not bugs — they reflect design choices where the type checker's permissiveness matches the runtime's actual semantics for an LLM-oriented language where everything is `long long`.
