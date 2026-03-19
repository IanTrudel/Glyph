# Asteroids in Glyph: GLFW + Vulkan Feasibility Assessment

## Question

Can we reproduce an Asteroids game in Glyph using GLFW for windowing and Vulkan for rendering, given the current state of the language and compilers?

**Short answer: No — not without substantial language additions. The blocking gaps are floats, bitwise ops, C struct layout, and math functions. However, a "thick C wrapper" architecture is feasible today where Glyph handles game logic and C handles all graphics.**

---

## What Asteroids Requires

An Asteroids clone with GLFW+Vulkan needs:

| Layer | Requirements |
|-------|-------------|
| **Windowing** | GLFW init, window creation, event polling, key callbacks |
| **Vulkan init** | Instance, physical/logical device, surface, swapchain |
| **Render pipeline** | Render pass, framebuffers, graphics pipeline, shaders (SPIR-V) |
| **Vertex data** | Vertex/index buffers with f32 positions, push constants or UBOs |
| **Game loop** | ~60Hz tick: poll input → update physics → record commands → present |
| **Physics** | 2D position/velocity vectors, rotation (sin/cos), wrapping, collision |
| **Math** | sin, cos, atan2, sqrt, vector normalize, matrix multiply |
| **Timing** | Frame delta time, frame rate control |

---

## Capability Assessment

### 1. Float Support — NOT WORKING

**Status**: The type system knows about `F`/`Float64` but neither compiler can actually use them.

- **Self-hosted compiler**: The lexer (`scan_number_end`) only scans digits — no `.` detection, no float literal parsing. `cg_operand` has no `ok_const_float` case. All locals and params are declared as `long long` in generated C.
- **Rust compiler**: The lexer scans `3.14` correctly and the MIR has `MirType::Float`, but the Cranelift codegen `BinOp` handler only emits `iadd`/`isub`/`imul`/`sdiv` (integer instructions). Float arithmetic silently produces garbage.

**Impact**: Fatal. Asteroids needs float positions, velocities, rotation angles. Every vertex position is a float. Every physics calculation uses floats.

**To fix**: ~20-30 new/modified definitions in self-hosted compiler:
- Lexer: float literal scanning (detect `.` in number)
- Parser: `tk_float` atom case, `ok_const_float` operand kind
- MIR: float-typed binops or implicit coercion
- C codegen: `double` locals when type is float, `double` params, float literal emission
- Runtime: float-to-string conversion (`float_to_str`)

### 2. Bitwise Operations — MISSING ENTIRELY

**Status**: No bitwise AND, OR, XOR, shift-left, shift-right at any level.

- The `BinOp` enum (Rust MIR) has: Add, Sub, Mul, Div, Mod, Eq, Neq, Lt, Gt, LtEq, GtEq, And, Or. No BitAnd, BitOr, BitXor, Shl, Shr.
- The self-hosted op constants go `op_add=1` through `op_or=13`. No bitwise ops.
- `&&`/`||` are logical (short-circuit boolean), not bitwise.
- `|` by itself is enum variant separator, not bitwise OR.
- `>>` is function composition, not right-shift.

**Impact**: Fatal for Vulkan. Vulkan uses bitmask flags pervasively:
```c
VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
VkFormat format = /* packed bit fields */;
```

**To fix**: ~15-20 new/modified definitions:
- Lexer: new tokens for `&&&` (bitwise AND), `|||` (bitwise OR), `^^^` (XOR), `<<<` (shift-left), `>>>` (shift-right) — or reuse unused single-char tokens
- Parser: new binary operator precedence level
- MIR: 5 new BinOp variants
- C codegen: emit `&`, `|`, `^`, `<<`, `>>` in generated C

### 3. C-Compatible Struct Layout — IMPOSSIBLE IN GLYPH

**Status**: All Glyph values are 8-byte `long long` slots. No mixed-type structs.

Gen=2 struct codegen produces:
```c
typedef struct { long long field1; long long field2; } Glyph_Name;
```

