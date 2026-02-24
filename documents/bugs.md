# Glyph Bug Reports

## BUG-001: glint `count_nl` segfaults on stack overflow

**Status:** Fixed (TCO pass implemented 2026-02-23)
**Severity:** Medium
**Component:** examples/glint/glint.glyph
**First observed:** 2026-02-22

### Description

Running glint on any database crashes with `segfault in function: count_nl`. The function recurses once per character in a string body to count newlines, which overflows the default stack.

### Reproduction

```bash
./glyph build examples/glint/glint.glyph examples/glint/glint
./examples/glint/glint examples/glint/glint.glyph
# Output: segfault in function: count_nl
```

Also crashes on smaller databases like `test_comprehensive.glyph`.

### Root cause

`count_nl` uses tail-position recursion to iterate over each character:

```
count_nl s i acc =
  match i >= str_len(s)
    true -> acc
    _ ->
      ch = str_char_at(s, i)
      match ch == 10
        true -> count_nl(s, i + 1, acc + 1)
        _ -> count_nl(s, i + 1, acc)
```

The self-hosted C codegen does not perform tail-call optimization, so each character adds a stack frame. With bodies up to 695 characters and `sum_lines` also recursing over all 26 rows, the combined stack depth exceeds the OS default limit.

The call chain is: `report_db` → `sum_lines` (recursive over rows) → `count_lines` → `count_nl` (recursive over characters). Both `sum_lines` and `count_nl` are recursive, creating nested recursion.

### Possible fixes

1. **Rewrite as iterative loop**: Replace `count_nl` recursion with a `for` loop (Glyph supports for-loops that desugar to index-based MIR loops).
2. **Increase stack size**: Link with `-Wl,-z,stacksize=8388608` or similar. Treats symptom, not cause.
3. **Tail-call optimization in codegen**: Add TCO pass to the C codegen. Large effort, fixes all similar patterns.

### Notes

- Pre-existing bug — present before generational versioning changes.
- The Rust/Cranelift compiler (glyph0) also doesn't perform TCO, so building with glyph0 would have the same issue.
- `sum_lines_kind` has the same recursive pattern and would hit the same issue on larger databases.

---

## BUG-002: Self-hosted compiler fails on databases without `gen` column

**Status:** Fixed
**Severity:** High
**Component:** glyph.glyph (self-hosted compiler)
**First observed:** 2026-02-22

### Description

After the generational versioning feature was added, the self-hosted compiler's `read_fn_defs`, `read_test_defs`, `read_test_names`, and `cmd_check` were updated to include `AND gen=1` in their SQL queries. However, the self-hosted compiler does not run database migration — only the Rust compiler (`glyph0`) adds the `gen` column via `migrate_gen()` on `Database::open()`.

This caused the self-hosted compiler to silently find 0 definitions when opening any database that hadn't been previously opened by glyph0, producing broken/empty binaries.

### Affected programs

Any `.glyph` database built with `./glyph build` that hadn't been opened by `glyph0` first. This includes `examples/gled/gled.glyph` and any user programs.

### Fix

Removed `AND gen=1` from the 4 self-hosted read queries. The self-hosted compiler is itself a gen=1 artifact and doesn't support `--gen`, so it compiles everything in the database. Generation filtering is only needed in the Rust compiler.

The `do_put` command retains `gen=1` in its INSERT since it explicitly sets the column value (and the target database has `gen` in its schema because `init_schema` includes it).

---

## BUG-003: `glyph put` cannot update gen=2 definitions

**Status:** Fixed (2026-02-23)
**Severity:** Medium
**Component:** glyph.glyph (self-hosted compiler, `do_put`/`cmd_put`)
**First observed:** 2026-02-23

### Description

`glyph put` always creates gen=1 rows. There is no `--gen=2` flag. Gen=2 rows are correctly preserved (the DELETE filters by `gen=1` and deletes by `id`), but there is no way to insert or update gen=2 definitions via the CLI.

### Consequence

When both gen=1 and gen=2 overrides of a function need updating (e.g., adding a new pass to `build_program`), `put` can only update the gen=1 version. The gen=2 version must be updated via raw SQL:

```bash
sqlite3 glyph.glyph "UPDATE def SET body='...' WHERE name='build_program' AND kind='fn' AND gen=2;"
```

### Fix

Added `--gen N` flag to `glyph put`. Without `--gen`, `put` auto-detects the highest existing generation for the given name/kind (falls back to gen=1 for new definitions). Three definitions updated: `do_put` (accepts gen parameter, auto-detect logic), `cmd_put` (parses `--gen` flag), `print_usage` (updated help text).

Also fixed a Cranelift codegen bug discovered during implementation: runtime functions registered with `glyph_` prefix (e.g., `glyph_int_to_str`) were not resolvable when called by user code without the prefix (e.g., `int_to_str(x)`). The codegen now falls back to trying the `glyph_`-prefixed name when a function reference is not found.

---

## BUG-004: `glyph dump` defaults to 500-token budget, not full dump

**Status:** Open
**Severity:** Low (usability)
**Component:** glyph.glyph (self-hosted compiler, `cmd_dump`)
**First observed:** 2026-02-23

### Description

`glyph dump <db>` defaults to `--budget 500` tokens and roots from `main`, outputting only a small subset of definitions. A plain `glyph dump` without flags gives misleading partial output — users expect a full dump.

### Expected behavior

`glyph dump <db>` with no flags should either:
1. Default to `--all` (full dump), or
2. Print a warning that output is truncated and suggest `--all`

### Workaround

Use `glyph dump <db> --all` for full output, or `sqlite3 <db> .dump` for raw SQL.
