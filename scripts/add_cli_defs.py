#!/usr/bin/env python3
"""Insert CLI command definitions into compiler.glyph.

Uses Python's sqlite3 parameterized queries to avoid escaping issues.
All new definitions use INSERT OR REPLACE with zeroblob(32) hash and 0 tokens.
The Rust compiler recomputes these during --full build.
"""
import sqlite3
import sys
import os

DB_PATH = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "..", "home/ian/Workspace/Experimental/Glyph/compiler.glyph"
)
DB_PATH = os.environ.get("GLYPH_DB", DB_PATH)
if len(sys.argv) > 1:
    DB_PATH = sys.argv[1]
else:
    DB_PATH = "/home/ian/Workspace/Experimental/Glyph/compiler.glyph"

conn = sqlite3.connect(DB_PATH)

# Drop triggers first (they reference custom functions not available in plain sqlite3)
conn.execute("DROP TRIGGER IF EXISTS trg_def_dirty")
conn.execute("DROP TRIGGER IF EXISTS trg_dep_dirty")

def put(name, body, kind="fn"):
    """Insert or replace a definition."""
    conn.execute(
        "INSERT OR REPLACE INTO def (name, kind, body, hash, tokens, compiled) "
        "VALUES (?, ?, ?, zeroblob(32), 0, 0)",
        (name, kind, body)
    )

# ═══════════════════════════════════════════════════════════════════
# HELPERS
# ═══════════════════════════════════════════════════════════════════

put("sql_escape", """\
sql_escape s =
  len = glyph_str_len(s)
  sb = glyph_sb_new()
  sql_esc_loop(sb, s, 0, len)""")

put("sql_esc_loop", """\
sql_esc_loop sb s i len =
  match i < len
    true ->
      c = glyph_str_char_at(s, i)
      match c == 39
        true ->
          sb2 = glyph_sb_append(sb, "''")
          sql_esc_loop(sb2, s, i + 1, len)
        _ ->
          ch = glyph_str_slice(s, i, i + 1)
          sb2 = glyph_sb_append(sb, ch)
          sql_esc_loop(sb2, s, i + 1, len)
    _ -> glyph_sb_build(sb)""")

put("extract_name", """\
extract_name body =
  len = glyph_str_len(body)
  extract_name_loop(body, 0, len)""")

put("extract_name_loop", """\
extract_name_loop body i len =
  match i < len
    true ->
      c = glyph_str_char_at(body, i)
      match c == 32
        true -> glyph_str_slice(body, 0, i)
        _ ->
          match c == 10
            true -> glyph_str_slice(body, 0, i)
            _ ->
              match c == 9
                true -> glyph_str_slice(body, 0, i)
                _ ->
                  match c == 61
                    true ->
                      match i > 0
                        true -> glyph_str_slice(body, 0, i)
                        _ -> extract_name_loop(body, i + 1, len)
                    _ -> extract_name_loop(body, i + 1, len)
    _ -> body""")

put("find_flag", """\
find_flag argv flag start =
  argc = glyph_array_len(argv)
  match start < argc
    true ->
      match glyph_str_eq(argv[start], flag) == 1
        true ->
          match start + 1 < argc
            true -> argv[start + 1]
            _ -> ""
        _ -> find_flag(argv, flag, start + 1)
    _ -> ""
""")

put("has_flag", """\
has_flag argv flag start =
  argc = glyph_array_len(argv)
  match start < argc
    true ->
      match glyph_str_eq(argv[start], flag) == 1
        true -> 1
        _ -> has_flag(argv, flag, start + 1)
    _ -> 0""")

put("print_usage", """\
print_usage u =
  eprintln("Usage: glyph <command> <db> [args...]")
  eprintln("")
  eprintln("Commands:")
  eprintln("  init <db>                     Create new database")
  eprintln("  build <db> [output]           Compile definitions")
  eprintln("  run <db>                      Build and execute")
  eprintln("  get <db> <name> [--kind K]    Read definition body")
  eprintln("  put <db> <kind> -b <body>     Create/update definition")
  eprintln("  put <db> <kind> -f <file>     Create/update from file")
  eprintln("  rm <db> <name> [--force]      Remove definition")
  eprintln("  ls <db> [--kind K] [--sort S] List definitions")
  eprintln("  find <db> <pat> [--body]      Search definitions")
  eprintln("  deps <db> <name>              Forward dependencies")
  eprintln("  rdeps <db> <name>             Reverse dependencies")
  eprintln("  stat <db>                     Image overview")
  eprintln("  dump <db> [--budget N]        Token-budgeted context")
  eprintln("  sql <db> <query>              Execute raw SQL")
  eprintln("  extern <db> <n> <sym> <sig>   Add extern declaration")
  1""")

