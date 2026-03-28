# Monomorphization in Glyph

## What Is At Stake

Every value in Glyph today is a `GVal` (`typedef intptr_t GVal`). A string, an integer, a record pointer, an array — they're all the same type to the C compiler. This means:

- **Wrong field offset?** Compiles fine, segfaults at runtime.
- **Swapped function arguments?** Compiles fine, produces garbage.
- **Inverted discriminant check?** Compiles fine, takes the wrong branch.
- **String treated as int?** Compiles fine, corrupts data.

The C compiler cannot help because every function signature is `GVal f(GVal, GVal, ...)`. The `-w -Wno-int-conversion -Wno-incompatible-pointer-types` flags in the build command exist precisely because the generated C is type-unsafe by design.

**Monomorphization changes this.** By specializing polymorphic functions into concrete types and generating `typedef struct` for user-defined records, the C compiler becomes a free second type checker over generated code. Type mismatches that currently surface as runtime segfaults or silent corruption would become compile-time errors from `cc`.

---

## Current State

### What Exists

**37 `mono_*` functions** in `glyph.glyph` implement a complete two-phase monomorphization pass:

| Phase | Functions | Purpose |
|-------|-----------|---------|
| Collection | `mono_collect`, `mono_collect_fn`, `mono_coll_ast_loop`, `mono_coll_call` | Walk all function ASTs, find calls to polymorphic functions (`ty_forall`), record specialization specs |
| Spec building | `mono_is_poly`, `mono_fn_ty_idx`, `mono_build_var_map`, `mono_bvm_fn/arr/map`, `mono_spec_name`, `mono_type_name`, `mono_var_suffix`, `mono_spec_exists`, `mono_spec_add`, `mono_spec_append` | Check if callee is generic, build type variable → concrete type mapping, generate specialized names like `my_fn__int_str` |
| Compilation | `mono_compile_specs`, `mono_compile_specs_loop`, `mono_compile_one_spec`, `mono_find_parsed`, `mono_find_tr`, `mono_build_spec_tmap` | For each spec, re-lower the original function with concrete types substituted in |
| Call rewriting | `mono_build_call_map`, `mono_bcm_loop`, `mono_bcm_callers`, `mono_lookup_call`, `mono_lookup_loop` | At call sites in the caller, redirect polymorphic calls to their specialized versions |
| Utilities | `mono_copy_arr`, `mono_copy_loop`, `mono_concat_mirs`, `mono_concat_loop`, `mono_lam_check`, `mono_lam_names` | Array helpers, lambda lift detection |

**Struct codegen infrastructure** already generates `typedef struct` for known types:

- `build_full_struct_map` builds `struct_map` — an array of `[name, fields[], ctypes[]]` entries from type definitions
- `expand_with_generics` expands generic types with their concrete applications (e.g., `List<Int>` → `List_int`)
- `cg_all_typedefs` generates C typedefs: `typedef struct { GVal x; GVal y; } Glyph_Point2D;`
- `cg_field_stmt2` generates typed field access: `((Glyph_Point2D*)v)->x` instead of `((GVal*)v)[0]`
- `cg_record_aggregate2` generates typed allocation: `sizeof(Glyph_Point2D)` instead of `nfields * 8`
- `cg_record_update` generates typed copy-and-update
- `build_local_types` infers which locals hold which struct types (4-phase: direct assignment → propagation → field access collection → inference by field names)

**All struct-aware codegen has fallback paths.** When the struct type is unknown, it falls back to generic `GVal*` pointer arithmetic. This means monomorphization can be adopted incrementally.

### How It's Wired Into the Build Pipeline

```
build_program:
  1. parse_all_fns(sources)
  2. Type inference (register_builtins → tc_pre_register → tc_infer_loop)
  3. mono_pass(parsed, tc_results, za_fns, eng, vmap)
     → Returns {mp_mirs: specialized MIRs, mp_specs: spec list}
  4. compile_fns_parsed_mono(parsed, za_fns, tc_results, mp.mp_specs, ...)
     → For each function, builds call_map from specs
     → During lower_call(), mono_lookup_call() redirects to specialized name
  5. mono_concat_mirs(mirs, mp.mp_mirs)  -- merge specialized MIRs in
  6. fix_all_field_offsets(mirs, struct_map)
  7. cg_program(mirs, struct_map, rt_set)  -- struct-aware C codegen
```

