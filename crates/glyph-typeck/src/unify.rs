use crate::error::{Result, TypeError};
use crate::types::{RowType, Type, TypeVarId};
use std::collections::BTreeMap;

/// Union-find based type variable substitution.
pub struct Substitution {
    /// parent[i] = the representative of type var i (or itself if root).
    parent: Vec<TypeVarId>,
    /// The resolved type for each root variable, if any.
    types: Vec<Option<Type>>,
    next_var: TypeVarId,
}

impl Substitution {
    pub fn new() -> Self {
        Self {
            parent: Vec::new(),
            types: Vec::new(),
            next_var: 0,
        }
    }

    /// Create a fresh type variable.
    pub fn fresh_var(&mut self) -> TypeVarId {
        let id = self.next_var;
        self.next_var += 1;
        self.parent.push(id);
        self.types.push(None);
        id
    }

    /// Create a fresh Type::Var.
    pub fn fresh(&mut self) -> Type {
        Type::Var(self.fresh_var())
    }

    /// Find the root representative of a type variable.
    pub fn find(&mut self, mut v: TypeVarId) -> TypeVarId {
        while self.parent[v as usize] != v {
            // Path compression
            self.parent[v as usize] = self.parent[self.parent[v as usize] as usize];
            v = self.parent[v as usize];
        }
        v
    }

    /// Get the type bound to a variable, if any.
    pub fn probe(&mut self, v: TypeVarId) -> Option<Type> {
        let root = self.find(v);
        self.types[root as usize].clone()
    }

    /// Walk a type, resolving all known type variables.
    pub fn walk(&mut self, ty: &Type) -> Type {
        match ty {
            Type::Var(v) => {
                let root = self.find(*v);
                if let Some(resolved) = self.types[root as usize].clone() {
                    self.walk(&resolved)
                } else {
                    Type::Var(root)
                }
            }
            _ => ty.clone(),
        }
    }

    /// Fully resolve a type, walking through all structure.
    pub fn resolve(&mut self, ty: &Type) -> Type {
        match ty {
            Type::Var(v) => {
                let root = self.find(*v);
                if let Some(resolved) = self.types[root as usize].clone() {
                    self.resolve(&resolved)
                } else {
                    Type::Var(root)
                }
            }
            Type::Fn(a, b) => Type::Fn(Box::new(self.resolve(a)), Box::new(self.resolve(b))),
            Type::Tuple(ts) => Type::Tuple(ts.iter().map(|t| self.resolve(t)).collect()),
            Type::Array(t) => Type::Array(Box::new(self.resolve(t))),
            Type::Map(k, v) => Type::Map(Box::new(self.resolve(k)), Box::new(self.resolve(v))),
            Type::Opt(t) => Type::Opt(Box::new(self.resolve(t))),
            Type::Res(t) => Type::Res(Box::new(self.resolve(t))),
            Type::Ref(t) => Type::Ref(Box::new(self.resolve(t))),
            Type::Ptr(t) => Type::Ptr(Box::new(self.resolve(t))),
            Type::Named(name, args) => {
                Type::Named(name.clone(), args.iter().map(|a| self.resolve(a)).collect())
            }
            Type::Record(row) => {
                let fields = row
                    .fields
                    .iter()
                    .map(|(k, v)| (k.clone(), self.resolve(v)))
                    .collect();
                let rest = row.rest.map(|r| self.find(r));
                Type::Record(RowType { fields, rest })
            }
            Type::ForAll(vars, inner) => {
                Type::ForAll(vars.clone(), Box::new(self.resolve(inner)))
            }
            other => other.clone(),
        }
    }

    /// Unify two types.
    pub fn unify(&mut self, a: &Type, b: &Type) -> Result<()> {
        let a = self.walk(a);
        let b = self.walk(b);

        match (&a, &b) {
            // Same type
            _ if a == b => Ok(()),

            // Error absorbs
            (Type::Error, _) | (_, Type::Error) => Ok(()),

            // Type variables
            (Type::Var(v), _) => self.bind(*v, &b),
            (_, Type::Var(v)) => self.bind(*v, &a),

            // Functions
            (Type::Fn(a1, r1), Type::Fn(a2, r2)) => {
                self.unify(a1, a2)?;
                self.unify(r1, r2)
            }

            // Tuples
            (Type::Tuple(ts1), Type::Tuple(ts2)) if ts1.len() == ts2.len() => {
                for (t1, t2) in ts1.iter().zip(ts2.iter()) {
                    self.unify(t1, t2)?;
                }
                Ok(())
            }

            // Compound
            (Type::Array(t1), Type::Array(t2))
            | (Type::Opt(t1), Type::Opt(t2))
            | (Type::Res(t1), Type::Res(t2))
            | (Type::Ref(t1), Type::Ref(t2))
            | (Type::Ptr(t1), Type::Ptr(t2)) => self.unify(t1, t2),

            (Type::Map(k1, v1), Type::Map(k2, v2)) => {
                self.unify(k1, k2)?;
                self.unify(v1, v2)
            }

            // Named types
            (Type::Named(n1, args1), Type::Named(n2, args2))
                if n1 == n2 && args1.len() == args2.len() =>
            {
                for (a1, a2) in args1.iter().zip(args2.iter()) {
                    self.unify(a1, a2)?;
                }
                Ok(())
            }

            // Records (row unification)
            (Type::Record(r1), Type::Record(r2)) => self.unify_rows(r1, r2),

            _ => Err(TypeError::Mismatch {
                expected: format!("{a}"),
                found: format!("{b}"),
                span: glyph_parse::span::Span::new(0, 0),
            }),
        }
    }

