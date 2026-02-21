use std::collections::BTreeMap;

use glyph_parse::ast;
use glyph_parse::span::Span;

use crate::builtins;
use crate::env::TypeEnv;
use crate::error::TypeError;
use crate::types::{RowType, Type};
use crate::unify::Substitution;

/// The main type inference engine.
pub struct InferEngine {
    pub subst: Substitution,
    pub env: TypeEnv,
    pub errors: Vec<TypeError>,
}

impl InferEngine {
    pub fn new() -> Self {
        let mut env = TypeEnv::new();
        builtins::register_builtins(&mut env);
        Self {
            subst: Substitution::new(),
            env,
            errors: Vec::new(),
        }
    }

    /// Infer the type of an expression.
    pub fn infer_expr(&mut self, expr: &ast::Expr) -> Type {
        match &expr.kind {
            ast::ExprKind::IntLit(_) => Type::Int,
            ast::ExprKind::FloatLit(_) => Type::Float,
            ast::ExprKind::StrLit(_) => Type::Str,
            ast::ExprKind::StrInterp(_) => Type::Str,
            ast::ExprKind::ByteStrLit(_) => Type::Array(Box::new(Type::UInt)),
            ast::ExprKind::BoolLit(_) => Type::Bool,

            ast::ExprKind::Ident(name) => self.lookup_var(name, expr.span),

            ast::ExprKind::TypeIdent(name) => self.resolve_type_name(name),

            ast::ExprKind::Binary(op, left, right) => {
                let lt = self.infer_expr(left);
                let rt = self.infer_expr(right);
                self.infer_binop(op, &lt, &rt, expr.span)
            }

            ast::ExprKind::Unary(op, operand) => {
                let t = self.infer_expr(operand);
                let resolved = self.subst.resolve(&t);
                match builtins::unaryop_type(op, &resolved) {
                    Some(ty) => ty,
                    None => {
                        // For unresolved vars, create fresh output
                        let out = self.subst.fresh();
                        out
                    }
                }
            }

            ast::ExprKind::Call(func, args) => {
                let ft = self.infer_expr(func);
                self.infer_call(&ft, args, expr.span)
            }

            ast::ExprKind::FieldAccess(expr_inner, field) => {
                let record_ty = self.infer_expr(expr_inner);
                self.infer_field_access(&record_ty, field, expr.span)
            }

            ast::ExprKind::Index(expr_inner, idx) => {
                let ct = self.infer_expr(expr_inner);
                let _it = self.infer_expr(idx);
                let resolved = self.subst.resolve(&ct);
                match &resolved {
                    Type::Array(elem) => *elem.clone(),
                    Type::Str => Type::Str, // string indexing returns string
                    _ => {
                        let elem = self.subst.fresh();
                        elem
                    }
                }
            }

            ast::ExprKind::FieldAccessor(field) => {
                // .field ≡ \x -> x.field
                let rv = self.subst.fresh_var();
                let result = self.subst.fresh();
                let record = Type::Record(RowType::open(
                    [(field.clone(), result.clone())].into_iter().collect(),
                    rv,
                ));
                Type::Fn(Box::new(record), Box::new(result))
            }

            ast::ExprKind::Pipe(left, right) => {
                // x |> f  ≡  f(x)
                let xt = self.infer_expr(left);
                let ft = self.infer_expr(right);
                let ret = self.subst.fresh();
                let expected_fn = Type::Fn(Box::new(xt), Box::new(ret.clone()));
                if let Err(e) = self.subst.unify(&ft, &expected_fn) {
                    self.errors.push(e);
                }
                ret
            }

            ast::ExprKind::Compose(left, right) => {
                // f >> g  ≡  \x -> g(f(x))
                let a = self.subst.fresh();
                let b = self.subst.fresh();
                let c = self.subst.fresh();
                let ft = self.infer_expr(left);
                let gt = self.infer_expr(right);
                let f_expected = Type::Fn(Box::new(a.clone()), Box::new(b.clone()));
                let g_expected = Type::Fn(Box::new(b), Box::new(c.clone()));
                if let Err(e) = self.subst.unify(&ft, &f_expected) {
                    self.errors.push(e);
                }
                if let Err(e) = self.subst.unify(&gt, &g_expected) {
                    self.errors.push(e);
                }
                Type::Fn(Box::new(a), Box::new(c))
            }

            ast::ExprKind::Propagate(inner) => {
                // expr? : if expr : !T, result is T
                let t = self.infer_expr(inner);
                let inner_ty = self.subst.fresh();
                let res_ty = Type::Res(Box::new(inner_ty.clone()));
                if let Err(e) = self.subst.unify(&t, &res_ty) {
                    // Try optional
                    let opt_ty = Type::Opt(Box::new(inner_ty.clone()));
                    if let Err(_e2) = self.subst.unify(&t, &opt_ty) {
                        self.errors.push(e);
                    }
                }
                inner_ty
            }

            ast::ExprKind::Unwrap(inner) => {
                let t = self.infer_expr(inner);
                let inner_ty = self.subst.fresh();
                let res_ty = Type::Res(Box::new(inner_ty.clone()));
                if let Err(_) = self.subst.unify(&t, &res_ty) {
                    let opt_ty = Type::Opt(Box::new(inner_ty.clone()));
                    if let Err(e) = self.subst.unify(&t, &opt_ty) {
                        self.errors.push(e);
                    }
                }
                inner_ty
            }

            ast::ExprKind::Lambda(params, body) => {
                self.env.push_scope();
                let mut param_types = Vec::new();
                for p in params {
                    let pt = if let Some(ty_expr) = &p.ty {
                        self.type_expr_to_type(ty_expr)
                    } else {
                        self.subst.fresh()
                    };
                    self.env.insert(p.name.clone(), pt.clone());
                    param_types.push(pt);
                }
                let body_ty = self.infer_expr(body);
                self.env.pop_scope();
                Type::func(param_types, body_ty)
            }

            ast::ExprKind::If(cond, then_e, else_e) => {
                let ct = self.infer_expr(cond);
                if let Err(e) = self.subst.unify(&ct, &Type::Bool) {
                    self.errors.push(e);
                }
                let tt = self.infer_expr(then_e);
                if let Some(else_e) = else_e {
                    let et = self.infer_expr(else_e);
                    if let Err(e) = self.subst.unify(&tt, &et) {
                        self.errors.push(e);
                    }
                    tt
                } else {
                    tt
                }
            }

            ast::ExprKind::Match(scrutinee, arms) => {
                let st = self.infer_expr(scrutinee);
                let result_ty = self.subst.fresh();
                for arm in arms {
                    let pat_ty = self.infer_pattern(&arm.pattern);
                    if let Err(e) = self.subst.unify(&st, &pat_ty) {
                        self.errors.push(e);
                    }
                    self.env.push_scope();
                    self.bind_pattern_vars(&arm.pattern);
                    let body_ty = self.infer_expr(&arm.body);
                    if let Err(e) = self.subst.unify(&result_ty, &body_ty) {
                        self.errors.push(e);
                    }
                    self.env.pop_scope();
                }
                result_ty
            }

            ast::ExprKind::For(pat, iter, filter, body) => {
                let iter_ty = self.infer_expr(iter);
                let elem_ty = self.subst.fresh();
                let array_ty = Type::Array(Box::new(elem_ty.clone()));
                if let Err(e) = self.subst.unify(&iter_ty, &array_ty) {
                    self.errors.push(e);
                }
                self.env.push_scope();
                let pat_ty = self.infer_pattern(pat);
                if let Err(e) = self.subst.unify(&elem_ty, &pat_ty) {
                    self.errors.push(e);
                }
                self.bind_pattern_vars(pat);
                if let Some(f) = filter {
                    let ft = self.infer_expr(f);
                    if let Err(e) = self.subst.unify(&ft, &Type::Bool) {
                        self.errors.push(e);
                    }
                }
                let body_ty = self.infer_expr(body);
                self.env.pop_scope();
                Type::Array(Box::new(body_ty))
            }

            ast::ExprKind::Block(stmts) => {
                self.env.push_scope();
                let mut last_ty = Type::Void;
                for stmt in stmts {
                    last_ty = self.infer_stmt(stmt);
                }
                self.env.pop_scope();
                last_ty
            }

            ast::ExprKind::Array(elems) => {
                if elems.is_empty() {
                    Type::Array(Box::new(self.subst.fresh()))
                } else {
                    let first = self.infer_expr(&elems[0]);
                    for e in &elems[1..] {
                        let t = self.infer_expr(e);
                        if let Err(e) = self.subst.unify(&first, &t) {
                            self.errors.push(e);
                        }
                    }
                    Type::Array(Box::new(first))
                }
            }

            ast::ExprKind::ArrayRange(start, end) => {
                let st = self.infer_expr(start);
                if let Err(e) = self.subst.unify(&st, &Type::Int) {
                    self.errors.push(e);
                }
                if let Some(end) = end {
                    let et = self.infer_expr(end);
                    if let Err(e) = self.subst.unify(&et, &Type::Int) {
                        self.errors.push(e);
                    }
                }
                Type::Array(Box::new(Type::Int))
            }

            ast::ExprKind::Record(fields) => {
                let mut field_types = BTreeMap::new();
                for f in fields {
                    let ft = self.infer_expr(&f.value);
                    field_types.insert(f.name.clone(), ft);
                }
                Type::Record(RowType::closed(field_types))
            }

            ast::ExprKind::Tuple(elems) => {
                let types: Vec<_> = elems.iter().map(|e| self.infer_expr(e)).collect();
                Type::Tuple(types)
            }
        }
    }

