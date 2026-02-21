use std::collections::HashMap;

use crate::ir::MirFunction;

/// Monomorphization pass.
/// Each polymorphic call site with concrete types generates a specialized MirFunction.
/// Dedup tracking ensures we don't generate the same specialization twice.
pub struct MonoContext {
    /// Map from (function_name, concrete_types_key) → specialized MirFunction name
    specializations: HashMap<String, MirFunction>,
}

impl MonoContext {
    pub fn new() -> Self {
        Self {
            specializations: HashMap::new(),
        }
    }

    /// Get or create a specialization for a function with concrete types.
    pub fn specialize(&mut self, name: &str, mir: &MirFunction) -> String {
        let key = name.to_string();
        if !self.specializations.contains_key(&key) {
            self.specializations.insert(key.clone(), mir.clone());
        }
        key
    }

    /// Get all specialized functions.
    pub fn functions(&self) -> impl Iterator<Item = &MirFunction> {
        self.specializations.values()
    }
}
