# Data.Array Model for Glyph

## Data.Array in a Nutshell

Haskell's `Data.Array` has three key properties:

1. **Constructed all-at-once** — `listArray (0, n-1) elems` or `array (lo, hi) [(i, v), ...]`
2. **Immutable after construction** — no push, no set. Period.
3. **`//` for functional updates** — `arr // [(i1, v1), (i2, v2)]` does a full O(n) copy with modifications

The mutable side lives in `ST`/`IO` monads (`Data.Array.MArray`), and `runSTArray` freezes a mutable array into an immutable one — the type system guarantees the mutable version can't escape.

## How This Maps to Glyph

The appeal is **simplicity**. No RRB-trees, no HAMTs, no transients, no bitmap-compressed tries. Just:

| Concept | Haskell | Glyph Equivalent |
|---------|---------|-------------------|
| All-at-once construction | `listArray (0,n) xs` | `array_of(n, \i -> f(i))` / array literal `[1,2,3]` (frozen) |
| Immutable access | `arr ! i` | `xs[i]` (unchanged) |
| Functional update | `arr // [(i, v)]` | `array_update(xs, i, v)` → new array, O(n) |
| Mutable builder | `runSTArray (do ...)` | Builder pattern: `build(\b -> push(b,...); push(b,...))` |
| Accumulation | fold into `STArray` | `accumulate(n, f, init, assocs)` |

The dominant `map` pattern becomes declarative:

```
-- Current (imperative accumulator):
map f xs = map_loop(f, xs, 0, [])
map_loop f xs i acc =
  match i >= array_len(xs)
    true -> acc
    _ ->
      _ = array_push(acc, f(xs[i]))
      map_loop(f, xs, i + 1, acc)

-- Data.Array style (declarative):
map f xs = generate(array_len(xs), \i -> f(xs[i]))
```

That's a real improvement — no mutation at all, just a function from index to value.

## Where It Gets Tricky

**`filter`** — you don't know the output length upfront. Options:
- Two-pass: count matches, then `generate`
- Build with a mutable builder, then freeze
- Over-allocate, fill, truncate (what C stdlib `qsort`-style code does)

**Compiler internals** — MIR lowering pushes statements one at a time as it walks the AST. You genuinely don't know how many statements a function will have until you've lowered it. This requires a mutable builder.

**`sort`** — recursive quicksort/mergesort builds sub-arrays. With O(n) copy on every `//` update, sorting by functional update would be O(n^2 log n). You'd need a mutable builder internally.

## The Honest Comparison

| | Freeze Bit (current plan) | Data.Array Style | Persistent (RRB/HAMT) |
|-|---------------------------|------------------|-----------------------|
| Implementation cost | ~30 lines of C | ~100 lines of C | ~2,000 lines of C |
| `generate(n, f)` | N/A (still push loop) | O(n), single alloc | O(n) via transient |
| Functional update | `array_thaw` + set + freeze = O(n) | `array_update` = O(n) | O(log32 n) |
| Random access | O(1) | O(1) | O(log32 n) |
| Push (builder) | O(1) amortized | O(1) via builder | O(log32 n) |
| Append after construction | Panic (frozen) | Panic (immutable) | O(log32 n), new version |
| Memory per "copy" | O(n) full copy | O(n) full copy | O(log32 n) shared |

## What Is Interesting Here

The `generate` / `tabulate` constructor is the real win from Data.Array. It eliminates the accumulator loop for a huge class of operations (`map`, `zip`, `range`, `enumerate`). And it's orthogonal to the freeze-bit plan — you could add `generate` to the runtime right now:

```c
GVal glyph_array_generate(GVal n, GVal fn) {
    long long len = (long long)n;
    long long* hdr = malloc(24);
    long long* data = malloc(len * 8);
    for (long long i = 0; i < len; i++) {
        data[i] = glyph_call_closure(fn, i);  // fn(i)
    }
    hdr[0] = (long long)data; hdr[1] = len; hdr[2] = len;
    // freeze the result — it's a finished value
    hdr[2] |= (1LL << 63);
    return (GVal)hdr;
}
```

Single allocation, no reallocs, and the result comes out frozen. Combined with multi-line lambdas (which were just shipped), the usage is clean:

```
doubled = generate(array_len(xs), \i ->
  x = xs[i]
  x * 2)
```

The Data.Array model doesn't replace the freeze-bit plan — it augments it. You still need mutable builders for `filter`, `sort`, and compiler internals. But for the ~60% of array operations that are index-to-value transforms, `generate` is both faster (one allocation vs. repeated `push` with potential reallocs) and cleaner (no mutation, no freeze needed).

## Relationship to Existing Plans

The `generate` + `accumulate` primitives can be added as runtime functions alongside the freeze bit. The two approaches are complementary:

- **Freeze bit** handles the cases where you must build incrementally (unknown length, conditional inclusion, compiler internals)
- **`generate`** handles the cases where the output length is known and each element is a function of its index (map, zip, range, enumerate, tabulate)

Together they cover the full spectrum of array construction patterns in Glyph.
