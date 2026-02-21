use std::path::Path;

use glyph_codegen::cranelift::CodegenContext;
use glyph_codegen::linker;
use glyph_codegen::runtime;
use glyph_db::{Database, DefKind, DefRow};
use glyph_mir::lower::MirLower;
use glyph_parse::lexer::Lexer;
use glyph_parse::parser::Parser;
use glyph_typeck::infer::InferEngine;

/// Build (compile) the .glyph database.
pub fn cmd_build(path: &Path, full: bool, emit_mir: bool) -> miette::Result<()> {
    let db = Database::open(path).map_err(|e| miette::miette!("failed to open database: {e}"))?;

    // Get definitions to compile
    let defs = if full {
        db.all_defs().map_err(|e| miette::miette!("{e}"))?
    } else {
        db.dirty_defs().map_err(|e| miette::miette!("{e}"))?
    };

    if defs.is_empty() {
        eprintln!("Nothing to compile.");
        return Ok(());
    }

    eprintln!("Compiling {} definition(s)...", defs.len());

    // Phase 1: Parse all definitions
    let mut parsed_fns = Vec::new();
    for def in &defs {
        if def.kind != DefKind::Fn {
            continue;
        }
        match parse_def(def) {
            Ok(parsed) => parsed_fns.push((def.clone(), parsed)),
            Err(e) => {
                eprintln!("  parse error in '{}': {e}", def.name);
            }
        }
    }

    // Phase 2: Type-check
    let mut infer = InferEngine::new();
    let mut typed_fns = Vec::new();
    for (def_row, parsed) in &parsed_fns {
        if let glyph_parse::ast::DefKind::Fn(fndef) = &parsed.kind {
            let fn_ty = infer.infer_fn_def(fndef);
            let resolved = infer.subst.resolve(&fn_ty);
            eprintln!("  {} : {resolved}", def_row.name);
            typed_fns.push((def_row, fndef, resolved));
        }
    }

    if !infer.errors.is_empty() {
        for err in &infer.errors {
            eprintln!("  type error: {err}");
        }
        // Continue anyway for now
    }

    // Phase 3: Lower to MIR
    let mut mir_fns = Vec::new();
    for (def_row, fndef, fn_ty) in &typed_fns {
        let mut lower = MirLower::new();
        let mir = lower.lower_fn(&def_row.name, fndef, fn_ty);
        if emit_mir {
            eprintln!("{}", mir.display());
        }
        mir_fns.push((def_row, mir));
    }

    // Phase 4: Codegen
    let mut codegen = CodegenContext::new();

    // Declare all functions first
    for (_def_row, mir) in &mir_fns {
        codegen.declare_function(mir);
    }

    // Declare extern functions
    let externs = db.all_externs().map_err(|e| miette::miette!("{e}"))?;
    for ext in &externs {
        let sig = build_extern_sig(&codegen, &ext.sig);
        codegen.declare_extern(&ext.name, &ext.symbol, &sig);
    }

    // Declare runtime functions
    declare_runtime(&mut codegen);

    // Compile all functions
    for (_def_row, mir) in &mir_fns {
        codegen.compile_function(mir);
    }

    // Emit object file
    let object_bytes = codegen.finish();

    // Link
    let exe_path = path.with_extension("");
    let extern_libs: Vec<String> = externs
        .iter()
        .filter_map(|e| e.lib.clone())
        .collect();

    linker::link(&object_bytes, &exe_path, &extern_libs, Some(runtime::RUNTIME_C))
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
pub fn cmd_check(path: &Path) -> miette::Result<()> {
    let db = Database::open(path).map_err(|e| miette::miette!("failed to open database: {e}"))?;

    let defs = db.all_defs().map_err(|e| miette::miette!("{e}"))?;

    if defs.is_empty() {
        eprintln!("No definitions found.");
        return Ok(());
    }

    let mut infer = InferEngine::new();
    let mut error_count = 0;

    for def in &defs {
        if def.kind != DefKind::Fn {
            continue;
        }
        match parse_def(def) {
            Ok(parsed) => {
                if let glyph_parse::ast::DefKind::Fn(fndef) = &parsed.kind {
                    let fn_ty = infer.infer_fn_def(fndef);
                    let resolved = infer.subst.resolve(&fn_ty);
                    eprintln!("  {} : {resolved}", def.name);
                }
            }
            Err(e) => {
                eprintln!("  parse error in '{}': {e}", def.name);
                error_count += 1;
            }
        }
    }

    if !infer.errors.is_empty() {
        for err in &infer.errors {
            eprintln!("  type error: {err}");
        }
        error_count += infer.errors.len();
    }

    if error_count > 0 {
        Err(miette::miette!("{error_count} error(s) found"))
    } else {
        eprintln!("OK");
        Ok(())
    }
}

// ── Helpers ──────────────────────────────────────────────────────────

fn parse_def(def: &DefRow) -> Result<glyph_parse::ast::Def, String> {
    let tokens = Lexer::new(&def.body).tokenize();
    let mut parser = Parser::new(tokens);
    parser
        .parse_def(&def.name, def.kind.as_str())
        .map_err(|e| format!("{e}"))
}

fn build_extern_sig(
    _codegen: &CodegenContext,
    _sig_str: &str,
) -> cranelift_codegen::ir::Signature {
    // Simplified: create a C calling convention signature
    // In a full implementation, we'd parse the Glyph type signature
    use cranelift_codegen::ir::{AbiParam, types};
    use cranelift_codegen::isa::CallConv;
    let mut sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    // Default to () -> i64 — proper parsing will be added
    sig.returns.push(AbiParam::new(types::I64));
    sig
}

fn declare_runtime(codegen: &mut CodegenContext) {
    use cranelift_codegen::ir::{AbiParam, types};
    use cranelift_codegen::isa::CallConv;

    // glyph_panic(msg: *const u8) -> void
    let mut panic_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    panic_sig.params.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_PANIC, runtime::RT_PANIC, &panic_sig);

    // glyph_alloc(size: u64) -> *void
    let mut alloc_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    alloc_sig.params.push(AbiParam::new(types::I64));
    alloc_sig.returns.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_ALLOC, runtime::RT_ALLOC, &alloc_sig);

    // glyph_dealloc(ptr: *void) -> void
    let mut dealloc_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    dealloc_sig.params.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_DEALLOC, runtime::RT_DEALLOC, &dealloc_sig);

    // glyph_print(msg: *const u8, len: i64) -> void
    let mut print_sig = cranelift_codegen::ir::Signature::new(CallConv::SystemV);
    print_sig.params.push(AbiParam::new(types::I64));
    print_sig.params.push(AbiParam::new(types::I64));
    codegen.declare_extern(runtime::RT_PRINT, runtime::RT_PRINT, &print_sig);
}
