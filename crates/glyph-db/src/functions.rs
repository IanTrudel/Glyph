use rusqlite::Connection;
use rusqlite::functions::FunctionFlags;

/// Register custom SQLite functions: `glyph_hash` and `glyph_tokens`.
pub fn register_all(conn: &Connection) -> rusqlite::Result<()> {
    register_glyph_hash(conn)?;
    register_glyph_tokens(conn)?;
    Ok(())
}

/// `glyph_hash(kind, sig, body)` → BLAKE3 hash as BLOB.
/// sig may be NULL. Hash input: `kind || sig || body`.
fn register_glyph_hash(conn: &Connection) -> rusqlite::Result<()> {
    conn.create_scalar_function(
        "glyph_hash",
        3,
        FunctionFlags::SQLITE_UTF8 | FunctionFlags::SQLITE_DETERMINISTIC,
        |ctx| {
            let kind: String = ctx.get(0)?;
            let sig: Option<String> = ctx.get(1)?;
            let body: String = ctx.get(2)?;

            let mut hasher = blake3::Hasher::new();
            hasher.update(kind.as_bytes());
            if let Some(s) = &sig {
                hasher.update(s.as_bytes());
            }
            hasher.update(body.as_bytes());
            let hash = hasher.finalize();
            Ok(hash.as_bytes().to_vec())
        },
    )
}

/// `glyph_tokens(body)` → token count (INTEGER) using cl200k_base tokenizer.
fn register_glyph_tokens(conn: &Connection) -> rusqlite::Result<()> {
    conn.create_scalar_function(
        "glyph_tokens",
        1,
        FunctionFlags::SQLITE_UTF8 | FunctionFlags::SQLITE_DETERMINISTIC,
        |ctx| {
            let body: String = ctx.get(0)?;
            let bpe = tiktoken_rs::cl100k_base().expect("failed to load tokenizer");
            let count = bpe.encode_with_special_tokens(&body).len() as i64;
            Ok(count)
        },
    )
}
