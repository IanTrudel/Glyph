use glyph_db::{Database, DefKind, DepEdge};
use glyph_parse::ast;

use crate::error::{Result, TypeError};

/// Resolve names in a parsed definition against the database.
/// Records dependency edges as a side effect.
pub struct Resolver<'a> {
    db: &'a Database,
    current_def_id: i64,
    deps: Vec<(i64, DepEdge)>,
    locals: Vec<Vec<String>>,
}

impl<'a> Resolver<'a> {
    pub fn new(db: &'a Database, current_def_id: i64) -> Self {
        Self {
            db,
            current_def_id,
            deps: Vec::new(),
            locals: vec![Vec::new()],
        }
    }

    /// Resolve a definition and write dependencies to the DB.
    pub fn resolve_def(&mut self, def: &ast::Def) -> Result<()> {
        match &def.kind {
            ast::DefKind::Fn(fndef) => {
                self.push_scope();
                for p in &fndef.params {
                    self.add_local(&p.name);
                }
                self.resolve_body(&fndef.body)?;
                self.pop_scope();
            }
            ast::DefKind::Type(typedef) => {
                self.resolve_type_body(&typedef.body)?;
            }
            ast::DefKind::Const(_) | ast::DefKind::Data(_) => {
                // Raw values — no name resolution needed
            }
            ast::DefKind::Test(tdef) => {
                self.resolve_body(&tdef.body)?;
            }
        }
        Ok(())
    }

    /// Flush recorded dependencies to the database.
    pub fn write_deps(&self) -> std::result::Result<(), glyph_db::DbError> {
        self.db.clear_deps_from(self.current_def_id)?;
        for (to_id, edge) in &self.deps {
            self.db
                .insert_dep(self.current_def_id, *to_id, *edge)?;
        }
        Ok(())
    }

    fn resolve_body(&mut self, body: &ast::Body) -> Result<()> {
        match body {
            ast::Body::Expr(expr) => self.resolve_expr(expr),
            ast::Body::Block(stmts) => {
                self.push_scope();
                for stmt in stmts {
                    self.resolve_stmt(stmt)?;
                }
                self.pop_scope();
                Ok(())
            }
        }
    }

    fn resolve_stmt(&mut self, stmt: &ast::Stmt) -> Result<()> {
        match &stmt.kind {
            ast::StmtKind::Expr(expr) => self.resolve_expr(expr),
            ast::StmtKind::Let(name, expr) => {
                self.resolve_expr(expr)?;
                self.add_local(name);
                Ok(())
            }
            ast::StmtKind::LetDestructure(names, expr) => {
                self.resolve_expr(expr)?;
                for name in names {
                    self.add_local(name);
                }
                Ok(())
            }
        }
    }

    fn resolve_expr(&mut self, expr: &ast::Expr) -> Result<()> {
        match &expr.kind {
            ast::ExprKind::Ident(name) => {
                if !self.is_local(name) {
                    self.resolve_value_name(name, expr.span)?;
                }
                Ok(())
            }
            ast::ExprKind::TypeIdent(name) => {
                self.resolve_type_name(name)?;
                Ok(())
            }
            ast::ExprKind::Binary(_, l, r) | ast::ExprKind::Pipe(l, r) | ast::ExprKind::Compose(l, r) => {
                self.resolve_expr(l)?;
                self.resolve_expr(r)
            }
            ast::ExprKind::Unary(_, e)
            | ast::ExprKind::Propagate(e)
            | ast::ExprKind::Unwrap(e) => self.resolve_expr(e),
            ast::ExprKind::Call(func, args) => {
                self.resolve_expr(func)?;
                for a in args {
                    self.resolve_expr(a)?;
                }
                Ok(())
            }
            ast::ExprKind::FieldAccess(expr, _) => self.resolve_expr(expr),
            ast::ExprKind::Index(expr, idx) => {
                self.resolve_expr(expr)?;
                self.resolve_expr(idx)
            }
            ast::ExprKind::Lambda(params, body) => {
                self.push_scope();
                for p in params {
                    self.add_local(&p.name);
                }
                self.resolve_expr(body)?;
                self.pop_scope();
                Ok(())
            }
            ast::ExprKind::Match(scrutinee, arms) => {
                self.resolve_expr(scrutinee)?;
                for arm in arms {
                    self.push_scope();
                    self.bind_pattern(&arm.pattern);
                    if let Some(guard) = &arm.guard {
                        self.resolve_expr(guard)?;
                    }
                    self.resolve_expr(&arm.body)?;
                    self.pop_scope();
                }
                Ok(())
            }
            ast::ExprKind::Block(stmts) => {
                self.push_scope();
                for s in stmts {
                    self.resolve_stmt(s)?;
                }
                self.pop_scope();
                Ok(())
            }
            ast::ExprKind::Array(elems) | ast::ExprKind::Tuple(elems) => {
                for e in elems {
                    self.resolve_expr(e)?;
                }
                Ok(())
            }
            ast::ExprKind::ArrayRange(start, end) => {
                self.resolve_expr(start)?;
                if let Some(e) = end {
                    self.resolve_expr(e)?;
                }
                Ok(())
            }
            ast::ExprKind::Record(fields) => {
                for f in fields {
                    self.resolve_expr(&f.value)?;
                }
                Ok(())
            }
            ast::ExprKind::StrInterp(parts) => {
                for part in parts {
                    if let ast::StringPart::Expr(e) = part {
                        self.resolve_expr(e)?;
                    }
                }
                Ok(())
            }
            // Literals and field accessors need no resolution
            ast::ExprKind::IntLit(_)
            | ast::ExprKind::FloatLit(_)
            | ast::ExprKind::StrLit(_)
            | ast::ExprKind::ByteStrLit(_)
            | ast::ExprKind::BoolLit(_)
            | ast::ExprKind::FieldAccessor(_) => Ok(()),
        }
    }