# ═══════════════════════════════════════════════════════════════════
# DISPATCH
# ═══════════════════════════════════════════════════════════════════

put("dispatch_cmd", """\
dispatch_cmd argv argc cmd =
  match glyph_str_eq(cmd, "build") == 1
    true -> cmd_build(argv, argc)
    _ ->
      match glyph_str_eq(cmd, "run") == 1
        true -> cmd_run(argv, argc)
        _ ->
          match glyph_str_eq(cmd, "get") == 1
            true -> cmd_get(argv, argc)
            _ ->
              match glyph_str_eq(cmd, "put") == 1
                true -> cmd_put(argv, argc)
                _ -> dispatch_cmd2(argv, argc, cmd)""")

put("dispatch_cmd2", """\
dispatch_cmd2 argv argc cmd =
  match glyph_str_eq(cmd, "rm") == 1
    true -> cmd_rm(argv, argc)
    _ ->
      match glyph_str_eq(cmd, "ls") == 1
        true -> cmd_ls(argv, argc)
        _ ->
          match glyph_str_eq(cmd, "find") == 1
            true -> cmd_find(argv, argc)
            _ ->
              match glyph_str_eq(cmd, "stat") == 1
                true -> cmd_stat(argv, argc)
                _ -> dispatch_cmd3(argv, argc, cmd)""")

put("dispatch_cmd3", """\
dispatch_cmd3 argv argc cmd =
  match glyph_str_eq(cmd, "deps") == 1
    true -> cmd_deps(argv, argc)
    _ ->
      match glyph_str_eq(cmd, "rdeps") == 1
        true -> cmd_rdeps(argv, argc)
        _ ->
          match glyph_str_eq(cmd, "dump") == 1
            true -> cmd_dump(argv, argc)
            _ ->
              match glyph_str_eq(cmd, "sql") == 1
                true -> cmd_sql(argv, argc)
                _ -> dispatch_cmd4(argv, argc, cmd)""")

put("dispatch_cmd4", """\
dispatch_cmd4 argv argc cmd =
  match glyph_str_eq(cmd, "extern") == 1
    true -> cmd_extern_add(argv, argc)
    _ ->
      match glyph_str_eq(cmd, "init") == 1
        true -> cmd_init(argv, argc)
        _ ->
          match glyph_str_eq(cmd, "check") == 1
            true -> cmd_check(argv, argc)
            _ -> print_usage(0)""")

# ═══════════════════════════════════════════════════════════════════
# PHASE 1: CRUD COMMANDS
# ═══════════════════════════════════════════════════════════════════

put("cmd_get", """\
cmd_get argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph get <db> <name> [--kind K]")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      db = glyph_db_open(db_path)
      kind = find_flag(argv, "--kind", 4)
      sql = match glyph_str_len(kind) > 0
        true -> s7("SELECT body FROM def WHERE name = '", sql_escape(name), "' AND kind = '", sql_escape(kind), "'", "", "")
        _ -> s3("SELECT body FROM def WHERE name = '", sql_escape(name), "'")
      rows = glyph_db_query_rows(db, sql)
      glyph_db_close(db)
      n = glyph_array_len(rows)
      match n == 0
        true ->
          eprintln(s3("error: '", name, "' not found"))
          1
        _ ->
          match n > 1
            true ->
              eprintln(s5("error: ", itos(n), " defs named '", name, "'. Use --kind"))
              6
            _ ->
              row = rows[0]
              println(row[0])
              0""")

put("cmd_put", """\
cmd_put argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph put <db> <kind> -b <body>")
      eprintln("       glyph put <db> <kind> -f <file>")
      1
    _ ->
      db_path = argv[2]
      kind = argv[3]
      body_flag = find_flag(argv, "-b", 4)
      file_flag = find_flag(argv, "-f", 4)
      body = match glyph_str_len(body_flag) > 0
        true -> body_flag
        _ ->
          match glyph_str_len(file_flag) > 0
            true -> glyph_read_file(file_flag)
            _ -> ""
      match glyph_str_len(body) == 0
        true ->
          eprintln("error: body required via -b or -f")
          3
        _ ->
          name = extract_name(body)
          do_put(db_path, kind, name, body)""")

