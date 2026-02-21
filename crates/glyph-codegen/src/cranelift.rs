use std::collections::HashMap;

use cranelift_codegen::ir::condcodes::IntCC;
use cranelift_codegen::ir::types;
use cranelift_codegen::ir::{AbiParam, Function, InstBuilder, Value};
use cranelift_codegen::settings::{self, Configurable};
use cranelift_codegen::Context;
use cranelift_frontend::{FunctionBuilder, FunctionBuilderContext, Variable};
use cranelift_module::{DataDescription, FuncId, Linkage, Module};
use cranelift_object::{ObjectBuilder, ObjectModule};

use glyph_mir::ir::*;

use crate::layout::mir_to_clif;

/// Translates MIR functions into Cranelift IR and produces an object file.
pub struct CodegenContext {
    module: ObjectModule,
    /// Map from function name to Cranelift FuncId.
    func_ids: HashMap<String, FuncId>,
    /// Map from string constant to data id.
    string_constants: HashMap<String, cranelift_module::DataId>,
}

impl CodegenContext {
    pub fn new() -> Self {
        let mut settings_builder = settings::builder();
        settings_builder.set("opt_level", "speed").unwrap();
        let flags = settings::Flags::new(settings_builder);
        let isa = cranelift_native::builder()
            .expect("host ISA not available")
            .finish(flags)
            .unwrap();

        let builder = ObjectBuilder::new(
            isa,
            "glyph_program",
            cranelift_module::default_libcall_names(),
        )
        .unwrap();
        let module = ObjectModule::new(builder);

        Self {
            module,
            func_ids: HashMap::new(),
            string_constants: HashMap::new(),
        }
    }

    /// Declare a function in the module (must be done before compilation).
    pub fn declare_function(&mut self, mir_fn: &MirFunction) -> FuncId {
        let mut sig = self.module.make_signature();
        for p in &mir_fn.params {
            let ty = mir_to_clif(&mir_fn.locals[*p as usize].ty);
            sig.params.push(AbiParam::new(ty));
        }
        if !matches!(mir_fn.return_ty, MirType::Void) {
            sig.returns.push(AbiParam::new(mir_to_clif(&mir_fn.return_ty)));
        }

        let linkage = if mir_fn.name == "main" {
            Linkage::Export
        } else {
            Linkage::Local
        };

        let func_id = self
            .module
            .declare_function(&mir_fn.name, linkage, &sig)
            .unwrap();
        self.func_ids.insert(mir_fn.name.clone(), func_id);
        func_id
    }

    /// Declare an extern function.
    pub fn declare_extern(&mut self, name: &str, symbol: &str, sig: &cranelift_codegen::ir::Signature) -> FuncId {
        let func_id = self
            .module
            .declare_function(symbol, Linkage::Import, sig)
            .unwrap();
        self.func_ids.insert(name.to_string(), func_id);
        func_id
    }

    /// Compile a MIR function into the module.
    pub fn compile_function(&mut self, mir_fn: &MirFunction) {
        let func_id = self.func_ids[&mir_fn.name];
        let mut sig = self.module.make_signature();
        for p in &mir_fn.params {
            let ty = mir_to_clif(&mir_fn.locals[*p as usize].ty);
            sig.params.push(AbiParam::new(ty));
        }
        if !matches!(mir_fn.return_ty, MirType::Void) {
            sig.returns.push(AbiParam::new(mir_to_clif(&mir_fn.return_ty)));
        }

        let mut func = Function::with_name_signature(
            cranelift_codegen::ir::UserFuncName::user(0, func_id.as_u32()),
            sig,
        );

        let mut builder_ctx = FunctionBuilderContext::new();
        let mut builder = FunctionBuilder::new(&mut func, &mut builder_ctx);

        // Create Cranelift variables for all MIR locals
        let variables: Vec<Variable> = mir_fn
            .locals
            .iter()
            .map(|local| {
                let var = Variable::from_u32(local.id);
                let clif_ty = mir_to_clif(&local.ty);
                builder.declare_var(var, clif_ty);
                var
            })
            .collect();

        // Create Cranelift blocks for all MIR blocks
        let blocks: Vec<cranelift_codegen::ir::Block> = mir_fn
            .blocks
            .iter()
            .map(|_| builder.create_block())
            .collect();

        // Entry block: receive params
        builder.append_block_params_for_function_params(blocks[mir_fn.entry as usize]);
        builder.switch_to_block(blocks[mir_fn.entry as usize]);
        builder.seal_block(blocks[mir_fn.entry as usize]);

        // Initialize params
        for (i, param_id) in mir_fn.params.iter().enumerate() {
            let param_val = builder.block_params(blocks[mir_fn.entry as usize])[i];
            builder.def_var(variables[*param_id as usize], param_val);
        }

        // Compile entry block statements and terminator
        self.compile_block(&mir_fn.blocks[mir_fn.entry as usize], &variables, &blocks, &mut builder, mir_fn);

        // Compile remaining blocks
        for (i, mir_block) in mir_fn.blocks.iter().enumerate() {
            if i == mir_fn.entry as usize {
                continue;
            }
            builder.switch_to_block(blocks[i]);
            builder.seal_block(blocks[i]);
            self.compile_block(mir_block, &variables, &blocks, &mut builder, mir_fn);
        }

        builder.finalize();

        let mut ctx = Context::for_function(func);
        self.module.define_function(func_id, &mut ctx).unwrap();
    }

