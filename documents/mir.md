# Glyph MIR (Mid-level Intermediate Representation)

**Version:** 0.1 (2026-02-25)

---

## 1. Overview

MIR is the intermediate representation between the AST (parser output) and generated code (C or Cranelift). It is a **flat control-flow graph (CFG)** where each function is decomposed into basic blocks, each block contains a sequence of statements and ends with a terminator.

MIR serves three purposes:
1. **Simplification** — complex expressions (match, let bindings, pipes, closures) are desugared into simple operations
2. **Optimization** — tail-call optimization, field offset resolution, extern call rewriting
3. **Portability** — the same MIR feeds both the Cranelift backend (glyph0) and the C codegen backend (glyph1/glyph)

```
Source → AST → MIR → C code (self-hosted) or Cranelift IR (Rust compiler)
```

---

## 2. Structure

A compiled program is an array of MIR functions. Each function contains:

```
MirFunction =
  fn_name       : S       -- function name
  fn_params     : [I]     -- local IDs for parameters
  fn_locals     : [S]     -- local variable names (parallel to local IDs)
  fn_blocks_stmts : [[Statement]]  -- statements per block
  fn_blocks_terms : [Terminator]   -- terminator per block
  fn_entry      : I       -- entry block ID
```

### Basic Blocks

A basic block is a straight-line sequence of statements followed by exactly one terminator. Control flow only enters at the top and exits at the terminator.

```
bb_0:
  _2 = add(_0, _1)
  goto bb_1

bb_1:
  return _2
```

Blocks are identified by integer IDs (0, 1, 2, ...). The entry block is typically `bb_0`.

### Local Variables

Every value in MIR lives in a numbered local variable `_0`, `_1`, `_2`, etc. Function parameters occupy the first N locals. All other locals are temporaries created during lowering.

Locals are untyped in the self-hosted MIR — everything is a 64-bit integer at runtime. The Rust MIR tracks `MirType` per local.

---

## 3. Operands

An operand is a leaf value — either a constant or a reference to a local.

```
Operand = {okind:I, oval:I, ostr:S}
```

| Kind | Code | Fields | Example |
|------|------|--------|---------|
| Local variable | `ok_local = 1` | `oval` = local ID | `_3` |
| Integer constant | `ok_const_int = 2` | `oval` = value | `42` |
| Boolean constant | `ok_const_bool = 3` | `oval` = 0 or 1 | `true` |
| String constant | `ok_const_str = 4` | `ostr` = value | `"hello"` |
| Unit constant | `ok_const_unit = 5` | — | `()` |
| Function reference | `ok_func_ref = 6` | `ostr` = name | `fn:factorial` |

Constructors:

```
mk_op_int n    = {okind: 2, oval: n, ostr: ""}
mk_op_str s    = {okind: 4, oval: 0, ostr: s}
mk_op_bool b   = {okind: 3, oval: b, ostr: ""}
mk_op_unit     = {okind: 5, oval: 0, ostr: ""}
mk_op_local id = {okind: 1, oval: id, ostr: ""}
mk_op_func nm  = {okind: 6, oval: 0, ostr: nm}
```

---

## 4. Statements

A statement assigns an rvalue to a destination local.

```
Statement = {sdest:I, skind:I, sival:I, sstr:S, sop1:Operand, sop2:Operand, sops:[Operand]}
```

Each statement kind uses the fields differently:

### 4.1 Use (`rv_use = 1`)

Copy one operand to the destination.

```
_2 = use(_0)           -- copy local _0 into _2
_3 = use(42)           -- load constant 42 into _3
```

Fields: `sop1` = source operand.

### 4.2 Binary Operation (`rv_binop = 2`)

```
_3 = add(_0, _1)       -- _3 = _0 + _1
_4 = eq(_2, 0)         -- _4 = (_2 == 0)
```

Fields: `sival` = operator code, `sop1` = left, `sop2` = right.

| Operator | Code | Glyph |
|----------|------|-------|
| `mir_add` | 1 | `+` |
| `mir_sub` | 2 | `-` |
| `mir_mul` | 3 | `*` |
| `mir_div` | 4 | `/` |
| `mir_mod` | 5 | `%` |
| `mir_eq` | 6 | `==` |
| `mir_neq` | 7 | `!=` |
| `mir_lt` | 8 | `<` |
| `mir_gt` | 9 | `>` |
| `mir_lt_eq` | 10 | `<=` |
| `mir_gt_eq` | 11 | `>=` |
| `mir_and` | 12 | `&&` |
| `mir_or` | 13 | `\|\|` |