put("do_put", """\
do_put db_path kind name body =
  db = glyph_db_open(db_path)
  check_sql = s5("SELECT id, tokens FROM def WHERE name = '", sql_escape(name), "' AND kind = '", sql_escape(kind), "'")
  existing = glyph_db_query_rows(db, check_sql)
  n = glyph_array_len(existing)
  old_tokens = match n > 0
    true ->
      old = existing[0]
      old_id = old[0]
      glyph_db_exec(db, s3("DELETE FROM def WHERE id = ", old_id, ""))
      old[1]
    _ -> ""
  esc_body = sql_escape(body)
  ins_sql = s7("INSERT INTO def (name, kind, body, hash, tokens) VALUES ('", sql_escape(name), "', '", sql_escape(kind), "', '", esc_body, "', zeroblob(32), 0)")
  glyph_db_exec(db, ins_sql)
  glyph_db_close(db)
  match glyph_str_len(old_tokens) > 0
    true ->
      match glyph_str_eq(old_tokens, "0") == 1
        true -> println(s3(kind, " ", name))
        _ -> println(s3(kind, " ", name))
    _ -> println(s3(kind, " ", name))
  0""")

put("cmd_rm", """\
cmd_rm argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph rm <db> <name> [--kind K] [--force]")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      db = glyph_db_open(db_path)
      kind = find_flag(argv, "--kind", 4)
      force = has_flag(argv, "--force", 4)
      sql = match glyph_str_len(kind) > 0
        true -> s7("SELECT id, kind FROM def WHERE name = '", sql_escape(name), "' AND kind = '", sql_escape(kind), "'", "", "")
        _ -> s3("SELECT id, kind FROM def WHERE name = '", sql_escape(name), "'")
      rows = glyph_db_query_rows(db, sql)
      n = glyph_array_len(rows)
      match n == 0
        true ->
          glyph_db_close(db)
          eprintln(s3("error: '", name, "' not found"))
          1
        _ ->
          match n > 1
            true ->
              glyph_db_close(db)
              eprintln(s3("error: ambiguous '", name, "'. Use --kind"))
              6
            _ ->
              row = rows[0]
              def_id = row[0]
              def_kind = row[1]
              rm_check_rdeps(db, def_id, def_kind, name, force)""")

put("rm_check_rdeps", """\
rm_check_rdeps db def_id def_kind name force =
  rdeps = glyph_db_query_rows(db, s3("SELECT from_id FROM dep WHERE to_id = ", def_id, ""))
  rdep_n = glyph_array_len(rdeps)
  match rdep_n > 0
    true ->
      match force == 1
        true ->
          glyph_db_exec(db, s3("DELETE FROM def WHERE id = ", def_id, ""))
          glyph_db_close(db)
          println(s4("removed ", def_kind, " ", name))
          0
        _ ->
          glyph_db_close(db)
          eprintln(s5("error: ", name, " has ", itos(rdep_n), " rdeps. Use --force"))
          1
    _ ->
      glyph_db_exec(db, s3("DELETE FROM def WHERE id = ", def_id, ""))
      glyph_db_close(db)
      println(s4("removed ", def_kind, " ", name))
      0""")

put("cmd_ls", """\
cmd_ls argv argc =
  match argc < 3
    true ->
      eprintln("Usage: glyph ls <db> [--kind K] [--sort S]")
      1
    _ ->
      db_path = argv[2]
      db = glyph_db_open(db_path)
      kind = find_flag(argv, "--kind", 3)
      sort = find_flag(argv, "--sort", 3)
      base = match glyph_str_len(kind) > 0
        true -> s3("SELECT kind, name, COALESCE(sig,'-'), tokens FROM def WHERE kind = '", sql_escape(kind), "'")
        _ -> "SELECT kind, name, COALESCE(sig,'-'), tokens FROM def"
      order = match glyph_str_eq(sort, "tokens") == 1
        true -> " ORDER BY CAST(tokens AS INTEGER)"
        _ ->
          match glyph_str_eq(sort, "name") == 1
            true -> " ORDER BY name"
            _ -> " ORDER BY kind, name"
      rows = glyph_db_query_rows(db, s2(base, order))
      glyph_db_close(db)
      print_ls_rows(rows, 0, glyph_array_len(rows))
      0""")

