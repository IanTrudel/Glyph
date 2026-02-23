# Glyph Bug Reports

## BUG-001: glint `count_nl` segfaults on stack overflow

**Status:** Open
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
