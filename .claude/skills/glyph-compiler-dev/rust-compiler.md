# Rust Compiler Crate Guide

6 crates, ~10k LOC, 73 tests. **Maintenance mode** — glyph0 exists solely to bootstrap glyph.glyph. New features go in the self-hosted compiler, not here.

Dependency chain:
```
glyph-cli → glyph-codegen → glyph-mir → glyph-typeck → glyph-parse → glyph-db
```

## glyph-db (Database Layer)

**Files:** `connection.rs` (229), `queries.rs` (338), `model.rs` (170), `schema.rs` (150), `functions.rs` (48)

**Entry:** `Database::open(path)` → register custom functions → `PRAGMA foreign_keys=ON` → create TEMP triggers → `migrate_gen()`

**Model types:**
- `DefRow { id, name, kind: DefKind, sig?, body, hash, tokens, compiled, generation }`
- `ExternRow { id, name, symbol, lib?, sig, conv }`
- `NewDef { name, kind, sig?, body, generation }` — insert params

**Key queries (on `Database`):**

| Method | Purpose |
|--------|---------|
| `insert_def(NewDef) → i64` | Insert with auto hash/tokens |
| `resolve_name(name, kind) → DefRow` | Lookup by name+kind |
| `dirty_defs_gen(gen) → Vec<DefRow>` | Dirty defs within generation |
| `effective_defs(gen) → Vec<DefRow>` | Highest gen ≤ target per (name,kind) |
| `all_externs() → Vec<ExternRow>` | All FFI declarations |
| `insert_dep(from, to, edge)` | Record dependency edge |
| `mark_compiled(id)` | Set compiled=1 |

**Custom SQLite functions:** `glyph_hash(kind, sig?, body) → BLOB` (BLAKE3), `glyph_tokens(body) → INTEGER` (BPE count)

**TEMP triggers** (created per-connection, never persisted):
- `trg_def_dirty`: On UPDATE body/sig/kind → recompute hash+tokens, set compiled=0
- `trg_dep_dirty`: On SET compiled=0 → cascade to dependents via dep table

**When to modify:** Schema changes, new query methods, migration logic, custom SQL functions.

## glyph-parse (Lexer + Parser + AST)

**Files:** `parser.rs` (1760), `lexer.rs` (827), `ast.rs` (301), `token.rs` (83), `span.rs` (105)

### Lexer
- **Entry:** `Lexer::new(source).tokenize() → Vec<Token>`
- **State:** `source: Vec<char>, pos, tokens, indent_stack, bracket_depth`
- **Indentation:** `handle_line_start()` emits `Indent`/`Dedent`; suppressed when `bracket_depth > 0`
- **String interpolation:** `"text {expr}"` → `StrInterpStart, Str("text "), <expr tokens>, StrInterpEnd`
- **Raw strings:** `r"..."` — no escapes, no interpolation

### Parser
- **Entry:** `Parser::new(tokens).parse_def(name, kind) → Result<Def>`
- **Dispatches to:** `parse_fn_def`, `parse_type_def`, `parse_trait_def`, `parse_test_def`, etc.
- **Precedence (low → high):** pipe (`|>`) → compose (`>>`) → or → and → comparison → add → mul → unary → postfix → atom
- **Critical:** `parse_body()` calls `skip_newlines()` before checking for `Indent`

### AST
- **ExprKind:** 25+ variants — `IntLit, StrLit, BoolLit, Ident, Binary, Unary, Call, FieldAccess, Index, Lambda, Match, Block, Array, Record, Pipe, Compose, Propagate, Unwrap, StrInterp, FieldAccessor, ...`
- **PatternKind:** `Wildcard, Ident, IntLit, BoolLit, StrLit, Constructor, Record, Tuple, OrPattern(Vec<Pattern>)`
- **MatchArm:** `{ pattern, body, guard: Option<Expr> }` — match guards: `pat ? guard_expr -> body`
- **StmtKind:** includes `LetDestructure(Vec<String>, Expr)` — `{x, y} = expr` syntax
- **TypeExprKind:** `Named, App, Fn, Record, Ref, Ptr, Opt, Res, Arr, Map, Tuple`
- **TypeBody:** `Record(fields)`, `Enum(variants)`, `Alias(type_expr)`

