# Compiler Modification Recipes

Step-by-step recipes for the most common compiler changes.

## Recipe 1: Add a New Runtime Function

Both compilers embed the C runtime, so both must be updated.

**Rust compiler:**
1. Add C implementation to `crates/glyph-codegen/src/runtime.rs` in `RUNTIME_C`
2. Add signature to `declare_runtime()` in `crates/glyph-cli/src/build.rs`
3. Add to `add_runtime_known_functions()` in `build.rs` (so MIR lowering knows the type)
4. `cargo test` to verify

**Self-hosted compiler:**
5. Add C implementation to the appropriate `cg_runtime_*` function in glyph.glyph
6. Add to `is_runtime_fn` chain (extend the last `fn6`, or add `fn7` and chain it)
7. Add to `cg_fn_name` mapping if the name needs `glyph_` prefix
8. Rebuild: `./glyph0 build glyph.glyph --full`
9. Test: `./glyph test glyph.glyph`

**Runtime chain note:** `is_runtime_fn → fn2 → fn3 → fn4 → fn5 → fn6`. Each handles ~5-8 names. The last function in the chain returns 0 (not runtime). To add fn7: write `is_runtime_fn7 name = ...`, update fn6's default case to call `is_runtime_fn7(name)` instead of returning 0.

## Recipe 2: Add a New Expression / AST Node

**Rust compiler (5 files):**
1. `ast.rs`: Add `ExprKind` variant
2. `parser.rs`: Add parsing logic (usually in an existing precedence level or `parse_atom`)
3. `infer.rs`: Add type inference case in `infer_expr`
4. `lower.rs`: Add MIR lowering case in `lower_expr`
5. `cranelift.rs`: Add codegen case (if new Rvalue needed) or it may use existing Rvalue patterns
6. Add parser test in `parser.rs` `#[cfg(test)]` module
7. `cargo test`

**Self-hosted compiler:**
8. Add `ex_NEW = N` constant (next unused integer)
9. Add parser case in `parse_atom` or appropriate precedence function
10. Add lowering case in `lower_expr` or `lower_expr2`
11. Add codegen case in `cg_stmt` or `cg_stmt2` (if new `skind`)
12. Rebuild + test

## Recipe 3: Add a New CLI Command (Self-Hosted)

1. Write `cmd_NAME` function:
   ```
   cmd_NAME argv argc db_path =
     db = glyph_db_open(db_path)
     ... logic using db ...
     glyph_db_close(db)
     0
   ```

2. Add match case in the appropriate `dispatch_cmd*` function (whichever has room, or add `dispatch_cmd5`):
   ```
   "name" -> cmd_NAME(argv, argc, db_path)
   ```

3. Update `print_usage` to include the new command

4. Insert definitions:
   ```bash
   ./glyph put glyph.glyph fn -f /tmp/cmd_name.gl
   ```

5. Rebuild: `./glyph0 build glyph.glyph --full`

6. Test: `./glyph name test.glyph`

## Recipe 4: Fix a C Codegen Bug (Self-Hosted)

1. **Reproduce:** Build a minimal test program that triggers the bug

   Via MCP (preferred — no shell escaping, interactive):
   ```
   mcp__glyph__init(db="/tmp/test.glyph")
   mcp__glyph__put_def(db="/tmp/test.glyph", name="main", kind="fn", body="main = ...")
   mcp__glyph__run(db="/tmp/test.glyph")
   ```

   Via CLI (fallback):
   ```bash
   ./glyph init /tmp/test.glyph
   ./glyph put /tmp/test.glyph fn -b 'main = ...'
   ./glyph build /tmp/test.glyph /tmp/test_out
   ```

2. **Inspect generated C:**
   ```bash
   cat /tmp/glyph_out.c | less
   ```

3. **Identify responsible codegen function:** Trace from `cg_stmt`/`cg_term` by looking at the generated C pattern

4. **Read the function:**
   ```bash
   ./glyph get glyph.glyph <fn_name>
   ```

5. **Write the fix** to a temp file, then update:
   ```bash
   ./glyph put glyph.glyph fn -f /tmp/fix.gl
   ```