    /// Bind a type variable to a type (with occurs check).
    fn bind(&mut self, var: TypeVarId, ty: &Type) -> Result<()> {
        let root = self.find(var);
        if let Type::Var(v2) = ty {
            let root2 = self.find(*v2);
            if root == root2 {
                return Ok(());
            }
            self.parent[root as usize] = root2;
            return Ok(());
        }

        // Occurs check
        let resolved = self.resolve(ty);
        if resolved.contains_var(root) {
            return Err(TypeError::OccursCheck(
                format!("t{root}"),
                format!("{resolved}"),
            ));
        }

        self.types[root as usize] = Some(ty.clone());
        Ok(())
    }

    /// Row unification for extensible records.
    fn unify_rows(&mut self, r1: &RowType, r2: &RowType) -> Result<()> {
        // Unify common fields
        for (k, t1) in &r1.fields {
            if let Some(t2) = r2.fields.get(k) {
                self.unify(t1, t2)?;
            } else if r2.rest.is_none() {
                return Err(TypeError::MissingField {
                    field: k.clone(),
                    span: glyph_parse::span::Span::new(0, 0),
                });
            }
        }

        for (k, _t2) in &r2.fields {
            if !r1.fields.contains_key(k) && r1.rest.is_none() {
                return Err(TypeError::MissingField {
                    field: k.clone(),
                    span: glyph_parse::span::Span::new(0, 0),
                });
            }
        }

        // Handle row extensions
        match (r1.rest, r2.rest) {
            (Some(rv1), Some(rv2)) => {
                // Both open: create a new row variable for the remaining fields
                let extra1: BTreeMap<_, _> = r1
                    .fields
                    .iter()
                    .filter(|(k, _)| !r2.fields.contains_key(*k))
                    .map(|(k, v)| (k.clone(), v.clone()))
                    .collect();
                let extra2: BTreeMap<_, _> = r2
                    .fields
                    .iter()
                    .filter(|(k, _)| !r1.fields.contains_key(*k))
                    .map(|(k, v)| (k.clone(), v.clone()))
                    .collect();

                let fresh = self.fresh_var();

                // rv1 should be {extra2 fields ..fresh}
                if !extra2.is_empty() || true {
                    self.bind(
                        rv1,
                        &Type::Record(RowType {
                            fields: extra2,
                            rest: Some(fresh),
                        }),
                    )?;
                }

                // rv2 should be {extra1 fields ..fresh}
                if !extra1.is_empty() || true {
                    self.bind(
                        rv2,
                        &Type::Record(RowType {
                            fields: extra1,
                            rest: Some(fresh),
                        }),
                    )?;
                }
            }
            (Some(rv), None) => {
                // r1 is open, r2 is closed: rv must be empty record minus extra fields
                let extra: BTreeMap<_, _> = r2
                    .fields
                    .iter()
                    .filter(|(k, _)| !r1.fields.contains_key(*k))
                    .map(|(k, v)| (k.clone(), v.clone()))
                    .collect();
                self.bind(rv, &Type::Record(RowType::closed(extra)))?;
            }
            (None, Some(rv)) => {
                let extra: BTreeMap<_, _> = r1
                    .fields
                    .iter()
                    .filter(|(k, _)| !r2.fields.contains_key(*k))
                    .map(|(k, v)| (k.clone(), v.clone()))
                    .collect();
                self.bind(rv, &Type::Record(RowType::closed(extra)))?;
            }
            (None, None) => {
                // Both closed: all fields must match (already checked above)
                if r1.fields.len() != r2.fields.len() {
                    return Err(TypeError::Mismatch {
                        expected: format!("{}", Type::Record(r1.clone())),
                        found: format!("{}", Type::Record(r2.clone())),
                        span: glyph_parse::span::Span::new(0, 0),
                    });
                }
            }
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_unify_same() {
        let mut subst = Substitution::new();
        subst.unify(&Type::Int, &Type::Int).unwrap();
    }

    #[test]
    fn test_unify_var() {
        let mut subst = Substitution::new();
        let v = subst.fresh();
        subst.unify(&v, &Type::Int).unwrap();
        assert_eq!(subst.resolve(&v), Type::Int);
    }

    #[test]
    fn test_unify_fn() {
        let mut subst = Substitution::new();
        let a = subst.fresh();
        let b = subst.fresh();
        let f1 = Type::Fn(Box::new(a.clone()), Box::new(b.clone()));
        let f2 = Type::Fn(Box::new(Type::Int), Box::new(Type::Str));
        subst.unify(&f1, &f2).unwrap();
        assert_eq!(subst.resolve(&a), Type::Int);
        assert_eq!(subst.resolve(&b), Type::Str);
    }

    #[test]
    fn test_occurs_check() {
        let mut subst = Substitution::new();
        let v = subst.fresh();
        let recursive = Type::Array(Box::new(v.clone()));
        assert!(subst.unify(&v, &recursive).is_err());
    }

    #[test]
    fn test_row_unification() {
        let mut subst = Substitution::new();
        let rv = subst.fresh_var();

        let open_row = RowType::open(
            [("name".into(), Type::Str)].into_iter().collect(),
            rv,
        );
        let closed_row = RowType::closed(
            [("name".into(), Type::Str), ("age".into(), Type::Int)]
                .into_iter()
                .collect(),
        );

        subst
            .unify(&Type::Record(open_row), &Type::Record(closed_row))
            .unwrap();

        // rv should resolve to {age:I}
        let resolved = subst.resolve(&Type::Var(rv));
        if let Type::Record(row) = &resolved {
            assert!(row.fields.contains_key("age"));
            assert_eq!(row.fields.len(), 1);
        } else {
            panic!("expected record, got {resolved}");
        }
    }

    #[test]
    fn test_mismatch() {
        let mut subst = Substitution::new();
        assert!(subst.unify(&Type::Int, &Type::Str).is_err());
    }
}