**When to modify:** New syntax, new expression/pattern kinds, new token types, parser fixes.

## glyph-typeck (Type Checking)

**Files:** `infer.rs` (815), `unify.rs` (366), `resolve.rs` (288), `types.rs` (224)

### Type Enum
20 variants: `Int, Int32, UInt, Float, Float32, Str, Bool, Void, Never, Fn, Tuple, Array, Map, Opt, Res, Ref, Ptr, Named, Record(RowType), Var, ForAll, Error`

`RowType { fields: BTreeMap<String, Type>, rest: Option<TypeVarId> }` — `rest=None` is closed, `rest=Some(rv)` is open (row polymorphism)

### Substitution (unify.rs)
Union-find with path compression + occurs check. Key methods: `fresh_var()`, `find(v)`, `probe(v)`, `walk(ty)`, `resolve(ty)`, `unify(a, b)`, `bind(var, ty)`

### Row Unification
1. Unify common fields by name
2. Handle extensions: both open → fresh row var; one open → bind to missing fields; both closed → same field set

### InferEngine (infer.rs)
`InferEngine { subst, env, errors }` — errors are non-fatal (pushed, inference continues)

**Flow:** `infer_fn_def(fndef) → Type` — pre-register for recursion, allocate params, infer body, build curried fn type

### Resolver (resolve.rs)
Scope stack + DB queries for cross-definition references. Records dependency edges → `write_deps()`.

**When to modify:** New type constructors, unification rules, inference cases, built-in types.

## glyph-mir (MIR Representation + Lowering)

**Files:** `lower.rs` (1466), `ir.rs` (251)

### MIR Structures (ir.rs)
```
MirFunction { name, params, return_ty, locals, blocks, entry }
BasicBlock { id, stmts: Vec<Statement>, terminator }
Statement { dest: LocalId, rvalue: Rvalue }
```

**Rvalue:** `Use, BinOp, UnOp, Call, Aggregate, Field, Index, Cast, StrInterp, MakeClosure, Ref, Deref`
**AggregateKind:** `Tuple, Array, Record(Vec<String>), Variant(type, name, disc)`
**Operand:** `Local, ConstInt, ConstFloat, ConstBool, ConstStr, ConstUnit, FuncRef, ExternRef`
**Terminator:** `Goto, Branch, Switch, Return, Unreachable`
**MirType:** `Int, Str, Bool, Void, Ptr, Array, Record(BTreeMap), Enum, Fn, Named, ...`

### Lowering (lower.rs)
**Entry:** `lower_fn(name, fndef, fn_ty) → MirFunction`

**Key patterns:**
- **Records:** Fields sorted alphabetically → `Aggregate(Record(names), ops)`
- **Enums:** `Aggregate(Variant(type, name, disc), fields)`. Match → extract tag at offset 0.
- **Strings:** `+` → `str_concat`, interpolation → `sb_new → sb_append × N → sb_build`
- **Lambdas/Closures:** Collect free vars → lift to top-level → `MakeClosure(lifted_name, captures)`. Calling convention: closure ptr as hidden first arg.
- **TCO:** Self-recursive tail calls → copy args, `Goto(loop_header)`
- **Match:** Chain of tests → branch to match/next for each arm. Guards: `lower_arm_body_guarded` helper.
- **Or-patterns:** Desugared to chained Branch tests
- **Let destructuring:** `LetDestructure` → temp binding + field accesses

**When to modify:** New expression lowering, new Rvalue kinds, pattern compilation, TCO changes.

## glyph-codegen (Cranelift Backend + Runtime)

**Files:** `cranelift.rs` (908), `runtime.rs` (637), `layout.rs` (127), `linker.rs` (74)

### Type Layout

