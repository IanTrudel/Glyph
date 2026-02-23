# Glyph Compiler: Comprehensive Reference

*Internal reference for working on the Glyph Rust compiler. ~10,000 lines of Rust across 6 crates, 68 tests.*

## Architecture Overview

```
.glyph DB ──SELECT dirty defs──► Parser ──► Resolver ──► Type Infer ──► MIR Lower ──► Cranelift ──► Linker ──► EXE
              (glyph-db)        (glyph-parse)      (glyph-typeck)     (glyph-mir)  (glyph-codegen)
```

**Dependency chain (strict, no cycles):**
```
glyph-cli → glyph-codegen → glyph-mir → glyph-typeck → glyph-parse → glyph-db
```

**Key design:** Programs are SQLite databases. Each definition is a row in the `def` table. SQL queries replace imports/modules. Incremental compilation via BLAKE3 content hashing + dependency graph cascade.

## Build & Test

```bash
cargo build                  # Build compiler
cargo test                   # 68 tests (38 parse, 17 typeck, 6 db, 4 mir, 3 codegen)
cargo run -- build app.glyph # Compile a program
cargo run -- run app.glyph   # Build + execute
cargo run -- check app.glyph # Type-check only
cargo run -- test app.glyph  # Run test definitions
```

**Bootstrap (self-hosted compiler):**
```bash
ninja                        # glyph0 (Rust) → glyph (Cranelift) → glyph_new (C-codegen, must match)
ninja test                   # Self-hosted regression tests
```

---

## Crate 1: glyph-db (Database Layer)

**Files:** `connection.rs` (229), `queries.rs` (338), `model.rs` (170), `schema.rs` (150), `functions.rs` (48), `error.rs` (29)

### Schema (schema.rs)

**def** — All definitions:
```
id INTEGER PK, name TEXT, kind TEXT CHECK(fn|type|trait|impl|const|fsm|srv|macro|test),
sig TEXT?, body TEXT, hash BLOB, tokens INTEGER, compiled INTEGER DEFAULT 0,
gen INTEGER DEFAULT 1, created TEXT, modified TEXT
UNIQUE(name, kind, gen)
```

**dep** — Dependency graph:
```
from_id INTEGER FK, to_id INTEGER FK, edge TEXT CHECK(calls|uses_type|implements|field_of|variant_of)
PK(from_id, to_id, edge)
```

**extern_** — C FFI declarations:
```
id INTEGER PK, name TEXT UNIQUE, symbol TEXT, lib TEXT?, sig TEXT, conv TEXT DEFAULT 'C'
```

**compiled** — MIR cache: `def_id PK, ir BLOB, target TEXT, hash BLOB`
**tag** — Metadata: `def_id FK, key TEXT, val TEXT?, PK(def_id, key)`
**module/module_member** — Logical grouping (unused currently)

**Views:** `v_dirty` (dirty + transitive dependents via recursive CTE), `v_context` (defs sorted by dep depth), `v_callgraph` (caller/callee triples)

**TEMP triggers** (never persisted, created on `Database::open()`):
- `trg_def_dirty`: On UPDATE body/sig/kind → recompute hash+tokens, set compiled=0
- `trg_dep_dirty`: On SET compiled=0 → cascade to all dependents via dep table

### Custom SQLite Functions (functions.rs)

- `glyph_hash(kind, sig?, body) → BLOB` — 32-byte BLAKE3 hash
- `glyph_tokens(body) → INTEGER` — cl100k_base BPE token count (tiktoken)

### Model Types (model.rs)

```rust
DefRow { id, name, kind: DefKind, sig?, body, hash, tokens, compiled: bool, generation }
DefKind { Fn, Type, Trait, Impl, Const, Fsm, Srv, Macro, Test }
DepRow { from_id, to_id, edge: DepEdge }
DepEdge { Calls, UsesType, Implements, FieldOf, VariantOf }
ExternRow { id, name, symbol, lib?, sig, conv: CallingConv }
NewDef { name, kind, sig?, body, generation }  // Insert params
```

### Key Query Methods (queries.rs on Database)

| Method | Purpose |
|--------|---------|
| `insert_def(NewDef) → i64` | Insert with auto hash/tokens |
| `get_def(id) → DefRow` | Fetch by ID |
| `resolve_name(name, kind) → DefRow` | Lookup by name+kind |
| `dirty_defs() → Vec<DefRow>` | All dirty + transitive deps |
| `effective_defs(gen) → Vec<DefRow>` | Highest gen ≤ target per (name,kind) |
| `dirty_defs_gen(gen) → Vec<DefRow>` | Dirty within generation |
| `defs_by_kind(kind) → Vec<DefRow>` | All defs of a kind |
| `mark_compiled(id)` | Set compiled=1 |
| `insert_dep(from, to, edge)` | Record dependency |
| `clear_deps_from(id)` | Clear outgoing deps |
| `all_externs() → Vec<ExternRow>` | All FFI declarations |
| `cache_compiled(def_id, ir, target, hash)` | Cache MIR |

### Connection Setup (connection.rs)

`Database::open(path)` → register custom functions → `PRAGMA foreign_keys=ON` → create TEMP triggers → `migrate_gen()` (adds gen column if missing)

`Database::create(path)` → execute full SCHEMA_SQL + TRIGGER_SQL

---

## Crate 2: glyph-parse (Lexer + Parser + AST)

**Files:** `parser.rs` (1693), `lexer.rs` (755), `ast.rs` (301), `token.rs` (83), `span.rs` (105), `error.rs` (29)

### Lexer (lexer.rs)

**State:** `source: Vec<char>, pos, tokens: Vec<Token>, indent_stack: Vec<usize>, bracket_depth`

**Indentation handling:**
- `handle_line_start()` compares indent with stack top → emits `Indent`/`Dedent` tokens
- `bracket_depth > 0` suppresses all indentation tokens (multi-line exprs in parens)
- Tab = 4 spaces

**String interpolation** (lexer-level):
- `"text {expr}"` → `StrInterpStart, Str("text "), <expr tokens>, StrInterpEnd`
- Recursive tokenization inside `{}`

**Raw strings:** `r"..."` — no escape processing, no interpolation

**Entry:** `Lexer::new(source).tokenize() → Vec<Token>`

### Token Types (token.rs)

**Literals:** `Int(i64)`, `Float(f64)`, `Str(String)`, `ByteStr(Vec<u8>)`, `StrInterpStart/End`
**Identifiers:** `Ident(String)` (lowercase), `TypeIdent(String)` (uppercase)
**Keywords (8):** `Match`, `Trait`, `Impl`, `Const`, `Extern`, `Fsm`, `Srv`, `Test`, `As`
**Operators (22):** `Plus Minus Star Slash Percent Eq EqEq BangEq Lt Gt LtEq GtEq And Or Bang Pipe PipeGt GtGt Question Arrow Backslash Ampersand At ColonEq DotDot`
**Delimiters:** `LParen RParen LBracket RBracket LBrace RBrace`
**Layout (synthetic):** `Indent Dedent Newline`
**Special:** `Eof Error(String)`

