use std::collections::BTreeMap;
use std::fmt;

/// Type variable identifier.
pub type TypeVarId = u32;

/// Core type representation for Glyph's type system.
#[derive(Debug, Clone, PartialEq)]
pub enum Type {
    // Primitives
    Int,
    Int32,
    UInt,
    Float,
    Float32,
    Str,
    Bool,
    Void,
    Never,

    // Compound types
    Fn(Box<Type>, Box<Type>),
    Tuple(Vec<Type>),
    Array(Box<Type>),
    Map(Box<Type>, Box<Type>),
    Opt(Box<Type>),
    Res(Box<Type>),
    Ref(Box<Type>),
    Ptr(Box<Type>),

    // Named types (user-defined)
    Named(String, Vec<Type>),

    // Records with row polymorphism
    Record(RowType),

    // Type variable (for inference)
    Var(TypeVarId),

    // Universally quantified type
    ForAll(Vec<TypeVarId>, Box<Type>),

    // Error sentinel
    Error,
}

/// Row type for extensible records.
/// `fields` are the known fields, `rest` is an optional row variable for extension.
#[derive(Debug, Clone, PartialEq)]
pub struct RowType {
    pub fields: BTreeMap<String, Type>,
    pub rest: Option<TypeVarId>,
}

impl RowType {
    pub fn closed(fields: BTreeMap<String, Type>) -> Self {
        Self { fields, rest: None }
    }

    pub fn open(fields: BTreeMap<String, Type>, rest: TypeVarId) -> Self {
        Self {
            fields,
            rest: Some(rest),
        }
    }
}

impl Type {
    /// Shorthand for multi-param function: a -> b -> c
    pub fn func(params: Vec<Type>, ret: Type) -> Type {
        let mut ty = ret;
        for p in params.into_iter().rev() {
            ty = Type::Fn(Box::new(p), Box::new(ty));
        }
        ty
    }

    /// Check if this type contains a given type variable.
    pub fn contains_var(&self, var: TypeVarId) -> bool {
        match self {
            Type::Var(v) => *v == var,
            Type::Fn(a, b) => a.contains_var(var) || b.contains_var(var),
            Type::Tuple(ts) => ts.iter().any(|t| t.contains_var(var)),
            Type::Array(t) | Type::Opt(t) | Type::Res(t) | Type::Ref(t) | Type::Ptr(t) => {
                t.contains_var(var)
            }
            Type::Map(k, v) => k.contains_var(var) || v.contains_var(var),
            Type::Named(_, args) => args.iter().any(|t| t.contains_var(var)),
            Type::Record(row) => {
                row.fields.values().any(|t| t.contains_var(var))
                    || row.rest.map_or(false, |r| r == var)
            }
            Type::ForAll(_, inner) => inner.contains_var(var),
            _ => false,
        }
    }

    /// Collect all free type variables.
    pub fn free_vars(&self) -> Vec<TypeVarId> {
        let mut vars = Vec::new();
        self.collect_free_vars(&mut vars);
        vars.sort_unstable();
        vars.dedup();
        vars
    }

    fn collect_free_vars(&self, vars: &mut Vec<TypeVarId>) {
        match self {
            Type::Var(v) => vars.push(*v),
            Type::Fn(a, b) => {
                a.collect_free_vars(vars);
                b.collect_free_vars(vars);
            }
            Type::Tuple(ts) => {
                for t in ts {
                    t.collect_free_vars(vars);
                }
            }
            Type::Array(t) | Type::Opt(t) | Type::Res(t) | Type::Ref(t) | Type::Ptr(t) => {
                t.collect_free_vars(vars);
            }
            Type::Map(k, v) => {
                k.collect_free_vars(vars);
                v.collect_free_vars(vars);
            }
            Type::Named(_, args) => {
                for a in args {
                    a.collect_free_vars(vars);
                }
            }
            Type::Record(row) => {
                for t in row.fields.values() {
                    t.collect_free_vars(vars);
                }
                if let Some(r) = row.rest {
                    vars.push(r);
                }
            }
            Type::ForAll(bound, inner) => {
                let mut inner_vars = Vec::new();
                inner.collect_free_vars(&mut inner_vars);
                for v in inner_vars {
                    if !bound.contains(&v) {
                        vars.push(v);
                    }
                }
            }
            _ => {}
        }
    }
}

impl fmt::Display for Type {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Type::Int => write!(f, "I"),
            Type::Int32 => write!(f, "I32"),
            Type::UInt => write!(f, "U"),
            Type::Float => write!(f, "F"),
            Type::Float32 => write!(f, "F32"),
            Type::Str => write!(f, "S"),
            Type::Bool => write!(f, "B"),
            Type::Void => write!(f, "V"),
            Type::Never => write!(f, "N"),
            Type::Fn(a, b) => {
                let needs_parens = matches!(**a, Type::Fn(_, _));
                if needs_parens {
                    write!(f, "({a}) -> {b}")
                } else {
                    write!(f, "{a} -> {b}")
                }
            }
            Type::Tuple(ts) => {
                write!(f, "(")?;
                for (i, t) in ts.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{t}")?;
                }
                write!(f, ")")
            }
            Type::Array(t) => write!(f, "[{t}]"),
            Type::Map(k, v) => write!(f, "{{{k}:{v}}}"),
            Type::Opt(t) => write!(f, "?{t}"),
            Type::Res(t) => write!(f, "!{t}"),
            Type::Ref(t) => write!(f, "&{t}"),
            Type::Ptr(t) => write!(f, "*{t}"),
            Type::Named(name, args) if args.is_empty() => write!(f, "{name}"),
            Type::Named(name, args) => {
                write!(f, "{name}[")?;
                for (i, a) in args.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{a}")?;
                }
                write!(f, "]")
            }
            Type::Record(row) => {
                write!(f, "{{")?;
                for (i, (name, ty)) in row.fields.iter().enumerate() {
                    if i > 0 {
                        write!(f, " ")?;
                    }
                    write!(f, "{name}:{ty}")?;
                }
                if row.rest.is_some() {
                    write!(f, " ..")?;
                }
                write!(f, "}}")
            }
            Type::Var(id) => write!(f, "t{id}"),
            Type::ForAll(vars, inner) => {
                write!(f, "forall")?;
                for v in vars {
                    write!(f, " t{v}")?;
                }
                write!(f, ". {inner}")
            }
            Type::Error => write!(f, "<error>"),
        }
    }
}