### 4.3 Unary Operation (`rv_unop = 3`)

```
_2 = neg(_0)            -- _2 = -_0
_3 = not(_1)            -- _3 = !_1
```

Fields: `sival` = operator code, `sop1` = operand.

| Operator | Code | Glyph |
|----------|------|-------|
| `mir_neg` | 20 | `-` (prefix) |
| `mir_not` | 21 | `!` (prefix) |

### 4.4 Call (`rv_call = 4`)

```
_5 = call fn:factorial(42)
_6 = call _3(_0, _1)        -- indirect call through closure
```

Fields: `sop1` = callee operand, `sops` = argument operands.

The callee is either a `ok_func_ref` (direct call) or `ok_local` (indirect/closure call).

### 4.5 Aggregate (`rv_aggregate = 5`)

Construct a compound value.

```
_4 = record{age,name}(_0, _1)     -- record with fields age, name
_5 = array[_0, _1, _2]            -- array literal
_6 = variant:Some(_3)              -- enum variant
_7 = tuple(_0, _1)                 -- tuple
```

Fields: `sival` = aggregate kind, `sstr` = field names (comma-separated for records, variant info for variants), `sops` = element operands.

| Aggregate Kind | Code | Description |
|---------------|------|-------------|
| `ag_tuple` | 1 | Tuple construction |
| `ag_array` | 2 | Array literal |
| `ag_record` | 3 | Record construction |
| `ag_variant` | 4 | Enum variant construction |

For records, field names in `sstr` are comma-separated and sorted alphabetically. Operands in `sops` correspond to fields in that alphabetical order.

For variants, `sstr` encodes `"discriminant,TypeName,VariantName"`.

### 4.6 Field Access (`rv_field = 6`)

```
_2 = _0.name              -- read field 'name' from record _0
```

Fields: `sop1` = base operand, `sival` = field offset (set by `fix_all_field_offsets`, initially 0), `sstr` = field name.

During lowering, `sival` starts at 0 and `sstr` holds the field name. The post-processing pass `fix_all_field_offsets` resolves the actual byte offset by matching the field name against known record types in the type registry.

### 4.7 Index (`rv_index = 7`)

```
_3 = _0[_1]                -- read element at index _1 from array _0
```

Fields: `sop1` = array operand, `sop2` = index operand.

The generated code emits a bounds check before the access.

### 4.8 String Interpolation (`rv_str_interp = 8`)

```
_4 = interp("hello ", _0, "!")
```

Fields: `sops` = alternating string constants and expression operands.

Compiles to `sb_new` → `sb_append` × N → `sb_build` in the generated code.

### 4.9 Make Closure (`rv_make_closure = 9`)

```
_5 = closure(fn:__lambda_0, _2, _3)
```

Fields: `sops` = `[fn_ref, capture1, capture2, ...]`.

Heap-allocates a `{fn_ptr, capture1, capture2, ...}` struct. Only supported by the Cranelift backend; the self-hosted C codegen does not implement this.

---

## 5. Terminators

A terminator ends a basic block and transfers control.

```
Terminator = {tkind:I, top:Operand, tgt1:I, tgt2:I}
```

### 5.1 Goto (`tm_goto = 1`)

Unconditional jump.

```
goto bb_3
```

Fields: `tgt1` = target block ID.

### 5.2 Branch (`tm_branch = 2`)

Conditional branch.

```
branch _4 bb_2 bb_3       -- if _4 then bb_2 else bb_3
```

Fields: `top` = condition operand, `tgt1` = then block, `tgt2` = else block.

The condition is an integer: nonzero = true, zero = false.

### 5.3 Return (`tm_return = 4`)

Return a value from the function.

```
return _2
```

Fields: `top` = return value operand.

### 5.4 Unreachable (`tm_unreachable = 5`)

Marks a block that should never be reached.

```
unreachable
```

Compiles to `__builtin_trap()` in C codegen or a hardware trap in Cranelift. Used as the default terminator for newly created blocks, and should remain for blocks after diverging control flow (panics, infinite loops).

---

## 6. Lowering: AST to MIR

The `lower_fn_def` function transforms an AST function node into a MIR function. It uses a lowering context (`mk_mir_lower`) that tracks:

