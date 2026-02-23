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
8. Rebuild: `./glyph0 build glyph.glyph --full --gen=2`
9. Test: `./glyph build test_comprehensive.glyph test_out && ./test_out`

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

5. Rebuild: `./glyph0 build glyph.glyph --full --gen=2`

6. Test: `./glyph name test.glyph`

## Recipe 4: Fix a C Codegen Bug (Self-Hosted)

1. **Reproduce:** Build a minimal test program that triggers the bug
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
   ./glyph0 build glyph.glyph --full --gen=2
   ```

7. **Verify:**
   ```bash
   ./glyph build /tmp/test.glyph /tmp/test_out && /tmp/test_out
   ./glyph build test_comprehensive.glyph test_out && ./test_out
   ```

## Recipe 5: Add a Gen=2 Override

Gen=2 overrides replace gen=1 functions when building with `--gen=2`.

1. **Read the gen=1 function** to understand what to override:
   ```bash
   ./glyph get glyph.glyph fn_name
   ```

2. **Write the gen=2 version.** If it calls gen=2-only functions, those must also exist.

3. **Insert with gen=2:**
   ```bash
   sqlite3 glyph.glyph "INSERT INTO def (name, kind, body, hash, tokens, gen) \
     VALUES ('fn_name', 'fn', readfile('/tmp/fn_name.gl'), zeroblob(32), 0, 2)"
   ```
   Or via the CLI (which always inserts gen=1):
   ```bash
   ./glyph put glyph.glyph fn -f /tmp/fn_name.gl
   # Then manually update gen:
   sqlite3 glyph.glyph "UPDATE def SET gen=2 WHERE name='fn_name' AND kind='fn' AND gen=1"
   ```

4. **Rebuild with gen=2:**
   ```bash
   ./glyph0 build glyph.glyph --full --gen=2
   ```

5. **Verify gen=1 still works:**
   ```bash
   ./glyph0 build glyph.glyph --full --gen=1
   ```

## Recipe 6: Verify the Bootstrap Chain

After any compiler change, verify the full bootstrap:

```bash
# Stage 0: Build Rust compiler
cargo build --release && cp target/release/glyph glyph0

# Stage 1: glyph0 compiles glyph.glyph (gen=2) via Cranelift → glyph
./glyph0 build glyph.glyph --full --gen=2

# Verify the self-hosted compiler works
./glyph build test_comprehensive.glyph /tmp/test_comp && /tmp/test_comp
./glyph stat test_comprehensive.glyph
```

Or use ninja for the standard chain:
```bash
ninja                    # 2-stage: glyph0 → glyph (gen=2)
ninja test               # Runs all tests
```

**Note:** The bootstrap is now 2-stage (not 3-stage). `glyph0 --gen=2` compiles all gen=2 overrides via Cranelift directly, producing the final `glyph` binary. The self-hosted compiler cannot self-build with `--gen=2` because it sees both gen=1 and gen=2 overrides with the same names.

## Recipe 7: Add a New Type Definition (Self-Hosted)

1. **Insert the type:**
   ```bash
   ./glyph put glyph.glyph type -b 'Name = {field1: I, field2: S}'
   ```

2. **Field name rules:**
   - Fields are sorted alphabetically at the MIR level
   - Use unique field names to avoid offset ambiguity across types
   - If sharing field names with existing types, use unique prefixes (e.g., `xfield` instead of `field`)

3. **Rebuild with gen=2** to get struct codegen:
   ```bash
   ./glyph0 build glyph.glyph --full --gen=2
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

4. **Update gen=2 codegen** if applicable — add case in `cg_stmt2`

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

4. **Insert definitions via Python script** (most reliable for multi-line code):
   ```python
   import sqlite3
   conn = sqlite3.connect("glyph.glyph")
   cur = conn.cursor()
   defs = [("tk_new_thing", "fn", "tk_new_thing = N")]
   for name, kind, body in defs:
       cur.execute("DELETE FROM def WHERE name=? AND kind=?", (name, kind))
       cur.execute("INSERT INTO def (name,kind,body,hash,tokens,compiled) VALUES (?,?,?,zeroblob(32),0,0)", (name, kind, body))
   conn.commit()
   ```

5. **Rebuild + test:** `./glyph0 build glyph.glyph --full --gen=2`

**Pitfall:** The `parse_atom` function uses a chain of `match k == tk_X` with `true ->` / `_ ->` branches. Each subsequent case is indented one more level. If you insert a new case, all subsequent cases must be re-indented. Getting this wrong causes silent misparsing or segfaults.

## Recipe 11: Batch Insert Definitions via Python

For inserting multiple definitions (especially multi-line ones), Python scripts using `sqlite3` are the most reliable approach. Avoids shell escaping issues with `!`, `{`, quotes, etc.

```python
#!/usr/bin/env python3
import sqlite3

DB = "glyph.glyph"
conn = sqlite3.connect(DB)
cur = conn.cursor()

defs = [
    ("fn_name", "fn", 1, """fn_name arg1 arg2 =
  result = arg1 + arg2
  match result > 0
    true -> result
    _ -> 0"""),
]

for name, kind, gen, body in defs:
    cur.execute("DELETE FROM def WHERE name=? AND kind=? AND gen=?", (name, kind, gen))
    cur.execute("""INSERT INTO def (name, kind, body, hash, tokens, compiled, gen)
                   VALUES (?, ?, ?, zeroblob(32), 0, 0, ?)""", (name, kind, body, gen))

conn.commit()
conn.close()
```

**Why Python over `./glyph put`:**
- No shell escaping issues with `{`, `!`, `"`, `\`, etc.
- Parameterized queries handle all special characters
- Can insert gen=2 definitions directly (CLI always inserts gen=1)
- Can batch multiple definitions in one script

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
2. Look for `((long long*)_N)[K]` (gen=1) or `((Glyph_Type*)_N)->field` (gen=2)
3. Verify field offset: fields are sorted alphabetically, 0-indexed
4. If gen=2, verify `build_local_types` tagged the local correctly

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

## Quick Reference: Reading/Writing Definitions

```bash
# Read a definition
./glyph get glyph.glyph fn_name                    # Print body
./glyph get glyph.glyph fn_name --kind type         # Specific kind

# Write from inline
./glyph put glyph.glyph fn -b 'add a b = a + b'

# Write from file (for multi-line functions)
./glyph put glyph.glyph fn -f /tmp/my_fn.gl

# List definitions matching a pattern
./glyph find glyph.glyph cg_stmt --body             # Search names and bodies

# Direct SQL for complex queries
./glyph sql glyph.glyph "SELECT name FROM def WHERE kind='fn' AND name LIKE 'cg_%' ORDER BY name"

# Check current state
./glyph stat glyph.glyph
./glyph ls glyph.glyph --kind fn --sort tokens       # Largest functions
```