### AST (ast.rs)

**Top-level:** `Def { id?, name, kind: DefKind, span }`

**DefKind variants:** `Fn(FnDef)`, `Type(TypeDef)`, `Trait(TraitDef)`, `Impl(ImplDef)`, `Const(ConstDef)`, `Fsm(FsmDef)`, `Srv(SrvDef)`, `Test(TestDef)`

**FnDef:** `params: Vec<Param>, ret_ty?, body: Body`
**Body:** `Expr(Expr)` | `Block(Vec<Stmt>)`
**Stmt:** `Expr(Expr)` | `Let(String, Expr)` | `Assign(Expr, Expr)`

**ExprKind:**
```
IntLit, FloatLit, StrLit, StrInterp(Vec<StringPart>), ByteStrLit, BoolLit,
Ident, TypeIdent,
Binary(BinOp, Box<Expr>, Box<Expr>), Unary(UnaryOp, Box<Expr>),
Call(Box<Expr>, Vec<Expr>), FieldAccess(Box<Expr>, String), Index(Box<Expr>, Box<Expr>),
FieldAccessor(String),   // .name shorthand → lambda
Pipe(lhs, rhs), Compose(lhs, rhs), Propagate(expr), Unwrap(expr),
Lambda(Vec<Param>, Box<Expr>), Match(Box<Expr>, Vec<MatchArm>), Block(Vec<Stmt>),
Array(Vec<Expr>), ArrayRange(start, end?), Record(Vec<FieldInit>), Tuple(Vec<Expr>)
```

**PatternKind:** `Wildcard, Ident(String), IntLit(i64), BoolLit(bool), StrLit(String), Constructor(String, Vec<Pattern>), Record(Vec<(String, Option<Pattern>)>), Tuple(Vec<Pattern>)`

**TypeExprKind:** `Named(String), App(String, Vec<TypeExpr>), Fn(Box, Box), Tuple(Vec), Record(Vec<(String, TypeExpr)>, bool), Ref(Box), Ptr(Box), Opt(Box), Res(Box), Arr(Box), Map(Box, Box)`

**TypeBody:** `Record(Vec<Field>)` | `Enum(Vec<Variant>)` | `Alias(TypeExpr)`

### Parser (parser.rs)

**Entry:** `Parser::new(tokens).parse_def(name, kind) → Result<Def>`
- Dispatches to `parse_fn_def`, `parse_type_def`, `parse_trait_def`, `parse_impl_def`, `parse_const_def`, `parse_fsm_def`, `parse_srv_def`, `parse_test_def`

**Expression precedence (lowest → highest):**
pipe (`|>`) → compose (`>>`) → or (`||`) → and (`&&`) → comparison (`== != < > <= >=`) → add (`+ -`) → mul (`* / %`) → unary (`- ! & *`) → postfix (`.field (args) [idx] ? !`) → atom

**Critical patterns:**
- `parse_body()` calls `skip_newlines()` before checking for `Indent` (block detection)
- `parse_stmt()`: `Ident = expr` → Let, `expr := expr` → Assign, else → Expr
- `parse_match_expr()`: Expect `match`, parse scrutinee, `Indent`, loop arms (pattern `->` body), `Dedent`
- Lambda: `\p1 p2 -> body`

### Span + Diagnostics (span.rs)

```rust
Span { start: u32, end: u32 }  // byte offsets
line_col(source, offset) → (line, col)  // 1-based
format_diagnostic(def_name, source, span, level, message) → String
  // "error in 'foo' at 2:10: message\n2 | source line\n  |          ^"
```

---

## Crate 3: glyph-typeck (Type Checking)

**Files:** `infer.rs` (791), `unify.rs` (366), `resolve.rs` (288), `types.rs` (224), `env.rs` (~100), `builtins.rs` (~100), `error.rs` (~30)

### Type Enum (types.rs)

```rust
Type {
    Int, Int32, UInt, Float, Float32, Str, Bool, Void, Never,       // primitives
    Fn(Box<Type>, Box<Type>),                    // curried: a -> b
    Tuple(Vec<Type>), Array(Box<Type>),          // collections
    Map(Box<Type>, Box<Type>),                   // {K:V}
    Opt(Box<Type>), Res(Box<Type>),              // ?T, !T
    Ref(Box<Type>), Ptr(Box<Type>),              // &T, *T
    Named(String, Vec<Type>),                    // user-defined
    Record(RowType),                             // {field:T ..}
    Var(TypeVarId),                              // inference variable
    ForAll(Vec<TypeVarId>, Box<Type>),           // ∀t. T
    Error,                                       // sentinel
}

RowType { fields: BTreeMap<String, Type>, rest: Option<TypeVarId> }
  // rest=None → closed record, rest=Some(rv) → open (row polymorphism)
```

### Substitution (unify.rs)

Union-find with path compression + occurs check.

```rust
Substitution { parent: Vec<TypeVarId>, types: Vec<Option<Type>>, next_var: TypeVarId }
```

**Key methods:** `fresh_var()`, `find(v)` (path-compressed), `probe(v)` → binding, `walk(ty)` → resolve one level, `resolve(ty)` → fully resolve, `unify(a, b)`, `bind(var, ty)` (with occurs check)

### Row Unification (unify.rs: unify_rows)

1. Unify common fields by name
2. Check for missing fields if row is closed
3. Handle row extensions:
   - Both open → create fresh row var for overlap
   - One open, one closed → bind open's rest to missing fields
   - Both closed → must have same field set

### InferEngine (infer.rs)

```rust
InferEngine { subst: Substitution, env: TypeEnv, errors: Vec<TypeError> }
```

**Flow:** `infer_fn_def(fndef) → Type`
1. Pre-register function for recursion
2. Allocate params (fresh vars if untyped)
3. Infer body, check against return type
4. Build curried fn type

**Key methods:** `infer_expr(expr) → Type`, `infer_call(fn_ty, args)`, `infer_stmt(stmt)`, `instantiate(ty)` (ForAll → fresh vars), `generalize(ty)` (free vars → ForAll)

**Errors:** Non-fatal — pushed to `errors`, inference continues.

### Resolver (resolve.rs)

```rust
Resolver { db: &Database, current_def_id, deps: Vec<(i64, DepEdge)>, locals: Vec<Vec<String>> }
```

- Scope stack for local name tracking
- Queries DB for cross-definition references
- Records dependency edges as side effect → flushed via `write_deps()`
- Builtins (I, S, B, etc.) skip DB lookup

### TypeError (error.rs)

`Mismatch { expected, found, span }`, `UnresolvedName { name, span }`, `OccursCheck(...)`, `MissingField { field, span }`, `CannotInfer { name, span }`, `Custom { message, span }`

---

## Crate 4: glyph-mir (MIR Representation + Lowering)

**Files:** `lower.rs` (1306), `ir.rs` (251), `mono.rs` (34), `closure.rs` (8), `match_compile.rs` (5)

