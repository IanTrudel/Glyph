# Documents — Table of Contents

## Living Reference

| Document | Description |
|----------|-------------|
| [glyph-spec.md](glyph-spec.md) | Original formal spec (v0.1, with historical notice) — language model, schema, syntax, type system |
| [glyph-self-hosted-programming-manual.md](glyph-self-hosted-programming-manual.md) | Comprehensive LLM programming manual — syntax, idioms, runtime, full workflow guide |
| [glyph-type-system.md](glyph-type-system.md) | Specification of the Hindley-Milner type system with row polymorphism |
| [glyph-cli-spec.md](glyph-cli-spec.md) | Original CLI design spec (with historical notice) — command reference, MCP note |

## Compiler Internals

| Document | Description |
|----------|-------------|
| [glyph-compiler-reference.md](glyph-compiler-reference.md) | Internal reference for Rust compiler (6 crates, 73 tests) and self-hosted compiler (~1,363 defs) |
| [bootstrapping.md](bootstrapping.md) | 4-stage bootstrap chain (glyph0 → glyph1 → glyph2 → glyph), pitfalls, binary sizes |
| [mir.md](mir.md) | Flat CFG intermediate representation between AST and codegen |
| [migrations.md](migrations.md) | Design for the schema migration system (`glyph migrate` command) |

## Testing

| Document | Description |
|----------|-------------|
| [glyph-property-testing.md](glyph-property-testing.md) | Property-based testing spec — `kind='prop'`, generators, shrinking, seed system, `prop_failure` table, MCP interface, compiler self-test suite |

## Tooling & Distribution

| Document | Description |
|----------|-------------|
| [distribution.md](distribution.md) | Distribution strategy: GitHub Releases + install script + `glyph update` command |
| [library-linking.md](library-linking.md) | Current linking behaviour, problems, and improvement options (provenance tags → lib_dep table → separate compilation) |

## Archives

Historical, superseded, and completed design documents have been moved to `../Archives/documents/`.
Notable archives:
- `glyph-programming-manual.md` — superseded by glyph-self-hosted-programming-manual.md
- `structs-and-generational-bootstrap.md` — completed; gen=2 struct codegen is live
- `error-messages-spec.md` — substantially completed; parse validation and check_def are live
- `result-type-design.md`, `new-constructs.md` — completed language features
- `assessment.md`, `llm-programming-experience.md` — one-time reflections
- `optimization.md`, `inline-intrinsics-release-mode-plan.md` — historical benchmarks
- Example specs and assessments (glint, gled, gstats, asteroids)