6. **Rebuild the compiler:**
   ```bash
   ./glyph0 build glyph.glyph --full
   ```

7. **Verify:**
   ```bash
   ./glyph build /tmp/test.glyph /tmp/test_out && /tmp/test_out
   ./glyph test glyph.glyph    # full test suite (315 tests)
   ```

## Recipe 5: Generational Versioning

The `--gen N` flag selects the highest `gen <= N` per (name, kind). Currently all compiler defs are gen=1. The gen system exists for application programs that want to overlay definitions.

1. **Insert with explicit generation:**
   ```bash
   ./glyph put app.glyph fn -f /tmp/fn_name.gl --gen 2
   ```
   Without `--gen`, auto-detects the highest existing gen for that name/kind (or defaults to 1 for new definitions).

2. **Build with specific generation:**
   ```bash
   ./glyph build app.glyph --gen=2
   ```

## Recipe 6: Verify the Bootstrap Chain

After any compiler change, verify the full bootstrap:

```bash
ninja                    # 4-stage: glyph0 → glyph1 → glyph2 → glyph
ninja test               # Runs all tests
```

Or manually:
```bash
# Stage 0: Build Rust compiler
cargo build --release && cp target/release/glyph glyph0

# Stage 1: glyph0 compiles glyph.glyph via Cranelift → glyph1
./glyph0 build glyph.glyph --full && mv glyph glyph1

# Stage 2: glyph1 self-builds via C codegen → glyph2
./glyph1 build glyph.glyph glyph2

# Stage 3: glyph2 re-builds via LLVM → glyph (final)
./glyph2 build glyph.glyph glyph --emit=llvm

# Verify
./glyph stat glyph.glyph
```

**Note:** The bootstrap is 4-stage: `glyph0` (Rust/Cranelift) → `glyph1` (Cranelift binary) → `glyph2` (C-codegen binary) → `glyph` (LLVM-compiled final binary).

## Recipe 7: Add a New Type Definition (Self-Hosted)

1. **Insert the type:**
   ```bash
   ./glyph put glyph.glyph type -b 'Name = {field1: I, field2: S}'
   ```

2. **Field name rules:**
   - Fields are sorted alphabetically at the MIR level
   - Use unique field names to avoid offset ambiguity across types
   - If sharing field names with existing types, use unique prefixes (e.g., `xfield` instead of `field`)

3. **Rebuild:**
   ```bash
   ./glyph0 build glyph.glyph --full
   ```

4. **Usage in functions:**
   ```
   mk_name a b = {field1: a, field2: b}
   get_field1 r = r.field1
   ```

## Recipe 8: Add an Extern Declaration (for Programs)

1. **Add to extern_ table:**
   ```bash
   ./glyph extern app.glyph getenv getenv "S -> S" --lib c
   ```

   Sig format: arrow-separated `I -> S -> I` (curried). Last type is return. No return arrow — last segment is the return type.

2. **For functions needing non-stdlib headers** (e.g., `time()` from `<time.h>`):

   Write a C wrapper file:
   ```c
   // app_ffi.c
   #include <time.h>
   long long glyph_get_time(void) { return (long long)time(NULL); }
   ```

   Build with concatenation:
   ```bash
   ./glyph build app.glyph || true
   cat app_ffi.c /tmp/glyph_out.c > /tmp/combined.c
   cc -O2 -o app /tmp/combined.c
   ```

3. **Heavy FFI alternative** (life, gled pattern):
   Skip extern_ table entirely. Put all wrappers in a C file with `long long` ABI. Call wrapper names directly from Glyph — unknown function names pass through as-is in generated C.

## Recipe 9: Add a New MIR Rvalue Kind

1. **Define the constant** (next unused `rv_*` value):
   ```
   rv_new_thing = 10
   ```

2. **Update MIR lowering** to emit it — modify `lower_expr`/`lower_expr2` or add new helper

3. **Update C codegen** — add case in `cg_stmt`/`cg_stmt2` for the new `skind`

4. **Update struct codegen** if applicable — add case in `cg_stmt2`

