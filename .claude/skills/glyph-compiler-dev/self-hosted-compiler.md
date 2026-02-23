# Self-Hosted Compiler Guide (glyph.glyph)

~597 gen=1 definitions (594 fn, 3 type) + 47 gen=2 definitions (~647 total). Compiles Glyph programs to C → `cc` → native executable.

## Overview

The self-hosted compiler is stored as rows in `glyph.glyph` (a SQLite database). It reimplements the Rust compiler's pipeline in Glyph itself, using C code generation instead of Cranelift.

**Pipeline (gen=2):**
```
glyph_db_open → read_fn_defs + read_type_defs + read_externs
  → parse_all_fns (batch tokenize+parse)
  → [optional: tc_pre_register → tc_infer_all → tc_report_errors]  (glyph check only)
  → compile_fns_parsed → lower_fn_def    (per function)
  → fix_all_field_offsets → fix_extern_calls   (post-processing)
  → cg_program → glyph_write_file("/tmp/glyph_out.c") → cc → EXE
```

**Key differences from Rust compiler:**
- C codegen backend (not Cranelift)
- No loops — all iteration is recursive with `*_loop` suffix
- Type system present (~96 functions), available via `glyph check` but not called during `glyph build`
- All values are `long long` — no type distinctions at C level
- String interpolation (`"text {expr}"`) supported in self-hosted parser (tokenizer + parser handle `{}`)

## Type Definitions (3)

```
Token       = {kind:I, start:I, end:I, line:I}
              Sorted: end, kind, line, start (4 fields)

AstNode     = {kind:I, ival:I, sval:S, n1:I, n2:I, n3:I, ns:[I]}
              Sorted: ival, kind, n1, n2, n3, ns, sval (7 fields)

ParseResult = {node:I, pos:I}
              Sorted: node, pos (2 fields)
```

Fields sorted alphabetically (BTreeMap convention). At C level: `typedef struct` (gen=2) or offset-based `((long long*)p)[N]` (gen=1).

## Subsystem Map

### 1. Tokenizer (~60 functions)

**Entry:** `tokenize(src) → [Token]`
**Core loop:** `tok_loop(src, pos, tokens, indents, bracket_depth)` → recursive, calls `tok_one`
**Naming:** `tk_*` constants (53 token kinds), `tok_*` functions, `scan_*` helpers

**String interpolation:** `scan_str_has_interp` checks for `{` in string → `tok_str_interp_loop` emits `tk_str_interp_start`, literal chunks + expression tokens, `tk_str_interp_end`. `tok_interp_expr` tokenizes `{}`-delimited expressions with brace nesting.

**Token kind ranges:**
- Literals: `tk_int=1, tk_str=2, tk_raw_str=3, tk_str_interp_start=4, tk_str_interp_end=5, tk_type_ident=6`
- Operators: `tk_plus=10..tk_dot_dot=39`
- Delimiters: `tk_lparen=40..tk_rbrace=43`
- Layout: `tk_indent=60..tk_error=64`

**Integer return packing:** `tok_pack` returns `kind * 1000000 + new_pos` — single integer encodes both values. Callers extract via division/modulo.

**Key functions:** `is_digit`, `is_alpha`, `scan_ident_end`, `scan_number_end`, `scan_string_end`, `keyword_kind`, `measure_indent`, `emit_dedents`, `tok_text`

### 2. Parser (~52 functions)

**Entry:** `parse_fn_def(src, tokens, pos, ast_pool) → ParseResult`
Returns `.node` (index into `ast_pool` array) and `.pos` (new token position).

**AST construction:** `mk_node(kind, ival, sval, n1, n2, n3, ns) → AstNode` record

**String interpolation parsing:** `parse_str_interp` → `psi_loop` collects `ex_str_lit` and expression nodes into `ex_str_interp` AST node with parts in `.ns` array.

**Node kind constants (`ex_*`):**
- Literals: `ex_int_lit=1, ex_str_lit=2, ex_bool_lit=3`
- Names: `ex_ident=4, ex_type_ident=5`
- Operators: `ex_binary=10, ex_unary=11, ex_call=12, ex_field_access=13, ex_index=14`
- Complex: `ex_lambda=15, ex_match=16, ex_block=17, ex_array=18, ex_record=19`
- Pipeline: `ex_pipe=20, ex_compose=21, ex_propagate=22, ex_unwrap=23`
- Other: `ex_str_interp=24, ex_field_accessor=25`