### MIR Data Structures (ir.rs)

```rust
MirFunction { name, params: Vec<LocalId>, return_ty: MirType, locals: Vec<MirLocal>,
              blocks: Vec<BasicBlock>, entry: BlockId }
BasicBlock { id: BlockId, stmts: Vec<Statement>, terminator: Terminator }
Statement { dest: LocalId, rvalue: Rvalue }
MirLocal { id: LocalId, ty: MirType, name: Option<String> }
```

**Rvalue** (what an instruction produces):
```
Use(Operand)                            // copy
BinOp(BinOp, Operand, Operand)         // +, -, *, /, %, ==, !=, <, >, <=, >=, &&, ||
UnOp(UnOp, Operand)                    // -, !
Call(Operand, Vec<Operand>)            // function call
Aggregate(AggregateKind, Vec<Operand>) // construct composite
Field(Operand, u32, String)            // record.field (index + name)
Index(Operand, Operand)                // array[i]
Cast(Operand, MirType)                 // type conversion
StrInterp(Vec<Operand>)               // string interpolation
MakeClosure(String, Vec<Operand>)      // {fn_ptr, captures...}
Ref(LocalId), Deref(Operand)           // &x, *x
```

**AggregateKind:** `Tuple`, `Array`, `Record(Vec<String>)`, `Variant(type_name, variant_name, discriminant)`

**Operand:** `Local(LocalId)`, `ConstInt(i64)`, `ConstFloat(f64)`, `ConstBool(bool)`, `ConstStr(String)`, `ConstUnit`, `FuncRef(String)`, `ExternRef(String)`

**Terminator:** `Goto(BlockId)`, `Branch(Operand, true_bb, false_bb)`, `Switch(Operand, Vec<(i64, BlockId)>, default_bb)`, `Return(Operand)`, `Unreachable`

**MirType:**
```
Int, Int32, UInt, Float, Float32, Str, Bool, Void, Never,
Ptr(Box), Ref(Box), Array(Box), Tuple(Vec), Record(BTreeMap<String,MirType>),
Enum(name, Vec<(variant_name, Vec<MirType>)>), Fn(Box, Box), Named(String), ClosureEnv(Vec)
```

### Lowering (lower.rs)

**MirLower state:**
```rust
locals, blocks, current_block, next_local, next_block,
known_functions: HashMap<String, MirType>,       // cross-function types
enum_variants: HashMap<String, (type_name, disc, field_types)>,
lifted_fns: Vec<MirFunction>,                    // lambda lifts
lambda_counter, current_fn_name,
tail_call_params: Vec<LocalId>, tail_call_entry: BlockId
```

**Entry:** `lower_fn(name, fndef, fn_ty) → MirFunction`
1. Allocate params
2. Create entry block → jump to loop header (for TCO)
3. Lower body in tail position
4. Return terminator

**Key lowering patterns:**

- **Records:** Fields sorted alphabetically → `Aggregate(Record(names), ops)`. Field access → `Field(op, index, name)` where index is position in sorted BTreeMap.
- **Enums:** Variant constructor → `Aggregate(Variant(type, name, disc), fields)`. Match → extract discriminant at offset 0, payload at offset 1+.
- **Strings:** `+` on strings → `Call(ExternRef("glyph_str_concat"), ...)`. Interpolation → `sb_new → sb_append × N → sb_build`.
- **Lambdas:** Collect free vars → lift to top-level fn with hidden env param → emit `MakeClosure(lifted_name, captures)`.
- **TCO:** Self-recursive tail calls → copy args to params, `Goto(loop_header)`.
- **Match:** Chain-of-tests. Each arm: compare → branch to match/next. Enum patterns: extract tag, compare, extract payload fields.
- **? operator:** Extract enum tag, if Ok extract payload, else return error variant.
- **! operator:** Same but panic on failure variant.

### Serde Cache

All MIR types derive `Serialize`/`Deserialize` (bincode). Enables incremental compilation cache (future optimization).

---

## Crate 5: glyph-codegen (Cranelift Backend + Runtime)

**Files:** `cranelift.rs` (893), `runtime.rs` (637), `layout.rs` (127), `abi.rs` (41), `linker.rs` (74)

### Type Layout (layout.rs)

| MirType | Cranelift | Size |
|---------|-----------|------|
| Bool | I8 | 1B (coerced to/from I64 at assignment) |
| Int/UInt | I64 | 8B |
| Float | F64 | 8B |
| Str | I64 (ptr) | 16B heap: {*char, len} |
| Array | I64 (ptr) | 24B heap: {*data, len, cap} |
| Record | I64 (ptr) | 8B/field heap, alphabetical |
| Enum | I64 (ptr) | 8+max_payload heap: {tag, payload...} |
| Closure | I64 (ptr) | 8*(1+captures) heap: {fn_ptr, caps...} |

### CodegenContext (cranelift.rs)

```rust
CodegenContext {
    module: ObjectModule,
    func_ids: HashMap<String, FuncId>,
    string_constants: HashMap<String, DataId>,
    complete_record_types: Vec<BTreeMap<String, MirType>>,  // global registry
    local_field_accesses: HashMap<u32, BTreeSet<String>>,   // per-function
}
```

**Record type disambiguation (row polymorphism):**
1. `register_record_types()`: Pre-scan all MIR functions, collect complete record types
2. `collect_field_accesses()`: Per-function, scan all `Rvalue::Field` → map local → field names
3. `resolve_field_offset()`: Union partial type fields + accessed fields → find smallest matching complete type → compute offset

**Bool/Int coercion:** At variable assignment, `uextend` I8→I64 or `ireduce` I64→I8 as needed.

**Closure calls:** Load fn_ptr from closure[0], `call_indirect` with closure as hidden first arg.

**"main" → "glyph_main":** Renamed at declaration time; C runtime's `main()` calls `glyph_main()`.

### C Runtime (runtime.rs)

Embedded as `RUNTIME_C` string constant (compiled by cc at link time).

**Categories:**
- **Memory:** glyph_alloc, dealloc, realloc
- **Print:** glyph_print, println, eprintln
- **String:** str_concat, str_eq, str_len, str_slice, str_char_at, int_to_str, str_to_int, str_to_cstr, cstr_to_str
- **StringBuilder:** sb_new, sb_append, sb_build
- **Array:** array_new, array_push, array_pop, array_set, array_len, array_bounds_check
- **I/O:** read_file, write_file, args, system
- **Control:** panic, exit, raw_set
- **Signal:** SIGSEGV handler prints `_glyph_current_fn`
- **Entry:** `main()` → `signal(SIGSEGV, handler)` → `glyph_set_args()` → `glyph_main()`

**SQLite runtime** (`RUNTIME_SQLITE_C`): Separate, conditionally linked when extern_ has sqlite3 libs.
- glyph_db_open, db_close, db_exec, db_query_rows, db_query_one

### Linker (linker.rs)