5. **Rust side:** Add `Rvalue` variant in `ir.rs`, emit in `lower.rs`, handle in `cranelift.rs`

6. **Test both compilers**

## Recipe 10: Add a New Token Type (Self-Hosted)

1. **Define the token constant** (find unused slot in the appropriate range):
   ```
   tk_new_thing = N
   ```
   Literal range: 1-9. Operator range: 10-39. Delimiter range: 40-49. Layout range: 60-69.

2. **Update the tokenizer.** Modify `tok_one` to emit the new token kind. For single-character tokens, add a case in the character-code dispatch. For multi-character tokens (like `tk_str_interp_start`), add scanning logic.

3. **Update the parser.** Add a case in `parse_atom` (or appropriate precedence function) for the new token kind. Beware: `parse_atom` uses deeply nested `match k == tk_X` chains — new cases must be inserted at the right nesting level.

4. **Insert definitions:**
   ```bash
   # Single-line constant
   ./glyph put glyph.glyph fn -b 'tk_new_thing = N'

   # Multi-line function — write to temp file, insert from file
   # (avoids shell escaping issues with {, !, ", \)
   ./glyph put glyph.glyph fn -f /tmp/my_fn.gl

   # Specific generation — use --gen flag
   ./glyph put glyph.glyph fn -f /tmp/my_fn.gl --gen 2
   ```

5. **Rebuild + test:** `./glyph0 build glyph.glyph --full`

**Pitfall:** The `parse_atom` function uses a chain of `match k == tk_X` with `true ->` / `_ ->` branches. Each subsequent case is indented one more level. If you insert a new case, all subsequent cases must be re-indented. Getting this wrong causes silent misparsing or segfaults.

## Recipe 11: Inserting Multi-Line Definitions

**Via MCP (preferred — no shell escaping, body passed as JSON string):**
```
mcp__glyph__put_def(db="glyph.glyph", name="fn_name", kind="fn",
  body="fn_name arg1 arg2 =\n  result = arg1 + arg2\n  match result > 0\n    true -> result\n    _ -> 0")
```

**Via CLI (fallback — write to temp file first):**
```bash
# 1. Write the definition body to a temp file (use Write tool, not echo/cat)
#    File contents — no quoting needed:
#    fn_name arg1 arg2 =
#      result = arg1 + arg2
#      match result > 0
#        true -> result
#        _ -> 0

# 2. Insert from file
./glyph put glyph.glyph fn -f /tmp/fn_name.gl

# 3. For specific generation, use --gen flag
./glyph put glyph.glyph fn -f /tmp/fn_name.gl --gen 2
```

**Shell escaping notes:**
- Use the Write tool (not echo/heredoc) to create temp files — avoids all escaping issues
- `./glyph put -f` reads the file directly with no shell interpretation
- For inline `-b`, single quotes protect most characters; use `'\''` for literal single quotes
- `\{` in Glyph source is a literal brace (not interpolation) — safe in files, tricky in shell strings

**Gen flag:** `./glyph put --gen N` inserts directly at the specified generation. Without `--gen`, auto-detects the highest existing gen for that name/kind, or defaults to gen=1 for new definitions.

**Batch inserts:** Write each definition to its own temp file and run `./glyph put -f` for each. Or use `sqlite3` with `.read` on a SQL file for bulk operations.

## Recipe 12: Debugging the Self-Hosted Compiler

### Inspecting generated C code
```bash
./glyph build app.glyph out
cat /tmp/glyph_out.c | less                    # Full generated C
grep "glyph_fn_name" /tmp/glyph_out.c          # Find specific function
```

### Debugging segfaults
The C runtime includes a SIGSEGV handler that prints `segfault in function: <name>`. This tells you which Glyph function crashed.

```bash
./glyph build app.glyph out && ./out
# Output: "segfault in function: my_broken_fn"
```

For deeper investigation, compile with debug info:
```bash
./glyph build app.glyph out || true
cc -g -O0 -o out_debug /tmp/glyph_out.c -lsqlite3 && gdb ./out_debug
```

### Debugging parse errors
If the self-hosted parser fails, use the Rust compiler to get better error messages:
```bash
cargo run -- build app.glyph              # Rust parser has line:col diagnostics
./glyph build app.glyph out              # Self-hosted parser gives less detail
```