**Statement kinds:** `st_expr=1, st_let=2, st_assign=3`
**Pattern kinds:** `pat_wildcard=1, pat_int=2, pat_str=3, pat_bool=4, pat_ident=5, pat_ctor=6`

**Precedence chain (low → high):**
`parse_pipe_expr → parse_compose → parse_logic_or → parse_logic_and → parse_cmp → parse_add → parse_mul → parse_unary → parse_postfix → parse_atom`

Each level has a `*_loop` companion for left-recursive iteration.

### 3. Type System (~96 functions)

**Available via `glyph check` but not called during `glyph build`.** The `cmd_check` (gen=2) override runs `tc_pre_register` → `tc_infer_all` → `tc_report_errors`. Record type unification has known bugs (crashes on some record patterns), so typecheck is advisory only.

**Engine:** `mk_engine()` → record with `bindings, env_marks, env_names, env_types, errors, next_var, parent, ty_pool`
**TyNode:** `{tag:I, n1:I, n2:I, ns:[I], sval:S}` stored in flat pool array
**Type tags:** `ty_int=1..ty_error=99`
**Key subsystems:** `subst_*` (union-find), `unify*` (type unification), `env_*` (scope stack), `infer_*`/`infer_expr2`/`infer_expr3` (split across 3 chain functions)

### 4. MIR Lowering (~108 functions)

**Entry:** `lower_fn_def(ast_pool, fn_node_idx) → MIR result record`

**Context:** `mk_mir_lower(fn_name)` creates state with:
- `block_stmts: [[Stmt]]` — 2D array (stmts per block)
- `block_terms: [Term]` — one terminator per block
- `local_names: [S]`, `local_types: [I]` — parallel arrays for locals
- `cur_block: [I]`, `nxt_local: [I]`, `nxt_block: [I]` — single-element mutable counters
- `var_names/var_locals/var_marks` — scope stack for variable environment

**Expression lowering:** `lower_expr`/`lower_expr2` dispatch by AST kind to: `lower_ident`, `lower_binary`, `lower_unary`, `lower_call`, `lower_field`, `lower_idx`, `lower_lambda`, `lower_pipe`, `lower_array`, `lower_record`, `lower_str_interp`

**Match lowering:** `lower_match → lower_match_arms → lower_match_wildcard/int/str/bool/ident/ctor`

**Emission helpers:** `mir_emit`, `mir_emit_use`, `mir_emit_binop`, `mir_emit_call`, `mir_emit_field`, `mir_emit_aggregate`
**Block management:** `mir_new_block`, `mir_switch_block`, `mir_terminate`
**Variable scope:** `mir_bind_var`, `mir_lookup_var`, `mir_push_scope`, `mir_pop_scope`

**String-aware lowering:** `lower_str_binop` checks `local_types` parallel array — when operand is known string, `+` → `str_concat`, `==` → `str_eq`, `!=` → `!str_eq`.

### 5. MIR Post-Processing (~35 functions)

**Field offset resolution (gen=1):**
`fix_all_field_offsets → fix_offs_mirs → fix_offs_fn → fix_offs_blks → fix_offs_stmts`

Supporting: `build_type_reg` (global record type registry from MIR aggregates), `coll_local_acc` (field accesses per local), `resolve_fld_off` (match field + accessed fields against registry)

**Extern call fixing:**
`fix_extern_calls → fix_ext_mirs → fix_ext_fn → fix_ext_blks → fix_ext_stmts`

Renames `ok_func_ref` operands from user name to `glyph_`-prefixed wrapper name.

**Type disambiguation:** `find_best_type` prefers largest type when field sets overlap (e.g., AstNode 7 fields beats TyNode 5 fields).

### 6. C Code Generation (~70 functions)

**Entry:** `cg_program(mirs)` (gen=1) or `cg_program(mirs, struct_map)` (gen=2)

**Program structure:** `cg_preamble()` + `cg_forward_decls` + `cg_functions` + `cg_main_wrapper()`

