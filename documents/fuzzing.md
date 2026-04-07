# Glyph Fuzzing — Design, Implementation, and Results

## Summary

A full fuzzing framework was designed, implemented, and executed against the Glyph compiler pipeline (tokenizer, parser, type checker, MIR lowering, C codegen). After 500,000+ rounds across multiple generation strategies, pipeline targets, and adversarial inputs, zero crashes were found. The fuzzer was removed from the codebase — self-hosting through a 4-stage bootstrap chain proved to be a more effective robustness guarantee than random testing.

## Motivation

The parser and type checker handle untrusted input (user code). A crash in these components is a real bug. With ~1,800 definitions in the self-hosted compiler, there are plenty of code paths that random generation could explore. The existing coverage infrastructure (`glyph test --cover`) provides function-level hit counts that could guide input selection.

## Implementation (2026-04-06)

### Architecture

```
glyph fuzz <db> [--target=parse|check|compile|run] [--seed=N] [--rounds=N] [--corpus=DIR] [--timeout=S]
```

**Core loop:** generate input -> write to temp file -> `system("timeout 5 ./glyph _fuzz-exec input target")` -> check exit code -> categorize result -> loop.

**Crash detection:** `glyph_system()` returns `128+signal` for signal deaths (139=SIGSEGV, 136=SIGFPE, 134=SIGABRT).

**Process isolation:** fork+exec via `system()` per test case. A crash in the target cannot bring down the fuzzer. ~270-300 rounds/sec throughput.

### Pipeline targets

| Target | Pipeline exercised |
|--------|-------------------|
| `parse` | `tokenize` -> `parse_fn_def` |
| `check` | parse + `mk_engine` -> `register_builtins` -> `tc_pre_register` -> `tc_infer_loop` |
| `compile` | parse + `lower_fn_def` -> `build_ret_map` -> `cg_program` |
| `run` | compile + `cc` syntax validation of generated C |

### Generation strategies