### Debugging MIR lowering
Use `--emit-mir` with the Rust compiler to inspect MIR:
```bash
cargo run -- build app.glyph --emit-mir   # Prints MIR for all functions
```

The self-hosted compiler has no MIR dump, but you can inspect the generated C to understand what MIR produced.

### Debugging field offset issues
If struct field access gives wrong values:
1. Check `/tmp/glyph_out.c` for the field access pattern
2. Look for `((long long*)_N)[K]` (offset-based) or `((Glyph_Type*)_N)->field` (struct codegen)
3. Verify field offset: fields are sorted alphabetically, 0-indexed
4. For struct codegen, verify `build_local_types` tagged the local correctly

### Debugging type system (`glyph check`)
```bash
./glyph check app.glyph                   # Run typecheck, print warnings
```
Known issue: record type unification can crash. If `glyph check` segfaults, the crash is in the type system, not your program.

### Debugging the compiler itself
To debug a crash in the self-hosted compiler (not the compiled program):
```bash
# Rebuild compiler with gen=1 (simpler C output, easier to read)
./glyph0 build glyph.glyph --full --gen=1
# Check which compiler function crashes
./glyph build app.glyph out
# "segfault in function: <compiler_fn>"

# Read the compiler function
./glyph get glyph.glyph <compiler_fn>

# Inspect the generated C for that compiler function
grep -A 50 "glyph_<compiler_fn>" /tmp/glyph_out.c
```

### Common crash causes
| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Segfault in `scan_string_end` | Unterminated string literal | Check string has closing `"` |
| Segfault in `parse_atom` | Wrong indentation in `parse_atom` source | Verify match nesting levels |
| Segfault in `resolve_fld_off` | Field offset ambiguity | Use unique field prefixes |
| Segfault in `subst_resolve` | Type pool index corruption | Bounds check or avoid the type path |
| Segfault in `cg_str_literal` | Escape processing on non-string data | Verify string literal boundaries |
| Wrong field value | Alphabetical sort order mismatch | Fields sorted A-Z, check offset |
| Function returns garbage | Dangling stack pointer | Heap-allocate via `glyph_alloc` |
| `_glyph_current_fn` shows wrong name | Crash in callee's prologue | Look at last called function |
| `parse_single_pattern` returns wrong strings | String match patterns always fall through to `_ ->` | Double-stripping: token text already unquoted; don't call `str_slice(s, 1, len-1)` again. Use `s` directly. |

## Recipe 13: Working with the MCP Server

The MCP server is the **primary workflow** for interacting with glyph.glyph. It avoids shell escaping issues, supports multi-line bodies, and returns structured JSON errors.

**Starting the MCP server:**
```bash
./glyph mcp glyph.glyph    # starts stdio JSON-RPC server
```

In Claude Code, MCP tools are available as `mcp__glyph__*` after the server is connected.

**18 available tools:**

| Tool | Purpose |
|------|---------|
| `mcp__glyph__init` | Create a new .glyph database |
| `mcp__glyph__get_def` | Read a definition body |
| `mcp__glyph__put_def` | Insert/update a definition |
| `mcp__glyph__remove_def` | Delete a definition |
| `mcp__glyph__list_defs` | List definitions (filter by kind/ns) |
| `mcp__glyph__search_defs` | Search names and bodies |
| `mcp__glyph__check_def` | Type-check a definition, returns structured errors |
| `mcp__glyph__deps` | Forward dependency edges |
| `mcp__glyph__rdeps` | Reverse dependency edges |
| `mcp__glyph__sql` | Raw SQL query |
| `mcp__glyph__build` | Build the program (shells out) |
| `mcp__glyph__run` | Build + run (shells out) |
| `mcp__glyph__coverage` | Coverage report |
| `mcp__glyph__link` | Link a library into an app |
| `mcp__glyph__migrate` | Run schema migrations |
| `mcp__glyph__use` | Register library as build-time dependency |
| `mcp__glyph__unuse` | Remove a registered library dependency |
| `mcp__glyph__libs` | List registered library dependencies |

