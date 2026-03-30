# Glyph Fuzzing Design

## Summary

Property-based testing (`kind='prop'`, shipped in v0.5.2) verifies specific invariants for random inputs. Fuzzing is the complement: continuous generation of arbitrary inputs to discover unknown crashes. The compiler pipeline (tokenizer -> parser -> type inference -> MIR -> codegen) is the primary target.

## Motivation

The parser and type checker handle untrusted input (user code). A crash in these components is a real bug. With 1,800 definitions in the self-hosted compiler, there are plenty of code paths that random generation can explore. The existing coverage infrastructure (`glyph test --cover`) provides function-level hit counts that can guide input selection.

## Design

### Architecture

```
glyph fuzz <db> [--target=parse|unify|compile] [--seed=N] [--corpus=DIR] [--timeout=S]
```

**Core loop:** generate input -> write to temp file -> `system("timeout 5 /tmp/glyph_fuzz_bin input")` -> check exit code -> categorize result -> loop.

**Crash detection:** `glyph_system()` already returns `128+signal` for signal deaths (139=SIGSEGV, 136=SIGFPE, 134=SIGABRT). No new externs needed.

**Process isolation:** fork+exec via `system()` per test case. A crash in the target cannot bring down the fuzzer. ~1-5ms overhead per case, acceptable for a Glyph-hosted fuzzer.

### Fuzz targets

**Target 1 -- Parser (highest value, simplest):** Generate random strings biased toward Glyph-like syntax. Feed through `tokenize` -> `parse_fn_def`. Should never crash, even on garbage input.

**Target 2 -- Type unification:** Generate random type trees via `gen_type(eng, seed, depth)`. Run `unify(eng, t1, t2)`. Verify no crash and check commutativity (`unify(a,b)` succeeds iff `unify(b,a)` succeeds).

**Target 3 -- Full compilation:** Generate parseable programs, run through the entire pipeline. Most complex, catches the deepest bugs.

### Generators needed

- `gen_glyph_source` -- random token sequences assembled with structural bias toward valid Glyph syntax
- `gen_identifier` -- valid Glyph identifiers (lowercase + alphanumeric, avoiding keywords)
- `gen_expr` / `gen_type` -- recursive AST/type generators with bounded depth (for targets 2-3)

Builds on existing stdlib PRNG: `xorshift64`, `seed_next`, `gen_int_range`, `gen_str`, `gen_array`.

### Corpus management

- `--corpus=DIR` (default `/tmp/glyph_fuzz_corpus/`)
- `crashes/` -- inputs causing signal deaths, named by seed
- `failures/` -- assertion failures or nonzero exit
- `interesting/` -- inputs that expanded coverage (when coverage-guided)

### Coverage guidance (Phase 2)

The `--cover` flag already produces function-level hit counts as TSV. The fuzzer can:

1. Build target with `-DGLYPH_COVERAGE`
2. After each case, read coverage file
3. Compare against cumulative bitmap
4. Save coverage-expanding inputs to corpus for mutation

Function-level granularity is coarser than AFL's edge coverage but useful -- different compiler code paths correspond to different functions (e.g., `parse_match_expr` only hit when input contains `match`).

## Implementation phases

### MVP (~15-20 new definitions)

- `cmd_fuzz` CLI command
- `gen_glyph_source` random source text generator
- `build_fuzz_program` harness builder (like `build_test_program`)
- `fuzz_loop` core loop with crash detection via `system()` return codes
- Crash saving to corpus directory
- Timeout via `timeout` utility

### Phase 2 -- Coverage guidance (~10 definitions)

- Coverage file parsing and cumulative tracking
- Corpus management (save coverage-expanding inputs)
- `mutate_source` byte-level mutations (bit flip, byte insert/delete, corpus splicing)

### Phase 3 -- Structured generation (~15 definitions)

- `gen_expr` / `gen_type` recursive AST generators
- `shrink_source` source-level minimization (line/token deletion)
- Type unification and full compilation targets
- Optional `kind='fuzz'` for user-defined fuzz targets

## Differences from property testing

| Aspect | `kind='prop'` | `glyph fuzz` |
|--------|--------------|--------------|
| Duration | Fixed trials (default 100) | Continuous until stopped |
| Isolation | In-process | fork+exec per case |
| Crash handling | Crash kills test runner | Detected, input saved |
| Coverage | Not used | Guides input generation |
| Corpus | None | Persistent on disk |
| Target | User-defined invariants | Compiler internals |

The two are complementary. Props verify "this property holds." Fuzzing discovers "this input crashes."

## Prior art

Relates to next-steps.md item 25 (property-based testing / fuzzing). The property testing half is complete (v0.5.2). This document covers the fuzzing half.