put("print_ls_rows", """\
print_ls_rows rows i n =
  match i < n
    true ->
      row = rows[i]
      line = s7(row[0], "\t", row[1], "\t", row[2], "\t", s2(row[3], "tok"))
      println(line)
      print_ls_rows(rows, i + 1, n)
    _ -> 0""")

# ═══════════════════════════════════════════════════════════════════
# PHASE 2: NAVIGATION COMMANDS
# ═══════════════════════════════════════════════════════════════════

put("cmd_find", """\
cmd_find argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph find <db> <pattern> [--body]")
      1
    _ ->
      db_path = argv[2]
      pat = argv[3]
      search_body = has_flag(argv, "--body", 4)
      db = glyph_db_open(db_path)
      esc_pat = sql_escape(pat)
      sql = match search_body == 1
        true -> s5("SELECT kind, name, COALESCE(sig,'-'), tokens FROM def WHERE name LIKE '%", esc_pat, "%' OR body LIKE '%", esc_pat, "%' ORDER BY kind, name")
        _ -> s3("SELECT kind, name, COALESCE(sig,'-'), tokens FROM def WHERE name LIKE '%", esc_pat, "%' ORDER BY kind, name")
      rows = glyph_db_query_rows(db, sql)
      glyph_db_close(db)
      print_ls_rows(rows, 0, glyph_array_len(rows))
      0""")

put("cmd_deps", """\
cmd_deps argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph deps <db> <name>")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      db = glyph_db_open(db_path)
      sql = s3("SELECT DISTINCT t.name FROM dep d JOIN def f ON d.from_id = f.id JOIN def t ON d.to_id = t.id WHERE f.name = '", sql_escape(name), "' ORDER BY t.name")
      rows = glyph_db_query_rows(db, sql)
      glyph_db_close(db)
      n = glyph_array_len(rows)
      match n == 0
        true ->
          println(s2(name, " -> (none)"))
          0
        _ ->
          deps_str = join_names(rows, 0, n, "")
          println(s3(name, " -> ", deps_str))
          0""")

put("join_names", """\
join_names rows i n acc =
  match i < n
    true ->
      row = rows[i]
      new_acc = match glyph_str_len(acc) == 0
        true -> row[0]
        _ -> s3(acc, ", ", row[0])
      join_names(rows, i + 1, n, new_acc)
    _ -> acc""")

put("cmd_rdeps", """\
cmd_rdeps argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph rdeps <db> <name>")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      db = glyph_db_open(db_path)
      sql = s3("SELECT DISTINCT f.name FROM dep d JOIN def f ON d.from_id = f.id JOIN def t ON d.to_id = t.id WHERE t.name = '", sql_escape(name), "' ORDER BY f.name")
      rows = glyph_db_query_rows(db, sql)
      glyph_db_close(db)
      n = glyph_array_len(rows)
      names = join_names(rows, 0, n, "")
      match n == 0
        true ->
          println("(none)")
          0
        _ ->
          println(names)
          0""")

put("cmd_stat", """\
cmd_stat argv argc =
  match argc < 3
    true ->
      eprintln("Usage: glyph stat <db>")
      1
    _ ->
      db_path = argv[2]
      db = glyph_db_open(db_path)
      kind_rows = glyph_db_query_rows(db, "SELECT kind, COUNT(*) FROM def GROUP BY kind ORDER BY COUNT(*) DESC")
      total_row = glyph_db_query_one(db, "SELECT COUNT(*) FROM def")
      token_row = glyph_db_query_one(db, "SELECT COALESCE(SUM(tokens),0) FROM def")
      dirty_row = glyph_db_query_one(db, "SELECT COUNT(*) FROM def WHERE compiled = 0")
      ext_row = glyph_db_query_one(db, "SELECT COUNT(*) FROM extern_")
      glyph_db_close(db)
      kinds_str = format_kind_counts(kind_rows, 0, glyph_array_len(kind_rows), "")
      line = s7(total_row, " defs (", kinds_str, ")  ", ext_row, " extern  ", s4(token_row, "tok total  ", dirty_row, " dirty"))
      println(line)
      0""")

