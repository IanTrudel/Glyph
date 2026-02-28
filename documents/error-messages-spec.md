# Glyph Error Messages Specification

**Version:** 0.1 (2026-02-25)
**Scope:** Self-hosted compiler only (glyph.glyph). Errors surfaced through the `glyph` CLI and MCP server.

---

## 1. Motivation

Currently, most programming errors in Glyph become segfaults at runtime. The parser can detect many issues on the fly, but errors are silently swallowed — `parse_fn_def` returns `{node: -1, pos: N}` on failure, `compile_fns_parsed` falls back to re-parsing, and `lower_fn_def` crashes on `ast[-1]`.

The MCP `put_def` tool inserts code into the database with zero validation. An LLM can write broken syntax that only surfaces as a segfault minutes later during `glyph build`.

**Goal:** Catch errors early, report them clearly, and surface them through both CLI and MCP before code reaches compilation.

---

## 2. Current State

### What exists
- `parse_fn_def` returns `mk_err(pos)` on parse failure (node = -1)
- `parse_all_fns` checks `fn_idx >= 0` and stores empty name on failure
- `tc_report_errors` prints unresolved name warnings from the type checker
- `cmd_check` runs parse + type check and reports type warnings

### What's missing
- **No error messages from the parser** — just a position index, no description of what went wrong
- **No validation in `put_def`** — CLI or MCP, code goes straight to the database
- **No validation in `build`** — parse failures silently produce `pf_fn_idx = -1`, then `compile_fn` re-parses and `lower_fn_def` segfaults on the bad AST index
- **No source location formatting** — error position is only a token index, not line:column
- **Type checker errors are warnings only** — "type: unresolved name X" with no source context

---

## 3. Error Categories

### 3.1 Lexer Errors (L)

Detected during `tokenize`. Currently cause panics or silent garbage tokens.

| Code | Message | Description |
|------|---------|-------------|
| `L001` | `unterminated string literal` | String opened with `"` but no closing `"` found |
| `L002` | `unterminated string interpolation` | `{` inside string with no matching `}` |
| `L003` | `invalid escape sequence '\X'` | Unrecognized escape like `\q` |
| `L004` | `unexpected character 'X'` | Character that doesn't start any valid token |
| `L005` | `unterminated raw string` | `r"` with no closing `"` |

### 3.2 Parse Errors (P)

Detected during `parse_fn_def`, `parse_expr`, `parse_body`, etc. Currently return `mk_err(pos)` with no message.

| Code | Message | Description |
|------|---------|-------------|
| `P001` | `expected '=' after function parameters` | Missing `=` in `fn_name params = body` |
| `P002` | `expected expression` | Got a token that can't start an expression (e.g., `=`, `)`) |
| `P003` | `expected identifier` | Got a non-identifier where a name was expected |
| `P004` | `expected indented block` | Body after `=` is empty or improperly indented |
| `P005` | `unclosed parenthesis` | `(` without matching `)` |
| `P006` | `unclosed bracket` | `[` without matching `]` |
| `P007` | `unclosed brace` | `{` without matching `}` |
| `P008` | `expected '->' in match arm` | Match pattern not followed by `->` |
| `P009` | `expected expression after '->'` | Match arm has `->` but no body |
| `P010` | `empty match expression` | `match` with no arms |
| `P011` | `empty function body` | Function has `=` but no body expression |
| `P012` | `duplicate parameter name 'X'` | Same parameter name used twice |
| `P013` | `unexpected token 'X'` | General catch-all with the offending token |

### 3.3 Name Resolution Errors (N)

Detected during compilation or type checking. Currently cause segfaults or silent wrong behavior.

| Code | Message | Description |
|------|---------|-------------|
| `N001` | `undefined function 'X'` | Function call to a name not in the database |
| `N002` | `undefined variable 'X'` | Local variable used before binding |
| `N003` | `undefined type 'X'` | Type name not found in type definitions |
| `N004` | `undefined variant 'X'` | Enum variant used in match or construction |

### 3.4 Type Errors (T)

Detected by the type checker (`glyph check`). Currently advisory warnings.

| Code | Message | Description |
|------|---------|-------------|
| `T001` | `type mismatch: expected X, found Y` | Unification failure |
| `T002` | `infinite type: X occurs in Y` | Occurs check failure |
| `T003` | `missing field 'X' in record` | Closed record lacks a required field |
| `T004` | `cannot infer type for 'X'` | Ambiguous type that can't be resolved |
| `T005` | `wrong number of arguments: X takes N, given M` | Arity mismatch on function call |
| `T006` | `operator 'X' not defined for type Y` | e.g., `+` on Bool |

### 3.5 Structural Warnings (W)

Detected by static analysis of valid code. Non-fatal but indicate likely bugs.

