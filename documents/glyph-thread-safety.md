# Glyph Thread Safety

## Current State

Glyph is fundamentally single-threaded. Several layers prevent safe concurrent execution:

1. **Global mutable state in the runtime** — `_glyph_current_fn` (current function name for stack traces), the coverage instrumentation globals, `_glyph_argc`/`_glyph_argv`
2. **Boehm GC** — the collector *can* be built thread-safe (`-DGC_THREADS`), but Glyph doesn't configure it that way
3. **No threading primitives** — no spawn, join, mutex, channel, or atomic operations in the language or runtime
4. **String builder / array internals** — `sb_append`, `array_push` do unsynchronized realloc on shared heap data
5. **SQLite programs-as-databases** — the program format itself is SQLite, which has its own threading model

## Motivation

Ruby and Python both deferred thread safety by implementing a Global Interpreter Lock (GIL). This started as "we don't need threads yet" and became a decade-long architectural debt touching every part of their runtimes.

- **Python/Ruby**: bolted on a GIL because C extensions and runtime internals assumed single-threaded access. Removing it years later required auditing every refcount operation, every global, every C extension API. Python is only now removing the GIL (PEP 703, free-threaded builds in 3.13+), and the pain of retrofitting has been enormous.
- **Java**: started with threads from day 1 but got the memory model wrong (pre-JSR-133). Still, having it baked in from the start meant the fix was a spec clarification, not a runtime rewrite.

Glyph is early enough that designing for thread safety now is relatively cheap compared to retrofitting later. The C codegen backend gives direct control over what primitives get emitted.

Beyond threads, we want to support multiple concurrency models — including Rust Tokio-style async/await, green threads, and potentially actors or CSP channels. Thread safety is not just about threads; it is about making the entire runtime robust from the get-go so that any concurrency model can be layered on top without fundamental redesign.

## Concurrency Models to Support

| Model | Inspiration | Use Case |
|-------|------------|----------|
| OS threads | Java, C pthreads | CPU-bound parallelism |
| Async/await | Rust Tokio, JS | I/O-bound concurrency (network, file) |
| Green threads / fibers | Go goroutines, Erlang | Lightweight concurrency |
| Channels | Go, Rust mpsc | Message-passing between tasks |
| Actors | Erlang/Elixir, Akka | Isolated state, fault tolerance |

Not all need to ship at once, but the runtime foundations must not preclude any of them.

## What Thread Safety Requires

### Phase 1: Runtime Foundations

Make the existing runtime safe for concurrent access, even before adding threading primitives to the language.

**1.1 Eliminate global mutable state**
- `_glyph_current_fn` → thread-local (`__thread` / `_Thread_local`)
- `_glyph_argc` / `_glyph_argv` → set once at startup, read-only thereafter (already safe, just document)
- Coverage counters → thread-local accumulation, merge on join
- Any future global state must justify itself; default is thread-local or immutable

**1.2 Configure Boehm GC for threads**
- Build with `-DGC_THREADS` and link `-lgc` (thread-safe variant)
- Call `GC_INIT()` from main thread before spawning
- Each thread must call `GC_register_my_thread` / `GC_unregister_my_thread`
- Alternative: evaluate switching to a concurrent GC later (Immix, etc.)

**1.3 Make data structures safe**
- Strings (fat pointers): already immutable after creation — safe as-is
- Arrays: `array_push` / realloc need either:
  - Per-array mutex (fine-grained), or
  - Copy-on-write semantics, or
  - Separate concurrent array type
- StringBuilder: not designed for sharing — document as thread-local only
- Hash maps (`hm_*`): same concerns as arrays

**1.4 FFI safety**
- C extern functions are opaque to the compiler — cannot be assumed thread-safe
- Need an annotation or convention: `extern thread_safe` vs default `extern` (assumed unsafe)
- Unsafe externs called from multiple threads → user's responsibility, but compiler can warn

### Phase 2: Language Primitives

Introduce concurrency at the language level.

