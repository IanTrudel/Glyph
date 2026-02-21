use crate::env::TypeEnv;
use crate::types::Type;

/// Register builtin types and operator signatures.
pub fn register_builtins(env: &mut TypeEnv) {
    // Boolean constants
    env.insert("true".into(), Type::Bool);
    env.insert("false".into(), Type::Bool);
    // null / none could be added here
}

/// Get the result type of a binary operator given operand types.
/// Returns None if the operator is not defined for these types.
pub fn binop_type(op: &glyph_parse::ast::BinOp, left: &Type, right: &Type) -> Option<Type> {
    use glyph_parse::ast::BinOp;

    match op {
        BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Mod => {
            // Numeric ops: same type in, same type out
            match (left, right) {
                (Type::Int, Type::Int) => Some(Type::Int),
                (Type::Int32, Type::Int32) => Some(Type::Int32),
                (Type::UInt, Type::UInt) => Some(Type::UInt),
                (Type::Float, Type::Float) => Some(Type::Float),
                (Type::Float32, Type::Float32) => Some(Type::Float32),
                _ => None,
            }
        }
        BinOp::Eq | BinOp::Neq => {
            // Equality: same type in, Bool out
            if left == right {
                Some(Type::Bool)
            } else {
                None
            }
        }
        BinOp::Lt | BinOp::Gt | BinOp::LtEq | BinOp::GtEq => {
            // Comparison: ordered types
            match (left, right) {
                (Type::Int, Type::Int)
                | (Type::Int32, Type::Int32)
                | (Type::UInt, Type::UInt)
                | (Type::Float, Type::Float)
                | (Type::Float32, Type::Float32)
                | (Type::Str, Type::Str) => Some(Type::Bool),
                _ => None,
            }
        }
        BinOp::And | BinOp::Or => {
            match (left, right) {
                (Type::Bool, Type::Bool) => Some(Type::Bool),
                _ => None,
            }
        }
    }
}

/// Get the result type of a unary operator.
pub fn unaryop_type(op: &glyph_parse::ast::UnaryOp, operand: &Type) -> Option<Type> {
    use glyph_parse::ast::UnaryOp;

    match op {
        UnaryOp::Neg => match operand {
            Type::Int => Some(Type::Int),
            Type::Int32 => Some(Type::Int32),
            Type::Float => Some(Type::Float),
            Type::Float32 => Some(Type::Float32),
            _ => None,
        },
        UnaryOp::Not => match operand {
            Type::Bool => Some(Type::Bool),
            _ => None,
        },
        UnaryOp::Ref => Some(Type::Ref(Box::new(operand.clone()))),
        UnaryOp::Deref => {
            match operand {
                Type::Ref(inner) | Type::Ptr(inner) => Some(*inner.clone()),
                _ => None,
            }
        }
    }
}
