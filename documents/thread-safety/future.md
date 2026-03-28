# The Future of Thread Safety and Immutability in Glyph

## Where We Stand

Phases 1-4 of the thread safety roadmap are complete. The runtime and compiler now have a solid foundation for immutability:

**Phase 1 — Runtime Primitives.** The freeze bit (sign bit of `cap`) is implemented for both arrays and hashmaps. `array_freeze`, `array_frozen`, `array_thaw`, `hm_freeze`, `hm_frozen` are runtime functions. All mutation operations (`array_push`, `array_set`, `array_pop`, `array_reverse`, `hm_set`, `hm_del`) check the freeze bit and panic on violation. `array_slice` and `hm_keys` return frozen results. The `Ref` type (`ref`, `deref`, `set_ref`) provides explicit 8-byte mutable cells. `generate(n, fn)` enables declarative frozen-at-birth array construction. Global runtime state (`_glyph_current_fn`, `_glyph_call_stack`, `_glyph_call_depth`) is `__thread`-qualified.

**Phase 2 — Stdlib Freeze Boundaries.** Every stdlib function that builds and returns an array now freezes its result: `map`, `filter`, `flat_map`, `zip`, `range`, `sort`. The convention is established — *functions return immutable collections.*

**Phase 3 — Ref Migration.** All 33+ uses of the `[0]` mutable cell pattern across the compiler (`mk_mir_lower`, `mk_engine`), libraries (`async.glyph`, `web.glyph`), and examples (`prolog`, `gwm`) have been replaced with `ref`/`deref`/`set_ref`. Mutation is now semantically explicit everywhere.

**Phase 4 — Compiler Internal Freeze.** `tokenize` returns frozen tokens. `build_program`, `build_program_llvm`, and `build_test_program` freeze their MIR arrays after all transformation passes (fix_field_offsets → fix_extern_calls → tco_transform → **freeze**). The compiler's own pipeline treats intermediate results as immutable data.

The Rust bootstrap compiler (`runtime.rs`) and Cranelift codegen (`build.rs`) support all of these primitives. The full 4-stage bootstrap chain passes. All 356 self-hosted tests and 76 Rust tests pass. All 17 example programs build and run.

**What this gives us today:**

| Value type | Immutable? | Thread-safe? |
|-----------|-----------|-------------|
| Integers, floats, bools | Always | Yes |
| Strings | Always | Yes |
| Records | Always (functional update) | Yes |
| Enum variants | Always | Yes |
| Closure captures | Always (captured by value) | Yes |
| Arrays (stdlib results) | Frozen at boundary | Yes (read-only) |
| Arrays (user-built) | Mutable until frozen | No (aliasing possible) |
| Hashmaps (stdlib results) | Frozen at boundary | Yes (read-only) |
| Hashmaps (user-built) | Mutable until frozen | No |
| Refs | Always mutable (by design) | No |

The foundation is laid. Everything below is what comes next.

---

## Phase 5: Threading Primitives

### What Glyph Needs

A minimal set of concurrency primitives that make the immutability work above actually useful:

```
-- Thread creation
handle = spawn(\u -> expensive_computation(data))
result = join(handle)

-- Mutual exclusion (for shared mutable state)
m = mutex_new()
_ = mutex_lock(m)
_ = mutex_unlock(m)
-- or scoped: with_lock(m, \u -> critical_section())

-- Channels (message passing)
ch = channel_new()
_ = channel_send(ch, value)
value = channel_recv(ch)
```

### Runtime Implementation

**`spawn(fn)`** wraps `pthread_create`. The runtime allocates a thread handle record, registers the new thread with Boehm GC via `GC_register_my_thread`, and invokes the closure's function pointer. The `__thread` globals from Phase 1 mean each thread gets its own `_glyph_current_fn` and call stack — stack traces work correctly per-thread from day one.