**2.1 Thread spawn/join**
```
handle = spawn(fn)
result = join(handle)
```

**2.2 Synchronization**
```
m = mutex_new()
mutex_lock(m)
mutex_unlock(m)
// or scoped: with_lock(m, fn)
```

**2.3 Channels (message passing)**
```
ch = channel_new()
channel_send(ch, value)
value = channel_recv(ch)
```

**2.4 Async/await (future phase)**
```
result = await fetch_url(url)
// or: fetch_url(url) |> await
```
Requires an event loop runtime (libuv, io_uring, or custom). This is the most complex model to add but the most important for I/O-heavy programs (web servers, API clients).

### Phase 3: Type System Integration

The type system can enforce safety guarantees.

- **Send** / **Sync** traits (Rust-inspired): types that can be transferred across threads vs shared
- **Mutable reference exclusivity**: if Glyph gains a borrow checker or ownership model, thread safety comes naturally
- **Effect system**: mark functions as `async`, `blocking`, `pure` — the compiler enforces valid composition

## Implementation Priority

1. **Phase 1.1 + 1.2** — thread-local globals + GC config. Zero language changes, purely runtime. Makes the foundation safe.
2. **Phase 1.3** — data structure audit. Decide immutable-by-default vs mutable-with-locking.
3. **Phase 2.1 + 2.2** — basic threading. Minimal viable concurrency.
4. **Phase 2.3** — channels. Enables safe communication patterns.
5. **Phase 2.4 + Phase 3** — async and type system. The long game.

## Design Decisions

### Immutable by default

Glyph is essentially a functional programming language. Records are already functional — `rec{field: val}` produces a new record, not a mutation. This is a strong foundation for thread safety: immutable data is inherently safe to share across threads without synchronization.

The path forward is to lean into this:
- Records: already immutable. No change needed.
- Strings: already immutable after creation. Safe.
- Arrays: currently mutable (`array_push`). The default should become immutable (persistent/copy-on-write), with an explicit mutable variant for performance-critical single-threaded code.
- Hash maps: same treatment as arrays.

This sidesteps the majority of data race issues at the design level rather than trying to bolt on locking after the fact. Languages that got this right early (Erlang, Clojure, Haskell) never had to fight the GIL battle.

### Boehm GC is the right choice for now

Most programming languages gain maturity after 12-15 years. Even with the accelerating power of LLMs, Glyph is in the early stages of that arc. Boehm GC is well-tested, supports threads (`-DGC_THREADS`), and is low-friction with C codegen. Switching to a custom concurrent GC (Immix, region-based, etc.) is a later-stage optimization — worth planning for but not worth building now. The GC interface should be kept thin so a future swap is possible.

### Async strategy: stackful coroutines (recommended)

Three approaches exist for async with a C codegen backend:

| Approach | Compiler changes | Memory cost | Complexity |
|----------|-----------------|-------------|------------|
| Stackful coroutines | None | ~8KB-1MB per coroutine | Low |
| Compiler state machines (Rust) | Major (MIR transform) | Minimal | Very high |
| CPS transform | Moderate | Moderate | High |

**Recommendation: stackful coroutines** via `ucontext`/`swapcontext` (POSIX) or a lightweight library (`minicoro`, `libaco`).

Rationale:
1. **Zero compiler changes** — async functions are just regular functions that happen to yield. The C codegen pipeline does not need to learn how to split functions into state machines.
2. **Natural fit with existing closures** — Glyph already has heap-allocated closures `{fn_ptr, captures...}`. Wrapping a coroutine entry point in a closure is straightforward.
3. **Works with Boehm GC** — coroutine stacks can be registered with the collector via `GC_register_my_thread` or by allocating stacks through `GC_MALLOC`.
4. **Simpler mental model** — from the programmer's perspective, async code looks like synchronous code that transparently yields at I/O points. No colored function problem.
5. **Proven at scale** — Go's goroutines use this model (green threads on a scheduler) and handle millions of concurrent tasks.

