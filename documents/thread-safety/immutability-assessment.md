# Glyph Immutability Assessment

## Executive Summary

Glyph is a functional language with pervasive immutability at the value level — records, strings, and enum variants are all immutable after construction. However, the two primary collection types (arrays and hashmaps) are fully mutable, and the language provides no compile-time or runtime distinction between mutable and immutable data. The type system, MIR, and codegen are completely oblivious to mutability — there is no `mut` keyword, no mutable reference type, and no enforcement mechanism at any stage of the pipeline.

This document catalogs every source of mutability in the language, runtime, and compiler, assesses which mutations are fundamental vs. incidental, and recommends a path toward stronger immutability guarantees that would support future thread safety goals.

## Current Immutability Landscape

### What IS Immutable

| Construct | Mechanism | Thread-Safe? |
|-----------|-----------|-------------|
| **Strings** | Fat pointer `{ptr, len}`. All operations (`str_concat`, `str_slice`, `str_char_at`) allocate new strings. No `str_set_char` exists. | Yes |
| **Records** | `rec{field: val}` performs a functional update: allocates a new record, copies all fields, overwrites specified fields. Original untouched. | Yes |
| **Enum variants** | Nullary: tagged integers (`disc * 2 + 1`). Payload: heap-allocated `{tag, payload...}` written once at construction. No variant-set operation exists. | Yes |
| **Closure captures** | Captured by value (snapshot at closure creation time). The closure struct `{fn_ptr, cap0, cap1, ...}` is written once. | Yes (values) |
| **Integer/Float/Bool** | Unboxed `i64` values (or `double` bitcast to `i64`). Passed by value. | Yes |

These represent the majority of Glyph's type system surface area. The functional core is genuinely immutable.

### What IS Mutable

| Construct | Operations | Scope |
|-----------|-----------|-------|
| **Arrays** | `array_push`, `array_set`, `array_pop`, `array_reverse` | Language-visible, pervasive |
| **Hashmaps** | `hm_set`, `hm_del`, `hm_resize` | Language-visible |
| **StringBuilder** | `sb_new`, `sb_append`, `sb_build` | Internal (string interpolation), limited user exposure |
| **Local variables** | `:=` reassignment | Language-visible (but unused in practice) |
| **Global state** | `_glyph_current_fn`, `_glyph_call_stack`, `g_argc/g_argv`, coverage counters, test state | Runtime internals only |
| **Raw memory** | `raw_set(ptr, idx, val)` | Escape hatch, 1 real use |

## Deep Dive: Array Mutability

Arrays are the critical case. They are Glyph's only general-purpose collection and the workhorse of nearly every program.

### Representation

```c
// Header: 3 slots on the heap
GVal header[3] = { (GVal)data_ptr, len, cap };
// Data: contiguous GVal buffer
GVal data[cap];
```

### Mutating Operations

**`array_push(hdr, val)`** — The single most common mutation in Glyph. Appears in 155 of ~1,320 compiler functions (11.7%). Increments `len`, may realloc `data` and update all three header fields. Returns the same header pointer.

**`array_set(hdr, i, val)`** — Direct indexed write: `data[i] = val`. Used in 35 compiler functions. Powers the "mutable cell" pattern (see below) and in-place algorithms (sorting, grid updates).

**`array_pop(hdr)`** — Decrements `len`, returns removed element. Used in 10 functions.

**`array_reverse(hdr)`** — In-place swap loop. Used for array reversal.

**`array_slice(hdr, start, end)`** — The exception: allocates a **new** array. Does not mutate.

### The Single-Element Array Pattern

Since Glyph has no mutable local variables in practice (`:=` exists but is never used), the compiler and complex programs use a `[value]` one-element array as a mutable cell:

```
-- Creation: allocate a mutable counter
counter = [0]

-- Read: index into the cell
id = counter[0]

-- Write: set the cell's value
_ = array_set(counter, 0, id + 1)
```

This pattern appears throughout the compiler's MIR lowering infrastructure:

```
mk_mir_lower ... =
  { cur_block: [0]      -- current basic block ID
  , nxt_local: [0]      -- next local variable ID
  , nxt_block: [0]      -- next block ID
  , fn_entry: [0]       -- entry block
  , lambda_ctr: [0]     -- lambda counter
  , ... }
```

And in user programs: the Prolog interpreter uses `ctr = [0]` for clause renaming, the window manager uses `wm_drag_mode: [0]` for drag state, the async library uses `sched_next_id: [1]` for task IDs.

