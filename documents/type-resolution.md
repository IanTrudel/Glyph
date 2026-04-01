# Type Resolution Pipeline

## 1. Read from DB

`read_type_defs_gen` queries `SELECT name, body FROM def WHERE kind='type'`. Each row is `[name, body]` — e.g., `["AstNode", "{ival:I, kind:I, sval:S, n1:I, n2:I, n3:I, ns:[I]}"]`. The name comes from `def.name`; bodies contain only the type content (no `Name = ` prefix), consistent with const syntax. Both `cmd_put` and `mcp_tool_put_def` strip the prefix if provided for backward compatibility.

## 2. Build struct_map

`build_struct_map` → `bsm_loop` parses each type body:

- `parse_type_params` — checks for generic params (`<T>`). Generics are skipped at this stage (handled separately by `expand_with_generics`)
- `parse_struct_fields` — extracts field names from `{...}`, sorts alphabetically → `["ival", "kind", "n1", "n2", "n3", "ns", "sval"]`
- `parse_struct_ctypes` — extracts field type annotations (`I`, `S`, `[I]`, etc.)
- Result: each struct_map entry = `[name, sorted_fields, ctypes]`

## 3. Gen=2 codegen (named struct types)

`cg_all_typedefs_loop` emits `typedef struct { ... } Glyph_Name;` for each entry in struct_map. Fields are sorted alphabetically, so offsets are deterministic.

## 4. Per-function local type resolution

`build_local_types2` determines which local variables are struct-typed:

1. **`blt_scan_blks`** — scans for `rv_aggregate` statements that directly construct a struct
2. **`blt_collect_fa_blks`** — collects the set of field names accessed (`.field`) on each local
3. **`blt_scan_calls_blks`** — propagates return types from `ret_map` (inter-procedural)
4. **`blt_propagate_blks`** — propagates types through `rv_use` assignments
5. **`blt_tag_by_fa`** — for still-untyped locals, uses the accessed-field set to find a matching struct via `blt_find_best`

## 5. `blt_find_best` — the disambiguation heuristic

Given a local's accessed fields, iterates struct_map looking for types where all accessed fields are present. Prefers an exact match (field count == accessed count). If multiple types match equally, returns `""` (ambiguous, falls back to gen=1 raw offsets).

## 6. Codegen dispatch (`cg_field_stmt2`)

- If `local_struct_types[local]` has a name → gen=2 path: `((Glyph_Name*)v)->field`
- Otherwise → gen=1 path: `((GVal*)v)[offset]` using numeric field offset from `fix_all_field_offsets`

## 7. Field offset conflicts (ambiguity)

Conflicts arise when two types share field names and code only accesses the shared subset. Consider **TyNode** and **AstNode**:

**TyNode** fields (sorted): `[n1, n2, ns, sval, tag]` — 5 fields
**AstNode** fields (sorted): `[ival, kind, n1, n2, n3, ns, sval]` — 7 fields

When code accesses only shared fields:

```glyph
node = pool_get(eng, idx)
x = node.n1
y = node.n2
```

The accessed-field set is `[n1, n2]`. Both types contain these fields, so `blt_find_best` sees two non-exact matches and returns `""` (ambiguous). The local falls through to the **gen=1 raw offset path**.

Gen=1 uses `fix_all_field_offsets` → `find_best_type_scan`, which has a different heuristic: it picks the **largest** matching type. AstNode (7 fields) wins over TyNode (5 fields).

The problem: field offsets differ because fields are sorted alphabetically:

| Field | TyNode offset | AstNode offset |
|-------|---------------|----------------|
| n1    | 0             | 2              |
| n2    | 1             | 3              |
| ns    | 2             | 5              |
| sval  | 3             | 6              |
| tag   | 4             | —              |

So `node.n1` generates `((GVal*)v)[2]` (AstNode offset) instead of `((GVal*)v)[0]` (TyNode offset) — reading the wrong memory slot.

### Disambiguation via unique fields

The fix is to access a field unique to the intended type, forcing `blt_all_in` to reject the wrong type:

```glyph
node = pool_get(eng, idx)
_ = node.tag          -- AstNode has .kind not .tag, so only TyNode matches
x = node.n1
y = node.n2
```

