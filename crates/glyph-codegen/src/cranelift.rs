use std::collections::{BTreeMap, BTreeSet, HashMap};

use cranelift_codegen::ir::condcodes::IntCC;
use cranelift_codegen::ir::types;
use cranelift_codegen::ir::{AbiParam, Function, InstBuilder, StackSlotData, StackSlotKind, Value};
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
    /// Global record type registry: maps each field name to the set of complete
    /// record types (sorted field lists) that contain it. Used to resolve field
    /// offsets when row polymorphism leaves partial record types in MIR.
    complete_record_types: Vec<BTreeMap<String, MirType>>,
    /// Per-function: all field names accessed on each local variable (pre-scanned).
    /// Used to disambiguate record types when MIR type info is partial.
    local_field_accesses: HashMap<u32, BTreeSet<String>>,
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
            complete_record_types: Vec::new(),
            local_field_accesses: HashMap::new(),
        }
    }

    /// Scan all MIR functions to collect complete record types for field offset resolution.
    pub fn register_record_types(&mut self, mir_fns: &[&MirFunction]) {
        let mut seen: BTreeSet<Vec<String>> = BTreeSet::new();
        for mir_fn in mir_fns {
            for local in &mir_fn.locals {
                self.collect_record_types(&local.ty, &mut seen);
            }
        }
    }

    fn collect_record_types(&mut self, ty: &MirType, seen: &mut BTreeSet<Vec<String>>) {
        match ty {
            MirType::Record(fields) if fields.len() > 1 => {
                let key: Vec<String> = fields.keys().cloned().collect();
                if seen.insert(key) {
                    self.complete_record_types.push(fields.clone());
                }
                for field_ty in fields.values() {
                    self.collect_record_types(field_ty, seen);
                }
            }
            MirType::Array(inner) => self.collect_record_types(inner, seen),
            MirType::Tuple(ts) => {
                for t in ts { self.collect_record_types(t, seen); }
            }
            MirType::Ref(inner) | MirType::Ptr(inner) => self.collect_record_types(inner, seen),
            _ => {}
        }
    }

    /// Find the field offset for a named field, resolving partial record types
    /// against the global registry of complete record types.
    fn resolve_field_offset(&self, partial_fields: &BTreeMap<String, MirType>, field_name: &str, all_accessed: Option<&BTreeSet<String>>) -> i32 {
        // Build the set of known field names: union of partial type fields and
        // all fields accessed on this same local variable (from pre-scan).
        let mut known_keys: BTreeSet<&str> = partial_fields.keys().map(|k| k.as_str()).collect();
        if let Some(accessed) = all_accessed {
            for k in accessed {
                known_keys.insert(k.as_str());
            }
        }
        for complete in &self.complete_record_types {
            let complete_keys: BTreeSet<&str> = complete.keys().map(|k| k.as_str()).collect();
            if known_keys.is_subset(&complete_keys) && complete.contains_key(field_name) {
                if let Some(full_pos) = complete.keys().position(|k| k == field_name) {
                    return (full_pos as i32) * 8;
                }
            }
        }
        if let Some(pos) = partial_fields.keys().position(|k| k == field_name) {
            (pos as i32) * 8
        } else {
            0
        }
    }

    /// Pre-scan a MIR function to collect all field names accessed per local variable.
    /// Used to disambiguate record types when MIR type info is partial.
    fn collect_field_accesses(mir_fn: &MirFunction) -> HashMap<u32, BTreeSet<String>> {
        let mut result: HashMap<u32, BTreeSet<String>> = HashMap::new();
        for block in &mir_fn.blocks {
            for stmt in &block.stmts {
                if let Rvalue::Field(Operand::Local(id), _idx, field_name) = &stmt.rvalue {
                    if !field_name.starts_with("__") {
                        result.entry(*id).or_default().insert(field_name.clone());
                    }
                }
            }
        }
        result
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

        // Rename "main" to "glyph_main" so we can add a C main wrapper
        // that sets up argc/argv before calling the user's main.
        let symbol_name = if mir_fn.name == "main" {
            "glyph_main".to_string()
        } else {
            mir_fn.name.clone()
        };
        let linkage = if mir_fn.name == "main" {
            Linkage::Export
        } else {
            Linkage::Local
        };

        let func_id = self
            .module
            .declare_function(&symbol_name, linkage, &sig)
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
        // Pre-scan field accesses to disambiguate partial record types
        self.local_field_accesses = Self::collect_field_accesses(mir_fn);

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
            self.compile_block(mir_block, &variables, &blocks, &mut builder, mir_fn);
        }

        // Seal all blocks after all predecessors are known
        for &block in &blocks {
            builder.seal_block(block);
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
            // Coerce value to match destination variable's declared type
            let dest_ty = mir_to_clif(&mir_fn.locals[stmt.dest as usize].ty);
            let val_ty = builder.func.dfg.value_type(val);
            let coerced = if val_ty == dest_ty {
                val
            } else if val_ty.bits() < dest_ty.bits() {
                builder.ins().uextend(dest_ty, val)
            } else if val_ty.bits() > dest_ty.bits() {
                builder.ins().ireduce(dest_ty, val)
            } else {
                val
            };
            builder.def_var(variables[stmt.dest as usize], coerced);
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
                    // Coerce return value to match function's declared return type
                    let ret_ty = mir_to_clif(&mir_fn.return_ty);
                    let val_ty = builder.func.dfg.value_type(val);
                    let coerced = if val_ty == ret_ty {
                        val
                    } else if val_ty.bits() < ret_ty.bits() {
                        builder.ins().uextend(ret_ty, val)
                    } else if val_ty.bits() > ret_ty.bits() {
                        builder.ins().ireduce(ret_ty, val)
                    } else {
                        val
                    };
                    builder.ins().return_(&[coerced]);
                }
            }
            Terminator::Unreachable => {
                builder.ins().trap(cranelift_codegen::ir::TrapCode::user(1).unwrap());
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
                let l_raw = self.operand_to_value(left, variables, builder);
                let r_raw = self.operand_to_value(right, variables, builder);
                // Widen i8 (Bool) to i64 when comparing mixed types
                let l_ty = builder.func.dfg.value_type(l_raw);
                let r_ty = builder.func.dfg.value_type(r_raw);
                let (l, r) = if l_ty != r_ty {
                    let target = if l_ty.bits() > r_ty.bits() { l_ty } else { r_ty };
                    let l = if l_ty.bits() < target.bits() { builder.ins().uextend(target, l_raw) } else { l_raw };
                    let r = if r_ty.bits() < target.bits() { builder.ins().uextend(target, r_raw) } else { r_raw };
                    (l, r)
                } else {
                    (l_raw, r_raw)
                };
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
                        // Indirect call through closure pointer.
                        // Closure layout: {fn_ptr: i64, captures...}
                        // Calling convention: fn(closure_ptr, user_args...) -> ret
                        let closure_ptr = self.operand_to_value(callee, variables, builder);

                        // Load fn_ptr from closure_ptr + 0
                        let fn_ptr = builder.ins().load(
                            types::I64,
                            cranelift_codegen::ir::MemFlags::new(),
                            closure_ptr,
                            0,
                        );

                        // Build signature: (i64 env_ptr, i64 args...) -> i64
                        let mut sig = self.module.make_signature();
                        sig.params.push(AbiParam::new(types::I64)); // env/closure ptr
                        for _ in &arg_vals {
                            sig.params.push(AbiParam::new(types::I64));
                        }
                        sig.returns.push(AbiParam::new(types::I64));
                        let sig_ref = builder.import_signature(sig);

                        // Call: fn_ptr(closure_ptr, args...)
                        let mut all_args = vec![closure_ptr];
                        all_args.extend_from_slice(&arg_vals);
                        let call = builder.ins().call_indirect(sig_ref, fn_ptr, &all_args);
                        let results = builder.inst_results(call);
                        if results.is_empty() {
                            builder.ins().iconst(types::I64, 0)
                        } else {
                            results[0]
                        }
                    }
                }
            }

            Rvalue::Aggregate(kind, ops) => {
                let vals: Vec<Value> = ops
                    .iter()
                    .map(|o| self.operand_to_value(o, variables, builder))
                    .collect();
                match kind {
                    AggregateKind::Record(_) | AggregateKind::Tuple => {
                        // All fields stored at 8-byte stride (uniform layout).
                        // Fields are in sorted (BTreeMap) order for records.
                        // Heap-allocated so records survive function returns.
                        let total_size = ((vals.len() * 8) as i64).max(8);
                        let alloc_size = builder.ins().iconst(types::I64, total_size);
                        let alloc_func = self.func_ids.get("glyph_alloc").copied();
                        let base_ptr = if let Some(func_id) = alloc_func {
                            let func_ref = self.module.declare_func_in_func(func_id, builder.func);
                            let call = builder.ins().call(func_ref, &[alloc_size]);
                            builder.inst_results(call)[0]
                        } else {
                            builder.ins().iconst(types::I64, 0)
                        };
                        for (i, val) in vals.iter().enumerate() {
                            let offset = builder.ins().iconst(types::I64, (i * 8) as i64);
                            let ptr = builder.ins().iadd(base_ptr, offset);
                            builder.ins().store(cranelift_codegen::ir::MemFlags::new(), *val, ptr, 0);
                        }
                        base_ptr
                    }
                    AggregateKind::Variant(_type_name, _variant_name, discriminant) => {
                        // Tagged union: [4-byte tag | payload...]
                        // Payload fields stored at 8-byte stride after the tag
                        let payload_size = (vals.len() * 8) as u32;
                        let total_size = (8 + payload_size).max(16); // tag (padded to 8) + payload
                        let slot = builder.create_sized_stack_slot(StackSlotData::new(
                            StackSlotKind::ExplicitSlot, total_size, 3,
                        ));
                        // Store discriminant tag at offset 0
                        let tag = builder.ins().iconst(types::I64, *discriminant as i64);
                        builder.ins().stack_store(tag, slot, 0);
                        // Store payload fields at offset 8+
                        for (i, val) in vals.iter().enumerate() {
                            builder.ins().stack_store(*val, slot, (8 + i * 8) as i32);
                        }
                        builder.ins().stack_addr(types::I64, slot, 0)
                    }
                    AggregateKind::Array => {
                        // Array header: {ptr: *T, len: u64, cap: u64} = 24 bytes
                        // HEAP-allocated so arrays survive function returns.
                        let len = vals.len() as i64;
                        let data_size = (vals.len() * 8) as i64;

                        let header_size = builder.ins().iconst(types::I64, 24);
                        let alloc_func = self.func_ids.get("glyph_alloc").copied();
                        let header_ptr = if let Some(func_id) = alloc_func {
                            let func_ref = self.module.declare_func_in_func(func_id, builder.func);
                            let call = builder.ins().call(func_ref, &[header_size]);
                            builder.inst_results(call)[0]
                        } else {
                            builder.ins().iconst(types::I64, 0)
                        };

                        if vals.is_empty() {
                            // Empty array: null ptr, len=0, cap=0
                            let zero = builder.ins().iconst(types::I64, 0);
                            builder.ins().store(cranelift_codegen::ir::MemFlags::new(), zero, header_ptr, 0);  // ptr
                            builder.ins().store(cranelift_codegen::ir::MemFlags::new(), zero, header_ptr, 8);  // len
                            builder.ins().store(cranelift_codegen::ir::MemFlags::new(), zero, header_ptr, 16); // cap
                        } else {
                            // Heap-allocate data
                            let alloc_size = builder.ins().iconst(types::I64, data_size);
                            let data_ptr = if let Some(func_id) = alloc_func {
                                let func_ref = self.module.declare_func_in_func(func_id, builder.func);
                                let call = builder.ins().call(func_ref, &[alloc_size]);
                                builder.inst_results(call)[0]
                            } else {
                                builder.ins().iconst(types::I64, 0)
                            };

                            // Store elements into heap data
                            for (i, val) in vals.iter().enumerate() {
                                let offset = builder.ins().iconst(types::I64, (i * 8) as i64);
                                let elem_ptr = builder.ins().iadd(data_ptr, offset);
                                builder.ins().store(cranelift_codegen::ir::MemFlags::new(), *val, elem_ptr, 0);
                            }

                            // Fill header
                            let len_val = builder.ins().iconst(types::I64, len);
                            builder.ins().store(cranelift_codegen::ir::MemFlags::new(), data_ptr, header_ptr, 0);   // ptr
                            builder.ins().store(cranelift_codegen::ir::MemFlags::new(), len_val, header_ptr, 8);    // len
                            builder.ins().store(cranelift_codegen::ir::MemFlags::new(), len_val, header_ptr, 16);   // cap
                        }

                        header_ptr
                    }
                }
            }

            Rvalue::Field(op, idx, field_name) => {
                let base_ptr = self.operand_to_value(op, variables, builder);
                let offset = if !field_name.starts_with("__") {
                    // Named record field — resolve offset using global type registry
                    // to handle partial record types from row polymorphism.
                    let mir_ty = match op {
                        Operand::Local(id) => &_mir_fn.locals[*id as usize].ty,
                        _ => &MirType::Int,
                    };
                    // Get all field names accessed on this local (for disambiguation)
                    let accessed = match op {
                        Operand::Local(id) => self.local_field_accesses.get(id),
                        _ => None,
                    };
                    match mir_ty {
                        MirType::Record(fields) => self.resolve_field_offset(fields, field_name, accessed),
                        _ => {
                            let empty = BTreeMap::new();
                            self.resolve_field_offset(&empty, field_name, accessed)
                        }
                    }
                } else {
                    // Internal fields (__tag, __payload, __capN) — use numeric index
                    (*idx as i32) * 8
                };
                builder.ins().load(types::I64, cranelift_codegen::ir::MemFlags::new(), base_ptr, offset)
            }

            Rvalue::Index(arr, idx) => {
                let arr_ptr = self.operand_to_value(arr, variables, builder);
                let idx_val = self.operand_to_value(idx, variables, builder);

                // Load data ptr and len from array header
                let data_ptr = builder.ins().load(types::I64, cranelift_codegen::ir::MemFlags::new(), arr_ptr, 0);
                let len = builder.ins().load(types::I64, cranelift_codegen::ir::MemFlags::new(), arr_ptr, 8);

                // Bounds check
                if let Some(&func_id) = self.func_ids.get("glyph_array_bounds_check") {
                    let func_ref = self.module.declare_func_in_func(func_id, builder.func);
                    builder.ins().call(func_ref, &[idx_val, len]);
                }

                // Load element at data_ptr + idx * 8
                let offset = builder.ins().imul_imm(idx_val, 8);
                let elem_ptr = builder.ins().iadd(data_ptr, offset);
                builder.ins().load(types::I64, cranelift_codegen::ir::MemFlags::new(), elem_ptr, 0)
            }

            Rvalue::Cast(op, _target_ty) => {
                self.operand_to_value(op, variables, builder)
            }

            Rvalue::StrInterp(parts) => {
                if parts.is_empty() {
                    // Empty string — heap-allocated {ptr, len}
                    let data_id = self.intern_string("");
                    let gv = self.module.declare_data_in_func(data_id, builder.func);
                    let data_ptr = builder.ins().global_value(types::I64, gv);
                    let len_val = builder.ins().iconst(types::I64, 0);
                    let sixteen = builder.ins().iconst(types::I64, 16);
                    let alloc_func = self.func_ids.get("glyph_alloc").copied();
                    let str_ptr = if let Some(func_id) = alloc_func {
                        let func_ref = self.module.declare_func_in_func(func_id, builder.func);
                        let call = builder.ins().call(func_ref, &[sixteen]);
                        builder.inst_results(call)[0]
                    } else {
                        builder.ins().iconst(types::I64, 0)
                    };
                    builder.ins().store(cranelift_codegen::ir::MemFlags::new(), data_ptr, str_ptr, 0);
                    builder.ins().store(cranelift_codegen::ir::MemFlags::new(), len_val, str_ptr, 8);
                    str_ptr
                } else {
                    // Convert each non-string part to string, then concatenate all parts
                    let mut result = self.operand_to_value(&parts[0], variables, builder);
                    // Convert first part to string if it's an int
                    if matches!(&parts[0], Operand::ConstInt(_) | Operand::Local(_))
                        && !matches!(&parts[0], Operand::ConstStr(_))
                    {
                        if let Some(&int_to_str_id) = self.func_ids.get("glyph_int_to_str") {
                            let func_ref = self.module.declare_func_in_func(int_to_str_id, builder.func);
                            let call = builder.ins().call(func_ref, &[result]);
                            result = builder.inst_results(call)[0];
                        }
                    }
                    for part in &parts[1..] {
                        let mut part_val = self.operand_to_value(part, variables, builder);
                        // Convert non-string parts to string
                        if matches!(part, Operand::ConstInt(_) | Operand::Local(_))
                            && !matches!(part, Operand::ConstStr(_))
                        {
                            if let Some(&int_to_str_id) = self.func_ids.get("glyph_int_to_str") {
                                let func_ref = self.module.declare_func_in_func(int_to_str_id, builder.func);
                                let call = builder.ins().call(func_ref, &[part_val]);
                                part_val = builder.inst_results(call)[0];
                            }
                        }
                        // Concatenate
                        if let Some(&concat_id) = self.func_ids.get("glyph_str_concat") {
                            let func_ref = self.module.declare_func_in_func(concat_id, builder.func);
                            let call = builder.ins().call(func_ref, &[result, part_val]);
                            result = builder.inst_results(call)[0];
                        }
                    }
                    result
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

            Rvalue::MakeClosure(fn_name, captures) => {
                // Heap-allocate closure: {fn_ptr: i64, capture1: i64, capture2: i64, ...}
                let total_size = (1 + captures.len()) * 8;
                let size_val = builder.ins().iconst(types::I64, total_size as i64);

                // Call glyph_alloc to allocate the closure struct
                let closure_ptr = if let Some(&alloc_id) = self.func_ids.get("glyph_alloc") {
                    let alloc_ref = self.module.declare_func_in_func(alloc_id, builder.func);
                    let call = builder.ins().call(alloc_ref, &[size_val]);
                    builder.inst_results(call)[0]
                } else {
                    builder.ins().iconst(types::I64, 0)
                };

                // Store fn_ptr at offset 0
                if let Some(&func_id) = self.func_ids.get(fn_name.as_str()) {
                    let func_ref = self.module.declare_func_in_func(func_id, builder.func);
                    let fn_addr = builder.ins().func_addr(types::I64, func_ref);
                    builder.ins().store(
                        cranelift_codegen::ir::MemFlags::new(),
                        fn_addr,
                        closure_ptr,
                        0,
                    );
                }

                // Store captures at offsets 8, 16, ...
                for (i, cap) in captures.iter().enumerate() {
                    let cap_val = self.operand_to_value(cap, variables, builder);
                    let offset = ((i + 1) * 8) as i32;
                    builder.ins().store(
                        cranelift_codegen::ir::MemFlags::new(),
                        cap_val,
                        closure_ptr,
                        offset,
                    );
                }

                closure_ptr
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
                // Create a string struct {ptr, len} on the HEAP
                // so strings survive function returns.
                let data_id = self.intern_string(s);
                let gv = self.module.declare_data_in_func(data_id, builder.func);
                let data_ptr = builder.ins().global_value(types::I64, gv);
                let len_val = builder.ins().iconst(types::I64, s.len() as i64);

                let sixteen = builder.ins().iconst(types::I64, 16);
                let alloc_func = self.func_ids.get("glyph_alloc").copied();
                let str_ptr = if let Some(func_id) = alloc_func {
                    let func_ref = self.module.declare_func_in_func(func_id, builder.func);
                    let call = builder.ins().call(func_ref, &[sixteen]);
                    builder.inst_results(call)[0]
                } else {
                    builder.ins().iconst(types::I64, 0)
                };
                builder.ins().store(cranelift_codegen::ir::MemFlags::new(), data_ptr, str_ptr, 0);
                builder.ins().store(cranelift_codegen::ir::MemFlags::new(), len_val, str_ptr, 8);
                str_ptr
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