This is Glyph's equivalent of `Ref<T>` / `IORef` / `RefCell`. It works because arrays are heap-allocated and passed by pointer — all references to the same array see mutations immediately.

### Aliasing Implications

Array mutation combined with pointer aliasing creates the same class of bugs that plague imperative languages:

```
a = [1, 2, 3]
b = a                    -- b and a point to the SAME header
_ = array_push(a, 4)    -- mutates the shared array
-- b[3] is now 4 — surprising if you expected a copy
```

This is currently harmless in single-threaded Glyph. In a concurrent setting, two threads mutating the same array would produce data races — `array_push` performs unsynchronized realloc + len increment.

## Deep Dive: Hashmap Mutability

### Representation

```c
// Header: 3 slots
GVal header[3] = { (GVal)slot_array, count, capacity };
// Slots: open-addressing, 3 GVals per slot [key, value, state]
// state: 0=empty, 1=occupied, 2=tombstone
```

### Mutating Operations

**`hm_set(m, k, v)`** — Probes for slot, writes key/value/state. Triggers `hm_resize` at 70% load. Returns same map pointer.

**`hm_del(m, k)`** — Tombstone deletion (sets state to 2). Decrements count.

**`hm_resize(h)`** — Allocates new double-sized slot array, rehashes all entries.

Hashmaps are used sparingly (3 compiler functions use `hm_set`), but are essential for programs like the window manager (client tracking), async runtime (task/fd maps), and web framework (URL parameters, stores).

## Deep Dive: StringBuilder

StringBuilder is a special case — it is intentionally mutable with a limited lifetime:

```
sb = sb_new()              -- allocate builder {buf, len, cap}
_ = sb_append(sb, "hello") -- mutate buffer in place
_ = sb_append(sb, " world")
result = sb_build(sb)      -- consume builder, produce IMMUTABLE string
```

The pattern is create-mutate-freeze: the builder exists only during string construction, then is consumed. The resulting string is immutable. This is equivalent to Java's `StringBuilder` → `String`, Rust's `String` → `&str`, or Clojure's transient → persistent pattern.

StringBuilder is used in 22 compiler functions and in user programs for string building in loops (`join`, JSON escaping, etc.). It is not a general-purpose mutable structure — it has single-owner semantics by convention.

## Deep Dive: Assignment Operator (`:=`)

The `:=` operator is fully implemented in both compilers:

```
-- Parser: StmtKind::Assign(lhs, rhs)
-- MIR: Rvalue::Use(rhs) targeting existing local ID
-- C codegen: _N = <rhs>;
-- Also: arr[i] := val → glyph_array_set(arr, i, val)
```

Despite being fully operational, `:=` has **zero uses** across the entire codebase — not in the compiler (1,320 functions), not in any example program, not in any library. The compiler exclusively uses the single-element array pattern for mutable state, and all programs use recursive state-threading.

This is a significant finding: the Glyph ecosystem has organically converged on a purely functional style where variable reassignment is available but culturally unused. The `:=` operator is dead code in practice.

## Deep Dive: Global Mutable State

All global mutable state is in the C runtime, not accessible from Glyph code:

| Global | Purpose | Mutation Pattern |
|--------|---------|-----------------|
| `_glyph_current_fn` | Crash diagnostics — current function name | Written at every function entry |
| `_glyph_call_stack[256]` + `_glyph_call_depth` | Debug stack traces (debug builds only) | Push/pop at function entry/exit |
| `g_argc` + `g_argv` | Command-line arguments | Set once at startup, read-only thereafter |
| `_glyph_cov_hits[N]` + names/count | Code coverage counters | Increment per-function (coverage builds only) |
| `_test_failed` + `_test_name` | Test assertion state | Set during test execution |

For thread safety, `_glyph_current_fn` and `_glyph_call_stack` would need to be `__thread` / `_Thread_local`. The others are set-once or build-configuration-specific.

## Deep Dive: The `raw_set` Escape Hatch

```c
GVal glyph_raw_set(GVal ptr, GVal idx, GVal val) {
    ((long long*)ptr)[idx] = val;
    return 0;
}
```

An unchecked memory write with no bounds checking. Used in exactly one place in the compiler: `eng_reset_errors` clears the type checker engine's error array by writing directly to a record field offset. This exists because record update (`rec{field: val}`) allocates a new record — when you hold the only reference and want to mutate in place for performance, `raw_set` is the escape hatch.

## Type System and Compiler: No Mutability Model

The most significant finding from the compiler investigation is that **mutability is invisible to every stage of the compilation pipeline**:

