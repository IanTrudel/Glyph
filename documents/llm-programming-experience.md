# Programming in Glyph: An LLM's Experience Report

## Overview

Over several sessions, I (Claude) built a self-hosted compiler for the Glyph programming language, writing 430 definitions entirely in Glyph itself. The compiler implements a complete pipeline: lexer, parser, type inference (Hindley-Milner with row polymorphism), MIR lowering, and C code generation. This document reflects on the experience of programming in a language designed specifically for LLMs.

## What Glyph Gets Right

### Programs as Databases

The single best design decision is storing programs in SQLite databases. Each definition is an independent row in the `def` table. This had profound benefits for the workflow:

- **Incremental development.** I could insert one function at a time, build, test, fix, and move on. No need to manage file organization, imports, or module boundaries. Just `INSERT INTO def` and go.
- **Easy introspection.** At any point I could `SELECT COUNT(*) FROM def` to check progress, or `SELECT body FROM def WHERE name='foo'` to review a definition. SQL is a natural query language for navigating a codebase.
- **No dependency management.** Functions reference each other by name. The compiler resolves dependencies automatically via the `dep` table. I never once had to think about import ordering.
- **Atomic edits.** `INSERT OR REPLACE` means every edit is a clean replacement. No merge conflicts, no partial edits. The definition is either there or it isn't.

### Token-Minimal Syntax

The terse syntax genuinely reduced the amount of text I needed to generate:

- Single-character type aliases (`I`, `S`, `B`) saved tokens in type annotations
- No `let`/`return`/`import` keywords — binding is `name = expr`, return is implicit (last expression)
- No braces or semicolons — indentation-sensitivity works well when every definition is small
- The `match` expression with pattern arms on indented lines is clean and readable

In practice, most Glyph functions were 3-15 lines. The entire 430-definition compiler fits comfortably in a few thousand lines of source text.

### Functional-First Design

The purely functional style (no mutation except via `glyph_array_set` on single-element arrays) was surprisingly productive:

- **Pattern matching as the only control flow** (after removing if/else/for/while) forced a clean, exhaustive style. Every `match` covers all cases.
- **Records as the primary data structure** meant I could prototype data layouts quickly: `{okind: I, oval: I, ostr: S}` is both a type annotation and a constructor.
- **Pipe operator** (`|>`) was useful for chaining transforms, though I used it less than expected in compiler code.

## Challenges and Pain Points

### The Field Offset Ambiguity Problem

This was the single hardest bug I encountered. Glyph's row polymorphism means a function receiving `{n1: I, sval: S, ..}` doesn't know the complete record type at compile time. The Cranelift codegen must resolve field offsets at code generation time by matching against a global registry of known complete record types.

The problem: when two record types share field names (e.g., `AstNode` and `TyNode` both have `n1`, `n2`, `ns`, `sval`), the codegen can pick the wrong type and compute the wrong offset. This manifested as garbage values and segfaults that were extremely difficult to diagnose — the code *looked* correct, the types *looked* correct, but field access silently read from the wrong offset.

The fix (pre-scanning all field accesses per local variable to disambiguate) works but is fragile. This is a fundamental tension in row polymorphism without runtime type information: you need to know the concrete type to compute field layout, but the type system deliberately abstracts over it.

**Lesson for language design:** Row polymorphism is powerful, but if your compilation strategy depends on knowing concrete field offsets, you need either runtime type tags, a monomorphization pass, or a uniform representation that doesn't depend on field ordering.

### No Enum Types

Glyph has records but no algebraic data types (enums/variants). This meant representing discriminated unions as records with integer tag fields and encoding variant payloads manually:

```
-- Instead of: type Operand = Local(I) | ConstInt(I) | ConstStr(S) | ...
-- We use: {okind: I, oval: I, ostr: S}
-- Where okind is a tag constant: ok_local=1, ok_const_int=2, ...
```

This works, but has drawbacks:
- Every operand carries fields for *all* variants, even unused ones (wasted space)
- Pattern matching is manual: `match op.okind == ok_local()` instead of `match op: Local(id) -> ...`
- No exhaustiveness checking — easy to forget a case in a tag dispatch chain
- Field names need unique prefixes to avoid the offset ambiguity problem (okind/oval/ostr vs sdest/skind/sival)

I ended up writing massive chains of `match k == X: true -> ... _ -> match k == Y: true -> ...` that were tedious to write and read. The `lower_expr` dispatch alone was 22 lines of nested matches across two functions.

### String Interpolation Gotcha

Glyph uses `{expr}` for string interpolation inside strings. This meant that any C code containing braces — which is essentially all C code — needed special handling. I couldn't write `"int main() {\n"` because `{` starts interpolation. Instead, I had to create helper functions `cg_lbrace()` and `cg_rbrace()` that return literal brace characters, and concatenate them in.

