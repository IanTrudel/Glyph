use cranelift_codegen::ir::types;
use cranelift_codegen::ir::Type as ClifType;
use glyph_mir::ir::MirType;

/// Convert a MIR type to its Cranelift IR type.
pub fn mir_to_clif(ty: &MirType) -> ClifType {
    match ty {
        MirType::Bool => types::I8,
        MirType::Int32 | MirType::Float32 => types::I32,
        MirType::Int | MirType::UInt => types::I64,
        MirType::Float => types::F64,
        MirType::Void => types::I64, // represent void as i64 (unused)
        MirType::Ptr(_) | MirType::Ref(_) | MirType::Fn(_, _) => types::I64, // pointer-sized
        MirType::Str => types::I64,  // pointer to fat ptr (simplified)
        MirType::Array(_) => types::I64, // pointer (simplified)
        MirType::Never => types::I64,
        // Compound types passed as pointers
        MirType::Tuple(_) | MirType::Record(_) | MirType::Enum(_, _) | MirType::ClosureEnv(_) => {
            types::I64
        }
        MirType::Named(_) => types::I64,
    }
}

/// Check if a MIR type is a floating point type.
pub fn is_float(ty: &MirType) -> bool {
    matches!(ty, MirType::Float | MirType::Float32)
}

/// Size in bytes of a MIR type.
pub fn size_of(ty: &MirType) -> u32 {
    match ty {
        MirType::Bool => 1,
        MirType::Int32 | MirType::Float32 => 4,
        MirType::Int | MirType::UInt | MirType::Float => 8,
        MirType::Void => 0,
        MirType::Ptr(_) | MirType::Ref(_) | MirType::Fn(_, _) => 8,
        MirType::Str => 16,
        MirType::Array(_) => 24,
        MirType::Tuple(ts) => ts.iter().map(size_of).sum(),
        MirType::Record(fields) => fields.values().map(size_of).sum(),
        _ => 8,
    }
}