put("format_kind_counts", """\
format_kind_counts rows i n acc =
  match i < n
    true ->
      row = rows[i]
      entry = s3(row[1], " ", row[0])
      new_acc = match glyph_str_len(acc) == 0
        true -> entry
        _ -> s3(acc, ", ", entry)
      format_kind_counts(rows, i + 1, n, new_acc)
    _ -> acc""")

put("cmd_dump", """\
cmd_dump argv argc =
  match argc < 3
    true ->
      eprintln("Usage: glyph dump <db> [--budget N] [--root NAME] [--all] [--sigs]")
      1
    _ ->
      db_path = argv[2]
      db = glyph_db_open(db_path)
      budget_str = find_flag(argv, "--budget", 3)
      root = find_flag(argv, "--root", 3)
      do_all = has_flag(argv, "--all", 3)
      sigs_only = has_flag(argv, "--sigs", 3)
      root_name = match glyph_str_len(root) > 0
        true -> root
        _ -> "main"
      match do_all == 1
        true -> dump_all(db, sigs_only)
        _ ->
          budget = match glyph_str_len(budget_str) > 0
            true -> glyph_str_to_int(budget_str)
            _ -> 500
          dump_budgeted(db, root_name, budget, sigs_only)""")

put("dump_all", """\
dump_all db sigs_only =
  sql = match sigs_only == 1
    true -> "SELECT kind, name, COALESCE(sig,'-') FROM def ORDER BY kind, name"
    _ -> "SELECT kind, name, body FROM def ORDER BY kind, name"
  rows = glyph_db_query_rows(db, sql)
  glyph_db_close(db)
  dump_print_all(rows, 0, glyph_array_len(rows), sigs_only)
  0""")

put("dump_print_all", """\
dump_print_all rows i n sigs_only =
  match i < n
    true ->
      row = rows[i]
      match sigs_only == 1
        true -> println(s5("-- ", row[0], " ", row[1], s2(" : ", row[2])))
        _ ->
          println(s4("-- ", row[0], " ", row[1]))
          println(row[2])
          println("")
      dump_print_all(rows, i + 1, n, sigs_only)
    _ -> 0""")

put("dump_budgeted", """\
dump_budgeted db root_name budget sigs_only =
  root_rows = glyph_db_query_rows(db, s3("SELECT id FROM def WHERE name = '", sql_escape(root_name), "'"))
  match glyph_array_len(root_rows) == 0
    true ->
      glyph_db_close(db)
      eprintln(s3("error: root '", root_name, "' not found"))
      1
    _ ->
      all_rows = glyph_db_query_rows(db, "SELECT kind, name, body, tokens FROM def ORDER BY kind, name")
      glyph_db_close(db)
      total = glyph_array_len(all_rows)
      println(s7("-- [", itos(budget), "tok budget from ", root_name, ", ", itos(total), " defs total]"))
      dump_within_budget(all_rows, 0, total, budget, 0, sigs_only)
      0""")

put("dump_within_budget", """\
dump_within_budget rows i n budget used sigs_only =
  match i < n
    true ->
      row = rows[i]
      tok = glyph_str_to_int(row[3])
      match used + tok <= budget
        true ->
          match sigs_only == 1
            true -> println(s5("-- ", row[0], " ", row[1], ""))
            _ -> println(row[2])
          println("")
          dump_within_budget(rows, i + 1, n, budget, used + tok, sigs_only)
        _ ->
          println(s5("-- over budget: ", row[0], " ", row[1], ""))
          dump_within_budget(rows, i + 1, n, budget, used, sigs_only)
    _ ->
      println(s3("-- ", itos(used), "tok used"))
      0""")

# ═══════════════════════════════════════════════════════════════════
# PHASE 3: WORKFLOW COMMANDS
# ═══════════════════════════════════════════════════════════════════

put("cmd_sql", """\
cmd_sql argv argc =
  match argc < 4
    true ->
      eprintln("Usage: glyph sql <db> <query>")
      1
    _ ->
      db_path = argv[2]
      query = argv[3]
      db = glyph_db_open(db_path)
      first6 = match glyph_str_len(query) >= 6
        true -> glyph_str_slice(query, 0, 6)
        _ -> ""
      is_select = match glyph_str_eq(first6, "SELECT") == 1
        true -> 1
        _ ->
          match glyph_str_eq(first6, "select") == 1
            true -> 1
            _ ->
              match glyph_str_eq(first6, "Select") == 1
                true -> 1
                _ -> 0
      match is_select == 1
        true ->
          rows = glyph_db_query_rows(db, query)
          glyph_db_close(db)
          print_sql_rows(rows, 0, glyph_array_len(rows))
          0
        _ ->
          rc = glyph_db_exec(db, query)
          glyph_db_close(db)
          match rc == 0
            true -> 0
            _ ->
              eprintln("error: SQL execution failed")
              5""")

