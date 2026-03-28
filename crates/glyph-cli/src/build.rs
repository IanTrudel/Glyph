use std::collections::{HashMap, HashSet};
use std::path::Path;

use glyph_codegen::cranelift::CodegenContext;
use glyph_codegen::linker;
use glyph_codegen::runtime;
use glyph_db::{Database, DefKind, DefRow};
use glyph_mir::lower::{type_to_mir, MirLower};
use glyph_parse::lexer::Lexer;
use glyph_parse::parser::Parser;
use glyph_parse::span::format_diagnostic;
use glyph_typeck::infer::InferEngine;

/// Build (compile) the .glyph database.
pub fn cmd_build(path: &Path, full: bool, emit_mir: bool, target_gen: i64) -> miette::Result<()> {
    let db = Database::open(path).map_err(|e| miette::miette!("failed to open database: {e}"))?;

    // Get definitions to compile
    let defs = if full {
        db.effective_defs(target_gen).map_err(|e| miette::miette!("{e}"))?
    } else {
        db.dirty_defs_gen(target_gen).map_err(|e| miette::miette!("{e}"))?
    };

    if defs.is_empty() {
        eprintln!("Nothing to compile.");
        return Ok(());
    }

    eprintln!("Compiling {} definition(s)...", defs.len());

    // Phase 1: Parse all definitions
    let mut parsed_fns = Vec::new();
    let mut parsed_types = Vec::new();
    for def in &defs {
        match def.kind {
            DefKind::Fn => {
                match parse_def(def) {
                    Ok(parsed) => parsed_fns.push((def.clone(), parsed)),
                    Err(e) => report_parse_error(def, &e),
                }
            }
            DefKind::Type => {
                match parse_def(def) {
                    Ok(parsed) => parsed_types.push((def.clone(), parsed)),
                    Err(e) => report_parse_error(def, &e),
                }
            }
            _ => {}
        }
    }

    // Extract enum variant info from type definitions
    let enum_variants = extract_enum_variants(&parsed_types);

    // Phase 2: Type-check (two-pass for cross-function references)
    let mut infer = InferEngine::new();

    // Register enum variant constructors from type definitions
    for (_def_row, parsed) in &parsed_types {
        if let glyph_parse::ast::DefKind::Type(typedef) = &parsed.kind {
            if let glyph_parse::ast::TypeBody::Enum(variants) = &typedef.body {
                infer.register_enum(&_def_row.name, variants);
            }
        }
    }

    // Pass 1: Pre-register all function signatures so cross-references work
    for (def_row, parsed) in &parsed_fns {
        if let glyph_parse::ast::DefKind::Fn(fndef) = &parsed.kind {
            infer.pre_register_fn(&def_row.name, fndef);
        }
    }

    // Pass 2: Infer function bodies with all signatures available
    let mut inferred_fns = Vec::new();
    for (def_row, parsed) in &parsed_fns {
        if let glyph_parse::ast::DefKind::Fn(fndef) = &parsed.kind {
            let errors_before = infer.errors.len();
            let pre_ty = infer.env.lookup(&def_row.name).cloned();
            let fn_ty = infer.infer_fn_def(fndef);
            // Unify with pre-registered type to connect cross-function constraints
            if let Some(pre) = pre_ty {
                if let Err(e) = infer.subst.unify(&fn_ty, &pre) {
                    infer.errors.push(e);
                }
            }
            // Report errors from this function immediately with correct attribution
            for err in &infer.errors[errors_before..] {
                report_type_error(&def_row.name, &def_row.body, err);
            }
            inferred_fns.push((def_row, fndef, fn_ty));
        }
    }

    // Resolve all types after all inference is complete (so cross-function constraints propagate)
    let mut typed_fns = Vec::new();
    for (def_row, fndef, fn_ty) in inferred_fns {
        let resolved = infer.subst.resolve(&fn_ty);
        eprintln!("  {} : {resolved}", def_row.name);
        typed_fns.push((def_row, fndef, resolved));
    }

    // Build known function types map for MIR lowering
    let mut known_functions: HashMap<String, _> = typed_fns
        .iter()
        .map(|(def_row, _, resolved)| (def_row.name.clone(), type_to_mir(resolved)))
        .collect();
    add_runtime_known_functions(&mut known_functions);

    // Collect zero-arg function names for auto-call on bare reference
    let zero_arg_fns: HashSet<String> = typed_fns
        .iter()
        .filter(|(_, fndef, _)| fndef.params.is_empty())
        .map(|(def_row, _, _)| def_row.name.clone())
        .collect();

    // Phase 3: Lower to MIR
    let mut mir_fns = Vec::new();
    let mut lifted_fns = Vec::new();
    for (def_row, fndef, fn_ty) in &typed_fns {
        let mut lower = MirLower::new();
        lower.set_known_functions(known_functions.clone());
        lower.set_zero_arg_fns(zero_arg_fns.clone());
        // Register enum variants for constructor pattern matching
        for (type_name, variants) in &enum_variants {
            lower.register_enum(type_name, variants);
        }
        let mir = lower.lower_fn(&def_row.name, fndef, fn_ty);
        if emit_mir {
            eprintln!("{}", mir.display());
        }
        // Collect any lifted lambda functions
        for lfn in &lower.lifted_fns {
            if emit_mir {
                eprintln!("{}", lfn.display());
            }
        }
        lifted_fns.extend(lower.lifted_fns);
        mir_fns.push((def_row, mir));
    }

    // Phase 4: Codegen
    let mut codegen = CodegenContext::new();

    // Declare all functions first (user + lifted)
    for (_def_row, mir) in &mir_fns {
        codegen.declare_function(mir);
    }
    for lfn in &lifted_fns {
        codegen.declare_function(lfn);
    }

    // Declare extern functions
    let externs = db.all_externs().map_err(|e| miette::miette!("{e}"))?;
    for ext in &externs {
        let sig = build_extern_sig(&codegen, &ext.sig);
        codegen.declare_extern(&ext.name, &ext.symbol, &sig);
    }

    // Declare runtime functions
    declare_runtime(&mut codegen);

    // Register all record types for field offset resolution (row polymorphism)
    {
        let all_fns: Vec<&glyph_mir::ir::MirFunction> = mir_fns.iter().map(|(_, m)| m).chain(lifted_fns.iter()).collect();
        codegen.register_record_types(&all_fns);
    }

    // Compile all functions (user + lifted)
    for (_def_row, mir) in &mir_fns {
        codegen.compile_function(mir);
    }
    for lfn in &lifted_fns {
        codegen.compile_function(lfn);
    }

    // Emit object file
    let object_bytes = codegen.finish();

    // Link
    let exe_path = path.with_extension("");
    let mut extern_libs: Vec<String> = externs
        .iter()
        .filter_map(|e| e.lib.clone())
        .collect();
    extern_libs.sort();
    extern_libs.dedup();

    // Include SQLite runtime if sqlite3 is linked
    let mut extra_c_sources: Vec<(&str, &str)> = Vec::new();
    if extern_libs.iter().any(|l| l == "sqlite3") {
        extra_c_sources.push(("runtime_sqlite.c", runtime::RUNTIME_SQLITE_C));
    }

    linker::link_with_extras(&object_bytes, &exe_path, &extern_libs, Some(runtime::RUNTIME_C), &extra_c_sources)
        .map_err(|e| miette::miette!("link failed: {e}"))?;

    // Mark definitions as compiled
    for (def_row, _) in &mir_fns {
        db.mark_compiled(def_row.id)
            .map_err(|e| miette::miette!("{e}"))?;
    }

    eprintln!("Built {}", exe_path.display());
    Ok(())
}

