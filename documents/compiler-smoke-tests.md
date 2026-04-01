# Glyph Compiler Smoke Test Program

## 1. Overview

A dedicated `.glyph` program (`tests/smoke/smoke.glyph`) designed to stress-test the Glyph compiler beyond what the internal unit tests and `documentation.glyph` cover. The program uses `glyph use glyph.glyph` to access compiler internals directly, enabling white-box testing of each pipeline stage, plus black-box end-to-end tests that compile and run generated programs.

**Goals:**
- Find latent bugs before they surface in real programs
- Stress edge cases in the type checker, parser, MIR lowering, and codegen
- Verify cross-stage correctness (a valid AST produces valid MIR produces correct C)
- Serve as a regression gate for compiler changes
- Complement (not replace) the fuzzing system described in `documents/fuzzing.md`

**Non-goals:**
- Replacing the compiler's internal unit tests (those test stage internals in isolation)
- Replacing `documentation.glyph` (that tests language features from a user perspective)
- Performance benchmarking (see `examples/benchmark/`)

## 2. Architecture

### 2.1 Project Structure

```
tests/smoke/
  smoke.glyph          # the smoke test program (SQLite database)
  run_smoke.sh         # convenience wrapper: build + test + report
```

### 2.2 Dependency Model

```
smoke.glyph
  glyph use glyph.glyph        # compiler internals (tokenizer, parser, typeck, MIR, codegen)
  glyph use libraries/stdlib.glyph   # stdlib utilities (if needed for helpers)
```

When `glyph test smoke.glyph` runs, the build system:
1. Reads smoke.glyph's own definitions (test + helper functions)
2. Reads all definitions from glyph.glyph via `lib_dep`
3. Compiles everything together into a test binary
4. Executes the test binary

### 2.3 Build Time Considerations

Compiling with glyph.glyph as a dependency means ~1,774 extra definitions. Current test suite takes ~67s for 408 tests. Expected smoke test build time: **60-90 seconds**. Mitigation strategies:

- **Batch tests**: Run all smoke tests in a single build rather than iterating
- **Selective runs**: `./glyph test smoke.glyph smoke_tc_cyclic smoke_parse_deep` for targeted debugging
- **Separate from CI gate**: Smoke tests run as a separate step, not blocking every commit

### 2.4 Test Naming Convention

All test definitions use a `smoke_` prefix with a stage abbreviation:

| Prefix | Stage | Example |
|--------|-------|---------|
| `smoke_tok_` | Tokenizer | `smoke_tok_long_ident` |
| `smoke_parse_` | Parser | `smoke_parse_deep_match` |
| `smoke_tc_` | Type checker | `smoke_tc_cyclic_record` |
| `smoke_mir_` | MIR lowering | `smoke_mir_nested_closure` |
| `smoke_cg_` | C codegen | `smoke_cg_many_fields` |
| `smoke_ll_` | LLVM backend | `smoke_ll_record_update` |
| `smoke_e2e_` | End-to-end | `smoke_e2e_polymorphic` |

## 3. Testing Approach by Compiler Stage

### 3.1 Tokenizer Stress Tests

The tokenizer is mature but has boundary conditions worth exercising:

**API entry point:** `tokenize(src: S) -> [Token]`

