use std::collections::{BTreeMap, HashMap, HashSet};

use glyph_parse::ast;
use glyph_typeck::types::Type;

use crate::ir::*;

/// Lowers typed AST into MIR.
pub struct MirLower {
    locals: Vec<MirLocal>,
    blocks: Vec<BasicBlock>,
    current_block: BlockId,
    next_local: LocalId,
    next_block: BlockId,
    /// Known function types for cross-definition call resolution.
    known_functions: HashMap<String, MirType>,
    /// Enum variant info: variant_name -> (type_name, discriminant, field_types)
    enum_variants: HashMap<String, (String, u32, Vec<MirType>)>,
    /// Functions generated from lambda lifting.
    pub lifted_fns: Vec<MirFunction>,
    /// Counter for generating unique lambda names.
    lambda_counter: u32,
    /// Current function name (for naming lambdas).
    current_fn_name: String,
    /// Parameter locals for tail-call optimization (self-recursive calls).
    tail_call_params: Vec<LocalId>,
    /// Entry block for tail-call optimization.
    tail_call_entry: BlockId,
    /// Zero-arg function names: bare references auto-call these.
    zero_arg_fns: HashSet<String>,
}

impl MirLower {
    pub fn new() -> Self {
        Self {
            locals: Vec::new(),
            blocks: Vec::new(),
            current_block: 0,
            next_local: 0,
            next_block: 0,
            known_functions: HashMap::new(),
            enum_variants: HashMap::new(),
            lifted_fns: Vec::new(),
            lambda_counter: 0,
            current_fn_name: String::new(),
            tail_call_params: Vec::new(),
            tail_call_entry: 0,
            zero_arg_fns: HashSet::new(),
        }
    }

    /// Set known function types for cross-definition call resolution.
    pub fn set_known_functions(&mut self, fns: HashMap<String, MirType>) {
        self.known_functions = fns;
    }

    /// Set the names of zero-arg functions (bare references auto-call these).
    pub fn set_zero_arg_fns(&mut self, fns: HashSet<String>) {
        self.zero_arg_fns = fns;
    }

    /// Register enum variant info for constructor pattern matching and lowering.
    pub fn register_enum(&mut self, type_name: &str, variants: &[(String, Vec<MirType>)]) {
        for (disc, (vname, fields)) in variants.iter().enumerate() {
            self.enum_variants.insert(
                vname.clone(),
                (type_name.to_string(), disc as u32, fields.clone()),
            );
        }
    }

    /// Get the MIR type of an operand.
    fn operand_type(&self, op: &Operand) -> MirType {
        match op {
            Operand::Local(id) => self.locals[*id as usize].ty.clone(),
            Operand::ConstInt(_) => MirType::Int,
            Operand::ConstFloat(_) => MirType::Float,
            Operand::ConstBool(_) => MirType::Bool,
            Operand::ConstStr(_) => MirType::Str,
            Operand::ConstUnit => MirType::Void,
            Operand::FuncRef(name) | Operand::ExternRef(name) => {
                self.known_functions.get(name).cloned().unwrap_or(MirType::Int)
            }
        }
    }

    /// Get the return type of calling a function with given number of args.
    fn call_return_type(&self, callee: &Operand, arg_count: usize) -> MirType {
        let fn_ty = self.operand_type(callee);
        let mut current = fn_ty;
        for _ in 0..arg_count {
            if let MirType::Fn(_, ret) = current {
                current = *ret;
            } else {
                return MirType::Int; // fallback
            }
        }
        current
    }

    /// Lower a function definition into MIR.
    pub fn lower_fn(&mut self, name: &str, fndef: &ast::FnDef, fn_ty: &Type) -> MirFunction {
        self.locals.clear();
        self.blocks.clear();
        self.next_local = 0;
        self.next_block = 0;
        self.current_fn_name = name.to_string();

        // Allocate param locals
        let param_types = self.extract_param_types(fn_ty, fndef.params.len());
        let return_ty = self.extract_return_type(fn_ty, fndef.params.len());
        let params: Vec<LocalId> = fndef
            .params
            .iter()
            .zip(param_types.iter())
            .map(|(p, ty)| self.alloc_local(Some(p.name.clone()), ty.clone()))
            .collect();

        // Entry block
        let entry = self.new_block();
        self.current_block = entry;

        // Create a loop header for TCO (Cranelift can't jump back to entry block)
        let loop_header = self.new_block();
        self.terminate(Terminator::Goto(loop_header));
        self.current_block = loop_header;

        // Set up tail-call optimization info — tail calls jump to loop_header
        self.tail_call_params = params.clone();
        self.tail_call_entry = loop_header;

        // Lower body (in tail position for TCO)
        let result = match &fndef.body {
            ast::Body::Expr(expr) => self.lower_expr_tail(expr),
            ast::Body::Block(stmts) => self.lower_block_tail(stmts),
        };

        // Terminate with return
        self.terminate(Terminator::Return(result));

        MirFunction {
            name: name.to_string(),
            params,
            return_ty,
            locals: self.locals.clone(),
            blocks: self.blocks.clone(),
            entry,
        }
    }

