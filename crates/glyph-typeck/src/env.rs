use crate::types::{Type, TypeVarId};
use crate::unify::Substitution;
use std::collections::HashMap;

/// Type environment: maps names to their type schemes.
#[derive(Debug, Clone)]
pub struct TypeEnv {
    scopes: Vec<HashMap<String, Type>>,
}

impl TypeEnv {
    pub fn new() -> Self {
        Self {
            scopes: vec![HashMap::new()],
        }
    }

    pub fn push_scope(&mut self) {
        self.scopes.push(HashMap::new());
    }

    pub fn pop_scope(&mut self) {
        self.scopes.pop();
    }

    pub fn insert(&mut self, name: String, ty: Type) {
        self.scopes.last_mut().unwrap().insert(name, ty);
    }

    pub fn lookup(&self, name: &str) -> Option<&Type> {
        for scope in self.scopes.iter().rev() {
            if let Some(ty) = scope.get(name) {
                return Some(ty);
            }
        }
        None
    }

    /// Generalize a type: wrap free variables not in the environment into ForAll.
    pub fn generalize(&self, subst: &mut Substitution, ty: &Type) -> Type {
        let resolved = subst.resolve(ty);
        let ty_vars = resolved.free_vars();
        let env_vars = self.free_vars_in_env(subst);
        let gen_vars: Vec<TypeVarId> =
            ty_vars.into_iter().filter(|v| !env_vars.contains(v)).collect();
        if gen_vars.is_empty() {
            resolved
        } else {
            Type::ForAll(gen_vars, Box::new(resolved))
        }
    }

    fn free_vars_in_env(&self, subst: &mut Substitution) -> Vec<TypeVarId> {
        let mut vars = Vec::new();
        for scope in &self.scopes {
            for ty in scope.values() {
                let resolved = subst.resolve(ty);
                vars.extend(resolved.free_vars());
            }
        }
        vars.sort_unstable();
        vars.dedup();
        vars
    }
}