    fn resolve_type_body(&mut self, _body: &ast::TypeBody) -> Result<()> {
        // For type defs, we might want to resolve referenced types
        // This is a pass-through for now
        Ok(())
    }

    fn resolve_value_name(&mut self, name: &str, span: glyph_parse::span::Span) -> Result<()> {
        // Try to find in DB
        match self.db.resolve_name_any(name) {
            Ok(defs) if !defs.is_empty() => {
                for def in &defs {
                    let edge = match def.kind {
                        DefKind::Fn | DefKind::Const => DepEdge::Calls,
                        DefKind::Type => DepEdge::UsesType,
                        _ => DepEdge::Calls,
                    };
                    self.deps.push((def.id, edge));
                }
                Ok(())
            }
            _ => {
                // Could be an extern
                if self.db.resolve_extern(name).is_ok() {
                    return Ok(());
                }
                // Not found — this is an error but we collect rather than fail
                Err(TypeError::UnresolvedName {
                    name: name.to_string(),
                    span,
                })
            }
        }
    }

    fn resolve_type_name(&mut self, name: &str) -> Result<()> {
        // Builtins don't need resolution
        match name {
            "I" | "Int" | "I32" | "Int32" | "U" | "UInt" | "F" | "Float" | "F32" | "Float32"
            | "S" | "Str" | "B" | "Bool" | "V" | "Void" | "N" | "Never" => return Ok(()),
            _ => {}
        }
        match self.db.resolve_name(name, DefKind::Type) {
            Ok(def) => {
                self.deps.push((def.id, DepEdge::UsesType));
                Ok(())
            }
            Err(_) => {
                // Type might be a type variable — don't error
                Ok(())
            }
        }
    }

    fn bind_pattern(&mut self, pat: &ast::Pattern) {
        match &pat.kind {
            ast::PatternKind::Ident(name) => self.add_local(name),
            ast::PatternKind::Constructor(_, pats) | ast::PatternKind::Tuple(pats) => {
                for p in pats {
                    self.bind_pattern(p);
                }
            }
            ast::PatternKind::Record(fields) => {
                for (name, sub) in fields {
                    if sub.is_none() {
                        self.add_local(name);
                    } else if let Some(p) = sub {
                        self.bind_pattern(p);
                    }
                }
            }
            ast::PatternKind::Or(pats) => {
                for p in pats {
                    self.bind_pattern(p);
                }
            }
            _ => {}
        }
    }

    fn push_scope(&mut self) {
        self.locals.push(Vec::new());
    }

    fn pop_scope(&mut self) {
        self.locals.pop();
    }

    fn add_local(&mut self, name: &str) {
        self.locals.last_mut().unwrap().push(name.to_string());
    }

    fn is_local(&self, name: &str) -> bool {
        self.locals.iter().any(|scope| scope.iter().any(|n| n == name))
    }
}