    fn lower_expr(&mut self, expr: &ast::Expr) -> Operand {
        match &expr.kind {
            ast::ExprKind::IntLit(n) => Operand::ConstInt(*n),
            ast::ExprKind::FloatLit(f) => Operand::ConstFloat(*f),
            ast::ExprKind::StrLit(s) => Operand::ConstStr(s.clone()),
            ast::ExprKind::BoolLit(b) => Operand::ConstBool(*b),

            ast::ExprKind::Ident(name) => {
                // Look up in locals (reverse order: most recent binding shadows earlier ones)
                for local in self.locals.iter().rev() {
                    if local.name.as_deref() == Some(name.as_str()) {
                        return Operand::Local(local.id);
                    }
                }
                // Auto-call zero-arg functions on bare reference
                if self.zero_arg_fns.contains(name) {
                    let ret_ty = self.call_return_type(&Operand::FuncRef(name.clone()), 0);
                    let dest = self.alloc_local(None, ret_ty);
                    self.emit(Statement {
                        dest,
                        rvalue: Rvalue::Call(Operand::FuncRef(name.clone()), vec![]),
                    });
                    return Operand::Local(dest);
                }
                // Must be a reference to another function
                Operand::FuncRef(name.clone())
            }

            ast::ExprKind::Binary(op, left, right) => {
                let l = self.lower_expr(left);
                let r = self.lower_expr(right);
                let l_ty = self.operand_type(&l);

                // String concatenation: desugar to runtime call
                if matches!(l_ty, MirType::Str) && matches!(op, ast::BinOp::Add) {
                    let dest = self.alloc_local(None, MirType::Str);
                    self.emit(Statement {
                        dest,
                        rvalue: Rvalue::Call(
                            Operand::ExternRef("glyph_str_concat".to_string()),
                            vec![l, r],
                        ),
                    });
                    return Operand::Local(dest);
                }

                let mir_op = lower_binop(op);
                let result_ty = match mir_op {
                    BinOp::Eq | BinOp::Neq | BinOp::Lt | BinOp::Gt
                    | BinOp::LtEq | BinOp::GtEq | BinOp::And | BinOp::Or => MirType::Bool,
                    _ => l_ty,
                };
                let dest = self.alloc_local(None, result_ty);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::BinOp(mir_op, l, r),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Unary(op, operand) => {
                let inner = self.lower_expr(operand);
                let inner_ty = self.operand_type(&inner);
                let mir_op = match op {
                    ast::UnaryOp::Neg => UnOp::Neg,
                    ast::UnaryOp::Not => UnOp::Not,
                    ast::UnaryOp::Ref => {
                        if let Operand::Local(id) = inner {
                            let dest = self.alloc_local(None, MirType::Ptr(Box::new(inner_ty)));
                            self.emit(Statement {
                                dest,
                                rvalue: Rvalue::Ref(id),
                            });
                            return Operand::Local(dest);
                        }
                        return inner;
                    }
                    ast::UnaryOp::Deref => {
                        let pointee = match &inner_ty {
                            MirType::Ptr(t) | MirType::Ref(t) => *t.clone(),
                            _ => MirType::Int,
                        };
                        let dest = self.alloc_local(None, pointee);
                        self.emit(Statement {
                            dest,
                            rvalue: Rvalue::Deref(inner),
                        });
                        return Operand::Local(dest);
                    }
                };
                let result_ty = match mir_op {
                    UnOp::Not => MirType::Bool,
                    UnOp::Neg => inner_ty,
                };
                let dest = self.alloc_local(None, result_ty);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::UnOp(mir_op, inner),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Call(func, args) => {
                // Check if this is a variant constructor call, e.g. Some(42)
                if let ast::ExprKind::TypeIdent(name) = &func.kind {
                    if let Some((type_name, disc, _)) = self.enum_variants.get(name).cloned() {
                        let arg_ops: Vec<_> = args.iter().map(|a| self.lower_expr(a)).collect();
                        let dest = self.alloc_local(None, MirType::Named(type_name.clone()));
                        self.emit(Statement {
                            dest,
                            rvalue: Rvalue::Aggregate(
                                AggregateKind::Variant(type_name, name.clone(), disc),
                                arg_ops,
                            ),
                        });
                        return Operand::Local(dest);
                    }
                }
                // Use lower_callee_expr to avoid auto-calling zero-arg fns in callee position
                let callee = self.lower_callee_expr(func);
                let arg_ops: Vec<_> = args.iter().map(|a| self.lower_expr(a)).collect();
                let ret_ty = self.call_return_type(&callee, arg_ops.len());
                let dest = self.alloc_local(None, ret_ty);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Call(callee, arg_ops),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Pipe(left, right) => {
                // x |> f  →  f(x)
                let arg = self.lower_expr(left);
                let func = self.lower_callee_expr(right);
                let ret_ty = self.call_return_type(&func, 1);
                let dest = self.alloc_local(None, ret_ty);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Call(func, vec![arg]),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Compose(left, right) => {
                // f >> g  →  closure { f, g }
                // For now, lower as a pair of function refs
                let _f = self.lower_expr(left);
                let _g = self.lower_expr(right);
                // TODO: proper closure conversion
                Operand::ConstUnit
            }

            ast::ExprKind::FieldAccess(expr_inner, field) => {
                let record = self.lower_expr(expr_inner);
                let record_ty = self.operand_type(&record);
                let (field_idx, field_ty) = match &record_ty {
                    MirType::Record(fields) => {
                        let idx = fields.keys().position(|k| k == field).unwrap_or(0) as u32;
                        let ty = fields.get(field).cloned().unwrap_or(MirType::Int);
                        (idx, ty)
                    }
                    _ => (0, MirType::Int),
                };
                let dest = self.alloc_local(None, field_ty);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Field(record, field_idx, field.clone()),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Index(expr_inner, idx) => {
                let arr = self.lower_expr(expr_inner);
                let index = self.lower_expr(idx);
                let elem_ty = match self.operand_type(&arr) {
                    MirType::Array(elem) => *elem,
                    _ => MirType::Int,
                };
                let dest = self.alloc_local(None, elem_ty);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Index(arr, index),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Match(scrutinee, arms) => {
                let scrut = self.lower_expr(scrutinee);
                let merge_block = self.new_block();

                // Determine result type from the first arm's body
                // We'll allocate the result after lowering the first arm to know the type
                let remaining_arms: Vec<_> = arms.iter().collect();
                let result = self.alloc_local(None, MirType::Int); // will be updated
                self.lower_match_chain(&scrut, &remaining_arms, result, merge_block, false);

                self.current_block = merge_block;
                Operand::Local(result)
            }

            ast::ExprKind::Lambda(params, body) => {
                // Closure conversion: lift lambda to a top-level function.
                // Captures are passed via a heap-allocated env struct.

                // 1. Collect free variables (locals from enclosing scope used in body)
                let param_names: HashSet<String> = params.iter().map(|p| p.name.clone()).collect();
                let free_vars = self.collect_free_vars(body, &param_names);

                // 2. Generate unique name for lifted function
                let lambda_name = format!("{}_lambda_{}", self.current_fn_name, self.lambda_counter);
                self.lambda_counter += 1;

                // 3. Save outer lowering state
                let outer_locals = std::mem::take(&mut self.locals);
                let outer_blocks = std::mem::take(&mut self.blocks);
                let outer_current_block = self.current_block;
                let outer_next_local = self.next_local;
                let outer_next_block = self.next_block;
                let outer_fn_name = std::mem::replace(&mut self.current_fn_name, lambda_name.clone());

                self.next_local = 0;
                self.next_block = 0;

                // 4. Build lambda function: params are (env_ptr, lambda_params...)
                let env_param = self.alloc_local(Some("__env".into()), MirType::Int);
                let mut lambda_param_ids = vec![env_param];
                for p in params {
                    let pid = self.alloc_local(Some(p.name.clone()), MirType::Int);
                    lambda_param_ids.push(pid);
                }

                // Entry block
                let entry = self.new_block();
                self.current_block = entry;

                // Load captures from env_ptr at 8-byte stride (offset 8, 16, ...)
                for (i, (name, ty, _outer_id)) in free_vars.iter().enumerate() {
                    let cap_local = self.alloc_local(Some(name.clone()), ty.clone());
                    self.emit(Statement {
                        dest: cap_local,
                        rvalue: Rvalue::Field(Operand::Local(env_param), (i + 1) as u32, format!("__cap{}", i)),
                    });
                }

                // Lower the body
                let result = self.lower_expr(body);
                let return_ty = self.operand_type(&result);
                self.terminate(Terminator::Return(result));

                // Build MirFunction for lifted lambda
                let mir_fn = MirFunction {
                    name: lambda_name.clone(),
                    params: lambda_param_ids.clone(),
                    return_ty,
                    locals: self.locals.clone(),
                    blocks: self.blocks.clone(),
                    entry,
                };
                self.lifted_fns.push(mir_fn);

                // 5. Restore outer state
                self.locals = outer_locals;
                self.blocks = outer_blocks;
                self.current_block = outer_current_block;
                self.next_local = outer_next_local;
                self.next_block = outer_next_block;
                self.current_fn_name = outer_fn_name;

                // 6. In outer context: emit MakeClosure
                let capture_ops: Vec<Operand> = free_vars.iter()
                    .map(|(_, _, local_id)| Operand::Local(*local_id))
                    .collect();

                // Determine the closure's function type (params -> ret)
                let closure_ty = MirType::Fn(Box::new(MirType::Int), Box::new(MirType::Int));
                let dest = self.alloc_local(None, closure_ty);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::MakeClosure(lambda_name, capture_ops),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Array(elems) => {
                let ops: Vec<_> = elems.iter().map(|e| self.lower_expr(e)).collect();
                let elem_ty = ops.first()
                    .map(|op| self.operand_type(op))
                    .unwrap_or(MirType::Int);
                let dest = self.alloc_local(None, MirType::Array(Box::new(elem_ty)));
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Aggregate(AggregateKind::Array, ops),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Record(fields) => {
                // Sort fields alphabetically to match BTreeMap iteration order
                let mut sorted: Vec<_> = fields.iter().collect();
                sorted.sort_by_key(|f| &f.name);
                let names: Vec<String> = sorted.iter().map(|f| f.name.clone()).collect();
                let ops: Vec<_> = sorted.iter().map(|f| self.lower_expr(&f.value)).collect();
                let field_types: BTreeMap<String, MirType> = names.iter()
                    .zip(ops.iter())
                    .map(|(n, op)| (n.clone(), self.operand_type(op)))
                    .collect();
                let dest = self.alloc_local(None, MirType::Record(field_types));
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Aggregate(AggregateKind::Record(names), ops),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Tuple(elems) => {
                let ops: Vec<_> = elems.iter().map(|e| self.lower_expr(e)).collect();
                let dest = self.alloc_local(None, MirType::Tuple(Vec::new()));
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Aggregate(AggregateKind::Tuple, ops),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::StrInterp(parts) => {
                // Desugar to string builder: sb_new → N x sb_append → sb_build
                // This is O(n) vs O(n²) for chained concat.
                let mut str_ops: Vec<Operand> = Vec::new();
                for p in parts {
                    match p {
                        ast::StringPart::Lit(s) => {
                            str_ops.push(Operand::ConstStr(s.clone()));
                        }
                        ast::StringPart::Expr(e) => {
                            let val = self.lower_expr(e);
                            let ty = self.operand_type(&val);
                            if matches!(ty, MirType::Str) {
                                str_ops.push(val);
                            } else {
                                // Convert to string via glyph_int_to_str
                                let converted = self.alloc_local(None, MirType::Str);
                                self.emit(Statement {
                                    dest: converted,
                                    rvalue: Rvalue::Call(
                                        Operand::ExternRef("glyph_int_to_str".to_string()),
                                        vec![val],
                                    ),
                                });
                                str_ops.push(Operand::Local(converted));
                            }
                        }
                    }
                }

                if str_ops.is_empty() {
                    return Operand::ConstStr(String::new());
                }

                // For single-part interpolation, no builder needed
                if str_ops.len() == 1 {
                    let dest = self.alloc_local(None, MirType::Str);
                    self.emit(Statement {
                        dest,
                        rvalue: Rvalue::Use(str_ops[0].clone()),
                    });
                    return Operand::Local(dest);
                }

                // Use string builder for 2+ parts
                let sb = self.alloc_local(None, MirType::Ptr(Box::new(MirType::Void)));
                self.emit(Statement {
                    dest: sb,
                    rvalue: Rvalue::Call(
                        Operand::ExternRef("glyph_sb_new".to_string()),
                        vec![],
                    ),
                });

                for part in &str_ops {
                    let appended = self.alloc_local(None, MirType::Ptr(Box::new(MirType::Void)));
                    self.emit(Statement {
                        dest: appended,
                        rvalue: Rvalue::Call(
                            Operand::ExternRef("glyph_sb_append".to_string()),
                            vec![Operand::Local(sb), part.clone()],
                        ),
                    });
                    // sb_append returns the same sb pointer, update sb local
                    self.emit(Statement {
                        dest: sb,
                        rvalue: Rvalue::Use(Operand::Local(appended)),
                    });
                }

                let dest = self.alloc_local(None, MirType::Str);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Call(
                        Operand::ExternRef("glyph_sb_build".to_string()),
                        vec![Operand::Local(sb)],
                    ),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Propagate(inner) => {
                // expr? → match expr { Some(v)/Ok(v) -> v, None/Err(e) -> return None/Err(e) }
                let val = self.lower_expr(inner);

                // Load discriminant
                let disc = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest: disc,
                    rvalue: Rvalue::Field(val.clone(), 0, "__tag".into()),
                });

                let ok_block = self.new_block();
                let err_block = self.new_block();
                let merge_block = self.new_block();

                // Some/Ok has discriminant 1 (second variant)
                let cmp = self.alloc_local(None, MirType::Bool);
                self.emit(Statement {
                    dest: cmp,
                    rvalue: Rvalue::BinOp(BinOp::Eq, Operand::Local(disc), Operand::ConstInt(1)),
                });
                self.terminate(Terminator::Branch(Operand::Local(cmp), ok_block, err_block));

                // Ok path: extract inner value
                self.current_block = ok_block;
                let inner_val = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest: inner_val,
                    rvalue: Rvalue::Field(val.clone(), 1, "__payload".into()), // payload at offset 1
                });
                let result = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(Operand::Local(inner_val)),
                });
                self.terminate(Terminator::Goto(merge_block));

                // Err path: early return with the original value
                self.current_block = err_block;
                self.terminate(Terminator::Return(val));

                self.current_block = merge_block;
                Operand::Local(result)
            }

            ast::ExprKind::Unwrap(inner) => {
                // expr! → match expr { Some(v)/Ok(v) -> v, _ -> panic }
                let val = self.lower_expr(inner);

                // Load discriminant
                let disc = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest: disc,
                    rvalue: Rvalue::Field(val.clone(), 0, "__tag".into()),
                });

                let ok_block = self.new_block();
                let panic_block = self.new_block();
                let merge_block = self.new_block();

                // Some/Ok has discriminant 1 (second variant)
                let cmp = self.alloc_local(None, MirType::Bool);
                self.emit(Statement {
                    dest: cmp,
                    rvalue: Rvalue::BinOp(BinOp::Eq, Operand::Local(disc), Operand::ConstInt(1)),
                });
                self.terminate(Terminator::Branch(Operand::Local(cmp), ok_block, panic_block));

                // Ok path: extract inner value
                self.current_block = ok_block;
                let inner_val = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest: inner_val,
                    rvalue: Rvalue::Field(val, 1, "__payload".into()), // payload at offset 1
                });
                let result = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(Operand::Local(inner_val)),
                });
                self.terminate(Terminator::Goto(merge_block));

                // Panic path
                self.current_block = panic_block;
                // Call glyph_panic with "unwrap failed" message
                let panic_msg = self.alloc_local(None, MirType::Str);
                self.emit(Statement {
                    dest: panic_msg,
                    rvalue: Rvalue::Use(Operand::ConstStr("unwrap failed".to_string())),
                });
                let _panic_result = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest: _panic_result,
                    rvalue: Rvalue::Call(
                        Operand::ExternRef("glyph_panic_str".to_string()),
                        vec![Operand::Local(panic_msg)],
                    ),
                });
                self.terminate(Terminator::Unreachable);

