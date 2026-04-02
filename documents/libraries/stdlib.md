# Standard Library Design

**Status:** Implemented (v1) — not yet integrated into compiler
**Date:** 2026-03-25
**Location:** `libraries/stdlib.glyph`

## 1. Problem Statement

Every Glyph example program re-implements basic operations. There are no
`map`, `filter`, `fold`, `sort`, `zip`, `find`, `any`, `all`, `reverse`,
`join`, `split`, `starts_with`, `ends_with`, `contains` functions available
to user programs. Meanwhile, many of these already exist in the C runtime
preamble but are not exposed as callable Glyph functions.

## 2. Design Constraints

- **Single-binary distribution**: The compiler ships as one file (`glyph`).
  Adding companion files increases distribution complexity.
- **Self-contained programs**: A `.glyph` database should be buildable without
  locating external files at build time.
- **LLM-native users**: LLMs are the primary users. They can run setup commands,
  but fewer steps is better — fewer tokens, fewer failure modes.
- **Generalization bug (BUG-007)**: The self-hosted type checker cannot fully
  generalize polymorphic functions. In practice, higher-order stdlib functions
  compile and run correctly — the type checker emits advisory warnings when the
  same function is used at multiple types in one compilation unit, but code
  generation is unaffected.

## 3. Architecture

### 3.1 Chosen Approach: External Library (`libraries/stdlib.glyph`)

A separate `.glyph` database registered via `glyph use` and auto-linked at
build time via the `lib_dep` table. Uses **bare names** (`map`, `filter`,
`fold`) — cleaner for LLM token economy, no collision with compiler prefixes
(`cg_`, `tc_`, `parse_`).

### 3.2 Dependency Layering

The stdlib sits at the bottom of the dependency stack:

```
Layer 3: User programs     — can use stdlib, compiler, and runtime
Layer 2: Compiler          — can use stdlib and runtime
Layer 1: Standard library  — can use runtime only
Layer 0: C runtime         — standalone C code, no Glyph dependencies
```

**The critical constraint**: `stdlib.glyph` must only depend on the C runtime
(Layer 0) and language builtins. It cannot use language features that require
the stdlib to compile. This prevents circular dependencies.

This is a natural constraint because the stdlib is pure algorithmic Glyph:
- `map`, `filter`, `fold` use recursion, closures, pattern matching, arrays
- These are all language primitives — they don't need library support
- The C runtime provides `array_push`, `array_len`, `str_concat`, etc.
- No stdlib function needs another stdlib function to *compile* (though
  stdlib functions can call each other at runtime)

### 3.3 Future: Compiler Integration

The compiler (`glyph.glyph`) can use stdlib via `glyph use`, making it its
own customer. The build chain would produce a single binary:

```
glyph.glyph + stdlib.glyph → glyph (single binary)
```

The bootstrap chain would respect the layering:

```
Stage 0: glyph0 (Rust)   compiles stdlib.glyph → stdlib functions available
Stage 1: glyph0           compiles glyph.glyph + stdlib.glyph → glyph1
Stage 2: glyph1           compiles glyph.glyph + stdlib.glyph → glyph2
Stage 3: glyph2           compiles glyph.glyph + stdlib.glyph → glyph
```

`glyph0` (Rust compiler) already supports `lib_dep` / ATTACH, so no changes
needed to the bootstrap tool.

**What goes where — the rule**: If the compiler needs it to function (parsing,
type checking, codegen, MIR, CLI), it goes in `glyph.glyph`. If it's useful for
programs but the compiler could exist without it, it goes in `stdlib.glyph`.
The compiler *may* use stdlib for convenience (replacing internal ad-hoc
utilities like `arr_extend_s`), but it must not *require* stdlib for
compilation — only for a cleaner implementation.

### 3.4 Distribution

For the compiler itself, distribution remains a single binary once integrated.
`stdlib.glyph` would be compiled in.

For user programs during development, `stdlib.glyph` needs to be available.
Options:

1. **Ship `stdlib.glyph` alongside the binary** — `glyph` + `stdlib.glyph` in
   the tarball. The compiler locates it relative to its own binary path.

2. **`glyph init` embeds stdlib** — when creating a new program, `glyph init`
   copies stdlib definitions into the new database. The program becomes
   self-contained. `stdlib.glyph` is only needed at init time.

3. **`glyph init` registers stdlib** — `glyph init` adds a `lib_dep` entry
   pointing to `stdlib.glyph`. The program links it at build time. Lighter
   databases but requires `stdlib.glyph` present for every build.

Option 2 gives the strongest self-containment guarantee — a `.glyph` file can
be moved anywhere and built without locating external files.

## 4. Implemented Functions

### 4.1 Non-Polymorphic Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `range` | `I -> I -> [I]` | Integer range [start, end) |
| `min` | `I -> I -> I` | Minimum of two integers |
| `max` | `I -> I -> I` | Maximum of two integers |
| `iabs` | `I -> I` | Absolute value (named `iabs` to avoid C `abs` conflict) |
| `clamp` | `I -> I -> I -> I` | Clamp value to [lo, hi] |
| `join` | `S -> [S] -> S` | Join string array with separator (uses string builder) |
| `contains` | `S -> S -> B` | String contains substring (wraps `str_index_of`) |
| `take` | `I -> [a] -> [a]` | First n elements (wraps `array_slice`) |
| `drop` | `I -> [a] -> [a]` | Skip first n elements (wraps `array_slice`) |
| `sum` | `[I] -> I` | Sum of integer array (wraps `fold`) |
| `product` | `[I] -> I` | Product of integer array (wraps `fold`) |