**1. Random token sequences** (`fuzz_gen_source`): Picks tokens from a 55-element table including keywords (`match`, `true`, `Ok`, `Err`, `Some`, `None`), operators (`+`, `-`, `==`, `|>`, `->`, `:=`), delimiters, identifiers, literals, and special characters (`{`, `}`, `\`, `"strings"`).

**2. Structured generation** (`fuzz_gen_structured`): Depth-bounded recursive expression generators that produce syntactically valid Glyph programs. 25 expression generators covering:
- Atoms, binary ops, function calls, match expressions, lambdas
- Arrays, records, maps, pipe chains, field access, field accessors
- String interpolation, string literals, float literals
- Constructors (`Some`, `Ok`, `Err`, `None`), unwrap (`!`), propagate (`?`)
- Array indexing, compose (`>>`), unary negation
- Pathological variants: deep matches (5-15 arms), nested lambdas (10 levels), nested constructors (4 levels), long pipe chains (5-15 stages), many parameters (5-15)

Pattern generation includes: wildcards, identifiers, literals, constructor patterns (`Some(x)`, `Ok(y)`, `Err(z)`, `None`), or-patterns (`a | b`), range patterns (`N..M`), and match guards (`pat ? expr -> body`).

Block generation includes: let bindings, destructuring (`{a, b} = expr`), assignment (`:=`), and comments (`-- fuzz`).

**3. Mutation of real compiler definitions** (`fuzz_gen_mutated`): Loads all 1,596 fn definitions from the target database, picks one randomly, applies 1-15 byte-level mutations (delete, insert, or replace at random positions using a 58-character table).

### Definitions implemented

~50 definitions total across generators, infrastructure, and CLI. All were self-hosted in `glyph.glyph`, compiled with the self-hosted compiler. The fuzzer fuzzed the compiler that compiled the fuzzer.

## Results (2026-04-06)

### Quantitative

| Target | Rounds | Strategy | Crashes |
|--------|--------|----------|---------|
| `parse` | 310,000+ | random tokens | 0 |
| `parse` | 10,000 | mutated real defs | 0 |
| `check` | 10,000+ | structured generation | 0 |
| `check` | 10,000 | mutated real defs | 0 |
| `compile` | 170,000+ | structured + mutation | 0 |
| `run` (cc validation) | 5,000 | structured | 0 |
| **Total** | **515,000+** | **all strategies** | **0** |

### Adversarial inputs (hand-crafted)

30 adversarial edge cases were tested across all pipeline targets (90 tests total):

- Empty function body, match with no arms, lambda with no body, match arm with no body
- 50 match arms, 20 parameters, 10-level nested closures, 8-level nested constructors
- 5-level nested match, deeply nested parens, chained field access (`x.a.b.c.d.e`)
- Double unwrap (`x!!`), double propagate (`x??`), pipe to nothing (`x |>`)
- All escape sequences, nested string interpolation
- Or-pattern with 10 alternatives, complex guard expressions
- Assignment to literal (`42 := x`), destructuring non-record, duplicate record fields
- Very long identifiers (500 chars), empty records, empty array index

**Result: 0 crashes. Every input was handled gracefully across all 3 pipeline stages.**

## Analysis

### Why no bugs were found

The self-hosting process is a more thorough test than any fuzzer can be. The compiler compiles itself through a 4-stage bootstrap chain processing ~1,800 definitions. Every tokenizer path, every parser production, every MIR lowering case, every codegen pattern — they all must work correctly for the compiler to exist. The compiler's own source code is the most comprehensive test corpus possible.

Random fuzzing generates inputs that are either:
- **Too malformed** — rejected early by the parser (most random/mutated inputs)
- **Too shallow** — simple expressions that exercise only basic code paths
- **Structurally similar** — the generator vocabulary, however wide, produces inputs that follow predictable patterns

The compiler was already hardened against all of these by processing 1,800 real definitions that span the full complexity spectrum.

### The `kind='fuzz'` question

Extending fuzzing to user programs (`kind='fuzz'` definitions) was considered but doesn't fit the Glyph ecosystem. Glyph programs are databases, not text files. The traditional fuzzing model (generate random bytes, feed to parser) assumes file-based input processing. In Glyph:
- Definitions enter through controlled channels (`put_def`, MCP tools)
- Input is structured (one definition at a time), not raw text
- LLMs are the primary users — they generate well-formed code
- Property-based testing (`kind='prop'`) already covers structured random input with user-defined invariants

The only meaningful addition fuzzing could offer over property testing is crash isolation and corpus persistence — features better bolted onto `kind='prop'` than maintained as a separate system.

## Decision: Fuzzer removed (2026-04-07)

The ~50 fuzzer definitions were removed from `glyph.glyph`. Rationale:
- 500K+ rounds across all strategies found 0 bugs
- Maintenance burden (50 definitions) with zero ROI
- Self-hosting provides stronger robustness guarantees
- The `kind='fuzz'` extension doesn't fit the database-native model

The experience validated that the compiler pipeline is production-quality. The design and results are preserved in this document for reference.

## Original design document

The original design below was written before implementation. It is preserved for historical context.

### Original phases

**MVP (~15-20 definitions):** CLI command, random token generator, fuzz loop with crash detection, corpus saving, timeout.

**Phase 2 — Coverage guidance (~10 definitions):** Coverage file parsing, cumulative tracking, byte-level mutations, corpus splicing.

**Phase 3 — Structured generation (~15 definitions):** Recursive AST generators, source minimization, type unification target, `kind='fuzz'` for user targets.

All three phases were implemented. Coverage guidance (Phase 2 corpus management) was the only piece not fully realized — there were no crashes to guide corpus selection toward.

### Differences from property testing

| Aspect | `kind='prop'` | `glyph fuzz` |
|--------|--------------|--------------|
| Duration | Fixed trials (default 100) | Continuous until stopped |
| Isolation | In-process | fork+exec per case |
| Crash handling | Crash kills test runner | Detected, input saved |
| Coverage | Not used | Guides input generation |
| Corpus | None | Persistent on disk |
| Target | User-defined invariants | Compiler internals |

In practice, the two converged: property testing with crash isolation would achieve the same results with less infrastructure.

## Prior art

Relates to next-steps.md item 25 (property-based testing / fuzzing). The property testing half is complete (v0.5.2). This document covers the fuzzing half — designed, implemented, executed, and retired.