```c
typedef struct {
    GVal closure;
    GVal result;
    pthread_t thread;
    int joined;
} GlyphThread;

void* glyph_thread_entry(void* arg) {
    GlyphThread* gt = (GlyphThread*)arg;
    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
    GVal (*fp)(GVal, GVal) = (GVal(*)(GVal,GVal))((GVal*)gt->closure)[0];
    gt->result = fp(gt->closure, 0);
    GC_unregister_my_thread();
    return NULL;
}

GVal glyph_spawn(GVal closure) {
    GlyphThread* gt = GC_MALLOC(sizeof(GlyphThread));
    gt->closure = closure;
    gt->joined = 0;
    pthread_create(&gt->thread, NULL, glyph_thread_entry, gt);
    return (GVal)gt;
}

GVal glyph_join(GVal handle) {
    GlyphThread* gt = (GlyphThread*)handle;
    if (!gt->joined) {
        pthread_join(gt->thread, NULL);
        gt->joined = 1;
    }
    return gt->result;
}
```

**Mutex**, **channel**, and **atomic ref** follow the same pattern — thin C wrappers around pthreads primitives, registered as Glyph runtime functions.

### The Freeze Boundary at `spawn`

The critical safety rule: **values passed to `spawn` must be safe to share.** Frozen arrays, frozen hashmaps, strings, records, enums, integers — all safe. Mutable arrays, mutable hashmaps, and bare Refs — unsafe.

Two enforcement strategies:

**Strategy A: Auto-freeze on spawn.** The `spawn` implementation walks the closure's captured values and freezes any mutable arrays/hashmaps it finds. Refs are rejected (panic). This is the Erlang-inspired approach — the runtime guarantees safety, the programmer doesn't think about it.

**Strategy B: Panic on unsafe spawn.** The `spawn` implementation walks captured values and panics if any mutable array, mutable hashmap, or Ref is found. The programmer must explicitly freeze or copy before spawning. This is more explicit but catches bugs earlier.

**Recommendation: Strategy B** (panic on unsafe) for initial implementation. It's simpler, has zero hidden cost (no surprise deep-freeze walks), and teaches correct patterns. Strategy A can be offered as `spawn_freeze(fn)` for convenience.

### Boehm GC Configuration

Build with `-DGC_THREADS` and link the thread-safe `libgc`. Add to `cg_main_wrapper`:

```c
GC_INIT();  // already present — must be called before any allocation
```

Each spawned thread calls `GC_register_my_thread` on entry and `GC_unregister_my_thread` on exit (shown above). Boehm handles the rest — it stops all threads during collection (stop-the-world), scans each thread's stack, and resumes. This is correct if not optimal. A future move to a concurrent or incremental collector (Immix, G1-style) can happen behind the same API.

### Coverage Counters

Coverage instrumentation (`_glyph_cov_hits`) is currently a single global array. With threads, two options:

1. **Thread-local counters + merge on join.** Each thread maintains `__thread` counters, merged into the global array at `GC_unregister_my_thread` time. Clean, no synchronization during execution.
2. **Atomic increments.** Use `__atomic_fetch_add` on the global array. Simpler code, small overhead per function call.

Option 1 is better for performance-sensitive workloads. Option 2 is acceptable if coverage is a debugging tool (already behind `#ifdef GLYPH_COVERAGE`).

---

## Phase 6: Channels and Message Passing

Channels are the safe concurrency primitive — they eliminate shared mutable state by design. Glyph's `async.glyph` library already has a channel abstraction (in-process, cooperative scheduling). The next step is OS-thread-safe channels.

### Bounded and Unbounded

```
-- Unbounded (queue grows as needed)
ch = channel_new()

-- Bounded (blocks sender when full)
ch = channel_bounded(capacity)
```

### Implementation

An OS-thread channel is a mutex-protected queue with condition variables for blocking:

```c
typedef struct {
    GVal* buffer;
    long long head, tail, len, cap;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int closed;
} GlyphChannel;
```

**Send** locks, enqueues (or blocks if bounded and full), signals `not_empty`.
**Recv** locks, dequeues (or blocks if empty), signals `not_full`.
**Close** sets `closed`, broadcasts both condition variables. Subsequent sends panic; recvs return a sentinel or `None` (when optional types are supported).