| Test | What it stresses | Why |
|------|-----------------|-----|
| Very long identifiers (500+ chars) | Buffer handling | No existing test exceeds ~30 chars |
| Deeply nested string interpolation `"a {b {"c {d}"} e}"` | Interpolation state machine | Only simple interpolation tested |
| Many consecutive operators `+ - * / + - * /...` | Token dispatch chain | tok_one split assumes bounded chains |
| Mixed indentation (tabs + spaces) | Indent tracking | Only consistent indentation tested |
| Source with 100+ lines | Newline/indent tracking at scale | Most tests are 1-5 lines |
| Empty source string | Edge case | Not explicitly tested |
| String with every escape sequence | `tok_escape` completeness | Tested individually, not combined |
| Hex literals at boundary values `0xFFFFFFFFFFFFFFFF` | Number parsing | Only small hex tested |
| Raw string containing `"` and `\` | Raw string escaping | Lightly tested |
| Comment-heavy source (more comments than code) | Comment skipping | Trivial comments tested |

### 3.2 Parser Stress Tests

The parser has good coverage but lacks depth/complexity tests.

**API entry point:** `parse_fn_def(src: S) -> ParseResult`

| Test | What it stresses | Why |
|------|-----------------|-----|
| 10-level nested match expressions | Recursive descent depth | Max tested is 2-3 levels |
| Match with 30+ arms | Arm array scaling | Max tested is ~8 arms |
| Match with guards calling functions | Guard expression complexity | Only simple guards tested |
| Or-pattern with 10+ alternatives | Or-pattern array | Max tested is 3 |
| Or-pattern combined with guard | Feature interaction | Untested combination |
| Lambda returning lambda returning lambda (3 levels) | Nested lambda parsing | Max 1 level tested |
| 10+ chained pipe operators `x \|> f \|> g \|> h...` | Pipe parsing loop | Max 3 tested |
| Block with 20+ let bindings | Block parsing | Max ~5 tested |
| Record literal with 15+ fields | Record parsing | Max ~7 fields tested |
| Record update chain `r{a:1}{b:2}{c:3}` | Record update parsing | Untested |
| Let destructuring with 8+ fields | Destructuring parsing | Max 2-3 fields tested |
| String interpolation with complex expressions `"{if x then y else z}"` | Interp expression parsing | Only simple exprs tested |
| Function with 10+ parameters | Parameter list parsing | Max ~5 tested |
| Deeply nested if/else (8+ levels) | If/else nesting | Max 2-3 tested |
| Array literal with 50+ elements | Array literal parsing | Max ~10 tested |
| Binary expression with 15+ operators (precedence stress) | Precedence climbing | Max ~5 tested |

### 3.3 Type Checker Stress Tests

**The highest-value target.** BUG-005 through BUG-008 all involved the type checker. The depth-limit cycle detection (depth=30) may mask real issues.

**API entry points:**
- `tc_make_engine() -> TyEngine`
- `infer_fn(eng, name, params, body_nodes) -> TyEngine`
- `unify(eng, t1, t2) -> TyEngine`
- `pool_add(eng, node) -> {eng, idx}`

| Test | What it stresses | Why |
|------|-----------------|-----|
| Polymorphic function used at 5+ different types | Instantiation/generalization | Most tests use 1-2 types |
| `id(id)(42)` — polymorphic applied to itself | Higher-rank-like inference | Classic HM stress case |
| Mutually recursive functions with complex return types | Forward reference + cycle detection | Only simple mutual recursion tested |
| Record with 20+ fields | Type pool scaling | Max ~8 fields tested |
| Nested optional-of-array-of-record `?[{x:I, y:I}]` | Type constructor nesting | Untested depth |
| Function returning function returning function | Curried function inference | Lightly tested |
| Unification of two large record types | Row unification scaling | Only small records tested |
| Type error in deeply nested expression | Error reporting depth | Errors tested at top level |
| Record type with row variable accessed at multiple sites | Row polymorphism | Basic row tested |
| Occurs check on genuinely cyclic type `f x = f` | Cycle detection correctness | Depth limit may mishandle |
| 50+ type variables in scope | Pool scaling | Max ~15 in practice |
| Shadowed variable with different type in nested scope | Environment push/pop | Not explicitly tested |
| Generic function passed as argument to another generic | Polymorphism interaction | Untested |
| Chained `?` unwrap with type narrowing | Optional type inference | Lightly tested |

### 3.4 MIR Lowering Stress Tests

**API entry point:** `lower_fn_def(name, params, nodes, tmap) -> MirResult`

| Test | What it stresses | Why |
|------|-----------------|-----|
| Closure capturing 8+ variables | Capture analysis scaling | Max ~3 tested |
| Nested closure (closure inside closure) | Capture propagation | Not tested in compiler |
| For loop over array of records with field access in body | Loop + field interaction | Simple loops only |
| Match inside for loop inside match | Control flow nesting | Not combined |
| String interpolation with 10+ parts | Interp lowering array | Max ~3 parts tested |
| Let destructuring inside match arm | Feature interaction | Untested combination |
| Guard expression with side effects (function calls) | Guard lowering complexity | Simple guards only |
| Record update inside for loop | Loop + record interaction | Not combined |
| Function with 20+ local variables | Block/local allocation | Max ~8 locals tested |
| Chained pipe with lambdas `x \|> (\y -> y+1) \|> (\z -> z*2)` | Pipe + closure lowering | Untested |

### 3.5 C Codegen Stress Tests

**API entry points:**
- `cg_program(mirs: [MirResult]) -> S`
- `cg_function(mir: MirResult) -> S`

| Test | What it stresses | Why |
|------|-----------------|-----|
| Record with 20+ fields — create, access, update | Struct typedef + field offset | Max ~8 fields |
| Enum with 10+ variants, each with payload | Tag dispatch scaling | Max 4 variants tested |
| Closure passed to closure passed to closure | Indirect call chain | Max 1 level tested |
| String literal with all C-unsafe characters | `cg_escape` completeness | Tested individually |
| Function with 50+ MIR blocks (complex control flow) | Block label management | Max ~15 blocks |
| `#ifdef GLYPH_DEBUG` paths exercised in debug build | Debug mode codegen | Not explicitly tested |
| Array of closures | Array + closure interaction | Untested |
| Record update on struct with 15+ fields | memcpy size calculation | Recently fixed, needs regression |

