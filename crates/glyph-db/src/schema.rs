/// The complete Glyph database schema (spec §2).
pub const SCHEMA_SQL: &str = r#"
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

---------------------------------------------------------------------
-- DEFINITIONS: the atoms of a Glyph program
---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS def (
  id        INTEGER PRIMARY KEY,
  name      TEXT    NOT NULL,
  kind      TEXT    NOT NULL CHECK(kind IN (
              'fn','type','trait','impl','const','fsm','srv','macro','test'
            )),
  sig       TEXT,
  body      TEXT    NOT NULL,
  hash      BLOB    NOT NULL,
  tokens    INTEGER NOT NULL,
  compiled  INTEGER NOT NULL DEFAULT 0,
  gen       INTEGER NOT NULL DEFAULT 1,
  created   TEXT    NOT NULL DEFAULT (datetime('now')),
  modified  TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_def_name_kind_gen ON def(name, kind, gen);
CREATE INDEX IF NOT EXISTS idx_def_kind ON def(kind);
CREATE INDEX IF NOT EXISTS idx_def_gen ON def(gen);
CREATE INDEX IF NOT EXISTS idx_def_compiled ON def(compiled) WHERE compiled = 0;

---------------------------------------------------------------------
-- DEPENDENCIES: edges in the definition graph
---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS dep (
  from_id   INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  to_id     INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  edge      TEXT    NOT NULL CHECK(edge IN (
              'calls','uses_type','implements','field_of','variant_of'
            )),
  PRIMARY KEY (from_id, to_id, edge)
);

CREATE INDEX IF NOT EXISTS idx_dep_to ON dep(to_id);

---------------------------------------------------------------------
-- EXTERN: foreign function declarations (C ABI)
---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS extern_ (
  id        INTEGER PRIMARY KEY,
  name      TEXT    NOT NULL,
  symbol    TEXT    NOT NULL,
  lib       TEXT,
  sig       TEXT    NOT NULL,
  conv      TEXT    NOT NULL DEFAULT 'C' CHECK(conv IN ('C','system','rust')),
  UNIQUE(name)
);

---------------------------------------------------------------------
-- TAGS: arbitrary metadata on definitions
---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS tag (
  def_id    INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  key       TEXT    NOT NULL,
  val       TEXT,
  PRIMARY KEY (def_id, key)
);

CREATE INDEX IF NOT EXISTS idx_tag_key_val ON tag(key, val);

---------------------------------------------------------------------
-- MODULES: logical grouping
---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS module (
  id        INTEGER PRIMARY KEY,
  name      TEXT    NOT NULL UNIQUE,
  doc       TEXT
);

CREATE TABLE IF NOT EXISTS module_member (
  module_id INTEGER NOT NULL REFERENCES module(id) ON DELETE CASCADE,
  def_id    INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  exported  INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY (module_id, def_id)
);

---------------------------------------------------------------------
-- COMPILATION CACHE
---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS compiled (
  def_id    INTEGER PRIMARY KEY REFERENCES def(id) ON DELETE CASCADE,
  ir        BLOB    NOT NULL,
  target    TEXT    NOT NULL,
  hash      BLOB    NOT NULL
);

---------------------------------------------------------------------
-- VIEWS
---------------------------------------------------------------------

-- All dirty definitions and their transitive dependents
CREATE VIEW IF NOT EXISTS v_dirty AS
  WITH RECURSIVE dirty(id) AS (
    SELECT id FROM def WHERE compiled = 0
    UNION
    SELECT d.from_id FROM dep d JOIN dirty ON d.to_id = dirty.id
  )
  SELECT DISTINCT def.* FROM def JOIN dirty ON def.id = dirty.id;

-- Token budget: definitions sorted by dependency depth
CREATE VIEW IF NOT EXISTS v_context AS
  SELECT d.*, COUNT(dep.to_id) as dep_count
  FROM def d
  LEFT JOIN dep ON d.id = dep.from_id
  GROUP BY d.id
  ORDER BY dep_count ASC, d.tokens ASC;

-- Full call graph
CREATE VIEW IF NOT EXISTS v_callgraph AS
  SELECT
    f.name  AS caller,
    t.name  AS callee,
    d.edge
  FROM dep d
  JOIN def f ON d.from_id = f.id
  JOIN def t ON d.to_id   = t.id;

---------------------------------------------------------------------
-- DEFINITION HISTORY: automatic change tracking
---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS def_history (
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
END;

"#;

/// Triggers that use custom SQL functions (glyph_hash, glyph_tokens).
/// Created as TEMP triggers so they exist only during the Rust compiler's
/// connection and are never persisted to disk. This allows databases created
/// by the Rust compiler to be used freely with the self-hosted `./glyph` CLI
/// and plain `sqlite3` without trigger errors.
pub const TRIGGER_SQL: &str = r#"
CREATE TEMP TRIGGER trg_def_dirty AFTER UPDATE OF body, sig, kind ON def
BEGIN
  UPDATE def SET
    compiled = 0,
    hash = glyph_hash(NEW.kind, NEW.sig, NEW.body),
    tokens = glyph_tokens(NEW.body),
    modified = datetime('now')
  WHERE id = NEW.id;
END;

CREATE TEMP TRIGGER trg_dep_dirty AFTER UPDATE OF compiled ON def
  WHEN NEW.compiled = 0
BEGIN
  UPDATE def SET compiled = 0
  WHERE id IN (SELECT from_id FROM dep WHERE to_id = NEW.id);
END;
"#;