- `block_stmts` / `block_terms` — parallel arrays of statements and terminators per block
- `cur_block` — which block we're currently emitting into
- `nxt_local` / `nxt_block` — counters for allocating fresh locals and blocks
- `var_names` / `var_locals` / `var_marks` — variable scope stack (name → local ID)
- `fn_params` — parameter local IDs
- `local_types` — type tracking for string operator detection

### 6.1 Expressions

Each expression lowers to an operand (the result value), emitting statements along the way.

| Expression | Lowering |
|-----------|----------|
| `42` | `mk_op_int(42)` — no statements |
| `"hello"` | `mk_op_str("hello")` — no statements |
| `x` | Variable lookup → `mk_op_local(id)` |
| `a + b` | Lower both → emit `rv_binop(add, a_op, b_op)` |
| `f(x, y)` | Lower all → emit `rv_call(f_op, [x_op, y_op])` |
| `r.name` | Lower `r` → emit `rv_field(r_op, "name")` |
| `a[i]` | Lower both → emit `rv_index(a_op, i_op)` |
| `[1, 2, 3]` | Lower elements → emit `rv_aggregate(ag_array, ops)` |
| `{x: 1, y: 2}` | Lower fields → emit `rv_aggregate(ag_record, ops)` |

### 6.2 Let Bindings

```
x = expr
```

Lowers to: evaluate `expr` → allocate local for `x` → emit `rv_use` → bind `x` in scope.

### 6.3 Match Expressions

Match expressions are the most complex lowering. Each arm becomes a chain of blocks:

```
match scrutinee
  pattern1 -> body1
  pattern2 -> body2
  _ -> default
```

Lowers to:

```
bb_0:
  _scr = <scrutinee>
  -- test pattern1
  _cmp1 = eq(_scr, pattern1_val)
  branch _cmp1 bb_match1 bb_next1

bb_match1:
  _result = <body1>
  goto bb_merge

bb_next1:
  -- test pattern2
  _cmp2 = eq(_scr, pattern2_val)
  branch _cmp2 bb_match2 bb_next2

bb_match2:
  _result = <body2>
  goto bb_merge

bb_next2:
  -- wildcard
  _result = <default>
  goto bb_merge

bb_merge:
  return _result
```

Different pattern kinds generate different tests:
- **Integer patterns**: `eq(scrutinee, N)` → branch
- **String patterns**: `call str_eq(scrutinee, "s")` → branch
- **Boolean patterns**: `eq(scrutinee, 1)` or `eq(scrutinee, 0)` → branch
- **Constructor patterns**: extract tag with `rv_field` at offset 0, compare discriminant, then bind payload fields
- **Or-patterns**: chain of tests for each alternative, any match jumps to the body
- **Wildcard/ident**: unconditional match (no test needed)

### 6.4 For Loops

```
for x in arr
  body
```

Desugars to an index-based loop:

```
bb_header:
  _i = use(0)
  _len = call array_len(arr)
  goto bb_check

bb_check:
  _cond = lt(_i, _len)
  branch _cond bb_body bb_exit

bb_body:
  _x = arr[_i]
  <body>
  _i2 = add(_i, 1)
  _i := _i2     -- via raw_set or re-bind
  goto bb_check

bb_exit:
  ...
```

### 6.5 Pipe and Compose

```
x |> f          -- lowers to: f(x)
f >> g          -- lowers to: \x -> g(f(x))
```

These are syntactic sugar — they lower to regular calls.

### 6.6 String Interpolation

```
"hello {name}, age {int_to_str(age)}"
```

Lowers to `rv_str_interp` with operands for each part:

```
_5 = interp("hello ", _name, ", age ", _age_str)
```

The codegen expands this to `sb_new` → `sb_append` × N → `sb_build`.

### 6.7 Error Propagation (`?`)

```
result?
```

Lowers to: extract tag → branch on tag=0 (Ok) vs tag=1 (Err) → Ok path extracts payload and continues → Err path returns the error immediately.

---

## 7. Post-Processing Passes

After all functions are lowered, several passes transform the MIR:

### 7.1 Field Offset Resolution (`fix_all_field_offsets`)

The lowering emits `rv_field` with `sival=0` and the field name in `sstr`. This pass:

1. Builds a **type registry** from all `rv_aggregate/ag_record` statements — collects every distinct set of field names
2. Collects **per-local field accesses** — for each local, what field names are accessed on it
3. For each `rv_field` statement, uses `find_best_type` to match the local's accessed fields against the registry, then sets `sival` to the correct byte offset

This is necessary because the self-hosted MIR has no type information — field names are all we have.