| Code | Message | Description |
|------|---------|-------------|
| `W001` | `non-exhaustive match (missing wildcard '_')` | Match without a catch-all arm |
| `W002` | `unreachable match arm` | Arm after a wildcard `_` |
| `W003` | `unused variable 'X'` | Bound variable never referenced |
| `W004` | `shadowed variable 'X'` | Let binding shadows an existing name |
| `W005` | `zero-argument function has side effects` | Function with no params calling print/eprintln (eager eval gotcha) |

---

## 4. Error Format

### 4.1 Structured Error Record

Every error is a record:

```
{level:S, code:S, message:S, def_name:S, line:I, col:I, source_line:S}
```

Fields:
- **level** — `"error"`, `"warning"`, or `"info"`
- **code** — error code like `"P001"`
- **message** — human-readable description
- **def_name** — the definition name where the error occurs
- **line** — 1-based line number within the definition body
- **col** — 1-based column number
- **source_line** — the source line containing the error

### 4.2 CLI Display Format

```
error[P001]: expected '=' after function parameters
 --> factorial:2:10
  |
2 |   match n <= 1
  |          ^ expected '='
```

Components:
- First line: `level[code]: message`
- Location: `def_name:line:col`
- Source context with caret pointing to the error position

### 4.3 MCP JSON Format

Errors returned as a JSON array in the MCP tool response:

```json
{
  "status": "error",
  "errors": [
    {
      "level": "error",
      "code": "P001",
      "message": "expected '=' after function parameters",
      "def_name": "factorial",
      "line": 2,
      "col": 10,
      "source_line": "  match n <= 1"
    }
  ]
}
```

On success with warnings:
```json
{
  "status": "ok",
  "name": "factorial",
  "warnings": [
    {"level": "warning", "code": "W001", "message": "non-exhaustive match", ...}
  ]
}
```

---

## 5. Validation Pipeline

### 5.1 `put_def` Validation (CLI and MCP)

When inserting a definition, validate before writing to the database:

```
validate_def kind body =
  -- 1. Tokenize
  tokens = tokenize(body)
  lex_errors = collect_lex_errors(tokens)
  match array_len(lex_errors) > 0
    true -> lex_errors
    _ ->
      -- 2. Parse
      ast = []
      r = parse_fn_def(body, tokens, 0, ast)
      match is_err(r)
        true -> [mk_parse_error(tokens, r.pos, body)]
        _ ->
          -- 3. Name extraction
          name = ast[r.node].sval
          match str_len(name) == 0
            true -> [mk_error("P003", "could not extract function name", ...)]
            _ -> []  -- success, no errors
```

**CLI `cmd_put`**: Validate, print errors to stderr, refuse to insert on error. Add `--force` flag to skip validation.

**MCP `put_def`**: Validate, return errors in JSON response, refuse to insert on error. Add `"force": true` argument to skip validation.

### 5.2 `build` Validation

During `compile_db` / `build_program`, after `parse_all_fns`:

```
check_parse_results parsed i errors =
  match i >= array_len(parsed)
    true -> errors
    _ ->
      pf = parsed[i]
      match pf.pf_fn_idx < 0
        true ->
          glyph_array_push(errors, mk_parse_error_for(pf.pf_src))
          check_parse_results(parsed, i + 1, errors)
        _ -> check_parse_results(parsed, i + 1, errors)
```

If any definition fails to parse, print all errors and abort before attempting MIR lowering. This prevents the `lower_fn_def(ast, -1, ...)` segfault.

### 5.3 `check` Enhanced Output

Extend `cmd_check` to run the full validation pipeline:

1. Tokenize all definitions — report lexer errors
2. Parse all definitions — report parse errors
3. Type-check all definitions — report type errors and warnings
4. Print summary: `"Checked N definitions: E errors, W warnings"`

---

## 6. Position-to-Location Mapping

The parser works with token positions (indices into the token array). To produce line:col, we need a mapping function:

```
pos_to_linecol src byte_pos =
  -- Count newlines before byte_pos
  count_lines src byte_pos 0 1 1

count_lines src byte_pos i line col =
  match i >= byte_pos
    true -> {line: line, col: col}
    _ ->
      ch = str_char_at(src, i)
      match ch == 10  -- newline
        true -> count_lines(src, byte_pos, i + 1, line + 1, 1)
        _ -> count_lines(src, byte_pos, i + 1, line, col + 1)
```

Extract the source line for context display:

```
extract_source_line src line_num =
  -- Find start of line_num, extract until next newline
  find_line_start src line_num 0 1

find_line_start src target_line i current_line =
  match current_line == target_line
    true -> extract_until_newline(src, i)
    _ ->
      ch = str_char_at(src, i)
      match ch < 0
        true -> ""
        _ -> match ch == 10
          true -> find_line_start(src, target_line, i + 1, current_line + 1)
          _ -> find_line_start(src, target_line, i + 1, current_line)
```