| MirType | Cranelift | Heap Layout |
|---------|-----------|-------------|
| Bool | I8 (coerced ↔ I64) | — |
| Int/UInt | I64 | — |
| Str | I64 (ptr) | `{*char, len}` 16B |
| Array | I64 (ptr) | `{*data, len, cap}` 24B |
| Record | I64 (ptr) | 8B/field, alphabetical |
| Enum | I64 (ptr) | `{tag, payload...}` |
| Closure | I64 (ptr) | `{fn_ptr, caps...}` |

### CodegenContext (cranelift.rs)
```rust
CodegenContext {
    module: ObjectModule,
    func_ids: HashMap<String, FuncId>,
    complete_record_types: Vec<BTreeMap<String, MirType>>,  // global registry
    local_field_accesses: HashMap<u32, BTreeSet<String>>,   // per-function
}
```

**Record type disambiguation (3-step):**
1. `register_record_types()` — pre-scan all MIR, collect complete record types
2. `collect_field_accesses()` — per-function, map local → accessed field names
3. `resolve_field_offset()` — union partial type + accessed fields → smallest matching complete type

### C Runtime (runtime.rs)
Embedded as `RUNTIME_C` string constant. Categories: Memory, Print, String, StringBuilder, Array, I/O, Control, Signal. Separate `RUNTIME_SQLITE_C` conditionally linked.

**Runtime name resolution:** Runtime functions are registered with the `glyph_` prefix (e.g., `glyph_int_to_str`). They are also resolvable without the prefix (e.g., `int_to_str`) so that user code calling runtime functions by their short names works in Cranelift-compiled binaries.

### Linker (linker.rs)
`link_with_extras(object_bytes, exe_path, extern_libs, runtime_c, extra_sources)` → writes temp files → `cc program.o runtime.c -o OUTPUT -lLIBS`

**When to modify:** New Cranelift codegen patterns, runtime functions, type layouts, linker flags.

## glyph-cli (Build Orchestration)

**Files:** `build.rs` (677), `main.rs` (121)

**Commands:** `init`, `build [--full] [--emit-mir] [--gen N]`, `run [--gen N]`, `check [--gen N]`, `test [--gen N]`

### Build Pipeline (cmd_build, 5 phases)

1. **Load:** `dirty_defs_gen(gen)` or `effective_defs(gen)` if `--full`. Separate fns from types.
2. **Type-check:** Extract enum variants → register in InferEngine → pre-register signatures → infer bodies → report errors with `format_diagnostic`
3. **MIR Lower:** Per function: `MirLower::new()`, register known functions + enums, `lower_fn()`, collect lifted lambdas
4. **Codegen:** `CodegenContext::new()` → declare functions + externs + runtime → `register_record_types()` → `compile_function()` each → `finish()`
5. **Link:** Collect `-l` flags → conditionally include sqlite runtime → `link_with_extras()` → `mark_compiled()`

### Key Build Helpers

| Function | Purpose |
|----------|---------|
| `extract_enum_variants(types)` | Parse type defs → variant lists for MIR |
| `ast_type_to_mir(type_expr)` | AST TypeExpr → MirType |
| `build_extern_sig(codegen, sig_str)` | Parse `I -> S -> V` → Cranelift Signature |
| `declare_runtime(codegen)` | Register 27+ runtime fn signatures |
| `add_runtime_known_functions(map)` | Populate known_functions for MIR |

**When to modify:** Build pipeline changes, new command flags, error reporting.

## Test Distribution

| Crate | Tests | Focus |
|-------|-------|-------|
| glyph-parse | 38 | Lexer (16): tokens, indent, interpolation. Parser (18): all expr types. Span (4) |
| glyph-typeck | 17 | Infer (12): literals, records, lambdas, pipe. Unify (5): rows, occurs check |
| glyph-db | 6 | Insert, dirty cascade, name resolve, externs, tags |
| glyph-mir | 4 | Lowering, match, constants, serde roundtrip |
| glyph-codegen | 3 | Simple codegen, add, branch |
| glyph-cli | 0 | Integration tested via `cargo run` |
| **Total** | **73** | |