/// Type-check only.
pub fn cmd_check(path: &Path, target_gen: i64) -> miette::Result<()> {
    let db = Database::open(path).map_err(|e| miette::miette!("failed to open database: {e}"))?;

    let defs = db.effective_defs(target_gen).map_err(|e| miette::miette!("{e}"))?;

    if defs.is_empty() {
        eprintln!("No definitions found.");
        return Ok(());
    }

    // Parse all definitions
    let mut parsed_fns = Vec::new();
    let mut parsed_types = Vec::new();
    let mut error_count = 0;
    for def in &defs {
        match def.kind {
            DefKind::Fn => {
                match parse_def(def) {
                    Ok(parsed) => parsed_fns.push((def.clone(), parsed)),
                    Err(e) => {
                        report_parse_error(def, &e);
                        error_count += 1;
                    }
                }
            }
            DefKind::Type => {
                match parse_def(def) {
                    Ok(parsed) => parsed_types.push((def.clone(), parsed)),
                    Err(e) => {
                        report_parse_error(def, &e);
                        error_count += 1;
                    }
                }
            }
            _ => {}
        }
    }

    let mut infer = InferEngine::new();

    // Register enum variant constructors from type definitions
    for (_def_row, parsed) in &parsed_types {
        if let glyph_parse::ast::DefKind::Type(typedef) = &parsed.kind {
            if let glyph_parse::ast::TypeBody::Enum(variants) = &typedef.body {
                infer.register_enum(&_def_row.name, variants);
            }
        }
    }

    // Pass 1: Pre-register all function signatures
    for (def_row, parsed) in &parsed_fns {
        if let glyph_parse::ast::DefKind::Fn(fndef) = &parsed.kind {
            infer.pre_register_fn(&def_row.name, fndef);
        }
    }

    // Pass 2: Infer function bodies
    for (def_row, parsed) in &parsed_fns {
        if let glyph_parse::ast::DefKind::Fn(fndef) = &parsed.kind {
            let errors_before = infer.errors.len();
            let pre_ty = infer.env.lookup(&def_row.name).cloned();
            let fn_ty = infer.infer_fn_def(fndef);
            if let Some(pre) = pre_ty {
                if let Err(e) = infer.subst.unify(&fn_ty, &pre) {
                    infer.errors.push(e);
                }
            }
            // Report errors from this function immediately with correct attribution
            for err in &infer.errors[errors_before..] {
                report_type_error(&def_row.name, &def_row.body, err);
            }
            let resolved = infer.subst.resolve(&fn_ty);
            eprintln!("  {} : {resolved}", def_row.name);
        }
    }

    error_count += infer.errors.len();

    if error_count > 0 {
        Err(miette::miette!("{error_count} error(s) found"))
    } else {
        eprintln!("OK");
        Ok(())
    }
}

