# Result Type (`!T`) + `?` Propagation Design

*February 2026. Deferred — saved for later implementation.*

## Summary

Implement proper error handling via Result types and the `?` propagation operator. This replaces sentinel-value checking (`len == -1`, `return -1`) with a structured approach.

## Runtime Representation

A Result is an enum — heap-allocated `{tag, payload}`:
- Tag 0 = Ok(value)
- Tag 1 = Err(error_string)

Identical to existing enum layout. No new runtime concept.

## The `?` Operator

`expr?` is syntactic sugar that lowers to MIR as:

```
tmp = expr
tag = tmp[0]        // field access: tag
if tag == 1:
  return tmp        // propagate error (already a Result)
val = tmp[1]        // field access: unwrap Ok payload
```

This is a field access, a branch, and a return — all things MIR already expresses.

## What Needs to Change

### Self-hosted compiler only (no Rust compiler changes needed)

1. **Parser** — verify `?` postfix is already parsed in `parse_postfix_loop`
2. **MIR lowering** — lower `?` to: eval → check tag → branch to error-return or unwrap
3. **C codegen** — nothing new, it already handles the MIR constructs involved

### Runtime additions (in `cg_runtime_c`)

```c
long long glyph_ok(long long val) {
  long long* r = (long long*)malloc(16);
  r[0] = 0; r[1] = val;
  return (long long)r;
}
long long glyph_err(long long msg) {
  long long* r = (long long*)malloc(16);
  r[0] = 1; r[1] = msg;
  return (long long)r;
}
```

### New fallible I/O functions

Add parallel functions instead of breaking existing API:
- `try_read_file(path)` → `!S` (returns Result instead of sentinel `{NULL, -1}`)
- `try_write_file(path, content)` → `!V` (returns Result instead of `-1`)

Existing `read_file`/`write_file` remain unchanged for backward compatibility.

## Scope

- **Self-hosted compiler only**: programs using `?` get compiled by `./glyph build`, not `glyph0`
- **glyph.glyph cannot use `?`**: since glyph0 compiles it and glyph0 doesn't know about `?`
- **No type system changes needed**: `?` works purely on the tag value at runtime, no type checking required
- **Existing programs unaffected**: new `try_*` functions, old ones kept

## Usage Example

```
load_file path =
  content = try_read_file(path)?
  split_lines(content)

save_file path buf =
  content = join_lines(buf)
  try_write_file(path, content)?
  1

main =
  result = load_file("test.txt")
  match result[0] == 0
    true -> println(result[1])
    _ -> eprintln("failed to load file")
```

## Context

Motivated by the gled assessment (`documents/gled-assessment.md`) which identified error handling as an unproven pattern. The runtime already signals errors via sentinels (`read_file` returns len=-1, `write_file` returns -1), but no example program checks them. This design provides a structured alternative.
