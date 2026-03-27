# Immutability Solutions for Glyph

## The Problem in One Sentence

Arrays and hashmaps are the only mutable values in an otherwise functional language, and nothing in the type system, MIR, or runtime distinguishes "building a collection" from "sharing a finished collection."

## Two Approaches

This document presents two complementary approaches to the collection mutability problem:

1. **The Freeze Bit** (Solutions 1-6) — A pragmatic near-term mechanism that adds a runtime immutability flag to existing mutable arrays and hashmaps. Zero layout change, minimal performance cost, backward compatible. Draws the line between "building" and "sharing" phases.

2. **Persistent Data Structures** (Solution 7) — A fundamental redesign where collections are immutable by default, with structural sharing so that "updates" cheaply produce new versions. The theoretically clean answer that eliminates the aliasing problem entirely, at the cost of constant-factor performance overhead.

These are not mutually exclusive. The freeze bit can ship immediately and provide safety today. Persistent data structures are the longer-term evolution that makes immutability the default rather than an opt-in discipline. The freeze bit also serves as the bridge between the two — frozen arrays and persistent arrays have the same semantics from the caller's perspective (read-only, safe to share).

## Design Principles

1. **Mutation is a phase, not a property.** Every array in Glyph follows the same lifecycle: create empty, push/set during construction, return to caller. The transition from "building" to "using" happens naturally — we just need to make it explicit.

2. **Two performance tiers.** Mutable C arrays for building (transient phase) deliver maximum performance. Persistent structures for sharing (immutable phase) deliver maximum safety. The right choice depends on the use case.

3. **Runtime enforcement first, static enforcement later.** Runtime panics on misuse are cheap to implement and immediately useful. Type system integration can come later and upgrade panics to compile errors.

4. **The stdlib is the freeze boundary.** Library functions are the natural place to separate building from sharing. Every public stdlib function that returns an array should return an immutable one — whether that's a frozen C array today or a persistent structure tomorrow.

---

## Solution 1: The Freeze Bit

### Mechanism

Steal the sign bit of the `cap` field in the array header. Capacity is always non-negative, so `cap < 0` means "frozen." The actual capacity is `cap & 0x7FFFFFFFFFFFFFFF`.

```
Array header (unchanged layout, 24 bytes):
  h[0] = data pointer
  h[1] = length
  h[2] = capacity (high bit = frozen flag)

Frozen check:
  if (h[2] < 0) → frozen
  actual cap = h[2] & 0x7FFFFFFFFFFFFFFFLL
```

No additional memory. No layout change. The check is a single signed comparison — one branch instruction.

### Runtime Changes

**`glyph_array_freeze(hdr)`** — Sets the sign bit on `cap`. Returns the same pointer. Idempotent (freezing a frozen array is a no-op).

```c
GVal glyph_array_freeze(GVal hdr) {
    long long* h = (long long*)hdr;
    if (h[2] >= 0) h[2] = h[2] | (1LL << 63);
    return hdr;
}
```

**`glyph_array_frozen(hdr)`** — Returns 1 if frozen, 0 if mutable. Read-only query.

```c
GVal glyph_array_frozen(GVal hdr) {
    return ((long long*)hdr)[2] < 0 ? 1 : 0;
}
```

**`glyph_array_thaw(hdr)`** — Creates a **mutable copy** of a frozen array. Does NOT unfreeze in place (that would defeat the purpose — other references still expect immutability).

```c
GVal glyph_array_thaw(GVal hdr) {
    long long* h = (long long*)hdr;
    long long len = h[1];
    long long cap = h[2] & 0x7FFFFFFFFFFFFFFFLL;
    if (cap < len) cap = len;
    long long* nh = (long long*)malloc(24);
    long long* nd = (long long*)malloc(cap * 8);
    memcpy(nd, (void*)h[0], len * 8);
    nh[0] = (long long)nd; nh[1] = len; nh[2] = cap;
    return (GVal)nh;
}
```

**Modified mutation operations** — `array_push`, `array_set`, `array_pop`, `array_reverse` all gain a freeze check:

```c
GVal glyph_array_push(GVal hdr, GVal val) {
    long long* h = (long long*)hdr;
    if (h[2] < 0) glyph_panic("push on frozen array");
    // ... existing code unchanged ...
}
```

Same one-line addition to `array_set`, `array_pop`, and `array_reverse`.

### Cost

- **Memory:** Zero. No layout change.
- **Per-mutation overhead:** One signed comparison + branch. Modern CPUs predict this correctly ~99.9% of the time (almost always "not frozen"). Negligible.
- **Per-freeze overhead:** One bitwise OR. Negligible.

### Hashmaps: Same Pattern

Hashmaps use the same `{slots, count, cap}` header layout. Apply the identical sign-bit trick:

```c
GVal glyph_hm_freeze(GVal hdr) {
    long long* h = (long long*)hdr;
    if (h[2] >= 0) h[2] = h[2] | (1LL << 63);
    return hdr;
}
```

`hm_set` and `hm_del` gain the same one-line check. `hm_resize` is only called from `hm_set`, so it's already covered.

---

## Solution 2: Stdlib as Freeze Boundary

### The Pattern

Every public stdlib function that builds and returns an array wraps its result in `array_freeze`:

```
-- Before:
map f xs = map_loop(f, xs, 0, [])

-- After:
map f xs = array_freeze(map_loop(f, xs, 0, []))
```

The internal `_loop` functions are unchanged — they mutate the accumulator freely during construction. The public function is the freeze boundary.

### Applying to Every Stdlib Function

| Function | Change | Notes |
|----------|--------|-------|
| `map` | `array_freeze(map_loop(...))` | Direct |
| `filter` | `array_freeze(filter_loop(...))` | Direct |
| `flat_map` | `array_freeze(flat_map_loop(...))` | Direct |
| `zip` | `array_freeze(zip_loop(...))` | Direct |
| `range` | `array_freeze(range_loop(...))` | Direct |
| `sort` | `array_freeze(result)` after concat | Freeze the final merged result |
| `take` | Already returns `array_slice` result | `array_slice` should return frozen |
| `drop` | Already returns `array_slice` result | Same |
| `fold` | Returns accumulator (not necessarily array) | No change — fold's result type is generic |
| `each` | Returns void (side-effect only) | No change |
| `join` | Returns a string (via `sb_build`) | No change — strings already immutable |

**`array_slice` should return frozen.** Slicing produces a new array that's a finished value, not a builder. This also means `take` and `drop` automatically return frozen arrays without modification.

**`sort` needs a small rework.** Currently:

```
sort cmp xs =
  ...
  result = []
  _ = concat_into(result, lo, 0)
  _ = array_push(result, pivot)
  _ = concat_into(result, hi, 0)
  result
```

The recursive `sort` calls would return frozen arrays. `concat_into` reads from `lo` and `hi` (no mutation on frozen inputs — it only reads via indexing). It pushes into `result` (mutable, just created). Then freeze on return:

```
sort cmp xs =
  ...
  result = []
  _ = concat_into(result, lo, 0)
  _ = array_push(result, pivot)
  _ = concat_into(result, hi, 0)
  array_freeze(result)
```

This works cleanly. The recursive calls returning frozen arrays is actually *safer* — `concat_into` can't accidentally mutate the sorted sub-arrays.

### What Callers See

```
xs = [3, 1, 4, 1, 5]
sorted = sort(\a b -> a - b, xs)

-- sorted is frozen:
_ = array_push(sorted, 6)   -- PANIC: push on frozen array

-- To modify, thaw first:
mutable = array_thaw(sorted)
_ = array_push(mutable, 6)  -- OK, mutable is a fresh copy
```

The convention becomes: **if a function gave you an array, assume it's frozen.** If you need to build on it, thaw it.

---

## Solution 3: First-Class Ref Type

### The Problem It Solves

The `[0]` single-element array pattern is used as a mutable cell throughout the compiler:

```
counter = [0]                            -- "declare mutable int"
id = counter[0]                          -- "read"
_ = array_set(counter, 0, id + 1)       -- "write"
```

This works but has problems:
- It looks like an array operation, not a variable mutation
- The `[0]` could be confused with a real one-element array
- `array_freeze` would break it — you can't freeze a mutable cell
- Future static analysis can't distinguish cells from arrays
- It's verbose: 3 operations for what should be `id = counter; counter := id + 1`

### The Ref API

Four runtime functions:

```
r = ref(42)          -- allocate a mutable cell holding 42
x = deref(r)         -- read: x = 42
_ = set_ref(r, 99)   -- write: cell now holds 99
x = deref(r)         -- read: x = 99
```

### Runtime Implementation

A Ref is a 1-slot heap allocation:

```c
GVal glyph_ref(GVal val) {
    GVal* r = (GVal*)malloc(8);
    r[0] = val;
    return (GVal)r;
}

GVal glyph_deref(GVal r) {
    return ((GVal*)r)[0];
}

GVal glyph_set_ref(GVal r, GVal val) {
    ((GVal*)r)[0] = val;
    return 0;
}
```

8 bytes instead of 24 (array header) + 8 (array data) = 32 bytes per cell. 4x memory savings over the `[0]` pattern.

### Migration in the Compiler

The MIR lowering context would change from:

```
{ cur_block: [0]
, nxt_local: [0]
, nxt_block: [0]
, lambda_ctr: [0]
, ... }
```

To:

```
{ cur_block: ref(0)
, nxt_local: ref(0)
, nxt_block: ref(0)
, lambda_ctr: ref(0)
, ... }
```

And access changes from:

```
id = ctx.nxt_local[0]
_ = array_set(ctx.nxt_local, 0, id + 1)
```

To:

```
id = deref(ctx.nxt_local)
_ = set_ref(ctx.nxt_local, id + 1)
```

This is clearer in intent and no longer abuses the array API.

### Ref and Freeze Coexistence

Refs are **always mutable**. They are explicitly for mutation — that's their purpose. They don't participate in the freeze mechanism. The distinction is:

- **Array**: a collection of values. Starts mutable (building phase), then frozen (sharing phase).
- **Ref**: a mutable cell. Always mutable. Used for counters, accumulators, state tracking.

If a Ref crosses a thread boundary, that's a data race — future thread safety work would need to either forbid it or require synchronization (atomic refs, mutex-wrapped refs). But that's a separate concern from collection immutability.

### Closure Interaction

Closures capture by value. Capturing a `Ref` captures the *pointer* to the cell (since a Ref is a GVal holding a pointer). Both the closure and the outer scope share the same cell — mutations are visible to both:

```
counter = ref(0)
increment = \u -> set_ref(counter, deref(counter) + 1)

-- Both see the same cell:
_ = increment(0)      -- counter is now 1
x = deref(counter)    -- x = 1
```

This is the correct behavior and matches how closures currently interact with the `[0]` pattern. The difference is that it's now explicit and self-documenting.

---

## Solution 4: Array Literals — Frozen or Mutable?

### The Question

When a user writes `[1, 2, 3]`, should the result be frozen or mutable?

### Arguments for Frozen by Default

- Array literals are finished values — you wrote all the elements already
- Matches the functional language philosophy
- `xs = [1, 2, 3]` followed by `array_push(xs, 4)` is surprising in a functional language
- Most uses of array literals are as data, not as accumulators

### Arguments for Mutable by Default

- `acc = []` is the universal accumulator pattern — every `_loop` function starts with `[]`
- Making `[]` frozen would break every stdlib function and every user accumulation loop
- The builder pattern `arr = []; push; push; push; freeze` is the standard workflow

### Resolution: Mutable by Default, With Style Guide

Array literals stay mutable. The reason is pragmatic: the accumulator pattern `f(..., [])` is too fundamental to break. The freeze boundary is at the *function return*, not at the *literal*.

The convention is:
- `[]` in a function argument position → accumulator (will be mutated, then frozen on return)
- `[1, 2, 3]` as a local binding → data (should be frozen if shared, but often just used locally and discarded)

A future lint could warn when an unfrozen array escapes a function without being frozen, but this is not necessary in the near term.

**Exception:** `array_slice`, `array_thaw`, and future persistent operations that produce "finished" arrays should return frozen results. These are not accumulators.

---

## Solution 5: Compiler Internal Migration

The self-hosted compiler is the largest Glyph program (~1,320 functions) and the heaviest user of mutation. Here is how each mutation pattern maps to the new primitives:

### Pattern: Single-Element Array Counters → Ref

~33 functions use `ctx.nxt_local[0]` / `array_set(ctx.nxt_local, 0, ...)`. All migrate to `ref` / `deref` / `set_ref`.

**Affected context fields in `mk_mir_lower`:**
- `cur_block`, `nxt_local`, `nxt_block`, `fn_entry`, `lambda_ctr`

**Affected context fields in type checker engine:**
- `next_var`

**Migration is mechanical:** search for `array_set(ctx.FIELD, 0, ...)` and replace with `set_ref(ctx.FIELD, ...)`. Search for `ctx.FIELD[0]` and replace with `deref(ctx.FIELD)`.

### Pattern: Array Accumulation → Same, Plus Freeze

~155 functions use `array_push`. Most are building arrays that get returned. The `_loop` pattern stays the same; the public entry point adds `array_freeze`.

**Key compiler functions that should freeze results:**
- `tokenize` → returns token array (frozen)
- `parse_fn_def` → returns AST node array (frozen)
- `compile_fns` → returns MIR array (frozen)
- All `collect_*` / `walk_*` functions that gather results

### Pattern: `raw_set` → Eliminate

The single real use (`eng_reset_errors`) should be replaced with either:
- A `Ref` for the error array field
- A dedicated `eng_clear_errors` that constructs a new engine record with empty errors

### Pattern: Type Checker Union-Find → Keep (Mutable Arrays)

`subst_bind` writes into `eng.bindings` and `eng.parent` arrays. These are internal mutable state of the type checker, not shared. They should NOT be frozen — they are correctly used as mutable working memory. The Ref type is not appropriate here (these are indexed arrays, not single cells).