### 3.6 LLVM Backend Stress Tests

The LLVM backend is newer and less tested than C codegen. Record update was recently broken.

**API entry point:** `ll_program(mirs: [MirResult]) -> S`

| Test | What it stresses | Why |
|------|-----------------|-----|
| Record update on struct with many fields | `ll_emit_record_update` (recently fixed) | Regression coverage |
| Closure with captured variables used in match | LLVM closure + match interaction | Untested |
| Large function with 30+ basic blocks | LLVM block label management | Max ~10 tested |
| Float arithmetic chain | LLVM float ops | Lightly tested |
| String operations (concat, compare, interpolation) | LLVM string runtime calls | Basic tests only |
| Enum construction and matching | LLVM tag dispatch | Basic tests only |
| Deeply nested field access `a.b.c.d` | LLVM GEP chain | Max 1 level tested |

### 3.7 End-to-End Integration Tests

These tests run the full pipeline and verify output correctness by examining the generated code string (not by executing it, since the smoke test runs inside the same compiler process).

**Approach:** Use `compile_fn` to get MIR, then `cg_program`/`ll_program` to get C/LLVM IR text. Assert on expected patterns in output.

| Test | What it verifies | Why |
|------|-----------------|-----|
| Polymorphic `map` function used with int and string arrays | Monomorphization produces two specializations | Polymorphism + codegen interaction |
| Recursive fibonacci with match and guards | Full pipeline on real algorithm | Cross-stage consistency |
| Program using closures, records, and arrays together | Feature interaction | Individual features tested, not combined |
| Record update followed by field access on updated field | Update correctness through pipeline | Recently broken in LLVM |
| For loop collecting results into array with push | Loop desugaring + array codegen | Common pattern, not integration-tested |
| String interpolation with int, float, and string parts | Type-dependent interpolation lowering | BUG-006 regression |
| Match with or-patterns and guards on enum | Three features interacting | Untested combination |

## 4. Specific Bug Regression Tests

Each historical bug gets at least one targeted regression test:

### BUG-005: pool_get Type Disambiguation

```
smoke_tc_pool_get_tag — Verify that accessing TyNode fields after pool_get
  produces correct values. Create a type node with known tag/n1/n2/sval,
  add to pool, retrieve via pool_get, verify all fields read correctly.
```

### BUG-006: String Interpolation Type

```
smoke_mir_interp_str_type — Lower a string interpolation expression where
  the interpolated value is already a string. Verify the MIR does NOT
  contain an int_to_str call for that operand.
```

### BUG-007: Generalization Order

```
smoke_tc_gen_order — Define two functions where B calls A. Infer A first,
  then B. Verify B correctly instantiates A's polymorphic type rather
  than seeing unresolved type variables.
```

### BUG-008: Cyclic Type Detection

```
smoke_tc_cyclic_record — Create a type variable, unify it with a record
  containing itself. Verify subst_resolve terminates (doesn't hang or crash)
  and the depth limit kicks in correctly.

smoke_tc_cyclic_mutual — Simulate mutually recursive function types.
  Verify tc_collect_fv and inst_type handle cycles without infinite recursion.
```

### LLVM Record Update Regression

```
smoke_ll_record_update_large — Create a MirResult representing a record
  update on a struct with 10+ fields. Generate LLVM IR via ll_program.
  Verify the output contains a memcpy of the full struct size, not just
  the updated fields.
```

### Closure Capture Ordering

```
smoke_mir_capture_order — Lower a closure that captures variables a, b, c
  in that order. Verify the captured variable array in MIR preserves
  the correct ordering (was reversed in a past bug).
```

### Float Coercion

```
smoke_e2e_float_arith — Compile a function that performs float arithmetic
  on an extern return value. Verify the generated C uses float operations
  (not integer) without the + 0.0 workaround.
```

## 5. Stress and Boundary Tests

Tests designed to push limits rather than test specific features:

### Scale Tests

