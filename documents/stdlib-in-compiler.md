# Would stdlib.glyph help glyph.glyph?

## The numbers

| Metric | Value |
|--------|-------|
| Total functions in glyph.glyph | 1,317 |
| Functions using `glyph_array_push` | 144 (11%) |
| `_loop` functions (manual recursion) | 120 (9%) |
| Token cost of all `_loop` fns | 5,990 / 37,252 (16%) |
| Simple map-like loops (push every element) | ~28 |
| Filter-like loops (conditional push) | ~8 |
| Complex/stateful loops (tokenizer, parser) | ~84 |

## Q1: Mutability reduction — **Yes, at the API level**

About **28-35 manual `_loop` functions** could be replaced by `map`/`filter`/`fold`/`flat_map` calls. Examples:

**Before** (2 defs, ~15 tokens):
```
foo items = foo_loop(items, 0, [])
foo_loop items i acc =
  match i >= glyph_array_len(items)
    true -> acc
    _ ->
      glyph_array_push(acc, transform(items[i]))
      foo_loop(items, i + 1, acc)
```

**After** (1 def, ~8 tokens):
```
foo items = map(\x -> transform(x), items)
```

The mutation doesn't disappear — it moves inside stdlib. But that's the point: **stdlib becomes the freeze boundary**. Once freeze-bit lands, stdlib functions freeze their output and callers get immutable arrays. Without stdlib, every call site would need manual `array_freeze`.

However, the **majority of mutation (84+ complex loops)** is inherently stateful — tokenizer, parser, MIR lowering, codegen string building. These can't be replaced by `map`/`filter`. The compiler is fundamentally a stateful pipeline.

## Q2: Code quality — **Moderate improvement**

- **~28-35 function *definitions* eliminated** (2-3% of total). Fewer `_loop` helpers cluttering the namespace.
- **More declarative intent**: `filter(\x -> x > 0, items)` says *what*, not *how*.
- **Closure limitation caps the benefit**: Multi-line lambdas don't parse, so complex transforms still need helper functions, partially negating the gain.

## Q3: Token efficiency for LLMs — **The real win is cognitive, not raw**

- **Raw token savings: ~200 tokens (~0.5%)**. Modest.
- **Cognitive token savings: significant.** An LLM reading `map(f, xs)` instantly grasps the pattern. Reading a manual 7-line `_loop` requires tracing the recursion to understand it's just a map. This matters for:
  - **Generation**: LLMs can emit `filter(pred, xs)` instead of re-inventing the loop each time
  - **Comprehension**: Standard vocabulary (`map`, `fold`, `filter`) is universally recognized
  - **Composability**: `sum(map(\x -> x.size, items))` chains naturally

## Practical caveats

1. **Bootstrap complexity**: `glyph use glyph.glyph libraries/stdlib.glyph` adds stdlib to the build. The bootstrap chain (glyph0 compiling glyph.glyph) would need to resolve and compile those defs too. Should work but needs testing.

2. **Naming**: glyph.glyph uses `glyph_array_push` (prefixed runtime calls). stdlib uses `array_push` (mapped to `glyph_array_push` at codegen). No conflict, but the compiler's own code would call stdlib functions that internally use the unprefixed form.

3. **Not a silver bullet**: The compiler's heaviest mutation hotspots (`tok_loop` at 2,282 chars, `parse_record_loop` at 1,674 chars) are complex state machines. No stdlib function helps there.

## Bottom line

**stdlib helps, but modestly.** It would eliminate ~28-35 boilerplate loops, provide a standard functional vocabulary for LLMs, and — critically — establish the natural freeze boundary for the future immutability plan. The token savings are small (<1%), but the code quality and LLM-friendliness improvements are real. The biggest immutability challenges (144 functions with `glyph_array_push`, 35 with `array_set`) remain because they're inherently stateful compiler operations.
