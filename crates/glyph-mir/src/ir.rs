use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::fmt;

/// Unique identifier for a local variable in a MIR function.
pub type LocalId = u32;
/// Unique identifier for a basic block.
pub type BlockId = u32;

/// A MIR function — the unit of compilation.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MirFunction {
    pub name: String,
    pub params: Vec<LocalId>,
    pub return_ty: MirType,
    pub locals: Vec<MirLocal>,
    pub blocks: Vec<BasicBlock>,
    pub entry: BlockId,
}

/// A local variable declaration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MirLocal {
    pub id: LocalId,
    pub ty: MirType,
    pub name: Option<String>,
}

/// A basic block in the CFG.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BasicBlock {
    pub id: BlockId,
    pub stmts: Vec<Statement>,
    pub terminator: Terminator,
}

/// An assignment statement.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Statement {
    pub dest: LocalId,
    pub rvalue: Rvalue,
}

/// Right-hand side of an assignment.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Rvalue {
    /// Use the value of a local.
    Use(Operand),
    /// Binary operation.
    BinOp(BinOp, Operand, Operand),
    /// Unary operation.
    UnOp(UnOp, Operand),
    /// Function call: callee(args...) -> result.
    Call(Operand, Vec<Operand>),
    /// Construct an aggregate (record, tuple, array, enum variant).
    Aggregate(AggregateKind, Vec<Operand>),
    /// Read a field from an aggregate. (operand, field_index, field_name)
    Field(Operand, u32, String),
    /// Read from an array index.
    Index(Operand, Operand),
    /// Cast one type to another.
    Cast(Operand, MirType),
    /// String interpolation.
    StrInterp(Vec<Operand>),
    /// Create a closure: (lifted_fn_name, captured_values) -> {fn_ptr, env_ptr}
    MakeClosure(String, Vec<Operand>),
    /// Get a reference to a local.
    Ref(LocalId),
    /// Dereference a pointer/ref.
    Deref(Operand),
}

/// An operand — either a constant or a local variable.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Operand {
    Local(LocalId),
    ConstInt(i64),
    ConstFloat(f64),
    ConstBool(bool),
    ConstStr(String),
    ConstUnit,
    /// Reference to a named function.
    FuncRef(String),
    /// Reference to an extern function.
    ExternRef(String),
}

/// Binary operations in MIR (post type-checking, concrete).
#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
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

/// Unary operations.
#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub enum UnOp {
    Neg,
    Not,
}

/// What kind of aggregate to construct.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum AggregateKind {
    Tuple,
    Array,
    Record(Vec<String>),
    /// Enum variant: (type_name, variant_name, discriminant)
    Variant(String, String, u32),
}

/// How a basic block ends.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Terminator {
    /// Unconditional jump.
    Goto(BlockId),
    /// Conditional branch.
    Branch(Operand, BlockId, BlockId),
    /// Multi-way switch on an integer discriminant.
    Switch(Operand, Vec<(i64, BlockId)>, BlockId),
    /// Return the value of a local.
    Return(Operand),
    /// Unreachable (e.g., after a panic or diverge).
    Unreachable,
}

/// Concrete types in MIR (all polymorphism resolved).
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum MirType {
    Int,
    Int32,
    UInt,
    Float,
    Float32,
    Str,
    Bool,
    Void,
    Never,
    Ptr(Box<MirType>),
    Ref(Box<MirType>),
    Array(Box<MirType>),
    Tuple(Vec<MirType>),
    Record(BTreeMap<String, MirType>),
    Enum(String, Vec<(String, Vec<MirType>)>),
    Fn(Box<MirType>, Box<MirType>),
    /// A named type that hasn't been expanded yet.
    Named(String),
    /// Closure environment struct.
    ClosureEnv(Vec<MirType>),
}

impl MirType {
    /// Size in bytes (for layout). Returns None for dynamically-sized types.
    pub fn size(&self) -> Option<usize> {
        match self {
            MirType::Bool => Some(1),
            MirType::Int32 | MirType::Float32 => Some(4),
            MirType::Int | MirType::UInt | MirType::Float => Some(8),
            MirType::Void => Some(0),
            MirType::Ptr(_) | MirType::Ref(_) | MirType::Fn(_, _) => Some(8), // pointer-sized
            MirType::Str => Some(16),  // ptr + len
            MirType::Array(_) => Some(24), // ptr + len + cap
            MirType::Tuple(ts) => {
                let mut size = 0;
                for t in ts {
                    size += t.size()?;
                }
                Some(size)
            }
            MirType::Record(fields) => {
                let mut size = 0;
                for t in fields.values() {
                    size += t.size()?;
                }
                Some(size)
            }
            _ => None,
        }
    }
}

impl fmt::Display for MirType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MirType::Int => write!(f, "i64"),
            MirType::Int32 => write!(f, "i32"),
            MirType::UInt => write!(f, "u64"),
            MirType::Float => write!(f, "f64"),
            MirType::Float32 => write!(f, "f32"),
            MirType::Str => write!(f, "str"),
            MirType::Bool => write!(f, "bool"),
            MirType::Void => write!(f, "void"),
            MirType::Never => write!(f, "never"),
            MirType::Ptr(t) => write!(f, "*{t}"),
            MirType::Ref(t) => write!(f, "&{t}"),
            MirType::Array(t) => write!(f, "[{t}]"),
            MirType::Fn(a, b) => write!(f, "{a} -> {b}"),
            MirType::Tuple(ts) => {
                write!(f, "(")?;
                for (i, t) in ts.iter().enumerate() {
                    if i > 0 { write!(f, ", ")?; }
                    write!(f, "{t}")?;
                }
                write!(f, ")")
            }
            MirType::Record(fields) => {
                write!(f, "{{")?;
                for (i, (k, v)) in fields.iter().enumerate() {
                    if i > 0 { write!(f, " ")?; }
                    write!(f, "{k}:{v}")?;
                }
                write!(f, "}}")
            }
            MirType::Enum(name, _) => write!(f, "{name}"),
            MirType::Named(name) => write!(f, "{name}"),
            MirType::ClosureEnv(ts) => write!(f, "env({})", ts.len()),
        }
    }
}

impl MirFunction {
    /// Pretty-print the function.
    pub fn display(&self) -> String {
        let mut out = format!("fn {}(", self.name);
        for (i, p) in self.params.iter().enumerate() {
            if i > 0 { out.push_str(", "); }
            out.push_str(&format!("_{p}: {}", self.locals[*p as usize].ty));
        }
        out.push_str(&format!(") -> {} {{\n", self.return_ty));
        for block in &self.blocks {
            out.push_str(&format!("  bb{}:\n", block.id));
            for stmt in &block.stmts {
                out.push_str(&format!("    _{} = {:?}\n", stmt.dest, stmt.rvalue));
            }
            out.push_str(&format!("    {:?}\n", block.terminator));
        }
        out.push_str("}\n");
        out
    }
}
