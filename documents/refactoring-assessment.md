# Refactoring Assessment: Glyph Self-Hosted Compiler

**Date:** 2026-03-02
**Scope:** Evaluating refactoring opportunities in `glyph.glyph` (~1,026 definitions, ~303k characters) using language features implemented since v0.1.

---

## Features Available for Refactoring

| Feature | Syntax | Status |
|---------|--------|--------|
| Match guards | `pat ? guard -> body` | Implemented |
| Or-patterns | `1 \| 2 \| 3 -> body` | Implemented |
| Let destructuring | `{x, y} = expr` | Implemented |
| Closures / lambdas | `\x -> expr` | Implemented |
| String interpolation | `"text {expr}"` | Implemented |
| Type-aware interpolation | `"{n}"` auto-coerces int/float | Implemented |

---

## 1. Match Guards — Highest Impact

### The Problem

The self-hosted compiler has no `if-else` — every boolean conditional is a nested match:

```
match cond
  true -> then_branch
  _ -> else_branch
```

When multiple conditions are tested sequentially, this produces deeply nested chains:

```
match k == X()
  true -> handle_X
  _ -> match k == Y()
    true -> handle_Y
    _ -> match k == Z()
      true -> handle_Z
      _ -> default
```

### Scale

- **121 functions** contain the `true -> ... _ -> match` nesting pattern
- **29,407 characters** across functions with `match k ==` chains (10% of codebase)
- **30 functions** have 4+ levels of match nesting for kind-dispatch alone

### Top Candidates

| Function | Size (chars) | Nesting depth | Description |
|----------|-------------|---------------|-------------|
| `parse_atom` | 3,884 | 14 | Token-to-AST dispatch |
| `parse_match_arms` | 2,235 | 8 | Pattern kind dispatch |
| `parse_postfix_loop` | 2,108 | 6 | Postfix operator dispatch |
| `parse_single_pattern` | 1,567 | 6 | Pattern parsing |
| `cg_stmt2` | 1,092 | 9 | Gen-2 codegen dispatch |
| `cg_stmt` | 1,047 | 9 | Codegen statement dispatch |
| `lower_expr` | 874 | 9 | MIR expression lowering |
| `lower_expr2` | 748 | 8 | MIR expression lowering (cont.) |
| `infer_expr_core` | 754 | 8 | Type inference dispatch |
| `walk_free_vars` | 802 | 5 | Closure analysis dispatch |
| `parse_cmp_loop` | 772 | 6 | Comparison operator dispatch |
| `cg_operand` | 674 | 7 | Operand rendering |
| `cg_term` | 453 | 4 | Terminator rendering |

### Example: `parse_cmp_loop`

**Before** (772 chars, 6 nesting levels):
```
parse_cmp_loop src tokens pos pool left =
  k = cur_kind(tokens, pos)
  op = match k == tk_eq_eq()
    true -> op_eq()
    _ -> match k == tk_bang_eq()
      true -> op_neq()
      _ -> match k == tk_lt()
        true -> op_lt()
        _ -> match k == tk_gt()
          true -> op_gt()
          _ -> match k == tk_lt_eq()
            true -> op_lt_eq()
            _ -> match k == tk_gt_eq()
              true -> op_gt_eq()
              _ -> 0 - 1
  ...
```

**After** (flat, ~500 chars):
```
parse_cmp_loop src tokens pos pool left =
  k = cur_kind(tokens, pos)
  op = match k
    _ ? k == tk_eq_eq() -> op_eq()
    _ ? k == tk_bang_eq() -> op_neq()
    _ ? k == tk_lt() -> op_lt()
    _ ? k == tk_gt() -> op_gt()
    _ ? k == tk_lt_eq() -> op_lt_eq()
    _ ? k == tk_gt_eq() -> op_gt_eq()
    _ -> 0 - 1
  ...
```

### Example: `is_runtime_fn` chain

The `is_runtime_fn` → `is_runtime_fn9` chain spans **9 functions, 3,584 characters** total, testing ~36 string names with nested matches. With guards, this collapses to 2-3 functions:

**Before** (9 functions, each 8 nesting levels):
```
is_runtime_fn name =
  match glyph_str_eq(name, "println")
    true -> 1
    _ -> match glyph_str_eq(name, "eprintln")
      true -> 1
      _ -> match glyph_str_eq(name, "exit")
        true -> 1
        _ -> ... (8 deep, chains to is_runtime_fn2)
```