// ── Helpers ──────────────────────────────────────────────────────────

/// Extract enum variant info from parsed type definitions.
/// Returns a map from type_name -> Vec<(variant_name, field_types)>.
fn extract_enum_variants(
    parsed_types: &[(DefRow, glyph_parse::ast::Def)],
) -> Vec<(String, Vec<(String, Vec<glyph_mir::ir::MirType>)>)> {
    let mut result = Vec::new();
    for (def_row, parsed) in parsed_types {
        if let glyph_parse::ast::DefKind::Type(typedef) = &parsed.kind {
            if let glyph_parse::ast::TypeBody::Enum(variants) = &typedef.body {
                let mut variant_list = Vec::new();
                for v in variants {
                    let field_types = match &v.fields {
                        glyph_parse::ast::VariantFields::None => vec![],
                        glyph_parse::ast::VariantFields::Positional(types) => {
                            types.iter().map(|t| ast_type_to_mir(t)).collect()
                        }
                        glyph_parse::ast::VariantFields::Named(fields) => {
                            fields.iter().map(|f| ast_type_to_mir(&f.ty)).collect()
                        }
                    };
                    variant_list.push((v.name.clone(), field_types));
                }
                result.push((def_row.name.clone(), variant_list));
            }
        }
    }
    result
}

