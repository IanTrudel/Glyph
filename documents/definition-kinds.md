# Definition Kinds in Glyph

**Date:** 2026-03-28
**Status:** Implemented (schema migration #9)

---

## 1. Schema

```sql
kind TEXT NOT NULL CHECK(kind IN ('fn', 'type', 'test', 'const', 'data'))
```

Applied to glyph.glyph, all 9 libraries, all 19 examples, and documentation.glyph via migration #9. Rust compiler (glyph0) schema updated in `crates/glyph-db/src/schema.rs`.

Previously the schema allowed 9 kinds (`fn`, `type`, `trait`, `impl`, `const`, `fsm`, `srv`, `macro`, `test`). Five were retired: `trait`, `impl`, `fsm`, `srv`, `macro`. Two were added: `const`, `data`. See Section 3 for rationale.

---

## 2. Kind Semantics

### `fn` — Functions (existing)

Code that gets parsed, type-checked, lowered to MIR, and compiled.

```
glyph put app.glyph fn -b 'add x y = x + y'
```

### `type` — Type Definitions (existing)

Named record and enum types. Gen=2 produces `typedef struct` in C codegen.

```
glyph put app.glyph type -b 'Point = {x: I, y: I}'
glyph put app.glyph type -b 'Color = Red(I) | Green(I) | Blue(I)'
```

### `test` — Test Definitions (existing)

Test functions compiled and run by `glyph test`.

```
glyph put app.glyph test -b 'test_add u = assert_eq(add(1, 2), 3)'
```

### `const` — Named Constants (new)

The body is the raw value. No `name = expr` wrapping needed — the def name is the constant's name, the body is the constant's value. Not parsed as code.

```
glyph put app.glyph const -b '8080'                          # integer
glyph put app.glyph const -b '0.4.1'                         # version string
glyph put app.glyph const -b 'Usage: app [options]'          # help text
glyph put app.glyph const -b 'CREATE TABLE foo (id INTEGER)' # SQL template
```

Covers strings, templates, numeric values, configuration — anything that is a named text value rather than executable code. The body is stored as `TEXT` in the `body` column and embedded as a string constant in the compiled binary.

**Pipeline:** No parsing, no type-checking, no MIR lowering. Codegen embeds the body directly as a `static GVal` string constant initialized at startup.

### `data` — Binary Blobs (new)

Raw byte content stored as text. The body bytes are embedded directly into the compiled binary as a static array.

```
glyph put app.glyph data -b 'raw text payload' -n payload
echo -n 'binary data' | base64 | glyph put app.glyph data -b - -n encoded_blob
```

**Storage:** The `body` column is `TEXT`. The raw bytes of the body string are embedded at compile time. For true binary data (images, etc.), encode as base64 before storing and decode at runtime using `b64_decode` from stdlib.

**Pipeline:** No parsing, no type-checking, no MIR lowering. Codegen iterates the body characters and emits a static byte array + length as a global.

**Runtime type:** Accessed as a Glyph array of integers (`[I]`) where each element is a byte (0-255).

---

## 3. Retired Kinds

### `trait` and `impl`

Traits and impl blocks are human ergonomic features for ad-hoc polymorphism. In an LLM-native language, the LLM generates specialized functions directly. Monomorphization handles generic code. Dictionary-passing or vtable dispatch adds complexity with no LLM benefit.

### `fsm` — State Machines

Originally spec'd as declarative state machine syntax. State machines are implemented naturally as functions with match expressions.

### `srv` — Server Routes

Originally spec'd as declarative HTTP route definitions. The `web.glyph` library handles routing through regular functions with a builder pattern.

### `macro` — Macro Definitions

In an LLM-native language, the LLM itself is the macro system — it generates code directly.

---

## 4. Summary

| Kind | Body | Parsed? | Type-Checked? | MIR? | Codegen | Storage |
|------|------|---------|---------------|------|---------|---------|
| `fn` | `name params = expr` | Yes | Yes | Yes | C function | TEXT |
| `type` | `Name = {fields\|variants}` | Yes | Yes | No | `typedef struct` | TEXT |
| `test` | `name unit = assertions` | Yes | Yes | Yes | Test binary fn | TEXT |
| `const` | Raw text value | No | No | No | Static string | TEXT |
| `data` | Raw byte content | No | No | No | Static byte array | TEXT |

### Implementation Status

- Schema migration: **Done** (migration #9, applied to all databases)
- Rust compiler types: **Done** (AST, model, parser, resolver updated)
- Self-hosted `init_schema`: **Done** (updated CHECK constraint)
- `const` codegen: **Done** (zero-arg C/LLVM function returning `cstr_to_str(body)`)
- `data` codegen: **Done** (raw body bytes emitted as static byte array → `[I]`)
- `glyph put` support for const/data: **Done** (`-b`, `-f`, `-b -` stdin, `-n name` required)
- `read_stdin` runtime function: **Done** (reads all stdin, used by `-b -`)
- `base64` stdlib: **Done** (`b64_encode`/`b64_decode` in stdlib.glyph for user programs)