`link_with_extras(object_bytes, exe_path, extern_libs, runtime_c?, extra_sources)`:
1. Write object to temp dir
2. Write runtime.c + extra sources
3. `cc program.o runtime.c extras... -no-pie -o OUTPUT -lLIBS`

---

## Crate 6: glyph-cli (Build Orchestration)

**Files:** `build.rs` (688), `main.rs` (121)

### Commands (main.rs)

5 commands via clap: `init`, `build [--full] [--emit-mir] [--gen N]`, `run [--gen N]`, `check [--gen N]`, `test [--gen N]`

### Build Pipeline (build.rs: cmd_build)

**Phase 1 — Load:** `db.dirty_defs_gen(target_gen)` or `db.effective_defs(target_gen)` if `--full`. Separate fns from types.

**Phase 2 — Type-check:**
1. Extract enum variants from type defs (`extract_enum_variants`)
2. Register enums in InferEngine
3. Pass 1: `pre_register_fn` all signatures (enables cross-references)
4. Pass 2: `infer_fn_def` for each function body
5. Resolve all types, report errors with `format_diagnostic`

**Phase 3 — MIR Lower:**
```rust
for each typed fn:
    MirLower::new()
    lower.set_known_functions(...)
    lower.register_enum(type_name, variants)
    mir = lower.lower_fn(name, fndef, fn_ty)
    collect lifted_fns (lambda lifts)
```

**Phase 4 — Codegen:**
1. `CodegenContext::new()`
2. Declare all functions (user + lifted)
3. Declare externs from extern_ table
4. `declare_runtime()` — 27+ runtime function signatures
5. `register_record_types()` — pre-scan for row polymorphism
6. `compile_function()` for each MIR function
7. `finish()` → object bytes

**Phase 5 — Link:**
1. Collect `-l` flags from extern_ libs
2. Conditionally include RUNTIME_SQLITE_C
3. `link_with_extras(object_bytes, exe_path, libs, RUNTIME_C, extras)`
4. `mark_compiled()` for each definition

### Key Helper Functions (build.rs)

| Function | Purpose |
|----------|---------|
| `extract_enum_variants(types)` | Parse type defs → variant lists for MIR |
| `ast_type_to_mir(type_expr)` | Convert AST TypeExpr → MirType |
| `build_extern_sig(codegen, sig_str)` | Parse `I -> S -> V` → Cranelift Signature |
| `declare_runtime(codegen)` | Register 27+ runtime fn signatures in codegen |
| `add_runtime_known_functions(map)` | Populate known_functions for MIR lowering |
| `report_parse_error(def, err)` | Format parse error with source context |
| `report_type_error(name, body, err)` | Format type error with source context |

### cmd_check (build.rs)

Same as build phases 1-2 only (parse + type-check). No MIR/codegen/link.

---

## Cross-Cutting Concerns

### Incremental Compilation Flow

```
update_body(id, new_body)
  → TEMP trigger: recompute hash + tokens, set compiled=0
  → TEMP trigger: cascade compiled=0 to all dependents via dep table
  → Next build: SELECT * FROM v_dirty returns changed def + transitives
  → Only those defs are re-parsed, re-typechecked, re-lowered, re-codegenned
  → After codegen: mark_compiled(id) for each
```

### Generational Versioning

`gen` column on def table (default 1). `--gen=N` flag → `effective_defs(N)` selects max(gen) ≤ N per (name,kind). Gen=2 adds struct codegen overrides in self-hosted compiler.

### Error Reporting Pipeline

```
Parse error → ParseError { span } → format_diagnostic(def_name, body, span, "error", msg)
Type error  → TypeError { span }  → format_diagnostic(def_name, body, span, "type error", msg)
Runtime     → SIGSEGV handler     → "segfault in function: NAME"
```

### Record Field Offset Resolution (Row Polymorphism)

**Problem:** MIR partial types like `{kind: I ..}` don't know the complete record → can't compute byte offsets.

**Solution (3-step):**
1. Pre-scan all MIR functions → collect complete record types (types with all fields known)
2. Per-function: pre-scan `Rvalue::Field` → map each local to its accessed field names
3. At codegen: union partial type + accessed fields → match against smallest complete type → position × 8

**Pitfall:** If two complete types share the accessed field set (e.g., TyNode and AstNode both have `n1, ns, sval`), picks the one with most fields. This is a heuristic, not a proof.

### Closure Compilation

**Lowering:** Collect free vars → lift lambda to top-level fn with hidden `env_ptr` first param → captures loaded from env at offsets 8, 16, ... → emit `MakeClosure(lifted_name, captures)` in outer context.

**Codegen:** Heap-allocate `(1 + N_captures) × 8` bytes → store fn_ptr at [0], captures at [1..N] → `call_indirect` with closure as first arg.

**Note:** Self-hosted C codegen does NOT support closures (MakeClosure = 9 is defined but unimplemented).

---

## File Quick Reference

| File | Lines | Key Contents |
|------|-------|-------------|
| `glyph-parse/src/parser.rs` | 1693 | Recursive-descent parser, all definition kinds |
| `glyph-mir/src/lower.rs` | 1306 | AST→MIR lowering, pattern compilation, TCO, lambda lifting |
| `glyph-codegen/src/cranelift.rs` | 893 | Cranelift IR generation, field offset resolution, closure calls |
| `glyph-typeck/src/infer.rs` | 791 | HM type inference engine |
| `glyph-parse/src/lexer.rs` | 755 | Indentation-sensitive lexer, string interpolation |
| `glyph-cli/src/build.rs` | 688 | 5-phase build pipeline orchestration |
| `glyph-codegen/src/runtime.rs` | 637 | Embedded C runtime (strings, arrays, I/O, signal handler) |
| `glyph-typeck/src/unify.rs` | 366 | Union-find substitution, row unification |
| `glyph-db/src/queries.rs` | 338 | All SQL queries (insert, resolve, dirty, deps) |
| `glyph-parse/src/ast.rs` | 301 | Full AST: 8 def kinds, expressions, patterns, types |
| `glyph-typeck/src/resolve.rs` | 288 | Name resolution + dependency tracking |
| `glyph-mir/src/ir.rs` | 251 | MIR data structures (Rvalue, Terminator, MirType) |
| `glyph-db/src/connection.rs` | 229 | DB open/create, triggers, migration |
| `glyph-typeck/src/types.rs` | 224 | Type enum (20 variants), RowType |
| `glyph-db/src/model.rs` | 170 | Rust model types for all DB tables |

## Test Distribution

| Crate | Tests | Coverage Focus |
|-------|-------|---------------|
| glyph-parse | 38 | Lexer (16): tokens, indent, interpolation, raw strings. Parser (18): all expr types, match, precedence. Span (4): diagnostics |
| glyph-typeck | 17 | Infer (12): literals, ops, records, lambdas, pipe, match. Unify (5): basics, fn, row, occurs check |
| glyph-db | 6 | Insert, mark compiled, dirty cascade, name resolve, externs, tags |
| glyph-mir | 4 | Simple lowering, match, constants, serde roundtrip |
| glyph-codegen | 3 | Simple codegen, add, branch |
| glyph-cli | 0 | (Integration tested via cargo run) |