    /// Infer the type of a statement, returns the type of the last expression.
    pub fn infer_stmt(&mut self, stmt: &ast::Stmt) -> Type {
        match &stmt.kind {
            ast::StmtKind::Expr(expr) => self.infer_expr(expr),
            ast::StmtKind::Let(name, expr) => {
                let ty = self.infer_expr(expr);
                let scheme = self.env.generalize(&mut self.subst, &ty);
                self.env.insert(name.clone(), scheme);
                Type::Void
            }
            ast::StmtKind::Assign(_lhs, rhs) => {
                self.infer_expr(rhs);
                Type::Void
            }
        }
    }

    /// Infer the type of a function definition.
    pub fn infer_fn_def(&mut self, fndef: &ast::FnDef) -> Type {
        self.env.push_scope();
        let mut param_types = Vec::new();
        for p in &fndef.params {
            let pt = if let Some(ty_expr) = &p.ty {
                self.type_expr_to_type(ty_expr)
            } else {
                self.subst.fresh()
            };
            self.env.insert(p.name.clone(), pt.clone());
            param_types.push(pt);
        }

        let body_ty = match &fndef.body {
            ast::Body::Expr(expr) => self.infer_expr(expr),
            ast::Body::Block(stmts) => {
                self.env.push_scope();
                let mut last = Type::Void;
                for s in stmts {
                    last = self.infer_stmt(s);
                }
                self.env.pop_scope();
                last
            }
        };

        // Check against declared return type
        if let Some(ret_ty_expr) = &fndef.ret_ty {
            let declared = self.type_expr_to_type(ret_ty_expr);
            if let Err(e) = self.subst.unify(&body_ty, &declared) {
                self.errors.push(e);
            }
        }

        self.env.pop_scope();

        let fn_ty = Type::func(param_types, body_ty);
        fn_ty
    }