**After** (2-3 functions, flat):
```
is_runtime_fn name =
  match name
    _ ? glyph_str_eq(name, "println") -> 1
    _ ? glyph_str_eq(name, "eprintln") -> 1
    _ ? glyph_str_eq(name, "exit") -> 1
    _ ? glyph_str_eq(name, "str_len") -> 1
    _ ? glyph_str_eq(name, "str_char_at") -> 1
    _ ? glyph_str_eq(name, "str_slice") -> 1
    _ ? glyph_str_eq(name, "str_concat") -> 1
    _ ? glyph_str_eq(name, "str_eq") -> 1
    _ ? glyph_str_eq(name, "int_to_str") -> 1
    _ ? glyph_str_eq(name, "str_to_int") -> 1
    _ ? glyph_str_eq(name, "array_push") -> 1
    _ ? glyph_str_eq(name, "array_len") -> 1
    _ -> is_runtime_fn2(name)
```

Same pattern applies to `dispatch_cmd` → `dispatch_cmd5` (5 functions, 2,413 chars, testing 19 command strings).

### Estimated Impact

- **~35% character reduction** in the 30 most-nested dispatch functions
- **Eliminates ~7 continuation functions** (`is_runtime_fn3`-`fn9` merge into 2, `dispatch_cmd3`-`cmd5` merge into 2)
- **~8,000-10,000 characters saved** across the 121 affected functions

---

## 2. String Interpolation — High Impact

### The Problem

C codegen functions build output strings using `s2()`-`s6()` helper functions that concatenate 2-6 arguments:

```
s6("  ", cg_local(stmt.sdest), " = ", cg_operand(stmt.sop1), cg_binop_str(stmt.sival), s2(cg_operand(stmt.sop2), ";\n"))
```

This is hard to read and requires nesting `s2()` inside `s6()` for >6 parts.

### Scale

- **161 functions** use `s2()`-`s6()` string builders
- **80,354 characters** across those functions (27% of codebase)
- **15 functions** use `s4()` or higher (most complex string assembly)

### Top Candidates

| Function | Size (chars) | `sN()` calls | Description |
|----------|-------------|-------------|-------------|
| `cg_stmt` | 1,047 | 6 | Statement codegen |
| `cg_stmt2` | 1,092 | 8 | Gen-2 statement codegen |
| `cg_term` | 453 | 3 | Terminator codegen |
| `cg_operand` | 674 | 4 | Operand rendering |
| `cg_field_stmt2` | 914 | 4 | Gen-2 field access codegen |
| `cg_runtime_float` | 853 | 3 | Float runtime C code |
| `build_program` | 1,213 | 5 | Build pipeline |
| `cmd_stat` | 855 | 5 | Statistics command |
| `cmd_history` | 996 | 3 | History command |

### Example: `cg_term`

**Before** (453 chars):
```
cg_term term =
  k = term.tkind
  match k == tm_goto()
    true -> s3("  goto ", cg_label(term.tgt1), ";\n")
    _ -> match k == tm_return()
      true -> s3("#ifdef GLYPH_DEBUG\n  _glyph_call_depth--;\n#endif\n  return ", cg_operand(term.top), ";\n")
      _ -> match k == tm_branch()
        true -> s6("  if (", cg_operand(term.top), ") goto ", cg_label(term.tgt1), "; else goto ", s2(cg_label(term.tgt2), ";\n"))
        _ -> "  __builtin_trap();\n"
```

**After** (guards + interpolation, ~320 chars):
```
cg_term term =
  k = term.tkind
  match k
    _ ? k == tm_goto() -> "  goto {cg_label(term.tgt1)};\n"
    _ ? k == tm_return() -> "#ifdef GLYPH_DEBUG\n  _glyph_call_depth--;\n#endif\n  return {cg_operand(term.top)};\n"
    _ ? k == tm_branch() -> "  if ({cg_operand(term.top)}) goto {cg_label(term.tgt1)}; else goto {cg_label(term.tgt2)};\n"
    _ -> "  __builtin_trap();\n"
```

### Example: `cg_operand`