| Test | Parameters | Purpose |
|------|-----------|---------|
| `smoke_scale_many_params` | Function with 15 parameters | Parameter passing limits |
| `smoke_scale_many_locals` | Function with 40 let bindings | Local variable allocation |
| `smoke_scale_many_arms` | Match with 50 arms | Match dispatch scaling |
| `smoke_scale_many_fields` | Record with 30 fields | Struct layout scaling |
| `smoke_scale_deep_nesting` | 15-level nested if/else | Recursive lowering depth |
| `smoke_scale_many_closures` | 10 closures in one function | Closure environment scaling |
| `smoke_scale_long_pipe` | 20-stage pipe chain | Pipe desugaring |
| `smoke_scale_big_array` | Array literal with 100 elements | Array literal codegen |

### Boundary Value Tests

| Test | Input | Purpose |
|------|-------|---------|
| `smoke_bound_empty_str` | `""` as function body | Empty string handling |
| `smoke_bound_empty_array` | `[]` operations | Empty array edge cases |
| `smoke_bound_zero_args` | Zero-argument function calls | Call convention edge case |
| `smoke_bound_single_arm` | Match with exactly one arm | Minimal match |
| `smoke_bound_empty_record` | `{}` record literal | Empty struct |
| `smoke_bound_max_int` | `9223372036854775807` literal | I64 max |
| `smoke_bound_neg_int` | `-9223372036854775808` literal | I64 min |

### Feature Interaction Tests

These test combinations that individually work but haven't been tested together:

| Test | Combination | Why risky |
|------|------------|-----------|
| `smoke_combo_closure_in_match` | Closure defined inside match arm | Capture analysis + match lowering |
| `smoke_combo_guard_with_closure` | Match guard that calls a closure | Guard eval + indirect call |
| `smoke_combo_destr_in_for` | Let destructuring inside for loop | Destructuring + loop |
| `smoke_combo_interp_in_match` | String interpolation in match arm body | Interpolation + match |
| `smoke_combo_pipe_closure_record` | Pipe into closure returning record | Three features interacting |
| `smoke_combo_or_pattern_guard` | Or-pattern combined with guard | Both are newer features |
| `smoke_combo_nested_record_update` | Update field that is itself a record | Record nesting |
| `smoke_combo_array_of_records_for` | For loop over array of records | Common real-world pattern |
| `smoke_combo_closure_returning_closure` | Closure that returns another closure | Nested environments |
| `smoke_combo_match_in_interp` | Match expression inside string interpolation | Parser + lowering interaction |

## 6. Relationship to Existing Test Infrastructure

```
                          ┌─────────────────────┐
                          │   glyph.glyph        │
                          │   408 internal tests  │
                          │   (unit tests per     │
                          │    compiler stage)     │
                          └─────────┬─────────────┘
                                    │ glyph use
                          ┌─────────▼─────────────┐
                          │   smoke.glyph          │
                          │   ~80-120 stress tests  │
                          │   (white-box +          │
                          │    integration)          │
                          └─────────────────────────┘

  ┌──────────────────────┐     ┌──────────────────────┐
  │  documentation.glyph  │     │  glyph fuzz           │
  │  49 language tests    │     │  (continuous, random   │
  │  (black-box, user-    │     │   input generation)    │
  │   facing features)    │     │  (not yet implemented) │
  └──────────────────────┘     └──────────────────────┘
```

**Complementary roles:**
- **glyph.glyph tests**: "Does each compiler function work correctly in isolation?"
- **smoke.glyph**: "Do the stages work together on complex/extreme inputs?"
- **documentation.glyph**: "Do language features work from the user's perspective?"
- **glyph fuzz** (future): "Can random inputs crash the compiler?"

## 7. Implementation Plan

### Phase 1: Foundation (~15 definitions)

Set up the smoke test database, helper utilities, and first round of tests targeting the highest-value area (type checker).

1. Create `tests/smoke/smoke.glyph` via `glyph init`
2. `glyph use smoke.glyph ../../glyph.glyph`
3. Add helper functions:
   - `smoke_parse(src)` — convenience: tokenize + parse, return ParseResult
   - `smoke_infer(src)` — convenience: tokenize + parse + infer, return TyEngine
   - `smoke_lower(src)` — convenience: full pipeline to MirResult
   - `smoke_compile_c(src)` — convenience: full pipeline to C string
   - `smoke_compile_ll(src)` — convenience: full pipeline to LLVM IR string
   - `smoke_assert_no_errors(eng)` — assert TyEngine has no errors
   - `smoke_assert_has_error(eng)` — assert TyEngine has at least one error
4. Type checker stress tests (8-10 tests):
   - `smoke_tc_cyclic_record`
   - `smoke_tc_cyclic_mutual`
   - `smoke_tc_poly_multi_use`
   - `smoke_tc_gen_order`
   - `smoke_tc_many_vars`
   - `smoke_tc_nested_generics`
   - `smoke_tc_row_poly`
   - `smoke_tc_shadowing`
   - `smoke_tc_pool_get_tag`

