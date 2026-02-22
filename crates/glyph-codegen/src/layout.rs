use std::collections::BTreeMap;

use cranelift_codegen::ir::types;
use cranelift_codegen::ir::Type as ClifType;
use glyph_mir::ir::MirType;

/// Convert a MIR type to its Cranelift IR type.
/// Compound types (records, tuples, arrays, strings) are passed as pointers.
pub fn mir_to_clif(ty: &MirType) -> ClifType {
    match ty {
        MirType::Bool => types::I8,
        MirType::Int32 | MirType::Float32 => types::I32,
        MirType::Int | MirType::UInt => types::I64,
        MirType::Float => types::F64,
        MirType::Void => types::I64, // represent void as i64 (unused)
        MirType::Ptr(_) | MirType::Ref(_) | MirType::Fn(_, _) => types::I64, // pointer-sized
        MirType::Str => types::I64,  // pointer to {ptr, len} struct
        MirType::Array(_) => types::I64, // pointer to {ptr, len, cap} struct
        MirType::Never => types::I64,
        // Compound types passed as pointers to stack-allocated data
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

/// Alignment of a MIR type in bytes.
pub fn align_of(ty: &MirType) -> u32 {
    match ty {
        MirType::Bool => 1,
        MirType::Int32 | MirType::Float32 => 4,
        MirType::Int | MirType::UInt | MirType::Float
        | MirType::Ptr(_) | MirType::Ref(_) | MirType::Fn(_, _)
        | MirType::Str | MirType::Array(_) => 8,
        MirType::Tuple(ts) => ts.iter().map(align_of).max().unwrap_or(1),
        MirType::Record(fields) => fields.values().map(align_of).max().unwrap_or(1),
        _ => 8,
    }
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
        MirType::Tuple(ts) => {
            let mut offset = 0u32;
            for t in ts {
                let align = align_of(t);
                offset = (offset + align - 1) & !(align - 1);
                offset += size_of(t);
            }
            let total_align = ts.iter().map(align_of).max().unwrap_or(1);
            (offset + total_align - 1) & !(total_align - 1)
        }
        MirType::Record(fields) => {
            compute_record_layout(fields).size
        }
        MirType::Enum(_, variants) => {
            let max_payload: u32 = variants.iter()
                .map(|(_, fields)| fields.iter().map(size_of).sum::<u32>())
                .max().unwrap_or(0);
            4 + max_payload // 4-byte tag + max payload
        }
        _ => 8,
    }
}

/// Layout information for a record (struct).
pub struct RecordLayout {
    pub size: u32,
    pub alignment: u32,
    /// Byte offset of each field, in the same order as the BTreeMap iteration.
    pub field_offsets: Vec<(String, u32)>,
}

/// Compute layout for a record type with proper alignment.
pub fn compute_record_layout(fields: &BTreeMap<String, MirType>) -> RecordLayout {
    let mut offset = 0u32;
    let mut max_align = 1u32;
    let mut field_offsets = Vec::new();

    for (name, ty) in fields {
        let align = align_of(ty);
        let field_size = size_of(ty);
        max_align = max_align.max(align);
        // Align offset
        offset = (offset + align - 1) & !(align - 1);
        field_offsets.push((name.clone(), offset));
        offset += field_size;
    }

    // Pad to overall alignment
    let size = (offset + max_align - 1) & !(max_align - 1);

    RecordLayout {
        size,
        alignment: max_align,
        field_offsets,
    }
}

/// Get the byte offset of a field in a record type.
pub fn record_field_offset(fields: &BTreeMap<String, MirType>, field_idx: u32) -> (u32, MirType) {
    let layout = compute_record_layout(fields);
    let (name, offset) = &layout.field_offsets[field_idx as usize];
    let ty = fields[name].clone();
    (offset.clone(), ty)
}

/// Check if a MirType is a compound type that needs stack slot storage.
pub fn is_compound(ty: &MirType) -> bool {
    matches!(ty, MirType::Str | MirType::Record(_) | MirType::Tuple(_)
        | MirType::Array(_) | MirType::Enum(_, _) | MirType::ClosureEnv(_))
}
