# Monomorphization Lessons Learned

Practical pitfalls discovered during the C codegen monomorphization (Phases 1-6), written as a guide for the LLVM backend implementation.

---

## 1. The JNode/AstNode Type Confusion

**The single hardest bug.** Three sessions were spent on this before finding the root cause.

### What happened

`build_local_types` infers struct types for locals by examining which fields they access. Both `JNode` and `AstNode` have a `.sval` field. When a local accesses `.sval` and `.tag`, the inference picks `JNode` (3 fields, has `.tag`). But later code accesses `.n1` on the same local — and `JNode` has no `.n1` field. The C compiler emits:

```
error: 'Glyph_JNode' has no member named 'n1'
```

The root cause is `pool_get`, which returns values from `eng.ty_pool`. These values have fields from BOTH `JNode` (`.tag`, `.sval`) and `AstNode` (`.n1`, `.n2`, `.n3`). No single type definition covers all the fields that code accesses on these values.

### How it was fixed

A post-validation pass (`blt_validate_types`) runs AFTER all type inference phases. For every typed local, it checks that ALL fields accessed on that local exist in the assigned struct type. If any field access is incompatible, the type assignment is reverted to `""` (untyped GVal).

### LLVM lesson

**This will happen again.** The LLVM backend already uses `build_local_types` for struct-aware field access (`ll_emit_field`). When you start emitting typed LLVM locals (`%Glyph_JNode*` instead of `i64`), the same confusion will surface. The validation pass must be applied to LLVM codegen too, not just C codegen. Either reuse `blt_validate_types` or port its logic.

The deeper lesson: **field-access-based type inference is inherently fragile.** It works by observing which field names code touches and guessing the type. When two struct types share field names, or when a value is used polymorphically (accessing different subsets of fields in different code paths), the guess is wrong. Always validate after inferring.

---

## 2. Type Propagation Across rv_use Creates Invisible Dependencies

### What happened

Local `_43` gets typed as `JNode` from a call-site return type. Then `_44 = _43` (an `rv_use` statement) propagates the type. Later, `_44` accesses `.n1` — which JNode doesn't have. The validation only checked `_43`'s direct field accesses (which were compatible), not the field accesses on `_44` which inherited the type.

### How it was fixed

`blt_validate_types` runs as the FINAL pass, after ALL type assignments including propagation. It checks every typed local independently against its own field accesses, regardless of how the type was assigned.

### LLVM lesson

In LLVM IR, `rv_use` becomes a `load` + `store` (or just an alias). If you type the source as `%Glyph_JNode*`, the destination inherits that type. You must validate the destination's field accesses independently. Don't assume that because a type is valid at the source, it's valid everywhere the value flows.

---

## 3. Two-Pass Return Map Is Required

### What happened

`build_ret_map` analyzes each function's MIR to determine what struct type it returns. It uses `build_local_types` to find the return local's type. But wrapper functions like `parse_destr_fields` don't create aggregates — they just call another function and return its result. In pass 1, the callee's return type isn't known yet (chicken-and-egg), so the wrapper's return type is empty.

### How it was fixed

Two-pass analysis:
- **Pass 1** (`build_ret_map_loop`): Uses `build_local_types` (phases 1-3 only: aggregate scan + propagation + field-access inference). No inter-procedural information.
- **Pass 2** (`build_ret_map_loop2`): Uses `build_local_types2` which takes Pass 1's ret_map. Now when analyzing a function that calls `parse_destr_fields`, it can look up that callee's return type from Pass 1 and propagate it.

### LLVM lesson

The LLVM backend currently has no ret_map equivalent. When you add typed function signatures to LLVM IR, you'll need the same two-pass approach. A function like:

```llvm
define i64 @wrapper() {
  %1 = call i64 @inner()
  ret i64 %1
}
```

needs to know that `@inner` returns `%Glyph_ParseResult*` to emit:

```llvm
define %Glyph_ParseResult* @wrapper() {
  %1 = call %Glyph_ParseResult* @inner()
  ret %Glyph_ParseResult* %1
}
```