### Phase 2: Parser + MIR (~20 definitions)

5. Parser stress tests (8-10 tests):
   - `smoke_parse_deep_match`
   - `smoke_parse_many_arms`
   - `smoke_parse_nested_interp`
   - `smoke_parse_long_pipe`
   - `smoke_parse_many_params`
   - `smoke_parse_deep_if`
   - `smoke_parse_record_chain`
   - `smoke_parse_complex_guard`
6. MIR lowering stress tests (8-10 tests):
   - `smoke_mir_many_captures`
   - `smoke_mir_nested_closure`
   - `smoke_mir_complex_match`
   - `smoke_mir_interp_str_type`
   - `smoke_mir_capture_order`
   - `smoke_mir_destr_in_match`
   - `smoke_mir_loop_with_records`
   - `smoke_mir_many_blocks`

### Phase 3: Codegen + Integration (~25 definitions)

7. C codegen stress tests (6-8 tests):
   - `smoke_cg_many_fields`
   - `smoke_cg_many_variants`
   - `smoke_cg_closure_chain`
   - `smoke_cg_all_escapes`
   - `smoke_cg_array_of_closures`
   - `smoke_cg_record_update_large`
8. LLVM stress tests (4-6 tests):
   - `smoke_ll_record_update_large`
   - `smoke_ll_closure_match`
   - `smoke_ll_many_blocks`
   - `smoke_ll_float_chain`
9. End-to-end integration tests (6-8 tests):
   - `smoke_e2e_polymorphic`
   - `smoke_e2e_closure_record_array`
   - `smoke_e2e_match_guard_or`
   - `smoke_e2e_float_arith`
   - `smoke_e2e_interp_all_types`
   - `smoke_e2e_recursive_adt`

### Phase 4: Scale + Boundary (~20 definitions)

10. Scale tests (8 tests)
11. Boundary value tests (7 tests)
12. Feature interaction / combo tests (10 tests from Section 5)

### Estimated Totals

| Phase | Helpers | Tests | Total Defs |
|-------|---------|-------|------------|
| 1 | 7 | 9 | 16 |
| 2 | 0 | 18 | 18 |
| 3 | 0 | 18 | 18 |
| 4 | 0 | 25 | 25 |
| **Total** | **7** | **~70** | **~77** |

## 8. Running the Smoke Tests

```bash
# Full suite
./glyph test tests/smoke/smoke.glyph

# Specific stage
./glyph test tests/smoke/smoke.glyph smoke_tc_cyclic_record smoke_tc_poly_multi_use

# All type checker smoke tests (via find)
./glyph find tests/smoke/smoke.glyph smoke_tc_ | xargs ./glyph test tests/smoke/smoke.glyph
```

### CI Integration

```bash
# In build.ninja or CI script
ninja test                           # existing: compiler + documentation tests
./glyph test tests/smoke/smoke.glyph # new: smoke tests (separate step)
```

Smoke tests should pass before any release but need not block every commit (given the 60-90s build time).

## 9. Success Criteria

The smoke test program will be considered successful when:

1. **Bug discovery**: At least 2-3 latent bugs found during initial development
2. **Regression catching**: Catches breakage that internal unit tests miss
3. **Coverage complement**: Exercises code paths that `glyph test glyph.glyph` does not (verifiable via `--cover`)
4. **Stable runtime**: Full suite runs in under 120 seconds
5. **Maintainability**: New compiler features can be smoke-tested by adding 1-2 definitions, not restructuring the suite

## 10. Open Questions

1. **`glyph use` with circular paths**: smoke.glyph uses glyph.glyph, but glyph.glyph is what compiles smoke.glyph. Does the build correctly handle this? Need to verify the `lib_dep` resolution doesn't create issues.

2. **Type definition conflicts**: If smoke.glyph defines a type `T` and glyph.glyph also has type definitions, do they collide? Need to verify the namespace isolation in `read_lib_defs`.

3. **Test definition filtering**: When running `glyph test smoke.glyph`, does the test binary include glyph.glyph's 360 tests too? If so, we need filtering. The `smoke_` prefix handles this if test names are passed explicitly.

4. **Memory pressure**: Compiling ~1,850 definitions in one pass may stress the compiler's own memory usage. The Boehm GC should handle this, but worth monitoring.

5. **Black-box alternative**: Some tests (especially codegen correctness) might be better as separate small `.glyph` programs that the smoke harness compiles and runs via `glyph_system`. This avoids examining generated code strings and instead checks runtime behavior. Worth exploring as Phase 5.