---

# Part 2: Self-Hosted Compiler (glyph.glyph)

*583 gen=1 definitions (580 fn, 3 type) + 38 gen=2 definitions. Compiles to C → invokes `cc`. The compiler itself is stored as rows in a SQLite database.*

## Overview

The self-hosted compiler reimplements the Rust compiler's pipeline in Glyph itself. It reads definitions from a `.glyph` database via SQL, tokenizes, parses, lowers to MIR, generates C code, writes it to `/tmp/glyph_out.c`, and invokes `cc` to produce a native executable.

**Pipeline:** `SQLite → tokenize → parse_fn_def → lower_fn_def → compile_fn_to_c → cg_program → write_file → cc`

**Key differences from Rust compiler:**
- **Backend**: C codegen (not Cranelift)
- **No loops**: All iteration is recursive with `*_loop` suffix convention
- **No closures in codegen**: `MakeClosure` (rv=9) defined but not emitted
- **Type system present but bypassed**: ~96 type inference functions exist but `compile_fn` doesn't call `typecheck_fn`
- **All values are `long long`**: No type distinctions at C level (everything is 8 bytes)
- **String interpolation**: Not processed by self-hosted parser; use `str_concat`/`s2`-`s7` helpers

## Type Definitions (3)

```
Token     = {kind:I start:I end:I line:I}        → 4 fields, sorted: end, kind, line, start
AstNode   = {kind:I ival:I sval:S n1:I n2:I n3:I ns:[I]} → 7 fields, sorted: ival, kind, n1, n2, n3, ns, sval
ParseResult = {node:I pos:I}                      → 2 fields, sorted: node, pos
```

Fields are sorted alphabetically (BTreeMap convention). Record construction uses `{field: value}` syntax; field access uses `.field` notation. At C level, these become `typedef struct` (gen=2) or offset-based `((long long*)p)[N]` (gen=1).

## Subsystem Map

### 1. Tokenizer (~56 functions)

**Entry:** `tokenize(src) → [Token]`

**Core loop:** `tok_loop(src, pos, tokens, indents, bracket_depth)` → recursive, calls `tok_one` per character

**Token kind constants** (`tk_*`): 51 constants returning integer IDs
```
tk_int=1, tk_float=2, tk_str=3, tk_ident=4, tk_type_ident=5,
tk_plus=10, tk_minus=11, tk_star=12, tk_slash=13, tk_percent=14,
tk_eq=20, tk_eq_eq=21, tk_bang_eq=22, tk_lt=23, tk_gt=24, ...
tk_lparen=40, tk_rparen=41, tk_lbrace=42, tk_rbrace=43, ...
tk_indent=60, tk_dedent=61, tk_newline=62, tk_eof=63, tk_error=64
```

**Integer return packing** (`tok_pack`): Returns `kind * 1000000 + new_pos` — encodes both token kind and new position in a single integer. Callers extract via division and modulo.

**Support functions:** `is_digit`, `is_alpha`, `is_alnum`, `is_upper`, `is_space`, `scan_ident_end`, `scan_number_end`, `scan_string_end`, `scan_raw_string_end`, `keyword_kind`, `measure_indent`/`measure_indent_loop`, `emit_dedents`/`flush_dedents`, `skip_blanks`, `skip_comment`

**Token text extraction:** `tok_text(src, token)` → `str_slice(src, token.start, token.end)`

### 2. Parser (~50 functions)

**Entry:** `parse_fn_def(src, tokens, pos, ast_pool) → ParseResult`

Returns `ParseResult` with `.node` (index into `ast_pool` array) and `.pos` (new token position).

**AST construction:** `mk_node(kind, ival, sval, n1, n2, n3, ns)` → `AstNode` record. Node kind constants use `ex_*` prefix:
```
ex_int_lit=1, ex_str_lit=2, ex_bool_lit=3, ex_ident=4, ex_type_ident=5,
ex_binary=10, ex_unary=11, ex_call=12, ex_field_access=13, ex_index=14,
ex_lambda=15, ex_match=16, ex_block=17, ex_array=18, ex_record=19,
ex_pipe=20, ex_compose=21, ex_propagate=22, ex_unwrap=23,
ex_str_interp=24, ex_field_accessor=25
```

**Statement kinds:** `st_expr=1, st_let=2, st_assign=3`
**Pattern kinds:** `pat_wildcard=1, pat_int=2, pat_str=3, pat_bool=4, pat_ident=5, pat_ctor=6`

**Expression precedence** (lowest → highest):
`parse_pipe_expr` → `parse_compose` → `parse_logic_or` → `parse_logic_and` → `parse_cmp` → `parse_add` → `parse_mul` → `parse_unary` → `parse_postfix` → `parse_atom`

Each level has a `*_loop` companion for left-recursive iteration (since Glyph has no loops).

**Pattern parsing:** `parse_pattern` dispatches to `pat_wildcard`, `pat_int`, `pat_str`, `pat_bool`, `pat_ident`, `pat_ctor`.

**Support:** `peek(tokens, pos)`, `expect_tok(tokens, pos, kind)`, `check_tok(tokens, pos, kind)`, `skip_nl`, `skip_ws`

### 3. Type System (~96 functions)

**Note:** The type system is fully implemented but **not called** in the production compilation pipeline. `compile_fn` goes directly from parse → MIR lower, skipping `typecheck_fn`. The type system exists for future use and was ported as part of the C.4 phase.

**Engine:** `mk_engine() → record` with fields: `bindings`, `env_marks`, `env_names`, `env_types`, `errors`, `next_var`, `parent`, `ty_pool`

**TyNode pool:** Types are stored as `TyNode = {tag:I, n1:I, n2:I, ns:[I], sval:S}` in a flat array (`ty_pool`). Type constructors (`mk_tint`, `mk_tstr`, `mk_tfn`, `mk_trecord`, etc.) push into the pool and return the index.

**Type tag constants** (`ty_*`): `ty_int=1, ty_uint=2, ty_float=3, ty_str=4, ty_bool=5, ty_void=6, ty_never=7, ty_fn=10, ty_array=11, ty_opt=12, ty_res=13, ty_record=14, ty_var=15, ty_field=20, ty_error=99`

**Substitution:** `subst_fresh_var`, `subst_find` (union-find with path compression), `subst_walk`, `subst_resolve`, `subst_bind` (with occurs check via `ty_contains_var`)

**Unification:** `unify(eng, a, b)` → `unify_tags` dispatches by type tag pair → `unify_records`, `unify_ns` (parallel array unification), `unify_array_elems`, `unify_fields_against`

**Environment:** Scope stack using parallel arrays. `env_push`, `env_pop`/`env_pop_to` (mark-based), `env_insert`, `env_lookup`/`env_lookup_at` (reverse scan)