    /// Convert a parsed type expression to a Type.
    pub fn type_expr_to_type(&mut self, texpr: &ast::TypeExpr) -> Type {
        match &texpr.kind {
            ast::TypeExprKind::Named(name) => self.resolve_type_name(name),
            ast::TypeExprKind::App(name, args) => {
                let arg_types: Vec<_> = args.iter().map(|a| self.type_expr_to_type(a)).collect();
                Type::Named(name.clone(), arg_types)
            }
            ast::TypeExprKind::Fn(a, b) => {
                Type::Fn(
                    Box::new(self.type_expr_to_type(a)),
                    Box::new(self.type_expr_to_type(b)),
                )
            }
            ast::TypeExprKind::Tuple(ts) => {
                Type::Tuple(ts.iter().map(|t| self.type_expr_to_type(t)).collect())
            }
            ast::TypeExprKind::Record(fields, has_rest) => {
                let field_types: BTreeMap<_, _> = fields
                    .iter()
                    .map(|(name, ty)| (name.clone(), self.type_expr_to_type(ty)))
                    .collect();
                if *has_rest {
                    let rv = self.subst.fresh_var();
                    Type::Record(RowType::open(field_types, rv))
                } else {
                    Type::Record(RowType::closed(field_types))
                }
            }
            ast::TypeExprKind::Ref(t) => Type::Ref(Box::new(self.type_expr_to_type(t))),
            ast::TypeExprKind::Ptr(t) => Type::Ptr(Box::new(self.type_expr_to_type(t))),
            ast::TypeExprKind::Opt(t) => Type::Opt(Box::new(self.type_expr_to_type(t))),
            ast::TypeExprKind::Res(t) => Type::Res(Box::new(self.type_expr_to_type(t))),
            ast::TypeExprKind::Arr(t) => Type::Array(Box::new(self.type_expr_to_type(t))),
            ast::TypeExprKind::Map(k, v) => {
                Type::Map(
                    Box::new(self.type_expr_to_type(k)),
                    Box::new(self.type_expr_to_type(v)),
                )
            }
        }
    }

