# Glyph Compiler Known Bugs

## ~~BUG-009: Raw strings don't escape backslashes in C codegen~~

**Status:** Open
**Severity:** Medium (workaround exists)
**Found:** 2026-04-01 (smoke test development)

**Description:** Raw string literals (`r"..."`) preserve backslash characters literally at the Glyph level, but the C codegen emits them unescaped into C string literals. The C compiler then interprets sequences like `\x`, `\n`, `\t` as C escape sequences.

**Reproduction:**
```glyph
f = r"hello \x world"
```

The generated C contains:
```c
glyph_cstr_to_str("hello \x world");
```

The C compiler interprets `\x` as a hex escape sequence and fails:
```
error: \x used with no following hex digits
```

**Expected behavior:** The C codegen should escape backslashes in raw string content, emitting `\\x` in the C string literal so the runtime receives the literal `\x`.

**Root cause:** `cg_escape` (or the string emission path) doesn't re-escape backslashes that came from raw strings. Regular strings have their escapes processed by the Glyph tokenizer, but raw strings pass through verbatim, and the C codegen doesn't account for this.

**Workaround:** Use `str_from_code(92)` to produce backslash characters at runtime:
```glyph
bs = str_from_code(92)
src = "f x = " + bs + "y -> x + y"
```

**Affected functions:** `cg_escape`, string literal emission in `cg_op` / `cg_stmt`

## ~~BUG-010: tc_collect_fv segfaults on cyclic types when called from user code~~

**Status:** Open
**Severity:** High (crash, no workaround)
**Found:** 2026-04-01 (smoke test development)

**Description:** Calling `tc_collect_fv` on a type graph that contains cycles (created via `unify(eng, var, container_of_var)`) causes a segmentation fault. The depth-limit cycle detection (depth=30) that was added in the BUG-008 fix protects the compiler's internal type checker, but when `tc_collect_fv` is called directly from user code (e.g., via `glyph use`), the crash still occurs.

**Reproduction:**
```glyph
test_crash =
  eng = mk_engine()
  p = eng.ty_pool
  a = subst_fresh(eng)
  b = subst_fresh(eng)
  unify(eng, a, mk_tarray(b, p))
  tc_collect_fv(eng, mk_tfn(a, b, p), [], 0, [])  -- segfault
  0
```

**Expected behavior:** `tc_collect_fv` should terminate on cyclic type graphs, returning the free variables found before hitting the cycle.

**Root cause:** The depth-limit cycle detection may not be active in all call paths, or the cycle created by `unify(a, array(b))` followed by traversal through `a -> array(b) -> b -> ...` creates a pattern the depth counter doesn't catch.

**Affected functions:** `tc_collect_fv`, `tc_collect_fv_fields`

## ~~BUG-011: Interleaved instantiate/unify produces ty_error~~

**Status:** Open
**Severity:** Medium (workaround: batch instantiations before unifications)
**Found:** 2026-04-01 (smoke test development)

**Description:** When `instantiate` and `unify` calls are interleaved on the same generalized type, the second `instantiate` produces types that resolve to `ty_error` (tag 99) instead of fresh type variables.

**Reproduction:**
```glyph
test_interleave =
  eng = mk_engine()
  p = eng.ty_pool
  a = subst_fresh(eng)
  id_ty = mk_tfn(a, a, p)
  gen = generalize(eng, id_ty)

  -- First: instantiate, then unify with int
  i1 = instantiate(eng, gen)
  n1 = pool_get(eng, i1)
  unify(eng, n1.n1, mk_tint(p))
  r1 = subst_resolve(eng, n1.n2)
  assert_eq(ty_tag(p, r1), ty_int())   -- PASS

  -- Second: instantiate again, then unify with str
  i2 = instantiate(eng, gen)
  n2 = pool_get(eng, i2)
  unify(eng, n2.n1, mk_tstr(p))
  r2 = subst_resolve(eng, n2.n2)
  assert_eq(ty_tag(p, r2), ty_str())   -- FAIL: ty_error (99)
```

**Working pattern:** Instantiate all copies first, then unify:
```glyph
  i1 = instantiate(eng, gen)
  i2 = instantiate(eng, gen)        -- all instantiations first
  -- then unify separately           -- works correctly
```

**Root cause:** `instantiate` likely uses `subst_bind` to map the forall's bound variables to fresh variables. After the first `unify` modifies the substitution state, the second `instantiate` sees the modified bindings and produces error types instead of fresh variables.

**Impact:** The compiler's own `infer_fn` may avoid this pattern naturally (by processing all uses of a polymorphic function in the right order), but it's a latent correctness issue that could surface with certain definition orderings.

**Affected functions:** `instantiate`, `inst_type`, `subst_bind`