### Parser / AST

- `StmtKind::Assign(Expr, Expr)` exists for `:=` — no restriction on the LHS beyond it being an expression
- `UnaryOp::Ref` and `TypeExprKind::Ref` exist — no mutability qualifier

### Type System

- `Type::Ref(Box<Type>)` and `Type::Ptr(Box<Type>)` — no `MutRef` variant
- No `mut` keyword or annotation anywhere in the type grammar
- Assignment type-checks by inferring the RHS and returning `Type::Void` — no check on LHS mutability
- `array_push` is not even registered in the type checker (polymorphic functions are excluded)

### MIR

- `MirLocal` has `{id, ty, name}` — no `is_mutable` flag
- `Rvalue::Ref(LocalId)` and `Rvalue::Deref(Operand)` carry no mutability information
- Field assignment (`rec.field := val`) is parsed but explicitly unimplemented in MIR lowering
- Any local can be reassigned via `Rvalue::Use` targeting an existing local ID

### Codegen (both Cranelift and C)

- Every MIR local becomes a mutable Cranelift `Variable` or C local — no distinction
- Record fields are only written during aggregate construction, then read-only by convention
- Array mutations are opaque runtime calls — codegen doesn't know they're mutations

## Mutation in Practice: Usage Patterns

### Pattern A: Array Accumulation (dominant)

```
map_loop f xs i acc =
  match i >= array_len(xs)
    true -> acc
    _ ->
      _ = array_push(acc, f(xs[i]))
      map_loop(f, xs, i + 1, acc)
```

The interface is functional (returns a new array, input unchanged), but the implementation mutates the accumulator. This is the most common pattern, appearing in stdlib (`map`, `filter`, `sort`, `range`, `zip`, `flat_map`, `join`), the compiler (MIR lowering, parsing), and every non-trivial example program.

### Pattern B: Mutable Cell (`[value]`)

```
counter = [0]
-- ... later ...
id = counter[0]
_ = array_set(counter, 0, id + 1)
```

Used for counters, IDs, and state tracking when functional state-threading would be too verbose. Appears in 33+ compiler functions (MIR lowering context) and in complex programs (Prolog interpreter, window manager, async runtime).

### Pattern C: State Threading (functional mutation)

```
event_loop buf cx cy top filename modified quit rows cols =
  -- ... process event, compute new state ...
  event_loop new_buf new_cx new_cy new_top filename is_modified quit_pressed rows cols
```

The text editor demonstrates pure state threading: 9 state parameters are passed through recursive calls. No mutable variables — all "mutation" happens by passing new values to the next iteration. This is genuinely functional but verbose.

### Pattern D: FFI State Delegation

```
-- In asteroids_ffi.c:
static double ship_x, ship_y, ship_vx, ship_vy;
double get_sx() { return ship_x; }
void set_sx(double v) { ship_x = v; }
```

When a program needs heavy mutable state (games, window managers), all state is pushed to C global variables with get/set accessor functions. The FFI comment is revealing: *"All game state lives here to avoid Glyph's partial-record-update codegen."* This indicates that record functional updates, while correct, are too expensive or awkward for high-frequency state updates.

### Pattern E: Hashmap State

```
_ = hm_set(clients, window_id_str, client_record)
client = hm_get(clients, window_id_str)
```

Used for lookup tables that grow over time. The web framework, async runtime, and window manager all use this pattern for mapping IDs to stateful objects.

### Mutation Prevalence by Program Complexity

| Program | Functions | `array_push` | `array_set` | `hm_set` | `:=` | Style |
|---------|----------|-------------|------------|---------|------|-------|
| Calculator | 18 | 1 | 0 | 0 | 0 | Nearly pure |
| Fibonacci | 3 | 0 | 0 | 0 | 0 | Pure |
| Countdown | 4 | 0 | 0 | 0 | 0 | Pure |
| Game of Life | 20 | 3 | 5 | 0 | 0 | Grid mutation |
| Text Editor | 34 | 5 | 8 | 0 | 0 | Buffer mutation + state threading |
| Asteroids | 40+ | ~5 | 0 | 0 | 0 | FFI state delegation |
| Window Manager | 50+ | ~10 | ~5 | ~5 | 0 | Hashmap + cell pattern |
| Async Runtime | 40+ | ~10 | ~5 | ~5 | 0 | Heavy mutation (scheduler) |
| Web API | 25+ | ~5 | 0 | ~3 | 0 | Hashmap state |
| **Compiler** | **1,320** | **155** | **35** | **3** | **0** | Cell pattern + accumulation |