**Per-function:** `cg_function → cg_locals_decl → cg_blocks → cg_block → cg_stmt/cg_term`

**Statement generators by `skind`:**
- `rv_use=1`: simple assignment
- `rv_binop=2`: `cg_binop_str` → C operator
- `rv_call=4`: `cg_call_stmt` → `fn_name(args)`
- `rv_aggregate=5`: `cg_aggregate_stmt` → record/array/variant construction
- `rv_field=6`: `cg_field_stmt` → `((long long*)p)[offset]`
- `rv_index=7`: `cg_index_stmt` → bounds check + load
- `rv_str_interp=8`: `cg_str_interp_stmt` → `sb_new → sb_append × N → sb_build`

**Terminator generators by `tkind`:**
- `tm_goto=1`: `goto L_N;`
- `tm_branch=2`: `if (_op) goto L_T; else goto L_F;`
- `tm_return=3`: `return _op;`
- `tm_switch=4`: `switch(_op) { case N: goto L_N; ... }`

**Operand rendering:** `cg_operand(op)` → `_N` (local), `42LL` (int), `glyph_make_str(...)` (string), `glyph_NAME` (func ref)

**Embedded C runtime:** `cg_runtime_c()` (core), `cg_runtime_args()`, `cg_runtime_io()`, `cg_runtime_sb()`, `cg_runtime_raw()`, `cg_runtime_sqlite()`, `cg_runtime_extra()` (SIGSEGV handler). Combined via `cg_runtime_full(externs)`.

**Runtime detection chain:** `is_runtime_fn → fn2 → fn3 → fn4 → fn5 → fn6` — 6 chained functions checking if a name is a built-in runtime function.

### 7. Gen=2 Struct Codegen (47 definitions)

Gen=2 overrides gen=1 counterparts for struct-aware codegen. Built with `glyph0 --gen=2`.

**Type reading:** `read_type_defs(db)`, `parse_struct_fields`/`psf_*` — read type defs from DB
**Struct map:** `build_struct_map(type_rows)`, `find_struct_name`/`fsn_*` — map sorted field sets to type names
**Typedef gen:** `cg_all_typedefs` → `typedef struct { long long f1; ... } Glyph_Name;`
**Local type tracking:** `build_local_types(mir, struct_map)` → scan MIR for record aggregates matching types, propagate through `rv_use` copies. **Field-access tagging:** `blt_collect_fa_*` scans all `rv_field` accesses per local; `blt_tag_by_fa` matches accessed field sets against struct map to tag parameters and other untagged locals.
**Codegen chain:** `cg_function2 → cg_blocks2 → cg_block2 → cg_stmt2 → cg_field_stmt2/cg_aggregate_stmt2`
**Pipeline overrides:** `compile_db`, `build_program`, `build_test_program`, `cg_program`, `cmd_build`, `cmd_test`, `cmd_check`

### 8. Extern System (~24 functions)

Programs declare externs in `extern_` table → compiler generates C wrappers.

**Sig format:** Arrow-separated `I -> S -> I` (curried), NOT space-separated.
**Wrapper pattern:** `long long glyph_NAME(params) { return (long long)(SYMBOL)(args); }` — parentheses suppress macro expansion.
**Skip rules:** Functions with `glyph_` prefix or matching `is_runtime_fn` chain are skipped.
**Key functions:** `cg_extern_wrappers`, `fix_extern_calls`, `split_arrow`, `collect_libs`

### 9. CLI Dispatch (~39 functions)

**Entry:** `main → dispatch_cmd → dispatch_cmd2 → dispatch_cmd3 → dispatch_cmd4`
**17 commands:** init, build, run, test, get, put, rm, ls, find, deps, rdeps, stat, dump, sql, extern, check
**DB pattern:** `glyph_db_open(path) → glyph_db_exec/query_rows/query_one → glyph_db_close`
**Put upserts:** DELETE+INSERT (avoids UPDATE triggers that call unavailable custom functions)

### 10. Build Orchestration (~10 functions)

**Gen=2 flow (`compile_db`):**
1. Open DB → read fn defs, externs, type defs
2. Build struct map from type defs
3. Compile each fn → collect MIR array
4. Fix field offsets + extern calls
5. Generate C source via `cg_program`
6. Write to `/tmp/glyph_out.c`
7. Invoke `cc -O2 ... -o OUTPUT /tmp/glyph_out.c`