**Inference:** `infer_expr`/`infer_expr2`/`infer_expr3` (split into three functions to keep each under ~30 match arms), `infer_call`, `infer_binary`, `infer_match`, `infer_lambda`, `infer_record`, `infer_field_access`, etc.

### 4. MIR Lowering (~108 functions)

**Entry:** `lower_fn_def(ast_pool, fn_node_idx) → MIR result record`

**Lowering context:** `mk_mir_lower(fn_name)` creates a record with:
- `block_stmts: [[Stmt]]` — parallel 2D array (stmts per block)
- `block_terms: [Term]` — one terminator per block
- `local_names: [S]`, `local_types: [I]` — parallel arrays for locals
- `cur_block: [I]`, `nxt_local: [I]`, `nxt_block: [I]` — single-element arrays (mutable counters)
- `var_names: [S]`, `var_locals: [I]`, `var_marks: [I]` — scope stack for variable env
- `fn_name`, `fn_params`, `fn_entry`

**MIR data model:**

*Operand* (`mk_op`): `{okind:I, oval:I, ostr:S}`. Kind constants: `ok_local=1, ok_const_int=2, ok_const_bool=3, ok_const_str=4, ok_const_unit=5, ok_func_ref=6, ok_extern_ref=7`

*Statement* (`mk_stmt`): `{sdest:I, skind:I, sival:I, sstr:S, sop1:Op, sop2:Op, sops:[Op]}`. Kind constants: `rv_use=1, rv_binop=2, rv_unop=3, rv_call=4, rv_aggregate=5, rv_field=6, rv_index=7, rv_str_interp=8, rv_make_closure=9`

*Terminator* (`mk_term`): `{tkind:I, top:Op, tgt1:I, tgt2:I}`. Kind constants: `tm_goto=1, tm_branch=2, tm_return=3, tm_switch=4, tm_unreachable=5`

*Aggregate kinds*: `ag_tuple=1, ag_array=2, ag_record=3, ag_variant=4`

**Unique field prefixes** avoid the row polymorphism disambiguation problem: `okind/oval/ostr` for operands, `sdest/skind/sival/sstr/sop1/sop2/sops` for statements, `tkind/top/tgt1/tgt2` for terminators. No two record types share field names.

**Lowering functions:**

*Expression lowering*: `lower_expr`/`lower_expr2` dispatch by AST kind to: `lower_ident`, `lower_binary`, `lower_unary`, `lower_call`, `lower_field`, `lower_idx`, `lower_lambda`, `lower_pipe`, `lower_array`, `lower_record`, `lower_str_interp`, `lower_type_ident`

*Statement lowering*: `lower_stmts`/`lower_stmt` → `lower_let`, `lower_assign`, or expression statement

*Match lowering*: `lower_match` → `lower_match_arms` → `lower_match_wildcard`, `lower_match_int`, `lower_match_str`, `lower_match_bool`, `lower_match_ident`, `lower_match_ctor`

*MIR emission helpers*: `mir_emit` (raw statement), plus typed helpers: `mir_emit_use`, `mir_emit_binop`, `mir_emit_unop`, `mir_emit_call`, `mir_emit_field`, `mir_emit_index`, `mir_emit_aggregate`

*Block management*: `mir_new_block`, `mir_switch_block`, `mir_terminate`, `mir_term_kind`

*Variable scope*: `mir_bind_var`, `mir_lookup_var`/`mir_lookup_var_at`, `mir_push_scope`, `mir_pop_scope`/`mir_pop_scope_to`

*Binary op constants*: `op_add=1..op_or=14`, mapped via `lower_binop`; unary: `op_neg=15, op_not=16` via `lower_unop`

*String-aware lowering*: `lower_str_binop` emits `str_concat` for `+`, `str_eq` for `==`, `!str_eq` for `!=` when operand types are known strings (via `local_types` parallel array). `is_str_op`/`is_str_ret_fn` track string-returning functions.

### 5. MIR Post-Processing (~35 functions)

**Field offset resolution** (gen=1 only — gen=2 uses struct access):

`fix_all_field_offsets(mirs)` → `fix_offs_mirs` → `fix_offs_fn` → `fix_offs_blks` → `fix_offs_stmts`