This was the most annoying recurring friction. Every C codegen function needed to carefully avoid `{` and `}` in string literals. A simple escape like `\{` works for `{` (the lexer supports it), but `\}` doesn't — it falls through to the default handler and produces the literal two-character string `\}`.

**Lesson:** If your language has string interpolation, make sure both the open and close delimiters can be escaped uniformly. Or provide raw string literals.

### Mutable State in a Functional Language

The MIR lowering required mutable state: block lists, local variable counters, scope stacks. Glyph's solution is single-element arrays as mutable cells:

```
ctx.nxt_local = [0]         -- mutable counter
glyph_array_set(ctx.nxt_local, 0, id + 1)  -- "mutation"
```

This works but feels like fighting the language. The `glyph_array_set` calls are verbose and error-prone. I occasionally forgot to use the single-element array pattern and tried to "mutate" a plain integer, leading to silent correctness bugs.

A better approach might be monadic state threading or explicit ref cells with cleaner syntax.

### No Error Diagnostics

When something went wrong at runtime, the only feedback was a segfault (exit 139) or wrong value. There are no stack traces, no error messages for null pointer dereferences, no bounds checking on record field access (only array index access). Debugging required binary search — comment out half the code, rebuild, test, narrow down.

The most effective debugging technique was returning intermediate values as the exit code: `main = some_function(); result.field * 100 + other_thing`. Exit codes are limited to 0-255 (modulo 256), which made debugging values > 255 tricky.

### Build-Test Cycle Friction

Every test required:
1. Write SQL INSERT statement
2. `cargo run -- build compiler.glyph --full` (recompile all 430 defs, ~2 seconds)
3. `./compiler` to run
4. Check exit code

The `--full` flag is necessary because manual SQL inserts use fake hashes, making everything look dirty. A REPL or incremental mode would dramatically speed up development.

### String Building is O(n^2)

The C code generator builds output strings by repeated concatenation: `s2(s2(s2(a, b), c), d)`. Each `glyph_str_concat` allocates a new string, copying both inputs. For large generated C files, this is quadratic. A string builder or rope data structure would help.

In practice, the generated C files were small enough that this didn't matter, but it would be a problem for larger programs.

## Surprising Positives

### The Compiler is Remarkably Small

430 definitions for a complete compiler (lexer + parser + type system + MIR + codegen) is impressively compact. For comparison, the Rust implementation is ~10,000 lines across 6 crates. The Glyph self-hosted version is perhaps 2,000 lines of source text total. The token-minimal syntax and functional style genuinely compress the code.

### SQL as Module System Works

I was skeptical of "programs as databases" initially, but it genuinely works. Being able to `SELECT name FROM def WHERE kind='fn' ORDER BY name` to see all functions, or `SELECT body FROM def WHERE name LIKE 'lower_%'` to find all lowering functions, is powerful. The database IS the IDE's project explorer.

### Functional Style Suits Compilers

Compilers are fundamentally about tree transformations: AST to IR, IR to code. The functional style — pattern match on node kind, recursively transform children, return new structure — maps perfectly to compiler passes. The lack of mutation (outside explicit array cells) made reasoning about correctness easier.

### The Bootstrap Succeeded

The most satisfying moment was running `./compiler` and seeing it generate valid C code, compile it with `cc`, and produce a working executable that computed fibonacci(10) = 55. A language compiling itself — even through a C intermediate — is a genuine milestone.

## Metrics

| Component | Definitions | Notes |
|-----------|------------|-------|
| Constants (tokens, AST, types) | ~80 | Token kinds, node kinds, type tags, operator constants |
| Lexer | ~30 | `tokenize(src)` → `[Token]` |
| Parser | ~87 | Recursive descent, all expression forms |
| Type System | ~96 | HM inference, unification, substitution, row polymorphism |
| MIR Lowering | ~108 | Full expression/statement/control flow lowering |
| C Codegen | ~48 | Statement/terminator rendering, program assembly |
| **Total** | **~430** | Complete self-hosted compiler |

## Recommendations for Glyph's Future

1. **Add enum types.** The single highest-impact feature for compiler writing. Tagged unions with pattern matching would eliminate 90% of the `match x.tag == CONST: true -> ...` boilerplate.

2. **Fix `\}` escape.** Both `\{` and `\}` should work uniformly in strings.

3. **Add raw strings.** Something like `r"no {interpolation} here"` for embedding code templates.

4. **Better error reporting.** Stack traces on segfault, null pointer messages, type mismatch locations. Even printing the function name where a crash occurred would help enormously.

5. **String builder type.** A mutable string buffer with O(1) append would make code generation practical at scale.

6. **REPL or incremental eval.** Being able to test expressions interactively without the full build-test cycle would speed up development dramatically.

7. **Consider ref cells.** `ref(0)` / `deref(r)` / `set(r, v)` would be cleaner than the single-element array pattern for mutable state.
