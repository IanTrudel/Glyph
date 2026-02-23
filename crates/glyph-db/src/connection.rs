use crate::error::{DbError, Result};
use crate::functions;
use crate::schema;
use rusqlite::Connection;
use std::path::Path;

/// The main database wrapper. A `Database` owns a `rusqlite::Connection`
/// with custom functions registered and schema initialized.
pub struct Database {
    pub(crate) conn: Connection,
}

impl Database {
    /// Open an existing .glyph database.
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        let conn = Connection::open(path)?;
        functions::register_all(&conn)?;
        conn.execute_batch("PRAGMA foreign_keys = ON;")?;
        conn.execute_batch(schema::TRIGGER_SQL)?;
        let db = Self { conn };
        db.migrate_gen()?;
        db.migrate_history()?;
        Ok(db)
    }

    /// Create a new .glyph database with the full schema.
    pub fn create(path: impl AsRef<Path>) -> Result<Self> {
        let conn = Connection::open(path)?;
        functions::register_all(&conn)?;
        conn.execute_batch(schema::SCHEMA_SQL)?;
        conn.execute_batch(schema::TRIGGER_SQL)?;
        Ok(Self { conn })
    }

    /// Create an in-memory database (for testing).
    pub fn in_memory() -> Result<Self> {
        let conn = Connection::open_in_memory()?;
        functions::register_all(&conn)?;
        conn.execute_batch(schema::SCHEMA_SQL)?;
        conn.execute_batch(schema::TRIGGER_SQL)?;
        Ok(Self { conn })
    }

    /// Get the underlying connection (for advanced use).
    pub fn conn(&self) -> &Connection {
        &self.conn
    }

    /// Migrate an existing database to add the `gen` column if missing.
    pub fn migrate_gen(&self) -> Result<()> {
        let has_gen: bool = self.conn
            .query_row(
                "SELECT COUNT(*) FROM pragma_table_info('def') WHERE name = 'gen'",
                [],
                |row| row.get::<_, i64>(0),
            )
            .map(|n| n > 0)
            .unwrap_or(false);

        if !has_gen {
            self.conn.execute_batch(
                "ALTER TABLE def ADD COLUMN gen INTEGER NOT NULL DEFAULT 1;
                 DROP INDEX IF EXISTS idx_def_name_kind;
                 CREATE UNIQUE INDEX IF NOT EXISTS idx_def_name_kind_gen ON def(name, kind, gen);
                 CREATE INDEX IF NOT EXISTS idx_def_gen ON def(gen);"
            )?;
        }
        Ok(())
    }

    /// Migrate an existing database to add the `def_history` table and triggers if missing.
    pub fn migrate_history(&self) -> Result<()> {
        let has_history: bool = self.conn
            .query_row(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='def_history'",
                [],
                |row| row.get::<_, i64>(0),
            )
            .map(|n| n > 0)
            .unwrap_or(false);

        if !has_history {
            self.conn.execute_batch(
                "CREATE TABLE IF NOT EXISTS def_history (
                   id         INTEGER PRIMARY KEY,
                   def_id     INTEGER NOT NULL,
                   name       TEXT NOT NULL,
                   kind       TEXT NOT NULL,
                   sig        TEXT,
                   body       TEXT NOT NULL,
                   hash       BLOB NOT NULL,
                   tokens     INTEGER NOT NULL,
                   gen        INTEGER NOT NULL DEFAULT 1,
                   changed_at TEXT NOT NULL DEFAULT (datetime('now'))
                 );
                 CREATE INDEX IF NOT EXISTS idx_history_name ON def_history(name, kind);
                 CREATE TRIGGER IF NOT EXISTS trg_def_history_delete BEFORE DELETE ON def
                 BEGIN
                   INSERT INTO def_history (def_id, name, kind, sig, body, hash, tokens, gen)
                   VALUES (OLD.id, OLD.name, OLD.kind, OLD.sig, OLD.body, OLD.hash, OLD.tokens, OLD.gen);
                 END;
                 CREATE TRIGGER IF NOT EXISTS trg_def_history_update BEFORE UPDATE OF body ON def
                 BEGIN
                   INSERT INTO def_history (def_id, name, kind, sig, body, hash, tokens, gen)
                   VALUES (OLD.id, OLD.name, OLD.kind, OLD.sig, OLD.body, OLD.hash, OLD.tokens, OLD.gen);
                 END;"
            )?;
        }
        Ok(())
    }

    /// Check if this database has the Glyph schema.
    pub fn is_initialized(&self) -> bool {
        self.conn
            .query_row(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='def'",
                [],
                |row| row.get::<_, i64>(0),
            )
            .map(|n| n > 0)
            .unwrap_or(false)
    }

    /// Begin a transaction. Returns a Transaction guard.
    pub fn transaction(&mut self) -> Result<rusqlite::Transaction<'_>> {
        self.conn.transaction().map_err(DbError::Sqlite)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::*;

    #[test]
    fn test_create_and_insert() {
        let db = Database::in_memory().unwrap();
        assert!(db.is_initialized());

        let id = db
            .insert_def(&NewDef {
                name: "hello".into(),
                kind: DefKind::Fn,
                sig: None,
                body: "hello = say \"world\"".into(),
                generation: 1,
            })
            .unwrap();

        let def = db.get_def(id).unwrap();
        assert_eq!(def.name, "hello");
        assert_eq!(def.kind, DefKind::Fn);
        assert!(!def.hash.is_empty());
        assert!(def.tokens > 0);
        assert!(!def.compiled);
    }

    #[test]
    fn test_mark_compiled() {
        let db = Database::in_memory().unwrap();
        let id = db
            .insert_def(&NewDef {
                name: "f".into(),
                kind: DefKind::Fn,
                sig: None,
                body: "f x = x".into(),
                generation: 1,
            })
            .unwrap();

        db.mark_compiled(id).unwrap();
        let def = db.get_def(id).unwrap();
        assert!(def.compiled);
    }

    #[test]
    fn test_dirty_cascade() {
        let db = Database::in_memory().unwrap();

        // Create two defs: `g` calls `f`
        let f_id = db
            .insert_def(&NewDef {
                name: "f".into(),
                kind: DefKind::Fn,
                sig: None,
                body: "f x = x + 1".into(),
                generation: 1,
            })
            .unwrap();
        let g_id = db
            .insert_def(&NewDef {
                name: "g".into(),
                kind: DefKind::Fn,
                sig: None,
                body: "g x = f(x)".into(),
                generation: 1,
            })
            .unwrap();

        // Record dependency
        db.insert_dep(g_id, f_id, DepEdge::Calls).unwrap();

        // Mark both compiled
        db.mark_compiled(f_id).unwrap();
        db.mark_compiled(g_id).unwrap();

        // Modify f's body — should dirty f
        db.update_body(f_id, "f x = x + 2").unwrap();

        // f should be dirty
        let f = db.get_def(f_id).unwrap();
        assert!(!f.compiled);

        // g should also be dirty (cascade via trigger)
        let g = db.get_def(g_id).unwrap();
        assert!(!g.compiled);

        // v_dirty should return both
        let dirty = db.dirty_defs().unwrap();
        assert_eq!(dirty.len(), 2);
    }

    #[test]
    fn test_resolve_name() {
        let db = Database::in_memory().unwrap();
        db.insert_def(&NewDef {
            name: "MyType".into(),
            kind: DefKind::Type,
            sig: None,
            body: "MyType = {x:I y:I}".into(),
            generation: 1,
        })
        .unwrap();

        let def = db.resolve_name("MyType", DefKind::Type).unwrap();
        assert_eq!(def.name, "MyType");

        assert!(db.resolve_name("MyType", DefKind::Fn).is_err());
    }

    #[test]
    fn test_extern() {
        let db = Database::in_memory().unwrap();
        db.insert_extern("write", "write", None, "I -> *V -> U -> I", CallingConv::C)
            .unwrap();

        let ext = db.resolve_extern("write").unwrap();
        assert_eq!(ext.symbol, "write");
        assert_eq!(ext.conv, CallingConv::C);
    }

    #[test]
    fn test_tags() {
        let db = Database::in_memory().unwrap();
        let id = db
            .insert_def(&NewDef {
                name: "alloc".into(),
                kind: DefKind::Fn,
                sig: None,
                body: "alloc n = malloc(n)".into(),
                generation: 1,
            })
            .unwrap();

        db.set_tag(id, "unsafe", None).unwrap();
        db.set_tag(id, "doc", Some("Allocate memory")).unwrap();

        let tags = db.get_tags(id).unwrap();
        assert_eq!(tags.len(), 2);
    }
}