/// Convert an AST TypeExpr to a MirType.
fn ast_type_to_mir(texpr: &glyph_parse::ast::TypeExpr) -> glyph_mir::ir::MirType {
    use glyph_mir::ir::MirType;
    use glyph_parse::ast::TypeExprKind;
    match &texpr.kind {
        TypeExprKind::Named(name) => match name.as_str() {
            "I" | "Int" | "I64" => MirType::Int,
            "U" | "UInt" | "U64" => MirType::UInt,
            "F" | "Float" | "F64" => MirType::Float,
            "S" | "Str" => MirType::Str,
            "B" | "Bool" => MirType::Bool,
            "V" | "Void" => MirType::Void,
            "N" | "Never" => MirType::Never,
            other => MirType::Named(other.to_string()),
        },
        TypeExprKind::Arr(inner) => MirType::Array(Box::new(ast_type_to_mir(inner))),
        TypeExprKind::Opt(inner) => {
            // Optional<T> = enum with None/Some(T) variants
            let inner_ty = ast_type_to_mir(inner);
            MirType::Enum("Option".to_string(), vec![
                ("None".to_string(), vec![]),
                ("Some".to_string(), vec![inner_ty]),
            ])
        }
        TypeExprKind::Ref(inner) => MirType::Ref(Box::new(ast_type_to_mir(inner))),
        TypeExprKind::Ptr(inner) => MirType::Ptr(Box::new(ast_type_to_mir(inner))),
        TypeExprKind::Fn(param, ret) => {
            MirType::Fn(Box::new(ast_type_to_mir(param)), Box::new(ast_type_to_mir(ret)))
        }
        TypeExprKind::Tuple(elems) => {
            MirType::Tuple(elems.iter().map(|e| ast_type_to_mir(e)).collect())
        }
        TypeExprKind::Record(fields, _) => {
            let map = fields.iter()
                .map(|(name, ty)| (name.clone(), ast_type_to_mir(ty)))
                .collect();
            MirType::Record(map)
        }
        _ => MirType::Int, // fallback
    }
}

fn parse_def(def: &DefRow) -> Result<glyph_parse::ast::Def, glyph_parse::error::ParseError> {
    let tokens = Lexer::new(&def.body).tokenize();
    let mut parser = Parser::new(tokens);
    parser.parse_def(&def.name, def.kind.as_str())
}

fn report_parse_error(def: &DefRow, e: &glyph_parse::error::ParseError) {
    let msg = format_diagnostic(&def.name, &def.body, e.span(), "error", &e.to_string());
    eprintln!("  {msg}");
}

fn report_type_error(def_name: &str, body: &str, e: &glyph_typeck::error::TypeError) {
    if let Some(span) = e.span() {
        let msg = format_diagnostic(def_name, body, span, "type error", &e.to_string());
        eprintln!("  {msg}");
    } else {
        eprintln!("  type error: {e}");
    }
}

fn build_extern_sig(
    _codegen: &CodegenContext,
    sig_str: &str,
) -> cranelift_codegen::ir::Signature {
    // Parse Glyph type signature like "I -> I -> I" or "S -> I -> V"
    use cranelift_codegen::ir::{AbiParam, types};
    use cranelift_codegen::isa::CallConv;
    let mut sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);

    if sig_str.is_empty() {
        sig.returns.push(AbiParam::new(types::I64));
        return sig;
    }

    // Split by " -> " to get components
    let parts: Vec<&str> = sig_str.split(" -> ").collect();
    if parts.len() < 2 {
        // No arrow — treat as return type only
        sig.returns.push(AbiParam::new(glyph_type_char_to_clif(parts[0].trim())));
        return sig;
    }

    // All parts except the last are params, last is return type
    for p in &parts[..parts.len() - 1] {
        sig.params.push(AbiParam::new(glyph_type_char_to_clif(p.trim())));
    }
    let ret = parts.last().unwrap().trim();
    if ret != "V" {
        sig.returns.push(AbiParam::new(glyph_type_char_to_clif(ret)));
    }
    sig
}

