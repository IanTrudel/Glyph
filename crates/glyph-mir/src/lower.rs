use std::collections::BTreeMap;

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
}

impl MirLower {
    pub fn new() -> Self {
        Self {
            locals: Vec::new(),
            blocks: Vec::new(),
            current_block: 0,
            next_local: 0,
            next_block: 0,
        }
    }

    /// Lower a function definition into MIR.
    pub fn lower_fn(&mut self, name: &str, fndef: &ast::FnDef, fn_ty: &Type) -> MirFunction {
        self.locals.clear();
        self.blocks.clear();
        self.next_local = 0;
        self.next_block = 0;

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

        // Lower body
        let result = match &fndef.body {
            ast::Body::Expr(expr) => self.lower_expr(expr),
            ast::Body::Block(stmts) => self.lower_block(stmts),
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
                // Look up in locals
                for local in &self.locals {
                    if local.name.as_deref() == Some(name.as_str()) {
                        return Operand::Local(local.id);
                    }
                }
                // Must be a reference to another function
                Operand::FuncRef(name.clone())
            }

            ast::ExprKind::Binary(op, left, right) => {
                let l = self.lower_expr(left);
                let r = self.lower_expr(right);
                let mir_op = lower_binop(op);
                let dest = self.alloc_local(None, MirType::Int); // placeholder type
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::BinOp(mir_op, l, r),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Unary(op, operand) => {
                let inner = self.lower_expr(operand);
                let mir_op = match op {
                    ast::UnaryOp::Neg => UnOp::Neg,
                    ast::UnaryOp::Not => UnOp::Not,
                    ast::UnaryOp::Ref => {
                        if let Operand::Local(id) = inner {
                            let dest = self.alloc_local(None, MirType::Ptr(Box::new(MirType::Int)));
                            self.emit(Statement {
                                dest,
                                rvalue: Rvalue::Ref(id),
                            });
                            return Operand::Local(dest);
                        }
                        return inner;
                    }
                    ast::UnaryOp::Deref => {
                        let dest = self.alloc_local(None, MirType::Int);
                        self.emit(Statement {
                            dest,
                            rvalue: Rvalue::Deref(inner),
                        });
                        return Operand::Local(dest);
                    }
                };
                let dest = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::UnOp(mir_op, inner),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Call(func, args) => {
                let callee = self.lower_expr(func);
                let arg_ops: Vec<_> = args.iter().map(|a| self.lower_expr(a)).collect();
                let dest = self.alloc_local(None, MirType::Int); // placeholder
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Call(callee, arg_ops),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Pipe(left, right) => {
                // x |> f  →  f(x)
                let arg = self.lower_expr(left);
                let func = self.lower_expr(right);
                let dest = self.alloc_local(None, MirType::Int);
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

            ast::ExprKind::FieldAccess(expr_inner, _field) => {
                let record = self.lower_expr(expr_inner);
                let field_idx = 0; // TODO: look up from type
                let dest = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Field(record, field_idx),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Index(expr_inner, idx) => {
                let arr = self.lower_expr(expr_inner);
                let index = self.lower_expr(idx);
                let dest = self.alloc_local(None, MirType::Int);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Index(arr, index),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::If(cond, then_e, else_e) => {
                let cond_op = self.lower_expr(cond);
                let then_block = self.new_block();
                let else_block = self.new_block();
                let merge_block = self.new_block();
                let result = self.alloc_local(None, MirType::Int);

                self.terminate(Terminator::Branch(cond_op, then_block, else_block));

                // Then branch
                self.current_block = then_block;
                let then_val = self.lower_expr(then_e);
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(then_val),
                });
                self.terminate(Terminator::Goto(merge_block));

                // Else branch
                self.current_block = else_block;
                if let Some(else_e) = else_e {
                    let else_val = self.lower_expr(else_e);
                    self.emit(Statement {
                        dest: result,
                        rvalue: Rvalue::Use(else_val),
                    });
                } else {
                    self.emit(Statement {
                        dest: result,
                        rvalue: Rvalue::Use(Operand::ConstUnit),
                    });
                }
                self.terminate(Terminator::Goto(merge_block));

                self.current_block = merge_block;
                Operand::Local(result)
            }

            ast::ExprKind::Match(scrutinee, arms) => {
                let scrut = self.lower_expr(scrutinee);
                let merge_block = self.new_block();
                let result = self.alloc_local(None, MirType::Int);

                // Simple chain of if-else for now (will use decision tree later)
                let remaining_arms: Vec<_> = arms.iter().collect();
                self.lower_match_chain(&scrut, &remaining_arms, result, merge_block);

                self.current_block = merge_block;
                Operand::Local(result)
            }

            ast::ExprKind::Lambda(params, body) => {
                // TODO: closure conversion (Phase 3 extension)
                // For now, lower the body inline
                for p in params {
                    self.alloc_local(Some(p.name.clone()), MirType::Int);
                }
                self.lower_expr(body)
            }

            ast::ExprKind::Array(elems) => {
                let ops: Vec<_> = elems.iter().map(|e| self.lower_expr(e)).collect();
                let dest = self.alloc_local(None, MirType::Array(Box::new(MirType::Int)));
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Aggregate(AggregateKind::Array, ops),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Record(fields) => {
                let names: Vec<String> = fields.iter().map(|f| f.name.clone()).collect();
                let ops: Vec<_> = fields.iter().map(|f| self.lower_expr(&f.value)).collect();
                let dest = self.alloc_local(None, MirType::Record(BTreeMap::new()));
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
                let ops: Vec<_> = parts
                    .iter()
                    .map(|p| match p {
                        ast::StringPart::Lit(s) => Operand::ConstStr(s.clone()),
                        ast::StringPart::Expr(e) => self.lower_expr(e),
                    })
                    .collect();
                let dest = self.alloc_local(None, MirType::Str);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::StrInterp(ops),
                });
                Operand::Local(dest)
            }

            ast::ExprKind::Propagate(inner) => {
                // expr? → match expr { Ok(v) -> v, Err(e) -> return Err(e) }
                let val = self.lower_expr(inner);
                // Simplified: just use the value directly for now
                val
            }

            ast::ExprKind::Unwrap(inner) => {
                // expr! → match expr { Ok(v)/Some(v) -> v, _ -> panic }
                let val = self.lower_expr(inner);
                val
            }

            ast::ExprKind::Block(stmts) => self.lower_block(stmts),

            ast::ExprKind::FieldAccessor(field) => {
                // .field → treated as a function reference
                Operand::FuncRef(format!(".{field}"))
            }

            ast::ExprKind::For(_pat, iter, _filter, body) => {
                // Desugar to iterator operations
                let iter_op = self.lower_expr(iter);
                let _body_op = self.lower_expr(body);
                // TODO: emit proper loop
                iter_op
            }

            ast::ExprKind::TypeIdent(name) => Operand::FuncRef(name.clone()),
            ast::ExprKind::ByteStrLit(bytes) => Operand::ConstStr(String::from_utf8_lossy(bytes).to_string()),
            ast::ExprKind::ArrayRange(start, _end) => {
                let _s = self.lower_expr(start);
                // TODO: emit range construction
                Operand::ConstUnit
            }
        }
    }

    fn lower_block(&mut self, stmts: &[ast::Stmt]) -> Operand {
        let mut last = Operand::ConstUnit;
        for stmt in stmts {
            last = self.lower_stmt(stmt);
        }
        last
    }

    fn lower_stmt(&mut self, stmt: &ast::Stmt) -> Operand {
        match &stmt.kind {
            ast::StmtKind::Expr(expr) => self.lower_expr(expr),
            ast::StmtKind::Let(name, expr) => {
                let val = self.lower_expr(expr);
                let dest = self.alloc_local(Some(name.clone()), MirType::Int);
                self.emit(Statement {
                    dest,
                    rvalue: Rvalue::Use(val),
                });
                Operand::ConstUnit
            }
            ast::StmtKind::Assign(lhs, rhs) => {
                let _l = self.lower_expr(lhs);
                let _r = self.lower_expr(rhs);
                // TODO: emit store
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
                    let local = self.alloc_local(Some(name.clone()), MirType::Int);
                    self.emit(Statement {
                        dest: local,
                        rvalue: Rvalue::Use(scrutinee.clone()),
                    });
                }
                let val = self.lower_expr(&arm.body);
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(val),
                });
                self.terminate(Terminator::Goto(merge));
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
                let val = self.lower_expr(&arm.body);
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(val),
                });
                self.terminate(Terminator::Goto(merge));

                // Next block
                self.current_block = next_block;
                self.lower_match_chain(scrutinee, rest, result, merge);
            }
            _ => {
                // For other patterns, treat as wildcard for now
                let val = self.lower_expr(&arm.body);
                self.emit(Statement {
                    dest: result,
                    rvalue: Rvalue::Use(val),
                });
                self.terminate(Terminator::Goto(merge));
            }
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
    fn test_lower_if() {
        let mir = lower_fn_str("f x = if x > 0: x else: 0");
        // Should have multiple blocks (entry, then, else, merge)
        assert!(mir.blocks.len() >= 4);
    }

    #[test]
    fn test_lower_constant() {
        let mir = lower_fn_str("f x = 42");
        let entry = &mir.blocks[0];
        assert!(matches!(entry.terminator, Terminator::Return(Operand::ConstInt(42))));
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