Simple programs are nearly pure. Complexity drives mutation adoption.

## Assessment: What Needs Rework?

### Already Good (No Changes Needed)

1. **Strings** — Immutable. No rework needed.
2. **Records** — Functional updates via copy. Correct semantics.
3. **Enum variants** — Immutable after construction. Correct semantics.
4. **Closure captures** — By-value snapshot. Correct for a functional language.
5. **StringBuilder** — Create-mutate-freeze lifecycle. Correct pattern.

### Needs Attention

#### 1. Arrays: The Core Problem

Arrays are Glyph's only general-purpose collection and they are fully mutable with no distinction between "I'm building this array" and "I'm sharing this array." The `array_push` accumulator pattern is functional in interface but imperative in implementation — there is no way to express "this array is finished being built and should not be mutated further."

**The risk:** When concurrency arrives, any array that crosses a thread boundary could be mutated by the sender while being read by the receiver. There is no mechanism to detect or prevent this.

**Options:**
- (a) **Freeze operation**: Add `array_freeze(arr)` that sets a "frozen" bit. Frozen arrays panic on push/set/pop. The compiler's for-loop desugaring would automatically freeze the result. Libraries like `map`/`filter` would freeze before returning.
- (b) **Copy-on-share**: Arrays crossing thread boundaries are deep-copied automatically.
- (c) **Persistent data structures**: Replace mutable arrays with HAMTs (see `glyph-thread-safety.md` Option A). Highest safety, highest performance cost.
- (d) **Ownership tracking**: Runtime single-owner check, panic on aliased mutation.

Recommendation: Start with **(a) freeze** — it is the lowest-cost change that provides a clear immutability boundary. The runtime cost is one branch per mutation operation. It naturally separates "building" from "using" phases, which is how arrays are already used in practice.

#### 2. Hashmaps: Same Problem as Arrays

Hashmaps have the same aliasing/mutation problem as arrays but are used far less frequently. They would benefit from the same freeze mechanism.

#### 3. The `:=` Operator: Dead but Alive

`:=` is fully implemented but has zero uses anywhere. It represents a philosophical inconsistency — Glyph is culturally functional but provides unrestricted mutation. Options:
- (a) **Deprecation warning**: Emit a compiler warning when `:=` is used on a local variable (not `arr[i]`).
- (b) **Remove entirely**: Since nobody uses it, remove `StmtKind::Assign` for local variables. Keep `arr[i] := val` as sugar for `array_set`.
- (c) **Keep as-is**: It costs nothing and provides an escape hatch.

Recommendation: **(a) deprecation warning** for local variable reassignment. Keep `arr[i] := val` as it is idiomatic for array element updates.

#### 4. `raw_set`: Unsafe Escape Hatch

`raw_set` bypasses all safety. It should be:
- Documented as `unsafe` (even though Glyph has no formal unsafe system)
- Audited periodically — current single use is justified
- Eventually gated behind a flag or annotation when an unsafe system is added

#### 5. The Single-Element Array Pattern: Language Gap

The `[value]` mutable cell pattern is a workaround for missing language features. It works but is ugly, error-prone (no type safety, easy to forget `[0]` indexing), and invisible to any future static analysis. Options:
- (a) **First-class `Ref` type**: `r = ref(0)` / `deref(r)` / `set_ref(r, 1)`. Type-safe mutable cell with clear semantics.
- (b) **Mutable `let`**: `let mut x = 0` / `x := 1`. Direct variable mutation with explicit opt-in.
- (c) **Leave as-is**: The pattern works. Don't add complexity.

Recommendation: **(a) First-class Ref type** — it makes mutability visible in the type system (future benefit) and eliminates the ambiguity between "this is a one-element array" and "this is a mutable variable."

#### 6. Type System: No Mutability Awareness

The type system has no concept of mutability. `Type::Ref(T)` carries no mutability qualifier. There is no `Send`/`Sync` trait, no ownership tracking, no borrow checking. This is the largest gap for thread safety.

