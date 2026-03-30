/// A row from the `def` table.
#[derive(Debug, Clone)]
pub struct DefRow {
    pub id: i64,
    pub name: String,
    pub kind: DefKind,
    pub sig: Option<String>,
    pub body: String,
    pub hash: Vec<u8>,
    pub tokens: i64,
    pub compiled: bool,
    pub generation: i64,
}

/// The kind of a definition, matching the CHECK constraint.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum DefKind {
    Fn,
    Type,
    Test,
    Const,
    Data,
    Prop,
}

impl DefKind {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Fn => "fn",
            Self::Type => "type",
            Self::Test => "test",
            Self::Const => "const",
            Self::Data => "data",
            Self::Prop => "prop",
        }
    }

    pub fn from_str(s: &str) -> Option<Self> {
        match s {
            "fn" => Some(Self::Fn),
            "type" => Some(Self::Type),
            "test" => Some(Self::Test),
            "const" => Some(Self::Const),
            "data" => Some(Self::Data),
            "prop" => Some(Self::Prop),
            _ => None,
        }
    }
}

impl std::fmt::Display for DefKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.as_str())
    }
}

/// A row from the `dep` table.
#[derive(Debug, Clone)]
pub struct DepRow {
    pub from_id: i64,
    pub to_id: i64,
    pub edge: DepEdge,
}

/// Dependency edge types.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DepEdge {
    Calls,
    UsesType,
    Implements,
    FieldOf,
    VariantOf,
}

impl DepEdge {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Calls => "calls",
            Self::UsesType => "uses_type",
            Self::Implements => "implements",
            Self::FieldOf => "field_of",
            Self::VariantOf => "variant_of",
        }
    }

    pub fn from_str(s: &str) -> Option<Self> {
        match s {
            "calls" => Some(Self::Calls),
            "uses_type" => Some(Self::UsesType),
            "implements" => Some(Self::Implements),
            "field_of" => Some(Self::FieldOf),
            "variant_of" => Some(Self::VariantOf),
            _ => None,
        }
    }
}

/// A row from the `extern` table.
#[derive(Debug, Clone)]
pub struct ExternRow {
    pub id: i64,
    pub name: String,
    pub symbol: String,
    pub lib: Option<String>,
    pub sig: String,
    pub conv: CallingConv,
}

/// Calling conventions for extern functions.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CallingConv {
    C,
    System,
    Rust,
}

impl CallingConv {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::C => "C",
            Self::System => "system",
            Self::Rust => "rust",
        }
    }

    pub fn from_str(s: &str) -> Option<Self> {
        match s {
            "C" => Some(Self::C),
            "system" => Some(Self::System),
            "rust" => Some(Self::Rust),
            _ => None,
        }
    }
}

/// A row from the `module` table.
#[derive(Debug, Clone)]
pub struct ModuleRow {
    pub id: i64,
    pub name: String,
    pub doc: Option<String>,
}

/// A row from the `compiled` table.
#[derive(Debug, Clone)]
pub struct CompiledRow {
    pub def_id: i64,
    pub ir: Vec<u8>,
    pub target: String,
    pub hash: Vec<u8>,
}

/// Parameters for inserting a new definition.
#[derive(Debug, Clone)]
pub struct NewDef {
    pub name: String,
    pub kind: DefKind,
    pub sig: Option<String>,
    pub body: String,
    pub generation: i64,
}
