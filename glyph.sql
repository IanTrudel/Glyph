-- fn add_type_to_reg
add_type_to_reg reg names i =
  match i >= glyph_array_len(reg)
    true ->
      sorted = sort_str_arr(names)
      glyph_array_push(reg, sorted)
      0
    _ ->
      existing = reg[i]
      match types_equal(existing, names)
        true -> 0
        _ -> add_type_to_reg(reg, names, i + 1)

-- fn add_types_to_reg
add_types_to_reg mirs reg =
  build_type_reg_mirs(mirs, 0, reg)
  reg

-- fn ag_array
ag_array = 2

-- fn ag_record
ag_record = 3

-- fn ag_tuple
ag_tuple = 1

-- fn ag_variant
ag_variant = 4

-- fn all_fields_in
all_fields_in type_fields local_fields i =
  match i >= glyph_array_len(local_fields)
    true -> 1
    _ -> match has_str_in(type_fields, local_fields[i])
      true -> all_fields_in(type_fields, local_fields, i + 1)
      _ -> 0

-- fn alpha_rank
alpha_rank names target =
  alpha_rank_loop(names, target, 0, 0)

-- fn alpha_rank_loop
alpha_rank_loop names target i acc =
  match i >= glyph_array_len(names)
    true -> acc
    _ ->
      match str_lt(names[i], target)
        true -> alpha_rank_loop(names, target, i + 1, acc + 1)
        _ -> alpha_rank_loop(names, target, i + 1, acc)

-- fn apply_args
apply_args eng ast fn_ty args i =
  match i >= glyph_array_len(args)
    true -> fn_ty
    _ ->
      arg_ty = infer_expr(eng, ast, args[i])
      ret = subst_fresh(eng)
      expected_fn = mk_tfn(arg_ty, ret, eng.ty_pool)
      unify(eng, fn_ty, expected_fn)
      resolved_ret = subst_walk(eng, ret)
      apply_args(eng, ast, resolved_ret, args, i + 1)

-- fn apply_fn
apply_fn f x = f(x)


-- fn bdec
bdec bd =
  match bd > 0
    true -> bd - 1
    _ -> 0

-- fn bind_ctor_fields
bind_ctor_fields ctx ast sub_pats scrutinee i =
  match i >= glyph_array_len(sub_pats)
    true -> 0
    _ ->
      sp = ast[sub_pats[i]]
      match sp.kind == pat_ident()
        true ->
          field_local = mir_alloc_local(ctx, sp.sval)
          mir_emit_field(ctx, field_local, scrutinee, i + 1, s2("__payload", itos(i)))
          mir_bind_var(ctx, sp.sval, field_local)
          bind_ctor_fields(ctx, ast, sub_pats, scrutinee, i + 1)
        _ -> bind_ctor_fields(ctx, ast, sub_pats, scrutinee, i + 1)

-- fn binop_arith
binop_arith eng lt rt =
  match lt == ty_int()
    true -> match rt == ty_int()
      true -> mk_tint(eng.ty_pool)
      _ -> 0 - 1
    _ -> match lt == ty_float()
      true -> match rt == ty_float()
        true -> mk_ty_prim(ty_float(), eng.ty_pool)
        _ -> 0 - 1
      _ -> match lt == ty_str()
        true -> match rt == ty_str()
          true -> mk_tstr(eng.ty_pool)
          _ -> 0 - 1
        _ -> 0 - 1

-- fn binop_arith_num
binop_arith_num eng lt rt =
  match lt == ty_int()
    true -> match rt == ty_int()
      true -> mk_tint(eng.ty_pool)
      _ -> 0 - 1
    _ -> match lt == ty_float()
      true -> match rt == ty_float()
        true -> mk_ty_prim(ty_float(), eng.ty_pool)
        _ -> 0 - 1
      _ -> 0 - 1

-- fn binop_type
binop_type eng op left right =
  lt = ty_tag(eng.ty_pool, left)
  rt = ty_tag(eng.ty_pool, right)
  match op == op_add()
    true -> binop_arith(eng, lt, rt)
    _ -> match op == op_sub()
      true -> binop_arith_num(eng, lt, rt)
      _ -> match op == op_mul()
        true -> binop_arith_num(eng, lt, rt)
        _ -> match op == op_div()
          true -> binop_arith_num(eng, lt, rt)
          _ -> match op == op_mod()
            true -> binop_arith_num(eng, lt, rt)
            _ -> match op == op_eq()
              true -> mk_tbool(eng.ty_pool)
              _ -> match op == op_neq()
                true -> mk_tbool(eng.ty_pool)
                _ -> match op == op_lt()
                  true -> mk_tbool(eng.ty_pool)
                  _ -> match op == op_gt()
                    true -> mk_tbool(eng.ty_pool)
                    _ -> match op == op_lt_eq()
                      true -> mk_tbool(eng.ty_pool)
                      _ -> match op == op_gt_eq()
                        true -> mk_tbool(eng.ty_pool)
                        _ -> match op == op_and()
                          true -> mk_tbool(eng.ty_pool)
                          _ -> match op == op_or()
                            true -> mk_tbool(eng.ty_pool)
                            _ -> binop_type2(eng, op)

-- fn binop_type2
binop_type2 eng op =
  match op == op_bitand()
    true -> mk_tint(eng.ty_pool)
    _ -> match op == op_bitor()
      true -> mk_tint(eng.ty_pool)
      _ -> match op == op_bitxor()
        true -> mk_tint(eng.ty_pool)
        _ -> match op == op_shl()
          true -> mk_tint(eng.ty_pool)
          _ -> match op == op_shr()
            true -> mk_tint(eng.ty_pool)
            _ -> 0 - 1

-- fn blt_all_in
blt_all_in arr fields i =
  match i >= glyph_array_len(arr)
    true -> true
    _ ->
      match blt_has_field(fields, arr[i], 0)
        true -> blt_all_in(arr, fields, i + 1)
        _ -> false

-- fn blt_collect_fa_blks
blt_collect_fa_blks blocks i fa =
  match i >= glyph_array_len(blocks)
    true -> 0
    _ ->
      blt_collect_fa_stmts(blocks[i], 0, fa)
      blt_collect_fa_blks(blocks, i + 1, fa)

-- fn blt_collect_fa_stmts
blt_collect_fa_stmts stmts i fa =
  match i >= glyph_array_len(stmts)
    true -> 0
    _ ->
      stmt = stmts[i]
      match stmt.skind == rv_field()
        true ->
          match stmt.sop1.okind == ok_local()
            true ->
              loc = stmt.sop1.oval
              match loc >= 0
                true ->
                  match loc < glyph_array_len(fa)
                    true ->
                      glyph_array_push(fa[loc], stmt.sstr)
                      0
                    _ -> 0
                _ -> 0
            _ -> 0
        _ -> 0
      blt_collect_fa_stmts(stmts, i + 1, fa)

-- fn blt_find_best
blt_find_best accessed struct_map i best_name best_count =
  match i >= glyph_array_len(struct_map)
    true -> best_name
    _ ->
      entry = struct_map[i]
      ename = entry[0]
      efields = entry[1]
      fcount = glyph_array_len(efields)
      match blt_all_in(accessed, efields, 0)
        true ->
          match glyph_str_len(best_name) == 0
            true -> blt_find_best(accessed, struct_map, i + 1, ename, fcount)
            _ -> blt_find_best(accessed, struct_map, i + 1, "", 0)
        _ -> blt_find_best(accessed, struct_map, i + 1, best_name, best_count)

-- fn blt_find_struct
blt_find_struct accessed struct_map i =
  blt_find_best(accessed, struct_map, 0, "", 0)

-- fn blt_has_field
blt_has_field fields name i =
  match i >= glyph_array_len(fields)
    true -> false
    _ ->
      match glyph_str_eq(fields[i], name)
        true -> true
        _ -> blt_has_field(fields, name, i + 1)

-- fn blt_init
blt_init n i =
  match i >= n
    true -> []
    _ ->
      rest = blt_init(n, i + 1)
      glyph_array_push(rest, "")
      rest

-- fn blt_init_fa
blt_init_fa n i =
  match i >= n
    true -> []
    _ ->
      rest = blt_init_fa(n, i + 1)
      glyph_array_push(rest, [])
      rest

-- fn blt_propagate_blks
blt_propagate_blks blocks i lst =
  match i >= glyph_array_len(blocks)
    true -> 0
    _ ->
      blt_propagate_stmts(blocks[i], 0, lst)
      blt_propagate_blks(blocks, i + 1, lst)

-- fn blt_propagate_stmts
blt_propagate_stmts stmts i lst =
  match i >= glyph_array_len(stmts)
    true -> 0
    _ ->
      stmt = stmts[i]
      match stmt.skind == rv_use()
        true ->
          match stmt.sop1.okind == ok_local()
            true ->
              src = stmt.sop1.oval
              match src >= 0
                true ->
                  match src < glyph_array_len(lst)
                    true ->
                      src_type = lst[src]
                      match glyph_str_len(src_type) > 0
                        true ->
                          match glyph_str_len(lst[stmt.sdest]) == 0
                            true -> glyph_array_set(lst, stmt.sdest, src_type)
                            _ -> 0
                        _ -> 0
                    _ -> 0
                _ -> 0
            _ -> 0
        _ -> 0
      blt_propagate_stmts(stmts, i + 1, lst)

-- fn blt_scan_blks
blt_scan_blks blocks i lst struct_map =
  match i >= glyph_array_len(blocks)
    true -> 0
    _ ->
      blt_scan_stmts(blocks[i], 0, lst, struct_map)
      blt_scan_blks(blocks, i + 1, lst, struct_map)

-- fn blt_scan_stmts
blt_scan_stmts stmts i lst struct_map =
  match i >= glyph_array_len(stmts)
    true -> 0
    _ ->
      stmt = stmts[i]
      match stmt.skind == rv_aggregate()
        true ->
          match stmt.sival == ag_record()
            true ->
              tname = find_struct_name(stmt.sstr, struct_map)
              match glyph_str_len(tname) > 0
                true -> glyph_array_set(lst, stmt.sdest, tname)
                _ -> 0
            _ -> 0
        _ -> 0
      blt_scan_stmts(stmts, i + 1, lst, struct_map)

-- fn blt_tag_by_fa
blt_tag_by_fa lst fa struct_map i =
  match i >= glyph_array_len(lst)
    true -> 0
    _ ->
      match glyph_str_len(lst[i]) == 0
        true ->
          match glyph_array_len(fa[i]) > 0
            true ->
              sname = blt_find_struct(fa[i], struct_map, 0)
              match glyph_str_len(sname) > 0
                true -> glyph_array_set(lst, i, sname)
                _ -> 0
            _ -> 0
        _ -> 0
      blt_tag_by_fa(lst, fa, struct_map, i + 1)

-- fn bsm_loop
bsm_loop rows i result =
  match i >= glyph_array_len(rows)
    true -> 0
    _ ->
      row = rows[i]
      name = row[0]
      body = row[1]
      fields = parse_struct_fields(body)
      match glyph_array_len(fields) > 0
        true ->
          ctypes = parse_struct_ctypes(body)
          entry = []
          glyph_array_push(entry, name)
          glyph_array_push(entry, fields)
          glyph_array_push(entry, ctypes)
          glyph_array_push(result, entry)
          bsm_loop(rows, i + 1, result)
        _ -> bsm_loop(rows, i + 1, result)

-- fn build_capture_ops
build_capture_ops free_vars i =
  n_caps = glyph_array_len(free_vars) / 2
  match i >= n_caps
    true -> []
    _ ->
      local_id = free_vars[i * 2]
      rest = build_capture_ops(free_vars, i + 1)
      glyph_array_push(rest, mk_op_local(local_id))
      rest


-- fn build_fn_type
build_fn_type eng param_types ret_ty i =
  idx = glyph_array_len(param_types) - 1 - i
  match idx < 0
    true -> ret_ty
    _ ->
      rest = build_fn_type(eng, param_types, ret_ty, i + 1)
      mk_tfn(param_types[idx], rest, eng.ty_pool)

-- fn build_local_types
build_local_types mir struct_map =
  nlocals = glyph_array_len(mir.fn_locals)
  lst = blt_init(nlocals, 0)
  blt_scan_blks(mir.fn_blocks_stmts, 0, lst, struct_map)
  blt_propagate_blks(mir.fn_blocks_stmts, 0, lst)
  fa = blt_init_fa(nlocals, 0)
  blt_collect_fa_blks(mir.fn_blocks_stmts, 0, fa)
  blt_tag_by_fa(lst, fa, struct_map, 0)
  lst

-- fn build_program
build_program sources externs output_path =
  mirs = compile_fns(sources, 0)
  fix_all_field_offsets(mirs)
  fix_extern_calls(mirs, externs)
  tco_optimize(mirs)
  c_code = cg_program(mirs)
  wrappers = cg_extern_wrappers(externs, 0)
  full_c = s5(cg_runtime_full(externs), "\n", wrappers, "\n", c_code)
  lib_flags = collect_libs(externs)
  c_path = "/tmp/glyph_out.c"
  glyph_write_file(c_path, full_c)
  glyph_system(s7("cc -w -Wno-error -Wno-int-conversion -Wno-incompatible-pointer-types -O1 ", c_path, " -o ", output_path, " -no-pie", lib_flags, ""))

-- fn build_program
build_program sources externs output_path struct_map mode =
  parsed = parse_all_fns(sources, 0)
  n_errs = check_parse_errors(parsed)
  match n_errs > 0
    true ->
      eprintln(s2(itos(n_errs), " parse error(s), aborting"))
      glyph_exit(1)
    _ -> 0
  n_defs = glyph_array_len(parsed)
  verbose = mode == 1
  tc_results = match n_defs < 200
    true ->
      eng = mk_engine()
      register_builtins(eng)
      tc_pre_register(eng, parsed, 0)
      tcr = tc_infer_loop(eng, parsed, 0, [])
      tc_report_errors(eng)
      tcr
    _ -> []
  za_fns = build_za_fns(parsed, 0, [])
  mirs = compile_fns_parsed(parsed, 0, za_fns, tc_results, verbose)
  fix_all_field_offsets(mirs)
  fix_extern_calls(mirs, externs)
  tco_optimize(mirs)
  c_code = cg_program(mirs, struct_map)
  wrappers = cg_extern_wrappers(externs, 0)
  full_c = s5(cg_runtime_full(externs), "\n", wrappers, "\n", c_code)
  lib_flags = collect_libs(externs)
  c_path = "/tmp/glyph_out.c"
  glyph_write_file(c_path, full_c)
  cc_flags = match mode == 1
    true -> "-w -Wno-int-conversion -Wno-incompatible-pointer-types -DGLYPH_DEBUG -O0 -g"
    _ -> match mode == 2
      true -> "-w -Wno-int-conversion -Wno-incompatible-pointer-types -O2"
      _ -> "-w -Wno-int-conversion -Wno-incompatible-pointer-types -DGLYPH_DEBUG -O1"
  glyph_system(s5("cc ", cc_flags, s2(" ", c_path), s3(" -o ", output_path, " -no-pie"), lib_flags))
  mirs


-- fn build_struct_map
build_struct_map type_rows =
  result = []
  bsm_loop(type_rows, 0, result)
  result

-- fn build_test_program
build_test_program fn_sources test_sources test_names externs output_path =
  fn_mirs = compile_fns(fn_sources, 0)
  test_mirs = compile_fns(test_sources, 0)
  reg = build_type_reg(fn_mirs)
  add_types_to_reg(test_mirs, reg)
  fix_offs_mirs(fn_mirs, 0, reg)
  fix_offs_mirs(test_mirs, 0, reg)
  fix_extern_calls(fn_mirs, externs)
  fix_extern_calls(test_mirs, externs)
  tco_optimize(fn_mirs)
  tco_optimize(test_mirs)
  c_code = cg_test_program(fn_mirs, test_mirs, test_names)
  wrappers = cg_extern_wrappers(externs, 0)
  full_c = s6(cg_runtime_full(externs), "\n", wrappers, "\n", cg_test_runtime(0), c_code)
  lib_flags = collect_libs(externs)
  c_path = "/tmp/glyph_test.c"
  glyph_write_file(c_path, full_c)
  glyph_system(s7("cc -w -Wno-error -Wno-int-conversion -Wno-incompatible-pointer-types -O1 ", c_path, " -o ", output_path, " -no-pie", lib_flags, ""))

-- fn build_test_program
build_test_program fn_sources test_sources test_names externs output_path struct_map mode cover_path =
  parsed_fns = parse_all_fns(fn_sources, 0)
  parsed_tests = parse_all_fns(test_sources, 0)
  za_fns = build_za_fns(parsed_fns, 0, [])
  build_za_fns(parsed_tests, 0, za_fns)
  fn_mirs = compile_fns_parsed(parsed_fns, 0, za_fns, [], false)
  test_mirs = compile_fns_parsed(parsed_tests, 0, za_fns, [], false)
  reg = build_type_reg(fn_mirs)
  add_types_to_reg(test_mirs, reg)
  fix_offs_mirs(fn_mirs, 0, reg)
  fix_offs_mirs(test_mirs, 0, reg)
  fix_extern_calls(fn_mirs, externs)
  fix_extern_calls(test_mirs, externs)
  tco_optimize(fn_mirs)
  tco_optimize(test_mirs)
  use_cover = glyph_str_len(cover_path) > 0
  c_code = match use_cover
    true -> cg_test_program_cover(fn_mirs, test_mirs, test_names, struct_map, cover_path)
    _ -> cg_test_program(fn_mirs, test_mirs, test_names, struct_map)
  wrappers = cg_extern_wrappers(externs, 0)
  full_c = s6(cg_runtime_full(externs), "\n", wrappers, "\n", cg_test_runtime, c_code)
  lib_flags = collect_libs(externs)
  c_path = "/tmp/glyph_test.c"
  glyph_write_file(c_path, full_c)
  cov_flag = match use_cover
    true -> " -DGLYPH_COVERAGE"
    _ -> ""
  cc_flags = match mode == 1
    true -> s2("-w -Wno-int-conversion -Wno-incompatible-pointer-types -DGLYPH_DEBUG -O0 -g", cov_flag)
    _ -> match mode == 2
      true -> s2("-w -Wno-int-conversion -Wno-incompatible-pointer-types -O2", cov_flag)
      _ -> s2("-w -Wno-int-conversion -Wno-incompatible-pointer-types -DGLYPH_DEBUG -O1", cov_flag)
  glyph_system(s5("cc ", cc_flags, s2(" ", c_path), s3(" -o ", output_path, " -no-pie"), lib_flags))


-- fn build_type_reg
build_type_reg mirs =
  reg = []
  build_type_reg_mirs(mirs, 0, reg)
  reg

-- fn build_type_reg_blks
build_type_reg_blks blocks bi reg =
  match bi >= glyph_array_len(blocks)
    true -> 0
    _ ->
      build_type_reg_stmts(blocks[bi], 0, reg)
      build_type_reg_blks(blocks, bi + 1, reg)

-- fn build_type_reg_mirs
build_type_reg_mirs mirs i reg =
  match i >= glyph_array_len(mirs)
    true -> 0
    _ ->
      mir = mirs[i]
      build_type_reg_blks(mir.fn_blocks_stmts, 0, reg)
      build_type_reg_mirs(mirs, i + 1, reg)

-- fn build_type_reg_stmts
build_type_reg_stmts stmts si reg =
  match si >= glyph_array_len(stmts)
    true -> 0
    _ ->
      stmt = stmts[si]
      match stmt.skind == rv_aggregate()
        true -> match stmt.sival == ag_record()
          true ->
            names = split_comma(stmt.sstr)
            match glyph_array_len(names) > 0
              true -> add_type_to_reg(reg, names, 0)
              _ -> 0
          _ -> 0
        _ -> 0
      build_type_reg_stmts(stmts, si + 1, reg)

-- fn build_za_fns
build_za_fns parsed i acc =
  match i >= glyph_array_len(parsed)
    true -> acc
    _ ->
      pf = parsed[i]
      match pf.pf_fn_idx >= 0
        true ->
          node = (pf.pf_ast)[pf.pf_fn_idx]
          match glyph_array_len(node.ns) == 0
            true ->
              glyph_array_push(acc, pf.pf_name)
              build_za_fns(parsed, i + 1, acc)
            _ -> build_za_fns(parsed, i + 1, acc)
        _ -> build_za_fns(parsed, i + 1, acc)

-- fn c_field_type
c_field_type spec =
  match glyph_str_eq(spec, "i8")
    true -> "int8_t"
    _ -> match glyph_str_eq(spec, "u8")
      true -> "uint8_t"
      _ -> match glyph_str_eq(spec, "i16")
        true -> "int16_t"
        _ -> match glyph_str_eq(spec, "u16")
          true -> "uint16_t"
          _ -> match glyph_str_eq(spec, "i32")
            true -> "int32_t"
            _ -> match glyph_str_eq(spec, "u32")
              true -> "uint32_t"
              _ -> c_field_type2(spec)

-- fn c_field_type2
c_field_type2 spec =
  match glyph_str_eq(spec, "i64")
    true -> "int64_t"
    _ -> match glyph_str_eq(spec, "u64")
      true -> "uint64_t"
      _ -> match glyph_str_eq(spec, "f32")
        true -> "float"
        _ -> match glyph_str_eq(spec, "f64")
          true -> "double"
          _ -> match glyph_str_eq(spec, "ptr")
            true -> "void*"
            _ -> gval_t()

-- fn cdp_blks
cdp_blks blocks bi fn_name pairs =
  match bi >= glyph_array_len(blocks)
    true -> 0
    _ ->
      cdp_stmts(blocks[bi], 0, fn_name, pairs)
      cdp_blks(blocks, bi + 1, fn_name, pairs)

-- fn cdp_check_op
cdp_check_op op fn_name pairs =
  match op.okind == ok_func_ref()
    true ->
      name = op.ostr
      match is_runtime_fn(name) == 1
        true -> 0
        _ -> match has_glyph_prefix(name) == 1
          true -> 0
          _ -> match glyph_str_eq(name, fn_name) == 1
            true -> 0
            _ ->
              glyph_array_push(pairs, fn_name)
              glyph_array_push(pairs, name)
              0
    _ -> 0

-- fn cdp_mir
cdp_mir mir pairs =
  fn_name = mir.fn_name
  cdp_blks(mir.fn_blocks_stmts, 0, fn_name, pairs)

-- fn cdp_mirs
cdp_mirs mirs i pairs =
  match i >= glyph_array_len(mirs)
    true -> 0
    _ ->
      cdp_mir(mirs[i], pairs)
      cdp_mirs(mirs, i + 1, pairs)

-- fn cdp_ops
cdp_ops ops oi fn_name pairs =
  match oi >= glyph_array_len(ops)
    true -> 0
    _ ->
      cdp_check_op(ops[oi], fn_name, pairs)
      cdp_ops(ops, oi + 1, fn_name, pairs)

-- fn cdp_stmts
cdp_stmts stmts si fn_name pairs =
  match si >= glyph_array_len(stmts)
    true -> 0
    _ ->
      stmt = stmts[si]
      cdp_check_op(stmt.sop1, fn_name, pairs)
      cdp_check_op(stmt.sop2, fn_name, pairs)
      cdp_ops(stmt.sops, 0, fn_name, pairs)
      cdp_stmts(stmts, si + 1, fn_name, pairs)

-- fn cfv_add_params
cfv_add_params bound ast params i =
  match i >= glyph_array_len(params)
    true -> 0
    _ ->
      pnode = ast[params[i]]
      glyph_array_push(bound, pnode.sval)
      cfv_add_params(bound, ast, params, i + 1)


-- fn cg_aggregate_stmt
cg_aggregate_stmt stmt =
  nops = glyph_array_len(stmt.sops)
  match stmt.sival == ag_array()
    true -> cg_array_aggregate(stmt, nops)
    _ -> match stmt.sival == ag_variant()
      true -> cg_variant_aggregate(stmt, nops)
      _ -> cg_record_aggregate(stmt, nops)

-- fn cg_aggregate_stmt2
cg_aggregate_stmt2 stmt struct_map =
  nops = glyph_array_len(stmt.sops)
  match stmt.sival == ag_array()
    true -> cg_array_aggregate(stmt, nops)
    _ -> match stmt.sival == ag_variant()
      true -> cg_variant_aggregate(stmt, nops)
      _ -> cg_record_aggregate2(stmt, nops, struct_map)

-- fn cg_all_typedefs
cg_all_typedefs struct_map =
  cg_all_typedefs_loop(struct_map, 0)

-- fn cg_all_typedefs_loop
cg_all_typedefs_loop struct_map i =
  match i >= glyph_array_len(struct_map)
    true -> "\n"
    _ ->
      entry = struct_map[i]
      name = entry[0]
      fields = entry[1]
      types = entry[2]
      body = cg_struct_fields(fields, types, 0)
      lb = cg_lbrace()
      rb = cg_rbrace()
      td = s7("typedef struct ", lb, " ", body, s2(rb, " Glyph_"), name, ";\n")
      s2(td, cg_all_typedefs_loop(struct_map, i + 1))

-- fn cg_array_aggregate
cg_array_aggregate stmt nops =
  lb = cg_lbrace()
  rb = cg_rbrace()
  gv = gval_t()
  stores = cg_store_ops(stmt.sops, 0, "__data")
  p1 = s5("  ", lb, " ", gv, "* __data = (")
  p2 = s5(gv, "*)glyph_alloc(", itos(nops * 8), "); ", gv)
  p3 = s5("* __hdr = (", gv, "*)glyph_alloc(24); __hdr[0] = (", gv, ")__data; __hdr[1] = ")
  p4 = s5(itos(nops), "; __hdr[2] = ", itos(nops), "; ", stores)
  p5 = s5(cg_local(stmt.sdest), " = (", gv, ")__hdr; ", s2(rb, "\n"))
  s5(p1, p2, p3, p4, p5)

-- fn cg_binop_str
cg_binop_str op =
  match op == mir_add()
    true -> " + "
    _ -> match op == mir_sub()
      true -> " - "
      _ -> match op == mir_mul()
        true -> " * "
        _ -> match op == mir_div()
          true -> " / "
          _ -> match op == mir_mod()
            true -> " % "
            _ -> match op == mir_eq()
              true -> " == "
              _ -> match op == mir_neq()
                true -> " != "
                _ -> match op == mir_lt()
                  true -> " < "
                  _ -> match op == mir_gt()
                    true -> " > "
                    _ -> match op == mir_lt_eq()
                      true -> " <= "
                      _ -> match op == mir_gt_eq()
                        true -> " >= "
                        _ -> match op == mir_and()
                          true -> " && "
                          _ -> match op == mir_or()
                            true -> " || "
                            _ -> cg_binop_str2(op)

-- fn cg_binop_str2
cg_binop_str2 op =
  match op == mir_bitand()
    true -> " & "
    _ -> match op == mir_bitor()
      true -> " | "
      _ -> match op == mir_bitxor()
        true -> " ^ "
        _ -> match op == mir_shl()
          true -> " << "
          _ -> match op == mir_shr()
            true -> " >> "
            _ -> " + "

-- fn cg_block
cg_block mir_fn block_id =
  stmts = mir_fn.fn_blocks_stmts[block_id]
  term = mir_fn.fn_blocks_terms[block_id]
  s4(s2(cg_label(block_id), ":\n"), cg_block_stmts(stmts, 0), cg_term(term), "")

-- fn cg_block2
cg_block2 mir_fn block_id struct_map local_struct_types mir_local_types =
  stmts = mir_fn.fn_blocks_stmts[block_id]
  term = mir_fn.fn_blocks_terms[block_id]
  s4(s2(cg_label(block_id), ":\n"), cg_block_stmts2(stmts, 0, struct_map, local_struct_types, mir_local_types), cg_term(term), "")


-- fn cg_block_stmts
cg_block_stmts stmts i =
  match i >= glyph_array_len(stmts)
    true -> ""
    _ -> s2(cg_stmt(stmts[i]), cg_block_stmts(stmts, i + 1))

-- fn cg_block_stmts2
cg_block_stmts2 stmts i struct_map local_struct_types mir_local_types =
  match i >= glyph_array_len(stmts)
    true -> ""
    _ -> s2(cg_stmt2(stmts[i], struct_map, local_struct_types, mir_local_types), cg_block_stmts2(stmts, i + 1, struct_map, local_struct_types, mir_local_types))


-- fn cg_blocks
cg_blocks mir_fn i =
  match i >= mir_block_count(mir_fn)
    true -> ""
    _ -> s2(cg_block(mir_fn, i), cg_blocks(mir_fn, i + 1))

-- fn cg_blocks2
cg_blocks2 mir_fn i struct_map local_struct_types mir_local_types =
  match i >= mir_block_count(mir_fn)
    true -> ""
    _ -> s2(cg_block2(mir_fn, i, struct_map, local_struct_types, mir_local_types), cg_blocks2(mir_fn, i + 1, struct_map, local_struct_types, mir_local_types))


-- fn cg_call_args
cg_call_args ops i =
  match i >= glyph_array_len(ops)
    true -> ""
    _ ->
      arg = ops[i]
      rest = cg_call_args(ops, i + 1)
      match i == 0
        true -> s2(cg_operand(arg), rest)
        _ -> s3(", ", cg_operand(arg), rest)

-- fn cg_call_stmt
cg_call_stmt stmt =
  callee = stmt.sop1
  match callee.okind == ok_func_ref()
    true ->
      callee_str = cg_fn_name(callee.ostr)
      args_str = cg_call_args(stmt.sops, 0)
      s6("  ", cg_local(stmt.sdest), " = ", callee_str, s2("(", args_str), ");\n")
    _ -> cg_indirect_call_stmt(stmt)


-- fn cg_closure_stmt
cg_closure_stmt stmt =
  fn_name = cg_fn_name(stmt.sstr)
  n_caps = glyph_array_len(stmt.sops)
  total = (1 + n_caps) * 8
  lb = cg_lbrace()
  rb = cg_rbrace()
  gv = gval_t()
  stores = cg_closure_stores(stmt.sops, 0)
  s7("  ", lb, s4(" ", gv, "* __c = (", gv), s3("*)glyph_alloc(", itos(total), "); __c[0] = ("), s2(gv, s2(")&", fn_name)), s4("; ", stores, cg_local(stmt.sdest), s3(" = (", gv, ")__c; ")), s2(rb, "\n"))

-- fn cg_closure_stores
cg_closure_stores ops i =
  match i >= glyph_array_len(ops)
    true -> ""
    _ ->
      s5("__c[", itos(i + 1), "] = ", cg_operand(ops[i]), s2("; ", cg_closure_stores(ops, i + 1)))


-- fn cg_cov_collect_loop
cg_cov_collect_loop mirs i acc =
  match i >= glyph_array_len(mirs)
    true -> 0
    _ ->
      mir = mirs[i]
      nm = mir.fn_name
      match is_runtime_fn(nm) == 1
        true -> 0
        _ ->
          glyph_array_push(acc, nm)
          0
      cg_cov_collect_subs(mir.fn_subs, 0, acc)
      cg_cov_collect_loop(mirs, i + 1, acc)


-- fn cg_cov_collect_names
cg_cov_collect_names fn_mirs test_mirs =
  acc = []
  cg_cov_collect_loop(fn_mirs, 0, acc)
  cg_cov_collect_loop(test_mirs, 0, acc)
  acc


-- fn cg_cov_collect_subs
cg_cov_collect_subs subs i acc =
  match i >= glyph_array_len(subs)
    true -> 0
    _ ->
      sub = subs[i]
      nm = sub.fn_name
      match is_runtime_fn(nm) == 1
        true -> 0
        _ ->
          glyph_array_push(acc, nm)
          0
      cg_cov_collect_subs(subs, i + 1, acc)


-- fn cg_cov_increment
cg_cov_increment cov_names fn_name i =
  match i >= glyph_array_len(cov_names)
    true -> ""
    _ ->
      match glyph_str_eq(cov_names[i], fn_name) == 1
        true -> s3("#ifdef GLYPH_COVERAGE\n  _glyph_cov_hits[", itos(i), "]++;\n#endif\n")
        _ -> cg_cov_increment(cov_names, fn_name, i + 1)


-- fn cg_cov_names_init
cg_cov_names_init cov_names i =
  n = glyph_array_len(cov_names)
  match i >= n
    true -> ""
    _ ->
      entry = s3("\"", cg_escape_str(cov_names[i]), "\"")
      match i < n - 1
        true -> s3(entry, ", ", cg_cov_names_init(cov_names, i + 1))
        _ -> entry


-- fn cg_escape_chars
cg_escape_chars s i len out =
  match i >= len
    true -> out
    _ ->
      ch = glyph_str_char_at(s, i)
      esc = match ch == 92
        true -> "\\\\"
        _ -> match ch == 34
          true -> "\\\""
          _ -> match ch == 10
            true -> "\\n"
            _ -> glyph_str_slice(s, i, i + 1)
      cg_escape_chars(s, i + 1, len, s2(out, esc))

-- fn cg_escape_str
cg_escape_str s =
  cg_escape_chars(s, 0, glyph_str_len(s), "")

-- fn cg_extern_wrapper
cg_extern_wrapper row =
  name = row[0]
  symbol = row[1]
  sig = row[2]
  parts = split_arrow(sig)
  nparams = sig_param_count(parts)
  is_void = sig_is_void_ret(parts)
  gv = gval_t()
  params = match nparams == 0
    true -> s2(gv, " _d")
    _ -> cg_wrap_params(nparams, 0)
  call_args = match nparams == 0
    true -> ""
    _ -> cg_wrap_call_args(nparams, 0)
  lb = cg_lbrace()
  rb = cg_rbrace()
  match is_void == 1
    true -> s7(gv, " glyph_", name, s3("(", params, ") "), lb, s5(" (", symbol, ")(", call_args, s3("); return 0; ", rb, "\n")), "")
    _ -> s7(gv, " glyph_", name, s3("(", params, ") "), lb, s5(s2(" return (", gv), ")(", symbol, ")(", s2(call_args, s3("); ", rb, "\n"))), "")

-- fn cg_extern_wrappers
cg_extern_wrappers externs i =
  match i >= glyph_array_len(externs)
    true -> ""
    _ ->
      row = externs[i]
      name = row[0]
      rest = cg_extern_wrappers(externs, i + 1)
      match is_runtime_fn(name)
        true -> rest
        _ -> match has_glyph_prefix(name)
          true -> rest
          _ -> s2(cg_extern_wrapper(row), rest)

-- fn cg_field_stmt
cg_field_stmt stmt =
  gvp = s3("((", gval_t(), "*)")
  chk = cg_null_check(stmt.sop1, s2("field offset ", itos(stmt.sival)))
  s2(chk, s6("  ", cg_local(stmt.sdest), s2(" = ", gvp), cg_operand(stmt.sop1), ")[", s3(itos(stmt.sival), "];\n", "")))

-- fn cg_field_stmt2
cg_field_stmt2 stmt local_struct_types struct_map mir_local_types =
  base_local = stmt.sop1.oval
  tname = match base_local >= 0
    true ->
      match base_local < glyph_array_len(local_struct_types)
        true -> local_struct_types[base_local]
        _ -> ""
    _ -> ""
  match glyph_str_len(tname) > 0
    true ->
      gtype = s2("Glyph_", tname)
      chk = cg_null_check(stmt.sop1, s2(".", stmt.sstr))
      access = s5("((", gtype, "*)", cg_operand(stmt.sop1), s2(")->", stmt.sstr))
      fctype = find_field_ctype(tname, stmt.sstr, struct_map)
      is_flt = is_float_ctype(fctype)
      _ = match is_flt == 1
        true -> glyph_array_set(mir_local_types, stmt.sdest, 3)
        _ -> 0
      wrapped = match is_flt == 1
        true -> s3("_glyph_f2i((double)", access, ")")
        _ -> access
      s2(chk, s4("  ", cg_local(stmt.sdest), " = ", s2(wrapped, ";\n")))
    _ -> cg_field_stmt(stmt)


-- fn cg_float_binop
cg_float_binop stmt =
  a = s3("_glyph_i2f(", cg_operand(stmt.sop1), ")")
  b = s3("_glyph_i2f(", cg_operand(stmt.sop2), ")")
  s7("  ", cg_local(stmt.sdest), " = _glyph_f2i(", a, cg_binop_str(stmt.sival), b, ");\n")


-- fn cg_float_cmp
cg_float_cmp stmt =
  a = s3("_glyph_i2f(", cg_operand(stmt.sop1), ")")
  b = s3("_glyph_i2f(", cg_operand(stmt.sop2), ")")
  s6("  ", cg_local(stmt.sdest), " = ", a, cg_binop_str(stmt.sival), s2(b, ";\n"))


-- fn cg_float_unop
cg_float_unop stmt =
  s5("  ", cg_local(stmt.sdest), " = _glyph_f2i(-_glyph_i2f(", cg_operand(stmt.sop1), "));\n")


-- fn cg_fn_name
cg_fn_name name =
  match is_runtime_fn(name)
    true -> s2("glyph_", name)
    _ -> name

-- fn cg_forward_decl
cg_forward_decl mir_fn =
  fname = cg_fn_name(mir_fn.fn_name)
  gt = s2(gval_t(), " ")
  s5(gt, fname, "(", cg_params_list(mir_fn, 0), ");\n")

-- fn cg_forward_decls
cg_forward_decls mirs i =
  match i >= glyph_array_len(mirs)
    true -> ""
    _ -> s2(cg_forward_decl(mirs[i]), cg_forward_decls(mirs, i + 1))

-- fn cg_function
cg_function mir_fn =
  param_count = glyph_array_len(mir_fn.fn_params)
  fname = cg_fn_name(mir_fn.fn_name)
  gt = s2(gval_t(), " ")
  header = s7(gt, fname, "(", cg_params_list(mir_fn, 0), ") ", cg_lbrace(), "\n")
  fntrack = s6("  _glyph_current_fn = \"", mir_fn.fn_name, "\";\n  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = \"", mir_fn.fn_name, "\";\n  _glyph_call_depth++;\n", "")
  locals = cg_locals_decl(mir_fn, 0, param_count)
  blocks = cg_blocks(mir_fn, 0)
  s6(header, fntrack, locals, blocks, cg_rbrace(), "\n\n")

-- fn cg_function2
cg_function2 mir_fn struct_map =
  param_count = glyph_array_len(mir_fn.fn_params)
  fname = cg_fn_name(mir_fn.fn_name)
  gt = s2(gval_t(), " ")
  header = s7(gt, fname, "(", cg_params_list(mir_fn, 0), ") ", cg_lbrace(), "\n")
  fntrack = s6("  _glyph_current_fn = \"", mir_fn.fn_name, "\";\n#ifdef GLYPH_DEBUG\n  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = \"", mir_fn.fn_name, "\";\n  _glyph_call_depth++;\n#endif\n", "")
  locals = cg_locals_decl(mir_fn, 0, param_count)
  local_struct_types = build_local_types(mir_fn, struct_map)
  mir_local_types = mir_fn.fn_types
  blocks = cg_blocks2(mir_fn, 0, struct_map, local_struct_types, mir_local_types)
  s6(header, fntrack, locals, blocks, cg_rbrace(), "\n\n")


-- fn cg_function2_cover
cg_function2_cover mir_fn struct_map cov_names =
  param_count = glyph_array_len(mir_fn.fn_params)
  fname = cg_fn_name(mir_fn.fn_name)
  gt = s2(gval_t(), " ")
  header = s7(gt, fname, "(", cg_params_list(mir_fn, 0), ") ", cg_lbrace(), "\n")
  fntrack = s6("  _glyph_current_fn = \"", mir_fn.fn_name, "\";\n#ifdef GLYPH_DEBUG\n  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = \"", mir_fn.fn_name, "\";\n  _glyph_call_depth++;\n#endif\n", "")
  cov_inc = cg_cov_increment(cov_names, mir_fn.fn_name, 0)
  locals = cg_locals_decl(mir_fn, 0, param_count)
  local_struct_types = build_local_types(mir_fn, struct_map)
  mir_local_types = mir_fn.fn_types
  blocks = cg_blocks2(mir_fn, 0, struct_map, local_struct_types, mir_local_types)
  s7(header, fntrack, cov_inc, locals, blocks, cg_rbrace(), "\n\n")


-- fn cg_functions
cg_functions mirs i =
  match i >= glyph_array_len(mirs)
    true -> ""
    _ -> s2(cg_function(mirs[i]), cg_functions(mirs, i + 1))

-- fn cg_functions2
cg_functions2 mirs i struct_map =
  match i >= glyph_array_len(mirs)
    true -> ""
    _ -> s2(cg_function2(mirs[i], struct_map), cg_functions2(mirs, i + 1, struct_map))

-- fn cg_functions2_cover
cg_functions2_cover mirs i struct_map cov_names =
  match i >= glyph_array_len(mirs)
    true -> ""
    _ -> s2(cg_function2_cover(mirs[i], struct_map, cov_names), cg_functions2_cover(mirs, i + 1, struct_map, cov_names))


-- fn cg_index_stmt
cg_index_stmt stmt =
  arr = cg_operand(stmt.sop1)
  idx = cg_operand(stmt.sop2)
  lb = cg_lbrace()
  rb = cg_rbrace()
  gv = gval_t()
  chk = cg_null_check(stmt.sop1, "array index")
  p1 = s5("  ", lb, " ", gv, "* __hdr = (")
  p2 = s5(gv, "*)", arr, "; ", gv)
  p3 = s5(" __idx = ", idx, "; glyph_array_bounds_check(__idx, __hdr[1]); ", cg_local(stmt.sdest), " = ((")
  p4 = s4(gv, "*)__hdr[0])[__idx]; ", rb, "\n")
  s5(chk, p1, p2, p3, p4)

-- fn cg_indirect_args
cg_indirect_args closure ops i =
  match i >= glyph_array_len(ops)
    true -> closure
    _ ->
      s4(closure, ", ", cg_operand(ops[i]), cg_indirect_args_rest(ops, i + 1))


-- fn cg_indirect_args_rest
cg_indirect_args_rest ops i =
  match i >= glyph_array_len(ops)
    true -> ""
    _ -> s3(", ", cg_operand(ops[i]), cg_indirect_args_rest(ops, i + 1))


-- fn cg_indirect_call_stmt
cg_indirect_call_stmt stmt =
  closure = cg_operand(stmt.sop1)
  n_args = glyph_array_len(stmt.sops)
  lb = cg_lbrace()
  rb = cg_rbrace()
  gv = gval_t()
  sig = cg_indirect_sig(n_args + 1)
  args = cg_indirect_args(closure, stmt.sops, 0)
  s7("  ", lb, s4(" ", gv, " __fp = ((", gv), s2("*)", closure), s2(")[0]; ", cg_local(stmt.sdest)), s6(" = ((", sig, ")__fp)(", args, "); ", s2(rb, "\n")), "")

-- fn cg_indirect_sig
cg_indirect_sig n_params =
  s3(gval_t(), "(*)(", s2(cg_sig_params(n_params, 0), ")"))

-- fn cg_label
cg_label id = s2("bb_", itos(id))

-- fn cg_lbrace
cg_lbrace = "\{"

-- fn cg_local
cg_local id = s2("_", itos(id))

-- fn cg_locals_decl
cg_locals_decl mir_fn i param_count =
  match i >= mir_local_count(mir_fn)
    true -> ""
    _ ->
      match i < param_count
        true -> cg_locals_decl(mir_fn, i + 1, param_count)
        _ -> s5("  ", gval_t(), s2(" ", cg_local(i)), " = 0;\n", cg_locals_decl(mir_fn, i + 1, param_count))

-- fn cg_main_wrapper
cg_main_wrapper =
  lb = cg_lbrace()
  rb = cg_rbrace()
  s7("extern void glyph_set_args(int argc, char** argv);\nint main(int argc, char** argv) ", lb, "\n  signal(SIGSEGV, _glyph_sigsegv);\n  signal(SIGFPE, _glyph_sigfpe);\n  glyph_set_args(argc, argv);\n  return (int)glyph_main();\n", rb, "\n", "", "")


-- fn cg_null_check
cg_null_check op ctx =
  gv = s3("(", gval_t(), ")")
  s6("#ifdef GLYPH_DEBUG\n  _glyph_null_check(", gv, s2(cg_operand(op), ", \""), ctx, "\");\n#endif\n", "")

-- fn cg_operand
cg_operand op =
  k = op.okind
  gv = gval_t()
  match k
    _ ? k == ok_local() -> cg_local(op.oval)
    _ ? k == ok_const_int() -> s3("(", gv, s2(")", itos(op.oval)))
    _ ? k == ok_const_bool() -> match op.oval == 0
      true -> "0"
      _ -> "1"
    _ ? k == ok_const_unit() -> "0"
    _ ? k == ok_const_str() -> cg_str_literal(op.ostr)
    _ ? k == ok_func_ref() -> s3("(", gv, s2(")&", op.ostr))
    _ ? k == ok_const_float() -> s2("_glyph_f2i(", s2(op.ostr, ")"))
    _ -> "0"

-- fn cg_params
cg_params mir_fn i =
  pc = glyph_array_len(mir_fn.fn_params)
  match i >= pc
    true -> ""
    _ ->
      param_id = mir_fn.fn_params[i]
      decl = s2("long long ", cg_local(param_id))
      match i == pc - 1
        true -> decl
        _ -> s2(decl, ", ")

-- fn cg_params_list
cg_params_list mir_fn i =
  pc = glyph_array_len(mir_fn.fn_params)
  match i >= pc
    true -> ""
    _ ->
      param_id = mir_fn.fn_params[i]
      decl = s3(gval_t(), " ", cg_local(param_id))
      rest = cg_params_list(mir_fn, i + 1)
      match i < pc - 1
        true -> s3(decl, ", ", rest)
        _ -> s2(decl, rest)

-- fn cg_preamble
cg_preamble =
  s2("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <signal.h>\n#include <stdint.h>\ntypedef intptr_t GVal;\n\nextern const char* _glyph_current_fn;\nextern void _glyph_sigsegv(int sig);\nextern void _glyph_sigfpe(int sig);\n#ifdef GLYPH_DEBUG\nextern const char* _glyph_call_stack[256];\nextern int _glyph_call_depth;\nextern void _glyph_null_check(GVal ptr, const char* ctx);\n#endif\nextern GVal glyph_alloc(GVal size);\nextern GVal glyph_cstr_to_str(const char* s);\nextern GVal glyph_str_concat(GVal a, GVal b);\nextern GVal glyph_str_eq(GVal a, GVal b);\nextern GVal glyph_int_to_str(GVal n);\nextern GVal glyph_println(GVal s);\nextern GVal glyph_print(GVal s);\nextern void glyph_array_bounds_check(GVal idx, GVal len);\nextern GVal glyph_array_push(GVal hdr, GVal val);\nextern GVal glyph_array_len(GVal hdr);\nextern GVal glyph_array_set(GVal hdr, GVal idx, GVal val);\nextern GVal glyph_array_pop(GVal hdr);\nextern GVal glyph_array_new(GVal cap);\nextern GVal glyph_str_len(GVal s);\nextern GVal glyph_str_char_at(GVal s, GVal idx);\nextern char* glyph_str_to_cstr(GVal s);\nextern GVal glyph_read_file(GVal ps);\nextern GVal glyph_write_file(GVal ps, GVal cs);\nextern GVal glyph_system(GVal cs);\nextern GVal glyph_db_open(GVal ps);\nextern GVal glyph_db_close(GVal h);\nextern GVal glyph_db_exec(GVal h, GVal ss);\nextern GVal glyph_db_query_rows(GVal h, GVal ss);\nextern GVal glyph_db_query_one(GVal h, GVal ss);\nextern GVal glyph_raw_set(GVal ptr, GVal idx, GVal val);\nextern GVal glyph_str_slice(GVal s, GVal start, GVal end);\nextern GVal glyph_exit(GVal code);\nextern GVal glyph_sb_new(void);\nextern GVal glyph_sb_append(GVal sb, GVal str);\nextern GVal glyph_sb_build(GVal sb);\n\n", "")

-- fn cg_program
cg_program mirs =
  s6(cg_preamble(), cg_forward_decls(mirs, 0), "\n", cg_functions(mirs, 0), "\n", cg_main_wrapper())

-- fn cg_program
cg_program mirs struct_map =
  s7(cg_preamble(), cg_all_typedefs(struct_map), cg_forward_decls(mirs, 0), "
", cg_functions2(mirs, 0, struct_map), "
", cg_main_wrapper())

-- fn cg_rbrace
cg_rbrace = "}"

-- fn cg_record_aggregate
cg_record_aggregate stmt nops =
  lb = cg_lbrace()
  rb = cg_rbrace()
  gv = gval_t()
  names = split_comma(stmt.sstr)
  stores = match glyph_array_len(names) == nops
    true -> cg_store_ops_alpha(stmt.sops, names, 0, "__r")
    _ -> cg_store_ops(stmt.sops, 0, "__r")
  s7("  ", lb, s4(" ", gv, "* __r = (", gv), s3("*)glyph_alloc(", itos(nops * 8), "); "), stores, s5(cg_local(stmt.sdest), " = (", gv, ")__r; ", rb), "\n")

-- fn cg_record_aggregate2
cg_record_aggregate2 stmt nops struct_map =
  tname = find_struct_name(stmt.sstr, struct_map)
  match glyph_str_len(tname) > 0
    true ->
      lb = cg_lbrace()
      rb = cg_rbrace()
      gv = gval_t()
      gtype = s2("Glyph_", tname)
      names = split_comma(stmt.sstr)
      ctypes = find_entry_types(tname, struct_map)
      stores = cg_struct_stores_typed(stmt.sops, names, ctypes, 0)
      s7("  ", lb, s3(" ", gtype, "* __r = ("), s3(gtype, "*)glyph_alloc(sizeof(", gtype), s2(")); ", stores), s4(cg_local(stmt.sdest), " = (", gv, ")__r; "), s2(rb, "\n"))
    _ -> cg_record_aggregate(stmt, nops)

-- fn cg_runtime_args
cg_runtime_args =
  s2("static int g_argc = 0;\nstatic char** g_argv = 0;\nvoid glyph_set_args(int argc, char** argv) \{ g_argc = argc; g_argv = argv; }\n",
  "GVal glyph_args(void) \{ long long c=(long long)g_argc; long long* d=(long long*)malloc(c*8); int i; for(i=0;i<g_argc;i++) \{ long long sl=(long long)strlen(g_argv[i]); char* s=(char*)malloc(16); *(const char**)s=g_argv[i]; *(long long*)(s+8)=sl; d[i]=(long long)s; } long long* h=(long long*)malloc(24); h[0]=(long long)d; h[1]=c; h[2]=c; return (GVal)h; }\n")

-- fn cg_runtime_c
cg_runtime_c =
  p1 = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <signal.h>\n#include <stdint.h>\ntypedef intptr_t GVal;\n\nconst char* _glyph_current_fn = \"(unknown)\";\n\n#ifdef GLYPH_DEBUG\nconst char* _glyph_call_stack[256];\nint _glyph_call_depth = 0;\nstatic void _glyph_print_stack(void) \{ int i; fprintf(stderr, \"--- stack trace ---\\n\"); for (i = _glyph_call_depth - 1; i >= 0; i--) fprintf(stderr, \"  %s\\n\", _glyph_call_stack[i]); }\n#endif\n\nstatic void _glyph_sigsegv(int sig) \{ fprintf(stderr, \"\\nsegfault in %s\\n\", _glyph_current_fn);\n#ifdef GLYPH_DEBUG\n  _glyph_print_stack();\n#endif\n  signal(sig, SIG_DFL); raise(sig); }\nstatic void _glyph_sigfpe(int sig) \{ fprintf(stderr, \"\\ndivision by zero in %s\\n\", _glyph_current_fn);\n#ifdef GLYPH_DEBUG\n  _glyph_print_stack();\n#endif\n  signal(sig, SIG_DFL); raise(sig); }\n\n#ifdef GLYPH_DEBUG\nvoid _glyph_null_check(GVal ptr, const char* ctx) \{ if (ptr == 0) \{ fprintf(stderr, \"\\nnull pointer in %s: %s\\n\", _glyph_current_fn, ctx); _glyph_print_stack(); abort(); } }\n#endif\n\n"
  p2 = "GVal glyph_panic(const char* msg) \{ fprintf(stderr, \"panic in %s: %s\\n\", _glyph_current_fn, msg);\n#ifdef GLYPH_DEBUG\n  _glyph_print_stack();\n#endif\n  exit(1); return 0; }\nGVal glyph_alloc(GVal size) \{ void* p = malloc((size_t)size); if (!p) glyph_panic(\"out of memory\"); return (GVal)p; }\nGVal glyph_realloc(GVal ptr, GVal size) \{ void* p = realloc((void*)ptr, (size_t)size); if (!p) glyph_panic(\"realloc failed\"); return (GVal)p; }\nvoid glyph_dealloc(GVal ptr) \{ free((void*)ptr); }\n"
  p3 = "GVal glyph_str_eq(GVal a, GVal b) \{ if (!a || !b) return (a == b) ? 1 : 0; long long la = *(long long*)((char*)a+8), lb = *(long long*)((char*)b+8); if (la != lb) return 0; return memcmp(*(char**)a, *(char**)b, (size_t)la) == 0 ? 1 : 0; }\nGVal glyph_str_len(GVal s) \{ if (!(void*)s) return 0; return *(long long*)((char*)s + 8); }\nGVal glyph_str_char_at(GVal s, GVal i) \{ const char* p = *(const char**)s; long long len = *(long long*)((char*)s + 8); if (i < 0 || i >= len) return -1; return (GVal)(unsigned char)p[i]; }\nGVal glyph_str_slice(GVal s, GVal start, GVal end) \{ const char* p = *(const char**)s; long long slen = *(long long*)((char*)s + 8); if (start < 0) start = 0; if (end > slen) end = slen; if (end <= start) \{ char* r = (char*)malloc(16); *(const char**)r = \"\"; *(long long*)(r+8) = 0; return (GVal)r; } long long len = end - start; char* r = (char*)malloc(16 + len); char* d = r + 16; memcpy(d, p + start, (size_t)len); *(const char**)r = d; *(long long*)(r+8) = len; return (GVal)r; }\n"
  p4 = "GVal glyph_str_concat(GVal a, GVal b) \{ if (!a) return b; if (!b) return a; long long la = *(long long*)((char*)a+8), lb = *(long long*)((char*)b+8); long long tl = la+lb; char* r = (char*)malloc(16+tl); char* d = r+16; memcpy(d, *(char**)a, (size_t)la); memcpy(d+la, *(char**)b, (size_t)lb); *(const char**)r = d; *(long long*)(r+8) = tl; return (GVal)r; }\nGVal glyph_int_to_str(GVal n) \{ char buf[32]; int len = snprintf(buf, 32, \"%lld\", (long long)n); char* r = (char*)malloc(16+len); char* d = r+16; memcpy(d, buf, len); *(const char**)r = d; *(long long*)(r+8) = (long long)len; return (GVal)r; }\nGVal glyph_cstr_to_str(const char* s) \{ if (!(void*)s) \{ char* r=(char*)malloc(16); *(const char**)r=\"\"; *(long long*)(r+8)=0; return (GVal)r; } long long l=(long long)strlen(s); char* r=(char*)malloc(16+l); char* d=r+16; memcpy(d,s,(size_t)l); *(const char**)r=d; *(long long*)(r+8)=l; return (GVal)r; }\n"
  p5 = "GVal glyph_println(GVal s) \{ if (!(void*)s) \{ fprintf(stdout, \"(null)\\n\"); fflush(stdout); return 0; } const char* p = *(const char**)s; long long l = *(long long*)((char*)s+8); fwrite(p, 1, (size_t)l, stdout); fputc(10, stdout); fflush(stdout); return 0; }\nGVal glyph_eprintln(GVal s) \{ const char* p = *(const char**)s; long long l = *(long long*)((char*)s+8); fwrite(p, 1, (size_t)l, stderr); fputc(10, stderr); fflush(stderr); return 0; }\nvoid glyph_array_bounds_check(GVal idx, GVal len) \{ if (idx < 0 || idx >= len) \{ fprintf(stderr, \"panic in %s: index %lld out of bounds (len %lld)\\n\", _glyph_current_fn, (long long)idx, (long long)len);\n#ifdef GLYPH_DEBUG\n  _glyph_print_stack();\n#endif\n  exit(1); } }\n"
  p6 = "GVal glyph_array_push(GVal hdr, GVal val) \{ long long* h=(long long*)hdr; long long* data=(long long*)h[0]; long long len=h[1],cap=h[2]; if(len>=cap)\{ cap=cap<4?4:cap*2; data=(long long*)realloc(data,cap*8); if(!data) glyph_panic(\"array push oom\"); h[0]=(long long)data; h[2]=cap; } data[len]=val; h[1]=len+1; return hdr; }\nGVal glyph_array_len(GVal hdr) \{ return ((long long*)hdr)[1]; }\nGVal glyph_array_set(GVal hdr, GVal i, GVal v) \{ long long* h=(long long*)hdr; glyph_array_bounds_check(i, h[1]); ((long long*)h[0])[i]=v; return 0; }\nGVal glyph_array_pop(GVal hdr) \{ long long* h=(long long*)hdr; if (h[1] <= 0) glyph_panic(\"pop on empty array\"); h[1]--; return ((long long*)h[0])[h[1]]; }\n"
  p7 = "GVal glyph_exit(GVal code) \{ exit((int)code); return 0; }\nGVal glyph_str_to_int(GVal s) \{ const char* p=*(const char**)s; long long l=*(long long*)((char*)s+8),r=0,i=0,sg=1; if(i<l&&p[i]==45) \{ sg=-1; i++; } while(i<l&&p[i]>=48&&p[i]<=57) \{ r=r*10+(p[i]-48); i++; } return r*sg; }\n"
  s7(p1, p2, p3, p4, p5, p6, p7)


-- fn cg_runtime_coverage
cg_runtime_coverage cover_path cov_names =
  n = glyph_array_len(cov_names)
  ns = itos(n)
  lb = cg_lbrace()
  rb = cg_rbrace()
  names_init = cg_cov_names_init(cov_names, 0)
  s1 = s5("#ifdef GLYPH_COVERAGE\nstatic const char* _glyph_cov_names[", ns, "] = ", lb, names_init)
  s2a = s5(rb, ";\nstatic long long _glyph_cov_hits[", ns, "] = ", lb)
  s3a = s5("0", rb, ";\nstatic int _glyph_cov_count = ", ns, ";\n")
  s4a = s5("static const char* _glyph_cov_path = \"", cg_escape_str(cover_path), "\";\n", "", "")
  writer_head = s5("void _glyph_cov_write(void) ", lb, "\n  FILE* f = fopen(_glyph_cov_path, \"w\");\n", "  if (!f) return;\n", "  for (int i = 0; i < _glyph_cov_count; i++) ")
  writer_body = s5(lb, "\n    fprintf(f, \"%s", "\\t%lld", "\\n\", _glyph_cov_names[i], _glyph_cov_hits[i]);\n  ", rb)
  writer_tail = s3("\n  fclose(f);\n", rb, "\n#endif\n\n")
  s7(s1, s2a, s3a, s4a, writer_head, writer_body, writer_tail)


-- fn cg_runtime_extra
cg_runtime_extra =
  s2("GVal glyph_print(GVal s) \{ const char* p=*(const char**)s; long long l=*(long long*)((char*)s+8); fwrite(p,1,(size_t)l,stdout); fflush(stdout); return 0; }\n",
  "GVal glyph_array_new(GVal cap) \{ if(cap<=0) cap=4; long long* d=(long long*)malloc(cap*8); if(!d) glyph_panic(\"array_new oom\"); long long* h=(long long*)malloc(24); h[0]=(long long)d; h[1]=0; h[2]=cap; return (GVal)h; }\n")


-- fn cg_runtime_float
cg_runtime_float =
  helpers = "static inline double _glyph_i2f(GVal v) \{ double d; memcpy(&d, &v, 8); return d; }\nstatic inline GVal _glyph_f2i(double d) \{ GVal v; memcpy(&v, &d, 8); return v; }\n"
  fts = "GVal glyph_float_to_str(GVal v) \{ char buf[32]; int len = snprintf(buf, 32, \"%g\", _glyph_i2f(v)); char* r = (char*)malloc(16+len); char* d2 = r+16; memcpy(d2, buf, len); *(const char**)r = d2; *(long long*)(r+8) = len; return (GVal)r; }\n"
  stf = "GVal glyph_str_to_float(GVal s) \{ const char* p=*(const char**)s; long long l=*(long long*)((char*)s+8); char buf[64]; if(l>63) l=63; memcpy(buf,p,(size_t)l); buf[l]=0; return _glyph_f2i(atof(buf)); }\n"
  itf = "GVal glyph_int_to_float(GVal n) \{ return _glyph_f2i((double)n); }\nGVal glyph_float_to_int(GVal v) \{ return (GVal)(long long)_glyph_i2f(v); }\n"
  s4(helpers, fts, stf, itf)


-- fn cg_runtime_full
cg_runtime_full externs =
  base1 = s7(cg_runtime_c(), cg_runtime_args(), cg_runtime_sb(), cg_runtime_raw(), cg_runtime_io(), cg_runtime_extra(), "\n")
  base = s5(base1, cg_runtime_float(), cg_runtime_result, cg_runtime_mcp, "\n")
  match needs_sqlite(externs)
    true -> s2(base, cg_runtime_sqlite())
    _ -> base


-- fn cg_runtime_io
cg_runtime_io =
  s2("char* glyph_str_to_cstr(GVal s) \{ const char* p=*(const char**)s; long long l=*(long long*)((char*)s+8); char* c=(char*)malloc(l+1); memcpy(c,p,(size_t)l); c[l]=0; return c; }\n",
  s2("GVal glyph_read_file(GVal ps) \{ char* cp=glyph_str_to_cstr(ps); FILE* f=fopen(cp,\"rb\"); free(cp); if(!f)\{ char* r=(char*)malloc(16); *(const char**)r=0; *(long long*)(r+8)=-1; return (GVal)r; } fseek(f,0,SEEK_END); long long sz=ftell(f); fseek(f,0,SEEK_SET); char* r=(char*)malloc(16+sz); char* d=r+16; size_t nr=fread(d,1,(size_t)sz,f); fclose(f); *(const char**)r=d; *(long long*)(r+8)=(long long)nr; return (GVal)r; }\n",
  s2("GVal glyph_write_file(GVal ps, GVal cs) \{ char* cp=glyph_str_to_cstr(ps); const char* dp=*(const char**)cs; long long dl=*(long long*)((char*)cs+8); FILE* f=fopen(cp,\"wb\"); free(cp); if(!f) return -1; size_t w=fwrite(dp,1,(size_t)dl,f); fclose(f); return w==(size_t)dl?0:-1; }\n",
  "GVal glyph_system(GVal cs) \{ char* c=glyph_str_to_cstr(cs); int rc=system(c); free(c); if(rc==-1) return -1; return (long long)((rc>>8)&0xFF); }\n")))


-- fn cg_runtime_mcp
cg_runtime_mcp =
  s2("GVal glyph_read_line(GVal dummy) \{ char buf[65536]; if(!fgets(buf,sizeof(buf),stdin))\{ void* s=malloc(16); char* e=malloc(1); e[0]=0; *(const char**)s=e; *(long long*)((char*)s+8)=0; return (GVal)s; } long long len=strlen(buf); if(len>0 && buf[len-1]==10) len--; char* data=malloc(len+1); memcpy(data,buf,len); data[len]=0; void* s=malloc(16); *(const char**)s=data; *(long long*)((char*)s+8)=len; return (GVal)s; }\n",
  "GVal glyph_flush(GVal dummy) \{ fflush(stdout); return 0; }\n")

-- fn cg_runtime_raw
cg_runtime_raw = "GVal glyph_raw_set(GVal ptr, GVal idx, GVal val) \{ ((long long*)ptr)[idx] = val; return 0; }\n" 

-- fn cg_runtime_result
cg_runtime_result =
  s2("GVal glyph_ok(GVal val) \{ long long* r=(long long*)glyph_alloc(16); r[0]=0; r[1]=val; return (long long)r; }\n",
  s2("GVal glyph_err(GVal msg) \{ long long* r=(long long*)glyph_alloc(16); r[0]=1; r[1]=msg; return (long long)r; }\n",
  s2("GVal glyph_try_read_file(GVal path) \{ GVal result=glyph_read_file(path); if(*(long long*)((char*)result+8)<0) \{ const char* m=\"file read failed\"; char* s=(char*)malloc(16); *(const char**)s=m; *(long long*)(s+8)=16; return glyph_err((GVal)s); } return glyph_ok(result); }\n",
  "GVal glyph_try_write_file(GVal path, GVal content) \{ long long result=glyph_write_file(path,content); if(result<0) \{ const char* m=\"file write failed\"; char* s=(char*)malloc(16); *(const char**)s=m; *(long long*)(s+8)=17; return glyph_err((GVal)s); } return glyph_ok(0); }\n")))

-- fn cg_runtime_sb
cg_runtime_sb =
  s2("GVal glyph_sb_new(void) \{ long long* sb=(long long*)malloc(24); char* buf=(char*)malloc(64); sb[0]=(long long)buf; sb[1]=0; sb[2]=64; return (GVal)sb; }\n",
  s2("GVal glyph_sb_append(GVal sp, GVal ss) \{ long long* sb=(long long*)(void*)sp; const char* p=*(const char**)ss; long long sl=*(long long*)((char*)ss+8); long long nl=sb[1]+sl; if(nl>sb[2]) \{ long long c=sb[2]; while(c<nl) c*=2; char* nb=(char*)malloc(c); memcpy(nb,(char*)sb[0],(size_t)sb[1]); free((char*)sb[0]); sb[0]=(long long)nb; sb[2]=c; } memcpy((char*)sb[0]+sb[1],p,(size_t)sl); sb[1]=nl; return sp; }\n",
  "GVal glyph_sb_build(GVal sp) \{ long long* sb=(long long*)(void*)sp; long long l=sb[1]; char* r=(char*)malloc(16+l); char* d=r+16; memcpy(d,(char*)sb[0],(size_t)l); *(const char**)r=d; *(long long*)(r+8)=l; free((char*)sb[0]); free(sb); return (GVal)r; }\n"))

-- fn cg_runtime_sqlite
cg_runtime_sqlite =
  s2("#include <sqlite3.h>\n",
  s2("GVal glyph_db_open(GVal ps) \{ char* cp=glyph_str_to_cstr(ps); sqlite3* db=0; int rc=sqlite3_open(cp,&db); free(cp); if(rc!=0)\{ if(db) sqlite3_close(db); return 0; } return (GVal)db; }\n",
  s2("GVal glyph_db_close(GVal h) \{ if(h) sqlite3_close((sqlite3*)h); return 0; }\n",
  s2("GVal glyph_db_exec(GVal h, GVal ss) \{ sqlite3* db=(sqlite3*)(void*)h; char* sql=glyph_str_to_cstr(ss); char* err=0; int rc=sqlite3_exec(db,sql,0,0,&err); free(sql); if(err)\{ fprintf(stderr,\"sqlite error: %s\\n\",err); sqlite3_free(err); } return rc==0?0:-1; }\n",
  s2("GVal glyph_db_query_rows(GVal h, GVal ss) \{ sqlite3* db=(sqlite3*)(void*)h; char* sql=glyph_str_to_cstr(ss); sqlite3_stmt* st=0; int rc=sqlite3_prepare_v2(db,sql,-1,&st,0); free(sql); long long rcap=16,rcnt=0; long long* rows=(long long*)malloc(rcap*8); if(rc!=0)\{ long long* hd=(long long*)malloc(24); hd[0]=(long long)rows; hd[1]=0; hd[2]=rcap; if(st) sqlite3_finalize(st); return(GVal)hd; } int cc=sqlite3_column_count(st); while(sqlite3_step(st)==100)\{ long long* cd=(long long*)malloc(cc*8); int c; for(c=0;c<cc;c++)\{ int t=sqlite3_column_type(st,c); if(t==5) cd[c]=(GVal)glyph_cstr_to_str(\"\"); else if(t==1)\{ long long v=sqlite3_column_int64(st,c); char buf[32]; snprintf(buf,32,\"%lld\",v); cd[c]=(GVal)glyph_cstr_to_str(buf); } else\{ const char* tx=(const char*)sqlite3_column_text(st,c); cd[c]=(GVal)glyph_cstr_to_str(tx?tx:\"\"); } } long long* rh=(long long*)malloc(24); rh[0]=(long long)cd; rh[1]=(long long)cc; rh[2]=(long long)cc; if(rcnt>=rcap)\{ rcap*=2; long long* nr=(long long*)malloc(rcap*8); memcpy(nr,rows,rcnt*8); free(rows); rows=nr; } rows[rcnt++]=(long long)rh; } sqlite3_finalize(st); long long* hd=(long long*)malloc(24); hd[0]=(long long)rows; hd[1]=rcnt; hd[2]=rcap; return(GVal)hd; }\n",
  "GVal glyph_db_query_one(GVal h, GVal ss) \{ sqlite3* db=(sqlite3*)(void*)h; char* sql=glyph_str_to_cstr(ss); sqlite3_stmt* st=0; int rc=sqlite3_prepare_v2(db,sql,-1,&st,0); free(sql); if(rc!=0||sqlite3_step(st)!=100)\{ if(st) sqlite3_finalize(st); return glyph_cstr_to_str(\"\"); } const char* tx=(const char*)sqlite3_column_text(st,0); GVal r=glyph_cstr_to_str(tx?tx:\"\"); sqlite3_finalize(st); return r; }\n")))))

-- fn cg_sb_appends
cg_sb_appends ops i =
  match i >= glyph_array_len(ops)
    true -> ""
    _ ->
      op = ops[i]
      append = s3("glyph_sb_append(__sb, ", cg_operand(op), "); ")
      s2(append, cg_sb_appends(ops, i + 1))

-- fn cg_sig_params
cg_sig_params n i =
  match i >= n
    true -> ""
    _ ->
      match i == 0
        true -> s2(gval_t(), cg_sig_params(n, i + 1))
        _ -> s3(", ", gval_t(), cg_sig_params(n, i + 1))

-- fn cg_stmt
cg_stmt stmt =
  k = stmt.skind
  match k
    _ ? k == rv_use() -> s4("  ", cg_local(stmt.sdest), " = ", s2(cg_operand(stmt.sop1), ";\n"))
    _ ? k == rv_binop() -> s6("  ", cg_local(stmt.sdest), " = ", cg_operand(stmt.sop1), cg_binop_str(stmt.sival), s2(cg_operand(stmt.sop2), ";\n"))
    _ ? k == rv_unop() -> s5("  ", cg_local(stmt.sdest), " = ", cg_unop_str(stmt.sival), s2(cg_operand(stmt.sop1), ";\n"))
    _ ? k == rv_call() -> cg_call_stmt(stmt)
    _ ? k == rv_field() -> cg_field_stmt(stmt)
    _ ? k == rv_index() -> cg_index_stmt(stmt)
    _ ? k == rv_aggregate() -> cg_aggregate_stmt(stmt)
    _ ? k == rv_str_interp() -> cg_str_interp_stmt(stmt)
    _ ? k == rv_make_closure() -> cg_closure_stmt(stmt)
    _ -> s3("  /* unknown rvalue kind ", itos(k), " */\n")

-- fn cg_stmt2
cg_stmt2 stmt struct_map local_struct_types mir_local_types =
  k = stmt.skind
  match k
    _ ? k == rv_field() -> cg_field_stmt2(stmt, local_struct_types, struct_map, mir_local_types)
    _ ? k == rv_aggregate() -> cg_aggregate_stmt2(stmt, struct_map)
    _ ? k == rv_binop() ->
      match mir_local_types[stmt.sdest] == 3
        true -> cg_float_binop(stmt)
        _ ->
          f1 = is_op_float(mir_local_types, stmt.sop1)
          f2 = is_op_float(mir_local_types, stmt.sop2)
          match f1 == 1 || f2 == 1
            true ->
              match is_arith_op(stmt.sival) == 1
                true ->
                  _ = glyph_array_set(mir_local_types, stmt.sdest, 3)
                  cg_float_binop(stmt)
                _ -> cg_float_cmp(stmt)
            _ -> cg_stmt(stmt)
    _ ? k == rv_unop() ->
      match mir_local_types[stmt.sdest] == 3
        true -> cg_float_unop(stmt)
        _ -> cg_stmt(stmt)
    _ -> cg_stmt(stmt)

-- fn cg_store_ops
cg_store_ops ops i ptr_name =
  match i >= glyph_array_len(ops)
    true -> ""
    _ ->
      op = ops[i]
      store = s6(ptr_name, "[", itos(i), "] = ", cg_operand(op), "; ")
      s2(store, cg_store_ops(ops, i + 1, ptr_name))

-- fn cg_store_ops_alpha
cg_store_ops_alpha ops names i varname =
  match i >= glyph_array_len(ops)
    true -> ""
    _ ->
      pos = alpha_rank(names, names[i])
      rest = cg_store_ops_alpha(ops, names, i + 1, varname)
      s5(varname, "[", itos(pos), "] = ", s3(cg_operand(ops[i]), "; ", rest))

-- fn cg_store_ops_offset
cg_store_ops_offset ops i ptr_name offset =
  match i >= glyph_array_len(ops)
    true -> ""
    _ ->
      op = ops[i]
      store = s6(ptr_name, "[", itos(i + offset), "] = ", cg_operand(op), "; ")
      s2(store, cg_store_ops_offset(ops, i + 1, ptr_name, offset))

-- fn cg_str_interp_stmt
cg_str_interp_stmt stmt =
  lb = cg_lbrace()
  rb = cg_rbrace()
  gv = gval_t()
  appends = cg_sb_appends(stmt.sops, 0)
  s7("  ", lb, s2(" ", gv), " __sb = glyph_sb_new(); ", s2(appends, cg_local(stmt.sdest)), s3(" = (", gv, ")glyph_sb_build(__sb); "), s2(rb, "\n"))

-- fn cg_str_literal
cg_str_literal s =
  s4("(", gval_t(), ")glyph_cstr_to_str(\"", s2(cg_escape_str(process_escapes(s)), "\")"))

-- fn cg_struct_fields
cg_struct_fields fields types i =
  match i >= glyph_array_len(fields)
    true -> ""
    _ ->
      ftype = match glyph_array_len(types) > i
        true -> types[i]
        _ -> gval_t()
      s4(ftype, " ", fields[i], s2("; ", cg_struct_fields(fields, types, i + 1)))

-- fn cg_struct_stores
cg_struct_stores ops names i =
  match i >= glyph_array_len(ops)
    true -> ""
    _ -> s4("__r->", names[i], s2(" = ", cg_operand(ops[i])), s2("; ", cg_struct_stores(ops, names, i + 1)))

-- fn cg_struct_stores_typed
cg_struct_stores_typed ops names types i =
  match i >= glyph_array_len(ops)
    true -> ""
    _ ->
      ftype = match glyph_array_len(types) > i
        true -> types[i]
        _ -> gval_t()
      val = cg_operand(ops[i])
      store = match glyph_str_eq(ftype, gval_t())
        true -> s4("__r->", names[i], " = ", s2(val, "; "))
        _ -> match is_float_ctype(ftype) == 1
          true -> s4("__r->", names[i], s3(" = (", ftype, ")_glyph_i2f("), s2(val, "); "))
          _ -> s4("__r->", names[i], s3(" = (", ftype, ")"), s2(val, "; "))
      s2(store, cg_struct_stores_typed(ops, names, types, i + 1))


-- fn cg_struct_typedef
cg_struct_typedef entry =
  name = entry[0]
  fields = entry[1]
  types = entry[2]
  body = cg_struct_fields(fields, types, 0)
  lb = cg_lbrace()
  rb = cg_rbrace()
  nl = "\n"
  s7("typedef struct ", lb, " ", body, s2(rb, " Glyph_"), name, s2(";", nl))

-- fn cg_term
cg_term term =
  k = term.tkind
  match k
    _ ? k == tm_goto() -> s3("  goto ", cg_label(term.tgt1), ";\n")
    _ ? k == tm_return() -> s3("#ifdef GLYPH_DEBUG\n  _glyph_call_depth--;\n#endif\n  return ", cg_operand(term.top), ";\n")
    _ ? k == tm_branch() -> s6("  if (", cg_operand(term.top), ") goto ", cg_label(term.tgt1), "; else goto ", s2(cg_label(term.tgt2), ";\n"))
    _ -> "  __builtin_trap();\n"

-- fn cg_test_dispatch
cg_test_dispatch names =
  lb = cg_lbrace()
  rb = cg_rbrace()
  s2(s5("static int should_run(const char* name, int argc, char** argv) ", lb, "\n  if (argc <= 1) return 1;\n  for (int i = 1; i < argc; i++) if (strcmp(argv[i], name) == 0) return 1;\n  return 0;\n", rb, "\n\n"),
  s2(s5("int main(int argc, char** argv) ", lb, "\n  int passed = 0, failed = 0;\n", cg_test_dispatch_case(names, 0), ""),
  s5("  printf(\"%d/%d passed\\n\", passed, passed + failed);\n  return failed > 0 ? 1 : 0;\n", rb, "\n", "", "")))

-- fn cg_test_dispatch_case
cg_test_dispatch_case names i =
  match i >= glyph_array_len(names)
    true -> ""
    _ ->
      name = names[i]
      lb = cg_lbrace()
      rb = cg_rbrace()
      s2(s7("  if (should_run(\"", name, "\", argc, argv)) ", lb, "\n    _test_name = \"", name, "\"; _test_failed = 0;\n"),
      s2(s5("    ", name, "(0);\n    if (_test_failed) ", lb, " failed++; "),
      s2(s7(rb, " else ", lb, " printf(\"PASS %s\\n\", \"", name, "\"); passed++; ", rb),
      s2("\n  ", s2(rb, s2("\n", cg_test_dispatch_case(names, i + 1)))))))

-- fn cg_test_dispatch_cover
cg_test_dispatch_cover names =
  lb = cg_lbrace()
  rb = cg_rbrace()
  s2(s5("static int should_run(const char* name, int argc, char** argv) ", lb, "\n  if (argc <= 1) return 1;\n  for (int i = 1; i < argc; i++) if (strcmp(argv[i], name) == 0) return 1;\n  return 0;\n", rb, "\n\n"),
  s2(s5("int main(int argc, char** argv) ", lb, "\n#ifdef GLYPH_COVERAGE\n  atexit(_glyph_cov_write);\n#endif\n  int passed = 0, failed = 0;\n", cg_test_dispatch_case(names, 0), ""),
  s5("  printf(\"%d/%d passed\\n\", passed, passed + failed);\n  return failed > 0 ? 1 : 0;\n", rb, "\n", "", "")))


-- fn cg_test_forward_decls
cg_test_forward_decls names i =
  match i >= glyph_array_len(names)
    true -> ""
    _ ->
      gt = gval_t()
      s2(s5(gt, " ", names[i], s3("(", gt, ");\n"), ""), cg_test_forward_decls(names, i + 1))

-- fn cg_test_preamble_extra
cg_test_preamble_extra =
  "extern GVal glyph_assert(GVal cond);\nextern GVal glyph_assert_eq(GVal a, GVal b);\nextern GVal glyph_assert_str_eq(GVal a, GVal b);\n\n"

-- fn cg_test_program
cg_test_program fn_mirs test_mirs test_names =
  s7(cg_preamble(), cg_test_preamble_extra(0), cg_forward_decls(fn_mirs, 0), cg_test_forward_decls(test_names, 0), cg_functions(fn_mirs, 0), cg_functions(test_mirs, 0), s2("\n", cg_test_dispatch(test_names)))


-- fn cg_test_program
cg_test_program fn_mirs test_mirs test_names struct_map =
  s7(cg_preamble(), s2(cg_all_typedefs(struct_map), cg_test_preamble_extra), s2(cg_forward_decls(fn_mirs, 0), cg_test_forward_decls(test_names, 0)), cg_functions2(fn_mirs, 0, struct_map), cg_functions2(test_mirs, 0, struct_map), "\n", cg_test_dispatch(test_names))

-- fn cg_test_program_cover
cg_test_program_cover fn_mirs test_mirs test_names struct_map cover_path =
  cov_names = cg_cov_collect_names(fn_mirs, test_mirs)
  cov_runtime = cg_runtime_coverage(cover_path, cov_names)
  s7(cg_preamble(), s3(cg_all_typedefs(struct_map), cov_runtime, cg_test_preamble_extra), s2(cg_forward_decls(fn_mirs, 0), cg_test_forward_decls(test_names, 0)), cg_functions2_cover(fn_mirs, 0, struct_map, cov_names), cg_functions2_cover(test_mirs, 0, struct_map, cov_names), "\n", cg_test_dispatch_cover(test_names))


-- fn cg_test_runtime
cg_test_runtime =
  lb = cg_lbrace()
  rb = cg_rbrace()
  s2("static int _test_failed = 0;\nstatic const char* _test_name = \"\";\nGVal glyph_assert(GVal cond) ", s2(lb, s2(" if (!cond) ", s2(lb, s2(" fprintf(stderr, \"  FAIL %s: assertion failed\\n\", _test_name); _test_failed = 1; ", s2(rb, s2(" return 0;\n", s2(rb, s2("\nGVal glyph_assert_eq(GVal a, GVal b) ", s2(lb, s2(" if (a != b) ", s2(lb, s2(" fprintf(stderr, \"  FAIL %s: %lld != %lld\\n\", _test_name, (long long)a, (long long)b); _test_failed = 1; ", s2(rb, s2(" return 0;\n", s2(rb, s2("\nGVal glyph_assert_str_eq(GVal a, GVal b) ", s2(lb, s2(" if (!glyph_str_eq((long long)a, (long long)b)) ", s2(lb, s2(" fprintf(stderr, \"  FAIL %s: strings differ\\n\", _test_name); _test_failed = 1; ", s2(rb, s2(" return 0;\n", s2(rb, "\n"))))))))))))))))))))))))

-- fn cg_unop_str
cg_unop_str op =
  match op == mir_neg()
    true -> "-"
    _ -> "!"

-- fn cg_variant_aggregate
cg_variant_aggregate stmt nops =
  lb = cg_lbrace()
  rb = cg_rbrace()
  gv = gval_t()
  total_size = (nops + 1) * 8
  disc = variant_discriminant(stmt.sstr)
  stores = cg_store_ops_offset(stmt.sops, 0, "__v", 1)
  s7("  ", lb, s4(" ", gv, "* __v = (", gv), s2("*)glyph_alloc(", itos(total_size)), s2("); __v[0] = ", itos(disc)), s2("; ", stores), s4(cg_local(stmt.sdest), s3(" = (", gv, ")__v; "), rb, "\n"))

-- fn cg_wrap_call_args
cg_wrap_call_args n i =
  match i >= n
    true -> ""
    _ ->
      arg = s2("_", itos(i))
      match i + 1 >= n
        true -> arg
        _ -> s3(arg, ", ", cg_wrap_call_args(n, i + 1))

-- fn cg_wrap_params
cg_wrap_params n i =
  match i >= n
    true -> ""
    _ ->
      param = s3(gval_t(), " _", itos(i))
      match i + 1 >= n
        true -> param
        _ -> s3(param, ", ", cg_wrap_params(n, i + 1))

-- fn check_parse_errors
check_parse_errors parsed = cpe_loop(parsed, 0, 0)

-- fn check_tok
check_tok tokens pos kind = cur_kind(tokens, pos) == kind

-- fn cmd_build
cmd_build argv argc =
  match argc >= 3
    true ->
      match has_flag(argv, "--emit-mir", 3) == 1
        true ->
          emit_mir_db(argv[2])
          0
        _ ->
          db_path = argv[2]
          default_out = strip_ext(db_path)
          output = match argc >= 4
            true ->
              match glyph_str_char_at(argv[3], 0) == 45
                true -> default_out
                _ -> argv[3]
            _ -> default_out
          mode = match has_flag(argv, "--debug", 3) == 1
            true -> 1
            _ -> match has_flag(argv, "--release", 3) == 1
              true -> 2
              _ -> 0
          gen_flag = find_flag(argv, "--gen", 3)
          gen_str = match glyph_str_len(gen_flag) > 0
            true -> gen_flag
            _ -> "1"
          compile_db(db_path, output, mode, gen_str)
          0
    _ ->
      eprintln("Usage: glyph build <db> [output] [--debug|--release] [--gen N]")
      1

-- fn cmd_check
cmd_check argv argc =
  match argc >= 3
    true ->
      db_path = argv[2]
      gen_flag = find_flag(argv, "--gen", 3)
      gen_str = match glyph_str_len(gen_flag) > 0
        true -> gen_flag
        _ -> "1"
      db = glyph_db_open(db_path)
      sources = read_fn_defs_gen(db, gen_str)
      glyph_db_close(db)
      parsed = parse_all_fns(sources, 0)
      n_errs = check_parse_errors(parsed)
      eng = mk_engine()
      register_builtins(eng)
      tc_pre_register(eng, parsed, 0)
      tc_infer_all_tc(eng, parsed, 0)
      tc_report_errors(eng)
      n_tc_errs = glyph_array_len(eng.errors)
      println(s5("Checked ", itos(glyph_array_len(parsed)), " definitions: ", itos(n_errs), s2(" parse errors, ", s2(itos(n_tc_errs), " type warnings"))))
    _ -> eprintln("Usage: glyph check <db.glyph> [--gen N]")


-- fn cmd_cover
cmd_cover argv argc =
  match argc >= 3
    true ->
      db_path = argv[2]
      cover_path = s2(db_path, ".cover")
      content = glyph_read_file(cover_path)
      match glyph_str_len(content) == 0
        true ->
          eprintln(s2("No coverage data found. Run: glyph test ", s2(db_path, " --cover")))
          1
        _ ->
          names = []
          hits = []
          parse_cover_lines(content, 0, names, hits)
          format_cover_report(names, hits)
          0
    _ ->
      eprintln("Usage: glyph cover <db>")
      1


-- fn cmd_deps
cmd_deps argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph deps <db> <name>")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      db = glyph_db_open(db_path)
      sql = s3("SELECT DISTINCT t.name FROM dep d JOIN def f ON d.from_id = f.id JOIN def t ON d.to_id = t.id WHERE f.name = '", sql_escape(name), "' ORDER BY t.name")
      rows = glyph_db_query_rows(db, sql)
      glyph_db_close(db)
      n = glyph_array_len(rows)
      match n == 0
        true ->
          println(s2(name, " -> (none)"))
          0
        _ ->
          deps_str = join_names(rows, 0, n, "")
          println(s3(name, " -> ", deps_str))
          0

-- fn cmd_dump
cmd_dump argv argc =
  match argc < 3
    true ->
      eprintln("Usage: glyph dump <db> [--budget N] [--root NAME] [--all] [--sigs]")
      1
    _ ->
      db_path = argv[2]
      db = glyph_db_open(db_path)
      budget_str = find_flag(argv, "--budget", 3)
      root = find_flag(argv, "--root", 3)
      do_all = has_flag(argv, "--all", 3)
      sigs_only = has_flag(argv, "--sigs", 3)
      has_budget = glyph_str_len(budget_str) > 0
      has_root = glyph_str_len(root) > 0
      match has_budget == 0
        true -> match has_root == 0
          true -> dump_all(db, sigs_only)
          _ -> dump_budgeted(db, root, 500, sigs_only)
        _ ->
          budget = glyph_str_to_int(budget_str)
          root_name = match has_root
            true -> root
            _ -> "main"
          dump_budgeted(db, root_name, budget, sigs_only)

-- fn cmd_extern_add
cmd_extern_add argv argc =
  match argc < 6
    true ->
      eprintln("Usage: glyph extern <db> <name> <symbol> <sig> [--lib L]")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      symbol = argv[4]
      sig = argv[5]
      lib = find_flag(argv, "--lib", 6)
      db = glyph_db_open(db_path)
      lib_val = match glyph_str_len(lib) > 0
        true -> s3("'", sql_escape(lib), "'")
        _ -> "NULL"
      sql = s7("INSERT OR REPLACE INTO extern_ (name, symbol, lib, sig, conv) VALUES ('", sql_escape(name), "', '", sql_escape(symbol), "', ", lib_val, s3(", '", sql_escape(sig), "', 'C')"))
      glyph_db_exec(db, sql)
      glyph_db_close(db)
      match glyph_str_len(lib) > 0
        true -> println(s5("extern ", name, " (", s3(lib, ":", symbol), ")"))
        _ -> println(s3("extern ", name, s2(" (", s2(symbol, ")"))))
      0

-- fn cmd_find
cmd_find argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph find <db> <pattern> [--body]")
      1
    _ ->
      db_path = argv[2]
      pat = argv[3]
      search_body = has_flag(argv, "--body", 4)
      db = glyph_db_open(db_path)
      esc_pat = sql_escape(pat)
      sql = match search_body == 1
        true -> s5("SELECT kind, name, COALESCE(sig,'-'), tokens FROM def WHERE name LIKE '%", esc_pat, "%' OR body LIKE '%", esc_pat, "%' ORDER BY kind, name")
        _ -> s3("SELECT kind, name, COALESCE(sig,'-'), tokens FROM def WHERE name LIKE '%", esc_pat, "%' ORDER BY kind, name")
      rows = glyph_db_query_rows(db, sql)
      glyph_db_close(db)
      print_ls_rows(rows, 0, glyph_array_len(rows))
      0

-- fn cmd_get
cmd_get argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph get <db> <name> [--kind K]")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      db = glyph_db_open(db_path)
      kind = find_flag(argv, "--kind", 4)
      sql = match glyph_str_len(kind) > 0
        true -> s7("SELECT body FROM def WHERE name = '", sql_escape(name), "' AND kind = '", sql_escape(kind), "'", "", "")
        _ -> s3("SELECT body FROM def WHERE name = '", sql_escape(name), "'")
      rows = glyph_db_query_rows(db, sql)
      glyph_db_close(db)
      n = glyph_array_len(rows)
      match n == 0
        true ->
          eprintln(s3("error: '", name, "' not found"))
          1
        _ ->
          match n > 1
            true ->
              eprintln(s5("error: ", itos(n), " defs named '", name, "'. Use --kind"))
              6
            _ ->
              row = rows[0]
              println(row[0])
              0

-- fn cmd_history
cmd_history argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph history <db> <name> [--kind K]")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      db = glyph_db_open(db_path)
      _m = migrate_history(db)
      kind = find_flag(argv, "--kind", 4)
      sql = match glyph_str_len(kind) > 0
        true -> s7("SELECT id, kind, gen, changed_at, length(body) FROM def_history WHERE name = '", sql_escape(name), "' AND kind = '", sql_escape(kind), "' ORDER BY id DESC", "", "")
        _ -> s5("SELECT id, kind, gen, changed_at, length(body) FROM def_history WHERE name = '", sql_escape(name), "' ORDER BY id DESC", "", "")
      rows = glyph_db_query_rows(db, sql)
      glyph_db_close(db)
      n = glyph_array_len(rows)
      match n == 0
        true ->
          println(s3("No history for '", name, "'"))
          0
        _ ->
          println(s5("History for '", name, "' (", glyph_int_to_str(n), " entries):"))
          print_history_rows(rows, 0, n)

-- fn cmd_init
cmd_init argv argc =
  match argc < 3
    true ->
      eprintln("Usage: glyph init <db>")
      1
    _ ->
      db = glyph_db_open(argv[2])
      glyph_db_exec(db, init_schema())
      glyph_db_close(db)
      println(s2("Created ", argv[2]))
      0

-- fn cmd_ls
cmd_ls argv argc =
  match argc < 3
    true ->
      eprintln("Usage: glyph ls <db> [--kind K] [--sort S]")
      1
    _ ->
      db_path = argv[2]
      db = glyph_db_open(db_path)
      kind = find_flag(argv, "--kind", 3)
      sort = find_flag(argv, "--sort", 3)
      base = match glyph_str_len(kind) > 0
        true -> s3("SELECT kind, name, COALESCE(sig,'-'), tokens FROM def WHERE kind = '", sql_escape(kind), "'")
        _ -> "SELECT kind, name, COALESCE(sig,'-'), tokens FROM def"
      order = match glyph_str_eq(sort, "tokens") == 1
        true -> " ORDER BY CAST(tokens AS INTEGER)"
        _ ->
          match glyph_str_eq(sort, "name") == 1
            true -> " ORDER BY name"
            _ -> " ORDER BY kind, name"
      rows = glyph_db_query_rows(db, s2(base, order))
      glyph_db_close(db)
      print_ls_rows(rows, 0, glyph_array_len(rows))
      0

-- fn cmd_mcp
cmd_mcp argv argc =
  match argc < 3
    true ->
      eprintln("Usage: glyph mcp <db_path>")
      1
    _ ->
      db_path = argv[2]
      mcp_loop(db_path)

-- fn cmd_put
cmd_put argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph put <db> <kind> -b <body> [--gen N]")
      eprintln("       glyph put <db> <kind> -f <file> [--gen N]")
      1
    _ ->
      db_path = argv[2]
      kind = argv[3]
      body_flag = find_flag(argv, "-b", 4)
      file_flag = find_flag(argv, "-f", 4)
      gen_str = find_flag(argv, "--gen", 4)
      gen = match glyph_str_len(gen_str) > 0
        true -> glyph_str_to_int(gen_str)
        _ -> 0
      body = match glyph_str_len(body_flag) > 0
        true -> body_flag
        _ ->
          match glyph_str_len(file_flag) > 0
            true -> glyph_read_file(file_flag)
            _ -> ""
      match glyph_str_len(body) == 0
        true ->
          eprintln("error: body required via -b or -f")
          3
        _ ->
          match glyph_str_eq(kind, "fn")
            true ->
              vr = validate_def(body)
              match vr.vr_ok == 0
                true ->
                  msg = match glyph_str_len(vr.vr_msg) > 0
                    true -> vr.vr_msg
                    _ -> "parse error"
                  eprintln(s2("error: ", msg))
                  eprintln(format_parse_err(body, vr.vr_tokens, vr.vr_pos, msg))
                  1
                _ ->
                  name = extract_name(body)
                  do_put(db_path, kind, name, body, gen)
            _ ->
              name = extract_name(body)
              do_put(db_path, kind, name, body, gen)

-- fn cmd_rdeps
cmd_rdeps argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph rdeps <db> <name>")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      db = glyph_db_open(db_path)
      sql = s3("SELECT DISTINCT f.name FROM dep d JOIN def f ON d.from_id = f.id JOIN def t ON d.to_id = t.id WHERE t.name = '", sql_escape(name), "' ORDER BY f.name")
      rows = glyph_db_query_rows(db, sql)
      glyph_db_close(db)
      n = glyph_array_len(rows)
      names = join_names(rows, 0, n, "")
      match n == 0
        true ->
          println("(none)")
          0
        _ ->
          println(names)
          0

-- fn cmd_rm
cmd_rm argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph rm <db> <name> [--kind K] [--force]")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      db = glyph_db_open(db_path)
      _m = migrate_history(db)
      kind = find_flag(argv, "--kind", 4)
      force = has_flag(argv, "--force", 4)
      sql = match glyph_str_len(kind) > 0
        true -> s7("SELECT id, kind FROM def WHERE name = '", sql_escape(name), "' AND kind = '", sql_escape(kind), "'", "", "")
        _ -> s3("SELECT id, kind FROM def WHERE name = '", sql_escape(name), "'")
      rows = glyph_db_query_rows(db, sql)
      n = glyph_array_len(rows)
      match n == 0
        true ->
          glyph_db_close(db)
          eprintln(s3("error: '", name, "' not found"))
          1
        _ ->
          match n > 1
            true ->
              glyph_db_close(db)
              eprintln(s3("error: ambiguous '", name, "'. Use --kind"))
              6
            _ ->
              row = rows[0]
              def_id = row[0]
              def_kind = row[1]
              rm_check_rdeps(db, def_id, def_kind, name, force)

-- fn cmd_run
cmd_run argv argc =
  match argc >= 3
    true ->
      output = "/tmp/glyph_run_tmp"
      compile_db(argv[2], output)
      glyph_system(output)
    _ ->
      eprintln("Usage: glyph run <db>")
      1

-- fn cmd_run
cmd_run argv argc =
  match argc >= 3
    true ->
      output = "/tmp/glyph_run_tmp"
      gen_flag = find_flag(argv, "--gen", 3)
      gen_str = match glyph_str_len(gen_flag) > 0
        true -> gen_flag
        _ -> "1"
      compile_db(argv[2], output, 0, gen_str)
      glyph_system(output)
    _ ->
      eprintln("Usage: glyph run <db> [--gen N]")
      1

-- fn cmd_sql
cmd_sql argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph sql <db> <query>")
      1
    _ ->
      db_path = argv[2]
      query = argv[3]
      db = glyph_db_open(db_path)
      first6 = match glyph_str_len(query) >= 6
        true -> glyph_str_slice(query, 0, 6)
        _ -> ""
      is_select = match glyph_str_eq(first6, "SELECT") == 1
        true -> 1
        _ ->
          match glyph_str_eq(first6, "select") == 1
            true -> 1
            _ ->
              match glyph_str_eq(first6, "Select") == 1
                true -> 1
                _ -> 0
      match is_select == 1
        true ->
          rows = glyph_db_query_rows(db, query)
          glyph_db_close(db)
          print_sql_rows(rows, 0, glyph_array_len(rows))
          0
        _ ->
          rc = glyph_db_exec(db, query)
          glyph_db_close(db)
          match rc == 0
            true -> 0
            _ ->
              eprintln("error: SQL execution failed")
              5

-- fn cmd_stat
cmd_stat argv argc =
  match argc < 3
    true ->
      eprintln("Usage: glyph stat <db>")
      1
    _ ->
      db_path = argv[2]
      db = glyph_db_open(db_path)
      kind_rows = glyph_db_query_rows(db, "SELECT kind, COUNT(*) FROM def GROUP BY kind ORDER BY COUNT(*) DESC")
      total_row = glyph_db_query_one(db, "SELECT COUNT(*) FROM def")
      token_row = glyph_db_query_one(db, "SELECT COALESCE(SUM(tokens),0) FROM def")
      dirty_row = glyph_db_query_one(db, "SELECT COUNT(*) FROM def WHERE compiled = 0")
      ext_row = glyph_db_query_one(db, "SELECT COUNT(*) FROM extern_")
      glyph_db_close(db)
      kinds_str = format_kind_counts(kind_rows, 0, glyph_array_len(kind_rows), "")
      line = s7(total_row, " defs (", kinds_str, ")  ", ext_row, " extern  ", s4(token_row, "tok total  ", dirty_row, " dirty"))
      println(line)
      0

-- fn cmd_test
cmd_test argv argc =
  match argc >= 3
    true ->
      db_path = argv[2]
      db = glyph_db_open(db_path)
      test_names = read_test_names(db)
      test_sources = read_test_defs(db)
      n = glyph_array_len(test_names)
      match n == 0
        true ->
          glyph_db_close(db)
          println("0 tests found")
          0
        _ ->
          fn_sources = read_fn_defs(db)
          externs = read_externs(db)
          glyph_db_close(db)
          println(s3("Compiling ", itos(n), " tests..."))
          bin_path = "/tmp/glyph_test_bin"
          build_test_program(fn_sources, test_sources, test_names, externs, bin_path)
          cmd = match argc >= 4
            true -> s3(bin_path, " ", join_args(argv, 3, argc))
            _ -> bin_path
          glyph_system(cmd)
    _ ->
      eprintln("Usage: glyph test <db> [test_name...]")
      1

-- fn cmd_test
cmd_test argv argc =
  match argc >= 3
    true ->
      db_path = argv[2]
      gen_flag = find_flag(argv, "--gen", 3)
      gen_str = match glyph_str_len(gen_flag) > 0
        true -> gen_flag
        _ -> "1"
      db = glyph_db_open(db_path)
      test_names = read_test_names_gen(db, gen_str)
      test_sources = read_test_defs_gen(db, gen_str)
      n = glyph_array_len(test_names)
      match n == 0
        true ->
          glyph_db_close(db)
          println("0 tests found")
          0
        _ ->
          fn_sources = read_fn_defs_gen(db, gen_str)
          externs = read_externs(db)
          type_rows = read_type_defs_gen(db, gen_str)
          glyph_db_close(db)
          struct_map = build_struct_map(type_rows)
          println(s3("Compiling ", itos(n), " tests..."))
          bin_path = "/tmp/glyph_test_bin"
          mode = match has_flag(argv, "--debug", 3) == 1
            true -> 1
            _ -> match has_flag(argv, "--release", 3) == 1
              true -> 2
              _ -> 0
          cover_flag = has_flag(argv, "--cover", 3)
          cover_path = match cover_flag == 1
            true -> s2(db_path, ".cover")
            _ -> ""
          build_test_program(fn_sources, test_sources, test_names, externs, bin_path, struct_map, mode, cover_path)
          test_args = filter_test_args(argv, 3, argc)
          cmd = match glyph_array_len(test_args) > 0
            true -> s3(bin_path, " ", join_str_arr(test_args, 0))
            _ -> bin_path
          result = glyph_system(cmd)
          match cover_flag == 1
            true ->
              println(s2("Coverage data written to ", cover_path))
              result
            _ -> result
    _ ->
      eprintln("Usage: glyph test <db> [test_name...] [--debug|--release] [--gen N] [--cover]")
      1


-- fn cmd_undo
cmd_undo argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph undo <db> <name> [--kind K]")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      db = glyph_db_open(db_path)
      _m = migrate_history(db)
      kind = find_flag(argv, "--kind", 4)
      do_undo(db, name, kind)

-- fn coerce_to_float
coerce_to_float ctx op =
  ot = op_type(ctx, op)
  match ot == 3
    true -> op
    _ ->
      dest = mir_alloc_local(ctx, "")
      mir_set_lt(ctx, dest, 3)
      mir_emit_call(ctx, dest, mk_op_func("glyph_int_to_float"), [op])
      mk_op_local(dest)


-- fn coll_acc_blks
coll_acc_blks blocks bi la =
  match bi >= glyph_array_len(blocks)
    true -> 0
    _ ->
      coll_acc_stmts(blocks[bi], 0, la)
      coll_acc_blks(blocks, bi + 1, la)

-- fn coll_acc_stmts
coll_acc_stmts stmts si la =
  match si >= glyph_array_len(stmts)
    true -> 0
    _ ->
      stmt = stmts[si]
      match stmt.skind == rv_field()
        true ->
          base_kind = stmt.sop1.okind
          match base_kind == ok_local()
            true ->
              local_id = stmt.sop1.oval
              match local_id >= 0
                true -> match local_id < glyph_array_len(la)
                  true ->
                    field_list = la[local_id]
                    match has_str_in(field_list, stmt.sstr)
                      true -> 0
                      _ -> glyph_array_push(field_list, stmt.sstr)
                  _ -> 0
                _ -> 0
            _ -> 0
        _ -> 0
      coll_acc_stmts(stmts, si + 1, la)

-- fn coll_local_acc
coll_local_acc mir =
  nlocals = glyph_array_len(mir.fn_locals)
  la = make_empty_2d(nlocals, 0)
  coll_acc_blks(mir.fn_blocks_stmts, 0, la)
  la

-- fn collect_dep_pairs
collect_dep_pairs mirs =
  pairs = []
  cdp_mirs(mirs, 0, pairs)
  pairs

-- fn collect_free_vars
collect_free_vars ctx ast body_idx params =
  bound = []
  cfv_add_params(bound, ast, params, 0)
  seen = []
  result = []
  walk_free_vars(ctx, ast, body_idx, bound, seen, result)
  result


-- fn collect_libs
collect_libs externs =
  collect_libs_loop(externs, 0, [], "")

-- fn collect_libs_loop
collect_libs_loop externs i seen result =
  match i >= glyph_array_len(externs)
    true -> result
    _ ->
      row = externs[i]
      lib = row[3]
      match glyph_str_len(lib) == 0
        true -> collect_libs_loop(externs, i + 1, seen, result)
        _ ->
          match lib_seen(seen, lib, 0)
            true -> collect_libs_loop(externs, i + 1, seen, result)
            _ ->
              glyph_array_push(seen, lib)
              new_result = s3(result, " -l", lib)
              collect_libs_loop(externs, i + 1, seen, new_result)

-- fn collect_nested_lifted
collect_nested_lifted dst src i =
  match i >= glyph_array_len(src)
    true -> 0
    _ ->
      glyph_array_push(dst, src[i])
      collect_nested_lifted(dst, src, i + 1)


-- fn compile_db
compile_db db_path output_path =
  db = glyph_db_open(db_path)
  sources = read_fn_defs(db)
  externs = read_externs(db)
  glyph_db_close(db)
  n = glyph_array_len(sources)
  println(s3("Compiling ", itos(n), " definitions..."))
  build_program(sources, externs, output_path)
  println(s2("Built ", output_path))

-- fn compile_db
compile_db db_path output_path mode gen_str =
  db = glyph_db_open(db_path)
  sources = read_fn_defs_gen(db, gen_str)
  externs = read_externs(db)
  type_rows = read_type_defs_gen(db, gen_str)
  glyph_db_close(db)
  struct_map = build_struct_map(type_rows)
  n = glyph_array_len(sources)
  println(s3("Compiling ", itos(n), " definitions..."))
  mirs = build_program(sources, externs, output_path, struct_map, mode)
  pairs = collect_dep_pairs(mirs)
  insert_deps(db_path, pairs)
  println(s2("Built ", output_path))

-- fn compile_fn
compile_fn src za_fns =
  tokens = tokenize(src)
  ast = []
  r = parse_fn_def(src, tokens, 0, ast)
  mir = lower_fn_def(ast, r.node, za_fns, mk_null_tctx(0))
  mir


-- fn compile_fn_parsed
compile_fn_parsed pf za_fns tctx =
  lower_fn_def(pf.pf_ast, pf.pf_fn_idx, za_fns, tctx)

-- fn compile_fn_to_c
compile_fn_to_c src =
  mir = compile_fn(src, [])
  cg_function(mir)

-- fn compile_fns
compile_fns sources i za_fns =
  match i >= glyph_array_len(sources)
    true -> []
    _ ->
      mir = compile_fn(sources[i], za_fns)
      rest = compile_fns(sources, i + 1, za_fns)
      collect_nested_lifted(rest, mir.fn_subs, 0)
      glyph_array_push(rest, mir)
      rest


-- fn compile_fns_parsed
compile_fns_parsed parsed i za_fns tc_results verbose =
  match i >= glyph_array_len(parsed)
    true -> []
    _ ->
      pf = parsed[i]
      match verbose
        true -> eprintln(s2("  compiling: ", pf.pf_name))
        _ -> 0
      tctx = match i < glyph_array_len(tc_results)
        true -> mk_tctx((tc_results[i]).fn_tmap)
        _ -> mk_null_tctx(0)
      mir = match pf.pf_fn_idx >= 0
        true -> compile_fn_parsed(pf, za_fns, tctx)
        _ -> compile_fn(pf.pf_src, za_fns)
      rest = compile_fns_parsed(parsed, i + 1, za_fns, tc_results, verbose)
      collect_nested_lifted(rest, mir.fn_subs, 0)
      glyph_array_push(rest, mir)
      rest


-- fn count_covered
count_covered hits i acc =
  match i >= glyph_array_len(hits)
    true -> acc
    _ ->
      match hits[i] > 0
        true -> count_covered(hits, i + 1, acc + 1)
        _ -> count_covered(hits, i + 1, acc)


-- fn cpe_loop
cpe_loop parsed i count =
  match i >= glyph_array_len(parsed)
    true -> count
    _ ->
      pf = parsed[i]
      new_count = match pf.pf_fn_idx < 0
        true ->
          msg = match glyph_str_len(pf.pf_err_msg) > 0
            true -> pf.pf_err_msg
            _ -> "parse error"
          eprintln(s2("error: ", msg))
          eprintln(format_parse_err(pf.pf_src, pf.pf_tokens, pf.pf_err_pos, msg))
          count + 1
        _ -> count
      cpe_loop(parsed, i + 1, new_count)

-- fn cur_ival
cur_ival src tokens pos =
  t = tokens[pos]
  glyph_str_to_int(glyph_str_slice(src, t.start, t.end))

-- fn cur_kind
cur_kind tokens pos =
  match pos < glyph_array_len(tokens)
    true -> (tokens[pos]).kind
    _ -> tk_eof()

-- fn cur_text
cur_text src tokens pos =
  match pos < glyph_array_len(tokens)
    true ->
      t = tokens[pos]
      glyph_str_slice(src, t.start, t.end)
    _ -> ""

-- fn df_fn
df_fn = 300

-- fn dispatch_cmd
dispatch_cmd argv argc cmd =
  match cmd
    _ ? glyph_str_eq(cmd, "build") -> cmd_build(argv, argc)
    _ ? glyph_str_eq(cmd, "run") -> cmd_run(argv, argc)
    _ ? glyph_str_eq(cmd, "get") -> cmd_get(argv, argc)
    _ ? glyph_str_eq(cmd, "put") -> cmd_put(argv, argc)
    _ ? glyph_str_eq(cmd, "rm") -> cmd_rm(argv, argc)
    _ ? glyph_str_eq(cmd, "ls") -> cmd_ls(argv, argc)
    _ ? glyph_str_eq(cmd, "find") -> cmd_find(argv, argc)
    _ ? glyph_str_eq(cmd, "stat") -> cmd_stat(argv, argc)
    _ ? glyph_str_eq(cmd, "deps") -> cmd_deps(argv, argc)
    _ ? glyph_str_eq(cmd, "rdeps") -> cmd_rdeps(argv, argc)
    _ -> dispatch_cmd2(argv, argc, cmd)

-- fn dispatch_cmd2
dispatch_cmd2 argv argc cmd =
  match cmd
    _ ? glyph_str_eq(cmd, "dump") -> cmd_dump(argv, argc)
    _ ? glyph_str_eq(cmd, "sql") -> cmd_sql(argv, argc)
    _ ? glyph_str_eq(cmd, "extern") -> cmd_extern_add(argv, argc)
    _ ? glyph_str_eq(cmd, "init") -> cmd_init(argv, argc)
    _ ? glyph_str_eq(cmd, "check") -> cmd_check(argv, argc)
    _ ? glyph_str_eq(cmd, "test") -> cmd_test(argv, argc)
    _ ? glyph_str_eq(cmd, "undo") -> cmd_undo(argv, argc)
    _ ? glyph_str_eq(cmd, "history") -> cmd_history(argv, argc)
    _ ? glyph_str_eq(cmd, "mcp") -> cmd_mcp(argv, argc)
    _ ? glyph_str_eq(cmd, "cover") -> cmd_cover(argv, argc)
    _ -> print_usage


-- fn do_put
do_put db_path kind name body gen =
  db = glyph_db_open(db_path)
  _m = migrate_history(db)
  use_gen = match gen > 0
    true -> gen
    _ ->
      max_q = s5("SELECT MAX(gen) FROM def WHERE name='", sql_escape(name), "' AND kind='", sql_escape(kind), "'")
      max_rows = glyph_db_query_rows(db, max_q)
      match glyph_array_len(max_rows) > 0
        true ->
          rv = max_rows[0]
          val = rv[0]
          match glyph_str_len(val) > 0
            true -> glyph_str_to_int(val)
            _ -> 1
        _ -> 1
  gen_s = int_to_str(use_gen)
  check_sql = s7("SELECT id, tokens FROM def WHERE name = '", sql_escape(name), "' AND kind = '", sql_escape(kind), "' AND gen=", gen_s, "")
  existing = glyph_db_query_rows(db, check_sql)
  n = glyph_array_len(existing)
  _del = match n > 0
    true ->
      old = existing[0]
      old_id = old[0]
      glyph_db_exec(db, s3("DELETE FROM def WHERE id = ", old_id, ""))
      0
    _ -> 0
  esc_body = sql_escape(body)
  vals = s7("'", sql_escape(name), "', '", sql_escape(kind), "', '", esc_body, "'")
  ins_sql = s5("INSERT INTO def (name, kind, body, hash, tokens, gen) VALUES (", vals, ", zeroblob(32), 0, ", gen_s, ")")
  glyph_db_exec(db, ins_sql)
  glyph_db_close(db)
  println(s3(kind, " ", name))
  0

-- fn do_undo
do_undo db name kind =
  sql = match glyph_str_len(kind) > 0
    true -> s7("SELECT id, kind, sig, body, gen FROM def_history WHERE name = '", sql_escape(name), "' AND kind = '", sql_escape(kind), "' ORDER BY id DESC LIMIT 1", "", "")
    _ -> s5("SELECT id, kind, sig, body, gen FROM def_history WHERE name = '", sql_escape(name), "' ORDER BY id DESC LIMIT 1", "", "")
  rows = glyph_db_query_rows(db, sql)
  n = glyph_array_len(rows)
  match n == 0
    true ->
      glyph_db_close(db)
      eprintln(s3("No history for '", name, "'"))
      1
    _ ->
      row = rows[0]
      hist_id = row[0]
      old_kind = row[1]
      old_sig = row[2]
      old_body = row[3]
      old_gen = row[4]
      del_sql = match glyph_str_len(kind) > 0
        true -> s7("DELETE FROM def WHERE name = '", sql_escape(name), "' AND kind = '", sql_escape(kind), "'", "", "")
        _ -> s3("DELETE FROM def WHERE name = '", sql_escape(name), "'")
      glyph_db_exec(db, del_sql)
      ins_sql = match glyph_str_len(old_sig) > 0
        true -> s7("INSERT INTO def (name, kind, sig, body, hash, tokens, gen) VALUES ('", sql_escape(name), s7("', '", sql_escape(old_kind), "', '", sql_escape(old_sig), "', '", sql_escape(old_body), s3("', zeroblob(32), 0, ", old_gen, ")")), "", "", "", "")
        _ -> s7("INSERT INTO def (name, kind, body, hash, tokens, gen) VALUES ('", sql_escape(name), s5("', '", sql_escape(old_kind), "', '", sql_escape(old_body), s3("', zeroblob(32), 0, ", old_gen, ")")), "", "", "", "")
      glyph_db_exec(db, ins_sql)
      glyph_db_exec(db, s3("DELETE FROM def_history WHERE id = ", hist_id, ""))
      glyph_db_close(db)
      println(s5("Restored '", name, "' (", old_kind, ")"))
      0

-- fn dump_all
dump_all db sigs_only =
  sql = match sigs_only == 1
    true -> "SELECT kind, name, COALESCE(sig,'-') FROM def ORDER BY kind, name"
    _ -> "SELECT kind, name, body FROM def ORDER BY kind, name"
  rows = glyph_db_query_rows(db, sql)
  glyph_db_close(db)
  dump_print_all(rows, 0, glyph_array_len(rows), sigs_only)
  0

-- fn dump_budgeted
dump_budgeted db root_name budget sigs_only =
  root_rows = glyph_db_query_rows(db, s3("SELECT id FROM def WHERE name = '", sql_escape(root_name), "'"))
  match glyph_array_len(root_rows) == 0
    true ->
      glyph_db_close(db)
      eprintln(s3("error: root '", root_name, "' not found"))
      1
    _ ->
      all_rows = glyph_db_query_rows(db, "SELECT kind, name, body, tokens FROM def ORDER BY kind, name")
      glyph_db_close(db)
      total = glyph_array_len(all_rows)
      println(s7("-- [", itos(budget), "tok budget from ", root_name, ", ", itos(total), " defs total]"))
      dump_within_budget(all_rows, 0, total, budget, 0, sigs_only)
      0

-- fn dump_print_all
dump_print_all rows i n sigs_only =
  match i < n
    true ->
      row = rows[i]
      match sigs_only == 1
        true -> println(s5("-- ", row[0], " ", row[1], s2(" : ", row[2])))
        _ ->
          println(s4("-- ", row[0], " ", row[1]))
          println(row[2])
          println("")
      dump_print_all(rows, i + 1, n, sigs_only)
    _ -> 0

-- fn dump_within_budget
dump_within_budget rows i n budget used sigs_only =
  match i < n
    true ->
      row = rows[i]
      tok = glyph_str_to_int(row[3])
      match used + tok <= budget
        true ->
          match sigs_only == 1
            true -> println(s5("-- ", row[0], " ", row[1], ""))
            _ -> println(row[2])
          println("")
          dump_within_budget(rows, i + 1, n, budget, used + tok, sigs_only)
        _ ->
          println(s5("-- over budget: ", row[0], " ", row[1], ""))
          dump_within_budget(rows, i + 1, n, budget, used, sigs_only)
    _ ->
      println(s3("-- ", itos(used), "tok used"))
      0

-- fn efv_loop
efv_loop eng i acc =
  match i >= glyph_array_len(eng.env_types)
    true -> acc
    _ ->
      ti = eng.env_types[i]
      tc_collect_fv(eng, ti, acc)
      efv_loop(eng, i + 1, acc)


-- fn emit_capture_loads
emit_capture_loads lam_ctx free_vars env_local i =
  n_caps = glyph_array_len(free_vars) / 2
  match i >= n_caps
    true -> 0
    _ ->
      cap_name = free_vars[i * 2 + 1]
      cap_local = mir_alloc_local(lam_ctx, cap_name)
      field_name = s2("__cap", itos(i))
      mir_emit(lam_ctx, mk_stmt(cap_local, rv_field(), i + 1, field_name, mk_op_local(env_local), mk_op_nil(), []))
      mir_bind_var(lam_ctx, cap_name, cap_local)
      emit_capture_loads(lam_ctx, free_vars, env_local, i + 1)


-- fn emit_dedents
emit_dedents indent_stack tokens new_indent pos line =
  match new_indent < indent_top(indent_stack)
    true ->
      glyph_array_pop(indent_stack)
      glyph_array_push(tokens, mk_token(tk_dedent(), pos, pos, line))
      emit_dedents(indent_stack, tokens, new_indent, pos, line)
    _ -> 0

-- fn emit_mir_db
emit_mir_db db_path =
  db = glyph_db_open(db_path)
  sources = read_fn_defs(db)
  externs = read_externs(db)
  glyph_db_close(db)
  parsed = parse_all_fns(sources, 0)
  za_fns = build_za_fns(parsed, 0, [])
  mirs = compile_fns_parsed(parsed, 0, za_fns, [], false)
  fix_all_field_offsets(mirs)
  fix_extern_calls(mirs, externs)
  mir_pp_fns(mirs, 0)
  0


-- fn eng_set_tmap
eng_set_tmap eng ast_len =
  tm = tmap_init(ast_len)
  glyph_array_set(eng.tmap, 0, tm)
  0

-- fn env_free_vars
env_free_vars eng =
  efv_loop(eng, 0, [])

-- fn env_insert
env_insert eng name ty =
  glyph_array_push(eng.env_names, name)
  glyph_array_push(eng.env_types, ty)
  0

-- fn env_lookup
env_lookup eng name =
  env_lookup_at(eng.env_names, eng.env_types, name, glyph_array_len(eng.env_names) - 1)

-- fn env_lookup_at
env_lookup_at names types name i =
  match i < 0
    true -> 0 - 1
    _ ->
      match glyph_str_eq(names[i], name) == 1
        true -> types[i]
        _ -> env_lookup_at(names, types, name, i - 1)

-- fn env_pop
env_pop eng =
  target = glyph_array_pop(eng.env_marks)
  env_pop_to(eng.env_names, eng.env_types, target)

-- fn env_pop_to
env_pop_to names types target =
  match glyph_array_len(names) > target
    true ->
      glyph_array_pop(names)
      glyph_array_pop(types)
      env_pop_to(names, types, target)
    _ -> 0

-- fn env_push
env_push eng =
  glyph_array_push(eng.env_marks, glyph_array_len(eng.env_names))
  0

-- fn ex_array
ex_array = 50

-- fn ex_binary
ex_binary = 20

-- fn ex_block
ex_block = 44

-- fn ex_bool_lit
ex_bool_lit = 4

-- fn ex_call
ex_call = 22

-- fn ex_compose
ex_compose = 31

-- fn ex_field_access
ex_field_access = 23

-- fn ex_field_accessor
ex_field_accessor = 25

-- fn ex_float_lit
ex_float_lit = 54

-- fn ex_ident
ex_ident = 10

-- fn ex_index
ex_index = 24

-- fn ex_int_lit
ex_int_lit = 1

-- fn ex_lambda
ex_lambda = 40

-- fn ex_match
ex_match = 42

-- fn ex_pipe
ex_pipe = 30

-- fn ex_propagate
ex_propagate = 32

-- fn ex_record
ex_record = 51

-- fn ex_str_interp
ex_str_interp = 53

-- fn ex_str_lit
ex_str_lit = 3

-- fn ex_type_ident
ex_type_ident = 60

-- fn ex_unary
ex_unary = 21

-- fn ex_unwrap
ex_unwrap = 33

-- fn expect_tok
expect_tok tokens pos kind =
  match cur_kind(tokens, pos) == kind
    true -> pos + 1
    _ -> 0 - 1

-- fn extract_bodies_acc
extract_bodies_acc rows i acc =
  match i >= glyph_array_len(rows)
    true -> acc
    _ ->
      row = rows[i]
      glyph_array_push(acc, row[0])
      extract_bodies_acc(rows, i + 1, acc)

-- fn extract_name
extract_name body =
  len = glyph_str_len(body)
  extract_name_loop(body, 0, len)

-- fn extract_name_loop
extract_name_loop body i len =
  match i < len
    true ->
      c = glyph_str_char_at(body, i)
      match c == 32
        true -> glyph_str_slice(body, 0, i)
        _ ->
          match c == 10
            true -> glyph_str_slice(body, 0, i)
            _ ->
              match c == 9
                true -> glyph_str_slice(body, 0, i)
                _ ->
                  match c == 61
                    true ->
                      match i > 0
                        true -> glyph_str_slice(body, 0, i)
                        _ -> extract_name_loop(body, i + 1, len)
                    _ -> extract_name_loop(body, i + 1, len)
    _ -> body

-- fn extract_new_var_ids
extract_new_var_ids eng var_map i =
  match i >= glyph_array_len(var_map)
    true -> []
    _ ->
      new_pool_idx = var_map[i + 1]
      node = pool_get(eng, new_pool_idx)
      _ = node.tag
      rest = extract_new_var_ids(eng, var_map, i + 2)
      glyph_array_push(rest, node.n1)
      rest


-- fn extract_source_line
extract_source_line src offset =
  ls = find_line_start(src, offset)
  le = find_line_end(src, offset)
  glyph_str_slice(src, ls, le)

-- fn extract_type_after_colon
extract_type_after_colon token =
  len = glyph_str_len(token)
  colon = psf_find_char(token, 58, 0, len)
  match colon > 0
    true -> glyph_str_slice(token, colon + 1, len)
    _ -> "I"

-- fn fcr_print_uncovered
fcr_print_uncovered names hits i =
  match i >= glyph_array_len(names)
    true -> 0
    _ ->
      match hits[i] == 0
        true -> println(s2("  ", names[i]))
        _ -> 0
      fcr_print_uncovered(names, hits, i + 1)


-- fn fet_loop
fet_loop name struct_map i =
  match i >= glyph_array_len(struct_map)
    true -> []
    _ ->
      entry = struct_map[i]
      match glyph_str_eq(entry[0], name)
        true -> entry[2]
        _ -> fet_loop(name, struct_map, i + 1)

-- fn ffc_idx
ffc_idx fname fields ctypes j =
  match j >= glyph_array_len(fields)
    true -> ""
    _ ->
      match glyph_str_eq(fields[j], fname) == 1
        true ->
          match j < glyph_array_len(ctypes)
            true -> ctypes[j]
            _ -> ""
        _ -> ffc_idx(fname, fields, ctypes, j + 1)


-- fn ffc_search
ffc_search tname fname struct_map i n =
  match i >= n
    true -> ""
    _ ->
      entry = struct_map[i]
      match glyph_str_eq(entry[0], tname) == 1
        true -> ffc_idx(fname, entry[1], entry[2], 0)
        _ -> ffc_search(tname, fname, struct_map, i + 1, n)


-- fn fill_empty_2d
fill_empty_2d result n i =
  match i >= n
    true -> 0
    _ ->
      glyph_array_push(result, [])
      fill_empty_2d(result, n, i + 1)

-- fn filter_test_args
filter_test_args argv start end =
  match start >= end
    true -> []
    _ ->
      arg = argv[start]
      rest = filter_test_args(argv, start + 1, end)
      skip = match glyph_str_eq(arg, "--gen") == 1
        true -> 1
        _ -> match glyph_str_eq(arg, "--debug") == 1
          true -> 1
          _ -> match glyph_str_eq(arg, "--release") == 1
            true -> 1
            _ -> match glyph_str_eq(arg, "--cover") == 1
              true -> 1
              _ -> 0
      match skip == 1
        true ->
          match glyph_str_eq(arg, "--gen") == 1
            true -> filter_test_args(argv, start + 2, end)
            _ -> rest
        _ ->
          flen = glyph_str_len(arg)
          match flen > 5
            true ->
              pfx = glyph_str_slice(arg, 0, 5)
              match glyph_str_eq(pfx, "--gen") == 1
                true ->
                  match glyph_str_char_at(arg, 5) == 61
                    true -> rest
                    _ ->
                      glyph_array_push(rest, arg)
                      rest
                _ ->
                  glyph_array_push(rest, arg)
                  rest
            _ ->
              glyph_array_push(rest, arg)
              rest


-- fn find_best_type
find_best_type reg fname local_fields i best_idx best_size =
  match i >= glyph_array_len(reg)
    true -> best_idx
    _ ->
      t = reg[i]
      match has_str_in(t, fname)
        true ->
          match all_fields_in(t, local_fields, 0)
            true ->
              sz = glyph_array_len(t)
              match sz > best_size
                true -> find_best_type(reg, fname, local_fields, i + 1, i, sz)
                _ -> find_best_type(reg, fname, local_fields, i + 1, best_idx, best_size)
            _ -> find_best_type(reg, fname, local_fields, i + 1, best_idx, best_size)
        _ -> find_best_type(reg, fname, local_fields, i + 1, best_idx, best_size)

-- fn find_entry_types
find_entry_types name struct_map =
  fet_loop(name, struct_map, 0)

-- fn find_extern
find_extern externs name =
  find_extern_loop(externs, name, 0)

-- fn find_extern_loop
find_extern_loop externs name i =
  match i >= glyph_array_len(externs)
    true -> []
    _ ->
      row = externs[i]
      match glyph_str_eq(row[0], name)
        true -> row
        _ -> find_extern_loop(externs, name, i + 1)

-- fn find_field
find_field eng fs name i =
  match i >= glyph_array_len(fs)
    true -> 0 - 1
    _ ->
      fi = fs[i]
      pool_len = glyph_array_len(eng.ty_pool)
      match fi >= pool_len
        true -> find_field(eng, fs, name, i + 1)
        _ -> match fi < 0
          true -> find_field(eng, fs, name, i + 1)
          _ ->
            f = pool_get(eng, fi)
            _ = f.tag
            match glyph_str_eq(f.sval, name)
              true -> fi
              _ -> find_field(eng, fs, name, i + 1)


-- fn find_field_ctype
find_field_ctype tname fname struct_map =
  n = glyph_array_len(struct_map)
  ffc_search(tname, fname, struct_map, 0, n)


-- fn find_flag
find_flag argv flag start =
  argc = glyph_array_len(argv)
  match start < argc
    true ->
      arg = argv[start]
      match glyph_str_eq(arg, flag) == 1
        true ->
          match start + 1 < argc
            true -> argv[start + 1]
            _ -> ""
        _ ->
          flen = glyph_str_len(flag)
          match glyph_str_len(arg) > flen + 1
            true ->
              prefix = glyph_str_slice(arg, 0, flen)
              match glyph_str_eq(prefix, flag) == 1
                true ->
                  match glyph_str_char_at(arg, flen) == 61
                    true -> glyph_str_slice(arg, flen + 1, glyph_str_len(arg))
                    _ -> find_flag(argv, flag, start + 1)
                _ -> find_flag(argv, flag, start + 1)
            _ -> find_flag(argv, flag, start + 1)
    _ -> ""


-- fn find_line_end
find_line_end src offset =
  match offset >= glyph_str_len(src)
    true -> offset
    _ ->
      match glyph_str_char_at(src, offset) == 10
        true -> offset
        _ -> find_line_end(src, offset + 1)

-- fn find_line_start
find_line_start src offset =
  match offset <= 0
    true -> 0
    _ ->
      match glyph_str_char_at(src, offset - 1) == 10
        true -> offset
        _ -> find_line_start(src, offset - 1)

-- fn find_matching_type
find_matching_type reg fname local_fields i =
  find_best_type(reg, fname, local_fields, 0, 0 - 1, 0 - 1)

-- fn find_str_in
find_str_in arr target i =
  match i >= glyph_array_len(arr)
    true -> 0 - 1
    _ -> match glyph_str_eq(arr[i], target)
      true -> i
      _ -> find_str_in(arr, target, i + 1)

-- fn find_struct_name
find_struct_name sstr struct_map =
  fields = split_comma(sstr)
  sorted = sort_str_arr(fields)
  fsn_loop(sorted, struct_map, 0)

-- fn fix_all_field_offsets
fix_all_field_offsets mirs =
  reg = build_type_reg(mirs)
  fix_offs_mirs(mirs, 0, reg)

-- fn fix_ext_blks
fix_ext_blks blocks externs bi =
  match bi >= glyph_array_len(blocks)
    true -> 0
    _ ->
      fix_ext_stmts(blocks[bi], externs, 0)
      fix_ext_blks(blocks, externs, bi + 1)

-- fn fix_ext_fn
fix_ext_fn mir externs =
  fix_ext_blks(mir.fn_blocks_stmts, externs, 0)

-- fn fix_ext_mirs
fix_ext_mirs mirs externs i =
  match i >= glyph_array_len(mirs)
    true -> 0
    _ ->
      fix_ext_fn(mirs[i], externs)
      fix_ext_mirs(mirs, externs, i + 1)

-- fn fix_ext_stmts
fix_ext_stmts stmts externs si =
  match si >= glyph_array_len(stmts)
    true -> 0
    _ ->
      stmt = stmts[si]
      match stmt.skind == rv_call()
        true ->
          callee = stmt.sop1
          match callee.okind == ok_func_ref()
            true ->
              name = callee.ostr
              match is_runtime_fn(name) == 0
                true -> match has_glyph_prefix(name) == 0
                  true ->
                    ext = find_extern(externs, name)
                    match glyph_array_len(ext) > 0
                      true ->
                        new_op = mk_op(ok_func_ref(), 0, s2("glyph_", name))
                        new_stmt = mk_stmt(stmt.sdest, stmt.skind, stmt.sival, stmt.sstr, new_op, stmt.sop2, stmt.sops)
                        glyph_array_set(stmts, si, new_stmt)
                        0
                      _ -> 0
                  _ -> 0
                _ -> 0
            _ -> 0
        _ -> 0
      fix_ext_stmts(stmts, externs, si + 1)

-- fn fix_extern_calls
fix_extern_calls mirs externs =
  fix_ext_mirs(mirs, externs, 0)

-- fn fix_offs_blks
fix_offs_blks blocks bi reg la =
  match bi >= glyph_array_len(blocks)
    true -> 0
    _ ->
      fix_offs_stmts(blocks[bi], 0, reg, la)
      fix_offs_blks(blocks, bi + 1, reg, la)

-- fn fix_offs_fn
fix_offs_fn mir reg =
  la = coll_local_acc(mir)
  fix_offs_blks(mir.fn_blocks_stmts, 0, reg, la)

-- fn fix_offs_mirs
fix_offs_mirs mirs i reg =
  match i >= glyph_array_len(mirs)
    true -> 0
    _ ->
      fix_offs_fn(mirs[i], reg)
      fix_offs_mirs(mirs, i + 1, reg)

-- fn fix_offs_stmts
fix_offs_stmts stmts si reg la =
  match si >= glyph_array_len(stmts)
    true -> 0
    _ ->
      stmt = stmts[si]
      match stmt.skind == rv_field()
        true ->
          match glyph_str_len(stmt.sstr) == 0
            true -> 0
            _ ->
              base_kind = stmt.sop1.okind
              match base_kind == ok_local()
                true ->
                  local_id = stmt.sop1.oval
                  local_fields = match local_id >= 0
                    true -> match local_id < glyph_array_len(la)
                      true -> la[local_id]
                      _ -> []
                    _ -> []
                  offset = resolve_fld_off(reg, stmt.sstr, local_fields)
                  match offset >= 0
                    true -> raw_set(stmt, 1, offset)
                    _ -> 0
                _ -> 0
        _ -> 0
      fix_offs_stmts(stmts, si + 1, reg, la)

-- fn flush_dedents
flush_dedents indent_stack tokens pos line =
  match glyph_array_len(indent_stack) > 1
    true ->
      glyph_array_pop(indent_stack)
      glyph_array_push(tokens, mk_token(tk_dedent(), pos, pos, line))
      flush_dedents(indent_stack, tokens, pos, line)
    _ -> 0

-- fn format_cover_report
format_cover_report names hits =
  total = glyph_array_len(names)
  covered = count_covered(hits, 0, 0)
  uncovered = total - covered
  pct = match total > 0
    true -> (covered * 1000) / total
    _ -> 0
  pct_whole = pct / 10
  pct_frac = pct - (pct_whole * 10)
  println(s7("Coverage: ", itos(covered), "/", itos(total), " (", s3(itos(pct_whole), ".", itos(pct_frac)), "%)"))
  match uncovered > 0
    true ->
      println(s3("\nUncovered (", itos(uncovered), "):"))
      fcr_print_uncovered(names, hits, 0)
    _ -> println("\nAll functions covered!")


-- fn format_diagnostic
format_diagnostic def_name src offset line msg =
  col = line_col(src, offset)
  src_line = extract_source_line(src, offset)
  line_str = itos(line)
  col_str = itos(col)
  caret = make_caret(col)
  s7("error in '", def_name, "' at ", line_str, s2(":", col_str), s2(": ", msg), s5("\n  ", line_str, " | ", src_line, s3("\n    | ", caret, "")))

-- fn format_kind_counts
format_kind_counts rows i n acc =
  match i < n
    true ->
      row = rows[i]
      entry = s3(row[1], " ", row[0])
      new_acc = match glyph_str_len(acc) == 0
        true -> entry
        _ -> s3(acc, ", ", entry)
      format_kind_counts(rows, i + 1, n, new_acc)
    _ -> acc

-- fn format_parse_err
format_parse_err src tokens pos msg =
  p = match pos >= glyph_array_len(tokens)
    true -> match glyph_array_len(tokens) > 0
      true -> glyph_array_len(tokens) - 1
      _ -> 0
    _ -> pos
  offset = match p < glyph_array_len(tokens)
    true -> (tokens[p]).start
    _ -> 0
  line = match p < glyph_array_len(tokens)
    true -> (tokens[p]).line
    _ -> 1
  src_line = extract_source_line(src, offset)
  p2 = match glyph_str_len(src_line) == 0
    true -> match p > 0
      true -> p - 1
      _ -> p
    _ -> p
  offset2 = match p2 < glyph_array_len(tokens)
    true -> (tokens[p2]).start
    _ -> 0
  line2 = match p2 < glyph_array_len(tokens)
    true -> (tokens[p2]).line
    _ -> 1
  col2 = line_col(src, offset2)
  src_line2 = extract_source_line(src, offset2)
  caret = make_caret(col2)
  line_s = itos(line2)
  s7("  ", line_s, " | ", src_line2, s2("\n", "    | "), caret, s2("^ ", msg))

-- fn fsn_fields_eq
fsn_fields_eq a b =
  match glyph_array_len(a) == glyph_array_len(b)
    true -> fsn_fields_eq_loop(a, b, 0)
    _ -> 0

-- fn fsn_fields_eq_loop
fsn_fields_eq_loop a b i =
  match i >= glyph_array_len(a)
    true -> 1
    _ ->
      match glyph_str_eq(a[i], b[i]) == 1
        true -> fsn_fields_eq_loop(a, b, i + 1)
        _ -> 0

-- fn fsn_loop
fsn_loop sorted struct_map i =
  match i >= glyph_array_len(struct_map)
    true -> ""
    _ ->
      entry = struct_map[i]
      entry_fields = entry[1]
      match fsn_fields_eq(sorted, entry_fields)
        true -> entry[0]
        _ -> fsn_loop(sorted, struct_map, i + 1)

-- fn fv_is_bound
fv_is_bound name bound i =
  match i >= glyph_array_len(bound)
    true -> 0
    _ ->
      match glyph_str_eq(bound[i], name) == 1
        true -> 1
        _ -> fv_is_bound(name, bound, i + 1)


-- fn fv_is_seen
fv_is_seen name seen i =
  match i >= glyph_array_len(seen)
    true -> 0
    _ ->
      match glyph_str_eq(seen[i], name) == 1
        true -> 1
        _ -> fv_is_seen(name, seen, i + 1)


-- fn generalize
generalize eng ty =
  free = tc_collect_fv(eng, ty, [])
  efv = env_free_vars(eng)
  to_bind = subtract_vars(free, efv, 0)
  match glyph_array_len(to_bind) == 0
    true -> ty
    _ ->
      var_map = make_inst_map(eng, to_bind, 0)
      new_body = inst_type(eng, ty, var_map)
      new_bound_ids = extract_new_var_ids(eng, var_map, 0)
      mk_tforall(new_body, new_bound_ids, eng.ty_pool)


-- fn gval_t
gval_t = "GVal"

-- fn has_flag
has_flag argv flag start =
  argc = glyph_array_len(argv)
  match start < argc
    true ->
      match glyph_str_eq(argv[start], flag) == 1
        true -> 1
        _ -> has_flag(argv, flag, start + 1)
    _ -> 0

-- fn has_glyph_prefix
has_glyph_prefix name =
  match glyph_str_len(name) >= 6
    true -> glyph_str_eq(glyph_str_slice(name, 0, 6), "glyph_")
    _ -> 0

-- fn has_kind
has_kind kinds k =
  has_kind_loop(kinds, k, 0)

-- fn has_kind_loop
has_kind_loop kinds k i =
  match i >= glyph_array_len(kinds)
    true -> 0
    _ -> match kinds[i] == k
      true -> 1
      _ -> has_kind_loop(kinds, k, i + 1)

-- fn has_str_in
has_str_in arr target =
  find_str_in(arr, target, 0) >= 0

-- fn history_migration_sql
history_migration_sql = "CREATE TABLE IF NOT EXISTS def_history (id INTEGER PRIMARY KEY, def_id INTEGER NOT NULL, name TEXT NOT NULL, kind TEXT NOT NULL, sig TEXT, body TEXT NOT NULL, hash BLOB NOT NULL, tokens INTEGER NOT NULL, gen INTEGER NOT NULL DEFAULT 1, changed_at TEXT NOT NULL DEFAULT (datetime('now'))); CREATE INDEX IF NOT EXISTS idx_history_name ON def_history(name, kind); CREATE TRIGGER IF NOT EXISTS trg_def_history_delete BEFORE DELETE ON def BEGIN INSERT INTO def_history (def_id, name, kind, sig, body, hash, tokens, gen) VALUES (OLD.id, OLD.name, OLD.kind, OLD.sig, OLD.body, OLD.hash, OLD.tokens, OLD.gen); END; CREATE TRIGGER IF NOT EXISTS trg_def_history_update BEFORE UPDATE OF body ON def BEGIN INSERT INTO def_history (def_id, name, kind, sig, body, hash, tokens, gen) VALUES (OLD.id, OLD.name, OLD.kind, OLD.sig, OLD.body, OLD.hash, OLD.tokens, OLD.gen); END;"

-- fn indent_top
indent_top stack = stack[glyph_array_len(stack) - 1]

-- fn infer_array
infer_array eng ast node =
  match glyph_array_len(node.ns) == 0
    true ->
      elem = subst_fresh(eng)
      mk_tarray(elem, eng.ty_pool)
    _ ->
      first = infer_expr(eng, ast, node.ns[0])
      unify_array_elems(eng, ast, node.ns, first, 1)
      mk_tarray(first, eng.ty_pool)

-- fn infer_binary
infer_binary eng ast node =
  lt = infer_expr(eng, ast, node.n1)
  rt = infer_expr(eng, ast, node.n2)
  lt_r = subst_resolve(eng, lt)
  rt_r = subst_resolve(eng, rt)
  r = binop_type(eng, node.ival, lt_r, rt_r)
  match r < 0
    true ->
      unify(eng, lt, rt)
      lt
    _ -> r

-- fn infer_block
infer_block eng ast node =
  env_push(eng)
  r = infer_stmts(eng, ast, node.ns, 0)
  env_pop(eng)
  r

-- fn infer_call
infer_call eng ast node =
  callee_node = ast[node.n1]
  match callee_node.kind == ex_type_ident()
    true -> subst_fresh(eng)
    _ ->
      callee_ty = infer_expr(eng, ast, node.n1)
      args = node.ns
      apply_args(eng, ast, callee_ty, args, 0)

-- fn infer_compose
infer_compose eng ast node =
  left_ty = infer_expr(eng, ast, node.n1)
  right_ty = infer_expr(eng, ast, node.n2)
  a = subst_fresh(eng)
  b = subst_fresh(eng)
  c = subst_fresh(eng)
  unify(eng, left_ty, mk_tfn(a, b, eng.ty_pool))
  unify(eng, right_ty, mk_tfn(b, c, eng.ty_pool))
  mk_tfn(a, c, eng.ty_pool)

-- fn infer_ctor_pattern
infer_ctor_pattern eng ast node =
  ctor_ty = env_lookup(eng, node.sval)
  match ctor_ty < 0
    true -> subst_fresh(eng)
    _ -> ctor_ty

-- fn infer_expr
infer_expr eng ast ni =
  result = infer_expr_core(eng, ast, ni)
  tm = eng.tmap[0]
  match ni < glyph_array_len(tm)
    true ->
      glyph_array_set(tm, ni, result)
      result
    _ -> result

-- fn infer_expr2
infer_expr2 eng ast node k =
  match k
    _ ? k == ex_field_access() -> infer_field_access(eng, ast, node)
    _ ? k == ex_index() -> infer_index(eng, ast, node)
    _ ? k == ex_array() -> infer_array(eng, ast, node)
    _ ? k == ex_record() -> infer_record(eng, ast, node)
    _ ? k == ex_lambda() -> infer_lambda(eng, ast, node)
    _ ? k == ex_match() -> infer_match(eng, ast, node)
    _ ? k == ex_block() -> infer_block(eng, ast, node)
    _ ? k == ex_pipe() -> infer_pipe(eng, ast, node)
    _ -> infer_expr3(eng, ast, node, k)

-- fn infer_expr3
infer_expr3 eng ast node k =
  match k
    _ ? k == ex_compose() -> infer_compose(eng, ast, node)
    _ ? k == ex_propagate() -> infer_propagate(eng, ast, node)
    _ ? k == ex_unwrap() -> infer_unwrap(eng, ast, node)
    _ ? k == ex_type_ident() -> subst_fresh(eng)
    _ ? k == ex_str_interp() -> mk_tstr(eng.ty_pool)
    _ -> mk_terror(eng.ty_pool)

-- fn infer_expr_core
infer_expr_core eng ast ni =
  node = ast[ni]
  k = node.kind
  match k
    _ ? k == ex_int_lit() -> mk_tint(eng.ty_pool)
    _ ? k == ex_str_lit() || k == ex_str_interp() -> mk_tstr(eng.ty_pool)
    _ ? k == ex_bool_lit() -> mk_tbool(eng.ty_pool)
    _ ? k == ex_ident() -> infer_ident(eng, node)
    _ ? k == ex_binary() -> infer_binary(eng, ast, node)
    _ ? k == ex_unary() -> infer_unary(eng, ast, node)
    _ ? k == ex_call() -> infer_call(eng, ast, node)
    _ -> infer_expr2(eng, ast, node, k)

-- fn infer_field_access
infer_field_access eng ast node =
  rec_ty = infer_expr(eng, ast, node.n1)
  result_ty = subst_fresh(eng)
  rest_var = subst_fresh_var(eng)
  field_idx = mk_tfield(node.sval, result_ty, eng.ty_pool)
  fields = []
  glyph_array_push(fields, field_idx)
  record_ty = mk_trecord(fields, rest_var, eng.ty_pool)
  unify(eng, rec_ty, record_ty)
  result_ty

-- fn infer_fn_def
infer_fn_def eng ast fi =
  node = ast[fi]
  env_push(eng)
  param_types = infer_fn_params(eng, ast, node.ns, 0)
  body_ty = infer_expr(eng, ast, node.n1)
  env_pop(eng)
  build_fn_type(eng, param_types, body_ty, 0)

-- fn infer_fn_params
infer_fn_params eng ast params i =
  match i >= glyph_array_len(params)
    true -> []
    _ ->
      pnode = ast[params[i]]
      pt = subst_fresh(eng)
      env_insert(eng, pnode.sval, pt)
      tm = eng.tmap[0]
      match params[i] < glyph_array_len(tm)
        true ->
          glyph_array_set(tm, params[i], pt)
          0
        _ -> 0
      rest = infer_fn_params(eng, ast, params, i + 1)
      glyph_array_push(rest, pt)
      rest

-- fn infer_ident
infer_ident eng node =
  r = env_lookup(eng, node.sval)
  match r < 0
    true -> mk_terror(eng.ty_pool)
    _ -> instantiate(eng, r)


-- fn infer_index
infer_index eng ast node =
  container = infer_expr(eng, ast, node.n1)
  idx = infer_expr(eng, ast, node.n2)
  elem = subst_fresh(eng)
  arr_ty = mk_tarray(elem, eng.ty_pool)
  unify(eng, container, arr_ty)
  unify(eng, idx, mk_tint(eng.ty_pool))
  elem

-- fn infer_lambda
infer_lambda eng ast node =
  env_push(eng)
  param_types = infer_lambda_params_ty(eng, ast, node.ns, 0)
  body_ty = infer_expr(eng, ast, node.n1)
  env_pop(eng)
  build_fn_type(eng, param_types, body_ty, 0)

-- fn infer_lambda_params_ty
infer_lambda_params_ty eng ast params i =
  match i >= glyph_array_len(params)
    true -> []
    _ ->
      pnode = ast[params[i]]
      pt = subst_fresh(eng)
      env_insert(eng, pnode.sval, pt)
      rest = infer_lambda_params_ty(eng, ast, params, i + 1)
      glyph_array_push(rest, pt)
      rest

-- fn infer_match
infer_match eng ast node =
  scrutinee_ty = infer_expr(eng, ast, node.n1)
  result_ty = subst_fresh(eng)
  infer_match_arms_ty(eng, ast, node.ns, scrutinee_ty, result_ty, 0)
  result_ty

-- fn infer_match_arms_ty
infer_match_arms_ty eng ast arms scrutinee_ty result_ty i =
  match i >= glyph_array_len(arms)
    true -> 0
    _ ->
      env_push(eng)
      pat_ty = infer_pattern(eng, ast, arms[i])
      unify(eng, scrutinee_ty, pat_ty)
      body_ty = infer_expr(eng, ast, arms[i + 1])
      unify(eng, result_ty, body_ty)
      guard_idx = arms[i + 2]
      match guard_idx >= 0
        true ->
          guard_ty = infer_expr(eng, ast, guard_idx)
          0
        _ -> 0
      env_pop(eng)
      infer_match_arms_ty(eng, ast, arms, scrutinee_ty, result_ty, i + 3)


-- fn infer_pattern
infer_pattern eng ast pi =
  node = ast[pi]
  k = node.kind
  match k
    _ ? k == pat_wildcard() -> subst_fresh(eng)
    _ ? k == pat_ident() ->
      pt = subst_fresh(eng)
      env_insert(eng, node.sval, pt)
      pt
    _ ? k == pat_int() -> mk_tint(eng.ty_pool)
    _ ? k == pat_bool() -> mk_tbool(eng.ty_pool)
    _ ? k == pat_str() -> mk_tstr(eng.ty_pool)
    _ ? k == pat_ctor() -> infer_ctor_pattern(eng, ast, node)
    _ -> subst_fresh(eng)

-- fn infer_pipe
infer_pipe eng ast node =
  left_ty = infer_expr(eng, ast, node.n1)
  right_ty = infer_expr(eng, ast, node.n2)
  ret = subst_fresh(eng)
  fn_ty = mk_tfn(left_ty, ret, eng.ty_pool)
  unify(eng, right_ty, fn_ty)
  ret

-- fn infer_propagate
infer_propagate eng ast node =
  inner = infer_expr(eng, ast, node.n1)
  elem = subst_fresh(eng)
  unify(eng, inner, mk_topt(elem, eng.ty_pool))
  elem

-- fn infer_record
infer_record eng ast node =
  field_ns = infer_record_fields(eng, ast, node.ns, 0)
  mk_trecord(field_ns, 0 - 1, eng.ty_pool)

-- fn infer_record_fields
infer_record_fields eng ast ns i =
  match i >= glyph_array_len(ns)
    true -> []
    _ ->
      name_node = ast[ns[i]]
      val_ty = infer_expr(eng, ast, ns[i + 1])
      fi = mk_tfield(name_node.sval, val_ty, eng.ty_pool)
      result = infer_record_fields(eng, ast, ns, i + 2)
      glyph_array_push(result, fi)
      result

-- fn infer_stmt
infer_stmt eng ast si =
  node = ast[si]
  k = node.kind
  match k
    _ ? k == st_let() ->
      val_ty = infer_expr(eng, ast, node.n1)
      env_insert(eng, node.sval, val_ty)
      val_ty
    _ ? k == st_assign() ->
      val_ty = infer_expr(eng, ast, node.n1)
      existing = env_lookup(eng, node.sval)
      match existing >= 0
        true -> unify(eng, existing, val_ty)
        _ -> 0
      val_ty
    _ ? k == st_expr() -> infer_expr(eng, ast, node.n1)
    _ -> mk_tvoid(eng.ty_pool)

-- fn infer_stmts
infer_stmts eng ast stmts i =
  match i >= glyph_array_len(stmts)
    true -> mk_tvoid(eng.ty_pool)
    _ ->
      r = infer_stmt(eng, ast, stmts[i])
      match i == glyph_array_len(stmts) - 1
        true -> r
        _ -> infer_stmts(eng, ast, stmts, i + 1)

-- fn infer_unary
infer_unary eng ast node =
  operand = infer_expr(eng, ast, node.n1)
  operand_r = subst_resolve(eng, operand)
  r = unaryop_type(eng, node.ival, operand_r)
  match r < 0
    true -> operand
    _ -> r

-- fn infer_unwrap
infer_unwrap eng ast node =
  inner = infer_expr(eng, ast, node.n1)
  elem = subst_fresh(eng)
  unify(eng, inner, mk_topt(elem, eng.ty_pool))
  elem

-- fn init_schema
init_schema = "PRAGMA journal_mode = WAL; PRAGMA foreign_keys = ON; CREATE TABLE IF NOT EXISTS def (id INTEGER PRIMARY KEY, name TEXT NOT NULL, kind TEXT NOT NULL CHECK(kind IN ('fn','type','trait','impl','const','fsm','srv','macro','test')), sig TEXT, body TEXT NOT NULL, hash BLOB NOT NULL, tokens INTEGER NOT NULL, compiled INTEGER NOT NULL DEFAULT 0, gen INTEGER NOT NULL DEFAULT 1, created TEXT NOT NULL DEFAULT (datetime('now')), modified TEXT NOT NULL DEFAULT (datetime('now'))); CREATE UNIQUE INDEX IF NOT EXISTS idx_def_name_kind_gen ON def(name, kind, gen); CREATE INDEX IF NOT EXISTS idx_def_kind ON def(kind); CREATE INDEX IF NOT EXISTS idx_def_gen ON def(gen); CREATE INDEX IF NOT EXISTS idx_def_compiled ON def(compiled) WHERE compiled = 0; CREATE TABLE IF NOT EXISTS dep (from_id INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE, to_id INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE, edge TEXT NOT NULL CHECK(edge IN ('calls','uses_type','implements','field_of','variant_of')), PRIMARY KEY (from_id, to_id, edge)); CREATE INDEX IF NOT EXISTS idx_dep_to ON dep(to_id); CREATE TABLE IF NOT EXISTS extern_ (id INTEGER PRIMARY KEY, name TEXT NOT NULL, symbol TEXT NOT NULL, lib TEXT, sig TEXT NOT NULL, conv TEXT NOT NULL DEFAULT 'C' CHECK(conv IN ('C','system','rust')), UNIQUE(name)); CREATE TABLE IF NOT EXISTS tag (def_id INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE, key TEXT NOT NULL, val TEXT, PRIMARY KEY (def_id, key)); CREATE INDEX IF NOT EXISTS idx_tag_key_val ON tag(key, val); CREATE TABLE IF NOT EXISTS module (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, doc TEXT); CREATE TABLE IF NOT EXISTS module_member (module_id INTEGER NOT NULL REFERENCES module(id) ON DELETE CASCADE, def_id INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE, exported INTEGER NOT NULL DEFAULT 1, PRIMARY KEY (module_id, def_id)); CREATE TABLE IF NOT EXISTS compiled (def_id INTEGER PRIMARY KEY REFERENCES def(id) ON DELETE CASCADE, ir BLOB NOT NULL, target TEXT NOT NULL, hash BLOB NOT NULL); CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, val TEXT NOT NULL); INSERT OR IGNORE INTO meta (key, val) VALUES ('schema_version', '4'); CREATE TABLE IF NOT EXISTS def_history (id INTEGER PRIMARY KEY, def_id INTEGER NOT NULL, name TEXT NOT NULL, kind TEXT NOT NULL, sig TEXT, body TEXT NOT NULL, hash BLOB NOT NULL, tokens INTEGER NOT NULL, gen INTEGER NOT NULL DEFAULT 1, changed_at TEXT NOT NULL DEFAULT (datetime('now'))); CREATE INDEX IF NOT EXISTS idx_history_name ON def_history(name, kind); CREATE TRIGGER IF NOT EXISTS trg_def_history_delete BEFORE DELETE ON def BEGIN INSERT INTO def_history (def_id, name, kind, sig, body, hash, tokens, gen) VALUES (OLD.id, OLD.name, OLD.kind, OLD.sig, OLD.body, OLD.hash, OLD.tokens, OLD.gen); END; CREATE TRIGGER IF NOT EXISTS trg_def_history_update BEFORE UPDATE OF body ON def BEGIN INSERT INTO def_history (def_id, name, kind, sig, body, hash, tokens, gen) VALUES (OLD.id, OLD.name, OLD.kind, OLD.sig, OLD.body, OLD.hash, OLD.tokens, OLD.gen); END; CREATE VIEW IF NOT EXISTS v_dirty AS WITH RECURSIVE dirty(id) AS (SELECT id FROM def WHERE compiled = 0 UNION SELECT d.from_id FROM dep d JOIN dirty ON d.to_id = dirty.id) SELECT DISTINCT def.* FROM def JOIN dirty ON def.id = dirty.id; CREATE VIEW IF NOT EXISTS v_context AS SELECT d.*, COUNT(dep.to_id) as dep_count FROM def d LEFT JOIN dep ON d.id = dep.from_id GROUP BY d.id ORDER BY dep_count ASC, d.tokens ASC; CREATE VIEW IF NOT EXISTS v_callgraph AS SELECT f.name AS caller, t.name AS callee, d.edge FROM dep d JOIN def f ON d.from_id = f.id JOIN def t ON d.to_id = t.id;"

-- fn insert_dep_loop
insert_dep_loop db pairs i =
  match i >= glyph_array_len(pairs)
    true -> 0
    _ ->
      caller = pairs[i]
      callee = pairs[i + 1]
      from_id = lookup_def_id(db, caller)
      to_id = lookup_def_id(db, callee)
      match from_id > 0
        true -> match to_id > 0
          true ->
            sql = s5("INSERT OR IGNORE INTO dep (from_id, to_id, edge) VALUES (", itos(from_id), s2(", ", itos(to_id)), ", 'calls'", ")")
            glyph_db_exec(db, sql)
            0
          _ -> 0
        _ -> 0
      insert_dep_loop(db, pairs, i + 2)

-- fn insert_deps
insert_deps db_path pairs =
  n = glyph_array_len(pairs)
  match n == 0
    true -> 0
    _ ->
      db = glyph_db_open(db_path)
      glyph_db_exec(db, "DELETE FROM dep WHERE edge = 'calls'")
      insert_dep_loop(db, pairs, 0)
      glyph_db_close(db)
      println(s2("Tracked ", s2(itos(n / 2), " dependency edges")))
      0

-- fn inst_type
inst_type eng ti var_map =
  pool_len = glyph_array_len(eng.ty_pool)
  match ti < 0
    true -> ti
    _ -> match ti >= pool_len
      true -> ti
      _ ->
        node = pool_get(eng, ti)
        tag = node.tag
        match tag == ty_var()
          true ->
            root = subst_find(eng.parent, node.n1)
            r = lookup_var_map(var_map, root, 0)
            match r < 0
              true -> ti
              _ -> r
          _ -> match tag == ty_fn()
            true ->
              p = inst_type(eng, node.n1, var_map)
              r = inst_type(eng, node.n2, var_map)
              mk_tfn(p, r, eng.ty_pool)
            _ -> match tag == ty_array()
              true ->
                e = inst_type(eng, node.n1, var_map)
                mk_tarray(e, eng.ty_pool)
              _ -> match tag == ty_opt()
                true ->
                  inner = inst_type(eng, node.n1, var_map)
                  mk_topt(inner, eng.ty_pool)
                _ -> match tag == ty_record()
                  true ->
                    new_fields = inst_type_fields(eng, node.ns, 0, var_map)
                    new_rest = match node.n1 < 0
                      true -> node.n1
                      _ -> inst_type(eng, node.n1, var_map)
                    mk_trecord(new_fields, new_rest, eng.ty_pool)
                  _ -> match tag == ty_tuple()
                    true ->
                      new_ns = inst_type_ns(eng, node.ns, 0, var_map)
                      mk_ttuple(new_ns, eng.ty_pool)
                    _ -> ti


-- fn inst_type_fields
inst_type_fields eng ns i var_map =
  match i >= glyph_array_len(ns)
    true -> []
    _ ->
      fi = ns[i]
      fnode = pool_get(eng, fi)
      _ = fnode.tag
      new_ty = inst_type(eng, fnode.n1, var_map)
      new_fi = mk_tfield(fnode.sval, new_ty, eng.ty_pool)
      rest = inst_type_fields(eng, ns, i + 1, var_map)
      glyph_array_push(rest, new_fi)
      rest


-- fn inst_type_ns
inst_type_ns eng ns i var_map =
  match i >= glyph_array_len(ns)
    true -> []
    _ ->
      new_elem = inst_type(eng, ns[i], var_map)
      rest = inst_type_ns(eng, ns, i + 1, var_map)
      glyph_array_push(rest, new_elem)
      rest


-- fn instantiate
instantiate eng ti =
  pool_len = glyph_array_len(eng.ty_pool)
  match ti < 0
    true -> ti
    _ -> match ti >= pool_len
      true -> ti
      _ ->
        node = pool_get(eng, ti)
        match node.tag == ty_forall()
          true ->
            var_map = make_inst_map(eng, node.ns, 0)
            inst_type(eng, node.n1, var_map)
          _ -> ti


-- fn is_alnum
is_alnum c = is_alpha(c) || is_digit(c)

-- fn is_alpha
is_alpha c = (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c == 95

-- fn is_arith_op
is_arith_op op =
  match op == op_add()
    true -> 1
    _ -> match op == op_sub()
      true -> 1
      _ -> match op == op_mul()
        true -> 1
        _ -> match op == op_div()
          true -> 1
          _ -> match op == op_mod()
            true -> 1
            _ -> 0


-- fn is_digit
is_digit c = c >= 48 && c <= 57

-- fn is_err
is_err r = r.node < 0

-- fn is_float_ctype
is_float_ctype t =
  match glyph_str_eq(t, "float") == 1
    true -> 1
    _ -> match glyph_str_eq(t, "double") == 1
      true -> 1
      _ -> 0


-- fn is_float_op
is_float_op ctx left right =
  lt = op_type(ctx, left)
  rt = op_type(ctx, right)
  match lt == 3
    true -> 1
    _ -> match rt == 3
      true -> 1
      _ -> 0


-- fn is_op_float
is_op_float mir_local_types op =
  match op.okind == ok_local()
    true -> match mir_local_types[op.oval] == 3
      true -> 1
      _ -> 0
    _ -> match op.okind == ok_const_float()
      true -> 1
      _ -> 0


-- fn is_runtime_fn
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
    _ ? glyph_str_eq(name, "array_set") -> 1
    _ ? glyph_str_eq(name, "array_pop") -> 1
    _ ? glyph_str_eq(name, "read_file") -> 1
    _ -> is_runtime_fn2(name)

-- fn is_runtime_fn2
is_runtime_fn2 name =
  match name
    _ ? glyph_str_eq(name, "write_file") -> 1
    _ ? glyph_str_eq(name, "system") -> 1
    _ ? glyph_str_eq(name, "args") -> 1
    _ ? glyph_str_eq(name, "alloc") -> 1
    _ ? glyph_str_eq(name, "dealloc") -> 1
    _ ? glyph_str_eq(name, "realloc") -> 1
    _ ? glyph_str_eq(name, "panic") -> 1
    _ ? glyph_str_eq(name, "main") -> 1
    _ ? glyph_str_eq(name, "sb_new") -> 1
    _ ? glyph_str_eq(name, "sb_append") -> 1
    _ ? glyph_str_eq(name, "sb_build") -> 1
    _ ? glyph_str_eq(name, "str_to_cstr") -> 1
    _ ? glyph_str_eq(name, "cstr_to_str") -> 1
    _ ? glyph_str_eq(name, "assert") -> 1
    _ ? glyph_str_eq(name, "assert_eq") -> 1
    _ -> is_runtime_fn3(name)

-- fn is_runtime_fn3
is_runtime_fn3 name =
  match name
    _ ? glyph_str_eq(name, "assert_str_eq") -> 1
    _ ? glyph_str_eq(name, "raw_set") -> 1
    _ ? glyph_str_eq(name, "print") -> 1
    _ ? glyph_str_eq(name, "array_new") -> 1
    _ ? glyph_str_eq(name, "ok") -> 1
    _ ? glyph_str_eq(name, "err") -> 1
    _ ? glyph_str_eq(name, "try_read_file") -> 1
    _ ? glyph_str_eq(name, "try_write_file") -> 1
    _ ? glyph_str_eq(name, "read_line") -> 1
    _ ? glyph_str_eq(name, "flush") -> 1
    _ ? glyph_str_eq(name, "float_to_str") -> 1
    _ ? glyph_str_eq(name, "str_to_float") -> 1
    _ ? glyph_str_eq(name, "int_to_float") -> 1
    _ ? glyph_str_eq(name, "float_to_int") -> 1
    _ -> 0

-- fn is_space
is_space c = c == 32 || c == 9

-- fn is_str_op
is_str_op ctx left right =
  lt = op_type(ctx, left)
  rt = op_type(ctx, right)
  match lt == 4
    true -> 1
    _ -> match rt == 4
      true -> 1
      _ -> 0


-- fn is_str_ret_fn
is_str_ret_fn name =
  match str_eq(name, "str_concat")
    true -> 1
    _ -> match str_eq(name, "int_to_str")
      true -> 1
      _ -> match str_eq(name, "str_slice")
        true -> 1
        _ -> match str_eq(name, "sb_build")
          true -> 1
          _ -> match str_eq(name, "read_file")
            true -> 1
            _ -> match str_eq(name, "cstr_to_str")
              true -> 1
              _ -> 0

-- fn is_upper
is_upper c = c >= 65 && c <= 90

-- fn is_za_fn
is_za_fn za_fns name i =
  match i >= glyph_array_len(za_fns)
    true -> 0
    _ ->
      match glyph_str_eq(za_fns[i], name) == 1
        true -> 1
        _ -> is_za_fn(za_fns, name, i + 1)

-- fn itos
itos n = glyph_int_to_str(n)

-- fn jb_arr
jb_arr pool =
  json_pool_push(pool, mk_jnode(jn_array, 0, "", [], []))

-- fn jb_bool
jb_bool pool b =
  json_pool_push(pool, mk_jnode(jn_bool, b, "", [], []))

-- fn jb_int
jb_int pool n =
  json_pool_push(pool, mk_jnode(jn_int, n, "", [], []))

-- fn jb_null
jb_null pool =
  json_pool_push(pool, mk_jnode(jn_null, 0, "", [], []))

-- fn jb_obj
jb_obj pool =
  json_pool_push(pool, mk_jnode(jn_object, 0, "", [], []))

-- fn jb_push
jb_push pool arr val =
  node = pool[arr]
  array_push(node.items, val)
  arr

-- fn jb_put
jb_put pool obj key val =
  node = pool[obj]
  array_push(node.keys, key)
  array_push(node.items, val)
  obj

-- fn jb_str
jb_str pool s =
  json_pool_push(pool, mk_jnode(jn_str, 0, s, [], []))

-- fn jn_array
jn_array = 4

-- fn jn_bool
jn_bool = 1

-- fn jn_int
jn_int = 2

-- fn jn_null
jn_null = 0

-- fn jn_object
jn_object = 5

-- fn jn_str
jn_str = 3

-- fn join_args
join_args argv start end =
  match start >= end
    true -> ""
    _ ->
      match start == end - 1
        true -> argv[start]
        _ -> s3(argv[start], " ", join_args(argv, start + 1, end))

-- fn join_cols
join_cols row i n acc =
  match i < n
    true ->
      new_acc = match glyph_str_len(acc) == 0
        true -> row[i]
        _ -> s3(acc, "	", row[i])
      join_cols(row, i + 1, n, new_acc)
    _ -> acc

-- fn join_names
join_names rows i n acc =
  match i < n
    true ->
      row = rows[i]
      new_acc = match glyph_str_len(acc) == 0
        true -> row[0]
        _ -> s3(acc, ", ", row[0])
      join_names(rows, i + 1, n, new_acc)
    _ -> acc

-- fn join_str_arr
join_str_arr arr i =
  n = glyph_array_len(arr)
  match i >= n
    true -> ""
    _ ->
      match i == n - 1
        true -> arr[i]
        _ -> s3(arr[i], " ", join_str_arr(arr, i + 1))


-- fn json_arr_get
json_arr_get pool node_idx i =
  node = pool[node_idx]
  node.items[i]

-- fn json_arr_len
json_arr_len pool node_idx =
  node = pool[node_idx]
  array_len(node.items)

-- fn json_gen
json_gen pool idx =
  node = pool[idx]
  match node.tag
    0 -> "null"
    1 -> match node.nval
      true -> "true"
      _ -> "false"
    2 -> int_to_str(node.nval)
    3 -> json_gen_str(node.sval)
    4 -> json_gen_arr(pool, node)
    5 -> json_gen_obj(pool, node)
    _ -> "null"

-- fn json_gen_arr
json_gen_arr pool node =
  sb = sb_new()
  sb_append(sb, "[")
  json_gen_arr_loop(pool, node.items, 0, array_len(node.items), sb)
  sb_append(sb, "]")
  sb_build(sb)

-- fn json_gen_arr_loop
json_gen_arr_loop pool items i len sb =
  match i < len
    true ->
      match i > 0
        true -> sb_append(sb, ",")
        _ -> 0
      sb_append(sb, json_gen(pool, items[i]))
      json_gen_arr_loop(pool, items, i + 1, len, sb)
    _ -> 0

-- fn json_gen_obj
json_gen_obj pool node =
  sb = sb_new()
  sb_append(sb, "\{")
  json_gen_obj_loop(pool, node.keys, node.items, 0, array_len(node.keys), sb)
  sb_append(sb, "}")
  sb_build(sb)

-- fn json_gen_obj_loop
json_gen_obj_loop pool keys items i len sb =
  match i < len
    true ->
      match i > 0
        true -> sb_append(sb, ",")
        _ -> 0
      sb_append(sb, json_gen_str(keys[i]))
      sb_append(sb, ":")
      sb_append(sb, json_gen(pool, items[i]))
      json_gen_obj_loop(pool, keys, items, i + 1, len, sb)
    _ -> 0

-- fn json_gen_str
json_gen_str s =
  sb = sb_new()
  sb_append(sb, "\"")
  json_gen_str_loop(s, 0, str_len(s), sb)
  sb_append(sb, "\"")
  sb_build(sb)

-- fn json_gen_str_loop
json_gen_str_loop s pos len sb =
  match pos < len
    true ->
      c = str_char_at(s, pos)
      match c
        34 ->
          sb_append(sb, "\\\"")
          json_gen_str_loop(s, pos + 1, len, sb)
        92 ->
          sb_append(sb, "\\\\")
          json_gen_str_loop(s, pos + 1, len, sb)
        10 ->
          sb_append(sb, "\\n")
          json_gen_str_loop(s, pos + 1, len, sb)
        9 ->
          sb_append(sb, "\\t")
          json_gen_str_loop(s, pos + 1, len, sb)
        13 ->
          sb_append(sb, "\\r")
          json_gen_str_loop(s, pos + 1, len, sb)
        _ ->
          sb_append(sb, str_slice(s, pos, pos + 1))
          json_gen_str_loop(s, pos + 1, len, sb)
    _ -> 0

-- fn json_get
json_get pool node_idx key =
  node = pool[node_idx]
  json_get_loop(node.keys, node.items, key, 0)

-- fn json_get_int
json_get_int pool node_idx key =
  ci = json_get(pool, node_idx, key)
  match ci >= 0
    true -> pool[ci].nval
    _ -> 0 - 1

-- fn json_get_loop
json_get_loop keys items key i =
  match i < array_len(keys)
    true ->
      match str_eq(keys[i], key)
        true -> items[i]
        _ -> json_get_loop(keys, items, key, i + 1)
    _ -> 0 - 1

-- fn json_get_str
json_get_str pool node_idx key =
  ci = json_get(pool, node_idx, key)
  match ci >= 0
    true ->
      node = pool[ci]
      _ = node.tag
      node.sval
    _ -> ""

-- fn json_parse
json_parse src tokens pos pool =
  tok = tokens[pos]
  k = tok.kind
  match k
    7 -> json_parse_str(src, tokens, pos, pool)
    8 -> json_parse_num(src, tokens, pos, pool)
    1 -> json_parse_obj(src, tokens, pos + 1, pool)
    3 -> json_parse_arr(src, tokens, pos + 1, pool)
    9 ->
      idx = json_pool_push(pool, mk_jnode(jn_bool, 1, "", [], []))
      {node: idx, pos: pos + 1}
    10 ->
      idx = json_pool_push(pool, mk_jnode(jn_bool, 0, "", [], []))
      {node: idx, pos: pos + 1}
    _ ->
      idx = json_pool_push(pool, mk_jnode(jn_null, 0, "", [], []))
      {node: idx, pos: pos + 1}

-- fn json_parse_arr
json_parse_arr src tokens pos pool =
  items = []
  match tokens[pos].kind == jt_rbracket
    true ->
      idx = json_pool_push(pool, mk_jnode(jn_array, 0, "", items, []))
      {node: idx, pos: pos + 1}
    _ -> json_parse_arr_loop(src, tokens, pos, pool, items)

-- fn json_parse_arr_loop
json_parse_arr_loop src tokens pos pool items =
  vr = json_parse(src, tokens, pos, pool)
  array_push(items, vr.node)
  npos = vr.pos
  match tokens[npos].kind == jt_comma
    true -> json_parse_arr_loop(src, tokens, npos + 1, pool, items)
    _ ->
      idx = json_pool_push(pool, mk_jnode(jn_array, 0, "", items, []))
      {node: idx, pos: npos + 1}

-- fn json_parse_num
json_parse_num src tokens pos pool =
  tok = tokens[pos]
  s = str_slice(src, tok.start, tok.end)
  n = str_to_int(s)
  idx = json_pool_push(pool, mk_jnode(jn_int, n, "", [], []))
  {node: idx, pos: pos + 1}

-- fn json_parse_obj
json_parse_obj src tokens pos pool =
  keys = []
  items = []
  match tokens[pos].kind == jt_rbrace
    true ->
      idx = json_pool_push(pool, mk_jnode(jn_object, 0, "", items, keys))
      {node: idx, pos: pos + 1}
    _ -> json_parse_obj_loop(src, tokens, pos, pool, keys, items)

-- fn json_parse_obj_loop
json_parse_obj_loop src tokens pos pool keys items =
  ktok = tokens[pos]
  key = json_unescape(src, ktok.start, ktok.end)
  array_push(keys, key)
  vr = json_parse(src, tokens, pos + 2, pool)
  array_push(items, vr.node)
  npos = vr.pos
  match tokens[npos].kind == jt_comma
    true -> json_parse_obj_loop(src, tokens, npos + 1, pool, keys, items)
    _ ->
      idx = json_pool_push(pool, mk_jnode(jn_object, 0, "", items, keys))
      {node: idx, pos: npos + 1}

-- fn json_parse_str
json_parse_str src tokens pos pool =
  tok = tokens[pos]
  s = json_unescape(src, tok.start, tok.end)
  idx = json_pool_push(pool, mk_jnode(jn_str, 0, s, [], []))
  {node: idx, pos: pos + 1}

-- fn json_pool_push
json_pool_push pool node =
  idx = array_len(pool)
  array_push(pool, node)
  idx

-- fn json_scan_num
json_scan_num src pos =
  len = str_len(src)
  match pos < len
    true ->
      c = str_char_at(src, pos)
      match c >= 48
        true -> match c <= 57
          true -> json_scan_num(src, pos + 1)
          _ -> pos
        _ -> pos
    _ -> pos

-- fn json_scan_str
json_scan_str src pos =
  len = str_len(src)
  match pos < len
    true ->
      c = str_char_at(src, pos)
      match c == 34
        true -> pos + 1
        _ -> match c == 92
          true -> json_scan_str(src, pos + 2)
          _ -> json_scan_str(src, pos + 1)
    _ -> pos

-- fn json_skip_ws
json_skip_ws src pos =
  len = str_len(src)
  match pos < len
    true ->
      c = str_char_at(src, pos)
      match c
        32 | 9 | 10 | 13 -> json_skip_ws(src, pos + 1)
        _ -> pos
    _ -> pos

-- fn json_tok_loop
json_tok_loop src len pos tokens =
  p = json_skip_ws(src, pos)
  match p < len
    true -> json_tok_one(src, len, p, tokens, str_char_at(src, p))
    _ -> tokens

-- fn json_tok_one
json_tok_one src len pos tokens c =
  match c
    123 ->
      array_push(tokens, mk_token(jt_lbrace, pos, pos + 1, 0))
      json_tok_loop(src, len, pos + 1, tokens)
    125 ->
      array_push(tokens, mk_token(jt_rbrace, pos, pos + 1, 0))
      json_tok_loop(src, len, pos + 1, tokens)
    91 ->
      array_push(tokens, mk_token(jt_lbracket, pos, pos + 1, 0))
      json_tok_loop(src, len, pos + 1, tokens)
    93 ->
      array_push(tokens, mk_token(jt_rbracket, pos, pos + 1, 0))
      json_tok_loop(src, len, pos + 1, tokens)
    58 ->
      array_push(tokens, mk_token(jt_colon, pos, pos + 1, 0))
      json_tok_loop(src, len, pos + 1, tokens)
    44 ->
      array_push(tokens, mk_token(jt_comma, pos, pos + 1, 0))
      json_tok_loop(src, len, pos + 1, tokens)
    34 ->
      end = json_scan_str(src, pos + 1)
      array_push(tokens, mk_token(jt_string, pos + 1, end - 1, 0))
      json_tok_loop(src, len, end, tokens)
    _ -> json_tok_one2(src, len, pos, tokens, c)

-- fn json_tok_one2
json_tok_one2 src len pos tokens c =
  match c == 45 || (c >= 48 && c <= 57)
    true ->
      start = pos
      npos = match c == 45
        true -> pos + 1
        _ -> pos
      end = json_scan_num(src, npos)
      array_push(tokens, mk_token(jt_number, start, end, 0))
      json_tok_loop(src, len, end, tokens)
    _ ->
      match c == 116
        true ->
          array_push(tokens, mk_token(jt_true, pos, pos + 4, 0))
          json_tok_loop(src, len, pos + 4, tokens)
        _ ->
          match c == 102
            true ->
              array_push(tokens, mk_token(jt_false, pos, pos + 5, 0))
              json_tok_loop(src, len, pos + 5, tokens)
            _ ->
              match c == 110
                true ->
                  array_push(tokens, mk_token(jt_null, pos, pos + 4, 0))
                  json_tok_loop(src, len, pos + 4, tokens)
                _ -> json_tok_loop(src, len, pos + 1, tokens)

-- fn json_tokenize
json_tokenize src =
  tokens = []
  json_tok_loop(src, str_len(src), 0, tokens)

-- fn json_unesc_loop
json_unesc_loop src pos end sb =
  match pos < end
    true ->
      c = str_char_at(src, pos)
      match c == 92
        true ->
          match pos + 1 < end
            true ->
              c2 = str_char_at(src, pos + 1)
              match c2
                110 ->
                  sb_append(sb, "\n")
                  json_unesc_loop(src, pos + 2, end, sb)
                116 ->
                  sb_append(sb, "\t")
                  json_unesc_loop(src, pos + 2, end, sb)
                114 ->
                  sb_append(sb, "\r")
                  json_unesc_loop(src, pos + 2, end, sb)
                _ ->
                  sb_append(sb, str_slice(src, pos + 1, pos + 2))
                  json_unesc_loop(src, pos + 2, end, sb)
            _ -> sb_build(sb)
        _ ->
          sb_append(sb, str_slice(src, pos, pos + 1))
          json_unesc_loop(src, pos + 1, end, sb)
    _ -> sb_build(sb)

-- fn json_unescape
json_unescape src start end =
  json_unesc_loop(src, start, end, sb_new())

-- fn jt_colon
jt_colon = 5

-- fn jt_comma
jt_comma = 6

-- fn jt_false
jt_false = 10

-- fn jt_lbrace
jt_lbrace = 1

-- fn jt_lbracket
jt_lbracket = 3

-- fn jt_null
jt_null = 11

-- fn jt_number
jt_number = 8

-- fn jt_rbrace
jt_rbrace = 2

-- fn jt_rbracket
jt_rbracket = 4

-- fn jt_string
jt_string = 7

-- fn jt_true
jt_true = 9

-- fn keyword_kind
keyword_kind text =
  match glyph_str_eq(text, "match")
    true -> tk_match()
    _ -> match glyph_str_eq(text, "trait")
      true -> tk_trait()
      _ -> match glyph_str_eq(text, "impl")
        true -> tk_impl()
        _ -> match glyph_str_eq(text, "const")
          true -> tk_const()
          _ -> match glyph_str_eq(text, "extern")
            true -> tk_extern()
            _ -> match glyph_str_eq(text, "test")
              true -> tk_test()
              _ -> match glyph_str_eq(text, "as")
                true -> tk_as()
                _ -> keyword_kind2(text)

-- fn keyword_kind2
keyword_kind2 text =
  match glyph_str_eq(text, "bitand")
    true -> tk_bitand()
    _ -> match glyph_str_eq(text, "bitor")
      true -> tk_bitor()
      _ -> match glyph_str_eq(text, "bitxor")
        true -> tk_bitxor()
        _ -> match glyph_str_eq(text, "shl")
          true -> tk_shl()
          _ -> match glyph_str_eq(text, "shr")
            true -> tk_shr()
            _ -> 0

-- fn lib_seen
lib_seen seen lib i =
  match i >= glyph_array_len(seen)
    true -> 0
    _ ->
      match glyph_str_eq(seen[i], lib)
        true -> 1
        _ -> lib_seen(seen, lib, i + 1)

-- fn line_col
line_col src offset =
  line_col_loop(src, 0, offset, 1)

-- fn line_col_loop
line_col_loop src i offset col =
  match i >= offset
    true -> col
    _ ->
      match glyph_str_char_at(src, i) == 10
        true -> line_col_loop(src, i + 1, offset, 1)
        _ -> line_col_loop(src, i + 1, offset, col + 1)

-- fn lld_loop
lld_loop ctx names i tmp =
  match i >= glyph_array_len(names)
    true -> 0
    _ ->
      name = names[i]
      dest = mir_alloc_local(ctx, name)
      mir_emit_field(ctx, dest, mk_op_local(tmp), 0, name)
      mir_bind_var(ctx, name, dest)
      lld_loop(ctx, names, i + 1, tmp)


-- fn lookup_def_id
lookup_def_id db name =
  sql = s3("SELECT id FROM def WHERE name = '", sql_escape(name), "' AND kind = 'fn' LIMIT 1")
  rows = glyph_db_query_rows(db, sql)
  match glyph_array_len(rows) > 0
    true -> str_to_int(rows[0][0])
    _ -> 0

-- fn lookup_var_map
lookup_var_map var_map var_id i =
  match i >= glyph_array_len(var_map)
    true -> 0 - 1
    _ ->
      match var_map[i] == var_id
        true -> var_map[i + 1]
        _ -> lookup_var_map(var_map, var_id, i + 2)


-- fn lower_array
lower_array ctx ast node =
  ops = lower_call_args(ctx, ast, node.ns, 0)
  dest = mir_alloc_local(ctx, "")
  mir_emit_aggregate(ctx, dest, ag_array(), "", ops)
  mk_op_local(dest)

-- fn lower_assign
lower_assign ctx ast node =
  val = lower_expr(ctx, ast, node.n1)
  existing = mir_lookup_var(ctx, node.sval)
  match existing >= 0
    true ->
      mir_emit_use(ctx, existing, val)
      mk_op_unit()
    _ -> mk_op_unit()

-- fn lower_binary
lower_binary ctx ast node =
  left = lower_expr(ctx, ast, node.n1)
  right = lower_expr(ctx, ast, node.n2)
  match tctx_is_str_bin(ctx.tctx, node.n1, node.n2, ctx, left, right) == 1
    true -> lower_str_binop(ctx, node.ival, left, right)
    _ -> match tctx_is_float_bin(ctx.tctx, node.n1, node.n2, ctx, left, right) == 1
      true ->
        left2 = coerce_to_float(ctx, left)
        right2 = coerce_to_float(ctx, right)
        lower_float_binop(ctx, node.ival, left2, right2)
      _ ->
        mop = lower_binop(node.ival)
        dest = mir_alloc_local(ctx, "")
        mir_emit_binop(ctx, dest, mop, left, right)
        mk_op_local(dest)


-- fn lower_binop
lower_binop op =
  match op == op_add()
    true -> mir_add()
    _ -> match op == op_sub()
      true -> mir_sub()
      _ -> match op == op_mul()
        true -> mir_mul()
        _ -> match op == op_div()
          true -> mir_div()
          _ -> match op == op_mod()
            true -> mir_mod()
            _ -> match op == op_eq()
              true -> mir_eq()
              _ -> match op == op_neq()
                true -> mir_neq()
                _ -> match op == op_lt()
                  true -> mir_lt()
                  _ -> match op == op_gt()
                    true -> mir_gt()
                    _ -> match op == op_lt_eq()
                      true -> mir_lt_eq()
                      _ -> match op == op_gt_eq()
                        true -> mir_gt_eq()
                        _ -> match op == op_and()
                          true -> mir_and()
                          _ -> match op == op_or()
                            true -> mir_or()
                            _ -> lower_binop2(op)

-- fn lower_binop2
lower_binop2 op =
  match op == op_bitand()
    true -> mir_bitand()
    _ -> match op == op_bitor()
      true -> mir_bitor()
      _ -> match op == op_bitxor()
        true -> mir_bitxor()
        _ -> match op == op_shl()
          true -> mir_shl()
          _ -> match op == op_shr()
            true -> mir_shr()
            _ -> mir_add()

-- fn lower_block
lower_block ctx ast node =
  mir_push_scope(ctx)
  r = lower_stmts(ctx, ast, node.ns, 0)
  mir_pop_scope(ctx)
  r

-- fn lower_call
lower_call ctx ast node =
  callee_node = ast[node.n1]
  match callee_node.kind == ex_type_ident()
    true ->
      args = lower_call_args(ctx, ast, node.ns, 0)
      dest = mir_alloc_local(ctx, "")
      mir_emit_aggregate(ctx, dest, ag_variant(), callee_node.sval, args)
      mk_op_local(dest)
    _ ->
      callee = lower_callee_expr(ctx, ast, node.n1)
      args = lower_call_args(ctx, ast, node.ns, 0)
      dest = mir_alloc_local(ctx, "")
      mir_emit_call(ctx, dest, callee, args)
      track_call_type(ctx, dest, callee)
      mk_op_local(dest)

-- fn lower_call_args
lower_call_args ctx ast ns i =
  acc = []
  lower_call_args_acc(ctx, ast, ns, 0, acc)

-- fn lower_call_args_acc
lower_call_args_acc ctx ast ns i acc =
  match i >= glyph_array_len(ns)
    true -> acc
    _ ->
      arg = lower_expr(ctx, ast, ns[i])
      glyph_array_push(acc, arg)
      lower_call_args_acc(ctx, ast, ns, i + 1, acc)

-- fn lower_callee_expr
lower_callee_expr ctx ast ni =
  node = ast[ni]
  k = node.kind
  match k == ex_ident()
    true ->
      local_id = mir_lookup_var(ctx, node.sval)
      match local_id < 0
        true -> mk_op_func(node.sval)
        _ -> mk_op_local(local_id)
    _ -> lower_expr(ctx, ast, ni)

-- fn lower_expr
lower_expr ctx ast ni =
  node = ast[ni]
  k = node.kind
  match k
    _ ? k == ex_int_lit() -> mk_op_int(node.ival)
    _ ? k == ex_str_lit() -> mk_op_str(node.sval)
    _ ? k == ex_bool_lit() -> mk_op_bool(node.ival)
    _ ? k == ex_ident() -> lower_ident(ctx, node)
    _ ? k == ex_binary() -> lower_binary(ctx, ast, node)
    _ ? k == ex_unary() -> lower_unary(ctx, ast, node)
    _ ? k == ex_call() -> lower_call(ctx, ast, node)
    _ ? k == ex_type_ident() -> lower_type_ident(ctx, ast, node)
    _ ? k == ex_str_interp() -> lower_str_interp(ctx, ast, node)
    _ -> lower_expr2(ctx, ast, node, k)

-- fn lower_expr2
lower_expr2 ctx ast node k =
  match k
    _ ? k == ex_field_access() -> lower_field(ctx, ast, node)
    _ ? k == ex_index() -> lower_idx(ctx, ast, node)
    _ ? k == ex_array() -> lower_array(ctx, ast, node)
    _ ? k == ex_record() -> lower_record(ctx, ast, node)
    _ ? k == ex_match() -> lower_match(ctx, ast, node)
    _ ? k == ex_block() -> lower_block(ctx, ast, node)
    _ ? k == ex_pipe() -> lower_pipe(ctx, ast, node)
    _ ? k == ex_lambda() -> lower_lambda(ctx, ast, node)
    _ -> lower_expr3(ctx, ast, node, k)

-- fn lower_expr3
lower_expr3 ctx ast node k =
  match k
    _ ? k == ex_float_lit() ->
      dest = mir_alloc_local(ctx, "")
      mir_set_lt(ctx, dest, 3)
      mir_emit_use(ctx, dest, mk_op_float(node.sval))
      mk_op_local(dest)
    _ ? k == ex_propagate() -> lower_propagate(ctx, ast, node)
    _ ? k == ex_unwrap() -> lower_unwrap(ctx, ast, node)
    _ -> mk_op_unit()

-- fn lower_field
lower_field ctx ast node =
  base = lower_expr(ctx, ast, node.n1)
  dest = mir_alloc_local(ctx, "")
  mir_emit_field(ctx, dest, base, 0, node.sval)
  mk_op_local(dest)

-- fn lower_float_binop
lower_float_binop ctx ast_op left right =
  mop = lower_binop(ast_op)
  dest = mir_alloc_local(ctx, "")
  mir_emit_binop(ctx, dest, mop, left, right)
  _ = match is_arith_op(ast_op)
    true -> mir_set_lt(ctx, dest, 3)
    _ -> 0
  mk_op_local(dest)


-- fn lower_fn_def
lower_fn_def ast fn_idx za_fns tctx =
  node = ast[fn_idx]
  ctx = mk_mir_lower(node.sval, za_fns, tctx)
  entry = mir_new_block(ctx)
  glyph_array_set(ctx.fn_entry, 0, entry)
  lower_fn_params(ctx, ast, node.ns, 0)
  body_op = lower_expr(ctx, ast, node.n1)
  mir_terminate(ctx, mk_term_return(body_op))
  {fn_name: node.sval, fn_params: ctx.fn_params, fn_locals: ctx.local_names,
   fn_blocks_stmts: ctx.block_stmts, fn_blocks_terms: ctx.block_terms,
   fn_entry: entry, fn_subs: ctx.lifted_fns, fn_types: ctx.local_types}


-- fn lower_fn_params
lower_fn_params ctx ast params i =
  match i >= glyph_array_len(params)
    true -> 0
    _ ->
      pnode = ast[params[i]]
      local_id = mir_alloc_local(ctx, pnode.sval)
      tt = tctx_query(ctx.tctx, params[i])
      match tt > 0
        true -> mir_set_lt(ctx, local_id, tt)
        _ -> 0
      mir_bind_var(ctx, pnode.sval, local_id)
      glyph_array_push(ctx.fn_params, local_id)
      lower_fn_params(ctx, ast, params, i + 1)

-- fn lower_guard_body
lower_guard_body ctx ast arms result merge i next_block =
  guard_idx = arms[i + 2]
  match guard_idx < 0
    true ->
      body_val = lower_expr(ctx, ast, arms[i + 1])
      mir_emit_use(ctx, result, body_val)
      mir_terminate(ctx, mk_term_goto(merge))
      0
    _ ->
      guard_val = lower_expr(ctx, ast, guard_idx)
      body_block = mir_new_block(ctx)
      mir_terminate(ctx, mk_term_branch(guard_val, body_block, next_block))
      mir_switch_block(ctx, body_block)
      bv = lower_expr(ctx, ast, arms[i + 1])
      mir_emit_use(ctx, result, bv)
      mir_terminate(ctx, mk_term_goto(merge))
      0


-- fn lower_ident
lower_ident ctx node =
  local_id = mir_lookup_var(ctx, node.sval)
  match local_id < 0
    true ->
      match is_za_fn(ctx.za_fns, node.sval, 0) == 1
        true ->
          dest = mir_alloc_local(ctx, "")
          mir_emit_call(ctx, dest, mk_op_func(node.sval), [])
          mk_op_local(dest)
        _ -> mk_op_func(node.sval)
    _ -> mk_op_local(local_id)

-- fn lower_idx
lower_idx ctx ast node =
  arr = lower_expr(ctx, ast, node.n1)
  idx = lower_expr(ctx, ast, node.n2)
  dest = mir_alloc_local(ctx, "")
  mir_emit_index(ctx, dest, arr, idx)
  mk_op_local(dest)

-- fn lower_lambda
lower_lambda ctx ast node =
  free_vars = collect_free_vars(ctx, ast, node.n1, node.ns)
  ctr = ctx.lambda_ctr[0]
  glyph_array_set(ctx.lambda_ctr, 0, ctr + 1)
  lam_name = s3(ctx.outer_fn_name, "_lam_", itos(ctr))
  lam_ctx = mk_mir_lower(lam_name, ctx.za_fns, ctx.tctx)
  entry = mir_new_block(lam_ctx)
  glyph_array_set(lam_ctx.fn_entry, 0, entry)
  env_local = mir_alloc_local(lam_ctx, "__env")
  glyph_array_push(lam_ctx.fn_params, env_local)
  mir_bind_var(lam_ctx, "__env", env_local)
  lower_lambda_params_full(lam_ctx, ast, node.ns, 0)
  emit_capture_loads(lam_ctx, free_vars, env_local, 0)
  body_op = lower_expr(lam_ctx, ast, node.n1)
  mir_terminate(lam_ctx, mk_term_return(body_op))
  lam_mir = {fn_name: lam_name, fn_params: lam_ctx.fn_params, fn_locals: lam_ctx.local_names, fn_blocks_stmts: lam_ctx.block_stmts, fn_blocks_terms: lam_ctx.block_terms, fn_entry: entry, fn_subs: [], fn_types: lam_ctx.local_types}
  collect_nested_lifted(ctx.lifted_fns, lam_ctx.lifted_fns, 0)
  glyph_array_push(ctx.lifted_fns, lam_mir)
  dest = mir_alloc_local(ctx, "")
  capture_ops = build_capture_ops(free_vars, 0)
  mir_emit(ctx, mk_stmt(dest, rv_make_closure(), 0, lam_name, mk_op_nil(), mk_op_nil(), capture_ops))
  mk_op_local(dest)


-- fn lower_lambda_params
lower_lambda_params ctx ast params i =
  match i >= glyph_array_len(params)
    true -> 0
    _ ->
      pnode = ast[params[i]]
      local_id = mir_alloc_local(ctx, pnode.sval)
      mir_bind_var(ctx, pnode.sval, local_id)
      lower_lambda_params(ctx, ast, params, i + 1)

-- fn lower_lambda_params_full
lower_lambda_params_full ctx ast params i =
  match i >= glyph_array_len(params)
    true -> 0
    _ ->
      pnode = ast[params[i]]
      local_id = mir_alloc_local(ctx, pnode.sval)
      mir_bind_var(ctx, pnode.sval, local_id)
      glyph_array_push(ctx.fn_params, local_id)
      lower_lambda_params_full(ctx, ast, params, i + 1)


-- fn lower_let
lower_let ctx ast node =
  val = lower_expr(ctx, ast, node.n1)
  local_id = mir_alloc_local(ctx, node.sval)
  mir_emit_use(ctx, local_id, val)
  ot = op_type(ctx, val)
  tt = tctx_query(ctx.tctx, node.n1)
  ty = match ot > 0
    true -> ot
    _ -> match tt > 0
      true -> tt
      _ -> 0
  mir_set_lt(ctx, local_id, ty)
  mir_bind_var(ctx, node.sval, local_id)
  mk_op_unit()

-- fn lower_let_destr
lower_let_destr ctx ast node =
  val = lower_expr(ctx, ast, node.n1)
  tmp = mir_alloc_local(ctx, "_d")
  mir_emit_use(ctx, tmp, val)
  ot = op_type(ctx, val)
  tt = tctx_query(ctx.tctx, node.n1)
  ty = match ot > 0
    true -> ot
    _ -> match tt > 0
      true -> tt
      _ -> 0
  mir_set_lt(ctx, tmp, ty)
  names = node.ns
  lld_loop(ctx, names, 0, tmp)
  mk_op_unit()


-- fn lower_match
lower_match ctx ast node =
  scrutinee = lower_expr(ctx, ast, node.n1)
  result = mir_alloc_local(ctx, "")
  merge = mir_new_block(ctx)
  lower_match_arms(ctx, ast, node.ns, scrutinee, result, merge, 0)
  mir_switch_block(ctx, merge)
  mk_op_local(result)

-- fn lower_match_arms
lower_match_arms ctx ast arms scrutinee result merge i =
  match i >= glyph_array_len(arms)
    true ->
      mir_terminate(ctx, mk_term_unreachable())
      0
    _ ->
      pat_node = ast[arms[i]]
      pk = pat_node.kind
      match pk
        _ ? pk == pat_wildcard() -> lower_match_wildcard(ctx, ast, arms, scrutinee, result, merge, i)
        _ ? pk == pat_ident() -> lower_match_ident(ctx, ast, arms, scrutinee, result, merge, i, pat_node)
        _ ? pk == pat_bool() -> lower_match_bool(ctx, ast, arms, scrutinee, result, merge, i, pat_node)
        _ ? pk == pat_int() -> lower_match_int(ctx, ast, arms, scrutinee, result, merge, i, pat_node)
        _ ? pk == pat_str() -> lower_match_str(ctx, ast, arms, scrutinee, result, merge, i, pat_node)
        _ ? pk == pat_ctor() -> lower_match_ctor(ctx, ast, arms, scrutinee, result, merge, i, pat_node)
        _ ? pk == pat_or() -> lower_match_or(ctx, ast, arms, scrutinee, result, merge, i, pat_node)
        _ -> lower_match_wildcard(ctx, ast, arms, scrutinee, result, merge, i)

-- fn lower_match_bool
lower_match_bool ctx ast arms scrutinee result merge i pat_node =
  match_block = mir_new_block(ctx)
  next_block = mir_new_block(ctx)
  cmp = mir_alloc_local(ctx, "")
  mir_emit_binop(ctx, cmp, mir_eq(), scrutinee, mk_op_bool(pat_node.ival))
  mir_terminate(ctx, mk_term_branch(mk_op_local(cmp), match_block, next_block))
  mir_switch_block(ctx, match_block)
  lower_guard_body(ctx, ast, arms, result, merge, i, next_block)
  mir_switch_block(ctx, next_block)
  lower_match_arms(ctx, ast, arms, scrutinee, result, merge, i + 3)


-- fn lower_match_ctor
lower_match_ctor ctx ast arms scrutinee result merge i pat_node =
  match_block = mir_new_block(ctx)
  next_block = mir_new_block(ctx)
  disc_local = mir_alloc_local(ctx, "")
  mir_emit_field(ctx, disc_local, scrutinee, 0, "__tag")
  expected = variant_discriminant(pat_node.sval)
  cmp = mir_alloc_local(ctx, "")
  mir_emit_binop(ctx, cmp, mir_eq(), mk_op_local(disc_local), mk_op_int(expected))
  mir_terminate(ctx, mk_term_branch(mk_op_local(cmp), match_block, next_block))
  mir_switch_block(ctx, match_block)
  mir_push_scope(ctx)
  bind_ctor_fields(ctx, ast, pat_node.ns, scrutinee, 0)
  guard_idx = arms[i + 2]
  match guard_idx < 0
    true ->
      body_val = lower_expr(ctx, ast, arms[i + 1])
      mir_emit_use(ctx, result, body_val)
      mir_pop_scope(ctx)
      mir_terminate(ctx, mk_term_goto(merge))
      mir_switch_block(ctx, next_block)
      lower_match_arms(ctx, ast, arms, scrutinee, result, merge, i + 3)
    _ ->
      gv = lower_expr(ctx, ast, guard_idx)
      gb = mir_new_block(ctx)
      mir_terminate(ctx, mk_term_branch(gv, gb, next_block))
      mir_switch_block(ctx, gb)
      bv = lower_expr(ctx, ast, arms[i + 1])
      mir_emit_use(ctx, result, bv)
      mir_terminate(ctx, mk_term_goto(merge))
      mir_switch_block(ctx, next_block)
      mir_pop_scope(ctx)
      lower_match_arms(ctx, ast, arms, scrutinee, result, merge, i + 3)


-- fn lower_match_ident
lower_match_ident ctx ast arms scrutinee result merge i pat_node =
  mir_push_scope(ctx)
  local_id = mir_alloc_local(ctx, pat_node.sval)
  mir_emit_use(ctx, local_id, scrutinee)
  mir_bind_var(ctx, pat_node.sval, local_id)
  guard_idx = arms[i + 2]
  match guard_idx < 0
    true ->
      body_val = lower_expr(ctx, ast, arms[i + 1])
      mir_emit_use(ctx, result, body_val)
      mir_pop_scope(ctx)
      mir_terminate(ctx, mk_term_goto(merge))
      0
    _ ->
      guard_val = lower_expr(ctx, ast, guard_idx)
      body_block = mir_new_block(ctx)
      next_block = mir_new_block(ctx)
      mir_terminate(ctx, mk_term_branch(guard_val, body_block, next_block))
      mir_switch_block(ctx, body_block)
      bv = lower_expr(ctx, ast, arms[i + 1])
      mir_emit_use(ctx, result, bv)
      mir_terminate(ctx, mk_term_goto(merge))
      mir_switch_block(ctx, next_block)
      mir_pop_scope(ctx)
      lower_match_arms(ctx, ast, arms, scrutinee, result, merge, i + 3)


-- fn lower_match_int
lower_match_int ctx ast arms scrutinee result merge i pat_node =
  match_block = mir_new_block(ctx)
  next_block = mir_new_block(ctx)
  cmp = mir_alloc_local(ctx, "")
  mir_emit_binop(ctx, cmp, mir_eq(), scrutinee, mk_op_int(pat_node.ival))
  mir_terminate(ctx, mk_term_branch(mk_op_local(cmp), match_block, next_block))
  mir_switch_block(ctx, match_block)
  lower_guard_body(ctx, ast, arms, result, merge, i, next_block)
  mir_switch_block(ctx, next_block)
  lower_match_arms(ctx, ast, arms, scrutinee, result, merge, i + 3)


-- fn lower_match_or
lower_match_or ctx ast arms scrutinee result merge i pat_node =
  body_block = mir_new_block(ctx)
  next_block = mir_new_block(ctx)
  subs = pat_node.ns
  lower_or_subs(ctx, ast, subs, scrutinee, body_block, next_block, 0)
  mir_switch_block(ctx, body_block)
  lower_guard_body(ctx, ast, arms, result, merge, i, next_block)
  mir_switch_block(ctx, next_block)
  lower_match_arms(ctx, ast, arms, scrutinee, result, merge, i + 3)


-- fn lower_match_str
lower_match_str ctx ast arms scrutinee result merge i pat_node =
  match_block = mir_new_block(ctx)
  next_block = mir_new_block(ctx)
  eq_result = mir_alloc_local(ctx, "")
  mir_emit_call(ctx, eq_result, mk_op_func("glyph_str_eq"), [scrutinee, mk_op_str(pat_node.sval)])
  cmp = mir_alloc_local(ctx, "")
  mir_emit_binop(ctx, cmp, mir_neq(), mk_op_local(eq_result), mk_op_int(0))
  mir_terminate(ctx, mk_term_branch(mk_op_local(cmp), match_block, next_block))
  mir_switch_block(ctx, match_block)
  lower_guard_body(ctx, ast, arms, result, merge, i, next_block)
  mir_switch_block(ctx, next_block)
  lower_match_arms(ctx, ast, arms, scrutinee, result, merge, i + 3)


-- fn lower_match_wildcard
lower_match_wildcard ctx ast arms scrutinee result merge i =
  guard_idx = arms[i + 2]
  match guard_idx < 0
    true ->
      body_val = lower_expr(ctx, ast, arms[i + 1])
      mir_emit_use(ctx, result, body_val)
      mir_terminate(ctx, mk_term_goto(merge))
      0
    _ ->
      guard_val = lower_expr(ctx, ast, guard_idx)
      body_block = mir_new_block(ctx)
      next_block = mir_new_block(ctx)
      mir_terminate(ctx, mk_term_branch(guard_val, body_block, next_block))
      mir_switch_block(ctx, body_block)
      bv = lower_expr(ctx, ast, arms[i + 1])
      mir_emit_use(ctx, result, bv)
      mir_terminate(ctx, mk_term_goto(merge))
      mir_switch_block(ctx, next_block)
      lower_match_arms(ctx, ast, arms, scrutinee, result, merge, i + 3)


-- fn lower_or_subs
lower_or_subs ctx ast subs scrutinee body_block next_block i =
  match i >= glyph_array_len(subs)
    true -> mir_terminate(ctx, mk_term_goto(next_block))
    _ ->
      sub = ast[subs[i]]
      is_last = i == glyph_array_len(subs) - 1
      fail_target = match is_last
        true -> next_block
        _ -> mir_new_block(ctx)
      pk = sub.kind
      match pk == pat_int()
        true ->
          cmp = mir_alloc_local(ctx, "")
          mir_emit_binop(ctx, cmp, mir_eq(), scrutinee, mk_op_int(sub.ival))
          mir_terminate(ctx, mk_term_branch(mk_op_local(cmp), body_block, fail_target))
        _ -> match pk == pat_str()
          true ->
            eq_result = mir_alloc_local(ctx, "")
            mir_emit_call(ctx, eq_result, mk_op_func("glyph_str_eq"), [scrutinee, mk_op_str(sub.sval)])
            cmp = mir_alloc_local(ctx, "")
            mir_emit_binop(ctx, cmp, mir_neq(), mk_op_local(eq_result), mk_op_int(0))
            mir_terminate(ctx, mk_term_branch(mk_op_local(cmp), body_block, fail_target))
          _ -> match pk == pat_bool()
            true ->
              cmp = mir_alloc_local(ctx, "")
              mir_emit_binop(ctx, cmp, mir_eq(), scrutinee, mk_op_bool(sub.ival))
              mir_terminate(ctx, mk_term_branch(mk_op_local(cmp), body_block, fail_target))
            _ -> mir_terminate(ctx, mk_term_goto(body_block))
      match is_last
        true -> 0
        _ ->
          mir_switch_block(ctx, fail_target)
          lower_or_subs(ctx, ast, subs, scrutinee, body_block, next_block, i + 1)


-- fn lower_pipe
lower_pipe ctx ast node =
  left = lower_expr(ctx, ast, node.n1)
  right = lower_callee_expr(ctx, ast, node.n2)
  dest = mir_alloc_local(ctx, "")
  mir_emit_call(ctx, dest, right, [left])
  mk_op_local(dest)

-- fn lower_propagate
lower_propagate ctx ast node =
  inner = lower_expr(ctx, ast, node.n1)
  tag = mir_alloc_local(ctx, "")
  mir_emit_field(ctx, tag, inner, 0, "")
  cmp = mir_alloc_local(ctx, "")
  mir_emit_binop(ctx, cmp, mir_eq(), mk_op_local(tag), mk_op_int(1))
  bb_err = mir_new_block(ctx)
  bb_ok = mir_new_block(ctx)
  mir_terminate(ctx, mk_term_branch(mk_op_local(cmp), bb_err, bb_ok))
  mir_switch_block(ctx, bb_err)
  mir_terminate(ctx, mk_term_return(inner))
  mir_switch_block(ctx, bb_ok)
  val = mir_alloc_local(ctx, "")
  mir_emit_field(ctx, val, inner, 1, "")
  mk_op_local(val)

-- fn lower_record
lower_record ctx ast node =
  r = lower_record_fields(ctx, ast, node.ns, 0)
  dest = mir_alloc_local(ctx, "")
  mir_emit_aggregate(ctx, dest, ag_record(), r.ostr, r.sops)
  mk_op_local(dest)

-- fn lower_record_fields
lower_record_fields ctx ast ns i =
  acc_names = ""
  acc_ops = []
  lower_record_fields_acc(ctx, ast, ns, 0, acc_names, acc_ops)

-- fn lower_record_fields_acc
lower_record_fields_acc ctx ast ns i acc_names acc_ops =
  match i >= glyph_array_len(ns)
    true -> {ostr: acc_names, sops: acc_ops}
    _ ->
      name_node = ast[ns[i]]
      val_op = lower_expr(ctx, ast, ns[i + 1])
      glyph_array_push(acc_ops, val_op)
      new_names = match glyph_str_len(acc_names) == 0
        true -> name_node.sval
        _ -> s3(acc_names, ",", name_node.sval)
      lower_record_fields_acc(ctx, ast, ns, i + 2, new_names, acc_ops)

-- fn lower_stmt
lower_stmt ctx ast si =
  node = ast[si]
  k = node.kind
  match k
    _ ? k == st_expr() -> lower_expr(ctx, ast, node.n1)
    _ ? k == st_let() -> lower_let(ctx, ast, node)
    _ ? k == st_assign() -> lower_assign(ctx, ast, node)
    _ ? k == st_let_destr() -> lower_let_destr(ctx, ast, node)
    _ -> mk_op_unit()

-- fn lower_stmts
lower_stmts ctx ast stmts i =
  match i >= glyph_array_len(stmts)
    true -> mk_op_unit()
    _ ->
      r = lower_stmt(ctx, ast, stmts[i])
      match i == glyph_array_len(stmts) - 1
        true -> r
        _ -> lower_stmts(ctx, ast, stmts, i + 1)

-- fn lower_str_binop
lower_str_binop ctx ast_op left right =
  match ast_op == op_add()
    true ->
      dest = mir_alloc_local(ctx, "")
      mir_set_lt(ctx, dest, 4)
      args = []
      glyph_array_push(args, left)
      glyph_array_push(args, right)
      mir_emit_call(ctx, dest, mk_op_func("str_concat"), args)
      mk_op_local(dest)
    _ -> match ast_op == op_eq()
      true ->
        dest = mir_alloc_local(ctx, "")
        args = []
        glyph_array_push(args, left)
        glyph_array_push(args, right)
        mir_emit_call(ctx, dest, mk_op_func("str_eq"), args)
        mk_op_local(dest)
      _ -> match ast_op == op_neq()
        true ->
          tmp = mir_alloc_local(ctx, "")
          args = []
          glyph_array_push(args, left)
          glyph_array_push(args, right)
          mir_emit_call(ctx, tmp, mk_op_func("str_eq"), args)
          dest = mir_alloc_local(ctx, "")
          mir_emit_binop(ctx, dest, mir_eq(), mk_op_local(tmp), mk_op_int(0))
          mk_op_local(dest)
        _ ->
          mop = lower_binop(ast_op)
          dest = mir_alloc_local(ctx, "")
          mir_emit_binop(ctx, dest, mop, left, right)
          mk_op_local(dest)


-- fn lower_str_interp
lower_str_interp ctx ast node =
  ops = lower_str_interp_parts(ctx, ast, node.ns, 0, [])
  dest = mir_alloc_local(ctx, "")
  mir_emit(ctx, mk_stmt(dest, rv_str_interp(), 0, "", mk_op_nil(), mk_op_nil(), ops))
  mk_op_local(dest)

-- fn lower_str_interp_parts
lower_str_interp_parts ctx ast parts i acc =
  match i >= glyph_array_len(parts)
    true -> acc
    _ ->
      pnode = ast[parts[i]]
      match pnode.kind == ex_str_lit()
        true ->
          glyph_array_push(acc, mk_op_str(pnode.sval))
          lower_str_interp_parts(ctx, ast, parts, i + 1, acc)
        _ ->
          val = lower_expr(ctx, ast, parts[i])
          conv = mir_alloc_local(ctx, "")
          vt = op_type(ctx, val)
          conv_fn = match vt == 3
            true -> "glyph_float_to_str"
            _ -> "glyph_int_to_str"
          mir_emit_call(ctx, conv, mk_op_func(conv_fn), [val])
          glyph_array_push(acc, mk_op_local(conv))
          lower_str_interp_parts(ctx, ast, parts, i + 1, acc)


-- fn lower_type_ident
lower_type_ident st pool node =
  dest = mir_alloc_local(st, "_nullvar")
  mir_emit_aggregate(st, dest, ag_variant(), node.sval, [])
  mk_op_local(dest)

-- fn lower_unary
lower_unary ctx ast node =
  operand = lower_expr(ctx, ast, node.n1)
  mop = lower_unop(node.ival)
  dest = mir_alloc_local(ctx, "")
  mir_emit_unop(ctx, dest, mop, operand)
  _ = match mop == mir_neg()
    true -> match op_type(ctx, operand) == 3
      true -> mir_set_lt(ctx, dest, 3)
      _ -> 0
    _ -> 0
  mk_op_local(dest)


-- fn lower_unop
lower_unop op =
  match op == op_neg()
    true -> mir_neg()
    _ -> mir_not()

-- fn lower_unwrap
lower_unwrap ctx ast node =
  inner = lower_expr(ctx, ast, node.n1)
  tag = mir_alloc_local(ctx, "")
  mir_emit_field(ctx, tag, inner, 0, "")
  cmp = mir_alloc_local(ctx, "")
  mir_emit_binop(ctx, cmp, mir_neq(), mk_op_local(tag), mk_op_int(0))
  bb_panic = mir_new_block(ctx)
  bb_ok = mir_new_block(ctx)
  mir_terminate(ctx, mk_term_branch(mk_op_local(cmp), bb_panic, bb_ok))
  mir_switch_block(ctx, bb_panic)
  p = mir_alloc_local(ctx, "")
  mir_emit_call(ctx, p, mk_op_func("eprintln"), [mk_op_str("panic: unwrap failed")])
  p2 = mir_alloc_local(ctx, "")
  mir_emit_call(ctx, p2, mk_op_func("exit"), [mk_op_int(1)])
  mir_terminate(ctx, mk_term_unreachable())
  mir_switch_block(ctx, bb_ok)
  val = mir_alloc_local(ctx, "")
  mir_emit_field(ctx, val, inner, 1, "")
  mk_op_local(val)

-- fn main
main =
  argv = glyph_args()
  argc = glyph_array_len(argv)
  match argc < 2
    true -> print_usage
    _ -> dispatch_cmd(argv, argc, argv[1])

-- fn make_caret
make_caret col =
  make_spaces(col - 1, "")

-- fn make_empty_2d
make_empty_2d n i =
  result = []
  fill_empty_2d(result, n, 0)
  result

-- fn make_inst_map
make_inst_map eng var_ids i =
  match i >= glyph_array_len(var_ids)
    true -> []
    _ ->
      old_id = var_ids[i]
      new_var = subst_fresh(eng)
      rest = make_inst_map(eng, var_ids, i + 1)
      glyph_array_push(rest, old_id)
      glyph_array_push(rest, new_var)
      rest


-- fn make_spaces
make_spaces n acc =
  match n <= 0
    true -> s2(acc, "^")
    _ -> make_spaces(n - 1, s2(acc, " "))

-- fn mcp_add_tools
mcp_add_tools pool tools =
  rn = jb_arr(pool)
  jb_push(pool, rn, jb_str(pool, "name"))
  p1 = jb_obj(pool)
  jb_put(pool, p1, "name", mcp_str_prop(pool, "Definition name"))
  jb_put(pool, p1, "kind", mcp_str_prop(pool, "Definition kind (fn, type, test). Default: fn"))
  jb_put(pool, p1, "db", mcp_str_prop(pool, "Database path. Default: CLI arg"))
  jb_push(pool, tools, mcp_tool_schema(pool, "get_def", "Read a definition from the database", p1, rn))
  mcp_add_tools2(pool, tools)

-- fn mcp_add_tools2
mcp_add_tools2 pool tools =
  rnb = jb_arr(pool)
  jb_push(pool, rnb, jb_str(pool, "name"))
  jb_push(pool, rnb, jb_str(pool, "body"))
  p2 = jb_obj(pool)
  jb_put(pool, p2, "name", mcp_str_prop(pool, "Definition name"))
  jb_put(pool, p2, "kind", mcp_str_prop(pool, "Definition kind (fn, type, test)"))
  jb_put(pool, p2, "body", mcp_str_prop(pool, "Definition body (source code)"))
  jb_put(pool, p2, "gen", mcp_int_prop(pool, "Generation (1 or 2). Default: 1"))
  jb_put(pool, p2, "db", mcp_str_prop(pool, "Database path. Default: CLI arg"))
  jb_push(pool, tools, mcp_tool_schema(pool, "put_def", "Insert or update a definition", p2, rnb))
  mcp_add_tools3(pool, tools)

-- fn mcp_add_tools3
mcp_add_tools3 pool tools =
  p3 = jb_obj(pool)
  jb_put(pool, p3, "kind", mcp_str_prop(pool, "Filter by kind (fn, type, test)"))
  jb_put(pool, p3, "pattern", mcp_str_prop(pool, "SQL LIKE pattern for name"))
  jb_put(pool, p3, "gen", mcp_int_prop(pool, "Filter by generation"))
  jb_put(pool, p3, "db", mcp_str_prop(pool, "Database path. Default: CLI arg"))
  jb_push(pool, tools, mcp_tool_schema(pool, "list_defs", "List definitions matching filters", p3, jb_arr(pool)))
  mcp_add_tools4(pool, tools)

-- fn mcp_add_tools4
mcp_add_tools4 pool tools =
  rp = jb_arr(pool)
  jb_push(pool, rp, jb_str(pool, "pattern"))
  p4 = jb_obj(pool)
  jb_put(pool, p4, "pattern", mcp_str_prop(pool, "SQL LIKE pattern to search in definition bodies"))
  jb_put(pool, p4, "kind", mcp_str_prop(pool, "Filter by kind"))
  jb_put(pool, p4, "db", mcp_str_prop(pool, "Database path. Default: CLI arg"))
  jb_push(pool, tools, mcp_tool_schema(pool, "search_defs", "Search definition bodies", p4, rp))
  mcp_add_tools5(pool, tools)

-- fn mcp_add_tools5
mcp_add_tools5 pool tools =
  rn = jb_arr(pool)
  jb_push(pool, rn, jb_str(pool, "name"))
  p5 = jb_obj(pool)
  jb_put(pool, p5, "name", mcp_str_prop(pool, "Definition name"))
  jb_put(pool, p5, "kind", mcp_str_prop(pool, "Definition kind. Default: fn"))
  jb_put(pool, p5, "db", mcp_str_prop(pool, "Database path. Default: CLI arg"))
  jb_push(pool, tools, mcp_tool_schema(pool, "remove_def", "Delete a definition", p5, rn))
  mcp_add_tools6(pool, tools)

-- fn mcp_add_tools6
mcp_add_tools6 pool tools =
  rn = jb_arr(pool)
  jb_push(pool, rn, jb_str(pool, "name"))
  p6 = jb_obj(pool)
  jb_put(pool, p6, "name", mcp_str_prop(pool, "Definition name to query dependencies for"))
  jb_put(pool, p6, "db", mcp_str_prop(pool, "Database path. Default: CLI arg"))
  jb_push(pool, tools, mcp_tool_schema(pool, "deps", "Get dependencies of a definition", p6, rn))
  p7 = jb_obj(pool)
  jb_put(pool, p7, "name", mcp_str_prop(pool, "Definition name to query reverse dependencies for"))
  jb_put(pool, p7, "db", mcp_str_prop(pool, "Database path. Default: CLI arg"))
  jb_push(pool, tools, mcp_tool_schema(pool, "rdeps", "Get reverse dependencies (callers) of a definition", p7, rn))
  rq = jb_arr(pool)
  jb_push(pool, rq, jb_str(pool, "query"))
  p8 = jb_obj(pool)
  jb_put(pool, p8, "query", mcp_str_prop(pool, "SQL query to execute"))
  jb_put(pool, p8, "db", mcp_str_prop(pool, "Database path. Default: CLI arg"))
  jb_push(pool, tools, mcp_tool_schema(pool, "sql", "Execute a raw SQL query", p8, rq))
  mcp_add_tools7(pool, tools)


-- fn mcp_add_tools7
mcp_add_tools7 pool tools =
  rb = jb_arr(pool)
  jb_push(pool, rb, jb_str(pool, "body"))
  p9 = jb_obj(pool)
  jb_put(pool, p9, "kind", mcp_str_prop(pool, "Definition kind (fn, type, test). Default: fn"))
  jb_put(pool, p9, "body", mcp_str_prop(pool, "Definition body (source code)"))
  jb_put(pool, p9, "db", mcp_str_prop(pool, "Database path. Default: CLI arg"))
  jb_push(pool, tools, mcp_tool_schema(pool, "check_def", "Validate a definition without inserting", p9, rb))
  mcp_add_tools8(pool, tools)


-- fn mcp_add_tools8
mcp_add_tools8 pool tools =
  p8 = jb_obj(pool)
  jb_put(pool, p8, "db", mcp_str_prop(pool, "Database path. Default: CLI arg"))
  rb = jb_arr(pool)
  jb_push(pool, tools, mcp_tool_schema(pool, "coverage", "Get code coverage report from last test run", p8, rb))
  0


-- fn mcp_bool_prop
mcp_bool_prop pool desc =
  p = jb_obj(pool)
  jb_put(pool, p, "type", jb_str(pool, "boolean"))
  jb_put(pool, p, "description", jb_str(pool, desc))
  p

-- fn mcp_cov_add_uncovered
mcp_cov_add_uncovered pool arr names hits i =
  match i >= glyph_array_len(names)
    true -> 0
    _ ->
      match hits[i] == 0
        true -> jb_push(pool, arr, jb_str(pool, names[i]))
        _ -> 0
      mcp_cov_add_uncovered(pool, arr, names, hits, i + 1)


-- fn mcp_dispatch
mcp_dispatch db_path line =
  pool = []
  tokens = json_tokenize(line)
  pr = json_parse(line, tokens, 0, pool)
  root = pr.node
  method = json_get_str(pool, root, "method")
  id = json_get_int(pool, root, "id")
  match str_eq(method, "initialize")
    true -> mcp_initialize(id)
    _ -> match str_eq(method, "notifications/initialized")
      true -> ""
      _ -> match str_eq(method, "tools/list")
        true -> mcp_tools_list(id)
        _ -> match str_eq(method, "tools/call")
          true -> mcp_tools_call(db_path, id, pool, root)
          _ -> match str_eq(method, "ping")
            true -> mcp_respond(id, "\{}")
            _ -> mcp_error(id, 0 - 32601, "Method not found")

-- fn mcp_do_put
mcp_do_put db_path id pool name kind body gen =
  db = glyph_db_open(db_path)
  del_sql = s7("DELETE FROM def WHERE name='", sql_escape(name), "' AND kind='", sql_escape(kind), "' AND gen=", int_to_str(gen), "")
  glyph_db_exec(db, del_sql)
  ins_sql = mcp_put_insert(name, kind, body, gen)
  glyph_db_exec(db, ins_sql)
  glyph_db_close(db)
  rp = []
  r = jb_obj(rp)
  jb_put(rp, r, "status", jb_str(rp, "ok"))
  jb_put(rp, r, "name", jb_str(rp, name))
  mcp_text_result(id, json_gen(rp, r))

-- fn mcp_error
mcp_error id code msg =
  sb = sb_new()
  sb_append(sb, "\{\"jsonrpc\":\"2.0\",\"id\":")
  sb_append(sb, int_to_str(id))
  sb_append(sb, ",\"error\":\{\"code\":")
  sb_append(sb, int_to_str(code))
  sb_append(sb, ",\"message\":")
  sb_append(sb, json_gen_str(msg))
  sb_append(sb, "}}")
  sb_build(sb)

-- fn mcp_fmt_dep_loop
mcp_fmt_dep_loop rp arr rows i len =
  match i < len
    true ->
      row = rows[i]
      item = jb_obj(rp)
      jb_put(rp, item, "name", jb_str(rp, row[0]))
      jb_put(rp, item, "kind", jb_str(rp, row[1]))
      jb_push(rp, arr, item)
      mcp_fmt_dep_loop(rp, arr, rows, i + 1, len)
    _ -> 0

-- fn mcp_fmt_list_loop
mcp_fmt_list_loop rp arr rows i len =
  match i < len
    true ->
      row = rows[i]
      item = jb_obj(rp)
      jb_put(rp, item, "name", jb_str(rp, row[0]))
      jb_put(rp, item, "kind", jb_str(rp, row[1]))
      jb_put(rp, item, "gen", jb_int(rp, str_to_int(row[2])))
      jb_put(rp, item, "tokens", jb_int(rp, str_to_int(row[3])))
      jb_push(rp, arr, item)
      mcp_fmt_list_loop(rp, arr, rows, i + 1, len)
    _ -> 0

-- fn mcp_fmt_search_loop
mcp_fmt_search_loop rp arr rows i len =
  match i < len
    true ->
      row = rows[i]
      item = jb_obj(rp)
      jb_put(rp, item, "name", jb_str(rp, row[0]))
      jb_put(rp, item, "kind", jb_str(rp, row[1]))
      jb_put(rp, item, "gen", jb_int(rp, str_to_int(row[2])))
      jb_push(rp, arr, item)
      mcp_fmt_search_loop(rp, arr, rows, i + 1, len)
    _ -> 0

-- fn mcp_fmt_sql_cols
mcp_fmt_sql_cols rp row_arr row j ncols =
  match j < ncols
    true ->
      jb_push(rp, row_arr, jb_str(rp, row[j]))
      mcp_fmt_sql_cols(rp, row_arr, row, j + 1, ncols)
    _ -> 0

-- fn mcp_fmt_sql_loop
mcp_fmt_sql_loop rp arr rows i len =
  match i < len
    true ->
      row = rows[i]
      row_arr = jb_arr(rp)
      mcp_fmt_sql_cols(rp, row_arr, row, 0, array_len(row))
      jb_push(rp, arr, row_arr)
      mcp_fmt_sql_loop(rp, arr, rows, i + 1, len)
    _ -> 0

-- fn mcp_format_def_list
mcp_format_def_list id rows =
  rp = []
  arr = jb_arr(rp)
  mcp_fmt_list_loop(rp, arr, rows, 0, array_len(rows))
  mcp_text_result(id, json_gen(rp, arr))

-- fn mcp_get_db
mcp_get_db pool args default_db =
  idx = json_get(pool, args, "db")
  match idx >= 0
    true ->
      node = pool[idx]
      _ = node.tag
      node.sval
    _ -> default_db


-- fn mcp_initialize
mcp_initialize id =
  pool = []
  result = jb_obj(pool)
  jb_put(pool, result, "protocolVersion", jb_str(pool, "2024-11-05"))
  caps = jb_obj(pool)
  jb_put(pool, caps, "tools", jb_obj(pool))
  jb_put(pool, result, "capabilities", caps)
  info = jb_obj(pool)
  jb_put(pool, info, "name", jb_str(pool, "glyph-mcp"))
  jb_put(pool, info, "version", jb_str(pool, "0.1.0"))
  jb_put(pool, result, "serverInfo", info)
  mcp_respond(id, json_gen(pool, result))

-- fn mcp_int_prop
mcp_int_prop pool desc =
  p = jb_obj(pool)
  jb_put(pool, p, "type", jb_str(pool, "integer"))
  jb_put(pool, p, "description", jb_str(pool, desc))
  p

-- fn mcp_loop
mcp_loop db_path =
  line = read_line(0)
  match str_len(line) == 0
    true -> 0
    _ ->
      response = mcp_dispatch(db_path, line)
      match str_len(response) > 0
        true ->
          print(response)
          print("\n")
          flush(0)
        _ -> 0
      mcp_loop(db_path)

-- fn mcp_put_error
mcp_put_error id pool body vr =
  rp = []
  r = jb_obj(rp)
  jb_put(rp, r, "status", jb_str(rp, "error"))
  msg = match glyph_str_len(vr.vr_msg) > 0
    true -> vr.vr_msg
    _ -> "parse error"
  jb_put(rp, r, "message", jb_str(rp, msg))
  detail = format_parse_err(body, vr.vr_tokens, vr.vr_pos, msg)
  jb_put(rp, r, "detail", jb_str(rp, detail))
  mcp_text_result(id, json_gen(rp, r))

-- fn mcp_put_insert
mcp_put_insert name kind body gen =
  sb = sb_new()
  sb_append(sb, "INSERT INTO def (name, kind, body, gen, hash, tokens) VALUES ('")
  sb_append(sb, sql_escape(name))
  sb_append(sb, "', '")
  sb_append(sb, sql_escape(kind))
  sb_append(sb, "', '")
  sb_append(sb, sql_escape(body))
  sb_append(sb, "', ")
  sb_append(sb, int_to_str(gen))
  sb_append(sb, ", zeroblob(32), 0)")
  sb_build(sb)

-- fn mcp_respond
mcp_respond id result_json =
  sb = sb_new()
  sb_append(sb, "\{\"jsonrpc\":\"2.0\",\"id\":")
  sb_append(sb, int_to_str(id))
  sb_append(sb, ",\"result\":")
  sb_append(sb, result_json)
  sb_append(sb, "}")
  sb_build(sb)

-- fn mcp_str_prop
mcp_str_prop pool desc =
  p = jb_obj(pool)
  jb_put(pool, p, "type", jb_str(pool, "string"))
  jb_put(pool, p, "description", jb_str(pool, desc))
  p

-- fn mcp_text_result
mcp_text_result id text =
  pool = []
  content = jb_arr(pool)
  item = jb_obj(pool)
  jb_put(pool, item, "type", jb_str(pool, "text"))
  jb_put(pool, item, "text", jb_str(pool, text))
  jb_push(pool, content, item)
  result = jb_obj(pool)
  jb_put(pool, result, "content", content)
  mcp_respond(id, json_gen(pool, result))

-- fn mcp_tool_check_def
mcp_tool_check_def db_path id pool args =
  body = json_get_str(pool, args, "body")
  vr = validate_def(body)
  rp = []
  r = jb_obj(rp)
  match vr.vr_ok > 0
    true ->
      jb_put(rp, r, "valid", jb_bool(rp, 1))
      jb_put(rp, r, "name", jb_str(rp, extract_name(body)))
      mcp_text_result(id, json_gen(rp, r))
    _ ->
      jb_put(rp, r, "valid", jb_bool(rp, 0))
      msg = match glyph_str_len(vr.vr_msg) > 0
        true -> vr.vr_msg
        _ -> "parse error"
      jb_put(rp, r, "message", jb_str(rp, msg))
      detail = format_parse_err(body, vr.vr_tokens, vr.vr_pos, msg)
      jb_put(rp, r, "detail", jb_str(rp, detail))
      mcp_text_result(id, json_gen(rp, r))

-- fn mcp_tool_coverage
mcp_tool_coverage db_path id pool args_idx =
  cover_path = s2(db_path, ".cover")
  content = glyph_read_file(cover_path)
  match glyph_str_len(content) == 0
    true -> mcp_error(id, 0 - 32602, "No coverage data found. Run: glyph test <db> --cover")
    _ ->
      names = []
      hits = []
      parse_cover_lines(content, 0, names, hits)
      total = glyph_array_len(names)
      covered = count_covered(hits, 0, 0)
      pct = match total > 0
        true -> (covered * 1000) / total
        _ -> 0
      pct_whole = pct / 10
      pct_frac = pct - (pct_whole * 10)
      result = jb_obj(pool)
      jb_put(pool, result, "total", jb_int(pool, total))
      jb_put(pool, result, "covered", jb_int(pool, covered))
      jb_put(pool, result, "percent", jb_str(pool, s3(itos(pct_whole), ".", itos(pct_frac))))
      uncov_arr = jb_arr(pool)
      mcp_cov_add_uncovered(pool, uncov_arr, names, hits, 0)
      jb_put(pool, result, "uncovered", uncov_arr)
      mcp_text_result(id, json_gen(pool, result))


-- fn mcp_tool_deps
mcp_tool_deps db_path id pool args =
  name = json_get_str(pool, args, "name")
  db = glyph_db_open(db_path)
  sql = s3("SELECT DISTINCT t.name, t.kind FROM dep d JOIN def f ON d.from_id = f.id JOIN def t ON d.to_id = t.id WHERE f.name = '", sql_escape(name), "' ORDER BY t.name")
  rows = glyph_db_query_rows(db, sql)
  glyph_db_close(db)
  rp = []
  arr = jb_arr(rp)
  mcp_fmt_dep_loop(rp, arr, rows, 0, glyph_array_len(rows))
  mcp_text_result(id, json_gen(rp, arr))

-- fn mcp_tool_get_def
mcp_tool_get_def db_path id pool args =
  name = json_get_str(pool, args, "name")
  kind_idx = json_get(pool, args, "kind")
  kind = match kind_idx >= 0
    true ->
      knode = pool[kind_idx]
      _ = knode.tag
      knode.sval
    _ -> "fn"
  db = glyph_db_open(db_path)
  sql = s5("SELECT name, kind, body, gen, tokens FROM def WHERE name='", name, "' AND kind='", kind, "'")
  rows = glyph_db_query_rows(db, sql)
  glyph_db_close(db)
  match glyph_array_len(rows) > 0
    true ->
      row = rows[0]
      rp = []
      r = jb_obj(rp)
      jb_put(rp, r, "name", jb_str(rp, row[0]))
      jb_put(rp, r, "kind", jb_str(rp, row[1]))
      jb_put(rp, r, "body", jb_str(rp, row[2]))
      jb_put(rp, r, "gen", jb_int(rp, str_to_int(row[3])))
      jb_put(rp, r, "tokens", jb_int(rp, str_to_int(row[4])))
      mcp_text_result(id, json_gen(rp, r))
    _ -> mcp_text_result(id, s3("Definition '", name, "' not found"))


-- fn mcp_tool_list_defs
mcp_tool_list_defs db_path id pool args =
  kind_idx = json_get(pool, args, "kind")
  pat_idx = json_get(pool, args, "pattern")
  gen_idx = json_get(pool, args, "gen")
  sb = sb_new()
  sb_append(sb, "SELECT name, kind, gen, tokens FROM def WHERE 1=1")
  match kind_idx >= 0
    true ->
      knode = pool[kind_idx]
      _ = knode.tag
      sb_append(sb, s3(" AND kind='", knode.sval, "'"))
    _ -> 0
  match pat_idx >= 0
    true ->
      pnode = pool[pat_idx]
      _ = pnode.tag
      sb_append(sb, s3(" AND name LIKE '", pnode.sval, "'"))
    _ -> 0
  match gen_idx >= 0
    true -> sb_append(sb, s2(" AND gen=", int_to_str(pool[gen_idx].nval)))
    _ -> 0
  sb_append(sb, " ORDER BY name LIMIT 100")
  sql = sb_build(sb)
  db = glyph_db_open(db_path)
  rows = glyph_db_query_rows(db, sql)
  glyph_db_close(db)
  mcp_format_def_list(id, rows)


-- fn mcp_tool_put_def
mcp_tool_put_def db_path id pool args =
  name = json_get_str(pool, args, "name")
  kind = json_get_str(pool, args, "kind")
  body = json_get_str(pool, args, "body")
  gen_idx = json_get(pool, args, "gen")
  gen = match gen_idx >= 0
    true -> pool[gen_idx].nval
    _ -> 1
  match str_eq(kind, "fn")
    true ->
      vr = validate_def(body)
      match vr.vr_ok == 0
        true -> mcp_put_error(id, pool, body, vr)
        _ -> mcp_do_put(db_path, id, pool, name, kind, body, gen)
    _ -> mcp_do_put(db_path, id, pool, name, kind, body, gen)

-- fn mcp_tool_rdeps
mcp_tool_rdeps db_path id pool args =
  name = json_get_str(pool, args, "name")
  db = glyph_db_open(db_path)
  sql = s3("SELECT DISTINCT f.name, f.kind FROM dep d JOIN def f ON d.from_id = f.id JOIN def t ON d.to_id = t.id WHERE t.name = '", sql_escape(name), "' ORDER BY f.name")
  rows = glyph_db_query_rows(db, sql)
  glyph_db_close(db)
  rp = []
  arr = jb_arr(rp)
  mcp_fmt_dep_loop(rp, arr, rows, 0, glyph_array_len(rows))
  mcp_text_result(id, json_gen(rp, arr))

-- fn mcp_tool_remove_def
mcp_tool_remove_def db_path id pool args =
  name = json_get_str(pool, args, "name")
  kind_idx = json_get(pool, args, "kind")
  kind = match kind_idx >= 0
    true ->
      knode = pool[kind_idx]
      _ = knode.tag
      knode.sval
    _ -> "fn"
  db = glyph_db_open(db_path)
  sql = s5("DELETE FROM def WHERE name='", sql_escape(name), "' AND kind='", kind, "'")
  glyph_db_exec(db, sql)
  glyph_db_close(db)
  rp = []
  r = jb_obj(rp)
  jb_put(rp, r, "status", jb_str(rp, "ok"))
  mcp_text_result(id, json_gen(rp, r))


-- fn mcp_tool_schema
mcp_tool_schema pool name desc props required =
  tool = jb_obj(pool)
  jb_put(pool, tool, "name", jb_str(pool, name))
  jb_put(pool, tool, "description", jb_str(pool, desc))
  schema = jb_obj(pool)
  jb_put(pool, schema, "type", jb_str(pool, "object"))
  jb_put(pool, schema, "properties", props)
  req_node = pool[required]
  match array_len(req_node.items) > 0
    true -> jb_put(pool, schema, "required", required)
    _ -> 0
  jb_put(pool, tool, "inputSchema", schema)
  tool

-- fn mcp_tool_search_defs
mcp_tool_search_defs db_path id pool args =
  pattern = json_get_str(pool, args, "pattern")
  kind_idx = json_get(pool, args, "kind")
  sb = sb_new()
  sb_append(sb, "SELECT name, kind, gen FROM def WHERE body LIKE '%")
  sb_append(sb, sql_escape(pattern))
  sb_append(sb, "%'")
  match kind_idx >= 0
    true ->
      knode = pool[kind_idx]
      _ = knode.tag
      sb_append(sb, s3(" AND kind='", knode.sval, "'"))
    _ -> 0
  sb_append(sb, " ORDER BY name LIMIT 50")
  sql = sb_build(sb)
  db = glyph_db_open(db_path)
  rows = glyph_db_query_rows(db, sql)
  glyph_db_close(db)
  rp = []
  arr = jb_arr(rp)
  mcp_fmt_search_loop(rp, arr, rows, 0, glyph_array_len(rows))
  mcp_text_result(id, json_gen(rp, arr))


-- fn mcp_tool_sql
mcp_tool_sql db_path id pool args =
  query = json_get_str(pool, args, "query")
  db = glyph_db_open(db_path)
  rows = glyph_db_query_rows(db, query)
  glyph_db_close(db)
  rp = []
  arr = jb_arr(rp)
  mcp_fmt_sql_loop(rp, arr, rows, 0, glyph_array_len(rows))
  mcp_text_result(id, json_gen(rp, arr))

-- fn mcp_tools_call
mcp_tools_call db_path id pool root =
  params = json_get(pool, root, "params")
  tool_name = json_get_str(pool, params, "name")
  args_idx = json_get(pool, params, "arguments")
  db = mcp_get_db(pool, args_idx, db_path)
  match str_eq(tool_name, "get_def")
    true -> mcp_tool_get_def(db, id, pool, args_idx)
    _ -> match str_eq(tool_name, "put_def")
      true -> mcp_tool_put_def(db, id, pool, args_idx)
      _ -> match str_eq(tool_name, "list_defs")
        true -> mcp_tool_list_defs(db, id, pool, args_idx)
        _ -> mcp_tools_call2(db, id, pool, args_idx, tool_name)

-- fn mcp_tools_call2
mcp_tools_call2 db_path id pool args_idx tool_name =
  match str_eq(tool_name, "search_defs")
    true -> mcp_tool_search_defs(db_path, id, pool, args_idx)
    _ -> match str_eq(tool_name, "remove_def")
      true -> mcp_tool_remove_def(db_path, id, pool, args_idx)
      _ -> match str_eq(tool_name, "deps")
        true -> mcp_tool_deps(db_path, id, pool, args_idx)
        _ -> match str_eq(tool_name, "rdeps")
          true -> mcp_tool_rdeps(db_path, id, pool, args_idx)
          _ -> match str_eq(tool_name, "sql")
            true -> mcp_tool_sql(db_path, id, pool, args_idx)
            _ -> match str_eq(tool_name, "check_def")
              true -> mcp_tool_check_def(db_path, id, pool, args_idx)
              _ -> match str_eq(tool_name, "coverage")
                true -> mcp_tool_coverage(db_path, id, pool, args_idx)
                _ -> mcp_error(id, 0 - 32602, s2("Unknown tool: ", tool_name))


-- fn mcp_tools_list
mcp_tools_list id =
  pool = []
  tools = jb_arr(pool)
  mcp_add_tools(pool, tools)
  result = jb_obj(pool)
  jb_put(pool, result, "tools", tools)
  mcp_respond(id, json_gen(pool, result))

-- fn measure_indent
measure_indent src pos =
  measure_indent_loop(src, pos, 0, glyph_str_len(src))

-- fn measure_indent_loop
measure_indent_loop src pos count len =
  match pos < len
    true ->
      match is_space(glyph_str_char_at(src, pos))
        true ->
          inc = match glyph_str_char_at(src, pos)
            9 -> 4
            _ -> 1
          measure_indent_loop(src, pos + 1, count + inc, len)
        _ -> count * 1000000 + pos
    _ -> count * 1000000 + pos

-- fn migrate_history
migrate_history db =
  rows = glyph_db_query_rows(db, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='def_history'")
  cnt = rows[0]
  match glyph_str_eq(cnt[0], "0") == 1
    true -> glyph_db_exec(db, history_migration_sql)
    _ -> 0

-- fn mir_add
mir_add = 1

-- fn mir_alloc_local
mir_alloc_local ctx name =
  id = ctx.nxt_local[0]
  glyph_array_push(ctx.local_names, name)
  glyph_array_push(ctx.local_types, 0)
  glyph_array_set(ctx.nxt_local, 0, id + 1)
  id


-- fn mir_and
mir_and = 12

-- fn mir_bind_var
mir_bind_var ctx name local_id =
  glyph_array_push(ctx.var_names, name)
  glyph_array_push(ctx.var_locals, local_id)
  0

-- fn mir_bitand
mir_bitand = 14

-- fn mir_bitor
mir_bitor = 15

-- fn mir_bitxor
mir_bitxor = 16

-- fn mir_block_count
mir_block_count mir_fn =
  glyph_array_len(mir_fn.fn_blocks_stmts)

-- fn mir_div
mir_div = 4

-- fn mir_emit
mir_emit ctx stmt =
  bi = ctx.cur_block[0]
  stmts = ctx.block_stmts[bi]
  glyph_array_push(stmts, stmt)
  0

-- fn mir_emit_aggregate
mir_emit_aggregate ctx dest ag_kind field_names ops =
  mir_emit(ctx, mk_stmt(dest, rv_aggregate(), ag_kind, field_names, mk_op_nil(), mk_op_nil(), ops))

-- fn mir_emit_binop
mir_emit_binop ctx dest op left right =
  mir_emit(ctx, mk_stmt(dest, rv_binop(), op, "", left, right, []))

-- fn mir_emit_call
mir_emit_call ctx dest callee args =
  mir_emit(ctx, mk_stmt(dest, rv_call(), 0, "", callee, mk_op_nil(), args))

-- fn mir_emit_field
mir_emit_field ctx dest base field_idx field_name =
  mir_emit(ctx, mk_stmt(dest, rv_field(), field_idx, field_name, base, mk_op_nil(), []))

-- fn mir_emit_index
mir_emit_index ctx dest arr idx =
  mir_emit(ctx, mk_stmt(dest, rv_index(), 0, "", arr, idx, []))

-- fn mir_emit_unop
mir_emit_unop ctx dest op operand =
  mir_emit(ctx, mk_stmt(dest, rv_unop(), op, "", operand, mk_op_nil(), []))

-- fn mir_emit_use
mir_emit_use ctx dest op =
  mir_emit(ctx, mk_stmt(dest, rv_use(), 0, "", op, mk_op_nil(), []))

-- fn mir_eq
mir_eq = 6

-- fn mir_gt
mir_gt = 9

-- fn mir_gt_eq
mir_gt_eq = 11

-- fn mir_local_count
mir_local_count mir_fn =
  glyph_array_len(mir_fn.fn_locals)

-- fn mir_lookup_var
mir_lookup_var ctx name =
  mir_lookup_var_at(ctx.var_names, ctx.var_locals, name, glyph_array_len(ctx.var_names) - 1)

-- fn mir_lookup_var_at
mir_lookup_var_at names locals name i =
  match i < 0
    true -> 0 - 1
    _ ->
      match glyph_str_eq(names[i], name) == 1
        true -> locals[i]
        _ -> mir_lookup_var_at(names, locals, name, i - 1)

-- fn mir_lt
mir_lt = 8

-- fn mir_lt_eq
mir_lt_eq = 10

-- fn mir_mod
mir_mod = 5

-- fn mir_mul
mir_mul = 3

-- fn mir_neg
mir_neg = 20

-- fn mir_neq
mir_neq = 7

-- fn mir_new_block
mir_new_block ctx =
  id = ctx.nxt_block[0]
  glyph_array_push(ctx.block_stmts, [])
  glyph_array_push(ctx.block_terms, mk_term_unreachable())
  glyph_array_set(ctx.nxt_block, 0, id + 1)
  id

-- fn mir_not
mir_not = 21

-- fn mir_or
mir_or = 13

-- fn mir_pop_scope
mir_pop_scope ctx =
  target = glyph_array_pop(ctx.var_marks)
  mir_pop_scope_to(ctx.var_names, ctx.var_locals, target)

-- fn mir_pop_scope_to
mir_pop_scope_to names locals target =
  match glyph_array_len(names) > target
    true ->
      glyph_array_pop(names)
      glyph_array_pop(locals)
      mir_pop_scope_to(names, locals, target)
    _ -> 0

-- fn mir_pp_binop_name
mir_pp_binop_name n =
  names = ["?", "add", "sub", "mul", "div", "mod", "eq", "neq", "lt", "gt", "lte", "gte", "and", "or"]
  match n >= 0
    true -> match n < glyph_array_len(names)
      true -> names[n]
      _ -> match n == 20
        true -> "neg"
        _ -> match n == 21
          true -> "not"
          _ -> s2("op", itos(n))
    _ -> s2("op", itos(n))


-- fn mir_pp_block
mir_pp_block mir bi =
  stmts = mir_pp_stmts(mir.fn_blocks_stmts[bi], 0)
  term = mir_pp_term(mir.fn_blocks_terms[bi])
  s4("  bb", itos(bi), ":\n", s2(stmts, term))


-- fn mir_pp_blocks
mir_pp_blocks mir i =
  n = glyph_array_len(mir.fn_blocks_terms)
  match i >= n
    true -> ""
    _ -> s2(mir_pp_block(mir, i), mir_pp_blocks(mir, i + 1))


-- fn mir_pp_fn
mir_pp_fn mir =
  params = mir_pp_params(mir.fn_locals, mir.fn_params, 0)
  hdr = s5("fn ", mir.fn_name, "(", params, ") \{\n")
  body = mir_pp_blocks(mir, 0)
  s3(hdr, body, "}\n")


-- fn mir_pp_fns
mir_pp_fns mirs i =
  n = glyph_array_len(mirs)
  match i >= n
    true -> 0
    _ ->
      eprintln(mir_pp_fn(mirs[i]))
      mir_pp_fns(mirs, i + 1)


-- fn mir_pp_op
mir_pp_op op =
  k = op.okind
  match k
    _ ? k == 1 -> s2("_", itos(op.oval))
    _ ? k == 2 -> itos(op.oval)
    _ ? k == 3 -> match op.oval == 1
      true -> "true"
      _ -> "false"
    _ ? k == 4 -> s3("\"", op.ostr, "\"")
    _ ? k == 5 -> "()"
    _ ? k == 6 -> s2("fn:", op.ostr)
    _ ? k == 7 -> s2("ext:", op.ostr)
    _ -> s2("?op:", itos(k))

-- fn mir_pp_ops
mir_pp_ops ops i =
  n = glyph_array_len(ops)
  match i >= n
    true -> ""
    _ ->
      cur = mir_pp_op(ops[i])
      match i + 1 >= n
        true -> cur
        _ -> s3(cur, ", ", mir_pp_ops(ops, i + 1))


-- fn mir_pp_params
mir_pp_params locals params i =
  n = glyph_array_len(params)
  match i >= n
    true -> ""
    _ ->
      p = params[i]
      name = match p < glyph_array_len(locals)
        true -> locals[p]
        _ -> ""
      cur = match glyph_str_len(name) > 0
        true -> s4("_", itos(p), ":", name)
        _ -> s2("_", itos(p))
      match i + 1 >= n
        true -> cur
        _ -> s3(cur, ", ", mir_pp_params(locals, params, i + 1))


-- fn mir_pp_stmt
mir_pp_stmt stmt =
  d = s3("    _", itos(stmt.sdest), " = ")
  k = stmt.skind
  match k
    _ ? k == 1 -> s3(d, "use(", s2(mir_pp_op(stmt.sop1), ")"))
    _ ? k == 2 -> s6(d, mir_pp_binop_name(stmt.sival), "(", mir_pp_op(stmt.sop1), ", ", s2(mir_pp_op(stmt.sop2), ")"))
    _ ? k == 3 -> s4(d, mir_pp_binop_name(stmt.sival), "(", s2(mir_pp_op(stmt.sop1), ")"))
    _ ? k == 4 -> s4(d, "call ", mir_pp_op(stmt.sop1), s3("(", mir_pp_ops(stmt.sops, 0), ")"))
    _ -> mir_pp_stmt2(stmt, d)

-- fn mir_pp_stmt2
mir_pp_stmt2 stmt d =
  k = stmt.skind
  match k
    _ ? k == 5 ->
      ak = stmt.sival
      match ak
        _ ? ak == 3 -> s5(d, "record\{", stmt.sstr, "}(", s2(mir_pp_ops(stmt.sops, 0), ")"))
        _ ? ak == 2 -> s4(d, "array[", mir_pp_ops(stmt.sops, 0), "]")
        _ ? ak == 4 -> s5(d, "variant:", stmt.sstr, "(", s2(mir_pp_ops(stmt.sops, 0), ")"))
        _ -> s4(d, "tuple(", mir_pp_ops(stmt.sops, 0), ")")
    _ ? k == 6 ->
      match glyph_str_len(stmt.sstr) > 0
        true -> s3(d, mir_pp_op(stmt.sop1), s2(".", stmt.sstr))
        _ -> s4(d, mir_pp_op(stmt.sop1), "[", s2(itos(stmt.sival), "]"))
    _ ? k == 7 -> s4(d, mir_pp_op(stmt.sop1), "[", s2(mir_pp_op(stmt.sop2), "]"))
    _ ? k == 8 -> s4(d, "interp(", mir_pp_ops(stmt.sops, 0), ")")
    _ ? k == 9 -> s4(d, "closure(", mir_pp_ops(stmt.sops, 0), ")")
    _ -> s3(d, "?rv:", itos(k))

-- fn mir_pp_stmts
mir_pp_stmts stmts i =
  n = glyph_array_len(stmts)
  match i >= n
    true -> ""
    _ -> s3(mir_pp_stmt(stmts[i]), "\n", mir_pp_stmts(stmts, i + 1))


-- fn mir_pp_term
mir_pp_term term =
  k = term.tkind
  match k
    _ ? k == 1 -> s3("    goto bb", itos(term.tgt1), "\n")
    _ ? k == 2 -> s6("    branch ", mir_pp_op(term.top), " bb", itos(term.tgt1), " bb", s2(itos(term.tgt2), "\n"))
    _ ? k == 4 -> s3("    return ", mir_pp_op(term.top), "\n")
    _ ? k == 5 -> "    unreachable\n"
    _ -> s3("    ?term:", itos(k), "\n")

-- fn mir_push_scope
mir_push_scope ctx =
  glyph_array_push(ctx.var_marks, glyph_array_len(ctx.var_names))
  0

-- fn mir_set_lt
mir_set_lt ctx local ty =
  glyph_array_set(ctx.local_types, local, ty)
  0


-- fn mir_shl
mir_shl = 17

-- fn mir_shr
mir_shr = 18

-- fn mir_stmt_count
mir_stmt_count mir_fn block_id =
  stmts = mir_fn.fn_blocks_stmts[block_id]
  glyph_array_len(stmts)

-- fn mir_sub
mir_sub = 2

-- fn mir_switch_block
mir_switch_block ctx block_id =
  glyph_array_set(ctx.cur_block, 0, block_id)
  0

-- fn mir_term_kind
mir_term_kind mir_fn block_id =
  term = mir_fn.fn_blocks_terms[block_id]
  term.tkind

-- fn mir_terminate
mir_terminate ctx term =
  bi = ctx.cur_block[0]
  glyph_array_set(ctx.block_terms, bi, term)
  0

-- fn mk_engine
mk_engine =
  {ty_pool: [], parent: [], bindings: [], next_var: [0],
   env_names: [], env_types: [], env_marks: [], errors: [],
   tmap: [[]]}

-- fn mk_err
mk_err pos = {node: 0 - 1, pos: pos, pr_err: ""}

-- fn mk_err_msg
mk_err_msg pos msg = {node: 0 - 1, pos: pos, pr_err: msg}

-- fn mk_jnode
mk_jnode tag nval sval items keys =
  {items: items, keys: keys, nval: nval, sval: sval, tag: tag}

-- fn mk_mir_lower
mk_mir_lower fn_name za_fns tctx =
  {block_stmts: [],
   block_terms: [],
   local_names: [],
   local_types: [],
   cur_block: [0],
   nxt_local: [0],
   nxt_block: [0],
   var_names: [],
   var_locals: [],
   var_marks: [],
   fn_name: fn_name,
   fn_params: [],
   fn_entry: [0],
   za_fns: za_fns,
   tctx: tctx,
   lifted_fns: [],
   lambda_ctr: [0],
   outer_fn_name: fn_name}


-- fn mk_node
mk_node kind ival sval n1 n2 n3 ns = {kind: kind, ival: ival, sval: sval, n1: n1, n2: n2, n3: n3, ns: ns}

-- fn mk_null_tctx
mk_null_tctx u = {tc_tmap: []}

-- fn mk_op
mk_op k v s = {okind: k, oval: v, ostr: s}

-- fn mk_op_bool
mk_op_bool b = mk_op(ok_const_bool(), b, "")

-- fn mk_op_float
mk_op_float s = mk_op(ok_const_float(), 0, s)

-- fn mk_op_func
mk_op_func name = mk_op(ok_func_ref(), 0, name)

-- fn mk_op_int
mk_op_int n = mk_op(ok_const_int(), n, "")

-- fn mk_op_local
mk_op_local id = mk_op(ok_local(), id, "")

-- fn mk_op_nil
mk_op_nil = mk_op(0, 0, "")

-- fn mk_op_str
mk_op_str s = mk_op(ok_const_str(), 0, s)

-- fn mk_op_unit
mk_op_unit = mk_op(ok_const_unit(), 0, "")

-- fn mk_result
mk_result node pos = {node: node, pos: pos, pr_err: ""}

-- fn mk_stmt
mk_stmt dest kind ival sstr op1 op2 ops =
  {sdest: dest, skind: kind, sival: ival, sstr: sstr, sop1: op1, sop2: op2, sops: ops}

-- fn mk_tarray
mk_tarray elem pool =
  mk_ty(ty_array(), elem, 0 - 1, pool)

-- fn mk_tbool
mk_tbool pool = mk_ty_prim(ty_bool(), pool)

-- fn mk_tctx
mk_tctx tmap = {tc_tmap: tmap}

-- fn mk_term
mk_term kind op tgt1 tgt2 = {tkind: kind, top: op, tgt1: tgt1, tgt2: tgt2}

-- fn mk_term_branch
mk_term_branch cond then_b else_b = mk_term(tm_branch(), cond, then_b, else_b)

-- fn mk_term_goto
mk_term_goto target = mk_term(tm_goto(), mk_op_nil(), target, 0 - 1)

-- fn mk_term_return
mk_term_return op = mk_term(tm_return(), op, 0 - 1, 0 - 1)

-- fn mk_term_unreachable
mk_term_unreachable = mk_term(tm_unreachable(), mk_op_nil(), 0 - 1, 0 - 1)

-- fn mk_terror
mk_terror pool = mk_ty_prim(ty_error(), pool)

-- fn mk_tfield
mk_tfield name type_idx pool =
  ty_push(pool, {tag: ty_field(), n1: type_idx, n2: 0 - 1, ns: [], sval: name})

-- fn mk_tfn
mk_tfn param ret pool =
  mk_ty(ty_fn(), param, ret, pool)

-- fn mk_tforall
mk_tforall body_ty bound_vars pool =
  ty_push(pool, {tag: ty_forall(), n1: body_ty, n2: 0 - 1, ns: bound_vars, sval: ""})

-- fn mk_tint
mk_tint pool = mk_ty_prim(ty_int(), pool)

-- fn mk_tnamed
mk_tnamed name type_args pool =
  ty_push(pool, {tag: ty_named(), n1: 0 - 1, n2: 0 - 1, ns: type_args, sval: name})

-- fn mk_tnever
mk_tnever pool = mk_ty_prim(ty_never(), pool)

-- fn mk_token
mk_token kind start end line = {kind: kind, start: start, end: end, line: line}

-- fn mk_topt
mk_topt inner pool =
  mk_ty(ty_opt(), inner, 0 - 1, pool)

-- fn mk_trecord
mk_trecord field_ns rest pool =
  ty_push(pool, {tag: ty_record(), n1: rest, n2: 0 - 1, ns: field_ns, sval: ""})

-- fn mk_tstr
mk_tstr pool = mk_ty_prim(ty_str(), pool)

-- fn mk_ttuple
mk_ttuple elem_types pool =
  ty_push(pool, {tag: ty_tuple(), n1: 0 - 1, n2: 0 - 1, ns: elem_types, sval: ""})

-- fn mk_tvar
mk_tvar var_id pool =
  mk_ty(ty_var(), var_id, 0 - 1, pool)

-- fn mk_tvoid
mk_tvoid pool = mk_ty_prim(ty_void(), pool)

-- fn mk_ty
mk_ty tag n1 n2 pool =
  ty_push(pool, {tag: tag, n1: n1, n2: n2, ns: [], sval: ""})

-- fn mk_ty_prim
mk_ty_prim tag pool =
  mk_ty(tag, 0 - 1, 0 - 1, pool)

-- fn nd_param
nd_param = 400

-- fn needs_sqlite
needs_sqlite externs =
  needs_sqlite_loop(externs, 0)

-- fn needs_sqlite_loop
needs_sqlite_loop externs i =
  match i >= glyph_array_len(externs)
    true -> 0
    _ ->
      row = externs[i]
      match glyph_str_eq(row[3], "sqlite3")
        true -> 1
        _ -> needs_sqlite_loop(externs, i + 1)

-- fn ok_const_bool
ok_const_bool = 3

-- fn ok_const_float
ok_const_float = 7

-- fn ok_const_int
ok_const_int = 2

-- fn ok_const_str
ok_const_str = 4

-- fn ok_const_unit
ok_const_unit = 5

-- fn ok_extern_ref
ok_extern_ref = 7

-- fn ok_func_ref
ok_func_ref = 6

-- fn ok_local
ok_local = 1

-- fn op_add
op_add = 1

-- fn op_and
op_and = 12

-- fn op_bitand
op_bitand = 14

-- fn op_bitor
op_bitor = 15

-- fn op_bitxor
op_bitxor = 16

-- fn op_div
op_div = 4

-- fn op_eq
op_eq = 6

-- fn op_gt
op_gt = 9

-- fn op_gt_eq
op_gt_eq = 11

-- fn op_lt
op_lt = 8

-- fn op_lt_eq
op_lt_eq = 10

-- fn op_mod
op_mod = 5

-- fn op_mul
op_mul = 3

-- fn op_neg
op_neg = 20

-- fn op_neq
op_neq = 7

-- fn op_not
op_not = 21

-- fn op_or
op_or = 13

-- fn op_shl
op_shl = 17

-- fn op_shr
op_shr = 18

-- fn op_sub
op_sub = 2

-- fn op_type
op_type ctx op =
  k = op.okind
  match k
    _ ? k == ok_const_float() -> 3
    _ ? k == ok_const_str() -> 4
    _ ? k == ok_const_int() -> 1
    _ ? k == ok_const_bool() -> 5
    _ ? k == ok_local() -> ctx.local_types[op.oval]
    _ -> 0

-- fn parse_add
parse_add src tokens pos pool =
  left = parse_mul(src, tokens, pos, pool)
  match is_err(left)
    true -> left
    _ -> parse_add_loop(src, tokens, left.pos, pool, left.node)

-- fn parse_add_loop
parse_add_loop src tokens pos pool left =
  k = cur_kind(tokens, pos)
  op = match k
    _ ? k == tk_plus() -> op_add()
    _ ? k == tk_minus() -> op_sub()
    _ -> 0 - 1
  match op < 0
    true -> mk_result(left, pos)
    _ ->
      right = parse_mul(src, tokens, pos + 1, pool)
      match is_err(right)
        true -> right
        _ ->
          idx = pool_push(pool, mk_node(ex_binary(), op, "", left, right.node, 0 - 1, []))
          parse_add_loop(src, tokens, right.pos, pool, idx)

-- fn parse_all_fns
parse_all_fns sources i =
  match i >= glyph_array_len(sources)
    true -> []
    _ ->
      src = sources[i]
      tokens = tokenize(src)
      ast = []
      r = parse_fn_def(src, tokens, 0, ast)
      fn_idx = r.node
      name = match fn_idx >= 0
        true -> (ast[fn_idx]).sval
        _ -> ""
      entry = {pf_src: src, pf_ast: ast, pf_fn_idx: fn_idx, pf_name: name, pf_tokens: tokens, pf_err_pos: r.pos, pf_err_msg: r.pr_err}
      rest = parse_all_fns(sources, i + 1)
      glyph_array_push(rest, entry)
      rest

-- fn parse_args
parse_args src tokens pos pool =
  match cur_kind(tokens, pos) == tk_rparen()
    true ->
      idx = pool_push(pool, mk_node(0, 0, "", 0 - 1, 0 - 1, 0 - 1, []))
      mk_result(idx, pos)
    _ ->
      args = []
      r = parse_expr(src, tokens, pos, pool)
      match is_err(r)
        true -> r
        _ ->
          glyph_array_push(args, r.node)
          parse_args_loop(src, tokens, r.pos, pool, args)

-- fn parse_args_loop
parse_args_loop src tokens pos pool args =
  match cur_kind(tokens, pos) == tk_comma()
    true ->
      pos2 = pos + 1
      match cur_kind(tokens, pos2) == tk_rparen()
        true ->
          idx = pool_push(pool, mk_node(0, 0, "", 0 - 1, 0 - 1, 0 - 1, args))
          mk_result(idx, pos2)
        _ ->
          r = parse_expr(src, tokens, pos2, pool)
          match is_err(r)
            true -> r
            _ ->
              glyph_array_push(args, r.node)
              parse_args_loop(src, tokens, r.pos, pool, args)
    _ ->
      idx = pool_push(pool, mk_node(0, 0, "", 0 - 1, 0 - 1, 0 - 1, args))
      mk_result(idx, pos)

-- fn parse_array
parse_array src tokens pos pool =
  match cur_kind(tokens, pos) == tk_rbracket()
    true ->
      idx = pool_push(pool, mk_node(ex_array(), 0, "", 0 - 1, 0 - 1, 0 - 1, []))
      mk_result(idx, pos + 1)
    _ ->
      elems = []
      r = parse_expr(src, tokens, pos, pool)
      match is_err(r)
        true -> r
        _ ->
          glyph_array_push(elems, r.node)
          parse_array_loop(src, tokens, r.pos, pool, elems)

-- fn parse_array_loop
parse_array_loop src tokens pos pool elems =
  match cur_kind(tokens, pos) == tk_comma()
    true ->
      pos2 = pos + 1
      match cur_kind(tokens, pos2) == tk_rbracket()
        true ->
          idx = pool_push(pool, mk_node(ex_array(), 0, "", 0 - 1, 0 - 1, 0 - 1, elems))
          mk_result(idx, pos2 + 1)
        _ ->
          r = parse_expr(src, tokens, pos2, pool)
          match is_err(r)
            true -> r
            _ ->
              glyph_array_push(elems, r.node)
              parse_array_loop(src, tokens, r.pos, pool, elems)
    _ ->
      p2 = expect_tok(tokens, pos, tk_rbracket())
      match p2 < 0
        true -> mk_err_msg(pos, "expected ']' or ','")
        _ ->
          idx = pool_push(pool, mk_node(ex_array(), 0, "", 0 - 1, 0 - 1, 0 - 1, elems))
          mk_result(idx, p2)

-- fn parse_atom
parse_atom src tokens pos pool =
  k = cur_kind(tokens, pos)
  match k
    _ ? k == tk_int() ->
      v = cur_ival(src, tokens, pos)
      idx = push_simple(pool, ex_int_lit(), v, "")
      mk_result(idx, pos + 1)
    _ ? k == tk_str() ->
      s = cur_text(src, tokens, pos)
      idx = push_simple(pool, ex_str_lit(), 0, s)
      mk_result(idx, pos + 1)
    _ ? k == tk_str_interp_start() -> parse_str_interp(src, tokens, pos + 1, pool)
    _ ? k == tk_ident() ->
      text = cur_text(src, tokens, pos)
      match text
        _ ? glyph_str_eq(text, "true") ->
          idx = push_simple(pool, ex_bool_lit(), 1, "")
          mk_result(idx, pos + 1)
        _ ? glyph_str_eq(text, "false") ->
          idx = push_simple(pool, ex_bool_lit(), 0, "")
          mk_result(idx, pos + 1)
        _ ->
          idx = push_simple(pool, ex_ident(), 0, text)
          mk_result(idx, pos + 1)
    _ ? k == tk_type_ident() ->
      text = cur_text(src, tokens, pos)
      idx = push_simple(pool, ex_type_ident(), 0, text)
      mk_result(idx, pos + 1)
    _ ? k == tk_lparen() ->
      r = parse_expr(src, tokens, pos + 1, pool)
      match is_err(r)
        true -> r
        _ ->
          p2 = expect_tok(tokens, r.pos, tk_rparen())
          match p2 < 0
            true -> mk_err_msg(r.pos, "expected ')'")
            _ -> mk_result(r.node, p2)
    _ ? k == tk_lbracket() -> parse_array(src, tokens, pos + 1, pool)
    _ ? k == tk_lbrace() -> parse_record(src, tokens, pos + 1, pool)
    _ ? k == tk_minus() ->
      inner = parse_unary(src, tokens, pos + 1, pool)
      match is_err(inner)
        true -> inner
        _ ->
          idx = pool_push(pool, mk_node(ex_unary(), op_neg(), "", inner.node, 0 - 1, 0 - 1, []))
          mk_result(idx, inner.pos)
    _ ? k == tk_bang() ->
      inner = parse_unary(src, tokens, pos + 1, pool)
      match is_err(inner)
        true -> inner
        _ ->
          idx = pool_push(pool, mk_node(ex_unary(), op_not(), "", inner.node, 0 - 1, 0 - 1, []))
          mk_result(idx, inner.pos)
    _ ? k == tk_backslash() -> parse_lambda(src, tokens, pos + 1, pool)
    _ ? k == tk_match() -> parse_match_expr(src, tokens, pos + 1, pool)
    _ ? k == tk_dot() ->
      match cur_kind(tokens, pos + 1) == tk_ident()
        true ->
          fname = cur_text(src, tokens, pos + 1)
          idx = push_simple(pool, ex_field_accessor(), 0, fname)
          mk_result(idx, pos + 2)
        _ -> mk_err_msg(pos, "expected field name after '.'")
    _ ? k == tk_float() ->
      s = cur_text(src, tokens, pos)
      idx = push_simple(pool, ex_float_lit(), 0, s)
      mk_result(idx, pos + 1)
    _ -> mk_err_msg(pos, "expected expression")

-- fn parse_bitwise
parse_bitwise src tokens pos pool =
  left = parse_add(src, tokens, pos, pool)
  match is_err(left)
    true -> left
    _ -> parse_bitwise_loop(src, tokens, left.pos, pool, left.node)

-- fn parse_bitwise_loop
parse_bitwise_loop src tokens pos pool left =
  k = cur_kind(tokens, pos)
  op = match k
    _ ? k == tk_bitand() -> op_bitand()
    _ ? k == tk_bitor() -> op_bitor()
    _ ? k == tk_bitxor() -> op_bitxor()
    _ ? k == tk_shl() -> op_shl()
    _ ? k == tk_shr() -> op_shr()
    _ -> 0 - 1
  match op < 0
    true -> mk_result(left, pos)
    _ ->
      right = parse_add(src, tokens, pos + 1, pool)
      match is_err(right)
        true -> right
        _ ->
          idx = pool_push(pool, mk_node(ex_binary(), op, "", left, right.node, 0 - 1, []))
          parse_bitwise_loop(src, tokens, right.pos, pool, idx)

-- fn parse_block
parse_block src tokens pos pool =
  stmts = []
  parse_block_stmts(src, tokens, pos, pool, stmts)

-- fn parse_block_stmts
parse_block_stmts src tokens pos pool stmts =
  k = cur_kind(tokens, pos)
  match k
    _ ? k == tk_dedent() ->
      idx = pool_push(pool, mk_node(ex_block(), 0, "", 0 - 1, 0 - 1, 0 - 1, stmts))
      mk_result(idx, pos + 1)
    _ ? k == tk_eof() ->
      idx = pool_push(pool, mk_node(ex_block(), 0, "", 0 - 1, 0 - 1, 0 - 1, stmts))
      mk_result(idx, pos)
    _ ->
      sr = parse_stmt(src, tokens, pos, pool)
      match is_err(sr)
        true -> sr
        _ ->
          glyph_array_push(stmts, sr.node)
          pos2 = skip_nl(tokens, sr.pos)
          parse_block_stmts(src, tokens, pos2, pool, stmts)

-- fn parse_body
parse_body src tokens pos pool =
  pos2 = skip_nl(tokens, pos)
  match cur_kind(tokens, pos2) == tk_indent()
    true -> parse_block(src, tokens, pos2 + 1, pool)
    _ -> parse_expr(src, tokens, pos2, pool)

-- fn parse_cmp
parse_cmp src tokens pos pool =
  left = parse_bitwise(src, tokens, pos, pool)
  match is_err(left)
    true -> left
    _ -> parse_cmp_loop(src, tokens, left.pos, pool, left.node)

-- fn parse_cmp_loop
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
  match op < 0
    true -> mk_result(left, pos)
    _ ->
      right = parse_add(src, tokens, pos + 1, pool)
      match is_err(right)
        true -> right
        _ ->
          idx = pool_push(pool, mk_node(ex_binary(), op, "", left, right.node, 0 - 1, []))
          parse_cmp_loop(src, tokens, right.pos, pool, idx)

-- fn parse_compose
parse_compose src tokens pos pool =
  left = parse_logic_or(src, tokens, pos, pool)
  match is_err(left)
    true -> left
    _ -> parse_compose_loop(src, tokens, left.pos, pool, left.node)

-- fn parse_compose_loop
parse_compose_loop src tokens pos pool left =
  match cur_kind(tokens, pos) == tk_gt_gt()
    true ->
      right = parse_logic_or(src, tokens, pos + 1, pool)
      match is_err(right)
        true -> right
        _ ->
          idx = pool_push(pool, mk_node(ex_compose(), 0, "", left, right.node, 0 - 1, []))
          parse_compose_loop(src, tokens, right.pos, pool, idx)
    _ -> mk_result(left, pos)

-- fn parse_cover_lines
parse_cover_lines content pos names hits =
  len = glyph_str_len(content)
  match pos >= len
    true -> 0
    _ ->
      next = pcl_line(content, pos, names, hits)
      parse_cover_lines(content, next, names, hits)


-- fn parse_ctor_pats
parse_ctor_pats src tokens pos pool name pats =
  match cur_kind(tokens, pos) == tk_rparen()
    true ->
      idx = pool_push(pool, mk_node(pat_ctor(), 0, name, 0 - 1, 0 - 1, 0 - 1, pats))
      mk_result(idx, pos + 1)
    _ ->
      pr = parse_pattern(src, tokens, pos, pool)
      match is_err(pr)
        true -> pr
        _ ->
          glyph_array_push(pats, pr.node)
          match cur_kind(tokens, pr.pos) == tk_comma()
            true -> parse_ctor_pats(src, tokens, pr.pos + 1, pool, name, pats)
            _ ->
              p2 = expect_tok(tokens, pr.pos, tk_rparen())
              match p2 < 0
                true -> mk_err(pr.pos)
                _ ->
                  idx = pool_push(pool, mk_node(pat_ctor(), 0, name, 0 - 1, 0 - 1, 0 - 1, pats))
                  mk_result(idx, p2)

-- fn parse_def
parse_def src tokens pos pool = parse_fn_def(src, tokens, pos, pool)

-- fn parse_destr_fields
parse_destr_fields src tokens pos pool =
  names = []
  pdf_loop(src, tokens, pos, pool, names)


-- fn parse_expr
parse_expr src tokens pos pool = parse_pipe_expr(src, tokens, pos, pool)

-- fn parse_fn_def
parse_fn_def src tokens pos pool =
  match cur_kind(tokens, pos) == tk_ident()
    true ->
      fname = cur_text(src, tokens, pos)
      params = []
      pr = parse_params_loop(src, tokens, pos + 1, pool, params)
      p2 = expect_tok(tokens, pr.pos, tk_eq())
      match p2 < 0
        true -> mk_err_msg(pr.pos, "expected '=' after parameters")
        _ ->
          body_r = parse_body(src, tokens, p2, pool)
          match is_err(body_r)
            true -> body_r
            _ ->
              idx = pool_push(pool, mk_node(df_fn(), 0, fname, body_r.node, 0 - 1, 0 - 1, params))
              mk_result(idx, body_r.pos)
    _ -> mk_err_msg(pos, "expected function name")

-- fn parse_lambda
parse_lambda src tokens pos pool =
  params = []
  parse_lambda_params(src, tokens, pos, pool, params)

-- fn parse_lambda_params
parse_lambda_params src tokens pos pool params =
  match cur_kind(tokens, pos) == tk_arrow()
    true ->
      body_r = parse_expr(src, tokens, pos + 1, pool)
      match is_err(body_r)
        true -> body_r
        _ ->
          idx = pool_push(pool, mk_node(ex_lambda(), 0, "", body_r.node, 0 - 1, 0 - 1, params))
          mk_result(idx, body_r.pos)
    _ -> match cur_kind(tokens, pos) == tk_ident()
      true ->
        pname = cur_text(src, tokens, pos)
        pidx = push_simple(pool, nd_param(), 0, pname)
        glyph_array_push(params, pidx)
        parse_lambda_params(src, tokens, pos + 1, pool, params)
      _ -> mk_err_msg(pos, "expected '->' or parameter name")

-- fn parse_logic_and
parse_logic_and src tokens pos pool =
  left = parse_cmp(src, tokens, pos, pool)
  match is_err(left)
    true -> left
    _ -> parse_logic_and_loop(src, tokens, left.pos, pool, left.node)

-- fn parse_logic_and_loop
parse_logic_and_loop src tokens pos pool left =
  match cur_kind(tokens, pos) == tk_and()
    true ->
      right = parse_cmp(src, tokens, pos + 1, pool)
      match is_err(right)
        true -> right
        _ ->
          idx = pool_push(pool, mk_node(ex_binary(), op_and(), "", left, right.node, 0 - 1, []))
          parse_logic_and_loop(src, tokens, right.pos, pool, idx)
    _ -> mk_result(left, pos)

-- fn parse_logic_or
parse_logic_or src tokens pos pool =
  left = parse_logic_and(src, tokens, pos, pool)
  match is_err(left)
    true -> left
    _ -> parse_logic_or_loop(src, tokens, left.pos, pool, left.node)

-- fn parse_logic_or_loop
parse_logic_or_loop src tokens pos pool left =
  match cur_kind(tokens, pos) == tk_or()
    true ->
      right = parse_logic_and(src, tokens, pos + 1, pool)
      match is_err(right)
        true -> right
        _ ->
          idx = pool_push(pool, mk_node(ex_binary(), op_or(), "", left, right.node, 0 - 1, []))
          parse_logic_or_loop(src, tokens, right.pos, pool, idx)
    _ -> mk_result(left, pos)

-- fn parse_match_arm_body
parse_match_arm_body src tokens pos pool =
  match cur_kind(tokens, pos) == tk_indent()
    true -> parse_block(src, tokens, pos + 1, pool)
    _ -> parse_expr(src, tokens, pos, pool)

-- fn parse_match_arms
parse_match_arms src tokens pos pool scrut arms =
  k = cur_kind(tokens, pos)
  match k
    _ ? k == tk_dedent() ->
      idx = pool_push(pool, mk_node(ex_match(), 0, "", scrut, 0 - 1, 0 - 1, arms))
      mk_result(idx, pos + 1)
    _ ? k == tk_eof() ->
      idx = pool_push(pool, mk_node(ex_match(), 0, "", scrut, 0 - 1, 0 - 1, arms))
      mk_result(idx, pos)
    _ ->
      pr = parse_pattern(src, tokens, pos, pool)
      match is_err(pr)
        true -> pr
        _ ->
          gpos = pr.pos
          match cur_kind(tokens, gpos) == tk_question()
            true ->
              gr = parse_expr(src, tokens, gpos + 1, pool)
              match is_err(gr)
                true -> gr
                _ ->
                  p2 = expect_tok(tokens, gr.pos, tk_arrow())
                  match p2 < 0
                    true -> mk_err_msg(gr.pos, "expected '->' in match arm")
                    _ ->
                      pos3 = skip_nl(tokens, p2)
                      br = parse_match_arm_body(src, tokens, pos3, pool)
                      match is_err(br)
                        true -> br
                        _ ->
                          glyph_array_push(arms, pr.node)
                          glyph_array_push(arms, br.node)
                          glyph_array_push(arms, gr.node)
                          pos4 = skip_nl(tokens, br.pos)
                          parse_match_arms(src, tokens, pos4, pool, scrut, arms)
            _ ->
              p2 = expect_tok(tokens, gpos, tk_arrow())
              match p2 < 0
                true -> mk_err_msg(gpos, "expected '->' in match arm")
                _ ->
                  pos3 = skip_nl(tokens, p2)
                  br = parse_match_arm_body(src, tokens, pos3, pool)
                  match is_err(br)
                    true -> br
                    _ ->
                      glyph_array_push(arms, pr.node)
                      glyph_array_push(arms, br.node)
                      glyph_array_push(arms, 0 - 1)
                      pos4 = skip_nl(tokens, br.pos)
                      parse_match_arms(src, tokens, pos4, pool, scrut, arms)

-- fn parse_match_expr
parse_match_expr src tokens pos pool =
  scrut = parse_expr(src, tokens, pos, pool)
  match is_err(scrut)
    true -> scrut
    _ ->
      pos2 = skip_nl(tokens, scrut.pos)
      p3 = expect_tok(tokens, pos2, tk_indent())
      match p3 < 0
        true -> mk_err_msg(pos2, "expected indented match arms")
        _ ->
          arms = []
          parse_match_arms(src, tokens, p3, pool, scrut.node, arms)

-- fn parse_mul
parse_mul src tokens pos pool =
  left = parse_unary(src, tokens, pos, pool)
  match is_err(left)
    true -> left
    _ -> parse_mul_loop(src, tokens, left.pos, pool, left.node)

-- fn parse_mul_loop
parse_mul_loop src tokens pos pool left =
  k = cur_kind(tokens, pos)
  op = match k
    _ ? k == tk_star() -> op_mul()
    _ ? k == tk_slash() -> op_div()
    _ ? k == tk_percent() -> op_mod()
    _ -> 0 - 1
  match op < 0
    true -> mk_result(left, pos)
    _ ->
      right = parse_unary(src, tokens, pos + 1, pool)
      match is_err(right)
        true -> right
        _ ->
          idx = pool_push(pool, mk_node(ex_binary(), op, "", left, right.node, 0 - 1, []))
          parse_mul_loop(src, tokens, right.pos, pool, idx)

-- fn parse_or_pats
parse_or_pats src tokens pos pool subs =
  match cur_kind(tokens, pos) == tk_pipe()
    true ->
      pr = parse_single_pattern(src, tokens, pos + 1, pool)
      match is_err(pr)
        true -> pr
        _ ->
          glyph_array_push(subs, pr.node)
          parse_or_pats(src, tokens, pr.pos, pool, subs)
    _ ->
      idx = pool_push(pool, mk_node(pat_or(), 0, "", 0 - 1, 0 - 1, 0 - 1, subs))
      mk_result(idx, pos)


-- fn parse_params
parse_params src tokens pos pool =
  params = []
  parse_params_loop(src, tokens, pos, pool, params)

-- fn parse_params_loop
parse_params_loop src tokens pos pool params =
  match cur_kind(tokens, pos) == tk_ident()
    true ->
      pname = cur_text(src, tokens, pos)
      pidx = push_simple(pool, nd_param(), 0, pname)
      glyph_array_push(params, pidx)
      parse_params_loop(src, tokens, pos + 1, pool, params)
    _ -> mk_result(0 - 1, pos)

-- fn parse_pattern
parse_pattern src tokens pos pool =
  pr = parse_single_pattern(src, tokens, pos, pool)
  match is_err(pr)
    true -> pr
    _ ->
      match cur_kind(tokens, pr.pos) == tk_pipe()
        true ->
          subs = [pr.node]
          parse_or_pats(src, tokens, pr.pos, pool, subs)
        _ -> pr


-- fn parse_pipe_expr
parse_pipe_expr src tokens pos pool =
  left = parse_compose(src, tokens, pos, pool)
  match is_err(left)
    true -> left
    _ -> parse_pipe_loop(src, tokens, left.pos, pool, left.node)

-- fn parse_pipe_loop
parse_pipe_loop src tokens pos pool left =
  match cur_kind(tokens, pos) == tk_pipe_gt()
    true ->
      right = parse_compose(src, tokens, pos + 1, pool)
      match is_err(right)
        true -> right
        _ ->
          idx = pool_push(pool, mk_node(ex_pipe(), 0, "", left, right.node, 0 - 1, []))
          parse_pipe_loop(src, tokens, right.pos, pool, idx)
    _ -> mk_result(left, pos)

-- fn parse_postfix
parse_postfix src tokens pos pool =
  r = parse_atom(src, tokens, pos, pool)
  match is_err(r)
    true -> r
    _ -> parse_postfix_loop(src, tokens, r.pos, pool, r.node)

-- fn parse_postfix_loop
parse_postfix_loop src tokens pos pool node =
  k = cur_kind(tokens, pos)
  match k
    _ ? k == tk_dot() ->
      match cur_kind(tokens, pos + 1) == tk_ident()
        true ->
          fname = cur_text(src, tokens, pos + 1)
          idx = pool_push(pool, mk_node(ex_field_access(), 0, fname, node, 0 - 1, 0 - 1, []))
          parse_postfix_loop(src, tokens, pos + 2, pool, idx)
        _ ->
          match cur_kind(tokens, pos + 1) == tk_type_ident()
            true ->
              fname = cur_text(src, tokens, pos + 1)
              idx = pool_push(pool, mk_node(ex_field_access(), 0, fname, node, 0 - 1, 0 - 1, []))
              parse_postfix_loop(src, tokens, pos + 2, pool, idx)
            _ -> mk_err(pos + 1)
    _ ? k == tk_lparen() ->
      ar = parse_args(src, tokens, pos + 1, pool)
      match is_err(ar)
        true -> ar
        _ ->
          idx = pool_push(pool, mk_node(ex_call(), 0, "", node, 0 - 1, 0 - 1, (pool[ar.node]).ns))
          p2 = expect_tok(tokens, ar.pos, tk_rparen())
          match p2 < 0
            true -> mk_err(ar.pos)
            _ -> parse_postfix_loop(src, tokens, p2, pool, idx)
    _ ? k == tk_lbracket() ->
      ir = parse_expr(src, tokens, pos + 1, pool)
      match is_err(ir)
        true -> ir
        _ ->
          p2 = expect_tok(tokens, ir.pos, tk_rbracket())
          match p2 < 0
            true -> mk_err(ir.pos)
            _ ->
              idx = pool_push(pool, mk_node(ex_index(), 0, "", node, ir.node, 0 - 1, []))
              parse_postfix_loop(src, tokens, p2, pool, idx)
    _ ? k == tk_question() ->
      idx = pool_push(pool, mk_node(ex_propagate(), 0, "", node, 0 - 1, 0 - 1, []))
      parse_postfix_loop(src, tokens, pos + 1, pool, idx)
    _ ? k == tk_bang() ->
      idx = pool_push(pool, mk_node(ex_unwrap(), 0, "", node, 0 - 1, 0 - 1, []))
      parse_postfix_loop(src, tokens, pos + 1, pool, idx)
    _ -> mk_result(node, pos)

-- fn parse_record
parse_record src tokens pos pool =
  match cur_kind(tokens, pos) == tk_rbrace()
    true ->
      idx = pool_push(pool, mk_node(ex_record(), 0, "", 0 - 1, 0 - 1, 0 - 1, []))
      mk_result(idx, pos + 1)
    _ ->
      pairs = []
      parse_record_loop(src, tokens, pos, pool, pairs)

-- fn parse_record_loop
parse_record_loop src tokens pos pool pairs =
  match cur_kind(tokens, pos) == tk_ident()
    true ->
      fname = cur_text(src, tokens, pos)
      p2 = expect_tok(tokens, pos + 1, tk_colon())
      match p2 < 0
        true -> mk_err_msg(pos + 1, "expected ':' after field name")
        _ ->
          r = parse_expr(src, tokens, p2, pool)
          match is_err(r)
            true -> r
            _ ->
              name_idx = push_simple(pool, ex_ident(), 0, fname)
              glyph_array_push(pairs, name_idx)
              glyph_array_push(pairs, r.node)
              match cur_kind(tokens, r.pos) == tk_comma()
                true -> parse_record_loop(src, tokens, r.pos + 1, pool, pairs)
                _ -> match cur_kind(tokens, r.pos) == tk_ident()
                  true -> parse_record_loop(src, tokens, r.pos, pool, pairs)
                  _ ->
                    p3 = expect_tok(tokens, r.pos, tk_rbrace())
                    match p3 < 0
                      true -> mk_err_msg(r.pos, "expected '\}' or field name")
                      _ ->
                        idx = pool_push(pool, mk_node(ex_record(), 0, "", 0 - 1, 0 - 1, 0 - 1, pairs))
                        mk_result(idx, p3)
    _ ->
      p3 = expect_tok(tokens, pos, tk_rbrace())
      match p3 < 0
        true -> mk_err_msg(pos, "expected '\}'")
        _ ->
          idx = pool_push(pool, mk_node(ex_record(), 0, "", 0 - 1, 0 - 1, 0 - 1, pairs))
          mk_result(idx, p3)

-- fn parse_single_pattern
parse_single_pattern src tokens pos pool =
  k = cur_kind(tokens, pos)
  match k
    _ ? k == tk_ident() ->
      text = cur_text(src, tokens, pos)
      match text
        _ ? glyph_str_eq(text, "_") ->
          idx = push_simple(pool, pat_wildcard(), 0, "")
          mk_result(idx, pos + 1)
        _ ? glyph_str_eq(text, "true") ->
          idx = push_simple(pool, pat_bool(), 1, "")
          mk_result(idx, pos + 1)
        _ ? glyph_str_eq(text, "false") ->
          idx = push_simple(pool, pat_bool(), 0, "")
          mk_result(idx, pos + 1)
        _ ->
          idx = push_simple(pool, pat_ident(), 0, text)
          mk_result(idx, pos + 1)
    _ ? k == tk_int() ->
      v = cur_ival(src, tokens, pos)
      idx = push_simple(pool, pat_int(), v, "")
      mk_result(idx, pos + 1)
    _ ? k == tk_str() ->
      s = cur_text(src, tokens, pos)
      idx = push_simple(pool, pat_str(), 0, glyph_str_slice(s, 1, glyph_str_len(s) - 1))
      mk_result(idx, pos + 1)
    _ ? k == tk_type_ident() ->
      name = cur_text(src, tokens, pos)
      match cur_kind(tokens, pos + 1) == tk_lparen()
        true ->
          cpats = []
          parse_ctor_pats(src, tokens, pos + 2, pool, name, cpats)
        _ ->
          idx = pool_push(pool, mk_node(pat_ctor(), 0, name, 0 - 1, 0 - 1, 0 - 1, []))
          mk_result(idx, pos + 1)
    _ -> mk_err(pos)

-- fn parse_stmt
parse_stmt src tokens pos pool =
  match cur_kind(tokens, pos) == tk_lbrace()
    true ->
      match cur_kind(tokens, pos + 1) == tk_ident()
        true ->
          k2 = cur_kind(tokens, pos + 2)
          match k2 == tk_comma() || k2 == tk_rbrace()
            true ->
              dr = parse_destr_fields(src, tokens, pos + 1, pool)
              names = dr.node
              p2 = expect_tok(tokens, dr.pos, tk_rbrace())
              match p2 < 0
                true -> mk_err_msg(dr.pos, "expected '}'")
                _ ->
                  p3 = expect_tok(tokens, p2, tk_eq())
                  match p3 < 0
                    true -> mk_err_msg(p2, "expected '='")
                    _ ->
                      rhs = parse_expr(src, tokens, p3, pool)
                      match is_err(rhs)
                        true -> rhs
                        _ ->
                          idx = pool_push(pool, mk_node(st_let_destr(), 0, "", rhs.node, 0 - 1, 0 - 1, names))
                          mk_result(idx, rhs.pos)
            _ -> parse_stmt_expr(src, tokens, pos, pool)
        _ -> parse_stmt_expr(src, tokens, pos, pool)
    _ -> parse_stmt_expr(src, tokens, pos, pool)


-- fn parse_stmt_expr
parse_stmt_expr src tokens pos pool =
  er = parse_expr(src, tokens, pos, pool)
  match is_err(er)
    true -> er
    _ ->
      match cur_kind(tokens, er.pos) == tk_colon_eq()
        true ->
          rhs = parse_expr(src, tokens, er.pos + 1, pool)
          match is_err(rhs)
            true -> rhs
            _ ->
              idx = pool_push(pool, mk_node(st_assign(), 0, "", er.node, rhs.node, 0 - 1, []))
              mk_result(idx, rhs.pos)
        _ -> match cur_kind(tokens, er.pos) == tk_eq()
          true ->
            enode = pool[er.node]
            match enode.kind == ex_ident()
              true ->
                rhs = parse_expr(src, tokens, er.pos + 1, pool)
                match is_err(rhs)
                  true -> rhs
                  _ ->
                    idx = pool_push(pool, mk_node(st_let(), 0, enode.sval, rhs.node, 0 - 1, 0 - 1, []))
                    mk_result(idx, rhs.pos)
              _ ->
                idx = pool_push(pool, mk_node(st_expr(), 0, "", er.node, 0 - 1, 0 - 1, []))
                mk_result(idx, er.pos)
          _ ->
            idx = pool_push(pool, mk_node(st_expr(), 0, "", er.node, 0 - 1, 0 - 1, []))
            mk_result(idx, er.pos)


-- fn parse_str_interp
parse_str_interp src tokens pos pool =
  psi_loop(src, tokens, pos, pool, [])

-- fn parse_struct_ctypes
parse_struct_ctypes body =
  len = glyph_str_len(body)
  lbrace = psf_find_char(body, 123, 0, len)
  match lbrace < 0
    true -> []
    _ ->
      rbrace = psf_find_char(body, 125, lbrace + 1, len)
      match rbrace < 0
        true -> []
        _ ->
          inner = glyph_str_slice(body, lbrace + 1, rbrace)
          pairs = []
          psct_extract_pairs(inner, 0, 0, pairs, "")
          sorted = sort_str_arr(pairs)
          types = []
          psct_types_from_pairs(sorted, 0, types)
          types

-- fn parse_struct_fields
parse_struct_fields body =
  len = glyph_str_len(body)
  lbrace = psf_find_char(body, 123, 0, len)
  match lbrace < 0
    true -> []
    _ ->
      rbrace = psf_find_char(body, 125, lbrace + 1, len)
      match rbrace < 0
        true -> []
        _ ->
          inner = glyph_str_slice(body, lbrace + 1, rbrace)
          names = psf_extract_names(inner)
          sort_str_arr(names)

-- fn parse_unary
parse_unary src tokens pos pool =
  k = cur_kind(tokens, pos)
  match k
    _ ? k == tk_minus() ->
      inner = parse_unary(src, tokens, pos + 1, pool)
      match is_err(inner)
        true -> inner
        _ ->
          idx = pool_push(pool, mk_node(ex_unary(), op_neg(), "", inner.node, 0 - 1, 0 - 1, []))
          mk_result(idx, inner.pos)
    _ ? k == tk_bang() ->
      inner = parse_unary(src, tokens, pos + 1, pool)
      match is_err(inner)
        true -> inner
        _ ->
          idx = pool_push(pool, mk_node(ex_unary(), op_not(), "", inner.node, 0 - 1, 0 - 1, []))
          mk_result(idx, inner.pos)
    _ -> parse_postfix(src, tokens, pos, pool)

-- fn pat_bool
pat_bool = 103

-- fn pat_ctor
pat_ctor = 105

-- fn pat_ident
pat_ident = 101

-- fn pat_int
pat_int = 102

-- fn pat_or
pat_or = 106


-- fn pat_str
pat_str = 104

-- fn pat_wildcard
pat_wildcard = 100

-- fn pcl_find_nl
pcl_find_nl content pos len =
  match pos >= len
    true -> len
    _ ->
      match glyph_str_char_at(content, pos) == 10
        true -> pos
        _ -> pcl_find_nl(content, pos + 1, len)


-- fn pcl_find_tab
pcl_find_tab content pos len =
  match pos >= len
    true -> len
    _ ->
      match glyph_str_char_at(content, pos) == 9
        true -> pos
        _ -> pcl_find_tab(content, pos + 1, len)


-- fn pcl_line
pcl_line content pos names hits =
  len = glyph_str_len(content)
  match pos >= len
    true -> pos
    _ ->
      ch = glyph_str_char_at(content, pos)
      match ch == 10
        true -> pos + 1
        _ ->
          tab_pos = pcl_find_tab(content, pos, len)
          match tab_pos >= len
            true -> len
            _ ->
              name = glyph_str_slice(content, pos, tab_pos)
              nl_pos = pcl_find_nl(content, tab_pos + 1, len)
              hits_str = glyph_str_slice(content, tab_pos + 1, nl_pos)
              glyph_array_push(names, name)
              glyph_array_push(hits, glyph_str_to_int(hits_str))
              match nl_pos >= len
                true -> len
                _ -> nl_pos + 1


-- fn pdf_loop
pdf_loop src tokens pos pool names =
  match cur_kind(tokens, pos) == tk_ident()
    true ->
      name = cur_text(src, tokens, pos)
      glyph_array_push(names, name)
      match cur_kind(tokens, pos + 1) == tk_comma()
        true -> pdf_loop(src, tokens, pos + 2, pool, names)
        _ -> {node: names, pos: pos + 1}
    _ -> {node: names, pos: pos}


-- fn peek
peek src pos len =
  match pos < len
    true -> glyph_str_char_at(src, pos)
    _ -> 0

-- fn pool_get
pool_get eng idx =
  node = eng.ty_pool[idx]
  _ = node.tag
  node

-- fn pool_push
pool_push pool node =
  idx = glyph_array_len(pool)
  glyph_array_push(pool, node)
  idx

-- fn print_history_rows
print_history_rows rows i n =
  match i >= n
    true -> 0
    _ ->
      row = rows[i]
      println(s7("  #", row[0], "  ", row[1], "  gen=", row[2], s5("  ", row[3], "  ", row[4], " bytes")))
      print_history_rows(rows, i + 1, n)

-- fn print_ls_rows
print_ls_rows rows i n =
  match i < n
    true ->
      row = rows[i]
      line = s7(row[0], "	", row[1], "	", row[2], "	", s2(row[3], "tok"))
      println(line)
      print_ls_rows(rows, i + 1, n)
    _ -> 0

-- fn print_sql_rows
print_sql_rows rows i n =
  match i < n
    true ->
      row = rows[i]
      cols = glyph_array_len(row)
      line = join_cols(row, 0, cols, "")
      println(line)
      print_sql_rows(rows, i + 1, n)
    _ -> 0

-- fn print_usage
print_usage =
  eprintln("Usage: glyph <command> <db> [args...]")
  eprintln("")
  eprintln("Commands:")
  eprintln("  init <db>                     Create new database")
  eprintln("  build <db> [output] [--gen N]  Compile definitions")
  eprintln("  run <db>                      Build and execute")
  eprintln("  test <db> [name...] [--cover] Run test definitions")
  eprintln("  cover <db>                    Show coverage report")
  eprintln("  get <db> <name> [--kind K]    Read definition body")
  eprintln("  put <db> <kind> -b <body> [--gen N]  Create/update definition")
  eprintln("  put <db> <kind> -f <file> [--gen N]  Create/update from file")
  eprintln("  rm <db> <name> [--force]      Remove definition")
  eprintln("  ls <db> [--kind K] [--sort S] List definitions")
  eprintln("  find <db> <pat> [--body]      Search definitions")
  eprintln("  deps <db> <name>              Forward dependencies")
  eprintln("  rdeps <db> <name>             Reverse dependencies")
  eprintln("  stat <db>                     Image overview")
  eprintln("  dump <db> [--budget N]        Token-budgeted context")
  eprintln("  sql <db> <query>              Execute raw SQL")
  eprintln("  extern <db> <n> <sym> <sig>   Add extern declaration")
  eprintln("  undo <db> <name> [--kind K]   Undo last change")
  eprintln("  history <db> <name> [--kind K] Show change history")
  eprintln("  mcp <db>                      Start MCP tool server")
  1


-- fn process_esc_loop
process_esc_loop s i len out =
  match i >= len
    true -> out
    _ ->
      ch = glyph_str_char_at(s, i)
      match ch == 92
        true ->
          match i + 1 < len
            true ->
              nch = glyph_str_char_at(s, i + 1)
              esc = match nch
                110 -> "\n"
                116 -> "\t"
                92 -> "\\"
                34 -> "\""
                123 -> "\{"
                125 -> "}"
                _ -> glyph_str_slice(s, i, i + 2)
              process_esc_loop(s, i + 2, len, s2(out, esc))
            _ -> process_esc_loop(s, i + 1, len, s2(out, glyph_str_slice(s, i, i + 1)))
        _ -> process_esc_loop(s, i + 1, len, s2(out, glyph_str_slice(s, i, i + 1)))

-- fn process_escapes
process_escapes s =
  process_esc_loop(s, 0, glyph_str_len(s), "")

-- fn psct_extract_pairs
psct_extract_pairs s start i pairs cur_name =
  len = glyph_str_len(s)
  match i >= len
    true ->
      match i > start
        true ->
          tok = glyph_str_slice(s, start, i)
          colon = psf_find_char(tok, 58, 0, glyph_str_len(tok))
          match colon > 0
            true -> 0
            _ ->
              match glyph_str_len(cur_name) > 0
                true ->
                  tval = psct_strip_comma(tok)
                  glyph_array_push(pairs, s2(cur_name, s2(":", tval)))
                _ -> 0
        _ -> 0
    _ ->
      ch = glyph_str_char_at(s, i)
      match ch == 32
        true ->
          match i > start
            true ->
              tok = glyph_str_slice(s, start, i)
              colon = psf_find_char(tok, 58, 0, glyph_str_len(tok))
              match colon > 0
                true ->
                  nname = glyph_str_slice(tok, 0, colon)
                  psct_extract_pairs(s, i + 1, i + 1, pairs, nname)
                _ ->
                  match glyph_str_len(cur_name) > 0
                    true ->
                      tval = psct_strip_comma(tok)
                      glyph_array_push(pairs, s2(cur_name, s2(":", tval)))
                      psct_extract_pairs(s, i + 1, i + 1, pairs, "")
                    _ -> psct_extract_pairs(s, i + 1, i + 1, pairs, "")
            _ -> psct_extract_pairs(s, i + 1, i + 1, pairs, cur_name)
        _ -> psct_extract_pairs(s, start, i + 1, pairs, cur_name)

-- fn psct_strip_comma
psct_strip_comma tok =
  len = glyph_str_len(tok)
  match len > 0
    true ->
      last = glyph_str_char_at(tok, len - 1)
      match last == 44
        true -> glyph_str_slice(tok, 0, len - 1)
        _ -> tok
    _ -> tok

-- fn psct_types_from_pairs
psct_types_from_pairs pairs i types =
  match i >= glyph_array_len(pairs)
    true -> 0
    _ ->
      pair = pairs[i]
      tspec = extract_type_after_colon(pair)
      glyph_array_push(types, c_field_type(tspec))
      psct_types_from_pairs(pairs, i + 1, types)

-- fn psf_extract_loop
psf_extract_loop s start i result =
  len = glyph_str_len(s)
  match i >= len
    true ->
      match i > start
        true ->
          tok = glyph_str_slice(s, start, i)
          colon = psf_find_char(tok, 58, 0, glyph_str_len(tok))
          match colon > 0
            true -> glyph_array_push(result, glyph_str_slice(tok, 0, colon))
            _ -> 0
        _ -> 0
    _ ->
      ch = glyph_str_char_at(s, i)
      match ch == 32
        true ->
          match i > start
            true ->
              tok = glyph_str_slice(s, start, i)
              colon = psf_find_char(tok, 58, 0, glyph_str_len(tok))
              match colon > 0
                true -> glyph_array_push(result, glyph_str_slice(tok, 0, colon))
                _ -> 0
            _ -> 0
          psf_extract_loop(s, i + 1, i + 1, result)
        _ -> psf_extract_loop(s, start, i + 1, result)

-- fn psf_extract_names
psf_extract_names inner =
  result = []
  psf_extract_loop(inner, 0, 0, result)
  result

-- fn psf_find_char
psf_find_char s ch start limit =
  match start >= limit
    true -> 0 - 1
    _ ->
      match glyph_str_char_at(s, start) == ch
        true -> start
        _ -> psf_find_char(s, ch, start + 1, limit)

-- fn psi_loop
psi_loop src tokens pos pool parts =
  k = cur_kind(tokens, pos)
  match k == tk_str_interp_end()
    true ->
      idx = pool_push(pool, mk_node(ex_str_interp(), 0, "", 0 - 1, 0 - 1, 0 - 1, parts))
      mk_result(idx, pos + 1)
    _ -> match k == tk_eof()
      true -> mk_err(pos)
      _ -> match k == tk_str()
        true ->
          s = cur_text(src, tokens, pos)
          idx = push_simple(pool, ex_str_lit(), 0, s)
          glyph_array_push(parts, idx)
          psi_loop(src, tokens, pos + 1, pool, parts)
        _ ->
          r = parse_expr(src, tokens, pos, pool)
          match is_err(r)
            true -> r
            _ ->
              glyph_array_push(parts, r.node)
              psi_loop(src, tokens, r.pos, pool, parts)

-- fn push_simple
push_simple pool kind ival sval =
  idx = pool_push(pool, mk_node(kind, ival, sval, 0 - 1, 0 - 1, 0 - 1, []))
  idx

-- fn read_externs
read_externs db =
  rows = glyph_db_query_rows(db, "SELECT name, symbol, sig, lib FROM extern_")
  rows

-- fn read_fn_defs
read_fn_defs db =
  rows = glyph_db_query_rows(db, "SELECT body FROM def WHERE kind='fn' ORDER BY name")
  extract_bodies_acc(rows, 0, [])

-- fn read_fn_defs_gen
read_fn_defs_gen db gen_str =
  q = s3("SELECT d.body FROM def d INNER JOIN (SELECT name, kind, MAX(gen) as max_gen FROM def WHERE gen <= ", gen_str, " GROUP BY name, kind) latest ON d.name = latest.name AND d.kind = latest.kind AND d.gen = latest.max_gen WHERE d.kind='fn' ORDER BY d.name")
  rows = glyph_db_query_rows(db, q)
  extract_bodies_acc(rows, 0, [])

-- fn read_test_defs
read_test_defs db =
  rows = glyph_db_query_rows(db, "SELECT body FROM def WHERE kind='test' ORDER BY name")
  extract_bodies_acc(rows, 0, [])

-- fn read_test_defs_gen
read_test_defs_gen db gen_str =
  q = s3("SELECT d.body FROM def d INNER JOIN (SELECT name, kind, MAX(gen) as max_gen FROM def WHERE gen <= ", gen_str, " GROUP BY name, kind) latest ON d.name = latest.name AND d.kind = latest.kind AND d.gen = latest.max_gen WHERE d.kind='test' ORDER BY d.name")
  rows = glyph_db_query_rows(db, q)
  extract_bodies_acc(rows, 0, [])

-- fn read_test_names
read_test_names db =
  rows = glyph_db_query_rows(db, "SELECT name FROM def WHERE kind='test' ORDER BY name")
  extract_bodies_acc(rows, 0, [])

-- fn read_test_names_gen
read_test_names_gen db gen_str =
  q = s3("SELECT d.name FROM def d INNER JOIN (SELECT name, kind, MAX(gen) as max_gen FROM def WHERE gen <= ", gen_str, " GROUP BY name, kind) latest ON d.name = latest.name AND d.kind = latest.kind AND d.gen = latest.max_gen WHERE d.kind='test' ORDER BY d.name")
  rows = glyph_db_query_rows(db, q)
  extract_bodies_acc(rows, 0, [])

-- fn read_type_defs
read_type_defs db =
  rows = glyph_db_query_rows(db, "SELECT name, body FROM def WHERE kind='type' ORDER BY name")
  rows

-- fn read_type_defs_gen
read_type_defs_gen db gen_str =
  q = s3("SELECT d.name, d.body FROM def d INNER JOIN (SELECT name, kind, MAX(gen) as max_gen FROM def WHERE gen <= ", gen_str, " GROUP BY name, kind) latest ON d.name = latest.name AND d.kind = latest.kind AND d.gen = latest.max_gen WHERE d.kind='type' ORDER BY d.name")
  glyph_db_query_rows(db, q)

-- fn register_builtins
register_builtins eng =
  p = eng.ty_pool
  i = mk_tint(p)
  s = mk_tstr(p)
  b = mk_tbool(p)
  v = mk_tvoid(p)
  env_insert(eng, "glyph_str_len", mk_tfn(s, i, p))
  env_insert(eng, "glyph_str_char_at", mk_tfn(s, mk_tfn(i, i, p), p))
  env_insert(eng, "glyph_str_slice", mk_tfn(s, mk_tfn(i, mk_tfn(i, s, p), p), p))
  env_insert(eng, "glyph_str_eq", mk_tfn(s, mk_tfn(s, i, p), p))
  env_insert(eng, "glyph_str_concat", mk_tfn(s, mk_tfn(s, s, p), p))
  env_insert(eng, "glyph_int_to_str", mk_tfn(i, s, p))
  env_insert(eng, "glyph_str_to_int", mk_tfn(s, i, p))
  env_insert(eng, "println", mk_tfn(s, i, p))
  env_insert(eng, "glyph_exit", mk_tfn(i, v, p))
  alen_arr = subst_fresh(eng)
  alen_ty = mk_tfn(alen_arr, i, p)
  env_insert(eng, "glyph_array_len", generalize(eng, alen_ty))
  aset_arr = subst_fresh(eng)
  aset_ty = mk_tfn(aset_arr, mk_tfn(i, mk_tfn(i, v, p), p), p)
  env_insert(eng, "glyph_array_set", generalize(eng, aset_ty))
  0


-- fn report_error
report_error def_name src offset line msg =
  diag = format_diagnostic(def_name, src, offset, line, msg)
  eprintln(diag)

-- fn resolve_fields
resolve_fields eng field_ns i =
  match i >= glyph_array_len(field_ns)
    true -> []
    _ ->
      fi = field_ns[i]
      pool_len = glyph_array_len(eng.ty_pool)
      match fi >= pool_len
        true ->
          eprintln(s5("  tc: FIELD OOB fi=", itos(fi), " pool_len=", itos(pool_len), ""))
          []
        _ ->
          fnode = pool_get(eng, fi)
          _ = fnode.tag
          fname = fnode.sval
          ftype = fnode.n1
          resolved_ty = subst_resolve(eng, ftype)
          new_fi = mk_tfield(fname, resolved_ty, eng.ty_pool)
          result = resolve_fields(eng, field_ns, i + 1)
          glyph_array_push(result, new_fi)
          result


-- fn resolve_fld_off
resolve_fld_off reg fname local_fields =
  match_type = find_matching_type(reg, fname, local_fields, 0)
  match match_type >= 0
    true ->
      t = reg[match_type]
      find_str_in(t, fname, 0)
    _ -> 0 - 1

-- fn resolve_record2
resolve_record2 eng fns rest =
  new_fields = resolve_fields(eng, fns, 0)
  new_rest = match rest < 0
    true -> rest
    _ -> rest
  mk_trecord(new_fields, new_rest, eng.ty_pool)

-- fn resolve_tmap
resolve_tmap eng tm = resolve_tmap_loop(eng, tm, 0)

resolve_tmap_loop eng tm i =
  match i >= glyph_array_len(tm)
    true -> tm
    _ ->
      raw = tm[i]
      tag = tmap_resolve_tag(eng, raw)
      glyph_array_set(tm, i, tag)
      resolve_tmap_loop(eng, tm, i + 1)

-- fn resolve_tmap_loop
resolve_tmap_loop eng tm i =
  match i >= glyph_array_len(tm)
    true -> tm
    _ ->
      raw = tm[i]
      tag = tmap_resolve_tag(eng, raw)
      glyph_array_set(tm, i, tag)
      resolve_tmap_loop(eng, tm, i + 1)

-- fn resolve_tuple_elems
resolve_tuple_elems eng elems i =
  match i >= glyph_array_len(elems)
    true -> []
    _ ->
      resolved = subst_resolve(eng, elems[i])
      rest = resolve_tuple_elems(eng, elems, i + 1)
      glyph_array_push(rest, resolved)
      rest


-- fn rm_check_rdeps
rm_check_rdeps db def_id def_kind name force =
  rdeps = glyph_db_query_rows(db, s3("SELECT from_id FROM dep WHERE to_id = ", def_id, ""))
  rdep_n = glyph_array_len(rdeps)
  match rdep_n > 0
    true ->
      match force == 1
        true ->
          glyph_db_exec(db, s3("DELETE FROM def WHERE id = ", def_id, ""))
          glyph_db_close(db)
          println(s4("removed ", def_kind, " ", name))
          0
        _ ->
          glyph_db_close(db)
          eprintln(s5("error: ", name, " has ", itos(rdep_n), " rdeps. Use --force"))
          1
    _ ->
      glyph_db_exec(db, s3("DELETE FROM def WHERE id = ", def_id, ""))
      glyph_db_close(db)
      println(s4("removed ", def_kind, " ", name))
      0

-- fn rv_aggregate
rv_aggregate = 5

-- fn rv_binop
rv_binop = 2

-- fn rv_call
rv_call = 4

-- fn rv_field
rv_field = 6

-- fn rv_index
rv_index = 7

-- fn rv_make_closure
rv_make_closure = 9

-- fn rv_str_interp
rv_str_interp = 8

-- fn rv_unop
rv_unop = 3

-- fn rv_use
rv_use = 1

-- fn s2
s2 a b = glyph_str_concat(a, b)

-- fn s3
s3 a b c = s2(s2(a, b), c)

-- fn s4
s4 a b c d = s2(s3(a, b, c), d)

-- fn s5
s5 a b c d e = s2(s4(a, b, c, d), e)

-- fn s6
s6 a b c d e f = s2(s5(a, b, c, d, e), f)

-- fn s7
s7 a b c d e f g = s2(s6(a, b, c, d, e, f), g)

-- fn scan_ident_end
scan_ident_end src pos =
  match pos < glyph_str_len(src)
    true ->
      c = glyph_str_char_at(src, pos)
      match is_alnum(c) || c == 95
        true -> scan_ident_end(src, pos + 1)
        _ -> pos
    _ -> pos

-- fn scan_number_end
scan_number_end src pos =
  match pos < glyph_str_len(src)
    true ->
      match is_digit(glyph_str_char_at(src, pos))
        true -> scan_number_end(src, pos + 1)
        _ -> pos
    _ -> pos

-- fn scan_raw_string_end
scan_raw_string_end src pos =
  match pos < glyph_str_len(src)
    true ->
      match glyph_str_char_at(src, pos) == 34
        true -> pos + 1
        _ -> scan_raw_string_end(src, pos + 1)
    _ -> pos

-- fn scan_raw_triple_end
scan_raw_triple_end src pos =
  match pos + 2 < glyph_str_len(src)
    true ->
      match glyph_str_char_at(src, pos) == 34
        true ->
          match glyph_str_char_at(src, pos + 1) == 34
            true ->
              match glyph_str_char_at(src, pos + 2) == 34
                true -> pos + 3
                _ -> scan_raw_triple_end(src, pos + 1)
            _ -> scan_raw_triple_end(src, pos + 1)
        _ -> scan_raw_triple_end(src, pos + 1)
    _ -> pos

-- fn scan_str_has_interp
scan_str_has_interp src pos =
  match pos < glyph_str_len(src)
    true ->
      ch = glyph_str_char_at(src, pos)
      match ch
        34 -> 0
        92 -> scan_str_has_interp(src, pos + 2)
        123 -> 1
        _ -> scan_str_has_interp(src, pos + 1)
    _ -> 0

-- fn scan_string_end
scan_string_end src pos =
  match pos < glyph_str_len(src)
    true ->
      match glyph_str_char_at(src, pos)
        34 -> pos + 1
        92 -> scan_string_end(src, pos + 2)
        _ -> scan_string_end(src, pos + 1)
    _ -> pos

-- fn sig_is_void_ret
sig_is_void_ret parts =
  n = glyph_array_len(parts)
  match glyph_str_eq(parts[n - 1], "V")
    true -> 1
    _ -> 0

-- fn sig_param_count
sig_param_count parts =
  n = glyph_array_len(parts) - 1
  match n == 1
    true -> match glyph_str_eq(parts[0], "V")
      true -> 0
      _ -> 1
    _ -> n

-- fn skip_blanks
skip_blanks src pos line len =
  match pos < len
    true ->
      match glyph_str_char_at(src, pos) == 10
        true -> skip_blanks(src, pos + 1, line + 1, len)
        _ -> pos * 1000000 + line
    _ -> pos * 1000000 + line

-- fn skip_comment
skip_comment src pos =
  match pos < glyph_str_len(src)
    true ->
      match glyph_str_char_at(src, pos) == 10
        true -> pos
        _ -> skip_comment(src, pos + 1)
    _ -> pos

-- fn skip_nl
skip_nl tokens pos =
  match cur_kind(tokens, pos) == tk_newline()
    true -> skip_nl(tokens, pos + 1)
    _ -> pos

-- fn skip_ws
skip_ws src pos =
  match pos < glyph_str_len(src)
    true ->
      match is_space(glyph_str_char_at(src, pos))
        true -> skip_ws(src, pos + 1)
        _ -> pos
    _ -> pos

-- fn sort_str_arr
sort_str_arr arr =
  result = []
  sort_str_copy(arr, result, 0)
  sort_str_do(result, 0)
  result

-- fn sort_str_copy
sort_str_copy arr result i =
  match i >= glyph_array_len(arr)
    true -> 0
    _ ->
      glyph_array_push(result, arr[i])
      sort_str_copy(arr, result, i + 1)

-- fn sort_str_do
sort_str_do arr i =
  match i >= glyph_array_len(arr)
    true -> 0
    _ ->
      sort_str_insert(arr, i)
      sort_str_do(arr, i + 1)

-- fn sort_str_insert
sort_str_insert arr i =
  match i <= 0
    true -> 0
    _ ->
      match str_lt(arr[i], arr[i - 1])
        true ->
          tmp = arr[i]
          glyph_array_set(arr, i, arr[i - 1])
          glyph_array_set(arr, i - 1, tmp)
          sort_str_insert(arr, i - 1)
        _ -> 0

-- fn split_arrow
split_arrow s =
  result = []
  match glyph_str_len(s) == 0
    true -> result
    _ -> split_arrow_loop(s, 0, 0, result)

-- fn split_arrow_loop
split_arrow_loop s start i result =
  len = glyph_str_len(s)
  match i + 3 >= len
    true ->
      glyph_array_push(result, glyph_str_slice(s, start, len))
      result
    _ ->
      match glyph_str_char_at(s, i) == 32
        true -> match glyph_str_char_at(s, i + 1) == 45
          true -> match glyph_str_char_at(s, i + 2) == 62
            true -> match glyph_str_char_at(s, i + 3) == 32
              true ->
                glyph_array_push(result, glyph_str_slice(s, start, i))
                split_arrow_loop(s, i + 4, i + 4, result)
              _ -> split_arrow_loop(s, start, i + 1, result)
            _ -> split_arrow_loop(s, start, i + 1, result)
          _ -> split_arrow_loop(s, start, i + 1, result)
        _ -> split_arrow_loop(s, start, i + 1, result)

-- fn split_comma
split_comma s =
  result = []
  match glyph_str_len(s) == 0
    true -> result
    _ -> split_comma_loop(s, 0, 0, result)

-- fn split_comma_loop
split_comma_loop s start i result =
  len = glyph_str_len(s)
  match i >= len
    true ->
      glyph_array_push(result, glyph_str_slice(s, start, i))
      result
    _ ->
      ch = glyph_str_char_at(s, i)
      match ch == 44
        true ->
          glyph_array_push(result, glyph_str_slice(s, start, i))
          split_comma_loop(s, i + 1, i + 1, result)
        _ -> split_comma_loop(s, start, i + 1, result)

-- fn sql_escape
sql_escape s =
  sql_escape_loop(s, 0, str_len(s), sb_new())

-- fn sql_escape_loop
sql_escape_loop s pos len sb =
  match pos < len
    true ->
      c = str_char_at(s, pos)
      match c == 39
        true ->
          sb_append(sb, "''")
          sql_escape_loop(s, pos + 1, len, sb)
        _ ->
          sb_append(sb, str_slice(s, pos, pos + 1))
          sql_escape_loop(s, pos + 1, len, sb)
    _ -> sb_build(sb)

-- fn st_assign
st_assign = 202

-- fn st_expr
st_expr = 200

-- fn st_let
st_let = 201

-- fn st_let_destr
st_let_destr = 203

-- fn str_contains
str_contains haystack needle =
  hlen = glyph_str_len(haystack)
  nlen = glyph_str_len(needle)
  str_contains_loop(haystack, needle, hlen, nlen, 0)


-- fn str_contains_loop
str_contains_loop haystack needle hlen nlen i =
  match i + nlen > hlen
    true -> 0
    _ ->
      sub = glyph_str_slice(haystack, i, i + nlen)
      match glyph_str_eq(sub, needle)
        true -> 1
        _ -> str_contains_loop(haystack, needle, hlen, nlen, i + 1)


-- fn str_lt
str_lt a b =
  la = glyph_str_len(a)
  lb = glyph_str_len(b)
  str_lt_loop(a, b, 0, la, lb)

-- fn str_lt_loop
str_lt_loop a b i la lb =
  match i >= la
    true -> match i >= lb
      true -> 0
      _ -> 1
    _ -> match i >= lb
      true -> 0
      _ ->
        ca = glyph_str_char_at(a, i)
        cb = glyph_str_char_at(b, i)
        match ca < cb
          true -> 1
          _ -> match ca > cb
            true -> 0
            _ -> str_lt_loop(a, b, i + 1, la, lb)

-- fn strip_ext
strip_ext path =
  len = glyph_str_len(path)
  match len > 6
    true ->
      suffix = glyph_str_slice(path, len - 6, len)
      match glyph_str_eq(suffix, ".glyph") == 1
        true -> glyph_str_slice(path, 0, len - 6)
        _ -> path
    _ -> path

-- fn subst_bind
subst_bind eng var ti =
  root = subst_find(eng.parent, var)
  walked = subst_walk(eng, ti)
  wnode = pool_get(eng, walked)
  wtag = wnode.tag
  wn1 = wnode.n1
  match wtag == ty_var()
    true ->
      root2 = subst_find(eng.parent, wn1)
      match root == root2
        true -> 0
        _ ->
          glyph_array_set(eng.parent, root2, root)
          0
    _ ->
      resolved = subst_resolve(eng, walked)
      match ty_contains_var(eng, resolved, root)
        true -> 0 - 1
        _ ->
          glyph_array_set(eng.bindings, root, resolved)
          0


-- fn subst_find
subst_find parent v =
  p = parent[v]
  match p == v
    true -> v
    _ ->
      root = subst_find(parent, p)
      glyph_array_set(parent, v, root)
      root

-- fn subst_fresh
subst_fresh eng =
  v = subst_fresh_var(eng)
  mk_tvar(v, eng.ty_pool)

-- fn subst_fresh_var
subst_fresh_var eng =
  v = eng.next_var[0]
  glyph_array_push(eng.parent, v)
  glyph_array_push(eng.bindings, 0 - 1)
  glyph_array_set(eng.next_var, 0, v + 1)
  v

-- fn subst_resolve
subst_resolve eng ti =
  w = subst_walk(eng, ti)
  pool_len = glyph_array_len(eng.ty_pool)
  match w < 0
    true -> mk_terror(eng.ty_pool)
    _ ->
      match w >= pool_len
        true -> mk_terror(eng.ty_pool)
        _ ->
          node = pool_get(eng, w)
          tag = node.tag
          n1 = node.n1
          n2 = node.n2
          match tag == ty_fn()
            true ->
              p = subst_resolve(eng, n1)
              r = subst_resolve(eng, n2)
              mk_tfn(p, r, eng.ty_pool)
            _ -> match tag == ty_array()
              true ->
                e = subst_resolve(eng, n1)
                mk_tarray(e, eng.ty_pool)
              _ -> match tag == ty_opt()
                true ->
                  inner = subst_resolve(eng, n1)
                  mk_topt(inner, eng.ty_pool)
                _ -> match tag == ty_record()
                  true ->
                    fns = node.ns
                    resolve_record2(eng, fns, n1)
                  _ -> match tag == ty_tuple()
                    true ->
                      resolved_elems = resolve_tuple_elems(eng, node.ns, 0)
                      mk_ttuple(resolved_elems, eng.ty_pool)
                    _ -> match tag == ty_forall()
                      true ->
                        resolved_body = subst_resolve(eng, n1)
                        mk_tforall(resolved_body, node.ns, eng.ty_pool)
                      _ -> w


-- fn subst_walk
subst_walk eng ti =
  pool_len = glyph_array_len(eng.ty_pool)
  match ti < 0
    true -> mk_terror(eng.ty_pool)
    _ ->
      match ti >= pool_len
        true -> mk_terror(eng.ty_pool)
        _ ->
          node = pool_get(eng, ti)
          match node.tag == ty_var()
            true ->
              root = subst_find(eng.parent, node.n1)
              match root >= glyph_array_len(eng.bindings)
                true -> mk_terror(eng.ty_pool)
                _ ->
                  b = eng.bindings[root]
                  match b < 0
                    true -> ti
                    _ ->
                      match b >= glyph_array_len(eng.ty_pool)
                        true -> mk_terror(eng.ty_pool)
                        _ -> b
            _ -> ti


-- fn subtract_vars
subtract_vars free exclude i =
  match i >= glyph_array_len(free)
    true -> []
    _ ->
      v = free[i]
      rest = subtract_vars(free, exclude, i + 1)
      match var_in_list(v, exclude, 0) == 0
        true ->
          glyph_array_push(rest, v)
          rest
        _ -> rest

-- fn tc_collect_fv
tc_collect_fv eng ti acc =
  w = subst_walk(eng, ti)
  pool_len = glyph_array_len(eng.ty_pool)
  match w < 0
    true -> acc
    _ -> match w >= pool_len
      true -> acc
      _ ->
        node = pool_get(eng, w)
        tag = node.tag
        match tag == ty_var()
          true ->
            root_var = subst_find(eng.parent, node.n1)
            b = eng.bindings[root_var]
            match b < 0
              true ->
                match var_in_list(root_var, acc, 0) == 0
                  true ->
                    glyph_array_push(acc, root_var)
                    acc
                  _ -> acc
              _ -> tc_collect_fv(eng, b, acc)
          _ -> match tag == ty_fn()
            true ->
              tc_collect_fv(eng, node.n1, acc)
              tc_collect_fv(eng, node.n2, acc)
            _ -> match tag == ty_array()
              true -> tc_collect_fv(eng, node.n1, acc)
              _ -> match tag == ty_opt()
                true -> tc_collect_fv(eng, node.n1, acc)
                _ -> match tag == ty_record()
                  true -> tc_collect_fv_fields(eng, node.ns, 0, acc)
                  _ -> match tag == ty_tuple()
                    true -> tc_collect_fv_ns(eng, node.ns, 0, acc)
                    _ -> acc


-- fn tc_collect_fv_fields
tc_collect_fv_fields eng ns i acc =
  match i >= glyph_array_len(ns)
    true -> acc
    _ ->
      fi = ns[i]
      fnode = pool_get(eng, fi)
      _ = fnode.tag
      tc_collect_fv(eng, fnode.n1, acc)
      tc_collect_fv_fields(eng, ns, i + 1, acc)


-- fn tc_collect_fv_ns
tc_collect_fv_ns eng ns i acc =
  match i >= glyph_array_len(ns)
    true -> acc
    _ ->
      tc_collect_fv(eng, ns[i], acc)
      tc_collect_fv_ns(eng, ns, i + 1, acc)


-- fn tc_infer_all
tc_infer_all eng parsed i =
  match i >= glyph_array_len(parsed)
    true -> []
    _ ->
      pf = parsed[i]
      ty = match pf.pf_fn_idx >= 0
        true -> infer_fn_def(eng, pf.pf_ast, pf.pf_fn_idx)
        _ -> 0 - 1
      resolved = match ty >= 0
        true -> subst_resolve(eng, ty)
        _ -> 0 - 1
      rest = tc_infer_all(eng, parsed, i + 1)
      glyph_array_push(rest, resolved)
      rest

-- fn tc_infer_all_tc
tc_infer_all_tc eng parsed i =
  tc_infer_loop(eng, parsed, 0, [])

tc_infer_loop eng parsed i acc =
  match i >= glyph_array_len(parsed)
    true -> acc
    _ ->
      pf = parsed[i]
      result = match pf.pf_fn_idx >= 0
        true ->
          ast_len = glyph_array_len(pf.pf_ast)
          eng_set_tmap(eng, ast_len)
          ty = infer_fn_def(eng, pf.pf_ast, pf.pf_fn_idx)
          tm = eng.tmap[0]
          resolve_tmap(eng, tm)
          resolved = match ty >= 0
            true -> subst_resolve(eng, ty)
            _ -> 0 - 1
          {fn_ty: resolved, fn_tmap: tm}
        _ -> {fn_ty: 0 - 1, fn_tmap: []}
      glyph_array_push(acc, result)
      tc_infer_loop(eng, parsed, i + 1, acc)

-- fn tc_infer_loop
tc_infer_loop eng parsed i acc =
  match i >= glyph_array_len(parsed)
    true -> acc
    _ ->
      pf = parsed[i]
      result = match pf.pf_fn_idx >= 0
        true ->
          ast_len = glyph_array_len(pf.pf_ast)
          eng_set_tmap(eng, ast_len)
          ty = infer_fn_def(eng, pf.pf_ast, pf.pf_fn_idx)
          tm = eng.tmap[0]
          resolve_tmap(eng, tm)
          resolved = match ty >= 0
            true -> subst_resolve(eng, ty)
            _ -> 0 - 1
          gen_ty = match resolved >= 0
            true -> generalize(eng, resolved)
            _ -> resolved
          env_insert(eng, pf.pf_name, gen_ty)
          {fn_ty: resolved, fn_tmap: tm}
        _ -> {fn_ty: 0 - 1, fn_tmap: []}
      glyph_array_push(acc, result)
      tc_infer_loop(eng, parsed, i + 1, acc)


-- fn tc_make_fn_type
tc_make_fn_type eng nparams =
  match nparams == 0
    true -> subst_fresh(eng)
    _ -> tc_make_fn_type_loop(eng, nparams, 0, subst_fresh(eng))

-- fn tc_make_fn_type_loop
tc_make_fn_type_loop eng nparams i ret =
  idx = nparams - 1 - i
  match idx < 0
    true -> ret
    _ ->
      rest = tc_make_fn_type_loop(eng, nparams, i + 1, ret)
      mk_tfn(subst_fresh(eng), rest, eng.ty_pool)

-- fn tc_pre_register
tc_pre_register eng parsed i =
  match i >= glyph_array_len(parsed)
    true -> 0
    _ ->
      pf = parsed[i]
      match pf.pf_fn_idx >= 0
        true ->
          node = (pf.pf_ast)[pf.pf_fn_idx]
          nparams = glyph_array_len(node.ns)
          ftype = tc_make_fn_type(eng, nparams)
          env_insert(eng, pf.pf_name, ftype)
          0
        _ -> 0
      tc_pre_register(eng, parsed, i + 1)

-- fn tc_report_errors
tc_report_errors eng =
  nerrs = glyph_array_len(eng.errors)
  match nerrs > 0
    true ->
      tc_report_loop(eng.errors, 0)
      suffix = match nerrs > 1
        true -> "s"
        _ -> ""
      eprintln(s3(itos(nerrs), " type warning", suffix))
    _ -> 0

-- fn tc_report_loop
tc_report_loop errors i =
  match i >= glyph_array_len(errors)
    true -> 0
    _ ->
      eprintln(s2("  ", errors[i]))
      tc_report_loop(errors, i + 1)

-- fn tco_alloc_temps
tco_alloc_temps locals nparams i = match i >= nparams
  true -> 0
  _ ->
    glyph_array_push(locals, "tco_tmp")
    tco_alloc_temps(locals, nparams, i + 1)

-- fn tco_build_stmts
tco_build_stmts old_stmts limit =
  acc = []
  tco_copy_stmts(acc, old_stmts, limit, 0)

-- fn tco_copy_stmts
tco_copy_stmts acc old_stmts limit i = match i >= limit
  true -> acc
  _ ->
    glyph_array_push(acc, old_stmts[i])
    tco_copy_stmts(acc, old_stmts, limit, i + 1)

-- fn tco_emit_params
tco_emit_params stmts base_tmp nparams i = match i >= nparams
  true -> 0
  _ ->
    tmp_id = base_tmp + i
    s = mk_stmt(i, rv_use(), 0, "", mk_op_local(tmp_id), mk_op_nil(), [])
    glyph_array_push(stmts, s)
    tco_emit_params(stmts, base_tmp, nparams, i + 1)

-- fn tco_emit_temps
tco_emit_temps stmts args base_tmp i = match i >= glyph_array_len(args)
  true -> 0
  _ ->
    tmp_id = base_tmp + i
    arg = args[i]
    s = mk_stmt(tmp_id, rv_use(), 0, "", arg, mk_op_nil(), [])
    glyph_array_push(stmts, s)
    tco_emit_temps(stmts, args, base_tmp, i + 1)

-- fn tco_is_ret_blk
tco_is_ret_blk blks terms tgt_bi ret_local =
  tgt_stmts = blks[tgt_bi]
  tgt_term = terms[tgt_bi]
  match glyph_array_len(tgt_stmts) == 0
    true -> match tgt_term.tkind == tm_return()
      true -> match tgt_term.top.okind == ok_local()
        true -> match tgt_term.top.oval == ret_local
          true -> 1
          _ -> 0
        _ -> 0
      _ -> 0
    _ -> 0

-- fn tco_opt_blks
tco_opt_blks mir blks terms bi = match bi >= glyph_array_len(blks)
  true -> 0
  _ ->
    stmts = blks[bi]
    term = terms[bi]
    n = glyph_array_len(stmts)
    match n >= 2
      true ->
        s_call = stmts[n - 2]
        s_use = stmts[n - 1]
        match s_call.skind == rv_call()
          true -> match s_call.sop1.okind == ok_func_ref()
            true -> match glyph_str_eq(s_call.sop1.ostr, mir.fn_name)
              true -> match s_use.skind == rv_use()
                true -> match s_use.sop1.okind == ok_local()
                  true -> match s_use.sop1.oval == s_call.sdest
                    true -> match term.tkind == tm_goto()
                      true -> match tco_is_ret_blk(blks, terms, term.tgt1, s_use.sdest)
                        true ->
                          tco_transform(mir, blks, bi, stmts, s_call, n)
                          0
                        _ -> 0
                      _ -> 0
                    _ -> 0
                  _ -> 0
                _ -> 0
              _ -> 0
            _ -> 0
          _ -> 0
      _ -> 0
    tco_opt_blks(mir, blks, terms, bi + 1)

-- fn tco_opt_fn
tco_opt_fn mir =
  tco_opt_blks(mir, mir.fn_blocks_stmts, mir.fn_blocks_terms, 0)

-- fn tco_opt_mirs
tco_opt_mirs mirs i = match i >= glyph_array_len(mirs)
  true -> 0
  _ ->
    tco_opt_fn(mirs[i])
    tco_opt_mirs(mirs, i + 1)

-- fn tco_optimize
tco_optimize mirs = tco_opt_mirs(mirs, 0)

-- fn tco_transform
tco_transform mir blks bi stmts s_call n =
  nparams = glyph_array_len(mir.fn_params)
  base_tmp = glyph_array_len(mir.fn_locals)
  tco_alloc_temps(mir.fn_locals, nparams, 0)
  new_stmts = tco_build_stmts(stmts, n - 2)
  tco_emit_temps(new_stmts, s_call.sops, base_tmp, 0)
  tco_emit_params(new_stmts, base_tmp, nparams, 0)
  glyph_array_set(blks, bi, new_stmts)
  glyph_array_set(mir.fn_blocks_terms, bi, mk_term_goto(mir.fn_entry))

-- fn tctx_is_float_bin
tctx_is_float_bin tctx n1 n2 ctx left right =
  lt = tctx_query(tctx, n1)
  rt = tctx_query(tctx, n2)
  match lt == 3
    true -> 1
    _ -> match rt == 3
      true -> 1
      _ -> is_float_op(ctx, left, right)


-- fn tctx_is_str_bin
tctx_is_str_bin tctx n1 n2 ctx left right =
  lt = tctx_query(tctx, n1)
  rt = tctx_query(tctx, n2)
  match lt == 4
    true -> 1
    _ -> match rt == 4
      true -> 1
      _ -> is_str_op(ctx, left, right)

-- fn tctx_query
tctx_query tctx ni =
  tm = tctx.tc_tmap
  match ni < glyph_array_len(tm)
    true -> tm[ni]
    _ -> 0 - 1

-- fn test_collect_kinds
test_collect_kinds toks i acc =
  match i >= glyph_array_len(toks)
    true -> acc
    _ ->
      glyph_array_push(acc, (toks[i]).kind)
      test_collect_kinds(toks, i + 1, acc)

-- fn test_find_kind
test_find_kind toks k i =
  match i >= glyph_array_len(toks)
    true -> 0 - 1
    _ -> match (toks[i]).kind == k
      true -> i
      _ -> test_find_kind(toks, k, i + 1)

-- fn test_find_stmt_kind
test_find_stmt_kind stmts kind i =
  match i >= glyph_array_len(stmts)
    true -> 0 - 1
    _ -> match (stmts[i]).skind == kind
      true -> i
      _ -> test_find_stmt_kind(stmts, kind, i + 1)

-- fn tk_and
tk_and = 53

-- fn tk_arrow
tk_arrow = 61

-- fn tk_as
tk_as = 32

-- fn tk_backslash
tk_backslash = 62

-- fn tk_bang
tk_bang = 55

-- fn tk_bang_eq
tk_bang_eq = 48

-- fn tk_bitand
tk_bitand = 56

-- fn tk_bitor
tk_bitor = 57

-- fn tk_bitxor
tk_bitxor = 65

-- fn tk_colon
tk_colon = 80

-- fn tk_colon_eq
tk_colon_eq = 46

-- fn tk_comma
tk_comma = 81

-- fn tk_const
tk_const = 27

-- fn tk_dedent
tk_dedent = 91

-- fn tk_dot
tk_dot = 82

-- fn tk_dot_dot
tk_dot_dot = 63

-- fn tk_else
tk_else = 21

-- fn tk_eof
tk_eof = 99

-- fn tk_eq
tk_eq = 45

-- fn tk_eq_eq
tk_eq_eq = 47

-- fn tk_error
tk_error = 100

-- fn tk_extern
tk_extern = 28

-- fn tk_float
tk_float = 2

-- fn tk_for
tk_for = 23

-- fn tk_gt
tk_gt = 50

-- fn tk_gt_eq
tk_gt_eq = 52

-- fn tk_gt_gt
tk_gt_gt = 59

-- fn tk_ident
tk_ident = 10

-- fn tk_impl
tk_impl = 26

-- fn tk_in
tk_in = 24

-- fn tk_indent
tk_indent = 90

-- fn tk_int
tk_int = 1

-- fn tk_lbrace
tk_lbrace = 74

-- fn tk_lbracket
tk_lbracket = 72

-- fn tk_lparen
tk_lparen = 70

-- fn tk_lt
tk_lt = 49

-- fn tk_lt_eq
tk_lt_eq = 51

-- fn tk_match
tk_match = 22

-- fn tk_minus
tk_minus = 41

-- fn tk_newline
tk_newline = 92

-- fn tk_or
tk_or = 54

-- fn tk_percent
tk_percent = 44

-- fn tk_pipe
tk_pipe = 64


-- fn tk_pipe_gt
tk_pipe_gt = 58

-- fn tk_plus
tk_plus = 40

-- fn tk_question
tk_question = 60

-- fn tk_rbrace
tk_rbrace = 75

-- fn tk_rbracket
tk_rbracket = 73

-- fn tk_rparen
tk_rparen = 71

-- fn tk_shl
tk_shl = 66

-- fn tk_shr
tk_shr = 67

-- fn tk_slash
tk_slash = 43

-- fn tk_star
tk_star = 42

-- fn tk_str
tk_str = 3

-- fn tk_str_interp_end
tk_str_interp_end = 5

-- fn tk_str_interp_start
tk_str_interp_start = 4

-- fn tk_test
tk_test = 31

-- fn tk_trait
tk_trait = 25

-- fn tk_type_ident
tk_type_ident = 11

-- fn tm_branch
tm_branch = 2

-- fn tm_goto
tm_goto = 1

-- fn tm_return
tm_return = 4

-- fn tm_switch
tm_switch = 3

-- fn tm_unreachable
tm_unreachable = 5

-- fn tmap_init
tmap_init n = tmap_init_loop([], n, 0)

tmap_init_loop acc n i =
  match i >= n
    true -> acc
    _ ->
      glyph_array_push(acc, 0 - 1)
      tmap_init_loop(acc, n, i + 1)

-- fn tmap_init_loop
tmap_init_loop acc n i =
  match i >= n
    true -> acc
    _ ->
      glyph_array_push(acc, 0 - 1)
      tmap_init_loop(acc, n, i + 1)

-- fn tmap_resolve_tag
tmap_resolve_tag eng ti =
  match ti < 0
    true -> 0 - 1
    _ ->
      pool = eng.ty_pool
      match ti >= glyph_array_len(pool)
        true -> 0 - 1
        _ ->
          resolved = subst_resolve(eng, ti)
          match resolved < 0
            true -> 0 - 1
            _ ->
              match resolved >= glyph_array_len(pool)
                true -> 0 - 1
                _ -> (pool[resolved]).tag

-- fn tok_interp_expr
tok_interp_expr src pos len line tokens depth =
  match pos < len
    true ->
      ch = glyph_str_char_at(src, pos)
      match ch == 125
        true ->
          match depth == 0
            true -> pos + 1
            _ ->
              glyph_array_push(tokens, mk_token(tk_rbrace(), pos, pos + 1, line))
              tok_interp_expr(src, pos + 1, len, line, tokens, depth - 1)
        _ -> match ch == 123
          true ->
            glyph_array_push(tokens, mk_token(tk_lbrace(), pos, pos + 1, line))
            tok_interp_expr(src, pos + 1, len, line, tokens, depth + 1)
          _ ->
            r = tok_one(src, len, pos, line, 0, tokens, ch)
            rpos = r / 100000000
            rrest = r - rpos * 100000000
            rline = rrest / 1000000
            tok_interp_expr(src, rpos, len, rline, tokens, depth)
    _ -> pos

-- fn tok_loop
tok_loop src len pos line bdepth indent_stack tokens =
  pos2 = skip_ws(src, pos)
  match pos2 < len
    true ->
      c = glyph_str_char_at(src, pos2)
      match c == 10
        true ->
          pl = skip_blanks(src, pos2 + 1, line + 1, len)
          pos3 = pl / 1000000
          line2 = pl - pos3 * 1000000
          match pos3 < len
            true ->
              match bdepth == 0
                true ->
                  tlen = glyph_array_len(tokens)
                  last_kind = match tlen > 0
                    true -> (tokens[tlen - 1]).kind
                    _ -> 0 - 1
                  match last_kind != tk_newline()
                    true ->
                      match last_kind != tk_indent()
                        true -> glyph_array_push(tokens, mk_token(tk_newline(), pos3, pos3, line2))
                        _ -> 0
                    _ -> 0
                  mi = measure_indent(src, pos3)
                  new_indent = mi / 1000000
                  pos4 = mi - new_indent * 1000000
                  cur_indent = indent_top(indent_stack)
                  match new_indent > cur_indent
                    true ->
                      glyph_array_push(indent_stack, new_indent)
                      glyph_array_push(tokens, mk_token(tk_indent(), pos4, pos4, line2))
                      tok_loop(src, len, pos4, line2, bdepth, indent_stack, tokens)
                    _ ->
                      emit_dedents(indent_stack, tokens, new_indent, pos4, line2)
                      tok_loop(src, len, pos4, line2, bdepth, indent_stack, tokens)
                _ -> tok_loop(src, len, pos3, line2, bdepth, indent_stack, tokens)
            _ ->
              flush_dedents(indent_stack, tokens, pos3, line2)
              glyph_array_push(tokens, mk_token(tk_eof(), pos3, pos3, line2))
              tokens
        _ ->
          r = tok_one(src, len, pos2, line, bdepth, tokens, c)
          rpos = r / 100000000
          rrest = r - rpos * 100000000
          rline = rrest / 1000000
          rbd = rrest - rline * 1000000
          tok_loop(src, len, rpos, rline, rbd, indent_stack, tokens)
    _ ->
      flush_dedents(indent_stack, tokens, pos2, line)
      glyph_array_push(tokens, mk_token(tk_eof(), pos2, pos2, line))
      tokens

-- fn tok_one
tok_one src len pos line bdepth tokens c =
  match c
    45 ->
      c2 = peek(src, pos + 1, len)
      match c2
        45 -> tok_pack(skip_comment(src, pos), line, bdepth)
        62 ->
          glyph_array_push(tokens, mk_token(tk_arrow(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        _ ->
          glyph_array_push(tokens, mk_token(tk_minus(), pos, pos + 1, line))
          tok_pack(pos + 1, line, bdepth)
    40 ->
      glyph_array_push(tokens, mk_token(tk_lparen(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdepth + 1)
    41 ->
      glyph_array_push(tokens, mk_token(tk_rparen(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdec(bdepth))
    91 ->
      glyph_array_push(tokens, mk_token(tk_lbracket(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdepth + 1)
    93 ->
      glyph_array_push(tokens, mk_token(tk_rbracket(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdec(bdepth))
    123 ->
      glyph_array_push(tokens, mk_token(tk_lbrace(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdepth + 1)
    125 ->
      glyph_array_push(tokens, mk_token(tk_rbrace(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdec(bdepth))
    44 ->
      glyph_array_push(tokens, mk_token(tk_comma(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdepth)
    92 ->
      glyph_array_push(tokens, mk_token(tk_backslash(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdepth)
    58 ->
      match peek(src, pos + 1, len) == 61
        true ->
          glyph_array_push(tokens, mk_token(tk_colon_eq(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        _ ->
          glyph_array_push(tokens, mk_token(tk_colon(), pos, pos + 1, line))
          tok_pack(pos + 1, line, bdepth)
    46 ->
      match peek(src, pos + 1, len) == 46
        true ->
          glyph_array_push(tokens, mk_token(tk_dot_dot(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        _ ->
          glyph_array_push(tokens, mk_token(tk_dot(), pos, pos + 1, line))
          tok_pack(pos + 1, line, bdepth)
    _ -> tok_one2(src, len, pos, line, bdepth, tokens, c)

-- fn tok_one2
tok_one2 src len pos line bdepth tokens c =
  match c
    61 ->
      match peek(src, pos + 1, len) == 61
        true ->
          glyph_array_push(tokens, mk_token(tk_eq_eq(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        _ ->
          glyph_array_push(tokens, mk_token(tk_eq(), pos, pos + 1, line))
          tok_pack(pos + 1, line, bdepth)
    33 ->
      match peek(src, pos + 1, len) == 61
        true ->
          glyph_array_push(tokens, mk_token(tk_bang_eq(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        _ ->
          glyph_array_push(tokens, mk_token(tk_bang(), pos, pos + 1, line))
          tok_pack(pos + 1, line, bdepth)
    60 ->
      match peek(src, pos + 1, len) == 61
        true ->
          glyph_array_push(tokens, mk_token(tk_lt_eq(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        _ ->
          glyph_array_push(tokens, mk_token(tk_lt(), pos, pos + 1, line))
          tok_pack(pos + 1, line, bdepth)
    62 ->
      c2 = peek(src, pos + 1, len)
      match c2
        62 ->
          glyph_array_push(tokens, mk_token(tk_gt_gt(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        61 ->
          glyph_array_push(tokens, mk_token(tk_gt_eq(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        _ ->
          glyph_array_push(tokens, mk_token(tk_gt(), pos, pos + 1, line))
          tok_pack(pos + 1, line, bdepth)
    124 ->
      c2 = peek(src, pos + 1, len)
      match c2
        62 ->
          glyph_array_push(tokens, mk_token(tk_pipe_gt(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        124 ->
          glyph_array_push(tokens, mk_token(tk_or(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        _ ->
          glyph_array_push(tokens, mk_token(tk_pipe(), pos, pos + 1, line))
          tok_pack(pos + 1, line, bdepth)
    _ -> tok_one3(src, len, pos, line, bdepth, tokens, c)

-- fn tok_one3
tok_one3 src len pos line bdepth tokens c =
  match c
    38 ->
      match peek(src, pos + 1, len) == 38
        true ->
          glyph_array_push(tokens, mk_token(tk_and(), pos, pos + 2, line))
          tok_pack(pos + 2, line, bdepth)
        _ -> tok_pack(pos + 1, line, bdepth)
    43 ->
      glyph_array_push(tokens, mk_token(tk_plus(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdepth)
    42 ->
      glyph_array_push(tokens, mk_token(tk_star(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdepth)
    47 ->
      glyph_array_push(tokens, mk_token(tk_slash(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdepth)
    37 ->
      glyph_array_push(tokens, mk_token(tk_percent(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdepth)
    63 ->
      glyph_array_push(tokens, mk_token(tk_question(), pos, pos + 1, line))
      tok_pack(pos + 1, line, bdepth)
    34 ->
      match scan_str_has_interp(src, pos + 1)
        true ->
          glyph_array_push(tokens, mk_token(tk_str_interp_start(), pos, pos + 1, line))
          end = tok_str_interp_loop(src, pos + 1, len, line, tokens, pos + 1)
          tok_pack(end, line, bdepth)
        _ ->
          end = scan_string_end(src, pos + 1)
          glyph_array_push(tokens, mk_token(tk_str(), pos + 1, end - 1, line))
          tok_pack(end, line, bdepth)
    _ -> tok_one4(src, len, pos, line, bdepth, tokens, c)

-- fn tok_one4
tok_one4 src len pos line bdepth tokens c =
  match is_digit(c)
    true ->
      end = scan_number_end(src, pos)
      match peek(src, end, len) == 46
        true ->
          match is_digit(peek(src, end + 1, len))
            true ->
              fend = scan_number_end(src, end + 1)
              glyph_array_push(tokens, mk_token(tk_float(), pos, fend, line))
              tok_pack(fend, line, bdepth)
            _ ->
              glyph_array_push(tokens, mk_token(tk_int(), pos, end, line))
              tok_pack(end, line, bdepth)
        _ ->
          glyph_array_push(tokens, mk_token(tk_int(), pos, end, line))
          tok_pack(end, line, bdepth)
    _ ->
      match c == 114
        true ->
          match peek(src, pos + 1, len) == 34
            true ->
              match peek(src, pos + 2, len) == 34
                true ->
                  match peek(src, pos + 3, len) == 34
                    true ->
                      end = scan_raw_triple_end(src, pos + 4)
                      glyph_array_push(tokens, mk_token(tk_str(), pos + 4, end - 3, line))
                      tok_pack(end, line, bdepth)
                    _ ->
                      end = scan_raw_string_end(src, pos + 2)
                      glyph_array_push(tokens, mk_token(tk_str(), pos + 2, end - 1, line))
                      tok_pack(end, line, bdepth)
                _ ->
                  end = scan_raw_string_end(src, pos + 2)
                  glyph_array_push(tokens, mk_token(tk_str(), pos + 2, end - 1, line))
                  tok_pack(end, line, bdepth)
            _ ->
              end = scan_ident_end(src, pos)
              text = glyph_str_slice(src, pos, end)
              kw = keyword_kind(text)
              match kw > 0
                true ->
                  glyph_array_push(tokens, mk_token(kw, pos, end, line))
                  tok_pack(end, line, bdepth)
                _ ->
                  glyph_array_push(tokens, mk_token(tk_ident(), pos, end, line))
                  tok_pack(end, line, bdepth)
        _ ->
          match is_alpha(c) || c == 95
            true ->
              end = scan_ident_end(src, pos)
              text = glyph_str_slice(src, pos, end)
              kw = keyword_kind(text)
              match kw > 0
                true ->
                  glyph_array_push(tokens, mk_token(kw, pos, end, line))
                  tok_pack(end, line, bdepth)
                _ ->
                  match is_upper(c)
                    true ->
                      glyph_array_push(tokens, mk_token(tk_type_ident(), pos, end, line))
                      tok_pack(end, line, bdepth)
                    _ ->
                      glyph_array_push(tokens, mk_token(tk_ident(), pos, end, line))
                      tok_pack(end, line, bdepth)
            _ -> tok_pack(pos + 1, line, bdepth)


-- fn tok_pack
tok_pack pos line bd = pos * 100000000 + line * 1000000 + bd

-- fn tok_str_interp_loop
tok_str_interp_loop src pos len line tokens start =
  match pos < len
    true ->
      ch = glyph_str_char_at(src, pos)
      match ch
        34 ->
          match pos > start
            true -> glyph_array_push(tokens, mk_token(tk_str(), start, pos, line))
            _ -> 0
          glyph_array_push(tokens, mk_token(tk_str_interp_end(), pos, pos + 1, line))
          pos + 1
        92 ->
          match pos + 1 < len
            true ->
              nch = glyph_str_char_at(src, pos + 1)
              match nch == 123
                true -> tok_str_interp_loop(src, pos + 2, len, line, tokens, start)
                _ -> tok_str_interp_loop(src, pos + 2, len, line, tokens, start)
            _ -> tok_str_interp_loop(src, pos + 1, len, line, tokens, start)
        123 ->
          match pos > start
            true -> glyph_array_push(tokens, mk_token(tk_str(), start, pos, line))
            _ -> 0
          after = tok_interp_expr(src, pos + 1, len, line, tokens, 0)
          tok_str_interp_loop(src, after, len, line, tokens, after)
        _ -> tok_str_interp_loop(src, pos + 1, len, line, tokens, start)
    _ ->
      glyph_array_push(tokens, mk_token(tk_str_interp_end(), pos, pos, line))
      pos

-- fn tok_text
tok_text src t = glyph_str_slice(src, t.start, t.end)

-- fn tokenize
tokenize src =
  len = glyph_str_len(src)
  tokens = []
  indent_stack = [0]
  mi = measure_indent(src, 0)
  indent = mi / 1000000
  pos = mi - indent * 1000000
  match indent > 0
    true ->
      glyph_array_push(indent_stack, indent)
      glyph_array_push(tokens, mk_token(tk_indent(), pos, pos, 1))
      tok_loop(src, len, pos, 1, 0, indent_stack, tokens)
    _ -> tok_loop(src, len, pos, 1, 0, indent_stack, tokens)

-- fn track_call_type
track_call_type ctx dest callee =
  match callee.okind == ok_func_ref()
    true -> match is_str_ret_fn(callee.ostr)
      true -> mir_set_lt(ctx, dest, 4)
      _ -> 0
    _ -> 0


-- fn ty_array
ty_array = 11

-- fn ty_bool
ty_bool = 5

-- fn ty_contains_var
ty_contains_var eng ti var =
  w = subst_walk(eng, ti)
  node = pool_get(eng, w)
  match node.tag == ty_var()
    true -> node.n1 == var
    _ -> match node.tag == ty_fn()
      true ->
        match ty_contains_var(eng, node.n1, var)
          true -> true
          _ -> ty_contains_var(eng, node.n2, var)
      _ -> match node.tag == ty_array()
        true -> ty_contains_var(eng, node.n1, var)
        _ -> match node.tag == ty_opt()
          true -> ty_contains_var(eng, node.n1, var)
          _ -> match node.tag == ty_record()
            true -> ty_fields_contain_var(eng, node.ns, 0, var)
            _ -> false


-- fn ty_error
ty_error = 99

-- fn ty_field
ty_field = 20

-- fn ty_fields_contain_var
ty_fields_contain_var eng field_ns i var =
  match i >= glyph_array_len(field_ns)
    true -> false
    _ ->
      fi = field_ns[i]
      fnode = pool_get(eng, fi)
      _ = fnode.tag
      match ty_contains_var(eng, fnode.n1, var)
        true -> true
        _ -> ty_fields_contain_var(eng, field_ns, i + 1, var)


-- fn ty_float
ty_float = 3

-- fn ty_fn
ty_fn = 10

-- fn ty_forall
ty_forall = 16

-- fn ty_int
ty_int = 1

-- fn ty_named
ty_named = 17

-- fn ty_never
ty_never = 7

-- fn ty_opt
ty_opt = 12

-- fn ty_push
ty_push pool node =
  glyph_array_push(pool, node)
  glyph_array_len(pool) - 1

-- fn ty_record
ty_record = 14

-- fn ty_res
ty_res = 13

-- fn ty_str
ty_str = 4

-- fn ty_tag
ty_tag pool idx = pool[idx].tag

-- fn ty_tuple
ty_tuple = 18

-- fn ty_uint
ty_uint = 2

-- fn ty_var
ty_var = 15

-- fn ty_void
ty_void = 6

-- fn typecheck_fn
typecheck_fn ast fn_idx =
  eng = mk_engine()
  register_builtins(eng)
  ty = infer_fn_def(eng, ast, fn_idx)
  resolved = subst_resolve(eng, ty)
  resolved

-- fn types_equal
types_equal a b =
  match glyph_array_len(a) == glyph_array_len(b)
    true -> types_equal_loop(a, b, 0)
    _ -> 0

-- fn types_equal_loop
types_equal_loop a b i =
  match i >= glyph_array_len(a)
    true -> 1
    _ -> match glyph_str_eq(a[i], b[i])
      true -> types_equal_loop(a, b, i + 1)
      _ -> 0

-- fn unaryop_type
unaryop_type eng op operand =
  t = ty_tag(eng.ty_pool, operand)
  match op == op_neg()
    true ->
      match t == ty_int()
        true -> mk_tint(eng.ty_pool)
        _ -> match t == ty_float()
          true -> mk_ty_prim(ty_float(), eng.ty_pool)
          _ -> 0 - 1
    _ -> match op == op_not()
      true -> mk_tbool(eng.ty_pool)
      _ -> 0 - 1

-- fn unify
unify eng a b =
  a1 = subst_walk(eng, a)
  b1 = subst_walk(eng, b)
  match a1 == b1
    true -> 0
    _ ->
      pool_len = glyph_array_len(eng.ty_pool)
      match a1 >= pool_len
        true -> 0
        _ -> match b1 >= pool_len
          true -> 0
          _ -> match a1 < 0
            true -> 0
            _ -> match b1 < 0
              true -> 0
              _ ->
                na = pool_get(eng, a1)
                nb = pool_get(eng, b1)
                match na.tag == ty_error()
                  true -> 0
                  _ -> match nb.tag == ty_error()
                    true -> 0
                    _ -> match na.tag == ty_var()
                      true -> subst_bind(eng, na.n1, b1)
                      _ -> match nb.tag == ty_var()
                        true -> subst_bind(eng, nb.n1, a1)
                        _ -> unify_tags(eng, na, nb, a1, b1)


-- fn unify_array_elems
unify_array_elems eng ast elems first_ty i =
  match i >= glyph_array_len(elems)
    true -> 0
    _ ->
      et = infer_expr(eng, ast, elems[i])
      unify(eng, first_ty, et)
      unify_array_elems(eng, ast, elems, first_ty, i + 1)

-- fn unify_fields_against
unify_fields_against eng fs1 fs2 rest_var i =
  match i >= glyph_array_len(fs1)
    true -> 0
    _ ->
      fi1 = fs1[i]
      pool_len = glyph_array_len(eng.ty_pool)
      match fi1 >= pool_len
        true -> 0
        _ -> match fi1 < 0
          true -> 0
          _ ->
            f1 = pool_get(eng, fi1)
            _ = f1.tag
            found = find_field(eng, fs2, f1.sval, 0)
            match found < 0
              true ->
                match rest_var < 0
                  true -> 0 - 1
                  _ -> unify_fields_against(eng, fs1, fs2, rest_var, i + 1)
              _ ->
                match found >= glyph_array_len(eng.ty_pool)
                  true -> 0
                  _ ->
                    f2 = pool_get(eng, found)
                    _ = f2.tag
                    r = unify(eng, f1.n1, f2.n1)
                    match r < 0
                      true -> r
                      _ -> unify_fields_against(eng, fs1, fs2, rest_var, i + 1)


-- fn unify_ns
unify_ns eng ns1 ns2 i =
  match i >= glyph_array_len(ns1)
    true -> match i >= glyph_array_len(ns2)
      true -> 0
      _ -> 0 - 1
    _ -> match i >= glyph_array_len(ns2)
      true -> 0 - 1
      _ ->
        r = unify(eng, ns1[i], ns2[i])
        match r < 0
          true -> r
          _ -> unify_ns(eng, ns1, ns2, i + 1)

-- fn unify_records
unify_records eng na nb =
  _ = na.tag
  _ = nb.tag
  r1 = unify_fields_against(eng, na.ns, nb.ns, nb.n1, 0)
  match r1 < 0
    true -> r1
    _ -> unify_fields_against(eng, nb.ns, na.ns, na.n1, 0)


-- fn unify_tags
unify_tags eng na nb a1 b1 =
  match na.tag == nb.tag
    true ->
      match na.tag == ty_fn()
        true ->
          r1 = unify(eng, na.n1, nb.n1)
          match r1 < 0
            true -> r1
            _ -> unify(eng, na.n2, nb.n2)
        _ -> match na.tag == ty_array()
          true -> unify(eng, na.n1, nb.n1)
          _ -> match na.tag == ty_opt()
            true -> unify(eng, na.n1, nb.n1)
            _ -> match na.tag == ty_res()
              true -> unify(eng, na.n1, nb.n1)
              _ -> match na.tag == ty_record()
                true -> unify_records(eng, na, nb)
                _ -> match na.tag == ty_tuple()
                  true -> unify_ns(eng, na.ns, nb.ns, 0)
                  _ -> 0
    _ -> 0 - 1

-- fn validate_def
validate_def body =
  tokens = tokenize(body)
  ast = []
  r = parse_fn_def(body, tokens, 0, ast)
  match is_err(r)
    true -> {vr_ok: 0, vr_msg: r.pr_err, vr_pos: r.pos, vr_tokens: tokens}
    _ -> {vr_ok: 1, vr_msg: "", vr_pos: 0, vr_tokens: tokens}

-- fn var_in_list
var_in_list var_id list i =
  match i >= glyph_array_len(list)
    true -> 0
    _ ->
      match list[i] == var_id
        true -> 1
        _ -> var_in_list(var_id, list, i + 1)

-- fn variant_discriminant
variant_discriminant name =
  match glyph_str_eq(name, "None")
    true -> 0
    _ -> match glyph_str_eq(name, "Some")
      true -> 1
      _ -> match glyph_str_eq(name, "Ok")
        true -> 0
        _ -> match glyph_str_eq(name, "Err")
          true -> 1
          _ -> match glyph_str_eq(name, "Left")
            true -> 0
            _ -> match glyph_str_eq(name, "Right")
              true -> 1
              _ -> 0

-- fn walk_free_vars
walk_free_vars ctx ast ni bound seen result =
  node = ast[ni]
  k = node.kind
  match k
    _ ? k == ex_ident() ->
      name = node.sval
      match fv_is_bound(name, bound, 0) == 1
        true -> 0
        _ -> match fv_is_seen(name, seen, 0) == 1
          true -> 0
          _ ->
            local_id = mir_lookup_var(ctx, name)
            match local_id >= 0
              true ->
                glyph_array_push(seen, name)
                glyph_array_push(result, local_id)
                glyph_array_push(result, name)
                0
              _ -> 0
    _ ? k == ex_int_lit() || k == ex_str_lit() || k == ex_bool_lit() -> 0
    _ -> walk_free_vars2(ctx, ast, node, k, bound, seen, result)

-- fn walk_free_vars2
walk_free_vars2 ctx ast node k bound seen result =
  match k
    _ ? k == ex_binary() || k == ex_index() ->
      walk_free_vars(ctx, ast, node.n1, bound, seen, result)
      walk_free_vars(ctx, ast, node.n2, bound, seen, result)
    _ ? k == ex_unary() || k == ex_field_access() -> walk_free_vars(ctx, ast, node.n1, bound, seen, result)
    _ ? k == ex_call() ->
      walk_free_vars(ctx, ast, node.n1, bound, seen, result)
      walk_fv_list(ctx, ast, node.ns, 0, bound, seen, result)
    _ -> walk_free_vars3(ctx, ast, node, k, bound, seen, result)

-- fn walk_free_vars3
walk_free_vars3 ctx ast node k bound seen result =
  match k
    _ ? k == ex_lambda() ->
      inner = []
      cfv_add_params(inner, ast, node.ns, 0)
      wfv_add_all(inner, bound, 0)
      walk_free_vars(ctx, ast, node.n1, inner, seen, result)
    _ ? k == ex_block() -> walk_fv_block(ctx, ast, node.ns, 0, bound, seen, result)
    _ ? k == ex_match() ->
      walk_free_vars(ctx, ast, node.n1, bound, seen, result)
      walk_fv_list(ctx, ast, node.ns, 0, bound, seen, result)
    _ ? k == ex_pipe() || k == ex_compose() ->
      walk_free_vars(ctx, ast, node.n1, bound, seen, result)
      walk_free_vars(ctx, ast, node.n2, bound, seen, result)
    _ -> walk_free_vars4(ctx, ast, node, k, bound, seen, result)

-- fn walk_free_vars4
walk_free_vars4 ctx ast node k bound seen result =
  match k
    _ ? k == ex_propagate() || k == ex_unwrap() -> walk_free_vars(ctx, ast, node.n1, bound, seen, result)
    _ ? k == ex_array() || k == ex_record() || k == ex_str_interp() -> walk_fv_list(ctx, ast, node.ns, 0, bound, seen, result)
    _ -> 0

-- fn walk_fv_block
walk_fv_block ctx ast stmts i bound seen result =
  match i >= glyph_array_len(stmts)
    true -> 0
    _ ->
      snode = ast[stmts[i]]
      sk = snode.kind
      match sk == st_let()
        true ->
          walk_free_vars(ctx, ast, snode.n1, bound, seen, result)
          glyph_array_push(bound, snode.sval)
          walk_fv_block(ctx, ast, stmts, i + 1, bound, seen, result)
        _ -> match sk == st_assign()
          true ->
            walk_free_vars(ctx, ast, snode.n1, bound, seen, result)
            walk_fv_block(ctx, ast, stmts, i + 1, bound, seen, result)
          _ ->
            walk_free_vars(ctx, ast, snode.n1, bound, seen, result)
            walk_fv_block(ctx, ast, stmts, i + 1, bound, seen, result)


-- fn walk_fv_list
walk_fv_list ctx ast ns i bound seen result =
  match i >= glyph_array_len(ns)
    true -> 0
    _ ->
      walk_free_vars(ctx, ast, ns[i], bound, seen, result)
      walk_fv_list(ctx, ast, ns, i + 1, bound, seen, result)


-- fn wfv_add_all
wfv_add_all dst src i =
  match i >= glyph_array_len(src)
    true -> 0
    _ ->
      glyph_array_push(dst, src[i])
      wfv_add_all(dst, src, i + 1)


-- test test_2d_arrays
test_2d_arrays u =
  r = make_empty_2d(3, 0)
  assert_eq(glyph_array_len(r), 3)
  assert_eq(glyph_array_len(r[0]), 0)
  glyph_array_push(r[0], "a")
  assert_eq(glyph_array_len(r[0]), 1)
  assert_eq(glyph_array_len(r[1]), 0)
  0


-- test test_alpha_rank
test_alpha_rank u =
  names = ["apple", "banana", "cherry"]
  assert_eq(alpha_rank(names, "banana"), 1)
  assert_eq(alpha_rank(names, "cherry"), 2)
  assert_eq(alpha_rank(names, "apple"), 0)
  assert_eq(alpha_rank(names, "zzz"), 3)
  0


-- test test_bitwise_ops
test_bitwise_ops u =
  assert_eq(5 bitand 3, 1)
  assert_eq(5 bitor 3, 7)
  assert_eq(5 bitxor 3, 6)
  assert_eq(1 shl 3, 8)
  assert_eq(16 shr 2, 4)
  assert_eq(255 bitand 15, 15)
  assert_eq(255 bitand 240, 240)
  0

-- test test_build_type_reg
test_build_type_reg u =
  mir = compile_fn("f x = x + 1", [])
  mirs = [mir]
  reg = build_type_reg(mirs)
  assert(glyph_array_len(reg) >= 0)
  0


-- test test_build_za
test_build_za u =
  srcs = ["f = 42", "g = 7"]
  parsed = parse_all_fns(srcs, 0)
  za = build_za_fns(parsed, 0, [])
  assert_eq(glyph_array_len(za), 2)
  srcs2 = ["h x = x"]
  parsed2 = parse_all_fns(srcs2, 0)
  za2 = build_za_fns(parsed2, 0, [])
  assert_eq(glyph_array_len(za2), 0)
  0


-- test test_c_layout_float
test_c_layout_float u =
  p = {fx: 1.5, fy: 2.5}
  xv = float_to_int(p.fx)
  yv = float_to_int(p.fy)
  assert_eq(xv, 1)
  assert_eq(yv, 2)
  sum = p.fx + p.fy
  assert_eq(float_to_int(sum), 4)
  0


-- test test_c_layout_struct
test_c_layout_struct u =
  p = {x: 100, y: 200}
  assert_eq(p.x, 100)
  assert_eq(p.y, 200)
  p2 = {x: 65535, y: 0 - 1}
  assert_eq(p2.x, 65535)
  0

-- test test_cg_binop_str
test_cg_binop_str u =
  assert_str_eq(cg_binop_str(mir_add()), " + ")
  assert_str_eq(cg_binop_str(mir_sub()), " - ")
  assert_str_eq(cg_binop_str(mir_mul()), " * ")
  assert_str_eq(cg_binop_str(mir_div()), " / ")
  assert_str_eq(cg_binop_str(mir_mod()), " % ")
  assert_str_eq(cg_binop_str(mir_eq()), " == ")
  assert_str_eq(cg_binop_str(mir_neq()), " != ")
  assert_str_eq(cg_binop_str(mir_lt()), " < ")
  assert_str_eq(cg_binop_str(mir_gt()), " > ")
  0

-- test test_cg_coverage
test_cg_coverage u =
  r = cg_runtime_coverage("test.cover", ["fn1", "fn2"])
  assert(str_contains(r, "GLYPH_COVERAGE"))
  assert(str_contains(r, "_glyph_cov_write"))
  assert(str_contains(r, "fn1"))
  0


-- test test_cg_escape_str
test_cg_escape_str u =
  result = cg_escape_str("hello")
  assert_str_eq(result, "hello")
  r2 = cg_escape_str("a\\b")
  assert_str_eq(r2, "a\\\\b")
  r3 = cg_escape_str("")
  assert_str_eq(r3, "")
  0

-- test test_cg_fn_name
test_cg_fn_name u =
  assert_str_eq(cg_fn_name("println"), "glyph_println")
  assert_str_eq(cg_fn_name("str_len"), "glyph_str_len")
  assert_str_eq(cg_fn_name("my_func"), "my_func")
  assert_str_eq(cg_fn_name("foo"), "foo")
  0

-- test test_cg_leaves
test_cg_leaves u =
  assert_str_eq(cg_lbrace(), "\{")
  assert_str_eq(cg_rbrace(), "}")
  assert_str_eq(cg_label(0), "bb_0")
  assert_str_eq(cg_label(5), "bb_5")
  assert_str_eq(cg_local(0), "_0")
  assert_str_eq(cg_local(3), "_3")
  assert_str_eq(cg_unop_str(mir_neg()), "-")
  assert_str_eq(cg_unop_str(mir_not()), "!")
  assert_str_eq(cg_binop_str2(mir_bitand()), " & ")
  assert_str_eq(cg_binop_str2(mir_bitor()), " | ")
  assert_str_eq(cg_binop_str2(mir_bitxor()), " ^ ")
  assert_str_eq(cg_binop_str2(mir_shl()), " << ")
  assert_str_eq(cg_binop_str2(mir_shr()), " >> ")
  sp = cg_sig_params(2, 0)
  assert(glyph_str_len(sp) > 0)
  0


-- test test_cg_operand
test_cg_operand u =
  r1 = cg_operand(mk_op_int(42))
  assert_str_eq(r1, "(GVal)42")
  r2 = cg_operand(mk_op_bool(0))
  assert_str_eq(r2, "0")
  r3 = cg_operand(mk_op_bool(1))
  assert_str_eq(r3, "1")
  r4 = cg_operand(mk_op_unit())
  assert_str_eq(r4, "0")
  0

-- test test_cg_runtime
test_cg_runtime u =
  r1 = cg_runtime_raw()
  assert(str_contains(r1, "glyph_raw_set"))
  r2 = cg_runtime_extra()
  assert(str_contains(r2, "glyph_print"))
  assert(str_contains(r2, "glyph_array_new"))
  r3 = cg_runtime_args()
  assert(str_contains(r3, "glyph_set_args"))
  r4 = cg_runtime_float()
  assert(str_contains(r4, "_glyph_i2f"))
  r5 = cg_runtime_sb()
  assert(str_contains(r5, "glyph_sb_new"))
  r6 = cg_runtime_result()
  assert(str_contains(r6, "glyph_ok"))
  r7 = cg_runtime_io()
  assert(str_contains(r7, "glyph_read_file"))
  r8 = cg_test_runtime()
  assert(str_contains(r8, "_test_failed"))
  r9 = cg_main_wrapper()
  assert(str_contains(r9, "glyph_main"))
  0


-- test test_cg_runtime2
test_cg_runtime2 u =
  r1 = cg_runtime_c()
  assert(str_contains(r1, "glyph_alloc"))
  assert(str_contains(r1, "glyph_str_concat"))
  assert(str_contains(r1, "glyph_println"))
  r2 = cg_preamble()
  assert(str_contains(r2, "typedef intptr_t GVal"))
  assert(str_contains(r2, "glyph_alloc"))
  r3 = cg_test_preamble_extra()
  assert(str_contains(r3, "glyph_assert"))
  0


-- test test_cg_sqlite
test_cg_sqlite u =
  r = cg_runtime_sqlite()
  assert(str_contains(r, "sqlite3"))
  assert(str_contains(r, "glyph_db_open"))
  assert(str_contains(r, "glyph_db_close"))
  0


-- test test_check_errors
test_check_errors u =
  srcs = ["f x = x + 1"]
  parsed = parse_all_fns(srcs, 0)
  errs = check_parse_errors(parsed)
  assert_eq(errs, 0)
  bad_srcs = ["f = "]
  bad_parsed = parse_all_fns(bad_srcs, 0)
  bad_errs = check_parse_errors(bad_parsed)
  assert(bad_errs >= 0)
  0


-- test test_check_tok
test_check_tok u =
  src = "f x = x"
  toks = tokenize(src)
  assert_eq(check_tok(toks, 0, tk_ident()), 1)
  assert_eq(check_tok(toks, 2, tk_eq()), 1)
  assert_eq(check_tok(toks, 0, tk_eq()), 0)
  0


-- test test_closure_as_arg
test_closure_as_arg u =
  result = apply_fn(\x -> x * 3, 7)
  assert_eq(result, 21)


-- test test_closure_basic
test_closure_basic u =
  add1 = \x -> x + 1
  result = add1(10)
  assert_eq(result, 11)


-- test test_closure_capture
test_closure_capture u =
  base = 100
  add_base = \x -> base + x
  result = add_base(42)
  assert_eq(result, 142)


-- test test_closure_lower
test_closure_lower u =
  mir = compile_fn("f x = \\y -> x + y", [])
  stmts = mir.fn_blocks_stmts[0]
  found = test_find_stmt_kind(stmts, rv_make_closure(), 0)
  assert(found >= 0)
  bc = mir_block_count(mir)
  assert(bc >= 1)
  lc = mir_local_count(mir)
  assert(lc >= 2)
  0


-- test test_closure_mir
test_closure_mir u =
  src = "add1 x =\n  base = 10\n  f = \\y -> base + y\n  f(x)"
  mir = compile_fn(src, [])
  n_subs = glyph_array_len(mir.fn_subs)
  assert_eq(n_subs, 1)


-- test test_closure_multi_cap
test_closure_multi_cap u =
  a = 10
  b = 20
  f = \x -> a + b + x
  result = f(3)
  assert_eq(result, 33)


-- test test_closure_nested
test_closure_nested u =
  x = 5
  outer = \y -> \z -> x + y + z
  inner = outer(10)
  result = inner(20)
  assert_eq(result, 35)


-- test test_collect_libs
test_collect_libs u =
  e1 = [["f1", "sym1", "I -> I", "m"], ["f2", "sym2", "I", "sqlite3"]]
  r = collect_libs(e1)
  assert(str_contains(r, "-lm"))
  assert(str_contains(r, "-lsqlite3"))
  e2 = []
  r2 = collect_libs(e2)
  assert_str_eq(r2, "")
  0


-- test test_constants_extra
test_constants_extra u =
  assert_eq(ex_compose(), 31)
  assert_eq(ex_field_accessor(), 25)
  assert_eq(ex_float_lit(), 54)
  assert_eq(ex_propagate(), 32)
  assert_eq(ex_unwrap(), 33)
  assert_eq(jt_false(), 10)
  assert(is_arith_op(op_add()))
  assert(is_arith_op(op_sub()))
  assert(is_arith_op(op_mul()))
  assert(is_arith_op(op_div()))
  assert(is_arith_op(op_mod()))
  assert(is_arith_op(op_eq()) == 0)
  0


-- test test_destr_in_match
test_destr_in_match u =
  p = {fst: 3, snd: 7}
  {fst, snd} = p
  r = match fst
    _ ? fst > 0 -> fst + snd
    _ -> snd
  assert_eq(r, 10)
  q = {fst: 0, snd: 42}
  {fst, snd} = q
  r2 = match fst
    _ ? fst > 0 -> fst + snd
    _ -> snd
  assert_eq(r2, 42)

-- test test_esc_backslash
test_esc_backslash u =
  result = process_escapes("\\\\")
  assert_eq(glyph_str_len(result), 1)
  assert_eq(glyph_str_char_at(result, 0), 92)
  0

-- test test_esc_brace
test_esc_brace u =
  result = process_escapes("\{")
  assert_eq(glyph_str_len(result), 1)
  assert_eq(glyph_str_char_at(result, 0), 123)
  0

-- test test_esc_newline
test_esc_newline u =
  result = process_escapes("\\n")
  assert_eq(glyph_str_len(result), 1)
  assert_eq(glyph_str_char_at(result, 0), 10)
  0

-- test test_esc_passthrough
test_esc_passthrough u =
  result = process_escapes("hello")
  assert_str_eq(result, "hello")
  r2 = process_escapes("")
  assert_str_eq(r2, "")
  0

-- test test_esc_tab
test_esc_tab u =
  result = process_escapes("\\t")
  assert_eq(glyph_str_len(result), 1)
  assert_eq(glyph_str_char_at(result, 0), 9)
  0

-- test test_extern_codegen
test_extern_codegen u =
  wp = cg_wrap_params(2, 0)
  assert(str_contains(wp, "GVal"))
  assert(str_contains(wp, "_0"))
  assert(str_contains(wp, "_1"))
  wa = cg_wrap_call_args(3, 0)
  assert(str_contains(wa, "_0"))
  assert(str_contains(wa, "_2"))
  row = ["gettime", "time", "I -> I"]
  wrapper = cg_extern_wrapper(row)
  assert(str_contains(wrapper, "glyph_gettime"))
  assert(str_contains(wrapper, "time"))
  row2 = ["myexit", "exit", "I -> V"]
  wrapper2 = cg_extern_wrapper(row2)
  assert(str_contains(wrapper2, "return 0"))
  assert_eq(sig_is_void_ret(split_arrow("I -> V")), 1)
  assert_eq(sig_is_void_ret(split_arrow("I -> I")), 0)
  assert_eq(has_glyph_prefix("glyph_foo"), 1)
  assert_eq(has_glyph_prefix("bar"), 0)
  0


-- test test_extract_bodies
test_extract_bodies u =
  rows = [["body1"], ["body2"], ["body3"]]
  r = extract_bodies_acc(rows, 0, [])
  assert_eq(glyph_array_len(r), 3)
  assert_str_eq(r[0], "body1")
  assert_str_eq(r[2], "body3")
  0


-- test test_float_basic
test_float_basic u =
  x = 3.14
  y = 2.0
  z = x + y
  assert_eq(float_to_int(z), 5)
  assert_eq(float_to_int(x * y), 6)
  assert_eq(float_to_int(10.0 / 3.0), 3)
  0


-- test test_float_cmp
test_float_cmp u =
  a = 1.5
  b = 2.5
  assert_eq(a < b, 1)
  assert_eq(a > b, 0)
  assert_eq(a == a, 1)
  assert_eq(a == b, 0)
  0


-- test test_float_coerce
test_float_coerce u =
  x = 3.14 + 1
  assert_eq(float_to_int(x), 4)
  y = 2 + 1.5
  assert_eq(float_to_int(y), 3)
  z = 10.0 - 3
  assert_eq(float_to_int(z), 7)
  w = 2 * 3.5
  assert_eq(float_to_int(w), 7)
  0


-- test test_float_convert
test_float_convert u =
  x = int_to_float(42)
  assert_eq(float_to_int(x), 42)
  n = 0 - 7
  y = int_to_float(n)
  assert_eq(float_to_int(y), n)
  0


-- test test_float_interp
test_float_interp u =
  x = 3.14
  s = float_to_str(x)
  assert_str_eq(s, "3.14")
  0


-- test test_format_diag
test_format_diag u =
  src = "f x = x + 1"
  toks = tokenize(src)
  err = format_parse_err(src, toks, 0, "test error")
  assert(str_contains(err, "test error"))
  assert(str_contains(err, "^"))
  diag = format_diagnostic("my_fn", src, 4, 1, "bad thing")
  assert(str_contains(diag, "my_fn"))
  assert(str_contains(diag, "bad thing"))
  0


-- test test_guard_dispatch
test_guard_dispatch u =
  x = 10
  r1 = match x
    _ ? x == 1 -> 100
    _ ? x == 10 -> 200
    _ ? x == 20 -> 300
    _ -> 0
  assert_eq(r1, 200)
  y = 99
  r2 = match y
    _ ? y == 1 -> 100
    _ ? y == 2 -> 200
    _ -> 999
  assert_eq(r2, 999)

-- test test_guard_fallthrough
test_guard_fallthrough u =
  a = 3
  r = match a
    _ ? a == 1 -> 10
    _ ? a == 2 -> 20
    _ ? a == 3 -> 30
    _ ? a == 4 -> 40
    _ ? a == 5 -> 50
    _ -> 0
  assert_eq(r, 30)
  b = 99
  r2 = match b
    _ ? b == 1 -> 10
    _ ? b == 2 -> 20
    _ -> 0
  assert_eq(r2, 0)

-- test test_is_float_ctype
test_is_float_ctype u =
  assert_eq(is_float_ctype("float"), 1)
  assert_eq(is_float_ctype("double"), 1)
  assert_eq(is_float_ctype("int"), 0)
  assert_eq(is_float_ctype("GVal"), 0)
  0


-- test test_is_op_float
test_is_op_float u =
  local_types = [0, 0, 3]
  f = mk_op_float("3.14")
  assert_eq(is_op_float(local_types, f), 1)
  i = mk_op_int(42)
  assert_eq(is_op_float(local_types, i), 0)
  loc = mk_op_local(2)
  assert_eq(is_op_float(local_types, loc), 1)
  loc2 = mk_op_local(0)
  assert_eq(is_op_float(local_types, loc2), 0)
  0


-- test test_is_runtime_fn
test_is_runtime_fn u =
  assert(is_runtime_fn("println"))
  assert(is_runtime_fn("str_len"))
  assert(is_runtime_fn("array_push"))
  assert(is_runtime_fn("sb_new"))
  assert(is_runtime_fn("assert_eq"))
  assert(is_runtime_fn("raw_set"))
  assert(is_runtime_fn("my_func") == 0)
  assert(is_runtime_fn("foo") == 0)
  assert(is_runtime_fn("test") == 0)
  0

-- test test_join_funcs
test_join_funcs u =
  argv = ["a", "b", "c"]
  r1 = join_args(argv, 0, 3)
  assert_str_eq(r1, "a b c")
  r1b = join_args(argv, 1, 3)
  assert_str_eq(r1b, "b c")
  rows = [["alice"], ["bob"]]
  r2 = join_names(rows, 0, 2, "")
  assert(str_contains(r2, "alice"))
  assert(str_contains(r2, "bob"))
  row = ["x", "y", "z"]
  r3 = join_cols(row, 0, 3, "")
  assert(str_contains(r3, "x"))
  assert(str_contains(r3, "z"))
  0


-- test test_json_access
test_json_access u =
  src = r"""{"name":"hello","val":42}"""
  pool = []
  toks = json_tokenize(src)
  pr = json_parse(src, toks, 0, pool)
  assert_str_eq(json_get_str(pool, pr.node, "name"), "hello")
  assert_eq(json_get_int(pool, pr.node, "val"), 42)
  assert_eq(json_get(pool, pr.node, "missing"), 0 - 1)

-- test test_json_array
test_json_array u =
  src = "[1,2,3]"
  pool = []
  toks = json_tokenize(src)
  pr = json_parse(src, toks, 0, pool)
  assert_eq(pool[pr.node].tag, jn_array)
  assert_eq(json_arr_len(pool, pr.node), 3)
  first = json_arr_get(pool, pr.node, 0)
  assert_eq(pool[first].nval, 1)
  last = json_arr_get(pool, pr.node, 2)
  assert_eq(pool[last].nval, 3)

-- test test_json_builder
test_json_builder u =
  pool = []
  obj = jb_obj(pool)
  jb_put(pool, obj, "name", jb_str(pool, "test"))
  jb_put(pool, obj, "count", jb_int(pool, 42))
  jb_put(pool, obj, "flag", jb_bool(pool, 1))
  jb_put(pool, obj, "empty", jb_null(pool))
  r = json_gen(pool, obj)
  assert(str_contains(r, "test"))
  assert(str_contains(r, "42"))
  assert(str_contains(r, "true"))
  assert(str_contains(r, "null"))
  pool2 = []
  arr = jb_arr(pool2)
  jb_push(pool2, arr, jb_int(pool2, 1))
  jb_push(pool2, arr, jb_int(pool2, 2))
  r2 = json_gen(pool2, arr)
  assert(str_contains(r2, "[1,2]"))
  0


-- test test_json_extra
test_json_extra u =
  pool = []
  toks = json_tokenize(r"""{"a":1,"b":"hi","c":null,"d":false}""")
  pr = json_parse(r"""{"a":1,"b":"hi","c":null,"d":false}""", toks, 0, pool)
  assert_eq(pool[pr.node].tag, jn_object())
  val_a = json_get(pool, pr.node, "a")
  assert_eq(pool[val_a].tag, jn_int())
  assert_eq(pool[val_a].nval, 1)
  val_b = json_get(pool, pr.node, "b")
  assert_eq(pool[val_b].tag, jn_str())
  val_c = json_get(pool, pr.node, "c")
  assert_eq(pool[val_c].tag, jn_null())
  pool2 = []
  toks2 = json_tokenize("[1,2,3]")
  pr2 = json_parse("[1,2,3]", toks2, 0, pool2)
  alen = json_arr_len(pool2, pr2.node)
  assert_eq(alen, 3)
  e0 = json_arr_get(pool2, pr2.node, 0)
  assert_eq(pool2[e0].nval, 1)
  pool3 = []
  gen_arr = jb_arr(pool3)
  jb_push(pool3, gen_arr, jb_str(pool3, "x"))
  jb_push(pool3, gen_arr, jb_null(pool3))
  jb_push(pool3, gen_arr, jb_bool(pool3, 0))
  r3 = json_gen(pool3, gen_arr)
  assert(str_contains(r3, "null"))
  assert(str_contains(r3, "false"))
  0


-- test test_json_gen
test_json_gen u =
  pool = []
  obj = jb_obj(pool)
  jb_put(pool, obj, "x", jb_int(pool, 42))
  jb_put(pool, obj, "s", jb_str(pool, "hi"))
  r = json_gen(pool, obj)
  src = r"""{"x":42,"s":"hi"}"""
  assert_str_eq(r, src)

-- test test_json_parse
test_json_parse u =
  pool = []
  toks = json_tokenize("42")
  pr = json_parse("42", toks, 0, pool)
  assert_eq(pool[pr.node].tag, jn_int)
  assert_eq(pool[pr.node].nval, 42)
  pool2 = []
  toks2 = json_tokenize("-7")
  pr2 = json_parse("-7", toks2, 0, pool2)
  assert_eq(pool2[pr2.node].nval, 0 - 7)
  pool3 = []
  toks3 = json_tokenize("true")
  pr3 = json_parse("true", toks3, 0, pool3)
  assert_eq(pool3[pr3.node].tag, jn_bool)
  assert_eq(pool3[pr3.node].nval, 1)

-- test test_json_tokenize
test_json_tokenize u =
  toks = json_tokenize("42")
  assert_eq(array_len(toks), 1)
  assert_eq(toks[0].kind, jt_number)
  toks2 = json_tokenize("true")
  assert_eq(array_len(toks2), 1)
  assert_eq(toks2[0].kind, jt_true)
  toks3 = json_tokenize("null")
  assert_eq(array_len(toks3), 1)
  assert_eq(toks3[0].kind, jt_null)

-- test test_let_destr
test_let_destr u =
  p = {x: 10, y: 20}
  {x, y} = p
  assert_eq(x, 10)
  assert_eq(y, 20)
  {node, pos} = {node: 42, pos: 7}
  assert_eq(node, 42)
  assert_eq(pos, 7)
  0


-- test test_match_guard
test_match_guard u =
  r1 = match 5
    n ? n > 3 -> "big"
    _ -> "small"
  assert_str_eq(r1, "big")
  r2 = match 2
    n ? n > 3 -> "big"
    _ -> "small"
  assert_str_eq(r2, "small")
  r3 = match 15
    n ? n > 10 -> "large"
    n ? n > 5 -> "medium"
    _ -> "small"
  assert_str_eq(r3, "large")
  r4 = match 7
    n ? n > 10 -> "large"
    n ? n > 5 -> "medium"
    _ -> "small"
  assert_str_eq(r4, "medium")
  flag = 1
  r5 = match 42
    _ ? flag == 1 -> "flagged"
    _ -> "default"
  assert_str_eq(r5, "flagged")
  r6 = match 42
    0 -> "zero"
    n ? n > 100 -> "huge"
    _ -> "other"
  assert_str_eq(r6, "other")
  0


-- test test_mcp_helpers
test_mcp_helpers u =
  e1 = mcp_error(1, 0 - 32602, "bad param")
  assert(str_contains(e1, "error"))
  assert(str_contains(e1, "-32602"))
  assert(str_contains(e1, "bad param"))
  t1 = mcp_text_result(2, "hello world")
  assert(str_contains(t1, "hello world"))
  assert(str_contains(t1, "jsonrpc"))
  assert(str_contains(t1, "result"))
  0


-- test test_mcp_respond
test_mcp_respond u =
  r = mcp_respond(1, r"""{"x":1}""")
  src = r"""{"jsonrpc":"2.0","id":1,"result":{"x":1}}"""
  assert_str_eq(r, src)

-- test test_mcp_tools_list
test_mcp_tools_list u =
  r = mcp_tools_list(1)
  assert(str_contains(r, "get_def"))
  assert(str_contains(r, "put_def"))
  assert(str_contains(r, "list_defs"))
  assert(str_contains(r, "search_defs"))
  assert(str_contains(r, "remove_def"))
  assert(str_contains(r, "deps"))
  assert(str_contains(r, "rdeps"))
  assert(str_contains(r, "sql"))
  assert(str_contains(r, "check_def"))
  assert(str_contains(r, "jsonrpc"))
  0


-- test test_mir_array_agg
test_mir_array_agg u =
  mir = compile_fn("f u = [1, 2]", [])
  stmts = mir.fn_blocks_stmts[mir.fn_entry]
  i = test_find_stmt_kind(stmts, rv_aggregate(), 0)
  assert(i >= 0)
  s = stmts[i]
  assert_eq(s.sival, ag_array())
  assert_eq(glyph_array_len(s.sops), 2)
  0

-- test test_mir_assign
test_mir_assign u =
  mir = compile_fn("f x = x := 1\n  x", [])
  bc = mir_block_count(mir)
  assert(bc >= 1)
  lc = mir_local_count(mir)
  assert(lc >= 1)
  0


-- test test_mir_binary_op
test_mir_binary_op u =
  mir = compile_fn("f x = x + 1", [])
  stmts = mir.fn_blocks_stmts[mir.fn_entry]
  i = test_find_stmt_kind(stmts, rv_binop(), 0)
  assert(i >= 0)
  s = stmts[i]
  assert_eq(s.sival, mir_add())
  0

-- test test_mir_call
test_mir_call u =
  mir = compile_fn("f x = foo(x)", [])
  stmts = mir.fn_blocks_stmts[mir.fn_entry]
  i = test_find_stmt_kind(stmts, rv_call(), 0)
  assert(i >= 0)
  s = stmts[i]
  assert_eq(s.sop1.okind, ok_func_ref())
  assert_str_eq(s.sop1.ostr, "foo")
  0

-- test test_mir_deep
test_mir_deep u =
  mir2 = compile_fn("f x = x := 5\n  x", [])
  assert(mir_local_count(mir2) >= 1)
  mir4 = compile_fn("f x = x |> foo", ["foo"])
  stmts4 = mir4.fn_blocks_stmts[0]
  found4 = test_find_stmt_kind(stmts4, rv_call(), 0)
  assert(found4 >= 0)
  mir5 = compile_fn("f x = match x\n  1 | 2 | 3 -> 10\n  _ -> 0", [])
  assert(mir_block_count(mir5) >= 3)
  0


-- test test_mir_emit_extra
test_mir_emit_extra u =
  mir1 = compile_fn("f x = \\y -> x", [])
  stmts1 = mir1.fn_blocks_stmts[0]
  found1 = test_find_stmt_kind(stmts1, rv_make_closure(), 0)
  assert(found1 >= 0)
  mir2 = compile_fn("f u = [1, 2, 3]", [])
  stmts2 = mir2.fn_blocks_stmts[0]
  found2 = test_find_stmt_kind(stmts2, rv_aggregate(), 0)
  assert(found2 >= 0)
  mir3 = compile_fn("f x = x + 1", [])
  stmts3 = mir3.fn_blocks_stmts[0]
  found3 = test_find_stmt_kind(stmts3, rv_binop(), 0)
  assert(found3 >= 0)
  0


-- test test_mir_field
test_mir_field u =
  mir = compile_fn("f x = x.name", [])
  stmts = mir.fn_blocks_stmts[mir.fn_entry]
  i = test_find_stmt_kind(stmts, rv_field(), 0)
  assert(i >= 0)
  s = stmts[i]
  assert_str_eq(s.sstr, "name")
  0

-- test test_mir_index
test_mir_index u =
  mir = compile_fn("f x = x[0]", [])
  stmts = mir.fn_blocks_stmts[mir.fn_entry]
  i = test_find_stmt_kind(stmts, rv_index(), 0)
  assert(i >= 0)
  0

-- test test_mir_int_return
test_mir_int_return u =
  mir = compile_fn("f u = 42", [])
  term = mir.fn_blocks_terms[mir.fn_entry]
  assert_eq(term.tkind, tm_return())
  assert_eq(term.top.okind, ok_const_int())
  assert_eq(term.top.oval, 42)
  0

-- test test_mir_let_binding
test_mir_let_binding u =
  mir = compile_fn("f u =\n  x = 42\n  x", [])
  stmts = mir.fn_blocks_stmts[mir.fn_entry]
  i = test_find_stmt_kind(stmts, rv_use(), 0)
  assert(i >= 0)
  0

-- test test_mir_lower_bool
test_mir_lower_bool u =
  mir1 = compile_fn("f x = match x\n  true -> 1\n  _ -> 0", [])
  assert(mir_block_count(mir1) >= 3)
  mir2 = compile_fn("f x = match x\n  \"hi\" -> 1\n  _ -> 0", [])
  assert(mir_block_count(mir2) >= 3)
  mir3 = compile_fn("f x = match x\n  y -> y", [])
  assert(mir_block_count(mir3) >= 1)
  0


-- test test_mir_lower_extra
test_mir_lower_extra u =
  mir1 = compile_fn("f x = match x\n  1 -> 10\n  2 -> 20\n  _ -> 0", [])
  assert(mir_block_count(mir1) >= 3)
  mir2 = compile_fn("f x = 0 - x", [])
  assert(mir_block_count(mir2) >= 1)
  mir3 = compile_fn("f s t = s + t", [])
  assert(mir_block_count(mir3) >= 1)
  mir4 = compile_fn("f x = match x\n  y -> y + 1", [])
  assert(mir_block_count(mir4) >= 1)
  mir5 = compile_fn("f u = y = 10\n  y", [])
  assert(mir_block_count(mir5) >= 1)
  0


-- test test_mir_match_blocks
test_mir_match_blocks u =
  mir = compile_fn("f x = match x\n  1 -> 10\n  _ -> 20", [])
  nblocks = glyph_array_len(mir.fn_blocks_terms)
  assert(nblocks >= 3)
  0

-- test test_mir_negate
test_mir_negate u =
  mir = compile_fn("f x = -x", [])
  stmts0 = mir.fn_blocks_stmts[0]
  assert(glyph_array_len(stmts0) >= 1)
  term = mir.fn_blocks_terms[0]
  assert_eq(term.tkind, tm_return())
  0


-- test test_mir_pipe
test_mir_pipe u =
  mir = compile_fn("f x = x |> foo", [])
  stmts = mir.fn_blocks_stmts[0]
  found = test_find_stmt_kind(stmts, rv_call(), 0)
  assert(found >= 0)
  s = stmts[found]
  assert_str_eq(s.sop1.ostr, "foo")
  0


-- test test_mir_pp
test_mir_pp u =
  mir = compile_fn("f u = 42", [])
  pp = mir_pp_fn(mir)
  assert(glyph_str_len(pp) > 10)
  0

-- test test_mir_pp2
test_mir_pp2 u =
  mir = compile_fn("f x = match x\n  1 -> 10\n  _ -> 20", [])
  pp = mir_pp_fn(mir)
  assert(str_contains(pp, "f"))
  assert(glyph_str_len(pp) > 10)
  op_pp = mir_pp_op(mk_op_int(42))
  assert(str_contains(op_pp, "42"))
  0


-- test test_mir_pp_fns
test_mir_pp_fns u =
  mir1 = compile_fn("f x = x + 1", [])
  mir2 = compile_fn("g x = x * 2", [])
  mirs = [mir1, mir2]
  mir_pp_fns(mirs, 0)
  pp1 = mir_pp_fn(mir1)
  assert(glyph_str_len(pp1) > 10)
  pp2 = mir_pp_fn(mir2)
  assert(glyph_str_len(pp2) > 10)
  0


-- test test_mir_record_agg
test_mir_record_agg u =
  mir = compile_fn("f u = \{a: 1, b: 2}", [])
  stmts = mir.fn_blocks_stmts[mir.fn_entry]
  i = test_find_stmt_kind(stmts, rv_aggregate(), 0)
  assert(i >= 0)
  s = stmts[i]
  assert_eq(s.sival, ag_record())
  0

-- test test_mir_str_interp
test_mir_str_interp u =
  src = r"""f x = "hi {x}""""
  mir = compile_fn(src, [])
  stmts = mir.fn_blocks_stmts[0]
  found = test_find_stmt_kind(stmts, rv_str_interp(), 0)
  assert(found >= 0)
  0


-- test test_mir_str_match
test_mir_str_match u =
  mir = compile_fn("f s = match s\n  \"a\" -> 1\n  _ -> 0", [])
  bc = mir_block_count(mir)
  assert(bc >= 2)
  mir2 = compile_fn("f x = match x\n  y -> y", [])
  bc2 = mir_block_count(mir2)
  assert(bc2 >= 1)
  mir3 = compile_fn("f x = match x\n  1 | 2 -> 10\n  _ -> 0", [])
  bc3 = mir_block_count(mir3)
  assert(bc3 >= 2)
  0


-- test test_mir_str_return
test_mir_str_return u =
  q = "\""
  src = s3("f u = ", q, s2("hello", q))
  mir = compile_fn(src, [])
  term = mir.fn_blocks_terms[mir.fn_entry]
  assert_eq(term.tkind, tm_return())
  assert_eq(term.top.okind, ok_const_str())
  assert_str_eq(term.top.ostr, "hello")
  0

-- test test_mir_unary
test_mir_unary u =
  mir = compile_fn("f x = 0 - x", [])
  stmts = mir.fn_blocks_stmts[0]
  assert(glyph_array_len(stmts) >= 1)
  term = mir.fn_blocks_terms[0]
  assert_eq(term.tkind, tm_return())
  0


-- test test_mir_unary2
test_mir_unary2 u =
  mir = compile_fn("f x = 0 - x", [])
  assert(mir.fn_local_count > 0)
  stmts0 = mir.fn_blocks_stmts[0]
  assert(glyph_array_len(stmts0) >= 1)
  mir2 = compile_fn("f b = match b\n  true -> 0\n  _ -> 1", [])
  assert(mir2.fn_block_count >= 2)
  0


-- test test_mir_zero_arg
test_mir_zero_arg u =
  za = ["my_const"]
  mir = compile_fn("f u = my_const", za)
  stmts = mir.fn_blocks_stmts[mir.fn_entry]
  i = test_find_stmt_kind(stmts, rv_call(), 0)
  assert(i >= 0)
  s = stmts[i]
  assert_str_eq(s.sop1.ostr, "my_const")
  0

-- test test_needs_sqlite
test_needs_sqlite u =
  e1 = [["f1", "sym1", "I -> I", "sqlite3"]]
  assert_eq(needs_sqlite(e1), 1)
  e2 = [["f2", "sym2", "I -> I", ""]]
  assert_eq(needs_sqlite(e2), 0)
  e3 = []
  assert_eq(needs_sqlite(e3), 0)
  assert_eq(lib_seen(["a", "b", "c"], "b", 0), 1)
  assert_eq(lib_seen(["a", "b", "c"], "d", 0), 0)
  0


-- test test_nested_guards
test_nested_guards u =
  x = 1
  y = 20
  r1 = match x
    _ ? x == 1 -> match y
      _ ? y == 10 -> 100
      _ ? y == 20 -> 200
      _ -> 0
    _ ? x == 2 -> 300
    _ -> 0 - 1
  assert_eq(r1, 200)
  a = 2
  r2 = match a
    _ ? a == 1 -> 100
    _ ? a == 2 -> 300
    _ -> 0 - 1
  assert_eq(r2, 300)

-- test test_or_guard
test_or_guard u =
  x = 2
  r1 = match x
    _ ? x == 1 || x == 2 || x == 3 -> 10
    _ ? x == 10 || x == 20 -> 20
    _ -> 0
  assert_eq(r1, 10)
  y = 20
  r2 = match y
    _ ? y == 1 || y == 2 || y == 3 -> 10
    _ ? y == 10 || y == 20 -> 20
    _ -> 0
  assert_eq(r2, 20)
  z = 99
  r3 = match z
    _ ? z == 1 || z == 2 || z == 3 -> 10
    _ ? z == 10 || z == 20 -> 20
    _ -> 0
  assert_eq(r3, 0)

-- test test_parse_array
test_parse_array u =
  src = "f x = [1, 2, 3]"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_array())
  assert_eq(glyph_array_len(body.ns), 3)
  0

-- test test_parse_binary
test_parse_binary u =
  src = "f x = x + 1"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_binary())
  assert_eq(body.ival, op_add())
  lhs = pool[body.n1]
  rhs = pool[body.n2]
  assert_eq(lhs.kind, ex_ident())
  assert_eq(rhs.kind, ex_int_lit())
  0

-- test test_parse_call
test_parse_call u =
  src = "f x = foo(1, 2)"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_call())
  assert_eq(glyph_array_len(body.ns), 2)
  0

-- test test_parse_error
test_parse_error u =
  src = "f x = ="
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r))
  assert(glyph_str_len(r.pr_err) > 0)
  0

-- test test_parse_extras
test_parse_extras u =
  src = "f u = \\y -> y + 1"
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  assert(r.node >= 0)
  src2 = r"""f r = \{a: 1, b: 2}"""
  toks2 = tokenize(src2)
  ast2 = []
  r2 = parse_fn_def(src2, toks2, 0, ast2)
  assert(r2.node >= 0)
  src3 = r"""f s = "hello {42}""""
  toks3 = tokenize(src3)
  ast3 = []
  r3 = parse_fn_def(src3, toks3, 0, ast3)
  assert(r3.node >= 0)
  0


-- test test_parse_field
test_parse_field u =
  src = "f x = x.name"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_field_access())
  assert_str_eq(body.sval, "name")
  0

-- test test_parse_helpers
test_parse_helpers u =
  src = "hello\nworld"
  ls = find_line_start(src, 6)
  assert_eq(ls, 6)
  le = find_line_end(src, 6)
  assert_eq(le, 11)
  sl = extract_source_line(src, 6)
  assert_str_eq(sl, "world")
  col = line_col(src, 7)
  assert_eq(col, 2)
  fkc = format_kind_counts([["fn", "5"], ["test", "3"]], 0, 2, "")
  assert(str_contains(fkc, "fn"))
  assert(str_contains(fkc, "test"))
  argv = ["glyph", "test", "db.glyph", "--gen", "2", "--debug", "test_foo"]
  assert_eq(has_flag(argv, "--debug", 0), 1)
  assert_eq(has_flag(argv, "--release", 0), 0)
  gv = find_flag(argv, "--gen", 0)
  assert_str_eq(gv, "2")
  filtered = filter_test_args(argv, 3, 7)
  assert_eq(glyph_array_len(filtered), 1)
  assert_str_eq(filtered[0], "test_foo")
  0


-- test test_parse_int_lit
test_parse_int_lit u =
  src = "f x = 42"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_int_lit())
  assert_eq(body.ival, 42)
  0

-- test test_parse_lambda
test_parse_lambda u =
  src = "f x = \\y -> y + 1"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_lambda())
  0

-- test test_parse_match
test_parse_match u =
  src = "f x = match x\n  1 -> 10\n  2 -> 20\n  _ -> 30"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_match())
  assert_eq(glyph_array_len(body.ns), 9)
  0


-- test test_parse_or_pat
test_parse_or_pat u =
  src = "f x = match x\n  1 | 2 | 3 -> 10\n  _ -> 0"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_match())
  arms = body.ns
  p0 = pool[arms[0]]
  assert_eq(p0.kind, pat_or())
  assert_eq(glyph_array_len(p0.ns), 3)
  0

-- test test_parse_patterns
test_parse_patterns u =
  src = "f x = match x\n  42 -> 1\n  true -> 2\n  y -> 3\n  _ -> 4"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_match())
  arms = body.ns
  p0 = pool[arms[0]]
  assert_eq(p0.kind, pat_int())
  p1 = pool[arms[3]]
  assert_eq(p1.kind, pat_bool())
  p2 = pool[arms[6]]
  assert_eq(p2.kind, pat_ident())
  p3 = pool[arms[9]]
  assert_eq(p3.kind, pat_wildcard())
  0


-- test test_parse_precedence
test_parse_precedence u =
  src = "f x = 1 + 2 * 3"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_binary())
  assert_eq(body.ival, op_add())
  rhs = pool[body.n2]
  assert_eq(rhs.kind, ex_binary())
  assert_eq(rhs.ival, op_mul())
  0

-- test test_parse_record
test_parse_record u =
  src = "f x = \{a: 1, b: 2}"
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_record())
  assert_eq(glyph_array_len(body.ns), 4)
  0

-- test test_parse_str_lit
test_parse_str_lit u =
  q = "\""
  src = s3("f x = ", q, s2("hello", q))
  toks = tokenize(src)
  pool = []
  r = parse_fn_def(src, toks, 0, pool)
  assert(is_err(r) == 0)
  fn_node = pool[r.node]
  body = pool[fn_node.n1]
  assert_eq(body.kind, ex_str_lit())
  0

-- test test_parse_struct
test_parse_struct u =
  r = parse_struct_fields("Point = \{x: I, y: I}")
  assert_eq(glyph_array_len(r), 2)
  assert_str_eq(r[0], "x")
  assert_str_eq(r[1], "y")
  r2 = parse_struct_fields("no braces")
  assert_eq(glyph_array_len(r2), 0)
  ct = parse_struct_ctypes("Config = \{name: S, count: I}")
  assert_eq(glyph_array_len(ct), 2)
  0


-- test test_resolve_fld
test_resolve_fld u =
  reg = [["a", "b", "c"], ["x", "y"]]
  r = resolve_fld_off(reg, "b", ["a", "c"])
  assert_eq(r, 1)
  r2 = resolve_fld_off(reg, "y", ["x"])
  assert_eq(r2, 1)
  r3 = resolve_fld_off(reg, "z", ["q"])
  assert_eq(r3, 0 - 1)
  0


-- test test_sort_str
test_sort_str u =
  arr = ["c", "a", "b"]
  sorted = sort_str_arr(arr)
  assert_str_eq(sorted[0], "a")
  assert_str_eq(sorted[1], "b")
  assert_str_eq(sorted[2], "c")
  arr2 = ["z"]
  sorted2 = sort_str_arr(arr2)
  assert_str_eq(sorted2[0], "z")
  arr3 = []
  sorted3 = sort_str_arr(arr3)
  assert_eq(glyph_array_len(sorted3), 0)
  0


-- test test_split_comma
test_split_comma u =
  r1 = split_comma("a,b,c")
  assert_eq(glyph_array_len(r1), 3)
  assert_str_eq(r1[0], "a")
  assert_str_eq(r1[1], "b")
  assert_str_eq(r1[2], "c")
  r2 = split_comma("single")
  assert_eq(glyph_array_len(r2), 1)
  0


-- test test_string_utils
test_string_utils u =
  r1 = join_str_arr(["a", "b", "c"], 0)
  assert_str_eq(r1, "a b c")
  r2 = join_str_arr(["x"], 0)
  assert_str_eq(r2, "x")
  r3 = join_str_arr([], 0)
  assert_str_eq(r3, "")
  r4 = sql_escape("it's")
  assert_str_eq(r4, "it''s")
  r5 = sql_escape("hello")
  assert_str_eq(r5, "hello")
  r6 = strip_ext("foo.glyph")
  assert_str_eq(r6, "foo")
  r7 = strip_ext("short")
  assert_str_eq(r7, "short")
  r8 = s6("a", "b", "c", "d", "e", "f")
  assert_str_eq(r8, "abcdef")
  r9 = s7("a", "b", "c", "d", "e", "f", "g")
  assert_str_eq(r9, "abcdefg")
  c1 = make_caret(1)
  assert_str_eq(c1, "^")
  c3 = make_caret(3)
  assert_str_eq(c3, "  ^")
  0


-- test test_tc_errors
test_tc_errors u =
  eng = mk_engine()
  srcs = ["f x = x + 1", "g y = y * 2", "h z = z"]
  parsed = parse_all_fns(srcs, 0)
  tc_pre_register(eng, parsed, 0)
  tc_infer_all(eng, parsed, 0)
  errs = tc_report_errors(eng)
  assert_eq(errs, 0)
  0


-- test test_tc_pipeline
test_tc_pipeline u =
  eng = mk_engine()
  srcs = ["f x = x + 1", "g x = x * 2"]
  parsed = parse_all_fns(srcs, 0)
  tc_pre_register(eng, parsed, 0)
  tc_infer_all(eng, parsed, 0)
  errs = eng.errors
  assert_eq(glyph_array_len(errs), 0)
  0


-- test test_tco
test_tco u =
  mir = compile_fn("f x = match x\n  0 -> 1\n  _ -> f(x - 1)", ["f"])
  mirs = [mir]
  tco_optimize(mirs)
  bc = mir_block_count(mirs[0])
  assert(bc >= 1)
  0


-- test test_tmap_init
test_tmap_init u =
  tm = tmap_init(5)
  assert_eq(glyph_array_len(tm), 5)
  assert_eq(tm[0], 0 - 1)
  assert_eq(tm[4], 0 - 1)
  ctx = mk_tctx(tm)
  assert_eq(glyph_array_len(ctx.tc_tmap), 5)
  0


-- test test_tok_brackets
test_tok_brackets u =
  toks1 = tokenize("(a)")
  k1 = test_collect_kinds(toks1, 0, [])
  assert(has_kind(k1, tk_lparen()))
  assert(has_kind(k1, tk_rparen()))
  toks2 = tokenize("[b]")
  k2 = test_collect_kinds(toks2, 0, [])
  assert(has_kind(k2, tk_lbracket()))
  assert(has_kind(k2, tk_rbracket()))
  0

-- test test_tok_comment
test_tok_comment u =
  toks = tokenize("-- comment\n42")
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_int()))
  i = test_find_kind(toks, tk_int(), 0)
  assert(i >= 0)
  assert_eq(cur_ival("-- comment\n42", toks, i), 42)
  0

-- test test_tok_empty
test_tok_empty u =
  toks = tokenize("")
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_eof()))
  assert_eq(glyph_array_len(kinds), 1)
  0

-- test test_tok_extras
test_tok_extras u =
  assert_eq(tk_else(), 21)
  assert_eq(tk_for(), 23)
  assert_eq(tk_in(), 24)
  assert(tk_error() != 0)
  src = r""""hello {x}""""
  toks = tokenize(src)
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_str_interp_end()))
  0


-- test test_tok_ident_keyword
test_tok_ident_keyword u =
  toks = tokenize("match x")
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_match()))
  assert(has_kind(kinds, tk_ident()))
  0

-- test test_tok_indent_dedent
test_tok_indent_dedent u =
  src = "x\n  y\nz"
  toks = tokenize(src)
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_indent()))
  assert(has_kind(kinds, tk_dedent()))
  0

-- test test_tok_integer
test_tok_integer u =
  toks = tokenize("42")
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_int()))
  i = test_find_kind(toks, tk_int(), 0)
  assert(i >= 0)
  assert_eq(cur_ival("42", toks, i), 42)
  0

-- test test_tok_negative
test_tok_negative u =
  toks = tokenize("0 - 7")
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_int()))
  assert(has_kind(kinds, tk_minus()))
  i1 = test_find_kind(toks, tk_int(), 0)
  i2 = test_find_kind(toks, tk_int(), i1 + 1)
  assert(i1 >= 0)
  assert(i2 >= 0)
  assert(i2 > i1)
  0

-- test test_tok_operators
test_tok_operators u =
  toks = tokenize("a + b == c")
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_plus()))
  assert(has_kind(kinds, tk_eq_eq()))
  assert(has_kind(kinds, tk_ident()))
  0

-- test test_tok_raw_string
test_tok_raw_string u =
  q = "\""
  src = s4("r", q, "hello\\nworld", q)
  toks = tokenize(src)
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_str()))
  i = test_find_kind(toks, tk_str(), 0)
  t = toks[i]
  tlen = t.end - t.start
  assert(tlen > 5)
  0

-- test test_tok_string
test_tok_string u =
  q = "\""
  src = s3(q, "hello", q)
  toks = tokenize(src)
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_str()))
  0

-- test test_tok_text
test_tok_text u =
  src = "foo 123"
  toks = tokenize(src)
  i = test_find_kind(toks, tk_ident(), 0)
  assert(i >= 0)
  assert_str_eq(cur_text(src, toks, i), "foo")
  j = test_find_kind(toks, tk_int(), 0)
  assert(j >= 0)
  assert_eq(cur_ival(src, toks, j), 123)
  0

-- test test_tok_two_char_ops
test_tok_two_char_ops u =
  toks1 = tokenize("a != b")
  assert(has_kind(test_collect_kinds(toks1, 0, []), tk_bang_eq()))
  toks2 = tokenize("a <= b")
  assert(has_kind(test_collect_kinds(toks2, 0, []), tk_lt_eq()))
  toks3 = tokenize("a >= b")
  assert(has_kind(test_collect_kinds(toks3, 0, []), tk_gt_eq()))
  toks4 = tokenize("a |> b")
  assert(has_kind(test_collect_kinds(toks4, 0, []), tk_pipe_gt()))
  toks5 = tokenize("a >> b")
  assert(has_kind(test_collect_kinds(toks5, 0, []), tk_gt_gt()))
  toks6 = tokenize("x := 1")
  assert(has_kind(test_collect_kinds(toks6, 0, []), tk_colon_eq()))
  toks7 = tokenize("a -> b")
  assert(has_kind(test_collect_kinds(toks7, 0, []), tk_arrow()))
  toks8 = tokenize("1..10")
  assert(has_kind(test_collect_kinds(toks8, 0, []), tk_dot_dot()))
  0

-- test test_tok_type_ident
test_tok_type_ident u =
  toks = tokenize("Int x")
  kinds = test_collect_kinds(toks, 0, [])
  assert(has_kind(kinds, tk_type_ident()))
  assert(has_kind(kinds, tk_ident()))
  0

-- test test_ty_block_call
test_ty_block_call u =
  eng1 = mk_engine()
  src1 = "f x = y = x + 1\n  y * 2"
  toks1 = tokenize(src1)
  ast1 = []
  r1 = parse_fn_def(src1, toks1, 0, ast1)
  ty1 = infer_fn_def(eng1, ast1, r1.node)
  res1 = subst_resolve(eng1, ty1)
  node1 = pool_get(eng1, res1)
  assert_eq(node1.tag, ty_fn())
  eng2 = mk_engine()
  src2 = "f x = foo(x, 1)"
  toks2 = tokenize(src2)
  ast2 = []
  r2 = parse_fn_def(src2, toks2, 0, ast2)
  ty2 = infer_fn_def(eng2, ast2, r2.node)
  node2 = pool_get(eng2, ty2)
  assert_eq(node2.tag, ty_fn())
  0


-- test test_ty_deep
test_ty_deep u =
  eng1 = mk_engine()
  src1 = "f x = y = x + 1\n  y * 2"
  toks1 = tokenize(src1)
  ast1 = []
  r1 = parse_fn_def(src1, toks1, 0, ast1)
  ty1 = infer_fn_def(eng1, ast1, r1.node)
  res1 = subst_resolve(eng1, ty1)
  n1 = pool_get(eng1, res1)
  assert_eq(n1.tag, ty_fn())
  eng2 = mk_engine()
  src2 = "f x = 0 - x"
  toks2 = tokenize(src2)
  ast2 = []
  r2 = parse_fn_def(src2, toks2, 0, ast2)
  ty2 = infer_fn_def(eng2, ast2, r2.node)
  res2 = subst_resolve(eng2, ty2)
  n2 = pool_get(eng2, res2)
  assert_eq(n2.tag, ty_fn())
  ret2 = pool_get(eng2, n2.n2)
  assert_eq(ret2.tag, ty_int())
  eng3 = mk_engine()
  src3 = "f x = foo(x)"
  toks3 = tokenize(src3)
  ast3 = []
  r3 = parse_fn_def(src3, toks3, 0, ast3)
  ty3 = infer_fn_def(eng3, ast3, r3.node)
  n3 = pool_get(eng3, ty3)
  assert_eq(n3.tag, ty_fn())
  0


-- test test_ty_engine_init
test_ty_engine_init u =
  eng = mk_engine()
  assert_eq(glyph_array_len(eng.ty_pool), 0)
  assert_eq(eng.next_var[0], 0)
  assert_eq(glyph_array_len(eng.errors), 0)
  assert_eq(glyph_array_len(eng.env_names), 0)
  0

-- test test_ty_env_scope
test_ty_env_scope u =
  eng = mk_engine()
  pool = eng.ty_pool
  ti = mk_tint(pool)
  env_push(eng)
  env_insert(eng, "x", ti)
  found = env_lookup(eng, "x")
  assert(found >= 0)
  env_pop(eng)
  not_found = env_lookup(eng, "x")
  assert(not_found < 0)
  0

-- test test_ty_forall
test_ty_forall u =
  eng = mk_engine()
  v = subst_fresh(eng)
  id_ty = mk_tfn(v, v, eng.ty_pool)
  gen = generalize(eng, id_ty)
  gen_node = eng.ty_pool[gen]
  _ = gen_node.tag
  assert_eq(gen_node.tag, 16)
  assert_eq(glyph_array_len(gen_node.ns), 1)
  inst1 = instantiate(eng, gen)
  inst2 = instantiate(eng, gen)
  i = mk_tint(eng.ty_pool)
  n1a = eng.ty_pool[inst1]
  _ = n1a.tag
  unify(eng, n1a.n1, i)
  r1 = subst_resolve(eng, inst1)
  r1n = eng.ty_pool[r1]
  _ = r1n.tag
  assert_eq(r1n.tag, 10)
  r1_param = eng.ty_pool[r1n.n1]
  _ = r1_param.tag
  assert_eq(r1_param.tag, 1)
  n2a = eng.ty_pool[inst2]
  _ = n2a.tag
  w2 = subst_walk(eng, n2a.n1)
  w2n = eng.ty_pool[w2]
  _ = w2n.tag
  assert_eq(w2n.tag, 15)
  0

-- test test_ty_fresh_var
test_ty_fresh_var u =
  eng = mk_engine()
  t0 = subst_fresh(eng)
  t1 = subst_fresh(eng)
  assert_eq(eng.next_var[0], 2)
  assert_eq((eng.ty_pool[t0]).tag, ty_var())
  assert_eq((eng.ty_pool[t1]).tag, ty_var())
  assert(t0 != t1)
  0

-- test test_ty_infer_binary
test_ty_infer_binary u =
  eng = mk_engine()
  src = "f x y = x + y"
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  ty = infer_fn_def(eng, ast, r.node)
  pool = eng.ty_pool
  resolved = subst_walk(eng, ty)
  assert_eq((pool[resolved]).tag, ty_fn())
  0

-- test test_ty_infer_complex
test_ty_infer_complex u =
  eng1 = mk_engine()
  src1 = "f x = match x\n  1 -> 10\n  _ -> 0"
  toks1 = tokenize(src1)
  ast1 = []
  r1 = parse_fn_def(src1, toks1, 0, ast1)
  ty1 = infer_fn_def(eng1, ast1, r1.node)
  resolved1 = subst_resolve(eng1, ty1)
  pool1 = eng1.ty_pool
  node1 = pool_get(eng1, resolved1)
  assert_eq(node1.tag, ty_fn())
  ret1 = pool_get(eng1, node1.n2)
  assert_eq(ret1.tag, ty_int())
  eng2 = mk_engine()
  src2 = r"""f u = \{a: 1, b: 2}"""
  toks2 = tokenize(src2)
  ast2 = []
  r2 = parse_fn_def(src2, toks2, 0, ast2)
  ty2 = infer_fn_def(eng2, ast2, r2.node)
  node2 = pool_get(eng2, ty2)
  assert_eq(node2.tag, ty_fn())
  0


-- test test_ty_infer_exprs
test_ty_infer_exprs u =
  eng1 = mk_engine()
  src1 = "f u = [1, 2, 3]"
  toks1 = tokenize(src1)
  ast1 = []
  r1 = parse_fn_def(src1, toks1, 0, ast1)
  ty1 = infer_fn_def(eng1, ast1, r1.node)
  node1 = pool_get(eng1, ty1)
  assert_eq(node1.tag, ty_fn())
  eng2 = mk_engine()
  src2 = "f r = r.x"
  toks2 = tokenize(src2)
  ast2 = []
  r2 = parse_fn_def(src2, toks2, 0, ast2)
  ty2 = infer_fn_def(eng2, ast2, r2.node)
  node2 = pool_get(eng2, ty2)
  assert_eq(node2.tag, ty_fn())
  eng3 = mk_engine()
  src3 = "f a = a[0]"
  toks3 = tokenize(src3)
  ast3 = []
  r3 = parse_fn_def(src3, toks3, 0, ast3)
  ty3 = infer_fn_def(eng3, ast3, r3.node)
  node3 = pool_get(eng3, ty3)
  assert_eq(node3.tag, ty_fn())
  eng4 = mk_engine()
  src4 = "f x = 0 - x"
  toks4 = tokenize(src4)
  ast4 = []
  r4 = parse_fn_def(src4, toks4, 0, ast4)
  ty4 = infer_fn_def(eng4, ast4, r4.node)
  node4 = pool_get(eng4, ty4)
  assert_eq(node4.tag, ty_fn())
  0


-- test test_ty_infer_int
test_ty_infer_int u =
  eng = mk_engine()
  src = "f u = 42"
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  ty = infer_fn_def(eng, ast, r.node)
  pool = eng.ty_pool
  node = pool[ty]
  assert_eq(node.tag, ty_fn())
  ret = pool[node.n2]
  assert_eq(ret.tag, ty_int())
  0

-- test test_ty_infer_str
test_ty_infer_str u =
  eng = mk_engine()
  q = "\""
  src = s3("f u = ", q, s2("hi", q))
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  ty = infer_fn_def(eng, ast, r.node)
  pool = eng.ty_pool
  node = pool[ty]
  assert_eq(node.tag, ty_fn())
  ret = pool[node.n2]
  assert_eq(ret.tag, ty_str())
  0

-- test test_ty_lambda
test_ty_lambda u =
  eng = mk_engine()
  src = "f x = \\y -> x + y"
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  ty = infer_fn_def(eng, ast, r.node)
  resolved = subst_resolve(eng, ty)
  node = pool_get(eng, resolved)
  assert_eq(node.tag, ty_fn())
  eng2 = mk_engine()
  src2 = "f x y = x |> y"
  toks2 = tokenize(src2)
  ast2 = []
  r2 = parse_fn_def(src2, toks2, 0, ast2)
  ty2 = infer_fn_def(eng2, ast2, r2.node)
  node2 = pool_get(eng2, ty2)
  assert_eq(node2.tag, ty_fn())
  0


-- test test_ty_pipeline
test_ty_pipeline u =
  eng = mk_engine()
  src = "f x = x + 1"
  toks = tokenize(src)
  ast = []
  r = parse_fn_def(src, toks, 0, ast)
  ty = infer_fn_def(eng, ast, r.node)
  resolved = subst_resolve(eng, ty)
  pool = eng.ty_pool
  node = pool_get(eng, resolved)
  assert_eq(node.tag, ty_fn())
  param_ty = pool_get(eng, node.n1)
  ret_ty = pool_get(eng, node.n2)
  assert_eq(param_ty.tag, ty_int())
  assert_eq(ret_ty.tag, ty_int())
  0


-- test test_ty_primitives
test_ty_primitives u =
  eng = mk_engine()
  pool = eng.ty_pool
  ti = mk_tint(pool)
  ts = mk_tstr(pool)
  tb = mk_tbool(pool)
  tv = mk_tvoid(pool)
  assert_eq((pool[ti]).tag, ty_int())
  assert_eq((pool[ts]).tag, ty_str())
  assert_eq((pool[tb]).tag, ty_bool())
  assert_eq((pool[tv]).tag, ty_void())
  0

-- test test_ty_record_field
test_ty_record_field dummy =
  eng = mk_engine()
  register_builtins(eng)
  int_ty = mk_tint(eng.ty_pool)
  f1 = mk_tfield("pos", int_ty, eng.ty_pool)
  f2 = mk_tfield("val", int_ty, eng.ty_pool)
  fields = []
  glyph_array_push(fields, f1)
  glyph_array_push(fields, f2)
  rec_ty = mk_trecord(fields, 0 - 1, eng.ty_pool)
  result_ty = subst_fresh(eng)
  rest_var = subst_fresh_var(eng)
  access_field = mk_tfield("val", result_ty, eng.ty_pool)
  access_fields = []
  glyph_array_push(access_fields, access_field)
  access_rec = mk_trecord(access_fields, rest_var, eng.ty_pool)
  r = unify(eng, rec_ty, access_rec)
  assert(r >= 0)
  resolved = subst_resolve(eng, result_ty)
  resolved_node = pool_get(eng, resolved)
  _ = resolved_node.tag
  assert_eq(resolved_node.tag, ty_int())


-- test test_ty_unary
test_ty_unary u =
  eng1 = mk_engine()
  src1 = "f x = -x"
  toks1 = tokenize(src1)
  ast1 = []
  r1 = parse_fn_def(src1, toks1, 0, ast1)
  ty1 = infer_fn_def(eng1, ast1, r1.node)
  res1 = subst_resolve(eng1, ty1)
  n1 = pool_get(eng1, res1)
  assert_eq(n1.tag, ty_fn())
  eng2 = mk_engine()
  src2 = "f x y = x >> y"
  toks2 = tokenize(src2)
  ast2 = []
  r2 = parse_fn_def(src2, toks2, 0, ast2)
  ty2 = infer_fn_def(eng2, ast2, r2.node)
  n2 = pool_get(eng2, ty2)
  assert_eq(n2.tag, ty_fn())
  0


-- test test_ty_unify_array
test_ty_unify_array u =
  eng = mk_engine()
  pool = eng.ty_pool
  va = subst_fresh_var(eng)
  ti = mk_tint(pool)
  arr1 = mk_tarray(va, pool)
  arr2 = mk_tarray(ti, pool)
  unify(eng, arr1, arr2)
  assert_eq(glyph_array_len(eng.errors), 0)
  ra = subst_walk(eng, va)
  assert_eq((pool[ra]).tag, ty_int())
  0

-- test test_ty_unify_fn
test_ty_unify_fn u =
  eng = mk_engine()
  pool = eng.ty_pool
  va = subst_fresh(eng)
  vb = subst_fresh(eng)
  ti = mk_tint(pool)
  ts = mk_tstr(pool)
  fn1 = mk_tfn(va, ti, pool)
  fn2 = mk_tfn(ts, vb, pool)
  unify(eng, fn1, fn2)
  assert_eq(glyph_array_len(eng.errors), 0)
  ra = subst_walk(eng, va)
  rb = subst_walk(eng, vb)
  assert_eq((pool[ra]).tag, ty_str())
  assert_eq((pool[rb]).tag, ty_int())
  0

-- test test_ty_unify_same
test_ty_unify_same u =
  eng = mk_engine()
  pool = eng.ty_pool
  ti1 = mk_tint(pool)
  ti2 = mk_tint(pool)
  unify(eng, ti1, ti2)
  assert_eq(glyph_array_len(eng.errors), 0)
  0

-- test test_ty_unify_var
test_ty_unify_var u =
  eng = mk_engine()
  pool = eng.ty_pool
  v = subst_fresh_var(eng)
  ti = mk_tint(pool)
  unify(eng, v, ti)
  assert_eq(glyph_array_len(eng.errors), 0)
  resolved = subst_walk(eng, v)
  assert_eq((pool[resolved]).tag, ty_int())
  0

-- test test_type_constructors
test_type_constructors u =
  pool = []
  t1 = mk_terror(pool)
  node1 = pool[t1]
  assert_eq(node1.tag, ty_error())
  t2 = mk_tnever(pool)
  node2 = pool[t2]
  assert_eq(node2.tag, ty_never())
  t3 = mk_topt(0, pool)
  node3 = pool[t3]
  assert_eq(node3.tag, ty_opt())
  t4 = mk_ttuple([1, 2], pool)
  node4 = pool[t4]
  assert_eq(node4.tag, ty_tuple())
  t5 = mk_tnamed("Foo", [1], pool)
  node5 = pool[t5]
  assert_eq(node5.tag, ty_named())
  assert_str_eq(node5.sval, "Foo")
  assert_eq(ty_uint(), 2)
  assert_eq(ty_never(), 7)
  assert_eq(ty_named(), 17)
  assert_eq(ag_tuple(), 1)
  assert_eq(ag_variant(), 4)
  assert_eq(rv_str_interp(), 8)
  assert_eq(rv_unop(), 3)
  assert_eq(ok_extern_ref(), 7)
  assert_eq(st_assign(), 202)
  assert_eq(st_let_destr(), 203)
  assert_eq(tm_switch(), 3)
  f1 = mk_op_float("3.14")
  assert_eq(f1.okind, ok_const_float())
  assert_str_eq(f1.ostr, "3.14")
  e1 = mk_err(5)
  assert_eq(e1.node, 0 - 1)
  assert_eq(e1.pos, 5)
  parts_v = split_arrow("I -> V")
  assert_eq(sig_is_void_ret(parts_v), 1)
  parts_i = split_arrow("I -> I")
  assert_eq(sig_is_void_ret(parts_i), 0)
  assert(op_sub() != op_add())
  assert(op_div() != op_mul())
  assert(op_mod() != op_div())
  assert(op_eq() != op_neq())
  assert(op_gt() != op_lt())
  assert(op_gt_eq() != op_lt_eq())
  assert(op_and() != op_or())
  assert(op_not() != op_neg())
  assert(op_bitand() != op_bitor())
  assert(op_bitxor() != op_shl())
  assert(op_shr() != op_shl())
  assert(mir_and() != mir_or())
  assert(mir_not() != mir_neg())
  assert(mir_gt_eq() != mir_lt_eq())
  assert(mir_bitand() != mir_bitor())
  assert(mir_bitxor() != mir_shl())
  assert(mir_shr() != mir_shl())
  mir = compile_fn("f x = x + 1", [])
  assert(mir_block_count(mir) >= 1)
  assert(mir_local_count(mir) >= 1)
  assert(mir_stmt_count(mir, 0) >= 0)
  tk = mir_term_kind(mir, 0)
  assert(tk >= 1)
  0


-- test test_types_equal2
test_types_equal2 u =
  a = ["a", "b", "c"]
  b = ["a", "b", "c"]
  assert_eq(types_equal(a, b), 1)
  c = ["a", "b", "d"]
  assert_eq(types_equal(a, c), 0)
  d = ["a", "b"]
  assert_eq(types_equal(a, d), 0)
  assert_eq(types_equal([], []), 1)
  0


-- test test_util_extract_name
test_util_extract_name u =
  assert_str_eq(extract_name("foo x = x + 1"), "foo")
  assert_str_eq(extract_name("main = 42"), "main")
  assert_str_eq(extract_name("hello_world x y = 0"), "hello_world")
  0

-- test test_util_find_str
test_util_find_str u =
  arr = ["apple", "banana", "cherry"]
  assert_eq(find_str_in(arr, "banana", 0), 1)
  assert_eq(find_str_in(arr, "apple", 0), 0)
  assert_eq(find_str_in(arr, "cherry", 0), 2)
  assert_eq(find_str_in(arr, "grape", 0), 0 - 1)
  assert(has_str_in(arr, "banana"))
  assert(has_str_in(arr, "grape") == 0)
  assert(has_str_in([], "x") == 0)
  0

-- test test_util_glyph_prefix
test_util_glyph_prefix u =
  assert(has_glyph_prefix("glyph_println"))
  assert(has_glyph_prefix("glyph_alloc"))
  assert(has_glyph_prefix("glyph_") == 1)
  assert(has_glyph_prefix("foo") == 0)
  assert(has_glyph_prefix("glyp") == 0)
  assert(has_glyph_prefix("") == 0)
  0

-- test test_util_is_alnum
test_util_is_alnum u =
  assert(is_alnum(65))
  assert(is_alnum(122))
  assert(is_alnum(48))
  assert(is_alnum(57))
  assert(is_alnum(95))
  assert(is_alnum(32) == 0)
  assert(is_alnum(64) == 0)
  assert(is_alnum(123) == 0)
  0

-- test test_util_is_alpha
test_util_is_alpha u =
  assert(is_alpha(65))
  assert(is_alpha(90))
  assert(is_alpha(97))
  assert(is_alpha(122))
  assert(is_alpha(95))
  assert(is_alpha(0) == 0)
  assert(is_alpha(48) == 0)
  assert(is_alpha(57) == 0)
  assert(is_alpha(64) == 0)
  assert(is_alpha(91) == 0)
  assert(is_alpha(96) == 0)
  assert(is_alpha(123) == 0)
  0

-- test test_util_is_digit
test_util_is_digit u =
  assert(is_digit(48))
  assert(is_digit(57))
  assert(is_digit(50))
  assert(is_digit(47) == 0)
  assert(is_digit(58) == 0)
  assert(is_digit(65) == 0)
  assert(is_digit(0) == 0)
  0

-- test test_util_is_space
test_util_is_space u =
  assert(is_space(32))
  assert(is_space(9))
  assert(is_space(10) == 0)
  assert(is_space(65) == 0)
  assert(is_space(0) == 0)
  0

-- test test_util_is_upper
test_util_is_upper u =
  assert(is_upper(65))
  assert(is_upper(90))
  assert(is_upper(77))
  assert(is_upper(64) == 0)
  assert(is_upper(91) == 0)
  assert(is_upper(97) == 0)
  assert(is_upper(122) == 0)
  0

-- test test_util_keyword
test_util_keyword u =
  assert_eq(keyword_kind("match"), tk_match())
  assert_eq(keyword_kind("trait"), tk_trait())
  assert_eq(keyword_kind("impl"), tk_impl())
  assert_eq(keyword_kind("const"), tk_const())
  assert_eq(keyword_kind("extern"), tk_extern())
  assert_eq(keyword_kind("test"), tk_test())
  assert_eq(keyword_kind("as"), tk_as())
  assert_eq(keyword_kind("foo"), 0)
  assert_eq(keyword_kind("matcher"), 0)
  0

-- test test_util_sig_params
test_util_sig_params u =
  assert_eq(sig_param_count(split_arrow("V -> I")), 0)
  assert_eq(sig_param_count(split_arrow("I -> I")), 1)
  assert_eq(sig_param_count(split_arrow("I -> S -> I")), 2)
  assert_eq(sig_param_count(split_arrow("I -> I -> I -> S")), 3)
  0

-- test test_util_split_arrow
test_util_split_arrow u =
  parts = split_arrow("I -> S -> I")
  assert_eq(glyph_array_len(parts), 3)
  assert_str_eq(parts[0], "I")
  assert_str_eq(parts[1], "S")
  assert_str_eq(parts[2], "I")
  p2 = split_arrow("V -> I")
  assert_eq(glyph_array_len(p2), 2)
  assert_str_eq(p2[0], "V")
  assert_str_eq(p2[1], "I")
  p3 = split_arrow("")
  assert_eq(glyph_array_len(p3), 0)
  0

-- test test_util_str_lt
test_util_str_lt u =
  assert(str_lt("abc", "abd"))
  assert(str_lt("abc", "abcd"))
  assert(str_lt("", "a"))
  assert(str_lt("abc", "abc") == 0)
  assert(str_lt("abd", "abc") == 0)
  assert(str_lt("b", "a") == 0)
  assert(str_lt("", "") == 0)
  0

-- test test_validate_err
test_validate_err u =
  vr = validate_def("f x = =")
  assert_eq(vr.vr_ok, 0)
  assert(glyph_str_len(vr.vr_msg) > 0)
  0

-- test test_validate_extra
test_validate_extra u =
  r1 = validate_def("f x = x + 1")
  assert_eq(r1.vr_ok, 1)
  assert_str_eq(r1.vr_msg, "")
  r2 = validate_def("f = = =")
  assert_eq(r2.vr_ok, 0)
  assert(glyph_str_len(r2.vr_msg) > 0)
  0


-- test test_validate_ok
test_validate_ok u =
  vr = validate_def("f x = x + 1")
  assert_eq(vr.vr_ok, 1)
  assert_str_eq(vr.vr_msg, "")
  0

-- test test_variant_disc
test_variant_disc u =
  assert_eq(variant_discriminant("None"), 0)
  assert_eq(variant_discriminant("Some"), 1)
  assert_eq(variant_discriminant("Ok"), 0)
  assert_eq(variant_discriminant("Err"), 1)
  assert_eq(variant_discriminant("Left"), 0)
  assert_eq(variant_discriminant("Right"), 1)
  assert_eq(variant_discriminant("Unknown"), 0)
  0


-- type AstNode
AstNode = {kind:I ival:I sval:S n1:I n2:I n3:I ns:[I]}

-- type FPoint
FPoint = {fx: f64, fy: f64}


-- type JNode
JNode = {items:[I] keys:[S] nval:I sval:S tag:I}

-- type ParseResult
ParseResult = {node:I pos:I}

-- type Point2D
Point2D = {x: i32, y: i32}

-- type Token
Token = {kind:I start:I end:I line:I}

