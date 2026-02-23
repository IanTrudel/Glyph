# Structs, Data Types, and Generational Bootstrap

*Design notes for Glyph's next major evolution. February 2026.*

## Context

Glyph's self-hosted compiler works but suffers from **i64 type erasure**: every value at the machine level is `long long`. Records are heap-allocated pointer arrays with alphabetically-sorted fields. The codegen has no knowledge of which concrete type a variable holds, leading to the field offset ambiguity problem — the single largest source of complexity in glyph.glyph (35+ mitigation definitions).

The extern system (`extern_` table) similarly suffers: all parameters and returns are `long long`, making it impossible to pass C structs, handle complex memory layouts, or interop naturally with real C libraries. The gled assessment showed that LLM sessions bypass the extern system entirely in favor of hand-written C wrappers.

Both problems have the same root cause: **no named struct types at the codegen level**.

## The Proposal: Named Structs in C Codegen

### Language Surface

Named struct definitions already exist in Glyph's syntax:

```
Point = {x: I, y: I}
TyNode = {tag: I, n1: I, n2: I, ns: [I], sval: S}
```

These are stored as `kind='type'` definitions in the `def` table. The Rust compiler uses them for type inference but erases them during codegen. The proposal: **make the C codegen emit actual C structs**.

### Generated C Output (Before)

```c
// Record: anonymous heap allocation, fields at 8-byte offsets
long long p = glyph_alloc(16);
((long long*)p)[0] = x_val;  // field 'x' (alphabetical offset 0)
((long long*)p)[1] = y_val;  // field 'y' (alphabetical offset 1)
long long x = ((long long*)p)[0];  // field access
```

### Generated C Output (After)

```c
typedef struct {
    long long x;
    long long y;
} GlyphPoint;

GlyphPoint* p = (GlyphPoint*)glyph_alloc(sizeof(GlyphPoint));
p->x = x_val;
p->y = y_val;
long long x = p->x;
```

### What This Eliminates

1. **Field offset ambiguity** — C compiler resolves `p->n1` to the correct offset for whichever struct type `p` is declared as. No more `build_type_reg`, `coll_local_acc`, `resolve_fld_off`, `fix_all_field_offsets`, `find_best_type`, `alpha_rank`. That's ~35-40 definitions that become dead code.

2. **The `-Wno-int-conversion` hack** — struct pointers are properly typed. The generated C code compiles cleanly.

3. **Unreadable generated code** — `p->name` is self-documenting. `((long long*)p)[3]` is not.

4. **GDB opacity** — with real struct types, GDB can print `p->name = "hello"` instead of showing raw pointer arithmetic.

### What This Enables

1. **Natural FFI** — extern declarations can reference Glyph struct types. The compiler generates C wrappers that pass real `struct` pointers to C libraries, not `long long` casts.

2. **Struct-aware extern system** — the `sig` column in `extern_` can reference named types: `Point -> I -> Point` instead of only `I -> I -> I`.

3. **Self-hosted compiler cleanup** — the compiler can use its own struct types internally. `TyNode.tag` instead of heuristic offset guessing.

## Generational Bootstrap

### The Problem

Currently, adding a new feature to Glyph requires implementing it in **both** the Rust compiler and the self-hosted compiler. This is token-inefficient and creates maintenance burden. But glyph0 (Rust) must be able to compile glyph.glyph, so glyph.glyph can only use features glyph0 understands.

### The Insight: Programs Are Databases

Glyph's core innovation — programs as SQLite databases — provides a natural solution. A single `glyph.glyph` database can contain **multiple generations** of definitions. Each generation is compiled by the previous generation's output.

### Schema Change

Add a `gen` (generation) column to the `def` table:

```sql
ALTER TABLE def ADD COLUMN gen INTEGER NOT NULL DEFAULT 1;
```

A gen=1 definition is visible to all generations. A gen=2 definition with the same name overrides the gen=1 version — but only when building for gen >= 2.

### Build Query

The key query selects the latest version of each definition available at the target generation:

```sql
SELECT id, name, kind, body, gen FROM (
    SELECT *, ROW_NUMBER() OVER (PARTITION BY name ORDER BY gen DESC) AS rn
    FROM def
    WHERE gen <= :target_gen AND kind = 'fn'
) WHERE rn = 1
```

For gen=1 builds, this returns exactly the current 583 definitions (all at gen=1). For gen=2 builds, gen=2 definitions override their gen=1 counterparts, and unchanged gen=1 definitions carry forward.

### Bootstrap Chain

```
glyph0 (Rust)  ──build --gen=1──►  glyph_gen1 (current capabilities)
glyph_gen1     ──build --gen=2──►  glyph_gen2 (understands structs)
glyph_gen2     ──build --gen=3──►  glyph_gen3 (closures, TCO, ...)
```

Each stage is compiled by the previous stage's binary. All stages read from the **same** `glyph.glyph` database. The Rust compiler only ever needs to understand gen=1.

### Default Behavior

- `glyph build db.glyph` — builds the latest generation present (`SELECT MAX(gen) FROM def`)
- `glyph build db.glyph --gen=1` — builds generation 1 specifically (what glyph0 uses)

### What Changes in Rust

Minimal, scoped changes — no parser/typechecker/MIR/codegen modifications:

1. **Schema** (`glyph-db`): Add `gen` column to `def`, update `v_dirty`/`v_context` views
2. **Queries** (`glyph-db`): All definition reads filter by `gen <= :target_gen`
3. **CLI** (`glyph-cli`): Add `--gen=N` flag, default to max gen present
4. **Hash/dep computation**: Generation-scoped (dep edges belong to the generation that created them)

Estimated: 50-100 lines of Rust across 2 crates.

### What Changes in Self-Hosted

Same scope:

1. `read_fn_defs` and `read_test_defs`: Accept gen parameter, use generation-aware query
2. `cmd_build` and `cmd_test`: Parse `--gen=N` flag
3. `init_schema`: Include `gen` column in table creation

Estimated: 5-8 modified definitions.

### SQL Diffing Between Generations

Because it's all SQL, generation management is transparent:

```sql
-- What's new or changed in gen 2?
SELECT name, kind FROM def WHERE gen = 2;

-- What does gen 2 inherit unchanged?
SELECT name, kind FROM def WHERE gen = 1
  AND name NOT IN (SELECT name FROM def WHERE gen = 2);

-- Flatten: promote gen 2 to become the new gen 1
UPDATE def SET gen = 1 WHERE gen = 2;
DELETE FROM def WHERE gen = 1
  AND name IN (SELECT name FROM def d2 WHERE d2.gen = 1 GROUP BY name HAVING COUNT(*) > 1 AND gen < MAX(gen));
```

## Implementation Roadmap

### Phase 1: Generational Infrastructure

**Goal**: `--gen` flag works, gen=1 builds produce identical output.

- Add `gen` column and update Rust queries
- Add `--gen` flag to both Rust and self-hosted CLIs
- Verify: `glyph0 build glyph.glyph --gen=1` produces the same binary as `glyph0 build glyph.glyph --full`

### Phase 2: Struct Codegen (Gen=2)

**Goal**: Self-hosted compiler emits C `typedef struct` from type definitions.

New gen=2 definitions (names are illustrative):

| Definition | Purpose |
|------------|---------|
| `read_type_defs` | Query `SELECT name, body FROM def WHERE kind='type' AND gen <= N` |
| `parse_type_def` | Parse record/enum type body into field list |
| `cg_struct_typedef` | Emit `typedef struct { ... } GlyphName;` |
| `cg_struct_alloc` | Emit `GlyphName* p = (GlyphName*)glyph_alloc(sizeof(GlyphName));` |
| `cg_struct_field_get` | Emit `p->field` instead of `((long long*)p)[N]` |
| `cg_struct_field_set` | Emit `p->field = val` instead of `((long long*)p)[N] = val` |
| `cg_all_typedefs` | Emit all struct typedefs before function code |

Override gen=1 definitions that currently use pointer arithmetic for records with struct-aware versions.

### Phase 3: Remove Field Offset Mitigation

**Goal**: Delete the 35+ definitions that exist solely to work around type erasure.