put("print_sql_rows", """\
print_sql_rows rows i n =
  match i < n
    true ->
      row = rows[i]
      cols = glyph_array_len(row)
      line = join_cols(row, 0, cols, "")
      println(line)
      print_sql_rows(rows, i + 1, n)
    _ -> 0""")

put("join_cols", """\
join_cols row i n acc =
  match i < n
    true ->
      new_acc = match glyph_str_len(acc) == 0
        true -> row[i]
        _ -> s3(acc, "\t", row[i])
      join_cols(row, i + 1, n, new_acc)
    _ -> acc""")

put("cmd_extern_add", """\
cmd_extern_add argv argc =
  match argc < 6
    true ->
      eprintln("Usage: glyph extern <db> <name> <symbol> <sig> [--lib L]")
      1
    _ ->
      db_path = argv[2]
      name = argv[3]
      symbol = argv[4]
      sig = argv[5]
      lib = find_flag(argv, "--lib", 6)
      db = glyph_db_open(db_path)
      lib_val = match glyph_str_len(lib) > 0
        true -> s3("'", sql_escape(lib), "'")
        _ -> "NULL"
      sql = s7("INSERT OR REPLACE INTO extern_ (name, symbol, lib, sig, conv) VALUES ('", sql_escape(name), "', '", sql_escape(symbol), "', ", lib_val, s3(", '", sql_escape(sig), "', 'C')"))
      glyph_db_exec(db, sql)
      glyph_db_close(db)
      match glyph_str_len(lib) > 0
        true -> println(s5("extern ", name, " (", s3(lib, ":", symbol), ")"))
        _ -> println(s3("extern ", name, s2(" (", s2(symbol, ")"))))
      0""")

put("cmd_init", """\
cmd_init argv argc =
  match argc < 3
    true ->
      eprintln("Usage: glyph init <db>")
      1
    _ ->
      db = glyph_db_open(argv[2])
      glyph_db_exec(db, init_schema())
      glyph_db_close(db)
      println(s2("Created ", argv[2]))
      0""")

# init_schema is a big string constant with the full schema minus triggers
put("init_schema", r"""init_schema = "PRAGMA journal_mode = WAL; PRAGMA foreign_keys = ON; CREATE TABLE IF NOT EXISTS def (id INTEGER PRIMARY KEY, name TEXT NOT NULL, kind TEXT NOT NULL CHECK(kind IN ('fn','type','trait','impl','const','fsm','srv','macro','test')), sig TEXT, body TEXT NOT NULL, hash BLOB NOT NULL, tokens INTEGER NOT NULL, compiled INTEGER NOT NULL DEFAULT 0, created TEXT NOT NULL DEFAULT (datetime('now')), modified TEXT NOT NULL DEFAULT (datetime('now'))); CREATE UNIQUE INDEX IF NOT EXISTS idx_def_name_kind ON def(name, kind); CREATE INDEX IF NOT EXISTS idx_def_kind ON def(kind); CREATE INDEX IF NOT EXISTS idx_def_compiled ON def(compiled) WHERE compiled = 0; CREATE TABLE IF NOT EXISTS dep (from_id INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE, to_id INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE, edge TEXT NOT NULL CHECK(edge IN ('calls','uses_type','implements','field_of','variant_of')), PRIMARY KEY (from_id, to_id, edge)); CREATE INDEX IF NOT EXISTS idx_dep_to ON dep(to_id); CREATE TABLE IF NOT EXISTS extern_ (id INTEGER PRIMARY KEY, name TEXT NOT NULL, symbol TEXT NOT NULL, lib TEXT, sig TEXT NOT NULL, conv TEXT NOT NULL DEFAULT 'C' CHECK(conv IN ('C','system','rust')), UNIQUE(name)); CREATE TABLE IF NOT EXISTS tag (def_id INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE, key TEXT NOT NULL, val TEXT, PRIMARY KEY (def_id, key)); CREATE INDEX IF NOT EXISTS idx_tag_key_val ON tag(key, val); CREATE TABLE IF NOT EXISTS module (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, doc TEXT); CREATE TABLE IF NOT EXISTS module_member (module_id INTEGER NOT NULL REFERENCES module(id) ON DELETE CASCADE, def_id INTEGER NOT NULL REFERENCES def(id) ON DELETE CASCADE, exported INTEGER NOT NULL DEFAULT 1, PRIMARY KEY (module_id, def_id)); CREATE TABLE IF NOT EXISTS compiled (def_id INTEGER PRIMARY KEY REFERENCES def(id) ON DELETE CASCADE, ir BLOB NOT NULL, target TEXT NOT NULL, hash BLOB NOT NULL); CREATE VIEW IF NOT EXISTS v_dirty AS WITH RECURSIVE dirty(id) AS (SELECT id FROM def WHERE compiled = 0 UNION SELECT d.from_id FROM dep d JOIN dirty ON d.to_id = dirty.id) SELECT DISTINCT def.* FROM def JOIN dirty ON def.id = dirty.id; CREATE VIEW IF NOT EXISTS v_context AS SELECT d.*, COUNT(dep.to_id) as dep_count FROM def d LEFT JOIN dep ON d.id = dep.from_id GROUP BY d.id ORDER BY dep_count ASC, d.tokens ASC; CREATE VIEW IF NOT EXISTS v_callgraph AS SELECT f.name AS caller, t.name AS callee, d.edge FROM dep d JOIN def f ON d.from_id = f.id JOIN def t ON d.to_id = t.id;" """)