**Before** (674 chars, 7 nesting levels, s3/s2 nesting):
```
cg_operand op =
  k = op.okind
  gv = gval_t()
  match k == ok_local()
    true -> cg_local(op.oval)
    _ -> match k == ok_const_int()
      true -> s3("(", gv, s2(")", itos(op.oval)))
      _ -> match k == ok_const_bool()
        true -> match op.oval == 0
          true -> "0"
          _ -> "1"
        _ -> match k == ok_const_unit()
          true -> "0"
          _ -> match k == ok_const_str()
            true -> cg_str_literal(op.ostr)
            _ -> match k == ok_func_ref()
              true -> s3("(", gv, s2(")&", op.ostr))
              _ -> match k == ok_const_float()
                true -> s2("_glyph_f2i(", s2(op.ostr, ")"))
                _ -> "0"
```

**After** (guards + interpolation, ~430 chars):
```
cg_operand op =
  k = op.okind
  gv = gval_t()
  match k
    _ ? k == ok_local() -> cg_local(op.oval)
    _ ? k == ok_const_int() -> "({gv}){itos(op.oval)}"
    _ ? k == ok_const_bool() -> match op.oval == 0
      true -> "0"
      _ -> "1"
    _ ? k == ok_const_unit() -> "0"
    _ ? k == ok_const_str() -> cg_str_literal(op.ostr)
    _ ? k == ok_func_ref() -> "({gv})&{op.ostr}"
    _ ? k == ok_const_float() -> "_glyph_f2i({op.ostr})"
    _ -> "0"
```

### Estimated Impact

- **~25% character reduction** in the 161 affected functions
- **Eliminates nested `s2()` workarounds** for >6-part strings
- **Dramatically improves readability** of codegen output — the C code being generated becomes visible in the template
- **~12,000-15,000 characters saved** across affected functions

### Limitation

String interpolation uses `{` which triggers interpolation parsing. C code containing braces must use `\{` and `\}` (already the case with `cg_lbrace()`/`cg_rbrace()` helpers). Runtime C code strings (`cg_runtime_c`, `cg_runtime_io`, etc.) have extensive C braces and are **poor candidates** for interpolation — the `\{`/`\}` escaping would negate readability gains.

Best candidates are **single-line format templates** like `cg_term`, `cg_operand`, `cg_stmt` where the interpolated parts are Glyph function calls, not C blocks.

---

## 3. Or-Patterns — Medium Impact

### The Problem

Multiple match arms with identical (or nearly identical) bodies:

```
match k == ex_int_lit()
  true -> 0
  _ -> match k == ex_str_lit()
    true -> 0
    _ -> match k == ex_bool_lit()
      true -> 0
      _ -> ...
```

### Scale

Primarily affects dispatch functions that skip over literal kinds. Less pervasive than guard opportunities, but high per-instance savings.

### Top Candidates

| Function | Pattern | Arms combinable |
|----------|---------|----------------|
| `walk_free_vars` | Literal kinds → 0 | 3 arms |
| `parse_postfix_loop` | `tk_ident` / `tk_type_ident` → same body | 2 arms |
| `infer_expr_core` | `ex_str_lit` / `ex_str_interp` → `mk_tstr` | 2 arms |

### Example: `walk_free_vars`

**Before:**
```
_ -> match k == ex_int_lit()
  true -> 0
  _ -> match k == ex_str_lit()
    true -> 0
    _ -> match k == ex_bool_lit()
      true -> 0
      _ -> walk_free_vars2(...)
```

**After:**
```
_ ? k == ex_int_lit() | k == ex_str_lit() | k == ex_bool_lit() -> 0
_ -> walk_free_vars2(...)
```

### Estimated Impact

- **~10-15 functions** benefit from or-pattern merging
- **~1,500-2,500 characters saved**
- Combines well with guards for maximum flattening

---

## 4. Let Destructuring — Medium Impact

### The Problem

Parse results and AST node fields are unpacked line by line:

```
er = parse_expr(src, tokens, pos, pool)
match is_err(er)
  true -> er
  _ ->
    rhs = parse_expr(src, tokens, er.pos + 1, pool)
    match is_err(rhs)
      true -> rhs
      _ ->
        idx = pool_push(pool, mk_node(st_assign(), 0, "", er.node, rhs.node, 0 - 1, []))
        mk_result(idx, rhs.pos)
```

### Scale

- **37 functions** unpack `{node, pos}` results from parse calls
- **39 functions** access both `.node` and `.pos` on records

### Application

