# Disconnected Features in glyph.glyph

**Date:** 2026-02-25
**Scope:** Self-hosted compiler. Features that exist in the database but are not connected to the main pipeline, or are only partially implemented.

---

## 1. Type Checker Not Wired into Build

**Status: Half-baked — exists, runs standalone, but build ignores it**

The type checker is substantial (~119 definitions): `mk_engine`, `infer_expr`, `unify`, `subst_resolve`, `tc_pre_register`, `tc_infer_all`, etc. It performs full HM inference with row polymorphism.

However:
- **`glyph build`** (`build_program`) calls `parse_all_fns` → `compile_fns_parsed` → `cg_program` with **zero type checking**. MIR lowering makes codegen decisions via heuristics (`is_str_op`, `find_best_type`).
- **`glyph check`** has TWO versions:
  - **gen=1**: Does nothing useful — queries rows, prints count, never calls the type checker at all.
  - **gen=2**: Actually runs `mk_engine`, `tc_pre_register`, `tc_infer_all`, `tc_report_errors`. This is the working version.
- **`typecheck_fn`**: Standalone single-function entry point. Called by nobody.

The entire type checker is disconnected from compilation. The context-aware inference spec (documented separately) addresses this.

---

## 2. Error Reporting Infrastructure — Built, Not Used

**Status: Functions exist but nothing calls them**

- **`format_diagnostic def_name src offset line msg`** — Produces formatted error messages with source context and caret pointer (`error in 'foo' at 3:5: ...`). Well-implemented.
- **`report_error def_name src offset line msg`** — Wraps `format_diagnostic` and prints to stderr.

**Who calls them?** Nobody. `report_error` references `format_diagnostic`, but `report_error` itself is called by nothing in the codebase. These were built for the error messages spec but never wired into the parser, build pipeline, or MCP server.

Supporting functions that are also orphaned:
- **`line_col`** — Converts byte offset to column number
- **`extract_source_line`** — Extracts source line from byte offset
- **`make_caret`** — Generates `^` pointer string

All built, all tested by nothing, all called by nothing except `format_diagnostic`.

---

## 3. Type System Placeholders — Tags Without Handlers

**Status: Constants defined, constructors exist, but unification/resolution ignores them**

Three type tags are defined with constructors but have no real integration:

| Tag | Value | Constructor | Used in `unify_tags`? | Used in `subst_resolve`? |
|-----|-------|-------------|----------------------|------------------------|
| `ty_forall` | 16 | `mk_tforall` | No | No |
| `ty_named` | 17 | `mk_tnamed` | No | No |
| `ty_tuple` | 18 | `mk_ttuple` | **Yes** (unify_ns) | No |

- **`ty_forall`** (∀ types / let-polymorphism): Tag 16 exists, `mk_tforall(body, bound_vars, pool)` creates nodes, but no inference rule produces forall types and `subst_resolve` doesn't handle it. Let-polymorphism generalization is not implemented.
- **`ty_named`** (named type constructors): Tag 17 exists, `mk_tnamed(name, type_args, pool)` creates nodes, but nothing ever creates a `ty_named` during inference. Would be needed for user-defined type aliases or ADTs.
- **`ty_tuple`**: Tag 18 exists, `mk_ttuple(elems, pool)` creates nodes, `unify_tags` handles it via `unify_ns`, but `subst_resolve` doesn't recursively resolve tuple elements. Partially working — unification works, resolution doesn't.

---

## 4. MIR Constants Without Codegen

**Status: MIR opcodes defined but C codegen doesn't handle them**

| Constant | Value | Purpose | Handled in codegen? |
|----------|-------|---------|-------------------|
| `rv_make_closure` | 9 | Closure construction | **No** — `cg_stmt` hits unknown case, emits comment |
| `ok_extern_ref` | 7 | External function reference | **No** — never emitted by lowering |
| `tm_switch` | 3 | Multi-way branch | **No** — never emitted by lowering |
| `mir_stmt_count` | — | MIR stats utility | Orphan — called by nothing |
| `mir_term_kind` | — | MIR inspection utility | Orphan — called by nothing |