Memory overhead is managed through stack pooling and small initial stacks with growth (split stacks or segmented stacks).

The LLVM backend can later use `@llvm.coro.*` intrinsics for zero-cost compiler-generated coroutines as an optimization path, but this is not required for the initial implementation.

**Event loop**: the coroutine scheduler needs an event loop for I/O multiplexing. Options:
- `io_uring` (Linux, highest performance)
- `epoll` (Linux, well-understood)
- `kqueue` (macOS/BSD)
- `libuv` (cross-platform, proven by Node.js)

`libuv` is the pragmatic first choice for portability. `io_uring` can be added as a Linux-specific fast path later.

### FFI and thread safety: the Rust lesson

The fundamental question is: how much should the Glyph runtime know about C code's thread safety?

**Rust's answer is simple: all FFI is `unsafe`.** Rust doesn't try to annotate, wrap, or reason about C thread safety. It draws a hard boundary — everything inside the boundary is Rust's problem, everything outside is yours. If you want a safe API, *you* write a safe Rust wrapper around the unsafe call. The wrapper is where thread safety gets enforced (via `Mutex`, `Send`/`Sync` bounds, etc.). The compiler and runtime don't need to know anything about C thread safety.

This is the right model for Glyph. Most C code is not thread-safe:
- SQLite needs careful `SQLITE_THREADSAFE` configuration
- GTK must be called from the main thread only
- ncurses is completely single-threaded
- Even "thread-safe" libraries like libcurl require per-handle isolation

Asking library authors to annotate externs with async metadata and write correct trampolines is a huge burden that scales poorly.

**Glyph's approach: FFI is unsafe with respect to threading. Library authors provide safe wrappers.**

- All `extern` calls are inherently unsafe for concurrent use
- The Glyph library author (not the FFI system or the compiler) decides how to make it safe
- For `gtk.glyph`: the library wraps GTK calls and ensures they happen on the main thread
- For `network.glyph`: the library can use a mutex around socket state
- The async runtime does not need special FFI handling

This means zero new annotations, zero new C wrapper complexity, and the runtime doesn't distinguish between FFI and native calls for scheduling purposes.

### FFI async bridging (advanced, optional)

For library authors who want tighter async integration, three patterns exist in C libraries. These are opt-in optimizations, not requirements — the default model above handles everything correctly.

**1. Blocking C functions** (most common — `read()`, `fwrite()`, SQLite queries, etc.)

A blocking extern called from an async context must not block the entire scheduler. Solution: **offload to a thread pool**, similar to Tokio's `spawn_blocking`.

```
// Glyph sees this as async-safe because the runtime handles it
result = db_query(conn, sql)   // actually runs on a worker thread, resumes coroutine on completion
```

The runtime maintains a pool of OS threads for blocking work. When an async task calls a blocking extern, the scheduler:
1. Moves the call to a worker thread
2. Suspends the calling coroutine
3. Resumes the coroutine when the worker thread completes

This requires no annotation — all externs are assumed blocking by default.

**2. Callback-based C libraries** (libuv, libcurl multi, GTK main loop)

These libraries accept a function pointer + user data, and invoke the callback when an operation completes. Bridge strategy:

```
// C side: library calls glyph_async_resume(coroutine_handle) in the callback
// Glyph side: the compiler generates a trampoline that suspends the coroutine and passes its handle as callback user data
```

This would require a `extern async_callback` annotation so the compiler knows to generate the suspend/resume trampoline:

```
extern async_callback curl_easy_perform "curl_easy_perform" (Ptr -> I)
```

**3. Poll-based / fd-based C libraries** (raw sockets, file descriptors, `io_uring`)

These return a file descriptor that becomes ready. The event loop monitors the fd directly:

```
extern async_fd net_accept "accept" (I -> I)
// Runtime registers the fd with epoll/kqueue/io_uring, suspends coroutine, resumes when fd is ready
```

**Summary of the layered approach:**