The accessed set becomes `[n1, n2, tag]`. AstNode fails `blt_all_in` (no `tag` field). Only TyNode matches → unambiguous → gen=2 struct access with correct offsets.

### When conflicts arise

In general, conflicts occur when:

1. Two or more types share field names (e.g., `n1`, `n2`, `ns`, `sval` appear in both AstNode and TyNode)
2. Code only accesses the shared subset — no distinguishing field is touched
3. The gen=1 fallback heuristic picks the wrong type based on size

The more overlapping field names between types, the more likely this occurs. Types with unique fields (`tag` on TyNode, `kind` on AstNode) serve as natural disambiguation hints.

## 8. Why types aren't attached to MIR locals

In a typical compiler (GCC, Rust, LLVM), the IR is typed — every SSA value carries its type. Field access on a struct knows which struct. No ambiguity is possible. Glyph's MIR is untyped by design, and this is a consequence of the build order.

The self-hosted compiler was built bottom-up:

1. **C.5** — MIR lowering designed with no type checker. Every local, operand, and return value is `GVal` (`intptr_t`). The MIR `Operand` is `{okind, oval, ostr}`, the `Statement` is `{sdest, skind, sop1, sop2, ...}` — no type field anywhere.
2. **C.6** — C codegen emits `GVal` everywhere.
3. **C.7/C.8** — Working self-compiling compiler, all untyped.
4. **Later** — Type checker (HM inference) added for error reporting and monomorphization (`tc_results` → `mono_pass`), but inferred per-expression types were never threaded into MIR nodes.
5. **Later** — Gen=2 struct codegen needed types to emit `((Glyph_Name*)v)->field`. Rather than retrofitting types into every MIR node, types are **reconstructed heuristically** at codegen time (`build_local_types2`).

Adding types to the MIR would touch core data structures, but most functions just pass MIR records through without inspecting types. The current heuristic system works but produces this class of edge-case bugs when types share field names. The `_ = node.tag` hints are a pragmatic workaround.

The proper fix would be carrying the type checker's inferred types into the MIR, which would eliminate the heuristic entirely. Two approaches exist:

### A. Typed MIR (recommended)

Add a type field to MIR locals and operands. After HM inference, each local gets its resolved type. Codegen reads it directly — no heuristics, no ambiguity, no `_ = node.tag` hints. This is what every production compiler does (GCC, LLVM, Rust).

The "200+ functions" concern is overstated. Most MIR-consuming functions just pass records through without inspecting types. Only functions that construct typed locals (lowering) or consume them (codegen field access) need changes. The core change is adding a type field to the MIR local/operand records and populating it during lowering from the type checker's inference results.

### B. Type map sidecar (incremental, not recommended)

Keep MIR untyped, but pass the type checker's `tmap` (per-expression type map) alongside the MIR into codegen. `build_local_types2` would first consult the tmap and only fall back to heuristics for locals the type checker didn't reach. This is partially in place already — `tc_results` feeds monomorphization — but the per-local types aren't forwarded to `cg_function2`.

**Why this is worse than A:** It adds complexity (two code paths: tmap lookup + heuristic fallback) without eliminating the heuristic. Any local the type checker doesn't reach still hits the ambiguity bug. It's a patch on a structural problem.

### C. Named record construction — rejected

`TypeName{field: val, ...}` syntax was considered but doesn't fit Glyph's type model. Records are structurally typed — `{x: 1, y: 2}` already IS a `Point` by structure. There's no nominal distinction to annotate. Additionally, `expr{field: val}` is already record update syntax, creating an ambiguity between `Point{x: 1}` (construction) and `point{x: 1}` (update). The disambiguation problem is a codegen gap (type info not reaching field offset resolution), not a source-level problem. Approach B is the correct fix.

## Key insight

Types don't "resolve" in a traditional type system sense. They're used to build a **struct_map** (compile-time type registry), which the codegen uses to decide between typed struct access vs raw GVal pointer offset access. The type inference engine (`mk_engine`, HM unification) is separate — it infers types for correctness checking, while struct_map drives the C code shape.