- **`rv_make_closure`**: The Rust/Cranelift backend handles closures. The self-hosted MIR lowerer can produce closure MIR (from `lower_lambda` in the Rust side), but `cg_stmt` in the self-hosted C codegen doesn't generate C code for it. Closures are completely blocked in the self-hosted pipeline.
- **`ok_extern_ref`**: Defined as operand kind 7, but `fix_extern_calls` rewrites extern references to `ok_func_ref` with `glyph_`-prefixed names. The `ok_extern_ref` constant is never used.
- **`tm_switch`**: A multi-way branch terminator (like a jump table). Never emitted — match compilation uses chains of `tm_branch` instead.

---

## 5. Duplicate/Superseded Functions

**Status: Old versions still in database alongside newer replacements**

### 5.1 `sql_esc_loop` vs `sql_escape_loop`

Two SQL escaping implementations:
- **`sql_esc_loop(sb, s, i, len)`** — Uses `glyph_sb_append` (prefixed runtime calls). Orphan — nothing calls it.
- **`sql_escape_loop(s, pos, len, sb)`** — Uses `sb_append` (unprefixed). Called by `sql_escape`, which is called by ~15 CLI/MCP commands.

`sql_esc_loop` appears to be an earlier draft superseded by `sql_escape_loop`.

### 5.2 `extract_bodies` vs `extract_bodies_acc`

Two row extraction implementations:
- **`extract_bodies(rows, i)`** — Builds result array with cons-style prepend (reversed order).
- **`extract_bodies_acc(rows, i, acc)`** — Accumulator-based, correct order.

Both are called by other functions (`read_fn_defs` uses one, `read_fn_defs_gen` uses the other). The non-acc version produces reversed results, which may or may not matter.

### 5.3 `build_za_fns` vs `build_za_fns_src`

- **`build_za_fns(parsed, i, acc)`** — Works on pre-parsed function records. Called by `build_program`.
- **`build_za_fns_src(sources, i, acc)`** — Works on raw source strings, does its own tokenize+parse. Called by nothing.

`build_za_fns_src` is an earlier version from before `parse_all_fns` existed.

### 5.4 `cmd_check` gen=1 vs gen=2

- **gen=1**: Queries rows, prints count. Does not type-check anything. A stub.
- **gen=2**: Full type checker integration with `mk_engine`, `tc_pre_register`, `tc_infer_all`, `tc_report_errors`.

The gen=1 version is dead code — gen=2 overrides it at compile time. But it's confusing that it exists.

### 5.5 `resolve_record` vs `resolve_record2`

- **`resolve_record(eng, node)`** — Doesn't actually resolve the rest variable (`new_rest = match rest < 0 | true -> rest | _ -> rest` — both branches return `rest` unchanged). Broken.
- **`resolve_record2(eng, fns, n1)`** — Correctly resolves fields and rest variable. Used by `subst_resolve`.

`resolve_record` is broken dead code.

---

## 6. JSON Utilities — Built but Unused

**Status: Functions available, never called from main pipeline**

| Function | Purpose | Called by |
|----------|---------|----------|
| `json_arr_len(pool, idx)` | Get JSON array length | Only `test_json_array` |
| `json_arr_get(pool, idx, i)` | Get JSON array element | Only `test_json_array` |
| `jb_bool(pool, val)` | Build JSON boolean | Nothing |
| `jb_null(pool)` | Build JSON null | Nothing |
| `jt_lbracket` | JSON token: `[` | Nothing beyond its own definition |
| `jt_rbracket` | JSON token: `]` | Nothing beyond its own definition |

The JSON subsystem has a tokenizer, parser, accessors, and builder. The MCP server uses `json_get`, `json_get_str`, `jb_obj`, `jb_str`, `jb_int`, `jb_arr`, `jb_put`. But array access (`json_arr_len`, `json_arr_get`) and some builder functions (`jb_bool`, `jb_null`) are built and tested but never used in production code.

These aren't broken — they're just API surface that was built ahead of need. The MCP server doesn't currently handle JSON arrays in tool arguments or produce boolean/null values in responses.

---

## 7. `cg_struct_typedef` — Exists but Inlined

**Status: Standalone function crashes in Cranelift, workaround inlines it**

`cg_struct_typedef(entry)` generates `typedef struct { ... } Glyph_Name;` for a type definition. However, due to a Cranelift heisenbug, calling it as a separate function crashes in the Cranelift-compiled binary. The workaround inlines the logic directly into `cg_all_typedefs_loop`.

The standalone function exists but is dead — it would crash if called from glyph1 (Cranelift stage).

---

## 8. `compile_to_exe` — Early Prototype