Supporting: `build_type_reg` (build global record type registry from all MIR aggregates), `coll_local_acc`/`coll_acc_blks`/`coll_acc_stmts` (collect field accesses per local), `resolve_fld_off` (match field name + local's accessed fields against type registry)

**Extern call fixing:**

`fix_extern_calls(mirs, externs)` → `fix_ext_mirs` → `fix_ext_fn` → `fix_ext_blks` → `fix_ext_stmts`

Renames `ok_func_ref` operands from user-declared name to `glyph_`-prefixed wrapper name. `find_extern`/`find_extern_loop` search the extern list.

**Type disambiguation:** `find_matching_type`, `find_best_type` (prefer largest type when field sets overlap), `all_fields_in`

### 6. C Code Generation (~70 functions)

**Entry (gen=1):** `cg_program(mirs)` → `s6(preamble, forward_decls, "\n", functions, "\n", main_wrapper)`

**Entry (gen=2):** `cg_program(mirs, struct_map)` → `s7(preamble, typedefs, forward_decls, "\n", functions, "\n", main_wrapper)`

**Program structure generation:**
- `cg_preamble()` — `#include <stdio.h>` etc. + embedded runtime
- `cg_forward_decls(mirs, i)` — `long long glyph_NAME(params);`
- `cg_functions(mirs, i)` / `cg_functions2(mirs, i, struct_map)` — all function bodies
- `cg_main_wrapper()` — C `main()` calling `glyph_main()`

**Per-function generation:**
- `cg_function(mir)` / `cg_function2(mir, struct_map, local_types)` → signature + locals + blocks
- `cg_locals_decl(mir)` — `long long _0, _1, _2, ...;`
- `cg_blocks(mir)` / `cg_blocks2(mir, struct_map, local_types)` → labeled blocks
- `cg_block`/`cg_block2` → statements + terminator
- `cg_stmt`/`cg_stmt2` → dispatches by `skind` to specific generators

**Statement generators:**
- `cg_call_stmt` — `_dest = fn_name(args);`
- `cg_field_stmt` / `cg_field_stmt2` — `((long long*)_op)[offset]` / `((Glyph_Name*)_op)->field`
- `cg_aggregate_stmt` / `cg_aggregate_stmt2` — record/array/variant construction
- `cg_record_aggregate` / `cg_record_aggregate2` — record-specific with field stores
- `cg_array_aggregate` — array construction with `glyph_array_new` + `glyph_array_push`
- `cg_variant_aggregate` — enum variant with tag + payload
- `cg_index_stmt` — `glyph_array_bounds_check` + load
- `cg_str_interp_stmt` — `sb_new → sb_append × N → sb_build`
- `cg_binop_str`, `cg_unop_str` — C operator strings

**Terminator generation:** `cg_term` dispatches by `tkind`:
- `tm_goto` → `goto L_N;`
- `tm_branch` → `if (_op) goto L_T; else goto L_F;`
- `tm_return` → `return _op;`
- `tm_switch` → `switch(_op) { case N: goto L_N; ... default: goto L_D; }`

**Operand rendering:** `cg_operand(op)` → `_N` (local), `42LL` (int), `str_literal` (string), `glyph_NAME` (func ref)

**String handling:**
- `cg_str_literal(s)` → `glyph_make_str("escaped", len)` with escape processing
- `cg_escape_str(s)` / `cg_escape_chars(s, i)` → C string escaping (backslashes, quotes, newlines)
- `process_escapes(s)` / `process_esc_loop(s, i)` → Glyph escape → raw byte conversion

**Brace escape critical pattern:** `cg_lbrace()` returns `"{"`, `cg_rbrace()` returns `"}"` — essential because `{` in Glyph string literals triggers interpolation in the Rust compiler.

**Embedded C runtime:**
- `cg_runtime_c()` — core: memory, print, strings, arrays, panic, exit
- `cg_runtime_args()` — argc/argv capture
- `cg_runtime_io()` — str_to_cstr, cstr_to_str, read_file, write_file, system
- `cg_runtime_sb()` — string builder (sb_new, sb_append, sb_build)
- `cg_runtime_raw()` — raw_set, print (no-newline), array_new
- `cg_runtime_sqlite()` — db_open, db_close, db_exec, db_query_rows, db_query_one
- `cg_runtime_extra()` — SIGSEGV handler, `_glyph_current_fn` tracking
- `cg_runtime_full(externs)` — combines all runtimes, conditionally includes sqlite

**Runtime function detection chain:** `is_runtime_fn → fn2 → fn3 → fn4 → fn5 → fn6` — chain of 6 functions (each handles ~5-8 names) checking if a function name is a built-in runtime function. Used to skip extern wrapper generation and to prefix calls with `glyph_`.

**String concat helpers:** `s2(a,b)` through `s7(a,b,c,d,e,f,g)` — concat 2-7 strings in one call. Used extensively in codegen to build C code strings. **Critical pitfall:** More than ~7 nested `s2()` calls cause stack overflow in Cranelift-compiled binary.

### 7. Gen=2 Struct Codegen (38 functions)

Gen=2 definitions override gen=1 counterparts for struct-aware C codegen. Built with `glyph0 --gen=2`.

**Type reading (7 fn):** `read_type_defs(db)`, `parse_struct_fields`/`psf_extract_names`/`psf_extract_loop`/`psf_find_char` — read type definitions from DB, extract field names

**Struct map (3 fn):** `build_struct_map(type_rows)`, `find_struct_name`/`fsn_loop`/`fsn_fields_eq`/`fsn_fields_eq_loop` — build mapping from sorted field sets to type names

**Typedef generation (3 fn):** `cg_all_typedefs`/`cg_all_typedefs_loop`, `cg_struct_typedef`, `cg_struct_fields` — emit `typedef struct { long long f1; ... } Glyph_Name;`

**Local type tracking (5 fn):** `build_local_types(mir, struct_map)`, `blt_init`, `blt_scan_blks`/`blt_scan_stmts` — scan MIR for record aggregates, match against struct map, tag locals. `blt_propagate_blks`/`blt_propagate_stmts` — propagate types through `rv_use` copies.

**Codegen overrides (9 fn):** `cg_function2`, `cg_blocks2`, `cg_block2`, `cg_block_stmts2`, `cg_stmt2`, `cg_field_stmt2`, `cg_aggregate_stmt2`, `cg_record_aggregate2`, `cg_struct_stores`

**Pipeline overrides (10 fn):** `compile_db(db_path, output_path)`, `build_program(sources, externs, type_rows)`, `build_test_program(...)`, `cg_program(mirs, struct_map)`, `cg_test_program(...)`, `cg_functions2(mirs, i, struct_map)`, `cmd_build(argv, argc, db_path)`, `cmd_test(argv, argc, db_path)`

**Known limitation:** `build_local_types` identifies records at construction sites (`rv_aggregate`) but doesn't propagate type info through function parameters. Functions receiving records as parameters fall back to offset-based field access. See `documents/gstat-assessment.md` for details.

### 8. Extern System (~24 functions)

**Architecture:** Programs declare externs in `extern_` table → compiler reads them → generates C wrappers → renames MIR func refs → collects `-l` flags.

**Sig parsing:** `split_arrow`/`split_arrow_loop`, `sig_param_count`, `sig_is_void_ret` — parse `I -> S -> I` format

**Extern reading:** `read_externs(db)`, `find_extern`/`find_extern_loop`

**Wrapper generation:** `cg_extern_wrapper(ext)`, `cg_extern_wrappers(externs, i)`, `cg_wrap_params(n, i)`, `cg_wrap_call_args(n, i)` — generates `long long glyph_NAME(params) { return (long long)(SYMBOL)(args); }`

**Library collection:** `collect_libs`/`collect_libs_loop`, `lib_seen`, `needs_sqlite`/`needs_sqlite_loop`

**Skip rules:** Functions with `glyph_` prefix or matching `is_runtime_fn` chain are skipped.

### 9. CLI Dispatch (~39 functions)

**Entry:** `main` → `dispatch_cmd(argv, argc, cmd)` → chain of 4 dispatch functions

**Dispatch chain:** `dispatch_cmd` → `dispatch_cmd2` → `dispatch_cmd3` → `dispatch_cmd4` — each handles 4-5 commands via match statements (Glyph match arms are limited, so commands are split across functions).

**17 commands:** `init`, `build`, `run`, `test`, `get`, `put`, `rm`, `ls`, `find`, `deps`, `rdeps`, `stat`, `dump`, `sql`, `extern`, `check`, plus `print_usage`

**Command implementations:** `cmd_init`, `cmd_build`, `cmd_run`, `cmd_test`, `cmd_get`, `cmd_put`, `cmd_rm`, `cmd_ls`, `cmd_find`, `cmd_deps`, `cmd_rdeps`, `cmd_stat`, `cmd_dump`, `cmd_sql`, `cmd_extern_add`, `cmd_check`

**Support:** `do_put` (upsert via DELETE+INSERT), `extract_name`/`extract_name_loop` (first token from body), `find_flag`/`has_flag` (CLI flag parsing), `sql_escape`/`sql_esc_loop`, `join_names`, `join_cols`, `join_args`, `print_ls_rows`, `print_sql_rows`, `format_kind_counts`, `rm_check_rdeps`

**Database interaction pattern:** `glyph_db_open(path) → glyph_db_exec/glyph_db_query_rows/glyph_db_query_one → glyph_db_close(db)`

### 10. Build Orchestration (~10 functions)

**Entry:** `cmd_build(argv, argc, db_path)` (gen=2 override) or `build_program` (gen=1)

**Gen=2 flow (compile_db):**
1. `glyph_db_open(db_path)` → `read_fn_defs(db)`, `read_externs(db)`, `read_type_defs(db)`
2. `build_struct_map(type_rows)` → map sorted field sets to type names
3. `compile_fns(sources, 0)` → `compile_fn` each → collect MIR array
4. `fix_all_field_offsets(mirs)` → resolve field indices
5. `fix_extern_calls(mirs, externs)` → rename extern refs
6. `cg_program(mirs, struct_map)` → full C source string
7. Prepend `cg_runtime_full(externs)` + `cg_extern_wrappers`
8. `glyph_write_file("/tmp/glyph_out.c", c_source)`
9. `glyph_system("cc -O2 -Wno-int-conversion ... -o OUTPUT /tmp/glyph_out.c -lLIBS")`

**Test build flow (build_test_program):** Similar but reads `test` defs, generates dispatch-table main, includes assertion runtime.

### 11. Utility Functions (~20 functions)

**String helpers:** `s2`-`s7` (multi-concat), `itos` (int_to_str alias), `sort_str_arr`/`sort_str_copy`/`sort_str_do`/`sort_str_insert` (insertion sort for string arrays), `str_lt`/`str_lt_loop` (lexicographic comparison)

**Diagnostics:** `format_diagnostic`, `extract_source_line`, `find_line_start`, `find_line_end`, `line_col`/`line_col_loop`, `make_caret`, `make_spaces`, `report_error`

**Schema:** `init_schema(db)` — embedded full schema SQL as string constant

**Array helpers:** `make_empty_2d`, `fill_empty_2d`, `pool_push`

---

## Programming Patterns

### 1. Recursive Iteration (No Loops)

All iteration uses tail-recursive functions. Convention: base function + `*_loop` helper.

```
parse_args src tokens pos ast =
  match check_tok(tokens, pos, tk_rparen())
    true -> {node: 0, pos: pos + 1}
    _ -> parse_args_loop(src, tokens, pos, ast, [])

parse_args_loop src tokens pos ast args =
  r = parse_expr(src, tokens, pos, ast)
  new_args = glyph_array_push(args, r.node)
  match check_tok(tokens, r.pos, tk_comma())
    true -> parse_args_loop(src, tokens, r.pos + 1, ast, new_args)
    _ -> ...
```

### 2. Match-as-Control-Flow

Glyph has no `if/else` — all branching is `match`:
```
match x > 0
  true -> handle_positive(x)
  _ -> handle_other(x)
```

### 3. Single-Element Array Mutation

Mutable state uses single-element arrays: `counter = [0]`, then `raw_set(counter, 0, counter[0] + 1)`. Used in `mk_mir_lower` for `cur_block`, `nxt_local`, `nxt_block`, `fn_entry`.

### 4. Integer Constant Functions

Enum-like constants are zero-arg functions: `tk_int = 1`, `rv_use = 1`, `ty_str = 4`. Each returns a fixed integer.

### 5. Chain Functions

When dispatch logic exceeds ~30 match arms, it's split across chained functions:
- `dispatch_cmd` → `dispatch_cmd2` → `dispatch_cmd3` → `dispatch_cmd4`
- `is_runtime_fn` → `fn2` → `fn3` → `fn4` → `fn5` → `fn6`
- `lower_expr` → `lower_expr2`
- `infer_expr` → `infer_expr2` → `infer_expr3`

### 6. Unique Field Prefixes

Record types use unique prefixes to avoid field offset ambiguity:
- Operand: `okind`, `oval`, `ostr`
- Statement: `sdest`, `skind`, `sival`, `sstr`, `sop1`, `sop2`, `sops`
- Terminator: `tkind`, `top`, `tgt1`, `tgt2`
- Token: `kind`, `start`, `end`, `line`
- AstNode: `kind`, `ival`, `sval`, `n1`, `n2`, `n3`, `ns`
- ParseResult: `node`, `pos`

### 7. Brace Escape Pattern

Cannot use `{` or `}` directly in Glyph string literals (triggers interpolation in Rust compiler). Use helper functions: `cg_lbrace()` returns `"{"`, `cg_rbrace()` returns `"}"`. In self-hosted compiler code, use `"\{"` for literal `{`.

### 8. Zero-Arg Function Gotcha

Zero-arg functions with side effects are evaluated eagerly (treated as constants). Add a dummy parameter:
```
print_usage u = eprintln("Usage: glyph <command> <db> [args]")
```

---

## Known Limitations

| Limitation | Impact | Workaround |
|-----------|--------|------------|
| Type system not called in pipeline | No type errors caught at compile time | Runtime errors only |
| No closures in C codegen | Lambda lifting defined but not emitted | Avoid closures in Glyph programs |
| No string interpolation in self-hosted parser | `"text {expr}"` not parsed | Use `s2()`-`s7()` or `str_concat` |
| Gen=2 field reads on parameters | Falls back to offset-based | Works correctly, cosmetic issue |
| `s2()` nesting limit ~7 | Stack overflow in Cranelift binary | Combine at same nesting level |
| No stdin support | `read_file` uses fseek | Use `-b` flag or temp files |
| No GC | All heap allocs persist | Short-lived programs only |
| Self-hosted can't self-build with gen=2 | Sees both gen=1 and gen=2 overrides | Use `glyph0 --gen=2` |
| `tokens=0` from self-hosted | Self-hosted doesn't compute tokens | Run `cargo run -- build --full` |

---

## Function Index by Subsystem

| Subsystem | Count | Prefix/Pattern | Key Entry Points |
|-----------|-------|----------------|-----------------|
| Tokenizer | ~56 | `tok_*`, `tk_*`, `scan_*` | `tokenize` |
| Parser | ~50 | `parse_*`, `pat_*` | `parse_fn_def` |
| Type System | ~96 | `infer_*`, `unify_*`, `subst_*`, `ty_*`, `mk_t*`, `env_*` | `typecheck_fn`, `mk_engine` |
| MIR Lowering | ~108 | `lower_*`, `mir_*`, `mk_op*`, `mk_stmt`, `rv_*`, `ok_*`, `op_*`, `tm_*` | `lower_fn_def`, `mk_mir_lower` |
| MIR Post-Process | ~35 | `fix_*`, `coll_*`, `build_type_reg*`, `resolve_fld_off` | `fix_all_field_offsets`, `fix_extern_calls` |
| C Codegen | ~70 | `cg_*` | `cg_program`, `cg_function` |
| Gen=2 Structs | 38 | `cg_*2`, `blt_*`, `bsm_*`, `fsn_*`, `psf_*` | `compile_db`, `build_local_types` |
| Extern System | ~24 | `cg_extern_*`, `cg_wrap_*`, `fix_ext_*`, `collect_libs*` | `cg_extern_wrappers`, `fix_extern_calls` |
| CLI | ~39 | `cmd_*`, `dispatch_cmd*` | `main`, `dispatch_cmd` |
| Build | ~10 | `compile_*`, `build_*`, `read_*` | `compile_db`, `build_program` |
| Utilities | ~20 | `s2`-`s7`, `sort_str_*`, `format_diagnostic` | `itos`, `s2`-`s7` |
