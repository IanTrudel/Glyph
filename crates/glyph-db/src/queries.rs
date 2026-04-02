use crate::connection::Database;
use crate::error::{DbError, Result};
use crate::model::*;
use rusqlite::params;

impl Database {
    // ── Definition queries ───────────────────────────────────────────

    /// Insert a new definition into the database.
    /// Hash and token count are computed automatically.
    pub fn insert_def(&self, def: &NewDef) -> Result<i64> {
        let hash = compute_hash(def.kind.as_str(), def.sig.as_deref(), &def.body);
        let tokens = compute_tokens(&def.body);
        self.conn.execute(
            "INSERT INTO def (name, kind, sig, body, hash, tokens, gen) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            params![def.name, def.kind.as_str(), def.sig, def.body, hash, tokens, def.generation],
        )?;
        Ok(self.conn.last_insert_rowid())
    }

    /// Get a definition by id.
    pub fn get_def(&self, id: i64) -> Result<DefRow> {
        self.conn
            .query_row("SELECT * FROM def WHERE id = ?1", params![id], row_to_def)
            .map_err(|e| match e {
                rusqlite::Error::QueryReturnedNoRows => DbError::Other(format!("def id {id} not found")),
                other => DbError::Sqlite(other),
            })
    }

    /// Look up a definition by name and kind.
    pub fn resolve_name(&self, name: &str, kind: DefKind) -> Result<DefRow> {
        self.conn
            .query_row(
                "SELECT * FROM def WHERE name = ?1 AND kind = ?2",
                params![name, kind.as_str()],
                row_to_def,
            )
            .map_err(|e| match e {
                rusqlite::Error::QueryReturnedNoRows => DbError::DefNotFound {
                    name: name.to_string(),
                    kind: kind.as_str().to_string(),
                },
                other => DbError::Sqlite(other),
            })
    }