Gen=2 overrides with empty/removed definitions:

| Definition | Status |
|------------|--------|
| `build_type_reg` | Dead — struct types replace the registry |
| `coll_local_acc` | Dead — no pre-scan needed |
| `resolve_fld_off` | Dead — C compiler resolves offsets |
| `fix_all_field_offsets` | Dead — no post-processing needed |
| `find_best_type` / `find_matching_type` | Dead — no disambiguation needed |
| `alpha_rank` / `cg_store_ops_alpha` | Dead — fields stored by name, not offset |

This should remove ~35-40 definitions and simplify the compilation pipeline significantly.

### Phase 4: Struct-Aware Extern System

**Goal**: Extern declarations can reference named types.

Extended extern signature format:
```
-- Current:  I -> I -> I
-- Extended: Point -> I -> Point
```

The compiler resolves type names to their struct typedefs, generating wrappers that pass actual `struct` pointers instead of `long long` casts. This makes the extern system viable for real C library interop (ncurses, SDL, etc.) without hand-written wrapper files.

### Phase 5: Compiler Self-Improvement (Gen=2 or Gen=3)

**Goal**: The self-hosted compiler uses named structs internally.

Replace anonymous record patterns like:
```
-- Gen 1: anonymous record with collision-prone field names
mk_operand kind val str_val =
  {okind: kind, oval: val, ostr: str_val}
```

With struct-typed versions:
```
-- Gen 2+: named struct, no prefix needed
Operand = {kind: I, val: I, str: S}

mk_operand kind val str_val =
  Operand{kind: kind, val: val, str: str_val}
```

This eliminates the unique-prefix convention (`okind`, `sdest`, `tkind`) that exists solely to avoid field name collisions across anonymous record types.

## Dependencies Between Phases

```
Phase 1 (gen infrastructure) ── required by all others
    │
    ├── Phase 2 (struct codegen)
    │       │
    │       ├── Phase 3 (remove offset mitigation)
    │       │
    │       └── Phase 4 (struct-aware extern)
    │               │
    │               └── Phase 5 (compiler self-improvement)
    │
    └── [Future: closures, TCO, hash maps — independent gen=N features]
```

## Risk Analysis

### Phase 1 Risk: Low
The `gen` column is additive. All existing definitions get `gen=1`. The build query with `gen <= 1` returns the same result set. Nothing breaks.

### Phase 2 Risk: Medium
C struct codegen must handle: forward declarations (structs referencing each other), array fields (still `long long` at ABI), enum payloads (tag + variant-specific fields). The generated C must compile under both gcc and clang.

### Phase 3 Risk: Low (once Phase 2 works)
Removing dead code is safe. The gen=2 codepath doesn't call the old mitigation functions, so they can be left in gen=1 as historical artifacts or actively deleted.

### Phase 4 Risk: Medium
Extending the extern signature parser to handle type names requires the extern system to know about type definitions. This creates a new dependency: extern processing must happen after type definition loading.

### Phase 5 Risk: High (but optional)
Rewriting the compiler's internal data structures changes the code that compiles itself. This is a "rebuilding the ship while sailing" problem. Mitigated by the generational system — gen=1 always exists as a fallback.

## Recovery Strategy

At any point, the system can be rebuilt from scratch:

```bash
cargo build --release              # rebuild glyph0 from Rust source
./target/release/glyph build glyph.glyph --gen=1   # rebuild gen1 from database
# gen1 binary can then rebuild any higher generation
```

The Rust source and gen=1 definitions form an indestructible bootstrap foundation. Higher generations are always recoverable from lower ones.

## Token Economics

| Approach | Cost to add structs | Cost for all future features |
|----------|--------------------|-----------------------------|
| Dual implementation (current) | ~5k tokens Rust + ~3k tokens Glyph | ~5k + ~3k per feature, forever |
| Generational (proposed) | ~2k tokens Rust (gen infra, one-time) + ~3k tokens Glyph | ~3k tokens Glyph only, forever |

The generational approach pays for itself immediately at Phase 2 and saves ~5k tokens of Rust work per feature thereafter.
