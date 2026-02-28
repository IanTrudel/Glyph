# Documents — Table of Contents

## Language

| Document | Description |
|----------|-------------|
| [glyph-spec.md](glyph-spec.md) | Formal specification of Glyph's token-minimal syntax and SQLite-based program format |
| [glyph-programming-manual.md](glyph-programming-manual.md) | User-facing guide covering syntax, quick start, and programming idioms |
| [glyph-type-system.md](glyph-type-system.md) | Specification of the Hindley-Milner type system with row polymorphism |
| [unification.md](unification.md) | Technical specification of standard HM unification with union-find |
| [result-type-design.md](result-type-design.md) | Deferred design for Result type (`!T`) and `?` propagation operator |
| [new-constructs.md](new-constructs.md) | Prioritized proposals for new language features (match guards, string slicing, typed arrays) |

## Compiler Internals

| Document | Description |
|----------|-------------|
| [glyph-compiler-reference.md](glyph-compiler-reference.md) | Internal reference for the ~10,000-line Rust compiler across 6 crates |
| [mir.md](mir.md) | Technical specification of the flat CFG intermediate representation between AST and codegen |
| [bootstrapping.md](bootstrapping.md) | Description of the 3-stage bootstrap chain (Rust -> Cranelift -> C-codegen) |
| [structs-and-generational-bootstrap.md](structs-and-generational-bootstrap.md) | Design notes for named struct types and generational codegen to eliminate type erasure |
| [context-aware-inference.md](context-aware-inference.md) | Proposal to wire type inference results into MIR lowering for better codegen decisions |
| [error-messages-spec.md](error-messages-spec.md) | Specification for early error detection and reporting through CLI and MCP |
| [glyph-cli-spec.md](glyph-cli-spec.md) | Design specification for the verb-based CLI treating `.glyph` databases as living programs |
| [extern-system-gaps.md](extern-system-gaps.md) | Identified issues with the `extern_` table preventing calls to common libc functions |

## Performance & Optimization

| Document | Description |
|----------|-------------|
| [optimization.md](optimization.md) | Benchmark results post-TCO with performance gap analysis vs C |
| [inline-intrinsics-release-mode-plan.md](inline-intrinsics-release-mode-plan.md) | Plan for inlining runtime intrinsics and adding `--release` mode |

## Assessments & Post-Mortems

| Document | Description |
|----------|-------------|
| [assessment.md](assessment.md) | Comprehensive evaluation of Glyph's core thesis with evidence from working compiler and programs |
| [c-runtime-assessment.md](c-runtime-assessment.md) | Safety analysis of the uniform `long long` ABI, bounds checking, and debug features |
| [schema-unused-assessment.md](schema-unused-assessment.md) | Audit of the ~40% of schema surface area that is dead or unpopulated |
| [disconnected-features.md](disconnected-features.md) | Catalog of partially-implemented features in the self-hosted compiler |
| [glyph-interpreter-assessment.md](glyph-interpreter-assessment.md) | Evaluation of an MIR-based interpreter as a faster feedback alternative |
| [llm-programming-experience.md](llm-programming-experience.md) | Reflection on building the self-hosted compiler from an LLM's perspective |

## Example Program Specs & Assessments

| Document | Description |
|----------|-------------|
| [glint-spec.md](glint-spec.md) | Spec for `glint` — command-line `.glyph` database analyzer |
| [glint-assessment.md](glint-assessment.md) | Post-mortem: fresh LLM built 26-def tool with 58% first-attempt accuracy |
| [gled-spec.md](gled-spec.md) | Spec for `gled` — ncurses terminal text editor |
| [gled-assessment.md](gled-assessment.md) | Post-mortem: 34-def editor proving Glyph handles sustained interactivity and real FFI |
| [gstats-spec.md](gstats-spec.md) | Spec for `gstats` — statistical analyzer exercising gen=2 struct codegen |
| [gstat-assessment.md](gstat-assessment.md) | Post-mortem: first program using gen=2 named record types |
| [asteroids-feasibility.md](asteroids-feasibility.md) | Feasibility analysis for Vulkan-based Asteroids clone (blocked by missing language features) |

## Speculative / Future

| Document | Description |
|----------|-------------|
| [context-stashing.md](context-stashing.md) | Design for ephemeral scratch storage in MCP to reduce context bloat |
| [bugs.md](bugs.md) | Bug tracker for known issues |