## MIR Data Model Quick Reference

**Operand (`okind`):** `ok_local=1, ok_const_int=2, ok_const_bool=3, ok_const_str=4, ok_const_unit=5, ok_func_ref=6, ok_extern_ref=7`

**Statement (`skind`):** `rv_use=1, rv_binop=2, rv_unop=3, rv_call=4, rv_aggregate=5, rv_field=6, rv_index=7, rv_str_interp=8, rv_make_closure=9`

**Aggregate (`ag_*`):** `ag_tuple=1, ag_array=2, ag_record=3, ag_variant=4`

**Terminator (`tkind`):** `tm_goto=1, tm_branch=2, tm_return=3, tm_switch=4, tm_unreachable=5`

**Binary ops:** `op_add=1, op_sub=2, op_mul=3, op_div=4, op_mod=5, op_eq=6, op_neq=7, op_lt=8, op_gt=9, op_lteq=10, op_gteq=11, op_and=13, op_or=14`
**Unary ops:** `op_neg=15, op_not=16`

## Programming Patterns

1. **Recursive iteration:** Base function + `*_loop` helper for all iteration
2. **Match-as-control-flow:** `match x > 0` with `true ->` / `_ ->` branches (no if/else)
3. **Single-element array mutation:** `counter = [0]`, `raw_set(counter, 0, counter[0] + 1)`
4. **Integer constant functions:** `tk_int = 1`, `rv_use = 1` — zero-arg functions returning fixed values
5. **Chain functions:** Split at ~30 match arms across numbered functions
6. **Unique field prefixes:** `okind/oval/ostr`, `sdest/skind/sival`, `tkind/top/tgt1/tgt2` etc.
7. **Brace escaping:** `cg_lbrace()`/`cg_rbrace()` for C braces; `"\{"` for literal `{` in self-hosted strings
8. **Zero-arg gotcha:** Side-effect functions need dummy param: `usage u = println(...)`

## Known Limitations

| Limitation | Impact | Workaround |
|-----------|--------|------------|
| Type system bugs on records | `glyph check` crashes on some record type patterns | Advisory only; build pipeline skips typecheck |
| No closures in C codegen | `MakeClosure` defined but not emitted | Avoid closures in programs |
| `s2()` nesting limit ~7 | Stack overflow in Cranelift binary | Combine at same nesting level |
| No stdin support | `read_file` uses fseek | Use `-b` flag or temp files |
| No GC | All heap allocs persist | Short-lived programs only |
| Self-hosted can't self-build gen=2 | Sees both gen=1 and gen=2 overrides | Use `glyph0 --gen=2` |
| `tokens=0` from self-hosted | No BPE computation | `cargo run -- build --full` for correct values |

## Function Index by Subsystem

| Subsystem | Count | Prefix/Pattern | Entry Point |
|-----------|-------|----------------|-------------|
| Tokenizer | ~60 | `tok_*`, `tk_*`, `scan_*` | `tokenize` |
| Parser | ~52 | `parse_*`, `pat_*`, `ex_*`, `st_*` | `parse_fn_def` |
| Type System | ~96 | `infer_*`, `unify_*`, `subst_*`, `ty_*`, `env_*` | `mk_engine` |
| MIR Lowering | ~108 | `lower_*`, `mir_*`, `mk_op`, `mk_stmt`, `rv_*`, `ok_*` | `lower_fn_def` |
| MIR Post-Process | ~35 | `fix_*`, `coll_*`, `build_type_reg` | `fix_all_field_offsets` |
| C Codegen | ~70 | `cg_*` | `cg_program` |
| Gen=2 Structs | 47 | `cg_*2`, `blt_*`, `fsn_*`, `psf_*` | `compile_db` |
| Extern System | ~24 | `cg_extern_*`, `fix_ext_*`, `collect_libs` | `cg_extern_wrappers` |
| CLI | ~39 | `cmd_*`, `dispatch_cmd*` | `main` |
| Build | ~10 | `compile_*`, `build_*`, `read_*` | `compile_db` |
| Utilities | ~20 | `s2`–`s7`, `sort_str_*`, `itos` | `s2` |