    fn resolve_type_name(&mut self, name: &str) -> Type {
        match name {
            "I" | "Int" => Type::Int,
            "I32" | "Int32" => Type::Int32,
            "U" | "UInt" => Type::UInt,
            "F" | "Float" => Type::Float,
            "F32" | "Float32" => Type::Float32,
            "S" | "Str" => Type::Str,
            "B" | "Bool" => Type::Bool,
            "V" | "Void" => Type::Void,
            "N" | "Never" => Type::Never,
            _ => Type::Named(name.to_string(), Vec::new()),
        }
    }

    fn lookup_var(&mut self, name: &str, _span: Span) -> Type {
        if let Some(ty) = self.env.lookup(name) {
            let ty = ty.clone();
            self.instantiate(&ty)
        } else {
            // Create a fresh variable for unresolved names
            self.subst.fresh()
        }
    }

    /// Instantiate a ForAll type with fresh variables.
    pub fn instantiate(&mut self, ty: &Type) -> Type {
        match ty {
            Type::ForAll(vars, inner) => {
                let mut mapping = std::collections::HashMap::new();
                for v in vars {
                    mapping.insert(*v, self.subst.fresh_var());
                }
                self.substitute_vars(inner, &mapping)
            }
            other => other.clone(),
        }
    }

    fn substitute_vars(
        &self,
        ty: &Type,
        mapping: &std::collections::HashMap<u32, u32>,
    ) -> Type {
        match ty {
            Type::Var(v) => {
                if let Some(new_v) = mapping.get(v) {
                    Type::Var(*new_v)
                } else {
                    ty.clone()
                }
            }
            Type::Fn(a, b) => Type::Fn(
                Box::new(self.substitute_vars(a, mapping)),
                Box::new(self.substitute_vars(b, mapping)),
            ),
            Type::Tuple(ts) => {
                Type::Tuple(ts.iter().map(|t| self.substitute_vars(t, mapping)).collect())
            }
            Type::Array(t) => Type::Array(Box::new(self.substitute_vars(t, mapping))),
            Type::Map(k, v) => Type::Map(
                Box::new(self.substitute_vars(k, mapping)),
                Box::new(self.substitute_vars(v, mapping)),
            ),
            Type::Opt(t) => Type::Opt(Box::new(self.substitute_vars(t, mapping))),
            Type::Res(t) => Type::Res(Box::new(self.substitute_vars(t, mapping))),
            Type::Ref(t) => Type::Ref(Box::new(self.substitute_vars(t, mapping))),
            Type::Ptr(t) => Type::Ptr(Box::new(self.substitute_vars(t, mapping))),
            Type::Named(name, args) => Type::Named(
                name.clone(),
                args.iter().map(|a| self.substitute_vars(a, mapping)).collect(),
            ),
            Type::Record(row) => {
                let fields = row
                    .fields
                    .iter()
                    .map(|(k, v)| (k.clone(), self.substitute_vars(v, mapping)))
                    .collect();
                let rest = row.rest.map(|r| *mapping.get(&r).unwrap_or(&r));
                Type::Record(RowType { fields, rest })
            }
            other => other.clone(),
        }
    }