Vulkan requires structs like:
```c
typedef struct VkExtent2D {
    uint32_t width;   // 4 bytes
    uint32_t height;  // 4 bytes
} VkExtent2D;

typedef struct VkApplicationInfo {
    VkStructureType sType;        // uint32_t (4 bytes)
    const void* pNext;            // pointer (8 bytes)
    const char* pApplicationName; // pointer (8 bytes)
    uint32_t applicationVersion;  // 4 bytes
    // ...
} VkApplicationInfo;
```

Glyph cannot express mixed-width fields. A `{width: I, height: I}` record uses 16 bytes where Vulkan expects 8. Passing a Glyph record pointer to a Vulkan function would produce memory layout mismatches.

**Impact**: Fatal for direct Vulkan calls. Every Vulkan struct has mixed uint32_t/pointer/float fields.

**Workaround**: All Vulkan structs must be allocated and populated in C wrapper code. Glyph passes values as `long long` arguments; C wrappers pack them into the correct struct layout.

### 4. Raw Memory / Sub-Word Writes — LIMITED

**Status**: `raw_set(ptr, idx, val)` writes 8-byte `long long` at `((long long*)ptr)[idx]`. No way to write 4-byte float or 4-byte uint32.

**Impact**: Can't fill vertex buffers directly from Glyph. Vertex data for Asteroids is typically `{float x, y; float r, g, b;}` = 20 bytes per vertex. Glyph would need to pass vertex positions as `long long` arrays and have C code repack them as floats.

**To fix (partial)**: Add `raw_set_f32(ptr, byte_offset, float_val)` and `raw_set_u32(ptr, byte_offset, uint_val)` runtime functions. But this gets unwieldy for complex buffer layouts.

### 5. Math Functions — NOT IN RUNTIME

**Status**: No sin, cos, sqrt, atan2, fabs, pow in the built-in runtime. No `<math.h>` include.

**Impact**: Fatal for Asteroids physics. Ship rotation, thrust direction, asteroid movement all need trig.

**Workaround (available today)**: Write a C wrapper file:
```c
// asteroids_math.c
long long glyph_sin_f(long long x) {
    double d; memcpy(&d, &x, 8);
    double r = sin(d); long long out;
    memcpy(&out, &r, 8); return out;
}
// similarly for cos, sqrt, atan2...
```
Link with `-lm`. This works but requires the float support gap to also be fixed (so Glyph can produce `double` bit-patterns to pass).

**To fix properly**: Add `cg_runtime_math` with bitcast wrappers for sin/cos/sqrt/atan2/fabs/pow/floor/ceil. ~1 new definition + modify `cg_runtime_full`.

### 6. C Callback Interop — REQUIRES TRAMPOLINES

**Status**: Glyph closures use a `(closure_ptr, captures...)` calling convention that is incompatible with C function pointers.

GLFW needs callbacks:
```c
glfwSetKeyCallback(window, my_key_handler);  // void (*)(GLFWwindow*, int, int, int, int)
glfwSetFramebufferSizeCallback(window, resize_handler);
```

Vulkan's debug messenger also uses a callback.

**Workaround (available today)**: Write static C trampoline functions that call the Glyph function:
```c
extern long long glyph_on_key(long long window, long long key, long long scancode, long long action, long long mods);
void key_trampoline(GLFWwindow* w, int key, int sc, int action, int mods) {
    glyph_on_key((long long)w, key, sc, action, mods);
}
```
Register the trampoline in C init code. This works today.

### 7. Game Loop / While Loop — MISSING (workaround exists)

**Status**: No `while`, `for`, or `loop` keyword. All iteration is tail recursion.

**Impact**: Moderate. A game loop becomes:
```
game_loop state =
  poll_events(0)
  new_state = update(state)
  render(new_state)
  match should_close(0) == 0
    true -> game_loop(new_state)
    _ -> cleanup(0)
```

The Rust compiler performs tail-call optimization for self-recursive calls (emits `Goto` to entry block). The self-hosted C codegen relies on C compiler TCO (`-O2` typically does this for tail-recursive patterns).

**Workaround (available today)**: Tail recursion works. Or put the game loop in C and call Glyph for per-frame logic.

### 8. Timing — PARTIAL

