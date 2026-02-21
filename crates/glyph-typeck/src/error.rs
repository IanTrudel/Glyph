use glyph_parse::span::Span;

#[derive(Debug, Clone, thiserror::Error)]
pub enum TypeError {
    #[error("type mismatch: expected {expected}, found {found}")]
    Mismatch {
        expected: String,
        found: String,
        span: Span,
    },

    #[error("unresolved name: {name}")]
    UnresolvedName { name: String, span: Span },

    #[error("occurs check failed: {0} occurs in {1}")]
    OccursCheck(String, String),

    #[error("cannot unify row types: missing field {field}")]
    MissingField { field: String, span: Span },

    #[error("cannot infer type for {name}")]
    CannotInfer { name: String, span: Span },

    #[error("{message}")]
    Custom { message: String, span: Span },
}

impl TypeError {
    pub fn span(&self) -> Option<Span> {
        match self {
            Self::Mismatch { span, .. }
            | Self::UnresolvedName { span, .. }
            | Self::MissingField { span, .. }
            | Self::CannotInfer { span, .. }
            | Self::Custom { span, .. } => Some(*span),
            Self::OccursCheck(_, _) => None,
        }
    }
}

pub type Result<T> = std::result::Result<T, TypeError>;