The same flow exists in `build_test_program`.

### What's Working

The pass **runs** on every build. It just doesn't find anything to specialize because:

1. **No polymorphic user functions in practice.** The compiler itself is gen=1 code that doesn't use generics. Example programs that define types (gstats, gbuild, gwm, sheet, fsm, prolog) use concrete types, not polymorphic ones.

2. **`mono_is_poly` requires `ty_forall`.** Only functions whose inferred type is wrapped in `∀` (let-polymorphism) trigger specialization. Most functions have concrete types after inference.

3. **Struct codegen works independently.** The `struct_map` and `cg_*2` functions generate typed code for any `type` definition, even without monomorphization. A program with `Point = {x: I, y: I}` gets `typedef struct` codegen today.

### What's Not Working / Missing

1. **Specializations are in-memory only.** The mono pass produces MIR that gets compiled in the same build but nothing is persisted to the database. This is the correct approach: writing specializations back to the database during compilation creates a chicken-and-egg problem (the compiler being compiled can't use definitions produced by the compilation in progress), and the invalidation machinery for cached specializations isn't worth the complexity.

2. **Enum monomorphization is incomplete.** `mono_type_name` handles `ty_int`, `ty_str`, `ty_bool`, `ty_float`, `ty_array`, `ty_map`, `ty_opt` but doesn't handle `ty_res` (Result) or user-defined enum types. Enum variants still use the generic `GVal* [tag | payload...]` layout.

3. **Function signatures stay `GVal`.** Even when a function is specialized, its C signature is still `GVal fn(GVal, GVal)`. The struct-awareness only applies to local variables inside the function body, not at call boundaries. This means the C compiler can't check argument types across function calls.

4. **No specialization of stdlib/library functions.** When a user program calls `map(arr, fn)` from stdlib, the call isn't specialized because stdlib is linked as pre-compiled gen=1 code. Higher-order functions like `map`/`filter`/`fold` are the prime candidates for monomorphization but aren't reached.

---

## Where the Code Lives

### Self-Hosted Compiler (`glyph.glyph`)

| Area | Key Functions | Count |
|------|--------------|-------|
| Mono pass | `mono_pass`, `mono_collect*`, `mono_coll_*`, `mono_compile_*` | 37 fn |
| Struct map | `build_full_struct_map`, `build_struct_map`, `expand_with_generics`, `bsm_loop`, `bgsm_loop` | ~12 fn |
| Generic expansion | `read_all_generic_types`, `collect_transitively`, `instantiate_generic`, `ctr_scan_*`, `expand_recursive_safe` | ~10 fn |
| Struct codegen | `cg_all_typedefs`, `cg_field_stmt2`, `cg_record_aggregate2`, `cg_record_update`, `cg_struct_stores_direct` | ~15 fn |
| Local type inference | `build_local_types`, `blt_scan_*`, `blt_propagate_*`, `blt_collect_*`, `blt_tag_by_fa`, `blt_find_struct` | ~18 fn |
| Field offset fixing | `fix_all_field_offsets`, `fix_offs_mirs`, `build_type_reg`, `seed_reg_from_smap` | ~8 fn |

### Rust Compiler (`crates/`)

- `crates/glyph-mir/src/mono.rs` — minimal `MonoContext` struct (skeleton, not a full monomorphizer)
- `crates/glyph-db/src/queries.rs` — `effective_defs(target_gen)` selects highest gen <= target per (name, kind)
- `crates/glyph-codegen/src/layout.rs` — proper C-style struct alignment computation
- `crates/glyph-codegen/src/cranelift.rs` — `resolve_field_offset` with complete type registry and "prefer largest" disambiguation
- `crates/glyph-parse/src/ast.rs` — `TypeDef`, `TypeBody::Record`, `TypeBody::Enum`, `Field` AST types
- `crates/glyph-mir/src/ir.rs` — `MirType::Record(BTreeMap)`, `MirType::Enum(name, variants)`, `MirType::Named(name)`

---

## How Monomorphization Works (Detail)

### Phase 1: Collection

For each function `f` in the program:
1. Walk `f`'s AST looking for call expressions (`ex_call`)
2. For each call to function `g`:
   - Check if `g` is polymorphic (`mono_is_poly` — is its type `ty_forall`?)
   - If yes, get the concrete type at this call site from the type map
   - Build a `var_map`: pairs of `[type_variable_root, concrete_type_index]`
   - Generate a specialization name: `g__int_str` (original name + type suffix)
   - Record the spec: `{ms_orig: "g", ms_spec: "g__int_str", ms_var_map: [...], ms_caller_fns: ["f"], ms_caller_nis: [call_node_index]}`

### Phase 2: Compilation

For each specialization spec:
1. Find the original function's parsed AST
2. Build a specialized type map by substituting type variables with concrete types
3. Re-lower the function to MIR using the specialized type map
4. The result is a new MIR function with the specialized name

### Phase 3: Call Rewriting

When compiling the *caller* function:
1. Build a call map: `{call_node_index → specialized_name}`
2. During MIR lowering, `lower_call` checks `mono_lookup_call(tctx, call_ni)`
3. If a specialization exists, the callee operand is replaced with `mk_op_func(specialized_name)`
4. The original polymorphic function still exists (for other unspecialized call sites)

### Struct Map Flow

```
type definitions (from def table WHERE kind='type')
    ↓
build_struct_map:  skip generics, parse fields + ctypes
    ↓
expand_with_generics:  find concrete applications of generics, instantiate
    ↓
struct_map = [ [name, [field_names...], [c_types...]], ... ]
    ↓
cg_all_typedefs:  typedef struct { ... } Glyph_Name;
    ↓
build_local_types per function:  local_struct_types[i] = "Name" or ""
    ↓
cg_field_stmt2:    ((Glyph_Name*)ptr)->field     (vs generic ((GVal*)ptr)[offset])
cg_record_aggregate2:  sizeof(Glyph_Name)        (vs nfields * 8)
cg_record_update:  memcpy + typed field update    (vs generic rebuild)
```

---

## What Would Full Monomorphization Enable

### Near-term (incremental, using existing infrastructure)

1. **Typed function signatures.** Instead of `GVal f(GVal a, GVal b)`, generate `Glyph_Point f(int64_t a, Glyph_Point b)`. The struct_map already knows the field types; extending this to parameter/return types would let `cc` catch argument mismatches.

2. **Enum typedef generation.** Currently only records get `typedef struct`. Enums could get tagged-union typedefs: `typedef struct { int64_t tag; union { ... } data; } Glyph_Event;`. This eliminates manual tag/payload offset arithmetic.

3. **Stack allocation for small structs.** Records matching a known struct type could be stack-allocated instead of heap-allocated when they don't escape the function. The struct_map already identifies which locals hold struct values.

### Medium-term (requires new work)

4. **Specialize stdlib functions.** When `map(arr, fn)` is called with `[Point] -> (Point -> Point)`, generate `map__Point_Point` that operates on `Glyph_Point*` arrays. This is where the mono pass becomes most valuable — higher-order functions in stdlib are inherently polymorphic.

5. **Cross-function type propagation.** Currently `build_local_types` infers struct types within a single function. Extending this across call boundaries (using the type checker's cross-function inference results) would mean more locals get typed.

6. **Remove `-w` flag.** Once enough of the generated C is type-safe, the warning suppression flags can be removed, making the C compiler an active safety net rather than a silenced one.

### Long-term (architectural)

7. **Unboxed values.** With concrete types known, small structs (2-3 fields) could be passed by value instead of by pointer. This eliminates heap allocation for temporary records entirely.

8. **LLVM type metadata.** The LLVM IR backend currently uses `i64` for everything. With monomorphized types, it could emit proper struct types, enabling LLVM's optimization passes (SROA, mem2reg) to work on struct fields directly.

---

## Reliability Impact

The bugs that monomorphization would prevent are among the hardest and most frequent in Glyph's history:

| Bug Class | Examples | How Mono Prevents It |
|-----------|----------|---------------------|
| Field offset errors | AstNode vs JNode both have `sval`, wrong type picked | Typed access `->field` instead of `[offset]` — C compiler resolves |
| Discriminant inversion | `?`/`!` operator had swapped branch targets | Typed enum access — wrong tag is a type error |
| GVal type confusion | String pointer treated as integer | Distinct C types — `Glyph_Point*` vs `int64_t` |
| Argument swapping | Two `GVal` args in wrong order | Distinct parameter types in signature |
| Null pointer dereference | Accessing field on uninitialized `GVal` | Typed pointers enable `-Wnull-dereference` |

The `-w` flag currently hides all of these. Monomorphization is the path to removing it.

---

## 13 Type Definitions in the Compiler

The compiler's own type definitions are the first candidates for improved struct codegen:

| Type | Fields | Used By |
|------|--------|---------|
| `AstNode` | kind, ival, sval, n1, n2, n3, ns | Parser, lowering, mono pass |
| `Token` | kind, start, end, line | Tokenizer, parser |
| `ParseResult` | node, pos | Parser (every parse function returns this) |
| `JNode` | items, keys, nval, sval, tag | JSON subsystem, MCP server |
| `Point2D` | x: i32, y: i32 | Test/example (first C-typed struct) |
| `FPoint` | fx: f64, fy: f64 | Test/example (float struct) |
| `FPoint32` | fx: f32, fy: f32 | Test/example |
| `StrBox` | val: S | Test (boxed string) |
| `Color` | Red(I), Green(I), Blue(I) | Test (enum) |
| `Dir` | North, South, East, West | Test (fieldless enum) |
| `Light` | LOff, LOn(I), LDim(I) | Test (mixed enum) |
| `StrComp` | SComp(StrBox) | Test (enum wrapping struct) |
| `StrTag` | SVal(S) | Test (enum wrapping string) |

`AstNode`, `Token`, `ParseResult`, and `JNode` are used pervasively. Making their field accesses typed would cover a large fraction of the compiler's code.

---

## Example Programs Using Types

| Program | Types | Notes |
|---------|-------|-------|
| gstats | Stats, Config | First gen=2 example with named records |
| gbuild | Task, BuildResult | Build system with typed task graph |
| gwm | Client, WmColors | X11 window manager with 7-field records |
| sheet | Sheet | Spreadsheet with typed cell storage |
| fsm | Event, State | ATM state machine (enum types) |
| prolog | Tag | Prolog interpreter (8-variant enum) |

These programs already benefit from `typedef struct` codegen via the struct_map. Monomorphization would extend this to their polymorphic function calls (e.g., `map` over `[Stats]`).

---

## Implementation Plan

### Phase 1: Typed Struct Locals

When `build_local_types` identifies a local's struct type, declare it as `Glyph_Name* _N = 0;` instead of `GVal _N = 0;`. Zero changes to function boundaries — only local declarations within function bodies. The struct-aware statement codegen (`cg_field_stmt2`, `cg_record_aggregate2`) already generates typed pointer casts; this makes the variable declaration match so `cc` can verify consistency.

**Modify:** `cg_locals_decl` (pass in `local_struct_types`, check for known type), `cg_function2` (thread `local_struct_types` to `cg_locals_decl`).

### Phase 2: Typed Function Parameters

When the type checker resolves a function parameter to a known struct type, emit `Glyph_Name*` instead of `GVal` in the signature. Insert casts at call boundaries where typed and untyped functions interact.

**Modify:** `cg_params_list`, `cg_forward_decls`, `cg_call_stmt`. **New:** `param_type_map` builder from `tc_results`.

### Phase 3: Typed Return Values

Extend Phase 2 to return types. When a function's return type is a known struct, emit `Glyph_Name*` as the return type.

**Modify:** `cg_function2`, `cg_term`, `cg_call_stmt`, forward declarations.

### Phase 4: Enum Typedef Generation

Generate tagged union typedefs for enum types. Fieldless enums keep the existing tagged-integer representation.

**New:** `cg_enum_typedefs`. **Modify:** `cg_variant_aggregate`, `cg_field_stmt` (for typed enum access). Can be done in parallel with Phases 2-3.

### Phase 5: Activate the Mono Pass

Make the 37 existing `mono_*` functions produce specializations. Fix `mono_type_name` for missing type tags (`ty_res`, `ty_record`, `ty_named`, `ty_fn`). Ensure library function ASTs are available for specialization when programs use `glyph use stdlib.glyph`.

### Phase 6: Remove Warning Suppression

Incrementally remove flags: `-w` first, then `-Wno-incompatible-pointer-types`, then `-Wno-int-conversion`. Fix what surfaces at each step. This is the end goal.

### Verification (after each phase)

1. `ninja` — full 3-stage bootstrap succeeds
2. `./glyph test glyph.glyph` — all tests pass
3. Inspect `/tmp/glyph_out.c` for expected typed output
4. Build example programs with types (gstats, fsm, gbuild)