    /// Look up a definition by name (any kind).
    pub fn resolve_name_any(&self, name: &str) -> Result<Vec<DefRow>> {
        let mut stmt = self.conn.prepare("SELECT * FROM def WHERE name = ?1")?;
        let rows = stmt.query_map(params![name], row_to_def)?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    /// Get all dirty definitions (from v_dirty view).
    pub fn dirty_defs(&self) -> Result<Vec<DefRow>> {
        let mut stmt = self.conn.prepare("SELECT * FROM v_dirty")?;
        let rows = stmt.query_map([], row_to_def)?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    /// Get all definitions of a given kind.
    pub fn defs_by_kind(&self, kind: DefKind) -> Result<Vec<DefRow>> {
        let mut stmt = self.conn.prepare("SELECT * FROM def WHERE kind = ?1")?;
        let rows = stmt.query_map(params![kind.as_str()], row_to_def)?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    /// Get all definitions.
    pub fn all_defs(&self) -> Result<Vec<DefRow>> {
        let mut stmt = self.conn.prepare("SELECT * FROM def")?;
        let rows = stmt.query_map([], row_to_def)?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    /// Get effective definitions for a target generation.
    /// For each (name, kind) pair, selects the highest-gen version at or below target_gen.
    pub fn effective_defs(&self, target_gen: i64) -> Result<Vec<DefRow>> {
        let mut stmt = self.conn.prepare(
            "SELECT d.* FROM def d
             INNER JOIN (
                 SELECT name, kind, MAX(gen) as max_gen
                 FROM def WHERE gen <= ?1
                 GROUP BY name, kind
             ) latest ON d.name = latest.name AND d.kind = latest.kind AND d.gen = latest.max_gen"
        )?;
        let rows = stmt.query_map(params![target_gen], row_to_def)?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    /// Get dirty definitions for a target generation (effective defs filtered to dirty + transitive dependents).
    pub fn dirty_defs_gen(&self, target_gen: i64) -> Result<Vec<DefRow>> {
        let mut stmt = self.conn.prepare(
            "WITH RECURSIVE effective AS (
                 SELECT d.* FROM def d
                 INNER JOIN (
                     SELECT name, kind, MAX(gen) as max_gen
                     FROM def WHERE gen <= ?1
                     GROUP BY name, kind
                 ) latest ON d.name = latest.name AND d.kind = latest.kind AND d.gen = latest.max_gen
             ),
             dirty(id) AS (
                 SELECT id FROM effective WHERE compiled = 0
                 UNION
                 SELECT d.from_id FROM dep d JOIN dirty ON d.to_id = dirty.id
             )
             SELECT DISTINCT e.* FROM effective e JOIN dirty ON e.id = dirty.id"
        )?;
        let rows = stmt.query_map(params![target_gen], row_to_def)?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    /// Get effective definitions of a given kind for a target generation.
    pub fn defs_by_kind_gen(&self, kind: DefKind, target_gen: i64) -> Result<Vec<DefRow>> {
        let mut stmt = self.conn.prepare(
            "SELECT d.* FROM def d
             INNER JOIN (
                 SELECT name, kind, MAX(gen) as max_gen
                 FROM def WHERE gen <= ?1 AND kind = ?2
                 GROUP BY name, kind
             ) latest ON d.name = latest.name AND d.kind = latest.kind AND d.gen = latest.max_gen"
        )?;
        let rows = stmt.query_map(params![target_gen, kind.as_str()], row_to_def)?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    /// Mark a definition as compiled.
    pub fn mark_compiled(&self, id: i64) -> Result<()> {
        self.conn
            .execute("UPDATE def SET compiled = 1 WHERE id = ?1", params![id])?;
        Ok(())
    }

    /// Update a definition's body. Triggers will recompute hash/tokens/dirty.
    pub fn update_body(&self, id: i64, body: &str) -> Result<()> {
        self.conn
            .execute("UPDATE def SET body = ?1 WHERE id = ?2", params![body, id])?;
        Ok(())
    }

    // ── Dependency queries ───────────────────────────────────────────

    /// Insert a dependency edge.
    pub fn insert_dep(&self, from_id: i64, to_id: i64, edge: DepEdge) -> Result<()> {
        self.conn.execute(
            "INSERT OR IGNORE INTO dep (from_id, to_id, edge) VALUES (?1, ?2, ?3)",
            params![from_id, to_id, edge.as_str()],
        )?;
        Ok(())
    }

    /// Clear all dependency edges originating from a definition.
    pub fn clear_deps_from(&self, from_id: i64) -> Result<()> {
        self.conn
            .execute("DELETE FROM dep WHERE from_id = ?1", params![from_id])?;
        Ok(())
    }

    /// Get all dependencies from a definition.
    pub fn deps_from(&self, from_id: i64) -> Result<Vec<DepRow>> {
        let mut stmt = self
            .conn
            .prepare("SELECT from_id, to_id, edge FROM dep WHERE from_id = ?1")?;
        let rows = stmt.query_map(params![from_id], |row| {
            Ok(DepRow {
                from_id: row.get(0)?,
                to_id: row.get(1)?,
                edge: DepEdge::from_str(&row.get::<_, String>(2)?).unwrap(),
            })
        })?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    /// Get all dependents of a definition (who depends on me).
    pub fn deps_to(&self, to_id: i64) -> Result<Vec<DepRow>> {
        let mut stmt = self
            .conn
            .prepare("SELECT from_id, to_id, edge FROM dep WHERE to_id = ?1")?;
        let rows = stmt.query_map(params![to_id], |row| {
            Ok(DepRow {
                from_id: row.get(0)?,
                to_id: row.get(1)?,
                edge: DepEdge::from_str(&row.get::<_, String>(2)?).unwrap(),
            })
        })?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    // ── Extern queries ───────────────────────────────────────────────

    /// Insert an extern function declaration.
    pub fn insert_extern(
        &self,
        name: &str,
        symbol: &str,
        lib: Option<&str>,
        sig: &str,
        conv: CallingConv,
    ) -> Result<i64> {
        self.conn.execute(
            "INSERT INTO extern_ (name, symbol, lib, sig, conv) VALUES (?1, ?2, ?3, ?4, ?5)",
            params![name, symbol, lib, sig, conv.as_str()],
        )?;
        Ok(self.conn.last_insert_rowid())
    }

    /// Get all extern declarations.
    pub fn all_externs(&self) -> Result<Vec<ExternRow>> {
        let mut stmt = self.conn.prepare("SELECT * FROM extern_")?;
        let rows = stmt.query_map([], |row| {
            Ok(ExternRow {
                id: row.get(0)?,
                name: row.get(1)?,
                symbol: row.get(2)?,
                lib: row.get(3)?,
                sig: row.get(4)?,
                conv: CallingConv::from_str(&row.get::<_, String>(5)?).unwrap(),
            })
        })?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    /// Look up an extern by Glyph-side name.
    pub fn resolve_extern(&self, name: &str) -> Result<ExternRow> {
        self.conn
            .query_row(
                "SELECT * FROM extern_ WHERE name = ?1",
                params![name],
                |row| {
                    Ok(ExternRow {
                        id: row.get(0)?,
                        name: row.get(1)?,
                        symbol: row.get(2)?,
                        lib: row.get(3)?,
                        sig: row.get(4)?,
                        conv: CallingConv::from_str(&row.get::<_, String>(5)?).unwrap(),
                    })
                },
            )
            .map_err(|e| match e {
                rusqlite::Error::QueryReturnedNoRows => DbError::Other(format!("extern '{name}' not found")),
                other => DbError::Sqlite(other),
            })
    }

    // ── Tag queries ──────────────────────────────────────────────────

    /// Set a tag on a definition.
    pub fn set_tag(&self, def_id: i64, key: &str, val: Option<&str>) -> Result<()> {
        self.conn.execute(
            "INSERT OR REPLACE INTO tag (def_id, key, val) VALUES (?1, ?2, ?3)",
            params![def_id, key, val],
        )?;
        Ok(())
    }

    /// Get all tags for a definition.
    pub fn get_tags(&self, def_id: i64) -> Result<Vec<(String, Option<String>)>> {
        let mut stmt = self
            .conn
            .prepare("SELECT key, val FROM tag WHERE def_id = ?1")?;
        let rows = stmt.query_map(params![def_id], |row| Ok((row.get(0)?, row.get(1)?)))?;
        rows.collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)
    }

    // ── Compiled cache ───────────────────────────────────────────────

    /// Cache compiled IR for a definition.
    pub fn cache_compiled(&self, def_id: i64, ir: &[u8], target: &str, hash: &[u8]) -> Result<()> {
        self.conn.execute(
            "INSERT OR REPLACE INTO compiled (def_id, ir, target, hash) VALUES (?1, ?2, ?3, ?4)",
            params![def_id, ir, target, hash],
        )?;
        Ok(())
    }

    /// Get cached compiled IR for a definition.
    pub fn get_compiled(&self, def_id: i64) -> Result<Option<CompiledRow>> {
        match self.conn.query_row(
            "SELECT def_id, ir, target, hash FROM compiled WHERE def_id = ?1",
            params![def_id],
            |row| {
                Ok(CompiledRow {
                    def_id: row.get(0)?,
                    ir: row.get(1)?,
                    target: row.get(2)?,
                    hash: row.get(3)?,
                })
            },
        ) {
            Ok(row) => Ok(Some(row)),
            Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
            Err(e) => Err(DbError::Sqlite(e)),
        }
    }
}

// ── Helpers ──────────────────────────────────────────────────────────

fn row_to_def(row: &rusqlite::Row) -> rusqlite::Result<DefRow> {
    Ok(DefRow {
        id: row.get("id")?,
        name: row.get("name")?,
        kind: DefKind::from_str(&row.get::<_, String>("kind")?).unwrap(),
        sig: row.get("sig")?,
        body: row.get("body")?,
        hash: row.get("hash")?,
        tokens: row.get("tokens")?,
        compiled: row.get::<_, i64>("compiled")? != 0,
        generation: row.get("gen")?,
    })
}

fn compute_hash(kind: &str, sig: Option<&str>, body: &str) -> Vec<u8> {
    let mut hasher = blake3::Hasher::new();
    hasher.update(kind.as_bytes());
    if let Some(s) = sig {
        hasher.update(s.as_bytes());
    }
    hasher.update(body.as_bytes());
    hasher.finalize().as_bytes().to_vec()
}

fn compute_tokens(body: &str) -> i64 {
    let bpe = tiktoken_rs::cl100k_base().expect("failed to load tokenizer");
    bpe.encode_with_special_tokens(body).len() as i64
}

impl Database {
    /// Attach libraries registered in `lib_dep` and return their definitions.
    /// Returns an empty vec if no `lib_dep` table exists.
    pub fn load_library_defs(&self, target_gen: i64) -> Result<Vec<DefRow>> {
        // Check if lib_dep table exists
        let has_table: bool = self.conn.query_row(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='lib_dep'",
            [],
            |row| row.get::<_, i64>(0),
        ).unwrap_or(0) > 0;
        if !has_table {
            return Ok(Vec::new());
        }

        // Read library paths
        let mut stmt = self.conn.prepare("SELECT lib_path FROM lib_dep")?;
        let paths: Vec<String> = stmt.query_map([], |row| row.get(0))?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(DbError::Sqlite)?;

        let mut all_defs = Vec::new();
        for (i, path) in paths.iter().enumerate() {
            let alias = format!("lib{}", i);
            let attach_sql = format!("ATTACH '{}' AS {}", path.replace('\'', "''"), alias);
            if self.conn.execute_batch(&attach_sql).is_err() {
                eprintln!("Warning: could not attach library '{}'", path);
                continue;
            }

            // Read effective defs from the attached library
            let query = format!(
                "SELECT d.* FROM {alias}.def d
                 INNER JOIN (
                     SELECT name, kind, MAX(gen) as max_gen
                     FROM {alias}.def WHERE gen <= ?1
                     GROUP BY name, kind
                 ) latest ON d.name = latest.name AND d.kind = latest.kind AND d.gen = latest.max_gen"
            );
            if let Ok(mut stmt) = self.conn.prepare(&query) {
                if let Ok(rows) = stmt.query_map(params![target_gen], row_to_def) {
                    for row in rows.flatten() {
                        all_defs.push(row);
                    }
                }
            }
        }

        Ok(all_defs)
    }
}
