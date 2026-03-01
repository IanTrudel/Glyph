# Asteroids Feasibility Reassessment

**Previous assessment**: `documents/asteroids-feasibility.md`
**Date**: 2026-02-28
**Context**: Three of the eight gaps identified in the original assessment have been fully addressed. This document re-evaluates the feasibility of an Asteroids game in Glyph.

---

## Changes Since Original Assessment

### Gap 1: Float Support — ~~NOT WORKING~~ → COMPLETE

**Added**: ~30 definitions across tokenizer, parser, MIR lowering, and C codegen.

- **Tokenizer**: `tok_one4` scans `3.14` as `tk_float` (detects `.` followed by digit after integer part)
- **Parser**: `ex_float_lit` AST node stores float text in `sval` (not `ival`, since GVal can't hold a double directly)
- **MIR**: `ok_const_float` operand kind, `is_float_op` detection (mirrors string operator pattern), `lower_float_binop` with arithmetic/comparison dispatch
- **C codegen**: Bitcast helpers `_glyph_i2f(GVal→double)` and `_glyph_f2i(double→GVal)` via `memcpy`. Float arithmetic emits `_glyph_f2i(_glyph_i2f(a) + _glyph_i2f(b))`. Comparisons emit `(_glyph_i2f(a) < _glyph_i2f(b)) ? 1 : 0`.
- **Runtime**: `int_to_float`, `float_to_int`, `float_to_str`, `str_to_float` conversion functions
- **Tests**: `test_float_basic`, `test_float_cmp`, `test_float_convert` — all passing

**Limitation**: Mixed int/float arithmetic (e.g., `3.14 + 1`) requires explicit `int_to_float(1)`. No implicit coercion.

### Gap 2: Bitwise Operations — ~~MISSING ENTIRELY~~ → COMPLETE

**Added**: 5 keyword-style operators with full pipeline support.

- **Syntax**: `bitand`, `bitor`, `bitxor`, `shl`, `shr` (keyword operators, not symbol overloads)
- **Operator IDs**: `op_bitand=14`, `op_bitor=15`, `op_bitxor=16`, `op_shl=17`, `op_shr=18`
- **C codegen**: Emits native C `&`, `|`, `^`, `<<`, `>>`
- **Test**: `test_bitwise_ops` — all 5 operators verified

**Example**:
```
flags = VK_COLOR_BIT bitor VK_TRANSFER_BIT
masked = value bitand 0xFF
shifted = 1 shl 16
```

### Gap 3: C-Compatible Struct Layout — ~~IMPOSSIBLE~~ → COMPLETE (gen=2)

**Added**: ~10 definitions for C-type specifiers in struct field declarations.

- **Syntax**: Type annotations on struct fields: `Point2D = {x: i32, y: i32}`
- **Supported specifiers**: `i8`/`u8`/`i16`/`u16`/`i32`/`u32`/`i64`/`u64`/`f32`/`f64`/`ptr`
- **Generated C**: `typedef struct { int32_t x; int32_t y; } Glyph_Point2D;`
- **Auto-detection**: Any field with a C-type specifier triggers C-compatible layout for the entire struct
- **Reads**: Implicit widening to GVal
- **Writes**: Explicit narrowing casts `(int32_t)val`
- **Test**: `test_c_layout_struct` — passing

### Additional Features Since Assessment

Several other features have been added that improve the Asteroids picture:

- **Match guards**: `pat ? guard_expr -> body` — useful for game logic branching
- **Or-patterns**: `1 | 2 | 3 -> body` — cleaner match expressions
- **Closures in C codegen**: Lambda lifting, heap-allocated environments — useful for callbacks (though still incompatible with C function pointer ABI)
- **Build modes**: `--debug` (O0 -g), `--release` (O2), default (O1+debug) — release mode for game performance
- **Safety hardening**: Bounds checks, SIGSEGV handler with full stack traces — helpful during development

---

## Remaining Gaps

### Gap 4: Raw Memory / Sub-Word Writes — PARTIALLY ADDRESSED

**Status**: C-layout structs handle the struct packing problem for *defined types*. A `Vertex = {x: f32, y: f32, r: f32, g: f32, b: f32}` now generates a proper 20-byte C struct.

**Remaining issue**: `cg_struct_stores_typed` currently emits `(float)val` for f32 fields, where `val` is a GVal (intptr_t holding bitcast double). This casts the raw integer bits to float, not the double value. The correct emit would be `(float)_glyph_i2f(val)` — unwrap the bitcast double first, then narrow to float. **This is a bug that must be fixed before f32/f64 struct fields work correctly with float values.**

**Fix estimate**: ~1 modified definition (`cg_struct_stores_typed` — detect float field types and emit `_glyph_i2f` unwrap before the cast). Reads also need a corresponding fix to bitcast float fields back to GVal via `_glyph_f2i`.

**After fix**: Vertex buffers could be defined and populated directly from Glyph:
```
Vertex = {x: f32, y: f32, r: f32, g: f32, b: f32}

make_vertex px py cr cg cb =
  {x: px, y: py, r: cr, g: cg, b: cb}
```

### Gap 5: Math Functions — STILL MISSING (now easy to add)

**Status**: No `sin`, `cos`, `sqrt`, `atan2`, `fabs`, `pow` in the runtime. No `<math.h>` include.

**Changed situation**: With float support working, adding math functions is now straightforward. The bitcast infrastructure (`_glyph_i2f`/`_glyph_f2i`) is already emitted by `cg_runtime_float`. Math wrappers follow the same pattern:

```c
GVal glyph_sin(GVal v) { return _glyph_f2i(sin(_glyph_i2f(v))); }
GVal glyph_cos(GVal v) { return _glyph_f2i(cos(_glyph_i2f(v))); }
GVal glyph_sqrt(GVal v) { return _glyph_f2i(sqrt(_glyph_i2f(v))); }
GVal glyph_atan2(GVal y, GVal x) { return _glyph_f2i(atan2(_glyph_i2f(y), _glyph_i2f(x))); }
GVal glyph_fabs(GVal v) { return _glyph_f2i(fabs(_glyph_i2f(v))); }
GVal glyph_pow(GVal b, GVal e) { return _glyph_f2i(pow(_glyph_i2f(b), _glyph_i2f(e))); }
GVal glyph_floor(GVal v) { return _glyph_f2i(floor(_glyph_i2f(v))); }
GVal glyph_ceil(GVal v) { return _glyph_f2i(ceil(_glyph_i2f(v))); }
```

**Fix estimate**: 1 new definition (`cg_runtime_math`), 1 modified (`cg_runtime_full` to include it), 1 modified (`is_runtime_fn` chain — add `is_runtime_fn10` for math names), add `-lm` linker flag. ~3 definitions total.

**Workaround (available now)**: Write a C wrapper file with the above functions and link it manually. This works today since float bitcasting is functional.

### Gap 6: C Callback Interop — STILL REQUIRES TRAMPOLINES

**Status**: Unchanged. Glyph closures use `(closure_ptr, captures...)` calling convention, incompatible with C function pointers. GLFW/Vulkan callbacks still need C trampoline functions.

**Mitigation**: This is a well-understood pattern. The Life and gled examples already use C wrapper files for FFI. A trampoline for GLFW key input is ~5 lines of C.

### Gap 7: Game Loop — STILL TAIL RECURSION (adequate)

**Status**: Unchanged. No `while`/`loop` keyword. All iteration is tail recursion or `for` loops (which desugar to index-based MIR loops).

**Assessment**: This is adequate for Asteroids. The `--release` build mode (O2) ensures C compiler TCO converts tail-recursive game loops to jumps. The functional style is natural:

```
game_loop state =
  poll_events(0)
  dt = get_delta_time(0)
  new_state = update_physics(state, dt)
  render_frame(new_state)
  match should_close(0) == 0
    true -> game_loop(new_state)
    _ -> cleanup(0)
```

### Gap 8: Timing — STILL FFI WRAPPER (trivial)

**Status**: Unchanged, but a non-issue. The gstats example already demonstrates the pattern (`time()` via C wrapper). A `gettimeofday` or `clock_gettime` wrapper for microsecond precision is ~10 lines of C.

---

## Revised Architecture Options

### Option A: Pure Glyph — Still Not Feasible

Still blocked by: no math functions (easily fixable), C-layout struct float field bug, callback trampolines. The callback issue is fundamental — Glyph can't produce C-compatible function pointers.

### Option B: Thick C Wrapper — Still Available (less necessary)

Same as before, but with floats and bitwise ops available in Glyph, much more game logic can move out of C.

### Option C: Thin C Wrapper — NOW FEASIBLE (recommended)

This was "Fix Language Gaps First" in the original assessment. With 3 of 4 major gaps closed, only math functions remain as a real blocker — and that's ~3 definitions of work.

**Revised effort to reach Option C**:

| Gap | Original Estimate | Current Status | Remaining |
|-----|------------------|----------------|-----------|
| Float literals + arithmetic | ~25 defs | **DONE** | 0 |
| Bitwise operators | ~18 defs | **DONE** | 0 |
| C-layout struct float fields | (part of struct work) | Bug in f32/f64 stores | ~2 defs |
| Math runtime (sin/cos/sqrt) | ~2 defs | Not started | ~3 defs |
| While loop | ~10 defs | Not started | Not needed |
| **Total remaining** | **~55 defs** | | **~5 defs** |

After those ~5 definitions, the architecture becomes:

- **C wrapper** (~150-200 lines): GLFW init/teardown, Vulkan init/pipeline/swapchain, key callback trampolines, frame timing
- **Glyph** (everything else): Game loop, physics (float math with sin/cos), entity management, collision detection, vertex buffer population (via C-layout f32 structs), render command dispatch, scoring, level progression

### Option D: SDL2 Software Rendering — NOW EVEN EASIER

The original assessment noted SDL2's integer-based 2D drawing API sidesteps floats entirely. With floats now available, SDL2 becomes even more attractive — Glyph can do float physics internally and convert to integer pixel coordinates for SDL draw calls. Only a minimal C wrapper for SDL init/teardown is needed.

**SDL2 remaining gaps**: Math functions only (~3 defs). Bitwise ops for SDL flags are already available.

---

## Revised Feasibility Summary

| Capability | Original | Now | Notes |
|-----------|----------|-----|-------|
| Float arithmetic | Fatal gap | **Working** | Bitcast via memcpy, full operator support |
| Bitwise ops | Fatal gap | **Working** | Keyword syntax: `bitand`, `bitor`, `shl`, etc. |
| C struct layout | Fatal gap | **Working*** | *f32/f64 field store bug needs fix |
| Math functions | Missing | Missing (easy) | ~3 defs, or FFI wrapper today |
| C callbacks | Trampoline | Trampoline | Fundamental — won't change |
| Game loop | Tail recursion | Tail recursion | Adequate with -O2 TCO |
| Timing | FFI wrapper | FFI wrapper | Trivial |

**Bottom line**: The original assessment concluded Glyph needed ~55 new definitions before Asteroids was practical. Three focused sessions later, ~45 of those definitions have been implemented. The remaining ~5 definitions (math runtime + struct float field fix) represent less than a single session of work. **Asteroids in Glyph with a thin C wrapper is now within immediate reach.**

The recommended path:
1. Fix `cg_struct_stores_typed` for f32/f64 field bitcasting (~30 min)
2. Add `cg_runtime_math` with sin/cos/sqrt/atan2/fabs (~30 min)
3. Write ~150 lines of C wrapper for GLFW+Vulkan (or ~80 lines for SDL2)
4. Write the game in Glyph