### Values Crossing Channels

The same freeze-boundary logic as `spawn` applies: values sent through a channel must be safe to share. `channel_send` either auto-freezes or panics on mutable collections, depending on the chosen strategy.

Frozen arrays crossing a channel are zero-copy — the receiver gets the same pointer. The freeze bit guarantees it won't be mutated. This is cheaper than Erlang's deep-copy model (Erlang copies because it has per-process heaps; Glyph shares a single GC heap).

### Select / Multiplex

A `select` primitive for waiting on multiple channels simultaneously:

```
result = select([ch1, ch2, ch3])
match result.channel
  _ ? result.channel == ch1 -> handle_ch1(result.value)
  _ ? result.channel == ch2 -> handle_ch2(result.value)
  _ -> handle_ch3(result.value)
```

This requires `pthread_cond_wait` with multiple condition variables or a unified wait mechanism (`poll`-style). It can wait for Phase 7 or be implemented as a runtime primitive with a polling loop.

---

## Phase 7: Persistent Data Structures

The freeze bit is a discipline mechanism — it tells you *when* mutation is wrong, but mutable arrays before the freeze point can still alias dangerously. Persistent data structures eliminate the problem at the root.

### What Changes

| | Frozen C Arrays (today) | Persistent Structures (future) |
|-|------------------------|-------------------------------|
| "Update" cost | O(n) deep copy via `array_thaw` | O(log32 n) structural sharing |
| Equality check | O(n) element-wise | O(1) pointer comparison |
| History / undo | Must save full copies | Free (keep old root) |
| Concatenation | O(n) | O(log32 n) with RRB-trees |
| Random access | O(1) | O(log32 n) ≈ 4-6 hops |
| Sequential iteration | O(1) per element | O(1) amortized (chunked leaves) |
| Append | O(1) amortized | O(log32 n) |
| Memory per "copy" | O(n) full copy | O(log32 n) shared nodes |

The constant factors matter: `log32(1,000,000) ≈ 4`. Four pointer hops per access is not O(n), but it is measurably slower than a single dereference for tight loops.

### Implementation: Persistent Vector (RRB-Tree)

A 32-way branching tree where leaf nodes are contiguous 32-element chunks:

```
Root: { node*, size, shift, tail* }
  └── Internal: { children[32], count }
       └── Leaf: { elements[32], count }
```

Key operations:
- **`pvec_push(vec, val)`** — path-copy from root to rightmost leaf, append element. Returns new root. Old root unchanged.
- **`pvec_set(vec, i, val)`** — path-copy from root to target leaf, update element. O(log32 n) allocations.
- **`pvec_get(vec, i)`** — traverse tree by index bits. O(log32 n) pointer hops.
- **`pvec_concat(a, b)`** — RRB-tree merge. O(log32 n) rather than O(n).

**Tail optimization** (from Clojure): the rightmost partial leaf is stored directly on the root node. Appends that don't overflow the tail are O(1) — no tree traversal needed. This makes the common `push` pattern fast.

### Implementation: Persistent Hash Map (HAMT)

A trie keyed on hash bits with 32-way branching and bitmap compression:

```
Root: { bitmap: u32, children: [Node | Leaf | Collision]* }
  └── Leaf: { hash, key, value }
  └── Collision: { hash, entries: [(key, value)] }  // same hash, different keys
```

Each internal node stores a 32-bit bitmap indicating which of the 32 positions are occupied, and a compact `children` array holding only the occupied entries. This is memory-efficient — a node with 3 children stores only 3 pointers plus a 4-byte bitmap, not 32 pointers.

**`phm_set(map, key, val)`** — hash the key, walk the trie by 5-bit chunks of the hash, path-copy to the insertion point. O(log32 n) ≈ O(1).

### Transients: The Performance Bridge

For builder patterns where you push thousands of elements, persistent append (O(log32 n) per push with allocation) is 3-10x slower than mutable C array push. Transients solve this:

