use crate::span::Span;

#[derive(Debug, Clone, PartialEq)]
pub struct Token {
    pub kind: TokenKind,
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TokenKind {
    // Literals
    Int(i64),
    Float(f64),
    Str(String),
    ByteStr(Vec<u8>),
    StrInterpStart,
    StrInterpEnd,

    // Identifiers
    Ident(String),
    TypeIdent(String),

    // Keywords
    If,
    Else,
    Match,
    For,
    In,
    Trait,
    Impl,
    Const,
    Extern,
    Fsm,
    Srv,
    Test,
    As,

    // Operators
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Eq,
    ColonEq,
    EqEq,
    BangEq,
    Lt,
    Gt,
    LtEq,
    GtEq,
    And,
    Or,
    Bang,
    Ampersand,
    Pipe,
    PipeGt,
    GtGt,
    Question,
    Arrow,
    Backslash,
    DotDot,
    At,

    // Delimiters
    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,

    // Punctuation
    Colon,
    Comma,
    Dot,

    // Layout
    Indent,
    Dedent,
    Newline,

    // Special
    Eof,
    Error(String),
}