    fn infer_binop(&mut self, op: &ast::BinOp, left: &Type, right: &Type, _span: Span) -> Type {
        let lt = self.subst.resolve(left);
        let rt = self.subst.resolve(right);

        // First try with resolved types
        if let Some(result) = builtins::binop_type(op, &lt, &rt) {
            return result;
        }

        // If one side is a var, unify them and try again
        if let Err(e) = self.subst.unify(&lt, &rt) {
            self.errors.push(e);
            return Type::Error;
        }

        let unified = self.subst.resolve(&lt);
        match op {
            ast::BinOp::Eq | ast::BinOp::Neq | ast::BinOp::Lt | ast::BinOp::Gt
            | ast::BinOp::LtEq | ast::BinOp::GtEq | ast::BinOp::And | ast::BinOp::Or => {
                Type::Bool
            }
            _ => unified,
        }
    }

    fn infer_call(&mut self, func_ty: &Type, args: &[ast::Expr], _span: Span) -> Type {
        let mut current = self.subst.resolve(func_ty);

        for arg in args {
            let arg_ty = self.infer_expr(arg);
            let ret = self.subst.fresh();
            let expected = Type::Fn(Box::new(arg_ty), Box::new(ret.clone()));
            if let Err(e) = self.subst.unify(&current, &expected) {
                self.errors.push(e);
                return Type::Error;
            }
            current = self.subst.resolve(&ret);
        }

        current
    }

    fn infer_field_access(&mut self, record_ty: &Type, field: &str, span: Span) -> Type {
        let resolved = self.subst.resolve(record_ty);
        match &resolved {
            Type::Record(row) => {
                if let Some(ty) = row.fields.get(field) {
                    ty.clone()
                } else if row.rest.is_some() {
                    // Open row: create constraint
                    let field_ty = self.subst.fresh();
                    let rv = self.subst.fresh_var();
                    let required = Type::Record(RowType::open(
                        [(field.to_string(), field_ty.clone())].into_iter().collect(),
                        rv,
                    ));
                    if let Err(e) = self.subst.unify(&resolved, &required) {
                        self.errors.push(e);
                    }
                    field_ty
                } else {
                    self.errors.push(TypeError::MissingField {
                        field: field.to_string(),
                        span,
                    });
                    Type::Error
                }
            }
            Type::Var(_) => {
                // Unknown record: create constraint
                let field_ty = self.subst.fresh();
                let rv = self.subst.fresh_var();
                let required = Type::Record(RowType::open(
                    [(field.to_string(), field_ty.clone())].into_iter().collect(),
                    rv,
                ));
                if let Err(e) = self.subst.unify(&resolved, &required) {
                    self.errors.push(e);
                }
                field_ty
            }
            _ => {
                self.errors.push(TypeError::Custom {
                    message: format!("cannot access field '{field}' on type {resolved}"),
                    span,
                });
                Type::Error
            }
        }
    }