                self.current_block = merge_block;
                Operand::Local(result)
            }

            ast::ExprKind::Block(stmts) => self.lower_block(stmts),

            ast::ExprKind::FieldAccessor(field) => {
                // .field → treated as a function reference
                Operand::FuncRef(format!(".{field}"))
            }

            ast::ExprKind::TypeIdent(name) => {
                // Check if this is a nullary enum variant constructor
                let is_nullary = self.enum_variants.get(name)
                    .map(|(_, _, ftypes)| ftypes.is_empty())
                    .unwrap_or(false);
                if is_nullary {
                    let (type_name, disc, _) = self.enum_variants.get(name).cloned().unwrap();
                    let dest = self.alloc_local(None, MirType::Named(type_name.clone()));
                    self.emit(Statement {
                        dest,
                        rvalue: Rvalue::Aggregate(
                            AggregateKind::Variant(type_name, name.clone(), disc),
                            vec![],
                        ),
                    });
                    Operand::Local(dest)
                } else {
                    Operand::FuncRef(name.clone())
                }
            }
            ast::ExprKind::ByteStrLit(bytes) => Operand::ConstStr(String::from_utf8_lossy(bytes).to_string()),
            ast::ExprKind::ArrayRange(start, _end) => {
                let _s = self.lower_expr(start);
                // TODO: emit range construction
                Operand::ConstUnit
            }
        }
    }

    /// Lower an expression in callee position (skips zero-arg auto-call for Ident).
    fn lower_callee_expr(&mut self, expr: &ast::Expr) -> Operand {
        if let ast::ExprKind::Ident(name) = &expr.kind {
            // Look up in locals first
            for local in self.locals.iter().rev() {
                if local.name.as_deref() == Some(name.as_str()) {
                    return Operand::Local(local.id);
                }
            }
            // Return raw FuncRef — no auto-call in callee position
            return Operand::FuncRef(name.clone());
        }
        self.lower_expr(expr)
    }

    fn lower_block(&mut self, stmts: &[ast::Stmt]) -> Operand {
        let mut last = Operand::ConstUnit;
        for stmt in stmts {
            last = self.lower_stmt(stmt);
        }
        last
    }

    /// Lower an expression in tail position (enables TCO for self-recursive calls).
    fn lower_expr_tail(&mut self, expr: &ast::Expr) -> Operand {
        match &expr.kind {
            ast::ExprKind::Call(func, args) => {
                // Check if this is a self-recursive call in tail position
                if let ast::ExprKind::Ident(name) = &func.kind {
                    if *name == self.current_fn_name && !self.tail_call_params.is_empty() {
                        // Self-recursive tail call: write args to params + jump to entry
                        let arg_ops: Vec<_> = args.iter().map(|a| self.lower_expr(a)).collect();
                        // Use temp locals to avoid clobbering params used in arg expressions
                        let temps: Vec<LocalId> = arg_ops.iter().map(|op| {
                            let tmp = self.alloc_local(None, self.operand_type(op));
                            self.emit(Statement { dest: tmp, rvalue: Rvalue::Use(op.clone()) });
                            tmp
                        }).collect();
                        // Write temps to params
                        for (i, tmp) in temps.iter().enumerate() {
                            if i < self.tail_call_params.len() {
                                self.emit(Statement {
                                    dest: self.tail_call_params[i],
                                    rvalue: Rvalue::Use(Operand::Local(*tmp)),
                                });
                            }
                        }
                        self.terminate(Terminator::Goto(self.tail_call_entry));
                        // Switch to a dead block so the caller can continue emitting
                        let dead = self.new_block();
                        self.current_block = dead;
                        return Operand::ConstUnit;
                    }
                }
                // Not a self-recursive call, lower normally
                self.lower_expr(expr)
            }

            ast::ExprKind::Match(scrutinee, arms) => {
                let scrut = self.lower_expr(scrutinee);
                let merge_block = self.new_block();
                let result = self.alloc_local(None, MirType::Int);
                self.lower_match_chain(&scrut, &arms.iter().collect::<Vec<_>>(), result, merge_block, true);
                self.current_block = merge_block;
                Operand::Local(result)
            }

            ast::ExprKind::Block(stmts) => self.lower_block_tail(stmts),

            // Everything else: not a tail-call candidate, lower normally
            _ => self.lower_expr(expr),
        }
    }

    /// Lower a block where the last expression is in tail position.
    fn lower_block_tail(&mut self, stmts: &[ast::Stmt]) -> Operand {
        if stmts.is_empty() {
            return Operand::ConstUnit;
        }
        let mut last = Operand::ConstUnit;
        for (i, stmt) in stmts.iter().enumerate() {
            if i == stmts.len() - 1 {
                // Last statement is in tail position
                match &stmt.kind {
                    ast::StmtKind::Expr(expr) => {
                        last = self.lower_expr_tail(expr);
                    }
                    _ => {
                        last = self.lower_stmt(stmt);
                    }
                }
            } else {
                last = self.lower_stmt(stmt);
            }
        }
        last
    }

    fn lower_stmt(&mut self, stmt: &ast::Stmt) -> Operand {
        match &stmt.kind {
            ast::StmtKind::Expr(expr) => self.lower_expr(expr),
            ast::StmtKind::Let(name, expr) => {
                let val = self.lower_expr(expr);
                let val_ty = self.operand_type(&val);
                let dest = self.alloc_local(Some(name.clone()), val_ty);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Use(val),
                });
                Operand::ConstUnit
            }
            ast::StmtKind::Assign(lhs, rhs) => {
                let rhs_val = self.lower_expr(rhs);
                match &lhs.kind {
                    ast::ExprKind::Ident(name) => {
                        // Variable reassignment: find existing local and overwrite
                        let local_id = self.locals.iter()
                            .rev()
                            .find(|l| l.name.as_deref() == Some(name.as_str()))
                            .map(|l| l.id);
                        if let Some(id) = local_id {
                            self.emit(Statement {
                                dest: id,
                                rvalue: Rvalue::Use(rhs_val),
                            });
                        }
                    }
                    ast::ExprKind::Index(arr_expr, idx_expr) => {
                        // Array index assignment: arr[i] := val
                        let arr_op = self.lower_expr(arr_expr);
                        let idx_op = self.lower_expr(idx_expr);
                        let scratch = self.alloc_local(None, MirType::Int);
                        self.emit(Statement {
                            dest: scratch,
                            rvalue: Rvalue::Call(
                                Operand::ExternRef("glyph_array_set".to_string()),
                                vec![arr_op, idx_op, rhs_val],
                            ),
                        });
                    }
                    _ => {
                        // Other lvalue forms (field access, etc.) — not yet supported
                    }
                }
                Operand::ConstUnit
            }
        }
    }

    fn lower_match_chain(
        &mut self,
        scrutinee: &Operand,
        arms: &[&ast::MatchArm],
        result: LocalId,
        merge: BlockId,
        tail: bool,
    ) {
        if arms.is_empty() {
            // Unreachable
            self.terminate(Terminator::Unreachable);
            return;
        }

        let arm = arms[0];
        let rest = &arms[1..];

        match &arm.pattern.kind {
            ast::PatternKind::Wildcard | ast::PatternKind::Ident(_) => {
                // Always matches
                if let ast::PatternKind::Ident(name) = &arm.pattern.kind {
                    let scrut_ty = self.operand_type(scrutinee);
                    let local = self.alloc_local(Some(name.clone()), scrut_ty);
                    self.emit(Statement {
                        dest: local,
                        rvalue: Rvalue::Use(scrutinee.clone()),
                    });
                }
                let val = if tail { self.lower_expr_tail(&arm.body) } else { self.lower_expr(&arm.body) };
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(val),
                });
                self.terminate(Terminator::Goto(merge));
            }
            ast::PatternKind::BoolLit(b) => {
                let match_block = self.new_block();
                let next_block = self.new_block();

                // Compare bool scrutinee with expected value
                let cmp_local = self.alloc_local(None, MirType::Bool);
                self.emit(Statement {
                    dest: cmp_local,
                    rvalue: Rvalue::BinOp(BinOp::Eq, scrutinee.clone(), Operand::ConstBool(*b)),
                });
                self.terminate(Terminator::Branch(
                    Operand::Local(cmp_local),
                    match_block,
                    next_block,
                ));

                // Match block
                self.current_block = match_block;
                let val = if tail { self.lower_expr_tail(&arm.body) } else { self.lower_expr(&arm.body) };
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(val),
                });
                self.terminate(Terminator::Goto(merge));

                // Next block
                self.current_block = next_block;
                self.lower_match_chain(scrutinee, rest, result, merge, tail);
            }
            ast::PatternKind::IntLit(n) => {
                let match_block = self.new_block();
                let next_block = self.new_block();

                // Compare
                let cmp_local = self.alloc_local(None, MirType::Bool);
                self.emit(Statement {
                    dest: cmp_local,
                    rvalue: Rvalue::BinOp(BinOp::Eq, scrutinee.clone(), Operand::ConstInt(*n)),
                });
                self.terminate(Terminator::Branch(
                    Operand::Local(cmp_local),
                    match_block,
                    next_block,
                ));

                // Match block
                self.current_block = match_block;
                let val = if tail { self.lower_expr_tail(&arm.body) } else { self.lower_expr(&arm.body) };
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(val),
                });
                self.terminate(Terminator::Goto(merge));

                // Next block
                self.current_block = next_block;
                self.lower_match_chain(scrutinee, rest, result, merge, tail);
            }
            ast::PatternKind::StrLit(s) => {
                let match_block = self.new_block();
                let next_block = self.new_block();

                // Compare using glyph_str_eq(scrutinee, pattern_str)
                let pat_str = self.alloc_local(None, MirType::Str);
                self.emit(Statement {
                    dest: pat_str,
                    rvalue: Rvalue::Use(Operand::ConstStr(s.clone())),
                });
                let cmp_local = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest: cmp_local,
                    rvalue: Rvalue::Call(
                        Operand::ExternRef("glyph_str_eq".to_string()),
                        vec![scrutinee.clone(), Operand::Local(pat_str)],
                    ),
                });
                let cmp_bool = self.alloc_local(None, MirType::Bool);
                self.emit(Statement {
                    dest: cmp_bool,
                    rvalue: Rvalue::BinOp(BinOp::Neq, Operand::Local(cmp_local), Operand::ConstInt(0)),
                });
                self.terminate(Terminator::Branch(
                    Operand::Local(cmp_bool),
                    match_block,
                    next_block,
                ));

                // Match block
                self.current_block = match_block;
                let val = if tail { self.lower_expr_tail(&arm.body) } else { self.lower_expr(&arm.body) };
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(val),
                });
                self.terminate(Terminator::Goto(merge));

                // Next block
                self.current_block = next_block;
                self.lower_match_chain(scrutinee, rest, result, merge, tail);
            }
            ast::PatternKind::Constructor(variant_name, sub_pats) => {
                let match_block = self.new_block();
                let next_block = self.new_block();

                // Load discriminant from scrutinee (offset 0)
                let disc_local = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest: disc_local,
                    rvalue: Rvalue::Field(scrutinee.clone(), 0, "__tag".into()),
                });

                // Look up expected discriminant and field types
                let (expected_disc, variant_field_types) = self.enum_variants
                    .get(variant_name)
                    .map(|(_, d, ftypes)| (*d as i64, ftypes.clone()))
                    .unwrap_or((0, vec![]));

                // Compare
                let cmp_local = self.alloc_local(None, MirType::Bool);
                self.emit(Statement {
                    dest: cmp_local,
                    rvalue: Rvalue::BinOp(BinOp::Eq, Operand::Local(disc_local), Operand::ConstInt(expected_disc)),
                });
                self.terminate(Terminator::Branch(
                    Operand::Local(cmp_local),
                    match_block,
                    next_block,
                ));

                // Match block: extract payload fields and bind sub-pattern variables
                self.current_block = match_block;
                for (i, sub_pat) in sub_pats.iter().enumerate() {
                    if let ast::PatternKind::Ident(name) = &sub_pat.kind {
                        // Extract payload field at offset (1 + i) in 8-byte stride
                        let field_ty = variant_field_types.get(i).cloned().unwrap_or(MirType::Int);
                        let field_local = self.alloc_local(Some(name.clone()), field_ty);
                        self.emit(Statement {
                            dest: field_local,
                            rvalue: Rvalue::Field(scrutinee.clone(), (1 + i) as u32, format!("__payload{}", i)),
                        });
                    } else if let ast::PatternKind::Wildcard = &sub_pat.kind {
                        // Skip wildcard sub-patterns
                    }
                    // TODO: nested constructor patterns
                }
                let val = if tail { self.lower_expr_tail(&arm.body) } else { self.lower_expr(&arm.body) };
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(val),
                });
                self.terminate(Terminator::Goto(merge));

                // Next block
                self.current_block = next_block;
                self.lower_match_chain(scrutinee, rest, result, merge, tail);
            }

            _ => {
                // For other patterns, treat as wildcard for now
                let val = if tail { self.lower_expr_tail(&arm.body) } else { self.lower_expr(&arm.body) };
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(val),
                });
                self.terminate(Terminator::Goto(merge));
            }
        }
    }

    // ── Closure helpers ──────────────────────────────────────────────

    /// Collect free variables in an expression that are locals in the enclosing scope.
    /// Returns (name, type, outer_local_id) for each captured variable.
    fn collect_free_vars(
        &self,
        expr: &ast::Expr,
        bound: &HashSet<String>,
    ) -> Vec<(String, MirType, LocalId)> {
        let mut result = Vec::new();
        let mut seen = HashSet::new();
        self.walk_free_vars(expr, bound, &mut seen, &mut result);
        result
    }

    fn walk_free_vars(
        &self,
        expr: &ast::Expr,
        bound: &HashSet<String>,
        seen: &mut HashSet<String>,
        result: &mut Vec<(String, MirType, LocalId)>,
    ) {
        match &expr.kind {
            ast::ExprKind::Ident(name) => {
                if !bound.contains(name) && !seen.contains(name) {
                    // Check if it's a local in the enclosing scope
                    for local in &self.locals {
                        if local.name.as_deref() == Some(name.as_str()) {
                            result.push((name.clone(), local.ty.clone(), local.id));
                            seen.insert(name.clone());
                            break;
                        }
                    }
                }
            }
            ast::ExprKind::Binary(_, l, r)
            | ast::ExprKind::Pipe(l, r)
            | ast::ExprKind::Compose(l, r) => {
                self.walk_free_vars(l, bound, seen, result);
                self.walk_free_vars(r, bound, seen, result);
            }
            ast::ExprKind::Unary(_, e)
            | ast::ExprKind::Propagate(e)
            | ast::ExprKind::Unwrap(e) => {
                self.walk_free_vars(e, bound, seen, result);
            }
            ast::ExprKind::Call(f, args) => {
                self.walk_free_vars(f, bound, seen, result);
                for a in args {
                    self.walk_free_vars(a, bound, seen, result);
                }
            }
            ast::ExprKind::Lambda(params, body) => {
                let mut inner_bound = bound.clone();
                for p in params {
                    inner_bound.insert(p.name.clone());
                }
                self.walk_free_vars(body, &inner_bound, seen, result);
            }
            ast::ExprKind::Match(scrut, arms) => {
                self.walk_free_vars(scrut, bound, seen, result);
                for arm in arms {
                    self.walk_free_vars(&arm.body, bound, seen, result);
                }
            }
            ast::ExprKind::Block(stmts) => {
                let mut block_bound = bound.clone();
                for stmt in stmts {
                    match &stmt.kind {
                        ast::StmtKind::Expr(e) => self.walk_free_vars(e, &block_bound, seen, result),
                        ast::StmtKind::Let(name, e) => {
                            self.walk_free_vars(e, &block_bound, seen, result);
                            block_bound.insert(name.clone());
                        }
                        ast::StmtKind::Assign(l, r) => {
                            self.walk_free_vars(l, &block_bound, seen, result);
                            self.walk_free_vars(r, &block_bound, seen, result);
                        }
                    }
                }
            }
            ast::ExprKind::FieldAccess(e, _) | ast::ExprKind::Index(e, _) => {
                self.walk_free_vars(e, bound, seen, result);
                if let ast::ExprKind::Index(_, idx) = &expr.kind {
                    self.walk_free_vars(idx, bound, seen, result);
                }
            }
            ast::ExprKind::Array(elems) | ast::ExprKind::Tuple(elems) => {
                for e in elems {
                    self.walk_free_vars(e, bound, seen, result);
                }
            }
            ast::ExprKind::Record(fields) => {
                for f in fields {
                    self.walk_free_vars(&f.value, bound, seen, result);
                }
            }
            ast::ExprKind::StrInterp(parts) => {
                for p in parts {
                    if let ast::StringPart::Expr(e) = p {
                        self.walk_free_vars(e, bound, seen, result);
                    }
                }
            }
            ast::ExprKind::ArrayRange(start, end) => {
                self.walk_free_vars(start, bound, seen, result);
                if let Some(e) = end {
                    self.walk_free_vars(e, bound, seen, result);
                }
            }
            // Literals and other leaf nodes — no free vars
            _ => {}
        }
    }

    // ── Helpers ──────────────────────────────────────────────────────

    fn alloc_local(&mut self, name: Option<String>, ty: MirType) -> LocalId {
        let id = self.next_local;
        self.next_local += 1;
        self.locals.push(MirLocal { id, ty, name });
        id
    }

    fn new_block(&mut self) -> BlockId {
        let id = self.next_block;
        self.next_block += 1;
        self.blocks.push(BasicBlock {
            id,
            stmts: Vec::new(),
            terminator: Terminator::Unreachable, // placeholder
        });
        id
    }

    fn emit(&mut self, stmt: Statement) {
        let block = self.current_block as usize;
        if block < self.blocks.len() {
            self.blocks[block].stmts.push(stmt);
        }
    }

    fn terminate(&mut self, term: Terminator) {
        let block = self.current_block as usize;
        if block < self.blocks.len() {
            self.blocks[block].terminator = term;
        }
    }

    fn extract_param_types(&self, ty: &Type, count: usize) -> Vec<MirType> {
        let mut types = Vec::new();
        let mut current = ty;
        for _ in 0..count {
            if let Type::Fn(param, ret) = current {
                types.push(type_to_mir(param));
                current = ret;
            } else {
                types.push(MirType::Int); // fallback
            }
        }
        types
    }

    fn extract_return_type(&self, ty: &Type, param_count: usize) -> MirType {
        let mut current = ty;
        for _ in 0..param_count {
            if let Type::Fn(_, ret) = current {
                current = ret;
            } else {
                return MirType::Void;
            }
        }
        type_to_mir(current)
    }
}