fn glyph_type_char_to_clif(s: &str) -> cranelift_codegen::ir::Type {
    use cranelift_codegen::ir::types;
    match s {
        "I" | "Int" | "I64" => types::I64,
        "U" | "UInt" | "U64" => types::I64,
        "F" | "Float" | "F64" => types::F64,
        "B" | "Bool" => types::I8,
        "S" | "Str" => types::I64,  // pointer to str struct
        "V" | "Void" => types::I64, // shouldn't be used for params
        _ => types::I64, // default to i64 for pointers, arrays, etc.
    }
}

fn declare_runtime(codegen: &mut CodegenContext) {
    use cranelift_codegen::ir::{AbiParam, types};
    use cranelift_codegen::isa::CallConv;

    // glyph_panic(msg: *const u8) -> void
    let mut panic_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    panic_sig.params.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_PANIC, runtime::RT_PANIC, &panic_sig);

    // glyph_panic_str(str_struct: *void) -> void
    let mut panic_str_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    panic_str_sig.params.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_PANIC_STR, runtime::RT_PANIC_STR, &panic_str_sig);

    // glyph_alloc(size: u64) -> *void
    let mut alloc_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    alloc_sig.params.push(AbiParam::new(types::I64));
    alloc_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_ALLOC, runtime::RT_ALLOC, &alloc_sig);

    // glyph_dealloc(ptr: *void) -> void
    let mut dealloc_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    dealloc_sig.params.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_DEALLOC, runtime::RT_DEALLOC, &dealloc_sig);

    // glyph_print(str_struct: *void) -> i64 (returns length)
    let mut print_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    print_sig.params.push(AbiParam::new(types::I64));
    print_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("print", runtime::RT_PRINT, &print_sig);

    // glyph_str_concat(a: *void, b: *void) -> *void
    let mut concat_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    concat_sig.params.push(AbiParam::new(types::I64));
    concat_sig.params.push(AbiParam::new(types::I64));
    concat_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_STR_CONCAT, runtime::RT_STR_CONCAT, &concat_sig);

    // glyph_int_to_str(n: i64) -> *void (returns str struct pointer)
    let mut int_to_str_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    int_to_str_sig.params.push(AbiParam::new(types::I64));
    int_to_str_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_INT_TO_STR, runtime::RT_INT_TO_STR, &int_to_str_sig);

    // glyph_array_bounds_check(index: i64, len: i64) -> void
    let mut bounds_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    bounds_sig.params.push(AbiParam::new(types::I64));
    bounds_sig.params.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_ARRAY_BOUNDS_CHECK, runtime::RT_ARRAY_BOUNDS_CHECK, &bounds_sig);

    // glyph_array_new(cap: i64) -> *void
    let mut array_new_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    array_new_sig.params.push(AbiParam::new(types::I64));
    array_new_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_ARRAY_NEW, runtime::RT_ARRAY_NEW, &array_new_sig);

    // glyph_array_push(header_ptr: *void, value: i64) -> *void
    let mut array_push_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    array_push_sig.params.push(AbiParam::new(types::I64));
    array_push_sig.params.push(AbiParam::new(types::I64));
    array_push_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_ARRAY_PUSH, runtime::RT_ARRAY_PUSH, &array_push_sig);

    // glyph_realloc(ptr: *void, size: i64) -> *void
    let mut realloc_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    realloc_sig.params.push(AbiParam::new(types::I64));
    realloc_sig.params.push(AbiParam::new(types::I64));
    realloc_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_REALLOC, runtime::RT_REALLOC, &realloc_sig);

    // glyph_str_eq(a: *void, b: *void) -> i64
    let mut str_eq_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    str_eq_sig.params.push(AbiParam::new(types::I64));
    str_eq_sig.params.push(AbiParam::new(types::I64));
    str_eq_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_STR_EQ, runtime::RT_STR_EQ, &str_eq_sig);

    // glyph_str_len(str: *void) -> i64
    let mut str_len_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    str_len_sig.params.push(AbiParam::new(types::I64));
    str_len_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_STR_LEN, runtime::RT_STR_LEN, &str_len_sig);

    // glyph_str_slice(str: *void, start: i64, end: i64) -> *void
    let mut str_slice_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    str_slice_sig.params.push(AbiParam::new(types::I64));
    str_slice_sig.params.push(AbiParam::new(types::I64));
    str_slice_sig.params.push(AbiParam::new(types::I64));
    str_slice_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_STR_SLICE, runtime::RT_STR_SLICE, &str_slice_sig);

    // glyph_str_char_at(str: *void, index: i64) -> i64
    let mut char_at_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    char_at_sig.params.push(AbiParam::new(types::I64));
    char_at_sig.params.push(AbiParam::new(types::I64));
    char_at_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_STR_CHAR_AT, runtime::RT_STR_CHAR_AT, &char_at_sig);

    // glyph_read_file(path: *void) -> *void
    let mut read_file_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    read_file_sig.params.push(AbiParam::new(types::I64));
    read_file_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_READ_FILE, runtime::RT_READ_FILE, &read_file_sig);

    // glyph_write_file(path: *void, content: *void) -> i64
    let mut write_file_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    write_file_sig.params.push(AbiParam::new(types::I64));
    write_file_sig.params.push(AbiParam::new(types::I64));
    write_file_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_WRITE_FILE, runtime::RT_WRITE_FILE, &write_file_sig);

    // glyph_exit(code: i64) -> void
    let mut exit_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    exit_sig.params.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_EXIT, runtime::RT_EXIT, &exit_sig);

    // glyph_args() -> *void (array header)
    let mut args_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    args_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_ARGS, runtime::RT_ARGS, &args_sig);

    // glyph_println(str: *void) -> i64
    let mut println_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    println_sig.params.push(AbiParam::new(types::I64));
    println_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("println", runtime::RT_PRINTLN, &println_sig);

    // glyph_eprintln(str: *void) -> i64
    let mut eprintln_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    eprintln_sig.params.push(AbiParam::new(types::I64));
    eprintln_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("eprintln", runtime::RT_EPRINTLN, &eprintln_sig);

    // glyph_str_to_cstr(str: *void) -> *char
    let mut str_to_cstr_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    str_to_cstr_sig.params.push(AbiParam::new(types::I64));
    str_to_cstr_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_STR_TO_CSTR, runtime::RT_STR_TO_CSTR, &str_to_cstr_sig);

    // glyph_cstr_to_str(cstr: *char) -> *void
    let mut cstr_to_str_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    cstr_to_str_sig.params.push(AbiParam::new(types::I64));
    cstr_to_str_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_CSTR_TO_STR, runtime::RT_CSTR_TO_STR, &cstr_to_str_sig);

    // glyph_array_set(header: *void, index: i64, value: i64) -> i64
    let mut array_set_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    array_set_sig.params.push(AbiParam::new(types::I64));
    array_set_sig.params.push(AbiParam::new(types::I64));
    array_set_sig.params.push(AbiParam::new(types::I64));
    array_set_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_ARRAY_SET, runtime::RT_ARRAY_SET, &array_set_sig);

    // glyph_raw_set(ptr: i64, idx: i64, val: i64) -> i64
    let mut raw_set_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    raw_set_sig.params.push(AbiParam::new(types::I64));
    raw_set_sig.params.push(AbiParam::new(types::I64));
    raw_set_sig.params.push(AbiParam::new(types::I64));
    raw_set_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_RAW_SET, runtime::RT_RAW_SET, &raw_set_sig);

    // glyph_array_pop(header: *void) -> i64
    let mut array_pop_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    array_pop_sig.params.push(AbiParam::new(types::I64));
    array_pop_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_ARRAY_POP, runtime::RT_ARRAY_POP, &array_pop_sig);

    // glyph_array_len(header: *void) -> i64
    let mut array_len_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    array_len_sig.params.push(AbiParam::new(types::I64));
    array_len_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_ARRAY_LEN, runtime::RT_ARRAY_LEN, &array_len_sig);

    // glyph_arr_get_str(header: *void, index: i64) -> i64
    let mut arr_get_str_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    arr_get_str_sig.params.push(AbiParam::new(types::I64));
    arr_get_str_sig.params.push(AbiParam::new(types::I64));
    arr_get_str_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_ARR_GET_STR, runtime::RT_ARR_GET_STR, &arr_get_str_sig);

    // glyph_str_to_int(str: *void) -> i64
    let mut str_to_int_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    str_to_int_sig.params.push(AbiParam::new(types::I64));
    str_to_int_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_STR_TO_INT, runtime::RT_STR_TO_INT, &str_to_int_sig);

    // glyph_system(cmd: *void) -> i64
    let mut system_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    system_sig.params.push(AbiParam::new(types::I64));
    system_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_SYSTEM, runtime::RT_SYSTEM, &system_sig);

    // glyph_sb_new() -> *void
    let mut sb_new_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    sb_new_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_SB_NEW, runtime::RT_SB_NEW, &sb_new_sig);

    // glyph_sb_append(sb: *void, str: *void) -> *void
    let mut sb_append_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    sb_append_sig.params.push(AbiParam::new(types::I64));
    sb_append_sig.params.push(AbiParam::new(types::I64));
    sb_append_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_SB_APPEND, runtime::RT_SB_APPEND, &sb_append_sig);

    // glyph_sb_build(sb: *void) -> *void
    let mut sb_build_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    sb_build_sig.params.push(AbiParam::new(types::I64));
    sb_build_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_SB_BUILD, runtime::RT_SB_BUILD, &sb_build_sig);

    // glyph_read_line(dummy: i64) -> *void (string)
    let mut read_line_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    read_line_sig.params.push(AbiParam::new(types::I64));
    read_line_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("read_line", runtime::RT_READ_LINE, &read_line_sig);

    // glyph_flush(dummy: i64) -> i64
    let mut flush_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    flush_sig.params.push(AbiParam::new(types::I64));
    flush_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("flush", runtime::RT_FLUSH, &flush_sig);

    // glyph_bitset_new(capacity: i64) -> i64
    let mut bitset_new_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    bitset_new_sig.params.push(AbiParam::new(types::I64));
    bitset_new_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("bitset_new", runtime::RT_BITSET_NEW, &bitset_new_sig);

    // glyph_bitset_set(bs: i64, idx: i64) -> i64
    let mut bitset_set_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    bitset_set_sig.params.push(AbiParam::new(types::I64));
    bitset_set_sig.params.push(AbiParam::new(types::I64));
    bitset_set_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("bitset_set", runtime::RT_BITSET_SET, &bitset_set_sig);

    // glyph_bitset_test(bs: i64, idx: i64) -> i64
    let mut bitset_test_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    bitset_test_sig.params.push(AbiParam::new(types::I64));
    bitset_test_sig.params.push(AbiParam::new(types::I64));
    bitset_test_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("bitset_test", runtime::RT_BITSET_TEST, &bitset_test_sig);

    // glyph_array_freeze(hdr: i64) -> i64
    let mut arr_freeze_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    arr_freeze_sig.params.push(AbiParam::new(types::I64));
    arr_freeze_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("array_freeze", runtime::RT_ARRAY_FREEZE, &arr_freeze_sig);

    // glyph_array_frozen(hdr: i64) -> i64
    let mut arr_frozen_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    arr_frozen_sig.params.push(AbiParam::new(types::I64));
    arr_frozen_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("array_frozen", runtime::RT_ARRAY_FROZEN, &arr_frozen_sig);

    // glyph_array_thaw(hdr: i64) -> i64
    let mut arr_thaw_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    arr_thaw_sig.params.push(AbiParam::new(types::I64));
    arr_thaw_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("array_thaw", runtime::RT_ARRAY_THAW, &arr_thaw_sig);

    // glyph_hm_freeze(hdr: i64) -> i64
    let mut hm_freeze_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    hm_freeze_sig.params.push(AbiParam::new(types::I64));
    hm_freeze_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("hm_freeze", runtime::RT_HM_FREEZE, &hm_freeze_sig);

    // glyph_hm_frozen(hdr: i64) -> i64
    let mut hm_frozen_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    hm_frozen_sig.params.push(AbiParam::new(types::I64));
    hm_frozen_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("hm_frozen", runtime::RT_HM_FROZEN, &hm_frozen_sig);

    // glyph_ref(val: i64) -> i64
    let mut ref_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    ref_sig.params.push(AbiParam::new(types::I64));
    ref_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("ref", runtime::RT_REF, &ref_sig);

    // glyph_deref(r: i64) -> i64
    let mut deref_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    deref_sig.params.push(AbiParam::new(types::I64));
    deref_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("deref", runtime::RT_DEREF, &deref_sig);

    // glyph_set_ref(r: i64, val: i64) -> i64
    let mut set_ref_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    set_ref_sig.params.push(AbiParam::new(types::I64));
    set_ref_sig.params.push(AbiParam::new(types::I64));
    set_ref_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("set_ref", runtime::RT_SET_REF, &set_ref_sig);

    // glyph_generate(n: i64, fn: i64) -> i64
    let mut generate_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    generate_sig.params.push(AbiParam::new(types::I64));
    generate_sig.params.push(AbiParam::new(types::I64));
    generate_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("generate", runtime::RT_GENERATE, &generate_sig);

    // glyph_file_exists(path: i64) -> i64
    let mut file_exists_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    file_exists_sig.params.push(AbiParam::new(types::I64));
    file_exists_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern("file_exists", runtime::RT_FILE_EXISTS, &file_exists_sig);

    // Note: SQLite wrapper functions (glyph_db_*) are NOT declared here.
    // They come from extern_ table rows in the .glyph database.
}