Destructuring is most useful for the `{node, pos}` pattern in parser functions, and `{okind, oval, ostr}` in codegen. However, most current code accesses fields directly on the result (e.g., `er.pos`, `er.node`) rather than binding to separate locals — meaning destructuring would only save lines when a function binds **both** fields to named locals.

### Example: `parse_stmt_expr` (the new refactored function)

**Before:**
```
enode = pool[er.node]
match enode.kind == ex_ident()
  true ->
    -- accesses enode.sval
```

**After:**
```
{kind, sval} = pool[er.node]
match kind == ex_ident()
  true ->
    -- uses sval directly
```

### Estimated Impact

- **~2,000-3,000 characters saved** across 37 functions
- Modest per-function savings (1-2 lines each)
- Primary value is **readability** rather than token reduction

---

## 5. Combined Refactoring — Guards + Interpolation + Or-Patterns

The highest-impact refactoring combines multiple features on the same functions. Many codegen functions suffer from **both** deep nesting **and** `sN()` string builders.

### Example: `cg_stmt` (all three features)

**Before** (1,047 chars, 9 nesting levels):
```
cg_stmt stmt =
  k = stmt.skind
  match k == rv_use()
    true -> s4("  ", cg_local(stmt.sdest), " = ", s2(cg_operand(stmt.sop1), ";\n"))
    _ -> match k == rv_binop()
      true -> s6("  ", cg_local(stmt.sdest), " = ", cg_operand(stmt.sop1), cg_binop_str(stmt.sival), s2(cg_operand(stmt.sop2), ";\n"))
      _ -> match k == rv_unop()
        true -> s5("  ", cg_local(stmt.sdest), " = ", cg_unop_str(stmt.sival), s2(cg_operand(stmt.sop1), ";\n"))
        _ -> match k == rv_call()
          true -> cg_call_stmt(stmt)
          _ -> match k == rv_field()
            true -> cg_field_stmt(stmt)
            _ -> match k == rv_index()
              true -> cg_index_stmt(stmt)
              _ -> match k == rv_aggregate()
                true -> cg_aggregate_stmt(stmt)
                _ -> match k == rv_str_interp()
                  true -> cg_str_interp_stmt(stmt)
                  _ -> match k == rv_make_closure()
                    true -> cg_closure_stmt(stmt)
                    _ -> s3("  /* unknown rvalue kind ", itos(k), " */\n")
```

**After** (~620 chars, flat):
```
cg_stmt stmt =
  k = stmt.skind
  d = cg_local(stmt.sdest)
  match k
    _ ? k == rv_use() -> "  {d} = {cg_operand(stmt.sop1)};\n"
    _ ? k == rv_binop() -> "  {d} = {cg_operand(stmt.sop1)}{cg_binop_str(stmt.sival)}{cg_operand(stmt.sop2)};\n"
    _ ? k == rv_unop() -> "  {d} = {cg_unop_str(stmt.sival)}{cg_operand(stmt.sop1)};\n"
    _ ? k == rv_call() -> cg_call_stmt(stmt)
    _ ? k == rv_field() -> cg_field_stmt(stmt)
    _ ? k == rv_index() -> cg_index_stmt(stmt)
    _ ? k == rv_aggregate() -> cg_aggregate_stmt(stmt)
    _ ? k == rv_str_interp() -> cg_str_interp_stmt(stmt)
    _ ? k == rv_make_closure() -> cg_closure_stmt(stmt)
    _ -> "  /* unknown rvalue kind {itos(k)} */\n"
```

**Savings: ~40% reduction (1,047 → ~620 chars)**

---

## 6. Function Consolidation Opportunities

Guards eliminate the need for continuation-chain functions that exist solely because match nesting gets too deep.

| Chain | Functions | Total chars | After guards |
|-------|-----------|-------------|-------------|
| `is_runtime_fn` → `fn9` | 9 | 3,584 | 2-3 functions |
| `dispatch_cmd` → `cmd5` | 5 | 2,413 | 2 functions |
| `lower_expr` → `expr3` | 3 | 1,622 | 1-2 functions |
| `walk_free_vars` → `vars4` | 4 | 3,257 | 2 functions |
| `infer_expr_core` → `expr3` | 3 | 1,208+ | 1-2 functions |

**Total: ~24 functions → ~10 functions** (14 eliminated, ~5,000 chars saved)

---

## Token Impact Analysis

