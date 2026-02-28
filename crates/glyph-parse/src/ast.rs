use crate::span::Span;

pub type DefId = i64;

#[derive(Debug, Clone)]
pub struct Def {
    pub id: Option<DefId>,
    pub name: String,
    pub kind: DefKind,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub enum DefKind {
    Fn(FnDef),
    Type(TypeDef),
    Trait(TraitDef),
    Impl(ImplDef),
    Const(ConstDef),
    Fsm(FsmDef),
    Srv(SrvDef),
    Test(TestDef),
}

#[derive(Debug, Clone)]
pub struct FnDef {
    pub params: Vec<Param>,
    pub ret_ty: Option<TypeExpr>,
    pub body: Body,
}

#[derive(Debug, Clone)]
pub struct Param {
    pub name: String,
    pub ty: Option<TypeExpr>,
    pub default: Option<Expr>,
    pub is_flag: bool,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub enum Body {
    Expr(Expr),
    Block(Vec<Stmt>),
}

#[derive(Debug, Clone)]
pub struct Stmt {
    pub kind: StmtKind,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub enum StmtKind {
    Expr(Expr),
    Let(String, Expr),
    Assign(Expr, Expr),
}

#[derive(Debug, Clone)]
pub struct Expr {
    pub kind: ExprKind,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub enum ExprKind {
    IntLit(i64),
    FloatLit(f64),
    StrLit(String),
    StrInterp(Vec<StringPart>),
    ByteStrLit(Vec<u8>),
    BoolLit(bool),

    Ident(String),
    TypeIdent(String),

    Binary(BinOp, Box<Expr>, Box<Expr>),
    Unary(UnaryOp, Box<Expr>),
    Call(Box<Expr>, Vec<Expr>),
    FieldAccess(Box<Expr>, String),
    Index(Box<Expr>, Box<Expr>),
    FieldAccessor(String),

    Pipe(Box<Expr>, Box<Expr>),
    Compose(Box<Expr>, Box<Expr>),
    Propagate(Box<Expr>),
    Unwrap(Box<Expr>),

    Lambda(Vec<Param>, Box<Expr>),
    Match(Box<Expr>, Vec<MatchArm>),
    Block(Vec<Stmt>),

    Array(Vec<Expr>),
    ArrayRange(Box<Expr>, Option<Box<Expr>>),
    Record(Vec<FieldInit>),
    Tuple(Vec<Expr>),
}

#[derive(Debug, Clone)]
pub enum StringPart {
    Lit(String),
    Expr(Expr),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BinOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Eq,
    Neq,
    Lt,
    Gt,
    LtEq,
    GtEq,
    And,
    Or,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UnaryOp {
    Neg,
    Not,
    Ref,
    Deref,
}

#[derive(Debug, Clone)]
pub struct MatchArm {
    pub pattern: Pattern,
    pub guard: Option<Expr>,
    pub body: Expr,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub struct Pattern {
    pub kind: PatternKind,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub enum PatternKind {
    Wildcard,
    Ident(String),
    IntLit(i64),
    BoolLit(bool),
    StrLit(String),
    Constructor(String, Vec<Pattern>),
    Record(Vec<(String, Option<Pattern>)>),
    Tuple(Vec<Pattern>),
    Or(Vec<Pattern>),
}

#[derive(Debug, Clone)]
pub struct FieldInit {
    pub name: String,
    pub value: Expr,
    pub span: Span,
}

// Type expressions
#[derive(Debug, Clone)]
pub struct TypeExpr {
    pub kind: TypeExprKind,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub enum TypeExprKind {
    Named(String),
    App(String, Vec<TypeExpr>),
    Fn(Box<TypeExpr>, Box<TypeExpr>),
    Tuple(Vec<TypeExpr>),
    Record(Vec<(String, TypeExpr)>, bool), // fields, has_rest (..)
    Ref(Box<TypeExpr>),
    Ptr(Box<TypeExpr>),
    Opt(Box<TypeExpr>),
    Res(Box<TypeExpr>),
    Arr(Box<TypeExpr>),
    Map(Box<TypeExpr>, Box<TypeExpr>),
}

// Type definitions
#[derive(Debug, Clone)]
pub struct TypeDef {
    pub type_params: Vec<String>,
    pub body: TypeBody,
}

#[derive(Debug, Clone)]
pub enum TypeBody {
    Record(Vec<Field>),
    Enum(Vec<Variant>),
    Alias(TypeExpr),
}

#[derive(Debug, Clone)]
pub struct Field {
    pub name: String,
    pub ty: TypeExpr,
    pub default: Option<Expr>,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub struct Variant {
    pub name: String,
    pub fields: VariantFields,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub enum VariantFields {
    None,
    Positional(Vec<TypeExpr>),
    Named(Vec<Field>),
}

// Trait definitions
#[derive(Debug, Clone)]
pub struct TraitDef {
    pub type_params: Vec<String>,
    pub methods: Vec<(String, TypeExpr)>,
}

// Impl definitions
#[derive(Debug, Clone)]
pub struct ImplDef {
    pub trait_name: String,
    pub type_name: String,
    pub methods: Vec<(String, FnDef)>,
}

// Const definition
#[derive(Debug, Clone)]
pub struct ConstDef {
    pub ty: Option<TypeExpr>,
    pub value: Expr,
}

// FSM definition
#[derive(Debug, Clone)]
pub struct FsmDef {
    pub states: Vec<StateDef>,
}

#[derive(Debug, Clone)]
pub struct StateDef {
    pub name: String,
    pub transitions: Vec<Transition>,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub struct Transition {
    pub event: String,
    pub params: Vec<Param>,
    pub target: String,
    pub action: Option<Expr>,
    pub span: Span,
}

// Srv definition
#[derive(Debug, Clone)]
pub struct SrvDef {
    pub addr: Expr,
    pub routes: Vec<Route>,
}

#[derive(Debug, Clone)]
pub struct Route {
    pub path: String,
    pub handlers: Vec<Handler>,
    pub span: Span,
}

#[derive(Debug, Clone)]
pub struct Handler {
    pub method: HttpMethod,
    pub params: Vec<Param>,
    pub body: Body,
    pub span: Span,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HttpMethod {
    Get,
    Post,
    Put,
    Delete,
    Head,
}

// Test definition
#[derive(Debug, Clone)]
pub struct TestDef {
    pub description: String,
    pub body: Body,
}