---

## 7. Implementation Plan

### Phase 1: Parse Error Messages

Modify `parse_fn_def`, `parse_body`, `parse_expr`, `parse_atom`, `expect_tok` to carry error messages. Change `mk_err(pos)` to `mk_err_msg(pos, message)`:

```
ParseResult = {node:I, pos:I, error:S}

mk_err_msg pos msg = {node: 0 - 1, pos: pos, error: msg}
mk_err pos = mk_err_msg(pos, "")
```

When `expect_tok` fails:
```
expect_tok_msg tokens pos kind msg =
  match cur_kind(tokens, pos) == kind
    true -> mk_result(0, pos + 1)
    _ -> mk_err_msg(pos, msg)
```

### Phase 2: Build Validation Gate

Add a parse check between `parse_all_fns` and `compile_fns_parsed` in `build_program`:

```
build_program sources externs output_path struct_map mode =
  parsed = parse_all_fns(sources, 0)
  errors = check_parse_results(parsed, 0, [])
  match array_len(errors) > 0
    true ->
      report_errors(errors, 0)
      panic("compilation failed")
    _ ->
      -- continue with compilation...
```

### Phase 3: put_def Validation

Add `validate_and_put` that tokenizes + parses before inserting:

- CLI: validate on `cmd_put`, print errors, exit 1
- MCP: validate on `mcp_tool_put_def`, return errors in JSON

### Phase 4: Position Mapping

Add `pos_to_linecol` and `extract_source_line` for human-readable error locations.

### Phase 5: Type Checker Integration

Enhance `tc_report_errors` to use the structured error format. Eventually integrate type checking into `build` as errors (not just warnings).

---

## 8. MCP Tool Changes

### `put_def` — Add Validation

Current behavior: insert blindly, return `{"status": "ok"}`.

New behavior:
1. Tokenize the body
2. Parse according to kind (`fn` → `parse_fn_def`, `type` → parse type def, `test` → parse test)
3. On error: return `{"status": "error", "errors": [...]}`, do NOT insert
4. On success: insert and return `{"status": "ok", "name": "...", "warnings": [...]}`
5. Optional `"force": true` argument to skip validation

### New tool: `check_def`

Validate a definition without inserting it:

```json
{
  "name": "check_def",
  "description": "Parse and type-check a definition without inserting it",
  "inputSchema": {
    "type": "object",
    "properties": {
      "kind": {"type": "string", "description": "Definition kind (fn, type, test)"},
      "body": {"type": "string", "description": "Definition source code"}
    },
    "required": ["kind", "body"]
  }
}
```

Response:
```json
{
  "valid": true,
  "name": "factorial",
  "warnings": []
}
```

Or:
```json
{
  "valid": false,
  "errors": [
    {"code": "P001", "message": "expected '=' after function parameters", "line": 1, "col": 15}
  ]
}
```

This lets an LLM check its code before committing it to the database.

---

## 9. Examples

### Parse error on put

```
$ ./glyph put db.glyph fn -b "broken x y"
error[P001]: expected '=' after function parameters
 --> broken:1:11
  |
1 | broken x y
  |           ^ expected '='
```

### MCP put_def with error

```json
// Request
{"method": "tools/call", "params": {"name": "put_def", "arguments": {"kind": "fn", "body": "broken x y"}}}

// Response
{"result": {"content": [{"type": "text", "text": "{\"status\":\"error\",\"errors\":[{\"code\":\"P001\",\"message\":\"expected '=' after function parameters\",\"line\":1,\"col\":11}]}"}]}}
```

### MCP check_def

```json
// Request
{"method": "tools/call", "params": {"name": "check_def", "arguments": {"kind": "fn", "body": "add x y = x + y"}}}

// Response
{"result": {"content": [{"type": "text", "text": "{\"valid\":true,\"name\":\"add\",\"warnings\":[]}"}]}}
```

### Build with parse errors

```
$ ./glyph build program.glyph
Compiling 15 definitions...
error[P001]: expected '=' after function parameters
 --> broken:1:11
  |
1 | broken x y
  |           ^ expected '='

error[P002]: expected expression
 --> also_broken:3:5
  |
3 |     = + 42
  |     ^ expected expression

compilation failed: 2 errors
```

### Type check warnings

```
$ ./glyph check program.glyph
Checked 15 definitions: 0 errors, 2 warnings
  warning[T001]: type mismatch: expected S, found I
   --> greet:3:15
  warning[N001]: undefined function 'frobnicate'
   --> main:5:3
```
