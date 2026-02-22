# Spec: `glint` — Glyph Project Analyzer

## Objective

Build a command-line tool that reads a `.glyph` database and produces a structured analysis report. This is a real tool for Glyph developers — it tells you what's in a project, what's large, and what's dead code.

The program should be implemented in Glyph itself, in `examples/glint/`. This tests whether the language can build useful tools that operate on its own program format.

## Interface

```
./glint <path.glyph>
./glint <path.glyph> --calls <name>
```

No arguments or bad arguments: print usage and exit.

## Output: Default Report

Three sections, separated by blank lines. Exact format matters — the output should be copy-pasteable into documentation.

### Section 1: Summary

```
=== Summary ===
Functions:  18
Types:      0
Tests:      5
Externs:    2
Total defs: 25
Total lines: 139
Avg lines/fn: 7
```

- "Lines" = newline count + 1 in each definition's body text.
- "Avg lines/fn" = total lines across fn defs only, integer division by fn count.
- Right-align the numbers for readability.

### Section 2: Largest Definitions

```
=== Largest Definitions ===
  1. parse_term_loop     21 lines  fn
  2. parse_factor        18 lines  fn
  3. parse_expr_loop     16 lines  fn
  4. repl_loop            9 lines  fn
  5. main                 8 lines  fn
```

- Top 5 definitions by line count, across ALL kinds (fn, test, type).
- Include the kind on each line.
- Rank number, name, line count, kind.

### Section 3: Orphan Detection

```
=== Orphans (not referenced by other definitions) ===
  main
  repl_loop
  test_parse_num
  test_simple_add
  ...
N orphans found
```

A definition is an "orphan" if its name does not appear as a substring in ANY other definition's body. `main` will always be an orphan (nothing calls it). Test definitions are typically orphans (called by the test framework, not by other code). The interesting orphans are fn definitions that nothing calls — potential dead code.

This requires implementing substring search: given a name and a body string, determine if the name appears anywhere in the body. Use `str_char_at` and `str_len` from the runtime — there is no built-in `str_contains`.

## Output: `--calls` Mode

```
./glint calc.glyph --calls skip_ws
```

```
=== Callers of skip_ws ===
  parse_factor
  parse_expr_loop
  parse_term_loop
3 callers found
```

List every definition whose body contains the given name as a substring. This is the reverse of orphan detection — find all bodies that mention a specific name.

## Verification

Run on `examples/calculator/calc.glyph` (the expression calculator REPL):

**Expected summary:**
- Functions: 18, Types: 0, Tests: 5, Externs: 2
- Total defs: 25, Total lines: 139
- Avg lines/fn: 7

**Expected top 5:**
1. parse_term_loop (21 lines)
2. parse_factor (18 lines)
3. parse_expr_loop (16 lines)
4. repl_loop (9 lines)
5. main or parse_num_loop (8 lines, tie)

**Expected orphan examples:**
- `main` (entry point, never called by another def)
- `repl_loop` (called only by main — wait, main's body contains "repl_loop", so repl_loop is NOT an orphan. main IS an orphan because no other body mentions "main")

Run on `examples/life/life.glyph` (Game of Life) as a second verification — it has 23 fn definitions with different structure.

## Constraints

- Do NOT hardcode anything about calculator or life. The tool must work on arbitrary `.glyph` databases.
- The SQLite runtime functions (`glyph_db_open`, `glyph_db_query_rows`, `glyph_db_close`) are built into the Glyph runtime. You may need to query `def` and `extern_` tables.
- String processing (line counting, substring search) must be implemented from scratch using `str_char_at`, `str_len`, and `str_slice`. There is no `str_contains` or `str_split` in the runtime.
- Target: roughly 20-30 definitions. This is a focused tool, not a framework.

## What This Tests

If a fresh LLM session can read the Glyph skill documentation and build this tool from scratch:

1. **Workflow**: Can it use `glyph init`, `glyph put -f`, `glyph build` correctly?
2. **String processing**: Can it implement substring search and line counting without built-in helpers?
3. **Database access**: Can it query SQLite tables and process the results?
4. **Recursion**: Can it iterate over arrays and strings using recursive functions?
5. **Output formatting**: Can it produce aligned, readable text output using string concatenation?
6. **Debugging**: Can it fix the inevitable issues (wrong string escaping, off-by-one errors, match expression mistakes) without good error messages?

If all of this works smoothly in one session, Glyph's LLM-native design is validated. If the LLM gets stuck on string escaping, can't figure out the `glyph_` prefix for SQLite functions, or produces code that segfaults with no useful diagnostics, those are real problems to fix.