**Status: Orphan from before the build orchestration existed**

```
compile_to_exe src output_path =
  mir = compile_fn(src, [])
  c_code = s3(cg_preamble(), cg_function(mir), "")
  glyph_write_file("/tmp/glyph_out.c", c_code)
  glyph_system(s3("cc /tmp/glyph_out.c -c -o /tmp/glyph_out.o && cc /tmp/glyph_out.o -o ", output_path, ""))
```

Single-function compiler from the early C codegen days. Doesn't include the runtime, doesn't handle multiple functions, doesn't use the struct codegen pipeline. Completely superseded by `build_program`.

---

## 9. `parse_def` and `parse_params` — Thin Wrappers

**Status: Unused indirection layers**

- **`parse_def(src, tokens, pos, pool)`** — One-liner that calls `parse_fn_def`. Called by nothing.
- **`parse_params(tokens, pos, ast)`** — Wrapper around `parse_params_loop`. Called only by `parse_fn_def`, but `parse_fn_def` could call `parse_params_loop` directly.
- **`check_tok(tokens, pos, kind)`** — Token check utility. Called by nothing.
- **`tok_text(src, t)`** — Extract token text from source. Called by nothing.

These appear to be API surface designed for extensibility that was never used.

---

## 10. `tk_error` — Token Kind with No Producer

**Status: Defined but never emitted by the tokenizer**

`tk_error = 100` — an error token kind. The tokenizer (`tokenize`, `tok_one` through `tok_one4`) never produces a token with `kind = tk_error`. On invalid input, the tokenizer either skips the character or produces an identifier token. This constant was defined for the error messages spec but the tokenizer doesn't use it yet.

---

## Summary: What Should Be Connected

| # | Feature | Status | Action |
|---|---------|--------|--------|
| 1 | **Type checker → build** | Exists, fully disconnected | Wire via context-aware inference |
| 2 | **Error reporting** | `format_diagnostic` + helpers exist, unused | Wire into parser errors, build failures, MCP responses |
| 3 | **`ty_forall`** | Constant + constructor, never produced | Implement let-polymorphism generalization |
| 4 | **`ty_named`** | Constant + constructor, never produced | Implement when named types are needed |
| 5 | **`ty_tuple` resolution** | Unification works, `subst_resolve` misses it | Add tuple case to `subst_resolve` |
| 6 | **`rv_make_closure`** | MIR opcode exists, C codegen doesn't handle | Implement closure codegen |
| 7 | **`tk_error`** | Token kind exists, tokenizer doesn't emit | Wire into tokenizer error paths |
| 8 | **`resolve_record`** | Broken (doesn't resolve rest var) | Delete — `resolve_record2` is the working version |
| 9 | **`sql_esc_loop`** | Duplicate of `sql_escape_loop` | Delete |
| 10 | **`build_za_fns_src`** | Superseded by `build_za_fns` | Delete |
| 11 | **`compile_to_exe`** | Early prototype | Delete |
| 12 | **`cmd_check` gen=1** | Stub, overridden by gen=2 | Delete |
| 13 | **`parse_def`**, **`check_tok`**, **`tok_text`** | Unused wrappers | Delete or wire in |
| 14 | **`ok_extern_ref`**, **`tm_switch`** | MIR constants, never emitted | Keep as reserved or delete |
| 15 | **`jb_bool`**, **`jb_null`**, **`json_arr_*`** | Built ahead of need | Keep — useful API surface |
| 16 | **`cg_struct_typedef`** | Crashes in Cranelift, inlined as workaround | Delete (logic lives in `cg_all_typedefs_loop`) |

### Priority grouping

**Wire in (high value):**
1. Type checker → build pipeline (context-aware inference spec)
2. Error reporting → parser + build + MCP
3. Closure codegen
4. `ty_tuple` in `subst_resolve`

**Clean up (low effort):**
5. Delete `resolve_record`, `sql_esc_loop`, `build_za_fns_src`, `compile_to_exe`, `cg_struct_typedef`, `cmd_check` gen=1
6. Delete or document `parse_def`, `check_tok`, `tok_text`

**Keep as-is:**
7. `jb_bool`, `jb_null`, `json_arr_*` — useful API, just not needed yet
8. `ok_extern_ref`, `tm_switch` — reserved for future use
9. `ty_forall`, `ty_named` — needed when the type system grows
