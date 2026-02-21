use crate::span::Span;

#[derive(Debug, Clone, thiserror::Error)]
pub enum ParseError {
    #[error("unexpected token: {found}, expected {expected}")]
    UnexpectedToken {
        found: String,
        expected: String,
        span: Span,
    },
    #[error("unexpected end of input")]
    UnexpectedEof { span: Span },
    #[error("invalid indentation")]
    InvalidIndentation { span: Span },
    #[error("{message}")]
    Custom { message: String, span: Span },
}

impl ParseError {
    pub fn span(&self) -> Span {
        match self {
            Self::UnexpectedToken { span, .. }
            | Self::UnexpectedEof { span }
            | Self::InvalidIndentation { span }
            | Self::Custom { span, .. } => *span,
        }
    }
}