put("cmd_build", """\
cmd_build argv argc =
  match argc >= 3
    true ->
      output = match argc >= 4
        true -> argv[3]
        _ -> "a.out"
      compile_db(argv[2], output)
      0
    _ ->
      eprintln("Usage: glyph build <db> [output]")
      1""")

put("cmd_run", """\
cmd_run argv argc =
  match argc >= 3
    true ->
      output = "/tmp/glyph_run_tmp"
      compile_db(argv[2], output)
      glyph_system(output)
    _ ->
      eprintln("Usage: glyph run <db>")
      1""")

put("cmd_check", """\
cmd_check argv argc =
  match argc >= 3
    true ->
      db_path = argv[2]
      db = glyph_db_open(db_path)
      rows = glyph_db_query_rows(db, "SELECT name, body FROM def WHERE kind = 'fn'")
      glyph_db_close(db)
      n = glyph_array_len(rows)
      println(s3("OK ", itos(n), " definitions"))
      0
    _ ->
      eprintln("Usage: glyph check <db>")
      1""")

# ═══════════════════════════════════════════════════════════════════
# UPDATED MAIN
# ═══════════════════════════════════════════════════════════════════

put("main", """\
main =
  argv = glyph_args()
  argc = glyph_array_len(argv)
  match argc < 2
    true -> print_usage(0)
    _ -> dispatch_cmd(argv, argc, argv[1])""")

# ═══════════════════════════════════════════════════════════════════
# RECREATE TRIGGERS
# ═══════════════════════════════════════════════════════════════════

conn.execute("""
CREATE TRIGGER IF NOT EXISTS trg_def_dirty AFTER UPDATE OF body, sig, kind ON def
BEGIN
  UPDATE def SET
    compiled = 0,
    hash = glyph_hash(NEW.kind, NEW.sig, NEW.body),
    tokens = glyph_tokens(NEW.body),
    modified = datetime('now')
  WHERE id = NEW.id;
END
""")

conn.execute("""
CREATE TRIGGER IF NOT EXISTS trg_dep_dirty AFTER UPDATE OF compiled ON def
  WHEN NEW.compiled = 0
BEGIN
  UPDATE def SET compiled = 0
  WHERE id IN (SELECT from_id FROM dep WHERE to_id = NEW.id);
END
""")

conn.commit()
conn.close()

# Count what we inserted
conn2 = sqlite3.connect(DB_PATH)
total = conn2.execute("SELECT COUNT(*) FROM def").fetchone()[0]
new_defs = conn2.execute(
    "SELECT COUNT(*) FROM def WHERE hash = zeroblob(32)"
).fetchone()[0]
conn2.close()

print(f"Done. {new_defs} new/updated defs, {total} total defs in {DB_PATH}")