    fn infer_pattern(&mut self, pat: &ast::Pattern) -> Type {
        match &pat.kind {
            ast::PatternKind::Wildcard => self.subst.fresh(),
            ast::PatternKind::Ident(_) => self.subst.fresh(),
            ast::PatternKind::IntLit(_) => Type::Int,
            ast::PatternKind::StrLit(_) => Type::Str,
            ast::PatternKind::Constructor(name, pats) => {
                // For now, return named type
                let args: Vec<_> = pats.iter().map(|p| self.infer_pattern(p)).collect();
                Type::Named(name.clone(), args)
            }
            ast::PatternKind::Record(fields) => {
                let mut field_types = BTreeMap::new();
                for (name, sub) in fields {
                    let ty = if let Some(p) = sub {
                        self.infer_pattern(p)
                    } else {
                        self.subst.fresh()
                    };
                    field_types.insert(name.clone(), ty);
                }
                let rv = self.subst.fresh_var();
                Type::Record(RowType::open(field_types, rv))
            }
            ast::PatternKind::Tuple(pats) => {
                let types: Vec<_> = pats.iter().map(|p| self.infer_pattern(p)).collect();
                Type::Tuple(types)
            }
        }
    }

    fn bind_pattern_vars(&mut self, pat: &ast::Pattern) {
        match &pat.kind {
            ast::PatternKind::Ident(name) => {
                let ty = self.subst.fresh();
                self.env.insert(name.clone(), ty);
            }
            ast::PatternKind::Constructor(_, pats) | ast::PatternKind::Tuple(pats) => {
                for p in pats {
                    self.bind_pattern_vars(p);
                }
            }
            ast::PatternKind::Record(fields) => {
                for (name, sub) in fields {
                    if sub.is_none() {
                        let ty = self.subst.fresh();
                        self.env.insert(name.clone(), ty);
                    } else if let Some(p) = sub {
                        self.bind_pattern_vars(p);
                    }
                }
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use glyph_parse::parser::Parser;

    fn infer(source: &str) -> Type {
        let expr = Parser::parse_expr_str(source).unwrap();
        let mut engine = InferEngine::new();
        let ty = engine.infer_expr(&expr);
        engine.subst.resolve(&ty)
    }

    #[test]
    fn test_infer_int() {
        assert_eq!(infer("42"), Type::Int);
    }

    #[test]
    fn test_infer_string() {
        assert_eq!(infer("\"hello\""), Type::Str);
    }

    #[test]
    fn test_infer_bool() {
        assert_eq!(infer("true"), Type::Bool);
    }

    #[test]
    fn test_infer_add() {
        assert_eq!(infer("1 + 2"), Type::Int);
    }

    #[test]
    fn test_infer_comparison() {
        assert_eq!(infer("1 < 2"), Type::Bool);
    }

    #[test]
    fn test_infer_lambda() {
        let ty = infer("\\x -> x + 1");
        let resolved = format!("{ty}");
        assert!(resolved.contains("->"), "expected function type, got {resolved}");
    }

    #[test]
    fn test_infer_if() {
        assert_eq!(infer("if true: 1 else: 2"), Type::Int);
    }

    #[test]
    fn test_infer_array() {
        let ty = infer("[1, 2, 3]");
        assert_eq!(ty, Type::Array(Box::new(Type::Int)));
    }

    #[test]
    fn test_infer_record() {
        let ty = infer("{name: \"alice\", age: 42}");
        if let Type::Record(row) = &ty {
            assert_eq!(row.fields.len(), 2);
            assert_eq!(row.fields["age"], Type::Int);
            assert_eq!(row.fields["name"], Type::Str);
        } else {
            panic!("expected Record, got {ty}");
        }
    }

    #[test]
    fn test_infer_field_accessor() {
        let ty = infer(".name");
        // Should be: {name:t0 ..} -> t0
        assert!(matches!(ty, Type::Fn(_, _)), "expected Fn, got {ty}");
    }

    #[test]
    fn test_infer_pipe() {
        // With builtins, pipe inference works
        let expr = Parser::parse_expr_str("\\x -> x |> \\y -> y + 1").unwrap();
        let mut engine = InferEngine::new();
        let ty = engine.infer_expr(&expr);
        let resolved = engine.subst.resolve(&ty);
        assert!(matches!(resolved, Type::Fn(_, _)));
    }
}