Without two passes, the wrapper stays `i64`.

---

## 4. Forward Declarations Must Match Definitions Exactly

### What happened

`cg_forward_decls` was generating `GVal fn_name(GVal, GVal)` while `cg_function2` was generating `Glyph_Stats* fn_name(GVal, Glyph_Config*)`. GCC errors on conflicting types for the same function name.

### How it was fixed

`cg_forward_decls` was updated to use the same ret_map lookup as `cg_function2`. Both now emit identical signatures.

### LLVM lesson

LLVM IR is stricter than C about this. In C, a mismatched forward declaration is an error caught by the compiler. In LLVM IR, a `declare` with wrong types followed by a `define` with different types is an **IR validation failure** — `llc` or `opt` will refuse to process the module. You'll get:

```
error: function definition and declaration have different types
```

The fix is the same: both `declare` and `define` must use the same ret_map. But the failure mode is different — LLVM catches it immediately at the IR level, which is actually better than C where it only sometimes errors depending on compiler flags.

---

## 5. Casting at Every Typed/Untyped Boundary

### What happened

Six distinct boundary types needed explicit casts:

| Boundary | Direction | Cast needed |
|----------|-----------|-------------|
| Typed local → GVal parameter | typed → untyped | `(GVal)typed_local` |
| GVal local → typed parameter | untyped → typed | `(Glyph_Name*)gval_local` |
| Typed return → GVal destination | typed → untyped | `(GVal)call_result` |
| GVal return → typed destination | untyped → typed | `(Glyph_Name*)call_result` |
| Typed local → variant element | typed → untyped | `(GVal)local` in variant store |
| Variant element → typed local | untyped → typed | `(Glyph_Name*)element` in field read |

Missing any ONE of these causes int-conversion errors. They were discovered incrementally across sessions — each time one was fixed, the next surfaced.

### LLVM lesson

In LLVM IR, these casts are `ptrtoint`/`inttoptr` or `bitcast` instructions. They're more verbose than C casts:

```llvm
; Typed → i64 (e.g., storing typed local into variant)
%cast = ptrtoint %Glyph_Stats* %typed_local to i64

; i64 → typed (e.g., loading variant element into typed local)
%cast = inttoptr i64 %element to %Glyph_Stats*
```

The key difference from C: **LLVM will reject the IR if you forget a cast.** In C, you get a warning (now an error). In LLVM IR, type mismatches are immediate validation failures. This is actually helpful — you'll find missing casts faster.

However, there are more cast sites in LLVM because SSA form means every value assignment is explicit. In C, `_7 = ((GVal*)_3)[1]` has an implicit int-to-pointer conversion that C allows with a warning. In LLVM, every step is explicit:

```llvm
%ptr = inttoptr i64 %_3 to i64*
%elem = getelementptr i64, i64* %ptr, i64 1
%raw = load i64, i64* %elem
%_7 = inttoptr i64 %raw to %Glyph_Stats*
```

**Recommendation:** Build a small set of helper functions for LLVM cast emission:
- `ll_cast_to_i64(typed_reg, type_name)` → emits `ptrtoint`
- `ll_cast_from_i64(i64_reg, type_name)` → emits `inttoptr`
- `ll_emit_boundary_cast(src_type, dst_type, reg)` → dispatches to the right one

---

## 6. Test Compilation Is a Separate World

### What happened

The test binary compiles BOTH `fn` definitions and `test` definitions. The ret_map was built from `fn` definitions only. Test functions that call `fn` functions with typed returns got no cast information. Result: 71 int-conversion warnings in the test build while the main build was clean.

### How it was fixed

`merge_ret_maps` combines the fn ret_map and test ret_map into a single map passed to the test code generator. Both `cg_test_program` and `cg_test_program_cover` use the merged map.

### LLVM lesson

The LLVM backend's test program generation (`build_test_program`) will need the same merge. Currently `cg_llvm_program` doesn't take a ret_map at all — it will need one. Don't forget that tests call library functions too, so the merged map must include all functions visible to test code.