| Extern kind | Annotation | Runtime behavior |
|-------------|-----------|-----------------|
| `extern` (default) | none | Unsafe for threads. Library author wraps for safety. In async context, blocking calls offloaded to thread pool. |
| `extern async_callback` | explicit, opt-in | Compiler generates suspend/resume trampoline with callback bridge |
| `extern async_fd` | explicit, opt-in | Runtime registers fd with event loop, suspends until ready |

The default handles the vast majority of C libraries with zero annotation. The explicit annotations are future optimizations for library authors who want to eliminate thread pool overhead for specific high-performance integrations. They should not be designed or built until the basic model proves insufficient.

## Addendum: How Functional Languages Handle Mutable Collections

Strings in Glyph are already immutable fat pointers — safe to share across threads as-is. Arrays and hash maps are the real challenge, as they are currently fully mutable (`array_push`, `hm_set` modify in place).

Three functional languages offer established models for this problem:

### Erlang

No mutable arrays or maps at all. Everything is immutable, always. Lists are linked lists (prepend O(1), append O(n)). Maps are immutable — `maps:put(K, V, Map)` returns a new map. Tuples are fixed-size immutable arrays. When you need "mutable" state, you use a process (actor) that holds state in its recursive loop. The runtime is optimized for this — small immutable values get copied between processes (no shared heap between actors), and the per-process GC is trivially concurrent because nothing is shared. The tradeoff is performance: CPU-bound array-heavy code is not Erlang's strength.

### Haskell

Immutable by default, with explicit opt-in for mutability:
- `Data.List` / `Data.Vector` — immutable. Operations return new values.
- `Data.Map` / `Data.HashMap` — immutable (persistent, tree-based with structural sharing). O(log n) updates that share most of the old structure.
- `Data.IORef` / `Data.STRef` — explicit mutable references, but the type system enforces that `IORef` lives in `IO` monad and `STRef` can't escape its scope (`ST` monad with rank-2 types).
- `Data.Vector.Mutable` — mutable arrays, but only usable inside `ST` or `IO`. The type system prevents accidentally sharing them.

Haskell lets you have mutable arrays/maps for performance, but the type system guarantees they can't be shared unsafely.

### Clojure

All data structures are immutable and persistent. Vectors, hash maps, sets all use hash array mapped tries (HAMTs) with structural sharing. Updating a 1M-element map creates a new map that shares ~99.99% of the old tree. O(log32 n) — effectively O(1) in practice. For hot loops, Clojure offers `transient` — a temporarily mutable version that you "freeze" back into a persistent structure when done. Transients enforce single-thread ownership at runtime.

### Comparison

| Language | Default | Mutable option | Safety mechanism |
|----------|---------|---------------|-----------------|
| Erlang | Immutable, always | Process state (actors) | No shared memory, period |
| Haskell | Immutable (persistent) | IORef/STRef/MVector | Type system prevents escape |
| Clojure | Immutable (persistent HAMT) | Transients | Runtime single-owner check |

### Implications for Glyph

Clojure's model is likely the closest fit for Glyph:
- **Persistent data structures by default** — arrays and maps would use structural sharing (e.g., HAMTs) so that "updates" produce new values cheaply without copying the entire structure.
- **Transients for performance** — a temporarily mutable version for hot loops (like the for-loop accumulator pattern, which desugars to index-based iteration with `array_push`). Transients enforce single-owner at runtime.
- **Freeze on thread boundary** — transients are automatically frozen back to persistent form when sent across threads.