    fn compile_block(
        &mut self,
        mir_block: &BasicBlock,
        variables: &[Variable],
        blocks: &[cranelift_codegen::ir::Block],
        builder: &mut FunctionBuilder,
        mir_fn: &MirFunction,
    ) {
        // Compile statements
        for stmt in &mir_block.stmts {
            let val = self.compile_rvalue(&stmt.rvalue, variables, blocks, builder, mir_fn);
            builder.def_var(variables[stmt.dest as usize], val);
        }

        // Compile terminator
        match &mir_block.terminator {
            Terminator::Goto(target) => {
                builder.ins().jump(blocks[*target as usize], &[]);
            }
            Terminator::Branch(cond, then_block, else_block) => {
                let cond_val = self.operand_to_value(cond, variables, builder);
                builder.ins().brif(
                    cond_val,
                    blocks[*then_block as usize],
                    &[],
                    blocks[*else_block as usize],
                    &[],
                );
            }
            Terminator::Switch(disc, cases, default) => {
                let disc_val = self.operand_to_value(disc, variables, builder);
                let mut switch = cranelift_frontend::Switch::new();
                for (val, block_id) in cases {
                    switch.set_entry(*val as u128, blocks[*block_id as usize]);
                }
                switch.emit(builder, disc_val, blocks[*default as usize]);
            }
            Terminator::Return(op) => {
                if matches!(mir_fn.return_ty, MirType::Void) {
                    builder.ins().return_(&[]);
                } else {
                    let val = self.operand_to_value(op, variables, builder);
                    builder.ins().return_(&[val]);
                }
            }
            Terminator::Unreachable => {
                builder.ins().trap(cranelift_codegen::ir::TrapCode::user(0).unwrap());
            }
        }
    }