**Status**: No `time()`, `gettimeofday()`, `clock_gettime()` in runtime. The gstats example solved this with a `gstats_ffi.c` wrapper for `time()`.

**Impact**: Need frame delta time for physics. Solvable with the existing FFI wrapper pattern.

---

## Architecture Options

### Option A: Pure Glyph (Not Feasible Today)

Write everything in Glyph. **Blocked** by: no floats, no bitwise ops, no C struct layout, no math.

### Option B: Thick C Wrapper (Feasible Today)

Write a C "engine" layer (~500-1000 lines) that handles:
- All GLFW setup, window management, input polling
- All Vulkan initialization, pipeline setup, rendering
- Vertex buffer management with proper f32 layout
- Math functions with bitcast wrappers
- Frame timing

Expose ~15-20 simple `long long`-ABI functions to Glyph:
```
engine_init(width, height) -> handle
engine_should_close(handle) -> bool
engine_poll_input(handle) -> input_state
engine_begin_frame(handle) -> void
engine_draw_ship(handle, x, y, angle) -> void
engine_draw_asteroid(handle, x, y, size, angle) -> void
engine_draw_bullet(handle, x, y) -> void
engine_end_frame(handle) -> void
engine_cleanup(handle) -> void
```

Glyph handles: game state, entity management, collision detection (integer or fixed-point), scoring, lives, level progression.

**This is viable today** but puts most of the hard work in C. Glyph is essentially scripting game logic atop a C engine.

### Option C: Fix Language Gaps First (Recommended)

Add the missing capabilities, then Glyph can handle significantly more of the stack:

| Gap | Effort | Definitions |
|-----|--------|-------------|
| Float literals + arithmetic | Medium | ~25 new/modified |
| Bitwise operators | Medium | ~18 new/modified |
| Math runtime (sin/cos/sqrt) | Small | ~2 new/modified |
| While loop | Small | ~10 new/modified |
| **Total** | | **~55 definitions** |

After these additions, the architecture becomes:

- **C wrapper** (~200 lines): GLFW/Vulkan init, struct packing, callbacks — pure boilerplate
- **Glyph** (everything else): game loop, physics, collision, entity management, render command recording via wrapper calls

This is a much more satisfying split where Glyph does the interesting work.

### Option D: Use SDL2 Software Rendering Instead of Vulkan

Vulkan's struct-heavy API is the hardest part. SDL2 with a 2D software renderer has a much simpler C API:
```c
SDL_Init(SDL_INIT_VIDEO);
SDL_Window* w = SDL_CreateWindow(...);
SDL_Renderer* r = SDL_CreateRenderer(w, -1, 0);
SDL_RenderDrawLine(r, x1, y1, x2, y2);  // all ints!
```

SDL2's 2D drawing API uses integers, not floats. Line-drawing Asteroids (classic vector look) would need minimal FFI. **This sidesteps the float and struct layout problems entirely** — only bitwise ops (for SDL flags) and math (for rotation) would need adding.

---

## Priority Roadmap (if pursuing Option C)

1. **Float support** — unlocks physics, vertex math, and math functions
2. **Bitwise operators** — unlocks Vulkan flags, SDL flags, general bit manipulation
3. **Math runtime** — unlocks sin/cos/atan2 for rotation and movement
4. **While loop** — quality-of-life for game loops and imperative patterns

Each is independently useful beyond Asteroids. Floats and bitwise ops are foundational language features that many programs would benefit from.

---

## Conclusion

Glyph today is a capable language for text-processing, database-driven, and CLI programs. The Asteroids assessment reveals that its **numeric type system is integer-only in practice**, and it **lacks the bit-level operations** that systems programming demands. These are not architectural limitations — the type system scaffolding for floats already exists, and adding bitwise ops follows established patterns. With ~55 new definitions (roughly one focused session), Glyph could handle most of an Asteroids implementation with only a thin C wrapper for GLFW/Vulkan boilerplate.

The existing Life example (`examples/life/life.glyph`) with X11 demonstrates that Glyph can drive a graphical application through C FFI. Asteroids is a larger step in the same direction.