```
-- Conceptual Glyph API:
result = build(\b ->
  _ = b_push(b, 1)
  _ = b_push(b, 2)
  _ = b_push(b, 3))
-- result is persistent (immutable)
```

A transient is a persistent tree in "edit mode." Each node gains an `owner_id`. A transient has a unique ID. When mutating:
- If `node.owner_id == transient.id`, mutate in place (this node was created during this transient session).
- Otherwise, copy-on-write.

This makes builder loops nearly as fast as mutable C arrays — the common case is in-place mutation of freshly allocated nodes.

**`persistent(transient)`** clears the owner_id on all nodes, returning an immutable persistent structure. O(1) — just null the ID. No copying.

### Coexistence Strategy

Persistent structures don't replace mutable C arrays overnight. The transition:

1. **Introduce `pvec` and `phm` as new runtime types** alongside existing arrays/hashmaps.
2. **Offer both variants in stdlib** — `map` returns a persistent vector, `map_mut` returns a frozen C array (for code that needs maximum performance).
3. **Collect benchmarks** on real workloads: compiler bootstrap, web APIs, game loops.
4. **If benchmarks support it**, make persistent the default for array literals (Phase 8).
5. **Keep mutable C arrays** as an internal type for compiler internals, FFI buffers, and performance-critical code.

### Estimated Effort

~1,500-2,500 lines of C for both persistent vector (RRB-tree) and HAMT, including transients. The algorithms are well-documented in Phil Bagwell's papers ("Ideal Hash Trees", 2001) and Clojure's reference implementation. This is significant but well-understood work.

---

## Phase 8: Async and Coroutines

### The Model: Stackful Coroutines

Three approaches exist for async with a C codegen backend:

| Approach | Compiler changes | Memory cost | Complexity |
|----------|-----------------|-------------|------------|
| Stackful coroutines | None | ~8KB-1MB per coroutine | Low |
| Compiler state machines (Rust-style) | Major (MIR transform) | Minimal | Very high |
| CPS transform | Moderate | Moderate | High |

**Stackful coroutines** via `ucontext`/`swapcontext` (POSIX) or a lightweight library (`minicoro`, `libaco`) are the right choice for Glyph. Reasons:

1. **Zero compiler changes.** Async functions are regular functions that yield at I/O points. No MIR splitting, no state machine generation, no colored function problem.
2. **Natural fit with closures.** Glyph's heap-allocated closures `{fn_ptr, captures...}` wrap coroutine entry points directly.
3. **Works with Boehm GC.** Coroutine stacks are registered with the collector.
4. **Simpler mental model.** Async code looks like synchronous code. No `async`/`await` syntax needed (though it can be added as sugar).

### Architecture

```
                    ┌─────────────┐
                    │  Scheduler  │
                    │ (run queue) │
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         ┌────┴───┐   ┌───┴────┐  ┌────┴───┐
         │ Task 1 │   │ Task 2 │  │ Task 3 │
         │ (coro) │   │ (coro) │  │ (coro) │
         └────────┘   └────────┘  └────────┘
                           │
                    ┌──────┴──────┐
                    │  Event Loop │
                    │ (epoll/     │
                    │  io_uring)  │
                    └─────────────┘
```

The scheduler maintains a run queue of ready coroutines. When a coroutine hits an I/O operation:

1. Register the fd with the event loop.
2. Suspend the coroutine (save stack context).
3. Schedule the next ready coroutine.
4. When the event loop signals the fd is ready, re-enqueue the coroutine.

### Event Loop

For I/O multiplexing:
- **`libuv`** — cross-platform, proven by Node.js. The pragmatic first choice.
- **`io_uring`** — Linux-only, highest performance. Add as a fast path later.
- **`epoll`/`kqueue`** — direct syscalls, less abstraction. Good for a minimal custom implementation.

### Interaction with Threads

Coroutines and OS threads are complementary:
- **Coroutines** handle I/O concurrency (thousands of concurrent network connections, file operations).
- **OS threads** handle CPU parallelism (computation spread across cores).