### 4.2 Higher-Order Functions (Polymorphic)

| Function | Signature | Description |
|----------|-----------|-------------|
| `map` | `(a -> b) -> [a] -> [b]` | Apply f to each element |
| `filter` | `(a -> B) -> [a] -> [a]` | Keep elements where pred is true |
| `fold` | `(b -> a -> b) -> b -> [a] -> b` | Left fold / reduce |
| `find_index` | `(a -> B) -> [a] -> I` | Index of first match (-1 if none) |
| `any` | `(a -> B) -> [a] -> B` | True if any element matches |
| `all` | `(a -> B) -> [a] -> B` | True if all elements match |
| `each` | `(a -> V) -> [a] -> V` | Apply f for side effects |
| `zip` | `[a] -> [b] -> [[I]]` | Pair up elements (as 2-element arrays) |
| `flat_map` | `(a -> [b]) -> [a] -> [b]` | Map then flatten |
| `sort` | `(a -> a -> I) -> [a] -> [a]` | Quicksort with comparator |

### 4.3 Internal Helpers

`range_loop`, `map_loop`, `filter_loop`, `fold_loop`, `find_index_loop`,
`any_loop`, `all_loop`, `each_loop`, `zip_loop`, `flat_map_loop`,
`concat_into`, `sort_part`, `join_loop`

### 4.4 Inventory

36 function definitions + 22 test definitions = 58 total definitions.

## 5. BUG-007 Interaction

The higher-order functions are inherently polymorphic. BUG-007 causes the
self-hosted type checker to emit `[tc_err]` warnings when the same function
(e.g., `map`) is used at multiple types in one compilation unit. However:

- **Code generation is unaffected** — all 22 tests pass, including
  `test_map_poly` which uses `map` at both `[I]` and `[S]` types.
- **Type errors are advisory** — they don't gate builds.
- **Single-type usage produces no warnings** — most user programs will use
  `map` at one type per function, which works cleanly.

The practical impact is minimal. A proper fix to BUG-007 would eliminate the
advisory warnings entirely.

## 6. Usage

```bash
# Register stdlib in a program (one-time setup)
glyph use myapp.glyph libraries/stdlib.glyph

# Build and run — stdlib functions are available
glyph build myapp.glyph
glyph run myapp.glyph

# Run stdlib's own tests
glyph test libraries/stdlib.glyph
```

Example program using stdlib:

```
main =
  xs = range(1, 11)
  doubled = map(\x -> x * 2, xs)
  evens = filter(\x -> x % 2 == 0, doubled)
  total = sum(evens)
  _ = println(int_to_str(total))
  sorted = sort(\a b -> a - b, [5, 3, 8, 1, 9, 2])
  _ = println(join(" ", map(\x -> int_to_str(x), sorted)))
  0
```

Output:
```
110
1 2 3 5 8 9
```

## 7. Not Yet Implemented

Functions from the original design that are not yet in stdlib:

| Function | Notes |
|----------|-------|
| `foldr` | Right fold |
| `enumerate` | Pair elements with indices |
| `id` | Identity function |
| `compose` | Function composition (available via `>>` operator) |
| `flip` | Swap argument order |
| `reverse` | Already in C runtime as `array_reverse`; could add Glyph wrapper |

C runtime promotion (exposing existing C functions via extern declarations):
`str_split`, `str_trim`, `starts_with`, `ends_with`, `to_upper`, `to_lower`,
`str_replace`. These are already compiled into every binary and callable from
Glyph — they just aren't in stdlib yet.

## 8. Open Questions

1. **`glyph init` auto-registration**: Should `glyph init` auto-register
   stdlib? Requires modifying `init_schema` in the compiler and deciding on
   a path resolution strategy.

2. **Namespace convention**: Currently bare names. If collisions become a
   problem, `glyph use --ns=std` can filter. No issues encountered so far.

3. **Documentation format**: Each function is self-documenting via
   `glyph get stdlib.glyph <name>`. A structured doc format (signatures,
   examples) would help LLMs discover functions via `glyph dump --budget`.

4. **Versioning**: If stdlib is embedded at init time, how do programs get
   updates? `glyph migrate` or a new `glyph update-stdlib` command.

## 9. Current Status

| Item | Status |
|------|--------|
| `libraries/stdlib.glyph` created | Done |
| Non-polymorphic functions (range, min, max, iabs, clamp, join, contains) | Done |
| Higher-order functions (map, filter, fold, find_index, any, all, each) | Done |
| Collection functions (take, drop, zip, flat_map, sort, sum, product) | Done |
| Unit tests (22 tests, 22/22 passing) | Done |
| Integration test (cross-library `glyph use` + build + run) | Done |
| Compiler integration (`glyph use` in `glyph.glyph`) | Not started |
| Bootstrap chain update (`build.ninja`) | Not started |
| `glyph init` auto-registration | Not started |
| C runtime function promotion | Not started |
| Example program migration to use stdlib | Not started |
| `abs` → `iabs` rename (C conflict with `stdlib.h`) | Done (workaround) |