/// Convert a typeck Type to a MIR type.
pub fn type_to_mir(ty: &Type) -> MirType {
    match ty {
        Type::Int => MirType::Int,
        Type::Int32 => MirType::Int32,
        Type::UInt => MirType::UInt,
        Type::Float => MirType::Float,
        Type::Float32 => MirType::Float32,
        Type::Str => MirType::Str,
        Type::Bool => MirType::Bool,
        Type::Void => MirType::Void,
        Type::Never => MirType::Never,
        Type::Fn(a, b) => MirType::Fn(Box::new(type_to_mir(a)), Box::new(type_to_mir(b))),
        Type::Tuple(ts) => MirType::Tuple(ts.iter().map(type_to_mir).collect()),
        Type::Array(t) => MirType::Array(Box::new(type_to_mir(t))),
        Type::Map(_k, _v) => MirType::Record(BTreeMap::new()), // TODO: proper map type
        Type::Opt(t) => MirType::Enum("Opt".into(), vec![("None".into(), vec![]), ("Some".into(), vec![type_to_mir(t)])]),
        Type::Res(t) => MirType::Enum("Res".into(), vec![("Err".into(), vec![MirType::Str]), ("Ok".into(), vec![type_to_mir(t)])]),
        Type::Ref(t) => MirType::Ref(Box::new(type_to_mir(t))),
        Type::Ptr(t) => MirType::Ptr(Box::new(type_to_mir(t))),
        Type::Named(name, _) => MirType::Named(name.clone()),
        Type::Record(row) => {
            let fields: BTreeMap<_, _> = row
                .fields
                .iter()
                .map(|(k, v)| (k.clone(), type_to_mir(v)))
                .collect();
            MirType::Record(fields)
        }
        Type::Var(_) => MirType::Int, // unresolved var → default to Int
        Type::ForAll(_, inner) => type_to_mir(inner),
        Type::Error => MirType::Void,
    }
}

