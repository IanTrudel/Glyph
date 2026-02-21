use cranelift_codegen::ir::{AbiParam, Signature};
use cranelift_codegen::isa::CallConv;
use cranelift_module::Module;

use glyph_mir::ir::MirType;

use crate::layout::mir_to_clif;

/// Build a Cranelift function signature from MIR types.
pub fn build_signature<M: Module>(
    module: &M,
    params: &[MirType],
    ret: &MirType,
) -> Signature {
    let mut sig = module.make_signature();
    for p in params {
        sig.params.push(AbiParam::new(mir_to_clif(p)));
    }
    if !matches!(ret, MirType::Void) {
        sig.returns.push(AbiParam::new(mir_to_clif(ret)));
    }
    sig
}

/// Build a C-ABI signature for extern functions.
pub fn build_extern_signature<M: Module>(
    _module: &M,
    param_types: &[MirType],
    ret: &MirType,
) -> Signature {
    let call_conv = CallConv::SystemV;
    let mut sig = Signature::new(call_conv);
    for p in param_types {
        sig.params.push(AbiParam::new(mir_to_clif(p)));
    }
    if !matches!(ret, MirType::Void) {
        sig.returns.push(AbiParam::new(mir_to_clif(ret)));
    }
    sig
}