### 7.2 Extern Call Rewriting (`fix_extern_calls`)

Renames function references from user-declared names to `glyph_`-prefixed names for extern functions. E.g., `ok_func_ref("db_open")` → `ok_func_ref("glyph_db_open")`.

### 7.3 Tail-Call Optimization (`tco_optimize`)

Detects self-recursive tail calls and rewrites them to loops:

```
-- Before:
bb_N:
  _r = call fn:factorial(_n2, _acc2)
  return _r

-- After:
bb_N:
  _0 := _n2    -- reassign params
  _1 := _acc2
  goto bb_0    -- jump to entry
```

---

## 8. C Code Generation from MIR

Each MIR construct maps to C:

### Operands
| MIR | C |
|-----|---|
| `ok_local(_3)` | `_3` |
| `ok_const_int(42)` | `42LL` |
| `ok_const_bool(1)` | `1` |
| `ok_const_str("hi")` | `(long long)glyph_cstr_to_str("hi")` |
| `ok_const_unit` | `0LL` |
| `ok_func_ref("f")` | `f` (as function name in call) |

### Statements
| MIR | C |
|-----|---|
| `_2 = use(_0)` | `_2 = _0;` |
| `_3 = add(_0, _1)` | `_3 = _0 + _1;` |
| `_4 = call f(_0, _1)` | `_4 = f(_0, _1);` |
| `_5 = _0.name` | `_5 = ((long long*)_0)[3];` (gen=1, offset 3) |
| `_5 = _0.name` | `_5 = ((Glyph_Person*)_0)->name;` (gen=2, struct) |
| `_6 = _0[_1]` | `{ glyph_array_bounds_check(_1, ((long long*)_0)[1]); _6 = ((long long*)((long long*)_0)[0])[_1]; }` |
| `_7 = record{a,b}(x,y)` | `{ long long* __r = (long long*)glyph_alloc(16); __r[0]=x; __r[1]=y; _7=(long long)__r; }` |

### Terminators
| MIR | C |
|-----|---|
| `goto bb_3` | `goto bb_3;` |
| `branch _4 bb_2 bb_3` | `if (_4) goto bb_2; else goto bb_3;` |
| `return _2` | `return _2;` |
| `unreachable` | `__builtin_trap();` |

### Function Structure

```c
long long factorial(long long _0, long long _1) {
#ifdef GLYPH_DEBUG
  _glyph_current_fn = "factorial";
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "factorial";
  _glyph_call_depth++;
#endif
  long long _2 = 0;
  long long _3 = 0;
bb_0:
  _2 = _0 <= 1LL;
  if (_2) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _3;
bb_2:
  _3 = _1;
  goto bb_1;
bb_3:
  _3 = _0 * _1;
  _0 = _0 - 1LL;
  goto bb_0;    // TCO: tail call became loop
}
```

---

## 9. Pretty Printing

The self-hosted compiler includes a full MIR pretty-printer (`mir_pp_fn`) accessible via `glyph build <db> --emit-mir`. Output format:

```
fn factorial(_0:n, _1:acc) {
  bb0:
    _2 = lte(_0, 1)
    branch _2 bb2 bb3
  bb1:
    return _3
  bb2:
    _3 = use(_1)
    goto bb1
  bb3:
    _4 = mul(_0, _1)
    _5 = sub(_0, 1)
    _3 = call fn:factorial(_5, _4)
    return _3
}
```

---

## 10. Differences: Rust MIR vs Self-Hosted MIR

| Aspect | Rust (`glyph-mir`) | Self-hosted (glyph.glyph) |
|--------|-------------------|---------------------------|
| **Representation** | Typed enums (`Rvalue`, `Operand`, `Terminator`) | Records with integer tags (`skind`, `okind`, `tkind`) |
| **Type tracking** | `MirType` per local | `local_types` parallel array (partial, for string ops) |
| **Closures** | `MakeClosure` rvalue | Tag exists (`rv_make_closure = 9`) but codegen not implemented |
| **Ref/Deref** | `Rvalue::Ref`, `Rvalue::Deref` | Not implemented |
| **Cast** | `Rvalue::Cast(op, ty)` | Not implemented |
| **Switch** | `Terminator::Switch(op, arms, default)` | Not implemented (uses chained branches instead) |
| **Serialization** | serde JSON cache in `compiled` table | Not cached (recompiled each build) |
| **Field offsets** | Resolved during lowering (type info available) | Post-processed by `fix_all_field_offsets` (heuristic matching) |
