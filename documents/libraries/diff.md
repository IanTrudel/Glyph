### diff.glyph — Text Diff and Three-Way Merge Library

**Location:** `libraries/diff.glyph`
**Dependencies:** `stdlib.glyph`
**Status:** Core complete (33 definitions, 11/11 tests passing)

---

## API

### Core Diff

- `diff_lines a b` — LCS-based diff of two `[S]` arrays, returns `[DiffOp]`
- `DiffOp = {kind: I, text: S}` — edit operation (kind: `diff_eq`=0, `diff_ins`=1, `diff_del`=2)

### Formatting

- `diff_format ops` — render ops as `+ `/`- `/`  ` prefixed text
- `diff_stats ops` — returns `{adds: I, dels: I, eqs: I}`

### Three-Way Merge

- `diff_merge3 ancestor a b` — returns `{lines: [S], conflicts: I}`
- Conflict markers: `<<<<<<< A` / `=======` / `>>>>>>> B`

---

## LLM Token-Efficient Diff Rendering (Design Notes)

When Monticello presents diffs to LLMs, the format should minimize BPE tokens while preserving all semantic information. These decisions are deferred until `gmc` is operational.

### Principles

1. **Definitions are the unit, not lines.** A Glyph diff shows which definitions changed, not which lines in a file changed.
2. **Show new body, not patches.** For small definitions, the complete new body is fewer tokens than diff markup overhead.
3. **No old body by default.** The LLM can query the old body via MCP if it needs comparison. Don't waste tokens showing both.
4. **Name only for removals.** The deleted body is not useful information.
5. **Data, not prose.** Summary is `+1 ~2 -1`, not "1 added, 2 modified, 1 removed".

### Proposed Format

**Summary line:**
```
12 → 13 | +1 ~2 -1
```

**Markers:** `+` added, `~` modified, `-` removed.

**Small definitions (below token threshold):** full new body.
```
+ fn parse_char_lit
parse_char_lit t =
  c = tok_val(t)
  mk_int(str_char_at(c, 0))

~ fn parse_atom
parse_atom t =
  match tok_kind(t)
    1 -> parse_int(t)
    7 -> parse_char_lit(t)

- fn old_helper
```

**Large definitions, small change:** compact line-level diff with 1 context line.
```
~ fn cg_runtime_c (2847 tok, 2 regions)
  = char* result = malloc(a.len + b.len);
  - memcpy(result, a.ptr, a.len);
  + memmove(result, a.ptr, a.len);
  ...
  = void glyph_array_push(GVal arr, GVal val) {
  + glyph_bounds_check(arr);
```

**Large definitions, big change:** full new body (it's essentially a rewrite).

### Threshold Strategy

- Token count is already in the `def` table — threshold check is free.
- `diff_stats` provides the changed-to-unchanged ratio to pick the right strategy.
- Exact thresholds TBD when real usage data is available.

### Open Questions

- Should `~` (modified) show old token count → new token count as a change magnitude signal?
- Should the format include dependency impact (e.g., "3 callers affected")?
- Is 1 context line enough for large-def diffs, or should it be configurable?
- Should there be a JSON variant for MCP tool responses vs. a text variant for dump/export?