The right approach: these arrays are created for a single type-checking pass and never escape. They are inherently single-owner. No freeze needed. A future ownership system could verify this statically.

---

## Solution 6: Thread Safety Implications

### What Freeze Enables

With freeze in place, the thread safety story for collections becomes straightforward:

- **Frozen arrays/maps can be shared across threads without synchronization.** They are read-only. Multiple threads can index, iterate, and read concurrently. No locks needed.
- **Mutable arrays/maps cannot cross thread boundaries.** A `spawn` primitive would need to check that all values passed to the new thread are either non-collections (ints, strings, records, enums — all immutable) or frozen collections. Passing a mutable array to `spawn` would panic.
- **Channels send frozen values.** `channel_send(ch, val)` would auto-freeze any mutable arrays/maps before sending (or panic if that's not desired). The receiver always gets frozen data.

This is essentially Erlang's "copy on send" model, but cheaper — we just flip a bit instead of deep-copying.

### What Freeze Doesn't Solve

- **Refs** are always mutable and cannot be safely shared. Need `AtomicRef` or `MutexRef` for cross-thread mutable cells.
- **Aliased mutable arrays** within a single thread are still possible. Two variables pointing to the same mutable array can step on each other. Freeze doesn't prevent this — it only prevents mutation after the freeze point.
- **No static enforcement.** The compiler doesn't know which arrays are frozen. A function that receives an array parameter can't declare "I need a frozen array" vs "I need a mutable array" in its type signature. This requires type system work (Phase 3 in the thread safety roadmap).

---

## Solution 7: Persistent Data Structures

### Why This Matters

The freeze bit (Solutions 1-6) is a discipline mechanism — it tells you *when* mutation is wrong, but it doesn't eliminate mutation. Aliased mutable arrays within a single thread are still possible. A function that receives a mutable array and stashes a reference to it can be surprised when the caller later mutates it. The freeze bit catches the obvious case (mutating after freeze) but not the subtle case (aliased mutation before freeze).

Persistent data structures eliminate the problem at the root: there is no mutation, so there are no aliasing bugs. An "update" produces a new structure that shares most of its memory with the old one. The old version remains intact. This is how Clojure, Scala, Haskell, and Erlang handle collections.

For a functional language like Glyph, persistent collections are the natural end state. The question is not *whether* but *when* and *how*.

### What Are Persistent Data Structures?

A persistent data structure preserves all previous versions of itself when modified. "Modification" produces a new root that shares most of its internal nodes with the original.

**Persistent Vector (RRB-Tree / Bagwell Trie):**

The replacement for mutable arrays. A tree with branching factor 32, where leaf nodes are contiguous 32-element chunks.

```
Logical view: [a, b, c, d, e, f, ... 1000 elements]

Physical structure (branching factor 32):
         [root]
        /      \
    [node]    [node]
    / | \      / | \
 [leaf] ... [leaf] ...
 [a..z]     [...]
```

"Appending" element 1001 creates a new root and a new path from root to the new leaf — at most `log32(n)` nodes. Everything else is shared with the old tree.

**Persistent Hash Map (HAMT — Hash Array Mapped Trie):**

The replacement for mutable hashmaps. A trie keyed on hash bits, with 32-way branching at each level.

```
Logical view: {"name": "Alice", "age": 30, ...}

Physical structure:
        [root: bitmap + children]
        /          |           \
  [node]       [node]       [leaf: "name"→"Alice"]
  /    \         |
[leaf] [leaf]  [leaf: "age"→30]
```

"Inserting" a new key creates a new path from root to the new/updated leaf. All other paths are shared.

### Performance Characteristics

**Persistent Vector vs Mutable Array:**

| Operation | Mutable Array (current) | Persistent Vector | Ratio |
|-----------|------------------------|-------------------|-------|
| Random read `xs[i]` | O(1) | O(log32 n) ≈ 1-6 hops | 2-5x slower |
| Sequential iteration | O(1) per element | O(1) amortized (chunked leaves) | 1.2-2x slower |
| Append (push) | O(1) amortized | O(log32 n) + allocation | 3-10x slower |
| Random update `set(xs, i, v)` | O(1) | O(log32 n) + allocation | 10-30x slower |
| Prepend | O(n) — must shift | O(log32 n) | Faster |
| Concatenation | O(n) — must copy | O(log32 n) with RRB | Much faster |
| Slice | O(n) — must copy | O(log32 n) — structural | Much faster |
| Equality check | O(n) — element-wise | O(1) if same root | Much faster |

The constant factors matter. `log32(1,000,000) ≈ 4`. Four pointer hops per random access is not O(n), but it is 4x slower than a single pointer dereference. For sequential iteration, the chunked leaf layout means you're iterating through contiguous 32-element arrays most of the time — close to cache-friendly.

**Persistent HAMT vs Mutable Open-Addressing HashMap:**

| Operation | Mutable HashMap (current) | Persistent HAMT | Ratio |
|-----------|--------------------------|-----------------|-------|
| Lookup | O(1) average | O(log32 n) ≈ 1-6 hops | 2-5x slower |
| Insert | O(1) amortized | O(log32 n) + allocation | 5-15x slower |
| Delete | O(1) | O(log32 n) + allocation | 5-15x slower |
| Iteration | O(n) | O(n) | Similar |
| Equality check | O(n) | O(1) if same root | Much faster |

### The Honest Tradeoffs

**What you gain:**

1. **True immutability.** No aliasing bugs, period. Two references to the same persistent vector can never interfere. Safe to share across threads without synchronization or freezing.

2. **Structural sharing.** "Copying" a 1M-element vector and changing one element costs O(log32 n) ≈ 4 allocations. With mutable arrays, this costs O(n) — a full deep copy. Programs that version, diff, or undo collections benefit enormously.

3. **Cheap equality.** Two persistent structures with the same root pointer are identical — O(1) check. This enables memoization, caching, and change detection patterns that are expensive with mutable arrays.

4. **Referential transparency.** Functions that receive persistent collections can't observe external state changes. This is the foundation of safe concurrency, reliable caching, and predictable behavior.

5. **History for free.** Keeping a reference to the "old" version costs almost nothing — it's just a pointer to a shared subtree. Undo, time-travel debugging, and snapshot semantics come naturally.

**What you lose:**

1. **Raw mutation speed.** Tight loops that push 10M elements will be 3-10x slower than C arrays. This matters for the compiler itself (MIR lowering pushes into statement arrays thousands of times per compilation) and for numerics-heavy user programs.

2. **Memory locality.** C arrays are contiguous. Persistent trees have pointer indirection at each level. Cache miss rates increase. For CPU-bound sequential processing, this is measurable.

3. **Allocation pressure.** Every "update" allocates O(log32 n) new nodes. With Boehm GC, this means more frequent collection cycles. The GC is already conservative (not generational), so increased allocation rate has outsized impact.

4. **Implementation complexity.** A correct, efficient persistent vector (RRB-tree with transients) is ~1,000-2,000 lines of C. A persistent HAMT is similar. This is significant new runtime code that must be correct, tested, and maintained.

### Transients: The Performance Bridge

Clojure solves the builder-phase performance problem with **transients** — temporarily mutable versions of persistent structures:

```
-- Conceptual Glyph syntax:
build_result xs =
  t = transient([])        -- create mutable builder
  _ = t_push(t, 1)         -- O(1) amortized, same as C array
  _ = t_push(t, 2)         -- still mutable
  _ = t_push(t, 3)
  persistent(t)            -- freeze into persistent vector, O(1)
```

A transient is backed by the same tree structure but allows in-place mutation of nodes. It enforces single-owner at runtime — if you alias a transient and mutate from two places, it panics (exactly like the freeze bit, but in reverse: panics on aliased *mutation*, not on post-freeze mutation).

The key insight: **transients and the freeze bit solve the same problem from opposite directions.** The freeze bit says "this C array is done being built." A transient says "this persistent structure is temporarily being built." Both enforce the phase transition from mutable to immutable.

With transients, the stdlib would look identical from the caller's perspective:

```
-- Persistent + transient version (same public API):
map f xs = persistent(map_loop(f, xs, 0, transient([])))

map_loop f xs i acc =
  match i >= array_len(xs)
    true -> acc
    _ ->
      _ = t_push(acc, f(xs[i]))
      map_loop(f, xs, i + 1, acc)
```

Callers get a persistent (immutable) vector. The build phase uses a transient for performance. The public API is unchanged.

### Implementation Strategy for Glyph

A persistent vector in C requires:

1. **Node structure** — 32-wide branching nodes. Each node is `{children[32], count}` for internal nodes or `{elements[32], count}` for leaf nodes. A single-bit flag distinguishes the two.

2. **Root structure** — `{root_node, size, shift, tail}`. The `shift` tracks tree depth. The `tail` is an optimization: the rightmost leaf is stored directly on the root to make append O(1) amortized (Clojure's "tail optimization").

3. **Path copying** — `push`, `set`, and `pop` copy the nodes along the path from root to the affected leaf. ~4 nodes for a million-element vector.

4. **Transient mode** — Each node gains an `owner_id` field. A transient has a unique ID. Mutation checks `node.owner_id == transient.id` — if true, mutate in place (this node was created during this transient session). If false, copy-on-write.

5. **Iteration** — A chunked iterator that walks leaf nodes sequentially. Inner loop iterates a contiguous 32-element array (cache-friendly).

**For hashmaps**, a HAMT implementation requires:

1. **Bitmap + compressed children** — Each node has a 32-bit bitmap indicating which positions are occupied, and a variably-sized `children[]` array holding only the occupied entries. This is more memory-efficient than a full 32-slot array.

2. **Leaf, collision, and internal node types** — Single key-value pairs, multiple entries with the same hash, and branching nodes.

3. **Path copying** — Same principle as persistent vectors.

**Estimated effort:** ~1,500-2,500 lines of C for both persistent vector and HAMT, including transients. This is a significant but well-understood implementation — the algorithms are thoroughly documented in Bagwell's papers and Clojure's source.

### Coexistence With Mutable Arrays

Persistent structures don't have to replace mutable arrays overnight. They can coexist:

**Option A: Two types, explicit choice.**

```
-- Mutable array (current, unchanged):
arr = []
_ = array_push(arr, 1)

-- Persistent vector (new):
vec = pvec(1, 2, 3)
vec2 = vec_push(vec, 4)    -- vec is unchanged, vec2 has 4 elements
```

The type system (eventually) distinguishes `[T]` (mutable array) from `Vec[T]` (persistent vector). User chooses which to use. Stdlib functions could be generic or offer both variants.

**Option B: Persistent by default, mutable opt-in.**

```
-- Default array literal creates persistent vector:
xs = [1, 2, 3]
ys = push(xs, 4)           -- xs unchanged, ys = [1,2,3,4]

-- Mutable builder for hot loops:
t = transient([])
_ = t_push(t, 1)
result = persistent(t)
```

This is the Clojure model. Array literals produce persistent vectors. Transients are the builder pattern. Stdlib internals use transient/persistent boundaries automatically.

**Option C: Persistent vectors + frozen C arrays (hybrid).**

Use persistent vectors as the user-facing type. Keep mutable C arrays internally for the compiler's own use (MIR lowering, parsing) where performance matters and single-ownership is guaranteed. The freeze bit protects C arrays in the rare cases where they escape to user code.

### Recommendation

**Near-term (now):** Ship the freeze bit (Solutions 1-6). It costs almost nothing, is fully backward compatible, and immediately makes stdlib results safe to share.

**Medium-term:** Implement persistent vectors and HAMTs as new runtime types. Expose them alongside mutable arrays. Let the stdlib offer both. Collect performance data from real programs.

**Long-term:** Make persistent the default. Array literals `[1, 2, 3]` produce persistent vectors. Transients handle the builder pattern automatically. Mutable C arrays become an internal implementation detail, used only by the compiler and performance-critical code behind explicit opt-in.

This progression — freeze → coexistence → persistent default — lets Glyph evolve toward true immutability without a flag day, without performance regression in the compiler bootstrap, and with real-world data guiding each step.

---

## Migration Plan

### Phase 1: Runtime Primitives (Immediate)

Add to the C runtime:
- `array_freeze`, `array_frozen`, `array_thaw`
- `hm_freeze`, `hm_frozen`
- `ref`, `deref`, `set_ref`
- Freeze checks in `array_push`, `array_set`, `array_pop`, `array_reverse`
- Freeze checks in `hm_set`, `hm_del`
- `array_slice` returns frozen

**Effort:** ~30 lines of C runtime code. Modify 6 existing functions (add one-line checks). Add 8 new functions.

**Risk:** Near zero. Existing code never calls `array_freeze`, so the freeze bit is never set, so the checks never trigger. Fully backward compatible.

### Phase 2: Stdlib Freeze Boundaries (Immediate, after Phase 1)

Add `array_freeze` to the return path of every stdlib function that builds an array:
- `map`, `filter`, `flat_map`, `zip`, `range`, `sort`, `take`, `drop`

**Effort:** One-line change per function (8 functions).

**Risk:** Low. Any code that mutates a stdlib result will now panic instead of silently working. This is a behavior change, but it catches bugs — if you were pushing onto a `map` result, you were aliasing someone else's data.

### Phase 3: Ref Migration (Near-term)

Replace `[0]` mutable cell patterns in the compiler with `ref`/`deref`/`set_ref`:
- `mk_mir_lower` context fields (~5 fields)
- Type checker engine fields (~2 fields)
- Any user programs using the pattern

**Effort:** Medium — mechanical but touches many functions. Can be done incrementally (one context field at a time, test after each).

**Risk:** Medium. Changing the compiler's internal data layout requires careful testing. The bootstrap chain must still work.

### Phase 4: Compiler Internal Freeze (Medium-term)

Add freeze to compiler functions that return arrays:
- `tokenize`, `parse_fn_def`, `compile_fns`, `collect_*` functions
- MIR parallel arrays (after lowering is complete, freeze the result)

**Effort:** Medium. Requires auditing which compiler-internal arrays are mutated after creation vs used as read-only data.

**Risk:** Medium. Some compiler arrays might be mutated after initial construction in ways that aren't obvious. Need thorough testing via bootstrap chain.

### Phase 5: Persistent Vector and HAMT (Medium-term)

Implement persistent data structures in the C runtime:
- Persistent vector (RRB-tree with branching factor 32, tail optimization, transient mode)
- Persistent hash map (HAMT with bitmap compression, transient mode)
- New runtime functions: `pvec`, `pvec_push`, `pvec_set`, `pvec_get`, `pvec_len`, `pvec_slice`, `pvec_concat`, `phm_new`, `phm_set`, `phm_get`, `phm_del`, `phm_keys`, `transient`, `persistent`, `t_push`, `t_set`

**Effort:** ~1,500-2,500 lines of C. Well-understood algorithms (Bagwell '01, Clojure reference implementation).

**Risk:** Medium. Significant new code, but isolated — existing arrays/maps are untouched. Persistent structures coexist with mutable ones.

### Phase 6: Stdlib Dual-Mode (Medium-term, after Phase 5)

Offer persistent-collection variants of stdlib functions. Either as separate functions (`pmap`, `pfilter`) or by making existing functions generic over collection type.

Collect performance benchmarks comparing mutable+freeze vs persistent+transient across real workloads. This data drives the decision on whether Phase 7 is worthwhile.

### Phase 7: Persistent by Default (Long-term, data-driven)

If benchmarks support it, make persistent structures the default:
- Array literals `[1, 2, 3]` produce persistent vectors
- Stdlib functions operate on persistent collections natively
- Mutable C arrays become an internal/opt-in type for compiler internals and performance-critical code

**Gate:** Only proceed if persistent+transient performance is within 2x of mutable+freeze for representative workloads (compiler bootstrap, web API, game loops).

### Phase 8: Type System Integration (Long-term)

Add collection mutability to the type system:
- `[T]` (persistent/frozen array — immutable)
- `Mut[T]` or `Builder[T]` (transient/mutable — building phase only)
- `Ref[T]` (mutable cell — explicit mutation)
- Functions declare whether they accept/return mutable or immutable collections
- The compiler enforces this statically, upgrading runtime panics to compile-time errors
- `Send` / `Sync` trait equivalents for thread safety

---

## Summary: Evolution of Glyph Collections

### Near-Term (Freeze Bit — Phases 1-4)

| Aspect | Before | After |
|--------|--------|-------|
| Array layout | `{ptr, len, cap}` 24 bytes | Same (freeze bit in cap sign bit) |
| Array performance | O(1) push/set/pop | Same + 1 branch per mutation |
| Stdlib results | Mutable (aliasable) | Frozen (safe to share) |
| `[0]` mutable cells | `array_set(arr, 0, val)` | `set_ref(r, val)` |
| Array literals | Mutable | Mutable (unchanged) |
| Strings, records, enums | Immutable | Immutable (unchanged) |
| Thread safety | Arrays unsafe to share | Frozen arrays safe to share |

### Medium-Term (Persistent Structures — Phases 5-6)

| Aspect | Freeze-Only | With Persistent Structures |
|--------|-------------|---------------------------|
| Collection types | Mutable array + freeze | Mutable array, persistent vector, persistent map |
| "Copy" cost | O(n) deep copy | O(log32 n) structural sharing |
| Equality check | O(n) element-wise | O(1) pointer comparison |
| Stdlib | Returns frozen C arrays | Returns persistent vectors |
| Builder pattern | `[] → push → freeze` | `transient → t_push → persistent` |
| History/undo | Must copy entire array | Free (keep old root pointer) |
| Concatenation | O(n) | O(log32 n) with RRB-trees |

### Long-Term (Persistent Default — Phases 7-8)

| Aspect | Current | End State |
|--------|---------|-----------|
| `[1, 2, 3]` | Mutable C array | Persistent vector (immutable) |
| `map f xs` | Frozen C array | Persistent vector |
| Builder loops | `acc = []; push; freeze` | `t = transient([]); t_push; persistent(t)` |
| Mutable cells | `[0]` hack | `Ref[T]` (first-class) |
| Type system | No mutability concept | `[T]` (immutable) / `Builder[T]` (transient) / `Ref[T]` (cell) |
| Thread safety | Runtime freeze checks | Static enforcement via types |
| C arrays | Everything | Compiler internals only |

---

## Suggested Approach: Freeze as Bridge to Persistent

The central recommendation of this document is a smooth, incremental transition from mutable C arrays to persistent data structures, using the freeze bit as the bridge.

### Why This Works

The freeze bit and persistent structures share the same semantic contract from the caller's perspective: **you receive a collection you cannot mutate.** Whether that collection is a frozen C array or a persistent vector is an implementation detail invisible to the caller.

This means code written against the freeze-bit API today will work unchanged when the underlying representation shifts to persistent structures later:

```
-- This code works identically in both worlds:
xs = [3, 1, 4, 1, 5]
sorted = sort(\a b -> a - b, xs)
-- sorted is immutable — whether via freeze bit or persistent vector
doubled = map(\x -> x * 2, sorted)
-- doubled is also immutable
```

The caller never calls `array_freeze` or `persistent` — the stdlib handles the transition internally. The caller just uses `map`, `filter`, `sort` and gets immutable results.

### The Transition in Detail

**Phase 1-2 (now): Freeze bit + stdlib freeze boundaries.**

The stdlib returns frozen C arrays. All existing code continues to work. New code gets safety guarantees at function boundaries. The convention is established: *functions return immutable collections.*

```
-- stdlib internals (unchanged):
map_loop f xs i acc = ... array_push(acc, ...) ...

-- stdlib public API (one line added):
map f xs = array_freeze(map_loop(f, xs, 0, []))
```

**Phase 3-4: Ref type + compiler internal freeze.**

The `[0]` mutable cell hack is replaced with `Ref`. The compiler's own arrays are frozen where possible. The ecosystem is now accustomed to immutable collections.

**Phase 5-6: Persistent structures land alongside mutable arrays.**

Persistent vectors and HAMTs are implemented in the runtime. The stdlib is updated to use transients internally and return persistent structures. The public API is unchanged — `map` still takes a function and a collection and returns an immutable collection. The only difference is that the returned collection now supports O(log32 n) structural sharing instead of requiring O(n) deep copy via `array_thaw`.

```
-- stdlib with persistent structures (same public API):
map f xs = persistent(map_loop(f, xs, 0, transient([])))

-- Or, if the runtime unifies the interface:
map f xs = freeze(map_loop(f, xs, 0, builder([])))
```

**Phase 7: Persistent becomes the default.**

Array literals `[1, 2, 3]` produce persistent vectors. The accumulator pattern uses transients. Mutable C arrays are still available for compiler internals and FFI-heavy code, but they're the exception rather than the rule.

### Why Not Jump Straight to Persistent?

1. **Implementation cost.** Persistent vectors and HAMTs are ~1,500-2,500 lines of C. The freeze bit is ~30 lines. Shipping safety *now* is better than shipping perfection *later*.

2. **Performance data.** The 2-10x overhead of persistent structures is theoretical. We need benchmarks on real Glyph workloads (compiler bootstrap, web APIs, game loops) to know whether the overhead is acceptable. The freeze bit buys time to collect that data.

3. **Ecosystem readiness.** The convention that "stdlib returns immutable collections" needs to be established before the underlying representation changes. The freeze bit establishes this convention with zero disruption.

4. **Boehm GC interaction.** Persistent structures allocate more frequently (O(log32 n) nodes per update). Boehm GC is conservative and not generational — increased allocation pressure may have outsized performance impact. The freeze bit has zero allocation overhead.

### The Unified API (End State)

In the final state, the distinction between frozen C arrays and persistent vectors disappears behind a unified collection API:

```
-- Creating:
xs = [1, 2, 3]                    -- persistent vector

-- Reading (same as today):
x = xs[0]                         -- O(log32 n), practically O(1)
n = array_len(xs)                 -- O(1)

-- "Updating" (returns new version):
ys = push(xs, 4)                  -- xs unchanged, ys = [1,2,3,4]
zs = set(xs, 0, 99)              -- xs unchanged, zs = [99,2,3]

-- Building (transient for performance):
result = build(\b ->
  _ = b_push(b, 1)
  _ = b_push(b, 2)
  b_push(b, 3))                   -- returns persistent [1,2,3]

-- Stdlib (unchanged from user's perspective):
doubled = map(\x -> x * 2, xs)   -- persistent, immutable
```

The recursive accumulator pattern that Glyph uses for iteration (`map_loop`, `filter_loop`, etc.) maps naturally onto the transient/persistent lifecycle: the `_loop` function operates on a transient builder, and the public entry point returns a persistent result. No language changes required — just a runtime swap.

### The Progression

```
Today:           Mutable arrays everywhere, no safety
Phase 1-2:       Freeze bit + stdlib boundaries → runtime safety, convention established
Phase 3-4:       Ref type + compiler freeze → clean mutable cells, internal discipline
Phase 5-6:       Persistent structures → structural sharing, coexistence, benchmarks
Phase 7:         Persistent default → true immutability
Phase 8:         Type system → static enforcement
```

Each phase is independently valuable, backward compatible with the previous one, and driven by real-world usage data rather than theoretical purity.
