# Debugging Improvements

Identified areas for improving the Glyph compiler's debugging and error reporting capabilities, ordered by impact.

## 1. Stack Traces on Panics (HIGH PRIORITY) -- DONE

**Problem:** The SIGSEGV handler prints a full `_glyph_call_stack` trace, but runtime panics (`glyph_array_bounds_check`, `tm_unreachable`, etc.) only call `fprintf + exit(1)` with a bare message like "index -1 out of bounds (len 38)". No context about which function or call chain caused it.

**Fix:** `glyph_panic()` and `glyph_array_bounds_check()` now print `_glyph_current_fn` and call `_glyph_print_stack()` under `#ifdef GLYPH_DEBUG`. Output: `panic in <fn>: <msg>\n--- stack trace ---\n  <frames>`.

**Impact:** Would have saved ~15 minutes in the calc debugging session. The "index -1" panic was in `infer_pattern` called from `infer_match_arms_ty`, but required GDB with a conditional breakpoint to find.

## 2. Silent SQL Errors

**Problem:** `glyph_db_query_rows` silently returns empty results when a SQL query fails (e.g., referencing a non-existent column). This caused `read_fn_defs_gen` to return 0 definitions from `gled.glyph` (which had an old schema without the `gen` column), producing a mysterious "Compiling 0 definitions..." with no error.

**Fix:** Check the return value of `sqlite3_prepare_v2` in the query functions and print the SQL error message to stderr when it fails.

**Impact:** Would have immediately explained the gled build failure instead of requiring manual schema inspection.

## 3. "Currently Compiling" Indicator -- DONE

**Problem:** When the compiler crashes during "Compiling 18 definitions...", there's no indication which function triggered the issue. Debugging requires binary search or GDB.

**Fix:** `compile_fns_parsed` takes a `verbose` parameter. When `--debug` mode is used, prints `"  compiling: <name>"` for each definition. `_glyph_current_fn` global already tracks the currently-executing function for panic messages.

**Impact:** Would narrow down crashes to a single function immediately.

## 4. BUG-005 Systematic Fix (TyNode/AstNode Offset Ambiguity) -- DONE (partial)

**Problem:** The gen=2 codegen's "prefer largest type" heuristic for field offset resolution causes wrong offsets when TyNode pool elements are accessed without a `.tag` hint. TyNode (5 fields) shares `.n1`, `.n2`, `.ns`, `.sval` with AstNode (7 fields) but at different offsets. Every new type checker function risks this bug.

**Fix:** Created `pool_get(eng, idx)` wrapper that reads `_ = node.tag` inside the function. All 15 type checker functions that access `eng.ty_pool[...]` now use `pool_get`. **Critical caveat:** The `.tag` hint inside `pool_get` only disambiguates within `pool_get`'s own codegen — the CALLER still needs `_ = result.tag` if it accesses `.n1`/`.sval`/`.ns` without also accessing `.tag` on the same local. 7 of the 15 functions needed explicit `_ = node.tag` after the `pool_get` call because they access fields without `.tag` dispatch (e.g., `find_field` only accesses `.sval`, `unify_fields_against` accesses `.sval`/`.n1`).

**Impact:** Prevents the bug class in existing code. New type checker functions should use `pool_get` AND ensure `.tag` is accessed on the result when field-accessing without type dispatch.

## 5. Schema Version Migration

**Problem:** Old `.glyph` databases silently break when the schema evolves (e.g., missing `gen` column, missing `def_history` table). No automatic detection or migration.

**Fix:** Check `meta.schema_version` in `cmd_build` against expected version. Auto-migrate (ALTER TABLE ADD COLUMN, CREATE TABLE IF NOT EXISTS) or print a clear error with migration instructions.

**Impact:** Would prevent silent failures when building old example programs after compiler upgrades.