---

## 7. Runtime Function Signatures Are Part of the Type System

### What happened

Two runtime functions had non-GVal signatures:
- `char* glyph_str_to_cstr(GVal s)` — returns `char*`, stored in GVal locals
- `GVal glyph_cstr_to_str(const char* s)` — takes `const char*`, but Glyph code passes GVal

With warning suppression removed, these became errors in user programs that call them directly (e.g., gstats's `get_env_str`).

### How it was fixed

Both functions changed to fully GVal signatures:
- `glyph_str_to_cstr` now returns `GVal`, internal callers cast with `(char*)(void*)`
- `glyph_cstr_to_str` now takes `GVal`, internal callers cast with `(GVal)(intptr_t)`
- All string literal call sites changed: `glyph_cstr_to_str((GVal)"hello")`

### LLVM lesson

The LLVM backend already declared `glyph_str_to_cstr` as returning `i64` (not `ptr`) — so the return type was already correct. But `glyph_cstr_to_str` was declared as taking `ptr`. This was updated to `i64` with `ptrtoint` at call sites.

**General principle:** When adding typed signatures to the LLVM backend, audit ALL runtime function declarations. Every `declare` in `ll_rt_decls1`/`ll_rt_decls2`/`ll_rt_decls3` must match the actual C runtime definition. If the C definition changes (as happened here), the LLVM declaration must change too, or you get linker errors or silent ABI mismatches.

List of runtime functions that have (or had) non-GVal C signatures — verify these are declared correctly:
- `glyph_cstr_to_str` — was `const char*` param, now `GVal`
- `glyph_str_to_cstr` — was `char*` return, now `GVal`
- `_glyph_panic_internal` — takes `const char*` (intentionally, only called from C runtime)
- `glyph_array_bounds_check` — returns `void`
- `glyph_dealloc` — returns `void`
- `glyph_set_args` — takes `(i32, ptr)` — this is special (called from main wrapper only)

---

## 8. Extern Wrappers Need Function Pointer Casts

### What happened

Extern wrappers generate: `GVal glyph_getenv(GVal _0) { return (GVal)(getenv)(_0); }`. The `_0` argument is GVal (intptr_t) but `getenv` expects `const char*`. With warning suppression off, this is an error.

The initial fix wrapped arguments in `(void*)`: `(getenv)((void*)_0)`. But this broke extern functions whose C implementations accept `GVal` directly (e.g., async library's `coro_create(GVal, GVal, GVal)`), because `void*` is not `GVal`.

### How it was fixed

`cg_extern_wrapper` now casts the function pointer itself to a GVal-based signature, suppressing all prototype checking:

```c
// Before (bare call — GCC checks arg types against getenv's prototype):
GVal glyph_getenv(GVal _0) { return (GVal)(getenv)(_0); }

// After (function pointer cast — GCC trusts the cast signature):
GVal glyph_getenv(GVal _0) { return (GVal)((GVal(*)(GVal))(getenv))(_0); }
```

New helpers: `cg_wrap_fnptr_cast(symbol, nparams, is_void)` and `cg_wrap_fnptr_params(n, i)` generate the cast expression. This approach works universally — both for externs taking typed C pointers (getenv, fflush) and externs taking GVal (coro_create, chan_send).

### LLVM lesson

The LLVM backend doesn't generate extern wrappers the same way — it uses `declare` for extern functions and calls them directly. Keep extern declarations as `i64` parameters and let LLVM handle the ABI. This avoids the cast issue entirely.

---

## 9. Variant Aggregates Have Two Cast Sites

### What happened

Enum variants are stored as `GVal* [tag, payload0, payload1, ...]`. Two operations need casts:

**Storing into a variant** (constructing): When a typed local is stored as a payload element, it needs `(GVal)` cast. Fixed by `cg_store_ops_offset2` (C) / needs `ptrtoint` (LLVM).

**Reading from a variant** (destructuring): When a variant payload element is read into a typed local, it needs a typed cast. Fixed in `cg_field_stmt2` with `cg_dest_cast` for the untyped-base path.

The second one was found last because it only appears in specific test cases (`test_str_enum_struct_field`) where an enum wraps a struct type.

### LLVM lesson

In LLVM IR, variant access already goes through pointer arithmetic:

```llvm
; Read tag:
%vptr = inttoptr i64 %variant to i64*
%tag = load i64, i64* %vptr

; Read payload[0]:
%pptr = getelementptr i64, i64* %vptr, i64 1
%payload = load i64, i64* %pptr
```

When the payload is known to be a `%Glyph_StrBox*`, the `load i64` must be followed by `inttoptr i64 %payload to %Glyph_StrBox*`. And when storing, the `store` must be preceded by `ptrtoint %Glyph_StrBox* %val to i64`.

---

## 10. The Order of Type Analysis Phases Matters

### What happened (multiple times)

The 4-phase type analysis in `build_local_types`:
1. **Aggregate scan** — locals assigned from `rv_aggregate` with a known struct type
2. **rv_use propagation** — `_y = _x` copies type from `_x` to `_y`
3. **Field-access collection** — record which fields each local accesses
4. **Field-access inference** — guess type from accessed field names

Running validation BEFORE propagation misses propagated-then-invalidated types. Running call-scan (inter-procedural) BEFORE field-access collection means newly typed locals aren't validated against their actual field usage.

The correct order for `build_local_types2` (with inter-procedural info):
1. Aggregate scan
2. Field-access collection (must be before call-scan, so call-scan can validate)
3. Call-scan with validation (uses ret_map, checks field compatibility before assigning)
4. rv_use propagation
5. Field-access inference
6. **Final validation** (catches anything propagation or inference got wrong)

### LLVM lesson

If the LLVM backend builds its own local type analysis (likely, since `ll_emit_function` already calls `build_local_types`), use the same ordering. Don't add new phases without running `blt_validate_types` at the end.

---

## 11. LLVM-Specific Considerations

These are NOT lessons from C codegen but predictions based on the C experience.

### SSA and typed locals

C codegen declares locals as `GVal _N = 0;` or `Glyph_Name* _N = 0;` and mutates them. LLVM uses `alloca` + `load`/`store`. When a local is typed:

```llvm
; Untyped:
%_7 = alloca i64

; Typed:
%_7 = alloca %Glyph_Stats*
```

But ALL MIR statements that read/write this local must agree on the type. If block 0 stores a `%Glyph_Stats*` but block 3 stores an `i64` (because the local is reused for a different purpose after the struct goes out of scope), the IR is invalid. C handles this silently via implicit casting; LLVM does not.

**Mitigation:** If a local has mixed-type usage across blocks, keep it as `i64` (alloca i64). Only promote to a typed alloca when ALL stores are compatible.

### GEP vs pointer arithmetic

C codegen uses `((Glyph_Name*)ptr)->field` which the C compiler resolves. LLVM needs explicit `getelementptr`:

```llvm
%typed = inttoptr i64 %raw to %Glyph_Stats*
%field_ptr = getelementptr %Glyph_Stats, %Glyph_Stats* %typed, i64 0, i32 1
%val = load i64, i64* %field_ptr
```

The field index (`i32 1`) must match the struct definition's field order. This is the LLVM equivalent of the field-offset problem in C codegen. If the struct definition changes field order, all GEP indices break silently.

**Mitigation:** Build a field-name-to-index lookup from the LLVM struct type definition. Never hardcode GEP indices.

### Phi nodes at block boundaries

When a typed local is assigned in multiple predecessor blocks (e.g., both branches of an if-else), LLVM needs a phi node. If one branch assigns a `%Glyph_Stats*` and the other assigns an `i64 0` (null), the phi must reconcile:

```llvm
; Both branches must produce the same type for the phi
bb_merge:
  %_7 = phi %Glyph_Stats* [%from_true, %bb_true], [null, %bb_false]
```

This means null must be `null` (typed null pointer), not `i64 0`. C handles this implicitly; LLVM does not.

### Function signatures are global

In C, you can have `GVal f(GVal)` in one file and `Glyph_Stats* f(Glyph_Config*)` in another — it's UB but compiles. In LLVM, a function has exactly one type. If `@f` is declared as `i64 (i64)`, ALL call sites must use that signature. You cannot have one call site use `i64 (i64)` and another use `%Glyph_Stats* (%Glyph_Config*)`.

This means the ret_map and param_map must be globally consistent. In C codegen, a missing cast just produces a warning. In LLVM, a type mismatch at any call site is a fatal validation error.

### The `opaque` escape hatch

If a local's type is uncertain, LLVM lets you keep it as `i64` and cast at boundaries. This is the equivalent of C's `GVal` fallback. The LLVM monomorphization should be opt-in per-local (just like C): only emit typed LLVM locals when the type is confidently known. `i64` is always safe.

---

## 12. Incremental Strategy That Worked

The C implementation was done in 6 phases, each independently testable. The phases that caused the most trouble were **not** the ones that seemed hardest upfront:

| Phase | Expected difficulty | Actual difficulty | Why |
|-------|-------------------|-------------------|-----|
| 1. Typed locals | Easy | Easy | Truly local, no cross-function effects |
| 2. Typed params | Medium | Hard | Cast explosion at every call site |
| 3. Typed returns | Medium | Hard | Forward decl consistency, ret_map needed |
| 4. Enum typedefs | Medium | Easy | Existing infrastructure handled most of it |
| 5. Mono pass | Hard | Easy | Infrastructure already existed, just needed type tag fixes |
| 6. Warning removal | Easy | **Very hard** | Surfaced every latent issue across all phases |

**Recommendation for LLVM:** Start with Phase 1 (typed locals only). LLVM's strict type checking will surface problems immediately — you won't need to wait for Phase 6 to find them. This is an advantage over C where issues were masked until warnings became errors.

---

## 13. Testing Strategy

### What worked

- **Bootstrap as integration test:** `ninja` runs the 4-stage bootstrap. If any stage produces wrong code, a later stage fails to compile. This caught several issues that unit tests missed.
- **gstats/gbuild/fsm as canaries:** Real programs with user-defined types. If these build clean, the codegen is likely correct for user code.
- **Inspecting `/tmp/glyph_out.c`:** Searching for a specific function name in the generated C was the fastest debugging technique.

### What didn't work

- **Relying on test suite alone:** 360 tests passed while gbuild still had 35 warnings. Tests exercise the compiler's own code (which has specific struct types), not the variety of patterns in user programs.
- **Fixing warnings one at a time:** Each fix revealed new warnings because the type system is interconnected. Better to enumerate ALL cast sites upfront, implement them all, then test.

### LLVM-specific testing

- `llc` validates IR before codegen — use this as a fast check after each change
- `opt -verify` can catch type mismatches without running full compilation
- The LLVM stage is stage 3 of the bootstrap (glyph2 → glyph), so it's automatically tested by `ninja`
- Compare output of a test program compiled via C codegen vs LLVM codegen to catch behavioral differences

---

## 14. Summary of All New Definitions (C Codegen)

For reference when building the LLVM equivalent:

| Definition | Purpose | LLVM equivalent needed? |
|------------|---------|------------------------|
| `build_local_types` | 4-phase local type inference | Already used by `ll_emit_function` |
| `build_local_types2` | + inter-procedural via ret_map | Yes, when adding typed signatures |
| `build_ret_map` | Two-pass return/param type collection | Yes |
| `blt_validate_types` | Post-validation safety net | Yes (critical) |
| `blt_type_fields` | Field list lookup by struct name | Yes |
| `cg_dest_cast` | Cast when destination is typed | `inttoptr` emission |
| `cg_src_cast` | Cast when source is typed | `ptrtoint` emission |
| `cg_ret_cast` | Cast typed return to GVal | `ptrtoint` after call |
| `cg_call_args2` | Smart argument casting | `ptrtoint`/`inttoptr` per arg |
| `cg_variant_aggregate2` | Type-aware variant stores | `ptrtoint` before store |
| `cg_store_ops_offset2` | Type-aware element stores | `ptrtoint` per element |
| `cg_field_stmt2` (untyped path) | Dest cast for variant reads | `inttoptr` after load |
| `merge_ret_maps` | Combine fn + test maps | Yes |
| `empty_ret_map` | Empty map for unit tests | Yes |
| `param_map_lookup` | Callee param types at call site | Yes |

---

## 15. Record/Closure/Indirect-Call Stores Need (GVal) Casts

### What happened

Three more boundary sites surfaced when building real examples (gwm, async, web-api):

| Site | Code pattern | Issue |
|------|-------------|-------|
| Record aggregate stores | `__r[N] = typed_local` | Typed `Glyph_WmColors*` stored into `GVal*` array |
| Closure capture stores | `__c[N] = typed_local` | Typed local captured into `GVal*` closure env |
| Indirect call arguments | `fn_ptr(closure, typed_local)` | Typed local passed as `GVal` param to closure call |

Each required `(GVal)` casts. Fixed in `cg_store_ops`, `cg_store_ops_alpha`, `cg_closure_stores`, `cg_indirect_args`, and `cg_indirect_args_rest`.

### Why these weren't caught in Section 5

Section 5 covered the **typed-function** boundaries (params, returns, variant elements). These are **generic infrastructure** boundaries — they move values between typed locals and GVal-typed storage that exists regardless of monomorphization. They only broke when mono actually typed a local as a struct pointer in code that uses closures or builds large records.

### LLVM lesson

Same pattern: `ptrtoint` before storing a typed value into an `i64*` slot (record, closure env, indirect call arg). The LLVM backend will catch these immediately as type mismatches since `store %Glyph_Foo* %val, i64* %slot` is invalid IR.

---

## 16. FFI C Files Must Be Portable Across intptr_t Definitions

### What happened

Hand-written FFI C files (`network_ffi.c`, `web_ffi.c`) used `%lld` in `snprintf` format strings for `GVal` values. On x86_64 Linux, `GVal` is `intptr_t` which maps to `long` (not `long long`). Both are 64-bit, but GCC's `-Wformat` distinguishes them — producing warnings on CI that don't appear on systems where `intptr_t` is `long long`.

Similarly, `gtk_ffi.c` declared `extern GVal glyph_cstr_to_str(const char *s)` but the runtime defines it as `GVal glyph_cstr_to_str(GVal s)` — conflicting types.

### How it was fixed

- `snprintf` args cast to `(long long)` to match `%lld` format specifier
- `write()` return values cast to `(void)` to suppress unused-result warnings
- `gtk_ffi.c` extern declaration changed to match runtime signature

### Lesson

FFI C files are outside the compiler's control — they're hand-written and prepended verbatim. They must not assume `GVal` is any specific C type beyond `intptr_t`. Rules:
1. Use `(long long)` casts with `%lld`, or use `PRIdPTR` from `<inttypes.h>`
2. Extern declarations of runtime functions must use `GVal`, not C-native types
3. `(void)write(...)` suppresses `-Wunused-result` portably

---

## 17. What Was NOT Needed (Avoid Over-Engineering)

- **Enum typedef structs:** The existing tagged-integer (fieldless) and GVal-array (payload) representations worked fine. No need for C `union` types.
- **Typed array elements:** Arrays stay `GVal*` internally. Only the locals holding array elements get typed after extraction.
- **Monomorphized function bodies:** The mono pass produces specialized MIR, but the specializations still use GVal signatures. Making specializations fully typed is a separate project.
- **Per-field C types in structs:** `typedef struct { GVal x; GVal y; } Glyph_Point;` — all fields are GVal. Only the struct pointer is typed. Per-field typing (e.g., `int64_t x; double y;`) is future work.
