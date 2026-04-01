use crate::ast::*;
use crate::error::ParseError;
use crate::lexer::Lexer;
use crate::span::Span;
use crate::token::{Token, TokenKind};

type Result<T> = std::result::Result<T, ParseError>;

/// Recursive-descent parser for Glyph.
/// Parses a single definition body given its kind from the DB.
pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Self { tokens, pos: 0 }
    }

    /// Convenience: lex + parse an expression.
    pub fn parse_expr_str(source: &str) -> Result<Expr> {
        let tokens = Lexer::new(source).tokenize();
        let mut parser = Parser::new(tokens);
        parser.parse_expr()
    }

    /// Parse a definition body given its kind.
    pub fn parse_def(&mut self, name: &str, kind: &str) -> Result<Def> {
        let start = self.span();
        match kind {
            "fn" => self.parse_fn_def(name),
            "type" => self.parse_type_def(name),
            "test" => self.parse_test_def(),
            _ => Err(ParseError::Custom {
                message: format!("unknown definition kind: {kind}"),
                span: start,
            }),
        }
    }

    // ── Function definition ──────────────────────────────────────────

    fn parse_fn_def(&mut self, name: &str) -> Result<Def> {
        let start = self.span();
        // name has already been identified from the DB row.
        // Body format: `name param1 param2 : RetType = body`
        // The name token should be first.
        let parsed_name = self.expect_ident()?;
        assert_eq!(parsed_name, name, "function name mismatch");

        let params = self.parse_params()?;
        let ret_ty = if self.check(&TokenKind::Colon) {
            self.advance();
            Some(self.parse_type_expr()?)
        } else {
            None
        };
        self.expect(&TokenKind::Eq)?;
        let body = self.parse_body()?;

        Ok(Def {
            id: None,
            name: name.to_string(),
            kind: DefKind::Fn(FnDef {
                params,
                ret_ty,
                body,
            }),
            span: start.merge(self.prev_span()),
        })
    }

    fn parse_params(&mut self) -> Result<Vec<Param>> {
        let mut params = Vec::new();
        while self.check_ident() || self.check(&TokenKind::Minus) {
            // Check for flag params: --name:Type=default
            if self.check(&TokenKind::Minus) && self.peek_at(1).map_or(false, |t| matches!(t.kind, TokenKind::Minus)) {
                self.advance(); // first -
                self.advance(); // second -
                let span = self.span();
                let name = self.expect_ident()?;
                let ty = if self.check(&TokenKind::Colon) {
                    self.advance();
                    Some(self.parse_type_expr()?)
                } else {
                    None
                };
                let default = if self.check(&TokenKind::Eq) {
                    self.advance();
                    Some(self.parse_atom()?)
                } else {
                    None
                };
                params.push(Param {
                    name,
                    ty,
                    default,
                    is_flag: true,
                    span: span.merge(self.prev_span()),
                });
            } else {
                let span = self.span();
                let name = self.expect_ident()?;
                let ty = if self.check(&TokenKind::Colon) {
                    self.advance();
                    Some(self.parse_type_expr()?)
                } else {
                    None
                };
                params.push(Param {
                    name,
                    ty,
                    default: None,
                    is_flag: false,
                    span: span.merge(self.prev_span()),
                });
            }
        }
        Ok(params)
    }

    fn parse_body(&mut self) -> Result<Body> {
        self.skip_newlines();
        if self.check(&TokenKind::Indent) {
            self.advance();
            let mut stmts = Vec::new();
            while !self.check(&TokenKind::Dedent) && !self.check(&TokenKind::Eof) {
                stmts.push(self.parse_stmt()?);
                self.skip_newlines();
            }
            if self.check(&TokenKind::Dedent) {
                self.advance();
            }
            Ok(Body::Block(stmts))
        } else {
            Ok(Body::Expr(self.parse_expr()?))
        }
    }

    fn parse_stmt(&mut self) -> Result<Stmt> {
        let start = self.span();

        // Check for let destructuring: {ident, ident, ...} = expr
        if self.check(&TokenKind::LBrace) {
            if let Some(tok1) = self.peek_at(1) {
                if matches!(tok1.kind, TokenKind::Ident(_)) {
                    if let Some(tok2) = self.peek_at(2) {
                        if matches!(tok2.kind, TokenKind::Comma | TokenKind::RBrace) {
                            return self.parse_let_destructure(start);
                        }
                    }
                }
            }
        }

        let expr = self.parse_expr()?;

        // Check for let binding: ident = expr (only if expr is a bare ident)
        if self.check(&TokenKind::Eq) {
            if let ExprKind::Ident(name) = &expr.kind {
                let name = name.clone();
                self.advance();
                let rhs = self.parse_expr()?;
                return Ok(Stmt {
                    span: start.merge(self.prev_span()),
                    kind: StmtKind::Let(name, rhs),
                });
            }
        }

        Ok(Stmt {
            span: start.merge(self.prev_span()),
            kind: StmtKind::Expr(expr),
        })
    }

    fn parse_let_destructure(&mut self, start: Span) -> Result<Stmt> {
        self.advance(); // consume '{'
        let mut names = Vec::new();
        loop {
            if let TokenKind::Ident(name) = &self.current().kind {
                names.push(name.clone());
                self.advance();
            } else {
                return Err(ParseError::Custom {
                    message: "expected field name in destructuring pattern".to_string(),
                    span: self.span(),
                });
            }
            if self.check(&TokenKind::Comma) {
                self.advance();
            } else {
                break;
            }
        }
        self.expect(&TokenKind::RBrace)?;
        self.expect(&TokenKind::Eq)?;
        let rhs = self.parse_expr()?;
        Ok(Stmt {
            span: start.merge(self.prev_span()),
            kind: StmtKind::LetDestructure(names, rhs),
        })
    }

    // ── Expressions (precedence climbing) ────────────────────────────

    pub fn parse_expr(&mut self) -> Result<Expr> {
        self.parse_pipe()
    }

    fn parse_pipe(&mut self) -> Result<Expr> {
        let mut left = self.parse_compose()?;
        while self.check(&TokenKind::PipeGt) {
            self.advance();
            let right = self.parse_compose()?;
            let span = left.span.merge(right.span);
            left = Expr {
                kind: ExprKind::Pipe(Box::new(left), Box::new(right)),
                span,
            };
        }
        Ok(left)
    }

    fn parse_compose(&mut self) -> Result<Expr> {
        let mut left = self.parse_or()?;
        while self.check(&TokenKind::GtGt) {
            self.advance();
            let right = self.parse_or()?;
            let span = left.span.merge(right.span);
            left = Expr {
                kind: ExprKind::Compose(Box::new(left), Box::new(right)),
                span,
            };
        }
        Ok(left)
    }

    fn parse_or(&mut self) -> Result<Expr> {
        let mut left = self.parse_and()?;
        while self.check(&TokenKind::Or) {
            self.advance();
            let right = self.parse_and()?;
            let span = left.span.merge(right.span);
            left = Expr {
                kind: ExprKind::Binary(BinOp::Or, Box::new(left), Box::new(right)),
                span,
            };
        }
        Ok(left)
    }

    fn parse_and(&mut self) -> Result<Expr> {
        let mut left = self.parse_cmp()?;
        while self.check(&TokenKind::And) {
            self.advance();
            let right = self.parse_cmp()?;
            let span = left.span.merge(right.span);
            left = Expr {
                kind: ExprKind::Binary(BinOp::And, Box::new(left), Box::new(right)),
                span,
            };
        }
        Ok(left)
    }

    fn parse_cmp(&mut self) -> Result<Expr> {
        let mut left = self.parse_add()?;
        loop {
            let op = match self.current_kind() {
                TokenKind::EqEq => BinOp::Eq,
                TokenKind::BangEq => BinOp::Neq,
                TokenKind::Lt => BinOp::Lt,
                TokenKind::Gt => BinOp::Gt,
                TokenKind::LtEq => BinOp::LtEq,
                TokenKind::GtEq => BinOp::GtEq,
                _ => break,
            };
            self.advance();
            let right = self.parse_add()?;
            let span = left.span.merge(right.span);
            left = Expr {
                kind: ExprKind::Binary(op, Box::new(left), Box::new(right)),
                span,
            };
        }
        Ok(left)
    }

    fn parse_add(&mut self) -> Result<Expr> {
        let mut left = self.parse_mul()?;
        loop {
            let op = match self.current_kind() {
                TokenKind::Plus => BinOp::Add,
                TokenKind::Minus => BinOp::Sub,
                _ => break,
            };
            self.advance();
            let right = self.parse_mul()?;
            let span = left.span.merge(right.span);
            left = Expr {
                kind: ExprKind::Binary(op, Box::new(left), Box::new(right)),
                span,
            };
        }
        Ok(left)
    }

    fn parse_mul(&mut self) -> Result<Expr> {
        let mut left = self.parse_unary()?;
        loop {
            let op = match self.current_kind() {
                TokenKind::Star => BinOp::Mul,
                TokenKind::Slash => BinOp::Div,
                TokenKind::Percent => BinOp::Mod,
                _ => break,
            };
            self.advance();
            let right = self.parse_unary()?;
            let span = left.span.merge(right.span);
            left = Expr {
                kind: ExprKind::Binary(op, Box::new(left), Box::new(right)),
                span,
            };
        }
        Ok(left)
    }

    fn parse_unary(&mut self) -> Result<Expr> {
        let start = self.span();
        let op = match self.current_kind() {
            TokenKind::Minus => Some(UnaryOp::Neg),
            TokenKind::Bang => Some(UnaryOp::Not),
            TokenKind::Ampersand => Some(UnaryOp::Ref),
            TokenKind::Star => Some(UnaryOp::Deref),
            _ => None,
        };
        if let Some(op) = op {
            self.advance();
            let expr = self.parse_unary()?;
            Ok(Expr {
                span: start.merge(expr.span),
                kind: ExprKind::Unary(op, Box::new(expr)),
            })
        } else {
            self.parse_postfix()
        }
    }

    fn parse_postfix(&mut self) -> Result<Expr> {
        let mut expr = self.parse_atom()?;
        loop {
            match self.current_kind() {
                TokenKind::Dot => {
                    self.advance();
                    let name = self.expect_ident_or_type_ident()?;
                    let span = expr.span.merge(self.prev_span());
                    expr = Expr {
                        kind: ExprKind::FieldAccess(Box::new(expr), name),
                        span,
                    };
                }
                TokenKind::LParen => {
                    self.advance();
                    let args = self.parse_args()?;
                    self.expect(&TokenKind::RParen)?;
                    let span = expr.span.merge(self.prev_span());
                    expr = Expr {
                        kind: ExprKind::Call(Box::new(expr), args),
                        span,
                    };
                }
                TokenKind::LBracket => {
                    self.advance();
                    let index = self.parse_expr()?;
                    self.expect(&TokenKind::RBracket)?;
                    let span = expr.span.merge(self.prev_span());
                    expr = Expr {
                        kind: ExprKind::Index(Box::new(expr), Box::new(index)),
                        span,
                    };
                }
                TokenKind::Question => {
                    self.advance();
                    let span = expr.span.merge(self.prev_span());
                    expr = Expr {
                        kind: ExprKind::Propagate(Box::new(expr)),
                        span,
                    };
                }
                TokenKind::Bang => {
                    // Only postfix `!` if it's immediately after (no whitespace check needed
                    // in token stream — the lexer already tokenized it)
                    self.advance();
                    let span = expr.span.merge(self.prev_span());
                    expr = Expr {
                        kind: ExprKind::Unwrap(Box::new(expr)),
                        span,
                    };
                }
                _ => break,
            }
        }
        Ok(expr)
    }

    fn parse_args(&mut self) -> Result<Vec<Expr>> {
        let mut args = Vec::new();
        if !self.check(&TokenKind::RParen) {
            args.push(self.parse_expr()?);
            while self.check(&TokenKind::Comma) {
                self.advance();
                if self.check(&TokenKind::RParen) {
                    break; // trailing comma
                }
                args.push(self.parse_expr()?);
            }
        }
        Ok(args)
    }

    fn parse_atom(&mut self) -> Result<Expr> {
        let start = self.span();
        match self.current_kind() {
            TokenKind::Int(n) => {
                let n = n;
                self.advance();
                Ok(Expr {
                    kind: ExprKind::IntLit(n),
                    span: start,
                })
            }
            TokenKind::Float(f) => {
                let f = f;
                self.advance();
                Ok(Expr {
                    kind: ExprKind::FloatLit(f),
                    span: start,
                })
            }
            TokenKind::Str(ref s) => {
                let s = s.clone();
                self.advance();
                Ok(Expr {
                    kind: ExprKind::StrLit(s),
                    span: start,
                })
            }
            TokenKind::StrInterpStart => {
                self.advance();
                let mut parts = Vec::new();
                while !self.check(&TokenKind::StrInterpEnd) && !self.check(&TokenKind::Eof) {
                    if let TokenKind::Str(ref s) = self.current_kind() {
                        parts.push(StringPart::Lit(s.clone()));
                        self.advance();
                    } else {
                        parts.push(StringPart::Expr(self.parse_expr()?));
                    }
                }
                if self.check(&TokenKind::StrInterpEnd) {
                    self.advance();
                }
                Ok(Expr {
                    kind: ExprKind::StrInterp(parts),
                    span: start.merge(self.prev_span()),
                })
            }
            TokenKind::ByteStr(ref b) => {
                let b = b.clone();
                self.advance();
                Ok(Expr {
                    kind: ExprKind::ByteStrLit(b),
                    span: start,
                })
            }
            TokenKind::Ident(ref s) if s == "true" => {
                self.advance();
                Ok(Expr {
                    kind: ExprKind::BoolLit(true),
                    span: start,
                })
            }
            TokenKind::Ident(ref s) if s == "false" => {
                self.advance();
                Ok(Expr {
                    kind: ExprKind::BoolLit(false),
                    span: start,
                })
            }
            TokenKind::Ident(ref s) => {
                let s = s.clone();
                self.advance();
                Ok(Expr {
                    kind: ExprKind::Ident(s),
                    span: start,
                })
            }
            TokenKind::TypeIdent(ref s) => {
                let s = s.clone();
                self.advance();
                Ok(Expr {
                    kind: ExprKind::TypeIdent(s),
                    span: start,
                })
            }
            TokenKind::LParen => {
                self.advance();
                if self.check(&TokenKind::RParen) {
                    self.advance();
                    return Ok(Expr {
                        kind: ExprKind::Tuple(Vec::new()),
                        span: start.merge(self.prev_span()),
                    });
                }
                let expr = self.parse_expr()?;
                if self.check(&TokenKind::Comma) {
                    // Tuple
                    let mut elems = vec![expr];
                    while self.check(&TokenKind::Comma) {
                        self.advance();
                        if self.check(&TokenKind::RParen) {
                            break;
                        }
                        elems.push(self.parse_expr()?);
                    }
                    self.expect(&TokenKind::RParen)?;
                    Ok(Expr {
                        kind: ExprKind::Tuple(elems),
                        span: start.merge(self.prev_span()),
                    })
                } else {
                    self.expect(&TokenKind::RParen)?;
                    Ok(expr) // grouped expression
                }
            }
            TokenKind::LBracket => {
                self.advance();
                if self.check(&TokenKind::RBracket) {
                    self.advance();
                    return Ok(Expr {
                        kind: ExprKind::Array(Vec::new()),
                        span: start.merge(self.prev_span()),
                    });
                }
                let first = self.parse_expr()?;
                if self.check(&TokenKind::DotDot) {
                    self.advance();
                    let end = if self.check(&TokenKind::RBracket) {
                        None
                    } else {
                        Some(Box::new(self.parse_expr()?))
                    };
                    self.expect(&TokenKind::RBracket)?;
                    Ok(Expr {
                        kind: ExprKind::ArrayRange(Box::new(first), end),
                        span: start.merge(self.prev_span()),
                    })
                } else {
                    let mut elems = vec![first];
                    while self.check(&TokenKind::Comma) {
                        self.advance();
                        if self.check(&TokenKind::RBracket) {
                            break;
                        }
                        elems.push(self.parse_expr()?);
                    }
                    self.expect(&TokenKind::RBracket)?;
                    Ok(Expr {
                        kind: ExprKind::Array(elems),
                        span: start.merge(self.prev_span()),
                    })
                }
            }
            TokenKind::LBrace => {
                self.advance();
                if self.check(&TokenKind::RBrace) {
                    self.advance();
                    return Ok(Expr {
                        kind: ExprKind::Record(Vec::new()),
                        span: start.merge(self.prev_span()),
                    });
                }
                let mut fields = Vec::new();
                loop {
                    let fstart = self.span();
                    let name = self.expect_ident()?;
                    self.expect(&TokenKind::Colon)?;
                    let value = self.parse_expr()?;
                    fields.push(FieldInit {
                        name,
                        value,
                        span: fstart.merge(self.prev_span()),
                    });
                    if !self.check(&TokenKind::Comma) && !self.check_ident() {
                        break;
                    }
                    if self.check(&TokenKind::Comma) {
                        self.advance();
                    }
                    if self.check(&TokenKind::RBrace) {
                        break;
                    }
                }
                self.expect(&TokenKind::RBrace)?;
                Ok(Expr {
                    kind: ExprKind::Record(fields),
                    span: start.merge(self.prev_span()),
                })
            }
            TokenKind::Backslash => {
                self.advance();
                let params = self.parse_lambda_params()?;
                self.expect(&TokenKind::Arrow)?;
                // Lambda body can be inline or an indented block
                self.skip_newlines();
                let body = if self.check(&TokenKind::Indent) {
                    let block_start = self.span();
                    self.advance();
                    let mut stmts = Vec::new();
                    while !self.check(&TokenKind::Dedent) && !self.check(&TokenKind::Eof) {
                        stmts.push(self.parse_stmt()?);
                        self.skip_newlines();
                    }
                    if self.check(&TokenKind::Dedent) {
                        self.advance();
                    }
                    Expr {
                        kind: ExprKind::Block(stmts),
                        span: block_start.merge(self.prev_span()),
                    }
                } else {
                    self.parse_expr()?
                };
                Ok(Expr {
                    kind: ExprKind::Lambda(params, Box::new(body)),
                    span: start.merge(self.prev_span()),
                })
            }
            TokenKind::Dot => {
                // Field accessor shorthand: .field
                self.advance();
                let name = self.expect_ident()?;
                Ok(Expr {
                    kind: ExprKind::FieldAccessor(name),
                    span: start.merge(self.prev_span()),
                })
            }
            TokenKind::Match => self.parse_match_expr(),
            _ => Err(ParseError::UnexpectedToken {
                found: format!("{:?}", self.current_kind()),
                expected: "expression".to_string(),
                span: start,
            }),
        }
    }

    fn parse_lambda_params(&mut self) -> Result<Vec<Param>> {
        let mut params = Vec::new();
        // Lambda params can be simple idents or patterns in parens
        while self.check_ident() || self.check(&TokenKind::LParen) {
            if self.check(&TokenKind::LParen) {
                // Destructuring pattern param
                let span = self.span();
                self.advance();
                let name = self.expect_ident()?;
                let mut rest = Vec::new();
                while !self.check(&TokenKind::RParen) && !self.check(&TokenKind::Eof) {
                    rest.push(self.expect_ident()?);
                }
                self.expect(&TokenKind::RParen)?;
                // For now, treat tuple destructuring as separate params
                params.push(Param {
                    name,
                    ty: None,
                    default: None,
                    is_flag: false,
                    span: span.merge(self.prev_span()),
                });
                for r in rest {
                    params.push(Param {
                        name: r,
                        ty: None,
                        default: None,
                        is_flag: false,
                        span: span.merge(self.prev_span()),
                    });
                }
            } else {
                let span = self.span();
                let name = self.expect_ident()?;
                params.push(Param {
                    name,
                    ty: None,
                    default: None,
                    is_flag: false,
                    span: span.merge(self.prev_span()),
                });
            }
        }
        Ok(params)
    }

    fn parse_match_expr(&mut self) -> Result<Expr> {
        let start = self.span();
        self.expect(&TokenKind::Match)?;
        let scrutinee = self.parse_expr()?;
        self.skip_newlines();
        self.expect(&TokenKind::Indent)?;
        let mut arms = Vec::new();
        while !self.check(&TokenKind::Dedent) && !self.check(&TokenKind::Eof) {
            let arm_start = self.span();
            let pattern = self.parse_pattern()?;
            let guard = if self.check(&TokenKind::Question) {
                self.advance();
                Some(self.parse_expr()?)
            } else {
                None
            };
            self.expect(&TokenKind::Arrow)?;
            // Match arm body can be inline or an indented block
            self.skip_newlines();
            let body = if self.check(&TokenKind::Indent) {
                let block_start = self.span();
                self.advance();
                let mut stmts = Vec::new();
                while !self.check(&TokenKind::Dedent) && !self.check(&TokenKind::Eof) {
                    stmts.push(self.parse_stmt()?);
                    self.skip_newlines();
                }
                if self.check(&TokenKind::Dedent) {
                    self.advance();
                }
                Expr {
                    kind: ExprKind::Block(stmts),
                    span: block_start.merge(self.prev_span()),
                }
            } else {
                self.parse_expr()?
            };
            arms.push(MatchArm {
                pattern,
                guard,
                body,
                span: arm_start.merge(self.prev_span()),
            });
            self.skip_newlines();
        }
        if self.check(&TokenKind::Dedent) {
            self.advance();
        }
        Ok(Expr {
            kind: ExprKind::Match(Box::new(scrutinee), arms),
            span: start.merge(self.prev_span()),
        })
    }

    fn parse_pattern(&mut self) -> Result<Pattern> {
        let start = self.span();
        let pat = self.parse_single_pattern()?;
        if self.check(&TokenKind::Pipe) {
            let mut pats = vec![pat];
            while self.check(&TokenKind::Pipe) {
                self.advance();
                pats.push(self.parse_single_pattern()?);
            }
            Ok(Pattern {
                kind: PatternKind::Or(pats),
                span: start.merge(self.prev_span()),
            })
        } else {
            Ok(pat)
        }
    }

    fn parse_single_pattern(&mut self) -> Result<Pattern> {
        let start = self.span();
        match self.current_kind() {
            TokenKind::Ident(ref s) if s == "_" => {
                self.advance();
                Ok(Pattern {
                    kind: PatternKind::Wildcard,
                    span: start,
                })
            }
            TokenKind::Ident(ref s) if s == "true" => {
                self.advance();
                Ok(Pattern {
                    kind: PatternKind::BoolLit(true),
                    span: start,
                })
            }
            TokenKind::Ident(ref s) if s == "false" => {
                self.advance();
                Ok(Pattern {
                    kind: PatternKind::BoolLit(false),
                    span: start,
                })
            }
            TokenKind::Ident(ref s) => {
                let s = s.clone();
                self.advance();
                Ok(Pattern {
                    kind: PatternKind::Ident(s),
                    span: start,
                })
            }
            TokenKind::Int(n) => {
                let n = n;
                self.advance();
                Ok(Pattern {
                    kind: PatternKind::IntLit(n),
                    span: start,
                })
            }
            TokenKind::Str(ref s) => {
                let s = s.clone();
                self.advance();
                Ok(Pattern {
                    kind: PatternKind::StrLit(s),
                    span: start,
                })
            }
            TokenKind::TypeIdent(ref s) => {
                let s = s.clone();
                self.advance();
                if self.check(&TokenKind::LParen) {
                    self.advance();
                    let mut pats = Vec::new();
                    if !self.check(&TokenKind::RParen) {
                        pats.push(self.parse_single_pattern()?);
                        while self.check(&TokenKind::Comma) {
                            self.advance();
                            pats.push(self.parse_single_pattern()?);
                        }
                    }
                    self.expect(&TokenKind::RParen)?;
                    Ok(Pattern {
                        kind: PatternKind::Constructor(s, pats),
                        span: start.merge(self.prev_span()),
                    })
                } else {
                    Ok(Pattern {
                        kind: PatternKind::Constructor(s, Vec::new()),
                        span: start,
                    })
                }
            }
            TokenKind::LBrace => {
                self.advance();
                let mut fields = Vec::new();
                while !self.check(&TokenKind::RBrace) && !self.check(&TokenKind::Eof) {
                    let name = self.expect_ident()?;
                    let pat = if self.check(&TokenKind::Colon) {
                        self.advance();
                        Some(self.parse_single_pattern()?)
                    } else {
                        None
                    };
                    fields.push((name, pat));
                    if self.check(&TokenKind::Comma) {
                        self.advance();
                    }
                }
                self.expect(&TokenKind::RBrace)?;
                Ok(Pattern {
                    kind: PatternKind::Record(fields),
                    span: start.merge(self.prev_span()),
                })
            }
            TokenKind::LParen => {
                self.advance();
                let mut pats = Vec::new();
                if !self.check(&TokenKind::RParen) {
                    pats.push(self.parse_single_pattern()?);
                    while self.check(&TokenKind::Comma) || self.check_ident() || self.check_type_ident() || matches!(self.current_kind(), TokenKind::Int(_)) {
                        if self.check(&TokenKind::Comma) {
                            self.advance();
                        }
                        if self.check(&TokenKind::RParen) {
                            break;
                        }
                        pats.push(self.parse_single_pattern()?);
                    }
                }
                self.expect(&TokenKind::RParen)?;
                Ok(Pattern {
                    kind: PatternKind::Tuple(pats),
                    span: start.merge(self.prev_span()),
                })
            }
            _ => Err(ParseError::UnexpectedToken {
                found: format!("{:?}", self.current_kind()),
                expected: "pattern".to_string(),
                span: start,
            }),
        }
    }

    // ── Type definitions ─────────────────────────────────────────────

    fn parse_type_def(&mut self, name: &str) -> Result<Def> {
        let start = self.span();

        // Type bodies no longer contain the "Name = " prefix — body starts
        // directly with the type content ({...}, | Variant, etc.)
        let type_params = if self.check(&TokenKind::LBracket) {
            self.advance();
            let mut params = Vec::new();
            params.push(self.expect_ident()?);
            while self.check(&TokenKind::Comma) {
                self.advance();
                params.push(self.expect_ident()?);
            }
            self.expect(&TokenKind::RBracket)?;
            params
        } else {
            Vec::new()
        };

        let body = if self.check(&TokenKind::Pipe) {
            // Enum
            let mut variants = Vec::new();
            while self.check(&TokenKind::Pipe) {
                self.advance();
                let vstart = self.span();
                let vname = self.expect_type_ident()?;
                let fields = if self.check(&TokenKind::LBrace) {
                    self.advance();
                    let fs = self.parse_field_list()?;
                    self.expect(&TokenKind::RBrace)?;
                    VariantFields::Named(fs)
                } else if self.check(&TokenKind::LParen) {
                    self.advance();
                    let mut types = Vec::new();
                    if !self.check(&TokenKind::RParen) {
                        types.push(self.parse_type_expr()?);
                        while self.check(&TokenKind::Comma) {
                            self.advance();
                            types.push(self.parse_type_expr()?);
                        }
                    }
                    self.expect(&TokenKind::RParen)?;
                    VariantFields::Positional(types)
                } else {
                    VariantFields::None
                };
                variants.push(Variant {
                    name: vname,
                    fields,
                    span: vstart.merge(self.prev_span()),
                });
                self.skip_newlines();
            }
            TypeBody::Enum(variants)
        } else if self.check(&TokenKind::LBrace) {
            // Record
            self.advance();
            let fields = self.parse_field_list()?;
            self.expect(&TokenKind::RBrace)?;
            TypeBody::Record(fields)
        } else {
            // Alias
            TypeBody::Alias(self.parse_type_expr()?)
        };

        Ok(Def {
            id: None,
            name: name.to_string(),
            kind: DefKind::Type(TypeDef { type_params, body }),
            span: start.merge(self.prev_span()),
        })
    }

    fn parse_field_list(&mut self) -> Result<Vec<Field>> {
        let mut fields = Vec::new();
        while self.check_ident() {
            let fstart = self.span();
            let name = self.expect_ident()?;
            self.expect(&TokenKind::Colon)?;
            let ty = self.parse_type_expr()?;
            let default = if self.check(&TokenKind::Eq) {
                self.advance();
                Some(self.parse_expr()?)
            } else {
                None
            };
            fields.push(Field {
                name,
                ty,
                default,
                span: fstart.merge(self.prev_span()),
            });
            self.skip_newlines();
            if self.check(&TokenKind::Comma) {
                self.advance();
            }
        }
        Ok(fields)
    }

    // ── Type expressions ─────────────────────────────────────────────

    pub fn parse_type_expr(&mut self) -> Result<TypeExpr> {
        let start = self.span();
        let ty = self.parse_type_atom()?;

        // Check for function type: T -> U
        if self.check(&TokenKind::Arrow) {
            self.advance();
            let ret = self.parse_type_expr()?; // right-associative
            return Ok(TypeExpr {
                span: start.merge(self.prev_span()),
                kind: TypeExprKind::Fn(Box::new(ty), Box::new(ret)),
            });
        }

        Ok(ty)
    }

    fn parse_type_atom(&mut self) -> Result<TypeExpr> {
        let start = self.span();
        match self.current_kind() {
            TokenKind::TypeIdent(ref s) => {
                let s = s.clone();
                self.advance();
                // Check for type application: Name[T, U]
                if self.check(&TokenKind::LBracket) {
                    self.advance();
                    let mut args = Vec::new();
                    args.push(self.parse_type_expr()?);
                    while self.check(&TokenKind::Comma) {
                        self.advance();
                        args.push(self.parse_type_expr()?);
                    }
                    self.expect(&TokenKind::RBracket)?;
                    Ok(TypeExpr {
                        kind: TypeExprKind::App(s, args),
                        span: start.merge(self.prev_span()),
                    })
                } else {
                    Ok(TypeExpr {
                        kind: TypeExprKind::Named(s),
                        span: start,
                    })
                }
            }
            TokenKind::Ident(ref s) => {
                // Type variables (lowercase in type position)
                let s = s.clone();
                self.advance();
                Ok(TypeExpr {
                    kind: TypeExprKind::Named(s),
                    span: start,
                })
            }
            TokenKind::Question => {
                self.advance();
                let inner = self.parse_type_atom()?;
                Ok(TypeExpr {
                    kind: TypeExprKind::Opt(Box::new(inner)),
                    span: start.merge(self.prev_span()),
                })
            }
            TokenKind::Bang => {
                self.advance();
                let inner = self.parse_type_atom()?;
                Ok(TypeExpr {
                    kind: TypeExprKind::Res(Box::new(inner)),
                    span: start.merge(self.prev_span()),
                })
            }
            TokenKind::Ampersand => {
                self.advance();
                let inner = self.parse_type_atom()?;
                Ok(TypeExpr {
                    kind: TypeExprKind::Ref(Box::new(inner)),
                    span: start.merge(self.prev_span()),
                })
            }
            TokenKind::Star => {
                self.advance();
                let inner = self.parse_type_atom()?;
                Ok(TypeExpr {
                    kind: TypeExprKind::Ptr(Box::new(inner)),
                    span: start.merge(self.prev_span()),
                })
            }
            TokenKind::LBracket => {
                self.advance();
                let inner = self.parse_type_expr()?;
                self.expect(&TokenKind::RBracket)?;
                Ok(TypeExpr {
                    kind: TypeExprKind::Arr(Box::new(inner)),
                    span: start.merge(self.prev_span()),
                })
            }
            TokenKind::LBrace => {
                self.advance();
                // Record type or Map type
                // Map: {K:V}, Record: {name:T ...}
                // We need to disambiguate. If first token is a type (uppercase), it's a map.
                // If first token is an ident (lowercase), it's a record.
                if self.check_type_ident() {
                    // Map type
                    let key = self.parse_type_expr()?;
                    self.expect(&TokenKind::Colon)?;
                    let val = self.parse_type_expr()?;
                    self.expect(&TokenKind::RBrace)?;
                    Ok(TypeExpr {
                        kind: TypeExprKind::Map(Box::new(key), Box::new(val)),
                        span: start.merge(self.prev_span()),
                    })
                } else {
                    // Record type
                    let mut fields = Vec::new();
                    let mut has_rest = false;
                    while !self.check(&TokenKind::RBrace) && !self.check(&TokenKind::Eof) {
                        if self.check(&TokenKind::DotDot) {
                            self.advance();
                            has_rest = true;
                            break;
                        }
                        let name = self.expect_ident()?;
                        self.expect(&TokenKind::Colon)?;
                        let ty = self.parse_type_expr()?;
                        fields.push((name, ty));
                        if self.check(&TokenKind::Comma) {
                            self.advance();
                        }
                    }
                    self.expect(&TokenKind::RBrace)?;
                    Ok(TypeExpr {
                        kind: TypeExprKind::Record(fields, has_rest),
                        span: start.merge(self.prev_span()),
                    })
                }
            }
            TokenKind::LParen => {
                self.advance();
                let first = self.parse_type_expr()?;
                if self.check(&TokenKind::Comma) {
                    let mut types = vec![first];
                    while self.check(&TokenKind::Comma) {
                        self.advance();
                        if self.check(&TokenKind::RParen) {
                            break;
                        }
                        types.push(self.parse_type_expr()?);
                    }
                    self.expect(&TokenKind::RParen)?;
                    Ok(TypeExpr {
                        kind: TypeExprKind::Tuple(types),
                        span: start.merge(self.prev_span()),
                    })
                } else {
                    self.expect(&TokenKind::RParen)?;
                    Ok(first) // grouped type
                }
            }
            _ => Err(ParseError::UnexpectedToken {
                found: format!("{:?}", self.current_kind()),
                expected: "type".to_string(),
                span: start,
            }),
        }
    }

    // ── Test definitions ─────────────────────────────────────────────

    fn parse_test_def(&mut self) -> Result<Def> {
        let start = self.span();
        self.expect(&TokenKind::Test)?;
        let desc = self.expect_string()?;
        self.expect(&TokenKind::Eq)?;
        let body = self.parse_body()?;
        Ok(Def {
            id: None,
            name: desc.clone(),
            kind: DefKind::Test(TestDef {
                description: desc,
                body,
            }),
            span: start.merge(self.prev_span()),
        })
    }

    // ── Token helpers ────────────────────────────────────────────────

    pub fn current(&self) -> &Token {
        self.tokens.get(self.pos).unwrap_or_else(|| {
            self.tokens
                .last()
                .expect("token stream should have at least EOF")
        })
    }

    fn current_kind(&self) -> TokenKind {
        self.current().kind.clone()
    }

    fn span(&self) -> Span {
        self.current().span
    }

    fn prev_span(&self) -> Span {
        if self.pos > 0 {
            self.tokens[self.pos - 1].span
        } else {
            Span::new(0, 0)
        }
    }

    fn peek_at(&self, offset: usize) -> Option<&Token> {
        self.tokens.get(self.pos + offset)
    }

    fn advance(&mut self) -> &Token {
        let tok = &self.tokens[self.pos];
        if self.pos < self.tokens.len() - 1 {
            self.pos += 1;
        }
        tok
    }

    fn check(&self, kind: &TokenKind) -> bool {
        std::mem::discriminant(&self.current().kind) == std::mem::discriminant(kind)
    }

    fn check_ident(&self) -> bool {
        matches!(self.current().kind, TokenKind::Ident(_))
    }

    fn check_type_ident(&self) -> bool {
        matches!(self.current().kind, TokenKind::TypeIdent(_))
    }

    fn expect(&mut self, kind: &TokenKind) -> Result<()> {
        if self.check(kind) {
            self.advance();
            Ok(())
        } else {
            Err(ParseError::UnexpectedToken {
                found: format!("{:?}", self.current().kind),
                expected: format!("{kind:?}"),
                span: self.span(),
            })
        }
    }

    fn expect_ident(&mut self) -> Result<String> {
        if let TokenKind::Ident(s) = &self.current().kind {
            let s = s.clone();
            self.advance();
            Ok(s)
        } else {
            Err(ParseError::UnexpectedToken {
                found: format!("{:?}", self.current().kind),
                expected: "identifier".to_string(),
                span: self.span(),
            })
        }
    }

    fn expect_type_ident(&mut self) -> Result<String> {
        if let TokenKind::TypeIdent(s) = &self.current().kind {
            let s = s.clone();
            self.advance();
            Ok(s)
        } else {
            Err(ParseError::UnexpectedToken {
                found: format!("{:?}", self.current().kind),
                expected: "type identifier".to_string(),
                span: self.span(),
            })
        }
    }

    fn expect_ident_or_type_ident(&mut self) -> Result<String> {
        match &self.current().kind {
            TokenKind::Ident(s) | TokenKind::TypeIdent(s) => {
                let s = s.clone();
                self.advance();
                Ok(s)
            }
            _ => Err(ParseError::UnexpectedToken {
                found: format!("{:?}", self.current().kind),
                expected: "identifier".to_string(),
                span: self.span(),
            }),
        }
    }

    fn expect_string(&mut self) -> Result<String> {
        if let TokenKind::Str(s) = &self.current().kind {
            let s = s.clone();
            self.advance();
            Ok(s)
        } else {
            Err(ParseError::UnexpectedToken {
                found: format!("{:?}", self.current().kind),
                expected: "string literal".to_string(),
                span: self.span(),
            })
        }
    }

    fn skip_newlines(&mut self) {
        while self.check(&TokenKind::Newline) {
            self.advance();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse_expr(input: &str) -> Expr {
        Parser::parse_expr_str(input).unwrap()
    }

    fn parse_fn(input: &str) -> Def {
        let tokens = Lexer::new(input).tokenize();
        let mut parser = Parser::new(tokens);
        let name = if let TokenKind::Ident(ref s) = parser.current().kind {
            s.clone()
        } else {
            panic!("expected ident");
        };
        parser.parse_def(&name, "fn").unwrap()
    }

    #[test]
    fn test_parse_int() {
        let expr = parse_expr("42");
        assert!(matches!(expr.kind, ExprKind::IntLit(42)));
    }

    #[test]
    fn test_parse_binary() {
        let expr = parse_expr("1 + 2");
        assert!(matches!(expr.kind, ExprKind::Binary(BinOp::Add, _, _)));
    }

    #[test]
    fn test_parse_precedence() {
        let expr = parse_expr("1 + 2 * 3");
        // Should be 1 + (2 * 3)
        if let ExprKind::Binary(BinOp::Add, left, right) = &expr.kind {
            assert!(matches!(left.kind, ExprKind::IntLit(1)));
            assert!(matches!(right.kind, ExprKind::Binary(BinOp::Mul, _, _)));
        } else {
            panic!("expected Add");
        }
    }

    #[test]
    fn test_parse_pipe() {
        let expr = parse_expr("x |> f |> g");
        if let ExprKind::Pipe(left, right) = &expr.kind {
            assert!(matches!(right.kind, ExprKind::Ident(ref s) if s == "g"));
            assert!(matches!(left.kind, ExprKind::Pipe(_, _)));
        } else {
            panic!("expected Pipe");
        }
    }

    #[test]
    fn test_parse_call() {
        let expr = parse_expr("f(x, y)");
        if let ExprKind::Call(func, args) = &expr.kind {
            assert!(matches!(func.kind, ExprKind::Ident(ref s) if s == "f"));
            assert_eq!(args.len(), 2);
        } else {
            panic!("expected Call");
        }
    }

    #[test]
    fn test_parse_field_access() {
        let expr = parse_expr("x.name");
        assert!(matches!(expr.kind, ExprKind::FieldAccess(_, ref s) if s == "name"));
    }

    #[test]
    fn test_parse_lambda() {
        let expr = parse_expr("\\x -> x + 1");
        if let ExprKind::Lambda(params, body) = &expr.kind {
            assert_eq!(params.len(), 1);
            assert_eq!(params[0].name, "x");
            assert!(matches!(body.kind, ExprKind::Binary(BinOp::Add, _, _)));
        } else {
            panic!("expected Lambda");
        }
    }

    #[test]
    fn test_parse_lambda_block() {
        let def = parse_fn("f x = \\y ->\n  z = y + 1\n  z");
        if let DefKind::Fn(fndef) = &def.kind {
            if let Body::Expr(ref expr) = fndef.body {
                if let ExprKind::Lambda(params, body) = &expr.kind {
                    assert_eq!(params.len(), 1);
                    assert_eq!(params[0].name, "y");
                    assert!(
                        matches!(body.kind, ExprKind::Block(ref stmts) if stmts.len() == 2),
                        "expected Block with 2 stmts, got {:?}",
                        body.kind
                    );
                } else {
                    panic!("expected Lambda, got {:?}", expr.kind);
                }
            } else {
                panic!("expected Expr body");
            }
        } else {
            panic!("expected Fn def");
        }
    }

    #[test]
    fn test_parse_lambda_block_multiline() {
        // Lambda with block body and captures
        let def = parse_fn("make_adder n =\n  \\x ->\n    y = n + x\n    y");
        if let DefKind::Fn(fndef) = &def.kind {
            if let Body::Block(ref stmts) = fndef.body {
                assert_eq!(stmts.len(), 1);
                if let StmtKind::Expr(ref expr) = stmts[0].kind {
                    if let ExprKind::Lambda(params, body) = &expr.kind {
                        assert_eq!(params.len(), 1);
                        assert_eq!(params[0].name, "x");
                        assert!(matches!(body.kind, ExprKind::Block(ref s) if s.len() == 2));
                    } else {
                        panic!("expected Lambda");
                    }
                } else {
                    panic!("expected Expr stmt");
                }
            } else {
                panic!("expected Block body");
            }
        } else {
            panic!("expected Fn def");
        }
    }

    #[test]
    fn test_parse_lambda_inline_still_works() {
        // Ensure single-line lambdas still parse correctly
        let def = parse_fn("f = \\x -> x + 1");
        if let DefKind::Fn(fndef) = &def.kind {
            if let Body::Expr(ref expr) = fndef.body {
                if let ExprKind::Lambda(_, body) = &expr.kind {
                    assert!(
                        matches!(body.kind, ExprKind::Binary(BinOp::Add, _, _)),
                        "expected Binary Add, got {:?}",
                        body.kind
                    );
                } else {
                    panic!("expected Lambda");
                }
            } else {
                panic!("expected Expr body");
            }
        } else {
            panic!("expected Fn def");
        }
    }

    #[test]
    fn test_parse_match() {
        let expr = parse_expr("match x\n  true -> 1\n  false -> 0");
        if let ExprKind::Match(_, arms) = &expr.kind {
            assert_eq!(arms.len(), 2);
        } else {
            panic!("expected Match");
        }
    }

    #[test]
    fn test_parse_propagate() {
        let expr = parse_expr("f(x)?");
        assert!(matches!(expr.kind, ExprKind::Propagate(_)));
    }

    #[test]
    fn test_parse_array() {
        let expr = parse_expr("[1, 2, 3]");
        if let ExprKind::Array(elems) = &expr.kind {
            assert_eq!(elems.len(), 3);
        } else {
            panic!("expected Array");
        }
    }

    #[test]
    fn test_parse_record_literal() {
        let expr = parse_expr("{name: x, age: 42}");
        if let ExprKind::Record(fields) = &expr.kind {
            assert_eq!(fields.len(), 2);
            assert_eq!(fields[0].name, "name");
        } else {
            panic!("expected Record");
        }
    }

    #[test]
    fn test_parse_field_accessor() {
        let expr = parse_expr(".name");
        assert!(matches!(expr.kind, ExprKind::FieldAccessor(ref s) if s == "name"));
    }

    #[test]
    fn test_parse_fn_def() {
        let def = parse_fn("f x = x + 1");
        if let DefKind::Fn(fndef) = &def.kind {
            assert_eq!(fndef.params.len(), 1);
            assert!(matches!(fndef.body, Body::Expr(_)));
        } else {
            panic!("expected Fn");
        }
    }

    #[test]
    fn test_parse_fn_with_type() {
        let def = parse_fn("f x:I : I = x + 1");
        if let DefKind::Fn(fndef) = &def.kind {
            assert_eq!(fndef.params.len(), 1);
            assert!(fndef.params[0].ty.is_some());
            assert!(fndef.ret_ty.is_some());
        } else {
            panic!("expected Fn");
        }
    }

    #[test]
    fn test_parse_type_def() {
        let tokens = Lexer::new("{path:S line:I text:S}").tokenize();
        let mut parser = Parser::new(tokens);
        let def = parser.parse_def("Match", "type").unwrap();
        if let DefKind::Type(typedef) = &def.kind {
            if let TypeBody::Record(fields) = &typedef.body {
                assert_eq!(fields.len(), 3);
            } else {
                panic!("expected Record");
            }
        } else {
            panic!("expected Type");
        }
    }

    #[test]
    fn test_parse_compose() {
        let expr = parse_expr("f >> g >> h");
        if let ExprKind::Compose(_left, right) = &expr.kind {
            assert!(matches!(right.kind, ExprKind::Ident(ref s) if s == "h"));
        } else {
            panic!("expected Compose");
        }
    }

    #[test]
    fn test_parse_unary_neg() {
        let expr = parse_expr("-x");
        assert!(matches!(expr.kind, ExprKind::Unary(UnaryOp::Neg, _)));
    }
}