**Adding a new MCP tool:**
1. Write `mcp_tool_NAME(db, params)` — parse params with `mcp_str_prop`/`mcp_int_prop`, return JSON string
2. Register in `mcp_add_toolsN` (whichever has room, or add `mcp_add_toolsN+1` and chain it)
3. Handle in `mcp_tools_callN` — add match arm that calls `mcp_tool_NAME`
4. If tool needs build/run, use `glyph_system(cmd)` — **never write to stdout directly** (corrupts JSON-RPC)
5. Chain pattern: `mcp_add_toolsN` ends with a call to `mcp_add_toolsN+1`; `mcp_tools_callN` falls through to `mcp_tools_callN+1` at the bottom

**Insert via MCP vs CLI:**
```
# MCP — preferred (no shell quoting, multi-line works naturally)
mcp__glyph__put_def(db="glyph.glyph", name="my_fn", kind="fn",
  body="my_fn x =\n  x + 1")

# CLI — fallback
./glyph put glyph.glyph fn -f /tmp/my_fn.gl
```

## Recipe 14: Adding to the LLVM Backend

The LLVM IR backend lives in `glyph.glyph` under the `llvm` namespace (`ll_*` prefix). It mirrors the structure of the C codegen backend.

**Trigger:** `./glyph build app.glyph out --emit=llvm` → writes `/tmp/glyph_out.ll`

**Entry point:** `cg_llvm_program(mirs, struct_map, externs) → S` — assembles complete LLVM IR module text

**Structure (mirrors C codegen):**
```
cg_llvm_program → ll_emit_functions → ll_emit_function → ll_emit_block → ll_emit_stmt / ll_emit_term
```

**LLVM type mapping:**
| Glyph | LLVM |
|-------|------|
| `I` (Int64) | `i64` |
| `S` (Str) | `{i64, i64}*` |
| `B` (Bool) | `i1` |
| Array | `{i64, i64, i64}*` |
| Record/Enum | `i64*` |

**Adding a new MIR statement kind to LLVM:**
1. Read the analogous C codegen handler in `cg_stmt`/`cg_stmt2` for reference
2. Add a case in `ll_stmt` for the new `skind` value — emit LLVM IR text
3. Use `ll_operand` to render operands (produces `%_N` for locals, integer constants, etc.)
4. Test: `mcp__glyph__build(db="glyph.glyph", emit_llvm=true)` or `./glyph build glyph.glyph out --emit=llvm`

**Self-compilation test:**
```bash
./glyph build glyph.glyph out --emit=llvm    # compiles glyph compiler via LLVM path
./out stat glyph.glyph                        # verify produced binary works
```

## Quick Reference: Reading/Writing Definitions

**Preferred: MCP tools (no shell escaping)**
```
mcp__glyph__get_def(db="glyph.glyph", name="fn_name")
mcp__glyph__put_def(db="glyph.glyph", name="fn_name", kind="fn", body="fn_name x = x + 1")
mcp__glyph__search_defs(db="glyph.glyph", query="cg_stmt")
mcp__glyph__sql(db="glyph.glyph", query="SELECT name FROM def WHERE kind='fn' AND name LIKE 'cg_%'")
```

**Fallback: CLI**
```bash
# Read a definition
./glyph get glyph.glyph fn_name                    # Print body
./glyph get glyph.glyph fn_name --kind type         # Specific kind

# Write from inline
./glyph put glyph.glyph fn -b 'add a b = a + b'

# Write from file (for multi-line functions)
./glyph put glyph.glyph fn -f /tmp/my_fn.gl

# Write with explicit generation
./glyph put glyph.glyph fn -f /tmp/my_fn.gl --gen 2

# List definitions matching a pattern
./glyph find glyph.glyph cg_stmt --body             # Search names and bodies

# Direct SQL for complex queries
./glyph sql glyph.glyph "SELECT name FROM def WHERE kind='fn' AND name LIKE 'cg_%' ORDER BY name"

# Check current state
./glyph stat glyph.glyph
./glyph ls glyph.glyph --kind fn --sort tokens       # Largest functions
```