    fn compile_rvalue(
        &mut self,
        rvalue: &Rvalue,
        variables: &[Variable],
        _blocks: &[cranelift_codegen::ir::Block],
        builder: &mut FunctionBuilder,
        _mir_fn: &MirFunction,
    ) -> Value {
        match rvalue {
            Rvalue::Use(op) => self.operand_to_value(op, variables, builder),

            Rvalue::BinOp(op, left, right) => {
                let l = self.operand_to_value(left, variables, builder);
                let r = self.operand_to_value(right, variables, builder);
                match op {
                    BinOp::Add => builder.ins().iadd(l, r),
                    BinOp::Sub => builder.ins().isub(l, r),
                    BinOp::Mul => builder.ins().imul(l, r),
                    BinOp::Div => builder.ins().sdiv(l, r),
                    BinOp::Mod => builder.ins().srem(l, r),
                    BinOp::Eq => builder.ins().icmp(IntCC::Equal, l, r),
                    BinOp::Neq => builder.ins().icmp(IntCC::NotEqual, l, r),
                    BinOp::Lt => builder.ins().icmp(IntCC::SignedLessThan, l, r),
                    BinOp::Gt => builder.ins().icmp(IntCC::SignedGreaterThan, l, r),
                    BinOp::LtEq => builder.ins().icmp(IntCC::SignedLessThanOrEqual, l, r),
                    BinOp::GtEq => builder.ins().icmp(IntCC::SignedGreaterThanOrEqual, l, r),
                    BinOp::And => builder.ins().band(l, r),
                    BinOp::Or => builder.ins().bor(l, r),
                }
            }

            Rvalue::UnOp(op, operand) => {
                let v = self.operand_to_value(operand, variables, builder);
                match op {
                    UnOp::Neg => builder.ins().ineg(v),
                    UnOp::Not => builder.ins().bnot(v),
                }
            }

            Rvalue::Call(callee, args) => {
                let arg_vals: Vec<Value> = args
                    .iter()
                    .map(|a| self.operand_to_value(a, variables, builder))
                    .collect();

                match callee {
                    Operand::FuncRef(name) | Operand::ExternRef(name) => {
                        if let Some(&func_id) = self.func_ids.get(name.as_str()) {
                            let func_ref = self.module.declare_func_in_func(func_id, builder.func);
                            let call = builder.ins().call(func_ref, &arg_vals);
                            let results = builder.inst_results(call);
                            if results.is_empty() {
                                builder.ins().iconst(types::I64, 0)
                            } else {
                                results[0]
                            }
                        } else {
                            // Unknown function — return 0
                            builder.ins().iconst(types::I64, 0)
                        }
                    }
                    _ => {
                        // Indirect call (function pointer)
                        let _callee_val = self.operand_to_value(callee, variables, builder);
                        // For indirect calls, we'd need a sig ref. Return 0 for now.
                        builder.ins().iconst(types::I64, 0)
                    }
                }
            }

            Rvalue::Aggregate(_kind, ops) => {
                // Simplified: for tuples/records, just return the first element
                // Full aggregate construction would allocate on the stack
                if ops.is_empty() {
                    builder.ins().iconst(types::I64, 0)
                } else {
                    self.operand_to_value(&ops[0], variables, builder)
                }
            }

            Rvalue::Field(op, _idx) => {
                // Simplified: just return the value (no actual field extraction yet)
                self.operand_to_value(op, variables, builder)
            }

            Rvalue::Index(arr, _idx) => {
                // Simplified
                self.operand_to_value(arr, variables, builder)
            }

            Rvalue::Cast(op, _target_ty) => {
                self.operand_to_value(op, variables, builder)
            }

            Rvalue::StrInterp(parts) => {
                // Simplified: return 0 (proper impl would concat strings)
                if parts.is_empty() {
                    builder.ins().iconst(types::I64, 0)
                } else {
                    self.operand_to_value(&parts[0], variables, builder)
                }
            }

            Rvalue::Ref(local) => {
                // Get stack address of local — simplified
                builder.use_var(variables[*local as usize])
            }

            Rvalue::Deref(op) => {
                let ptr = self.operand_to_value(op, variables, builder);
                builder.ins().load(types::I64, cranelift_codegen::ir::MemFlags::new(), ptr, 0)
            }
        }
    }

    fn operand_to_value(
        &mut self,
        op: &Operand,
        variables: &[Variable],
        builder: &mut FunctionBuilder,
    ) -> Value {
        match op {
            Operand::Local(id) => builder.use_var(variables[*id as usize]),
            Operand::ConstInt(n) => builder.ins().iconst(types::I64, *n),
            Operand::ConstFloat(f) => builder.ins().f64const(*f),
            Operand::ConstBool(b) => builder.ins().iconst(types::I8, *b as i64),
            Operand::ConstStr(s) => {
                // Store string as data and return pointer
                let data_id = self.intern_string(s);
                let gv = self.module.declare_data_in_func(data_id, builder.func);
                builder.ins().global_value(types::I64, gv)
            }
            Operand::ConstUnit => builder.ins().iconst(types::I64, 0),
            Operand::FuncRef(name) => {
                if let Some(&func_id) = self.func_ids.get(name.as_str()) {
                    let func_ref = self.module.declare_func_in_func(func_id, builder.func);
                    builder.ins().func_addr(types::I64, func_ref)
                } else {
                    builder.ins().iconst(types::I64, 0)
                }
            }
            Operand::ExternRef(name) => {
                if let Some(&func_id) = self.func_ids.get(name.as_str()) {
                    let func_ref = self.module.declare_func_in_func(func_id, builder.func);
                    builder.ins().func_addr(types::I64, func_ref)
                } else {
                    builder.ins().iconst(types::I64, 0)
                }
            }
        }
    }