/// Add runtime function types to the known_functions map for MIR lowering.
fn add_runtime_known_functions(known_functions: &mut HashMap<String, glyph_mir::ir::MirType>) {
    use glyph_mir::ir::MirType;
    known_functions.insert("print".to_string(),
        MirType::Fn(Box::new(MirType::Str), Box::new(MirType::Int)));
    known_functions.insert(runtime::RT_STR_CONCAT.to_string(),
        MirType::Fn(Box::new(MirType::Str), Box::new(MirType::Fn(Box::new(MirType::Str), Box::new(MirType::Str)))));
    known_functions.insert(runtime::RT_INT_TO_STR.to_string(),
        MirType::Fn(Box::new(MirType::Int), Box::new(MirType::Str)));
    known_functions.insert(runtime::RT_SB_NEW.to_string(),
        MirType::Fn(Box::new(MirType::Void), Box::new(MirType::Ptr(Box::new(MirType::Void)))));
    known_functions.insert(runtime::RT_SB_APPEND.to_string(),
        MirType::Fn(Box::new(MirType::Ptr(Box::new(MirType::Void))),
            Box::new(MirType::Fn(Box::new(MirType::Str), Box::new(MirType::Ptr(Box::new(MirType::Void)))))));
    known_functions.insert(runtime::RT_SB_BUILD.to_string(),
        MirType::Fn(Box::new(MirType::Ptr(Box::new(MirType::Void))), Box::new(MirType::Str)));
}