A work-stealing scheduler (like Go's runtime or Tokio's multi-threaded scheduler) can run N coroutines across M OS threads. But this is an optimization — the initial implementation can be single-threaded (all coroutines on one OS thread) with a thread pool for blocking operations.

### Blocking FFI

Most C library functions block (`read()`, `fwrite()`, SQLite queries). When called from an async context, they would block the entire scheduler. Solution: **offload to a thread pool.**

The runtime maintains a pool of OS threads for blocking work. When a coroutine calls a blocking extern:
1. Move the call to a worker thread.
2. Suspend the calling coroutine.
3. Resume the coroutine on the scheduler thread when the worker completes.

This requires no annotation — all externs are assumed potentially blocking. Library authors who know their extern is non-blocking can opt out (future optimization).

### LLVM Coroutine Intrinsics

The LLVM IR backend (`--emit=llvm`) can eventually use `@llvm.coro.*` intrinsics for zero-overhead compiler-generated coroutines. This is not required for the initial implementation but provides a future optimization path that avoids stack allocation overhead entirely.

---

## Phase 9: Type System Integration

The type system is where runtime safety becomes compile-time safety. This is the most ambitious phase and the one that should be driven entirely by real usage patterns from Phases 5-8.

### Collection Mutability in Types

```
-- Immutable array (persistent vector or frozen C array)
xs : [I]

-- Mutable builder (transient, cannot escape function)
builder : Mut[I]

-- Mutable cell (explicit mutation point)
counter : Ref[I]
```

Functions declare what they need:

```
-- Accepts immutable, returns immutable
map : (I -> I) -> [I] -> [I]

-- Accepts immutable, builds internally, returns immutable
sort : (I -> I -> I) -> [I] -> [I]

-- Modifies in place (rare, explicit)
fill : Mut[I] -> I -> V
```

### Send and Sync

Inspired by Rust, but adapted for Glyph's runtime enforcement model:

- **`Send`** — a type whose values can be transferred to another thread. All immutable types are Send. Mutable arrays are Send only after freezing. Refs are not Send.
- **`Sync`** — a type whose values can be *shared* (accessed concurrently by reference) across threads. Frozen arrays are Sync. Refs with atomic operations (`AtomicRef`) are Sync.

Initially these are conventions enforced at runtime (the `spawn` boundary check). The type system would upgrade them to compile-time errors:

```
-- This would be a compile error:
r = ref(0)
h = spawn(\u -> set_ref(r, deref(r) + 1))  -- ERROR: Ref[I] is not Send
```

### Effect Tracking

Mark functions with effects they perform:

```
-- Pure: no side effects, safe to memoize/parallelize
pure_fn : I -> I

-- IO: performs I/O, cannot be parallelized automatically
io_fn : S -> !S

-- Async: may suspend (coroutine)
async_fn : S -> !S
```