Glyph programs are measured by BPE token count. Character savings translate roughly to proportional token savings (Glyph syntax is ASCII-dense, ~5-6 chars per token).

| Category | Functions affected | Chars before | Est. chars after | Savings |
|----------|-------------------|-------------|-----------------|---------|
| Guard flattening | 121 | ~29,400 | ~20,500 | ~8,900 (30%) |
| String interpolation | 161 (subset) | ~80,350 | ~66,300 | ~14,050 (17%) |
| Or-pattern merging | ~15 | ~3,500 | ~2,000 | ~1,500 (43%) |
| Let destructuring | ~37 | ~8,000 | ~6,000 | ~2,000 (25%) |
| Function consolidation | ~24 → ~10 | ~12,000 | ~7,000 | ~5,000 (42%) |
| **Total** | | **~303k** | **~272k** | **~31,450 (10%)** |

### Per-Token Breakdown

At ~5.5 chars/token average:
- **~5,700 tokens saved** across the codebase
- From ~55,000 estimated tokens to ~49,300
- **~10% token reduction** with pure syntactic refactoring

### Readability Improvement

Token savings understate the true impact. The refactoring:
- Reduces maximum nesting depth from **14 levels** to **2-3 levels** in dispatch functions
- Makes C codegen output **visible as templates** instead of buried in `sN()` calls
- Eliminates **14 continuation functions** that exist only for nesting management
- Reduces the total definition count from ~1,026 to ~1,012

---

## Recommended Refactoring Order

### Phase 1: Guards (highest ROI, lowest risk)

1. **Kind-dispatch functions** (30 functions): `lower_expr/2/3`, `infer_expr_core/2/3`, `walk_free_vars/2/3/4`, `cg_stmt/2`, `cg_operand`, `parse_atom`
2. **String-equality chains** (14 functions): `is_runtime_fn`→`fn9`, `dispatch_cmd`→`cmd5`
3. **Operator dispatch** (4 functions): `parse_cmp_loop`, `parse_mul_loop`, `parse_add_loop`, `parse_bitwise_loop`

### Phase 2: String interpolation (high ROI, medium risk)

1. **Format-template functions** (codegen output): `cg_term`, `cg_operand`, `cg_stmt`, `cg_call_stmt`, `cg_field_stmt`
2. **CLI output functions**: `cmd_stat`, `cmd_history`, `cmd_ls`
3. **MIR pretty-printing**: `mir_pp_stmt`, `mir_pp_term`, `mir_pp_op`

Avoid: `cg_runtime_c`, `cg_runtime_io` and other C runtime string constants (too many braces).

### Phase 3: Or-patterns + destructuring (medium ROI, low risk)

1. **Literal-skip patterns**: `walk_free_vars` literal kind checks
2. **Dual-branch patterns**: `parse_postfix_loop` ident/type_ident
3. **Parse result unpacking**: Parser functions with `{node, pos}` bindings

### Risk Assessment

- **Guards**: Low risk. Pure syntax change, no semantic difference. Each function can be refactored independently and tested.
- **String interpolation**: Medium risk. Must verify that `{` in interpolated strings doesn't conflict with C code generation. Test each function after conversion.
- **Or-patterns**: Low risk. Existing feature, well-tested.
- **Destructuring**: Low risk. Just implemented, verified by test suite.
- **Function consolidation**: Medium risk. Merging continuation chains changes the call graph. Test thoroughly after each merge.

### Testing Strategy

Each refactored function should be tested by:
1. `./glyph test glyph.glyph --gen 2` — full self-hosted test suite (96 tests)
2. `ninja` — bootstrap verification (ensures the compiler can still compile itself)
3. Manual smoke test of affected subsystem (e.g., `./glyph build examples/calculator/calc.glyph` for codegen changes)

---

## Conclusion

The self-hosted compiler has accumulated significant syntactic debt from working within the constraints of v0.1's limited feature set. With guards, interpolation, or-patterns, and destructuring now available, a systematic refactoring pass would:

- **Remove ~31,000 characters** (~10% of codebase)
- **Eliminate ~14 functions** (continuation chains)
- **Reduce maximum nesting** from 14 levels to 2-3
- **Make codegen templates readable** as C-like format strings

The guard feature alone accounts for ~28% of the total savings and should be prioritized. Combined with string interpolation on codegen functions, the two features address 73% of the refactoring opportunity.
