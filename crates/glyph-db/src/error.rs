use thiserror::Error;

#[derive(Debug, Error)]
pub enum DbError {
    #[error("sqlite error: {0}")]
    Sqlite(#[from] rusqlite::Error),

    #[error("definition not found: {name} (kind: {kind})")]
    DefNotFound { name: String, kind: String },

    #[error("duplicate definition: {name} (kind: {kind})")]
    DuplicateDef { name: String, kind: String },

    #[error("database not initialized — run `glyph init`")]
    NotInitialized,

    #[error("{0}")]
    Other(String),
}

pub type Result<T> = std::result::Result<T, DbError>;