This is a long-term concern, not an immediate one. Adding mutability to the type system is a significant undertaking (Rust's borrow checker took years to stabilize). The pragmatic path:
- Phase 1: Runtime enforcement (freeze, ownership checks)
- Phase 2: Type-level annotations (`&T` vs `&mut T`)
- Phase 3: Static enforcement (borrow checking or effect system)

## Comparison with Thread Safety Document

The companion document `glyph-thread-safety.md` addresses concurrency models, GC configuration, and async strategy. This assessment complements it by providing the detailed inventory of what actually needs to change before any concurrency model can be safe.

**Key intersection points:**
- Thread safety Phase 1.1 (eliminate global mutable state) → see "Global Mutable State" section above; only `_glyph_current_fn` and `_glyph_call_stack` need `__thread`
- Thread safety Phase 1.3 (make data structures safe) → this document's "Arrays: The Core Problem" and "Hashmaps" sections provide the detailed analysis
- Thread safety "Immutable by default" design decision → confirmed: records, strings, enums are already immutable; arrays/maps are the sole remaining issue
- Thread safety Option D (defer — don't share collections) → supported by this analysis; the freeze mechanism provides a lighter-weight intermediate step

## Recommended Priority Order

1. **Array/Hashmap freeze** — Add a `frozen` bit to array and hashmap headers. Mutation operations check and panic if frozen. Compiler-generated array accumulations auto-freeze on completion. *(Low effort, high value, enables safe sharing without persistent data structures)*

2. **First-class `Ref` type** — Replace the `[value]` mutable cell pattern with a proper `Ref` type that the type system can reason about. *(Medium effort, cleans up the language, prerequisite for static mutability analysis)*

3. **`:=` deprecation for locals** — Warn on local variable reassignment. *(Trivial effort, removes a sharp edge)*

4. **`raw_set` gating** — Document as unsafe; plan for future gating when an unsafe/effect system exists. *(Documentation only for now)*

5. **Type-level mutability annotations** — Long-term: add `&T` vs `&mut T` distinction to the type system. *(High effort, defer until thread safety work begins in earnest)*

## Appendix: Complete Mutation Inventory

### Runtime Functions That Mutate

| Function | Defined In | What It Mutates |
|----------|-----------|----------------|
| `glyph_array_push` | `cg_runtime_c` | Array header (len, cap, data ptr) + data buffer |
| `glyph_array_set` | `cg_runtime_c` | Array data buffer at index |
| `glyph_array_pop` | `cg_runtime_c` | Array header (len) |
| `glyph_array_reverse` | `cg_runtime_arr_util` | Array data buffer (in-place swap) |
| `glyph_hm_set` | `cg_runtime_map` | Hashmap slot array + header count |
| `glyph_hm_del` | `cg_runtime_map` | Hashmap slot state (tombstone) + count |
| `glyph_hm_resize` | `cg_runtime_map` | Hashmap header (data ptr, cap) |
| `glyph_sb_append` | `cg_runtime_sb` | StringBuilder buffer + header (len, cap) |
| `glyph_raw_set` | `cg_runtime_raw` | Arbitrary memory at pointer+offset |
| `glyph_bitset_set` | `cg_runtime_extra` | Bitset word via bitwise OR |

### Global Mutable State

| Variable | Defined In | Scope | Thread-Safe? |
|----------|-----------|-------|-------------|
| `_glyph_current_fn` | `cg_runtime_c` | Always | No — needs `__thread` |
| `_glyph_call_stack` + `_glyph_call_depth` | `cg_runtime_c` | Debug builds | No — needs `__thread` |
| `g_argc` + `g_argv` | `cg_runtime_args` | Always | Yes — set once before threads |
| `_glyph_cov_hits[]` | `cg_runtime_coverage` | Coverage builds | No — needs atomics or thread-local |
| `_test_failed` + `_test_name` | `cg_test_runtime` | Test builds | N/A — tests are single-threaded |

### Compiler Functions Using Mutation (by category)

| Category | Functions | Primary Pattern |
|----------|----------|----------------|
| MIR lowering | `mir_alloc_local`, `mir_new_block`, `mir_switch_block`, `mir_terminate`, `mir_emit_*` (33 fns) | Single-element array counters |
| Parser / Tokenizer | `tokenize`, `parse_*` (~20 fns) | Array accumulation (token lists, AST nodes) |
| Type checker | `subst_bind`, `subst_find`, `subst_fresh_var`, `eng_set_tmap` (~15 fns) | Union-find mutation, array cells |
| C codegen | `cg_program`, `cg_function*`, `cg_stmt*` (~30 fns) | String builder, array accumulation |
| LLVM codegen | `ll_*` (~20 fns) | String builder, array accumulation |
| Stdlib | `map`, `filter`, `sort`, `range`, `zip`, `flat_map`, `join` (7 fns) | Array accumulation |
| CLI | `cmd_*` (~10 fns) | Array accumulation for results |
