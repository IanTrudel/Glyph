-- Glyph database schema

CREATE TABLE compiled (
  def_id    INTEGER PRIMARY KEY REFERENCES def(id) ON DELETE CASCADE,
  ir        BLOB    NOT NULL,
  target    TEXT    NOT NULL,
  hash      BLOB    NOT NULL
);

CREATE TABLE def (
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
  created   TEXT    NOT NULL DEFAULT (datetime('now')),
  modified  TEXT    NOT NULL DEFAULT (datetime('now'))
, gen INTEGER NOT NULL DEFAULT 1);

CREATE TABLE def_history (
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

CREATE TABLE dep (
  from_id   INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  to_id     INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  edge      TEXT    NOT NULL CHECK(edge IN (
              'calls','uses_type','implements','field_of','variant_of'
            )),
  PRIMARY KEY (from_id, to_id, edge)
);

CREATE TABLE extern_ (
  id        INTEGER PRIMARY KEY,
  name      TEXT    NOT NULL,
  symbol    TEXT    NOT NULL,
  lib       TEXT,
  sig       TEXT    NOT NULL,
  conv      TEXT    NOT NULL DEFAULT 'C' CHECK(conv IN ('C','system','rust')),
  UNIQUE(name)
);

CREATE TABLE module (
  id        INTEGER PRIMARY KEY,
  name      TEXT    NOT NULL UNIQUE,
  doc       TEXT
);

CREATE TABLE module_member (
  module_id INTEGER NOT NULL REFERENCES module(id) ON DELETE CASCADE,
  def_id    INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  exported  INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY (module_id, def_id)
);

CREATE TABLE tag (
  def_id    INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE,
  key       TEXT    NOT NULL,
  val       TEXT,
  PRIMARY KEY (def_id, key)
);

CREATE INDEX idx_def_compiled ON def(compiled) WHERE compiled = 0;

CREATE INDEX idx_def_gen ON def(gen);

CREATE INDEX idx_def_kind ON def(kind);

CREATE UNIQUE INDEX idx_def_name_kind_gen ON def(name, kind, gen);

CREATE INDEX idx_dep_to ON dep(to_id);

CREATE INDEX idx_history_name ON def_history(name, kind);

CREATE INDEX idx_tag_key_val ON tag(key, val);

CREATE TRIGGER trg_def_history_delete BEFORE DELETE ON def
                 BEGIN
                   INSERT INTO def_history (def_id, name, kind, sig, body, hash, tokens, gen)
                   VALUES (OLD.id, OLD.name, OLD.kind, OLD.sig, OLD.body, OLD.hash, OLD.tokens, OLD.gen);
                 END;

CREATE TRIGGER trg_def_history_update BEFORE UPDATE OF body ON def
                 BEGIN
                   INSERT INTO def_history (def_id, name, kind, sig, body, hash, tokens, gen)
                   VALUES (OLD.id, OLD.name, OLD.kind, OLD.sig, OLD.body, OLD.hash, OLD.tokens, OLD.gen);
                 END;


-- extern declarations
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('eprintln','glyph_eprintln',NULL,'S -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_args','glyph_args',NULL,'[S]');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_array_len','glyph_array_len',NULL,'[I] -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_array_pop','glyph_array_pop',NULL,'[I] -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_array_push','glyph_array_push',NULL,'[I] -> I -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_array_set','glyph_array_set',NULL,'[I] -> I -> I -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_db_close','glyph_db_close','sqlite3','I -> V');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_db_exec','glyph_db_exec','sqlite3','I -> S -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_db_open','glyph_db_open','sqlite3','S -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_db_query_one','glyph_db_query_one','sqlite3','I -> S -> S');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_db_query_rows','glyph_db_query_rows','sqlite3','I -> S -> [[S]]');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_exit','glyph_exit',NULL,'I -> V');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_int_to_str','glyph_int_to_str',NULL,'I -> S');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_read_file','glyph_read_file',NULL,'S -> S');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_str_char_at','glyph_str_char_at',NULL,'S -> I -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_str_concat','glyph_str_concat',NULL,'S -> S -> S');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_str_eq','glyph_str_eq',NULL,'S -> S -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_str_len','glyph_str_len',NULL,'S -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_str_slice','glyph_str_slice',NULL,'S -> I -> I -> S');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_str_to_int','glyph_str_to_int',NULL,'S -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_system','glyph_system',NULL,'S -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('glyph_write_file','glyph_write_file',NULL,'S -> S -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('println','glyph_println',NULL,'S -> I');
INSERT OR IGNORE INTO extern_ (name,symbol,lib,sig) VALUES ('raw_set','glyph_raw_set',NULL,'I -> I -> I -> I');

-- module declarations

-- meta