fn lower_binop(op: &ast::BinOp) -> BinOp {
    match op {
        ast::BinOp::Add => BinOp::Add,
        ast::BinOp::Sub => BinOp::Sub,
        ast::BinOp::Mul => BinOp::Mul,
        ast::BinOp::Div => BinOp::Div,
        ast::BinOp::Mod => BinOp::Mod,
        ast::BinOp::Eq => BinOp::Eq,
        ast::BinOp::Neq => BinOp::Neq,
        ast::BinOp::Lt => BinOp::Lt,
        ast::BinOp::Gt => BinOp::Gt,
        ast::BinOp::LtEq => BinOp::LtEq,
        ast::BinOp::GtEq => BinOp::GtEq,
        ast::BinOp::And => BinOp::And,
        ast::BinOp::Or => BinOp::Or,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use glyph_parse::lexer::Lexer;
    use glyph_parse::parser::Parser;
    use glyph_parse::token::TokenKind;

    fn lower_fn_str(source: &str) -> MirFunction {
        let tokens = Lexer::new(source).tokenize();
        let mut parser = Parser::new(tokens);
        let name = if let TokenKind::Ident(ref s) = parser.current().kind {
            s.clone()
        } else {
            panic!("expected ident");
        };
        let def = parser.parse_def(&name, "fn").unwrap();
        let fndef = match &def.kind {
            glyph_parse::ast::DefKind::Fn(f) => f,
            _ => panic!("expected fn"),
        };

        let fn_ty = Type::Fn(Box::new(Type::Int), Box::new(Type::Int));
        let mut lower = MirLower::new();
        lower.lower_fn(&name, fndef, &fn_ty)
    }

    #[test]
    fn test_lower_simple() {
        let mir = lower_fn_str("f x = x + 1");
        assert_eq!(mir.name, "f");
        assert_eq!(mir.params.len(), 1);
        assert!(!mir.blocks.is_empty());
        // Should end with a return
        let last_block = mir.blocks.last().unwrap();
        assert!(matches!(last_block.terminator, Terminator::Return(_)));
    }

    #[test]
    fn test_lower_match() {
        let mir = lower_fn_str("f x = match x\n  0 -> 1\n  _ -> 0");
        // Should have multiple blocks (entry, loop_header, match chain, merge)
        assert!(mir.blocks.len() >= 4);
    }

    #[test]
    fn test_lower_constant() {
        let mir = lower_fn_str("f x = 42");
        // Entry block jumps to loop header (for TCO), loop header returns
        let entry = &mir.blocks[0];
        assert!(matches!(entry.terminator, Terminator::Goto(1)));
        let loop_header = &mir.blocks[1];
        assert!(matches!(loop_header.terminator, Terminator::Return(Operand::ConstInt(42))));
    }

    #[test]
    fn test_mir_serialize_roundtrip() {
        let mir = lower_fn_str("f x = x + 1");
        let bytes = bincode::serialize(&mir).unwrap();
        let mir2: MirFunction = bincode::deserialize(&bytes).unwrap();
        assert_eq!(mir.name, mir2.name);
        assert_eq!(mir.blocks.len(), mir2.blocks.len());
    }
}