This is the most speculative part of the type system work. Effect systems are notoriously difficult to design well — they tend to either be too restrictive (Haskell's IO monad) or too permissive (meaningless annotations). Glyph should wait until there are enough real async and concurrent programs to understand what distinctions actually matter before designing an effect system.

### What Not to Build

Glyph should **not** pursue:

- **A borrow checker.** Rust's ownership model is powerful but adds enormous complexity to the type system, language surface, and learning curve. Glyph's functional core + freeze bit + persistent structures achieve safety through a different mechanism (immutability rather than tracked ownership). Adding a borrow checker to a language designed around immutability is solving the same problem twice.

- **Linear types.** Same reasoning — Glyph doesn't need to track resource lifetimes at the type level because GC handles memory and immutability handles sharing. Linear types are valuable for languages that manage resources manually (Rust, ATS). Glyph is not that language.

- **A GIL.** The entire point of this roadmap is to avoid needing one. The combination of immutable data, freeze boundaries, and explicit Refs means Glyph can support true parallelism from day one without a global lock.

---

## Phase 10: GC Evolution

Boehm GC is the right choice today. It supports threads (`-DGC_THREADS`), works with C codegen, and is battle-tested. But it has limitations that will matter as Glyph programs get larger and more concurrent:

### Boehm's Limitations

1. **Conservative scanning.** Boehm doesn't know which words are pointers and which are integers. It treats every aligned word as a potential pointer. This causes false retention — integers that happen to look like valid heap addresses keep dead objects alive.

2. **Stop-the-world collection.** All threads pause during GC. For programs with many threads (web servers, async runtimes with thousands of coroutines), this creates latency spikes.

3. **No generational collection.** Young objects (recently allocated) are not collected more frequently than old objects. Most functional programs allocate heavily (persistent data structure updates create many short-lived nodes), and a generational collector would handle this better.

4. **No compaction.** The heap fragments over time. Long-running servers will see increasing memory usage even if live data stays constant.

### Evolution Path

**Near-term: Stay on Boehm.** The `GC_MALLOC`/`GC_register_my_thread` API is thin enough that a future swap is feasible. Keep the GC interface narrow.

**Medium-term: Precise scanning.** The C codegen can emit stack maps (tables that tell the GC which stack slots hold pointers). This eliminates false retention and enables a precise collector. The LLVM backend can use `@llvm.gcroot` or the statepoint mechanism for precise GC integration.

**Long-term: Custom concurrent collector.** An Immix-style collector (mark-region, concurrent marking, incremental compaction) would give Glyph:
- No stop-the-world pauses (concurrent marking while mutators run)
- Generational behavior (fast nursery collection for short-lived allocations)
- Compaction (defragmentation for long-running servers)

This is a major undertaking (~10,000+ lines of C) but is the natural end state for a language with functional semantics and heavy allocation patterns.

**Alternative: Region-based allocation.** For programs with clear phase boundaries (parse → compile → codegen), region-based allocation (allocate into a region, free the entire region at once) is simpler than GC and has zero overhead. Compiler pipelines are a natural fit. This could coexist with GC — regions for compiler internals, GC for user programs.

---

## Phase 11: FFI Thread Safety

### The Model: FFI is Unsafe

Following Rust's approach: all `extern` calls are inherently unsafe for concurrent use. The compiler and runtime don't try to reason about C code's thread safety. Library authors provide safe wrappers.

```
-- Library author wraps unsafe FFI in a safe API:
-- gtk.glyph ensures all GTK calls happen on the main thread
-- network.glyph uses internal mutexes for socket state
-- sqlite.glyph uses SQLITE_THREADSAFE mode + connection-per-thread
```

### Async FFI Bridging (Advanced)

Three patterns for library authors who want deeper async integration:

| Pattern | Mechanism | Example |
|---------|-----------|---------|
| Blocking extern | Offloaded to thread pool automatically | `db_query(conn, sql)` |
| Callback-based library | Compiler-generated suspend/resume trampoline | `curl_easy_perform(handle)` |
| Poll/fd-based library | Event loop monitors fd, resumes coroutine | `accept(socket)` |

The default (blocking offload) handles the vast majority of C libraries with zero annotation. The explicit patterns are future optimizations for high-performance integrations.

### Thread-Safe Extern Annotation

A future extension to the extern system:

```
-- Default: assumed not thread-safe, offloaded to thread pool in async context
extern read_file "read" (I, Ptr, I -> I)

-- Explicit: known thread-safe, can be called from any thread
extern thread_safe strlen "strlen" (Ptr -> I)
```

This is an optimization hint, not a safety mechanism. The runtime doesn't trust the annotation — it just avoids the thread pool overhead for `thread_safe` externs.

---

## Implementation Priorities

The phases above are ordered by dependency and value. Here is the recommended execution sequence:

```
Completed:   Phase 1-4 (runtime primitives, stdlib freeze, ref migration, compiler freeze)
                │
Next:        Phase 5  — Threading primitives (spawn, join, mutex)
                │         ↳ ~200 lines of C runtime + is_runtime_fn registration
                │
Then:        Phase 6  — Channels (thread-safe message passing)
                │         ↳ ~150 lines of C runtime
                │
Then:        Phase 7  — Persistent data structures (RRB-tree, HAMT, transients)
                │         ↳ ~1,500-2,500 lines of C runtime
                │         ↳ Stdlib dual-mode (persistent + mutable variants)
                │         ↳ Benchmarks on real workloads
                │
Then:        Phase 8  — Async/coroutines (stackful, event loop)
                │         ↳ ~500 lines of C runtime (scheduler + context switching)
                │         ↳ Event loop integration (libuv or custom epoll)
                │
Then:        Phase 9  — Type system integration (collection mutability, Send/Sync)
                │         ↳ Major type checker changes
                │
Then:        Phase 10 — GC evolution (precise scanning, concurrent collection)
                │         ↳ Optional; Boehm may be sufficient for years
                │
Future:      Phase 11 — FFI thread safety (annotations, async bridging)
                          ↳ Driven by real library needs
```

### Gates Between Phases

- **Phase 5 → 6**: Threads must work before channels matter.
- **Phase 6 → 7**: Channels establish the message-passing pattern. Persistent structures make the messages cheaper to produce (structural sharing instead of deep copy).
- **Phase 7 → 8**: Persistent structures are not required for async, but they make async programs safer — coroutines that share data through persistent collections can't race.
- **Phase 8 → 9**: The type system should encode patterns that real async/concurrent programs have established, not speculative designs.
- **Phase 10**: Independent of Phases 5-9. Can be pursued whenever GC performance becomes a bottleneck.

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Boehm GC + threads causes latency spikes | Medium | Medium | Monitor GC pause times. Stop-the-world pauses scale with heap size. Keep heaps small via regions for compiler internals. |
| Persistent structures are too slow for compiler bootstrap | Medium | High | The compiler is the largest Glyph program and the most allocation-intensive. Benchmark the bootstrap chain with persistent vectors before making them default. Keep mutable C arrays as fallback. |
| Stackful coroutines have high memory overhead | Low | Low | Default 8KB stacks with growth. Stack pooling for reuse. Monitor via `mmap` + guard pages. |
| `spawn` freeze-boundary check is too expensive (deep walk) | Low | Low | Only walk closure captures, not the entire reachable heap. Captures are a flat array of GVals — checking `array_frozen` on each is O(captures). |
| Type system Send/Sync is too restrictive | Medium | Medium | Start with runtime enforcement only. Let real programs establish patterns before encoding them in types. |
| Channel implementation has subtle bugs (deadlock, lost messages) | Medium | High | Use well-established patterns (bounded buffer with condition variables). Extensive test suite including stress tests with many producers/consumers. |
| FFI thread safety is impossible to guarantee | High | Low | Don't try. FFI is unsafe. Library authors wrap. This is Rust's model and it works. |

---

## The End State

When all phases are complete, Glyph will be a functional language where:

- **Immutability is the default.** Array literals produce persistent vectors. Hashmap literals produce persistent hash maps. Updates return new values. The old version is preserved.
- **Mutation is explicit and contained.** `Ref[T]` for mutable cells. Transients for builder patterns. Both are clearly marked in the type system.
- **Concurrency is safe by construction.** Spawning a thread with immutable data requires no synchronization. Channels are the primary communication mechanism. Shared mutable state is possible but requires explicit `AtomicRef` or `MutexRef`.
- **Async is transparent.** Coroutines look like synchronous code. I/O operations yield automatically. The scheduler multiplexes thousands of tasks across a thread pool.
- **The type system enforces safety statically.** `Send`/`Sync` bounds prevent data races at compile time. Collection mutability is tracked in types. Effect annotations distinguish pure from effectful code.
- **Performance is preserved where it matters.** Transients keep builder loops close to C speed. Mutable C arrays remain available for compiler internals and FFI. The GC handles allocation pressure from persistent structure updates.

This is not a revolution — it's a series of incremental, independently valuable steps. Each phase ships safety and capability. Each phase is backward compatible with the previous. Each phase is driven by real usage rather than theoretical purity.

The foundation is built. The path forward is clear.