Glyph doesn't have Haskell's type system to enforce safety statically, but a runtime ownership check (like Clojure's) is simple to implement. And the functional nature of Glyph means most code would naturally use the immutable path anyway — mutable transients would be an optimization, not the default.

## Addendum: Performance Cost of Immutable Collections

**This section requires long reflection.** Glyph currently compiles to C and gets native C-level performance for arrays and hash maps. Switching to persistent data structures has a real, measurable cost that must be weighed carefully against the thread safety benefits.

### The honest numbers

None of the functional languages achieve immutable collections without performance penalty. They manage the cost well enough that it doesn't matter for *most* workloads, but the cost is real.

**Clojure's persistent vectors** use hash array mapped tries (HAMTs). Random access is O(log32 n) instead of O(1). In practice that means 1-3 pointer hops for collections up to billions of elements (log32 of 1 billion ≈ 6). That's roughly 5-10x slower than a C array for random access. Sequential iteration is nearly as fast because leaf nodes are contiguous 32-element chunks.

**Haskell** acknowledges the cost explicitly — that's why `Data.Vector.Mutable` and `Data.Vector.Unboxed` exist alongside immutable vectors. Developers switch to mutable arrays when performance matters and let the type system keep them safe.

**Erlang** is genuinely slow for array-heavy work. No persistent vector exists — you use lists or the `array` module (a tree). Programs needing fast array access use ETS (in-memory database tables) or NIFs (C extensions). The language simply isn't designed for that use case.

### Concrete overhead by operation

| Operation | C array (current Glyph) | Persistent vector | Penalty |
|-----------|------------------------|-------------------|---------|
| Random read | O(1) | O(log32 n) ≈ O(1) | ~2-5x |
| Sequential read | O(1) | O(1) amortized (chunked) | ~1.2-2x |
| Append | O(1) amortized | O(log32 n) | ~3-10x |
| Random update | O(1) | O(log32 n) + allocation | ~10-30x |
| Bulk build (loop) | O(n) | O(n) with transient | ~1.5-3x |

### Why this matters for Glyph specifically

Glyph currently generates C arrays (`{ptr, len, cap}`) that are as fast as hand-written C. This is a genuine competitive advantage — a functional language with C-level collection performance. Moving to persistent data structures would regress performance across the board, even if only by small constant factors for most operations.

The for-loop pattern is the critical case. Glyph desugars `for x in xs` to index-based iteration with `array_push` into an accumulator. With transients (temporarily mutable, single-owner), that stays close to C speed because the transient *is* a mutable array under the hood. Without transients, every loop iteration allocates a new array node — unacceptable.

### Possible paths forward

**Option A: Persistent by default, transients for speed (Clojure model)**
- Most code uses persistent arrays/maps — 2-5x slower, but safe
- For-loop accumulators automatically use transients — the compiler knows they're single-owner by construction
- Explicit `transient`/`freeze` for user code that needs mutable performance
- Risk: the 2-5x constant factor penalty is always there for non-loop code

**Option B: Mutable by default, ownership tracking (Go/Rust-inspired)**
- Keep current C arrays. Fast by default.
- Arrays are thread-local. Crossing a thread boundary requires explicit copy or freeze.
- Runtime ownership check: panic if a mutable array is accessed from multiple threads.
- Risk: less "functional" in feel, more runtime checks

**Option C: Hybrid — mutable in single-threaded context, copy on thread share**
- Keep current C arrays for single-threaded programs (zero cost)
- When concurrency is used, arrays that cross thread boundaries are automatically deep-copied or converted to persistent form
- For-loop accumulators stay mutable always (provably single-owner)
- Risk: two code paths in the runtime, complexity

**Option D: Defer — keep mutable arrays, don't share them**
- The simplest option. Arrays and maps simply cannot be sent across threads.
- Threads communicate via channels that copy values (like Erlang's message passing, but without persistent structures)
- Any shared state lives in an actor/process with message-passing access
- Risk: limits expressiveness, but may be sufficient for a long time

### Current assessment

Option D (defer) or Option C (hybrid) are the most pragmatic near-term choices. They preserve Glyph's current C-level performance while providing a safe foundation for concurrency. Option A (full persistent) is the theoretically cleanest but sacrifices a real advantage. The choice should be driven by actual concurrent programs written in Glyph — until there are real workloads to benchmark, optimizing for a hypothetical is premature.

The cases where persistent data structure overhead would actually be felt — tight numerical loops, image processing, game physics, large dataset manipulation — are cases where C FFI is the right tool anyway.