    fn intern_string(&mut self, s: &str) -> cranelift_module::DataId {
        if let Some(&id) = self.string_constants.get(s) {
            return id;
        }
        let name = format!(".str.{}", self.string_constants.len());
        let data_id = self
            .module
            .declare_data(&name, Linkage::Local, false, false)
            .unwrap();
        let mut desc = DataDescription::new();
        let mut bytes = s.as_bytes().to_vec();
        bytes.push(0); // null-terminate for C compat
        desc.define(bytes.into_boxed_slice());
        self.module.define_data(data_id, &desc).unwrap();
        self.string_constants.insert(s.to_string(), data_id);
        data_id
    }

    /// Finalize and emit the object file bytes.
    pub fn finish(self) -> Vec<u8> {
        let product = self.module.finish();
        product.emit().unwrap()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use glyph_mir::ir::*;

    fn make_simple_mir() -> MirFunction {
        // fn main() -> i64 { return 42; }
        MirFunction {
            name: "main".to_string(),
            params: vec![],
            return_ty: MirType::Int,
            locals: vec![MirLocal {
                id: 0,
                ty: MirType::Int,
                name: Some("result".into()),
            }],
            blocks: vec![BasicBlock {
                id: 0,
                stmts: vec![],
                terminator: Terminator::Return(Operand::ConstInt(42)),
            }],
            entry: 0,
        }
    }

    fn make_add_mir() -> MirFunction {
        // fn add(a: i64, b: i64) -> i64 { return a + b; }
        MirFunction {
            name: "add".to_string(),
            params: vec![0, 1],
            return_ty: MirType::Int,
            locals: vec![
                MirLocal { id: 0, ty: MirType::Int, name: Some("a".into()) },
                MirLocal { id: 1, ty: MirType::Int, name: Some("b".into()) },
                MirLocal { id: 2, ty: MirType::Int, name: Some("result".into()) },
            ],
            blocks: vec![BasicBlock {
                id: 0,
                stmts: vec![Statement {
                    dest: 2,
                    rvalue: Rvalue::BinOp(BinOp::Add, Operand::Local(0), Operand::Local(1)),
                }],
                terminator: Terminator::Return(Operand::Local(2)),
            }],
            entry: 0,
        }
    }

    #[test]
    fn test_codegen_simple() {
        let mir = make_simple_mir();
        let mut ctx = CodegenContext::new();
        ctx.declare_function(&mir);
        ctx.compile_function(&mir);
        let object_bytes = ctx.finish();
        assert!(!object_bytes.is_empty());
    }

    #[test]
    fn test_codegen_add() {
        let mir = make_add_mir();
        let mut ctx = CodegenContext::new();
        ctx.declare_function(&mir);
        ctx.compile_function(&mir);
        let object_bytes = ctx.finish();
        assert!(!object_bytes.is_empty());
    }

    #[test]
    fn test_codegen_with_branch() {
        // fn f(x: i64) -> i64 { if x > 0: x else: 0 }
        let mir = MirFunction {
            name: "f".to_string(),
            params: vec![0],
            return_ty: MirType::Int,
            locals: vec![
                MirLocal { id: 0, ty: MirType::Int, name: Some("x".into()) },
                MirLocal { id: 1, ty: MirType::Bool, name: None },
                MirLocal { id: 2, ty: MirType::Int, name: Some("result".into()) },
            ],
            blocks: vec![
                BasicBlock {
                    id: 0,
                    stmts: vec![Statement {
                        dest: 1,
                        rvalue: Rvalue::BinOp(BinOp::Gt, Operand::Local(0), Operand::ConstInt(0)),
                    }],
                    terminator: Terminator::Branch(Operand::Local(1), 1, 2),
                },
                BasicBlock {
                    id: 1,
                    stmts: vec![Statement {
                        dest: 2,
                        rvalue: Rvalue::Use(Operand::Local(0)),
                    }],
                    terminator: Terminator::Goto(3),
                },
                BasicBlock {
                    id: 2,
                    stmts: vec![Statement {
                        dest: 2,
                        rvalue: Rvalue::Use(Operand::ConstInt(0)),
                    }],
                    terminator: Terminator::Goto(3),
                },
                BasicBlock {
                    id: 3,
                    stmts: vec![],
                    terminator: Terminator::Return(Operand::Local(2)),
                },
            ],
            entry: 0,
        };

        let mut ctx = CodegenContext::new();
        ctx.declare_function(&mir);
        ctx.compile_function(&mir);
        let object_bytes = ctx.finish();
        assert!(!object_bytes.is_empty());
    }
}
