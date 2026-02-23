# gstats: Technical Assessment

*Written by Claude (Opus 4.6) after examining the gstats implementation built by a fresh LLM session from the gstats-spec.md. February 2026.*

## Summary

gstats is a statistical data analyzer: read a file of integers (one per line), compute count/sum/min/max/mean, print a formatted report. It was designed as the first Glyph example to exercise gen=2 struct codegen (named record types), the extern system, and C string interop.

**Verdict**: The program works correctly in all tested scenarios. The gen=2 struct codegen system is validated as *partially working* — record allocation uses typedef structs, but field reads on function parameters fall back to offset-based indexing. The extern system works for `getenv`; `time` was bypassed via a C wrapper file instead.

## What Works

### Program Correctness (5/5)

Every test case produces correct output:

| Input | Result |
|-------|--------|
| `10 20 30 5 45` | Count: 5, Sum: 110, Min: 5, Max: 45, Mean: 22 |
| Single value `42` | Count: 1, Sum/Min/Max: 42, Mean: 42 |
| Empty file | "No data found." |
| Mixed data (blanks, letters, negatives) | Skips non-numeric, handles `-5` and `0` correctly |
| Nonexistent file | "No data found." (graceful, no crash) |
| No arguments | Usage message to stderr |
| `GSTATS_VERBOSE=1` | Prints "Values: ..." before report |
| Large numbers (1M, 2M, 3M) | Correct arithmetic |

3/3 unit tests pass: `test_compute_stats`, `test_min_max`, `test_is_numeric`.

### Definition Quality (4/5)

24 definitions total (19 fn + 2 type + 3 test) — within the spec's ~20-25 target.

Clean architecture:
- **Parsing layer**: `is_digit`, `is_num_loop`, `is_numeric_str`, `try_add_num`, `scan_lines`, `read_numbers` — proper validation chain, handles negative numbers, rejects bare `-`
- **Stats layer**: `min_val`, `max_val`, `stats_loop`, `compute_stats` — functional accumulator pattern, correct initialization with first element
- **Config layer**: `get_env_str`, `check_verbose`, `read_config` — proper NULL-check on getenv return
- **Output layer**: `pad_left`, `print_vals_loop`, `print_values`, `print_report`, `print_usage`
- **Entry**: `main` — clean dispatch with proper edge case handling

Notable design choices:
- `scan_lines` combines line splitting and number parsing into a single recursive pass (avoids needing a separate `split_lines` → `parse_int` pipeline). More complex but more efficient.
- `is_numeric_str` validates the entire string before calling `str_to_int`, preventing `str_to_int("abc")` from silently returning 0.
- The `compute_stats` empty-array case returns a zero Stats record instead of crashing — good defensive design.

One minor issue: `pad_left` is called with width=1 everywhere (`pad_left(int_to_str(n), 1)`), which means it never actually pads anything (any integer string is already >= 1 character). The alignment in the output comes from the label strings ("Count:   ", "Sum:     ") having embedded spaces, not from dynamic padding. The `pad_left` function works correctly but is essentially unused.

### Build System (4/5)

The build script follows the established pattern from other examples and adds a nice `--test` flag for running tests:

```sh
#!/bin/sh
cd "$(dirname "$0")"
../../glyph build gstats.glyph gstats 2>/dev/null || true
cat gstats_ffi.c /tmp/glyph_out.c > /tmp/gstats_full.c
cc -O2 -Wno-int-conversion -Wno-incompatible-pointer-types -o gstats /tmp/gstats_full.c
echo "Built gstats"
```

The `--test` flag is a good addition not seen in other example build scripts — it compiles and runs the test binary with the FFI wrapper prepended.

27k binary — comparable to calculator (24k), smaller than gled (38k).

## Gen=2 Struct Codegen: Partial Success

This is the primary finding. The gen=2 system has two halves — **record allocation** and **record field reads** — and only the first half uses struct codegen.

### Record Allocation: Uses Structs

All 6 record construction sites emit typed struct allocation:

```c
// compute_stats — initial stats
{ Glyph_Stats* __r = (Glyph_Stats*)glyph_alloc(sizeof(Glyph_Stats));
  __r->count = 1LL; __r->max_val = _8; __r->min_val = _8; __r->sum = _8;
  _9 = (long long)__r; }

// read_config — config record
{ Glyph_Config* __r = (Glyph_Config*)glyph_alloc(sizeof(Glyph_Config));
  __r->filename = _0; __r->verbose = _2;
  _3 = (long long)__r; }
```

This is the gen=2 path working correctly: the MIR `rv_aggregate` with `ag_record` is matched against the type definitions by sorted field set, and `Glyph_Stats`/`Glyph_Config` typedefs are used.

### Record Field Reads: Falls Back to Offsets

All 14 field read sites use offset-based indexing:

```c
// print_report — reading stats fields
_3 = ((long long*)_0)[0];   // should be ((Glyph_Stats*)_0)->count
_8 = ((long long*)_0)[3];   // should be ((Glyph_Stats*)_0)->sum
_13 = ((long long*)_0)[2];  // should be ((Glyph_Stats*)_0)->min_val
_18 = ((long long*)_0)[1];  // should be ((Glyph_Stats*)_0)->max_val

// main — reading config fields
_12 = ((long long*)_11)[0]; // should be ((Glyph_Config*)_11)->filename
_21 = ((long long*)_11)[1]; // should be ((Glyph_Config*)_11)->verbose
```

### Root Cause

The gen=2 `build_local_types` pass identifies which local variables hold named records by scanning for `rv_aggregate` (record construction) statements. When a variable is constructed via `{count: n, sum: s, ...}`, the pass matches the sorted fields against type definitions and tags that local as `Glyph_Stats`.

But when a record is passed as a **function parameter**, no `rv_aggregate` exists — the parameter arrives as a plain `long long`. The type information isn't propagated across function boundaries. Functions like `print_report(stats, timestamp)` and `stats_loop(nums, i, st)` receive `stats`/`st` as untyped parameters, so field reads fall through to the gen=1 offset-based path.

This also explains why `stats_loop` has **both patterns** in the same function:
- Field **reads** on parameter `_2` (the incoming stats): offset-based `((long long*)_2)[0]`
- Field **writes** to `new_st` (locally constructed): struct-based `__r->count = _11`

### Impact

**Correctness: Zero impact.** Both access patterns produce identical results because:
1. Fields are sorted alphabetically in both paths
2. Each field is 8 bytes (`long long`) in both paths
3. `((long long*)p)[0]` and `((Glyph_Stats*)p)->count` access the same memory offset

**Readability: Moderate impact.** The generated C is a mix of typed and untyped access, which is confusing. A human reading the C output sees struct allocation but offset reads, making it harder to understand the data flow.

### Potential Fix

Extend `build_local_types` to propagate type information through:
1. **Function signatures**: If a function's MIR shows a parameter being used with fields `{count, max_val, min_val, sum}`, match those field names against type definitions to tag the parameter
2. **rv_use copies**: If `_10 = _2` and `_2` is typed, propagate to `_10` (this may already work for locally-constructed records)
3. **Call return values**: If `compute_stats` returns a `Glyph_Stats`, tag the return value at call sites

Option 1 (pre-scan field accesses on parameters) would handle the gstats case completely. It's the same disambiguation strategy already used in the gen=1 `fix_all_field_offsets` pass.

## Extern System: One of Two

### getenv: Works via Extern Table

```sql
extern_(name='getenv', symbol='getenv', lib='c', sig='I -> I', conv='C')
```

Generated wrapper:
```c
long long glyph_getenv(long long _0) { return (long long)(getenv)(_0); }
```

The full interop chain works: `str_to_cstr` → `glyph_getenv` → NULL check → `cstr_to_str`. This validates that the extern system handles C string functions correctly.

### time: Bypassed via C Wrapper

Instead of declaring `time` in the extern_ table, the implementation uses a 5-line C wrapper:

```c
#include <time.h>
long long get_timestamp(long long dummy) {
    return (long long)time(NULL);
}
```

This follows the "FFI without externs" pattern from life and gled (wrapper file + build script concatenation). It works, but means only one of the two spec'd externs actually tests the extern system.

The pragmatic reason: `time(NULL)` needs a literal NULL (0 cast to pointer), which the extern wrapper system would handle as `(time)(0)` — this actually works fine with the `I -> I` sig. So this was an unnecessary bypass, but not incorrect.

## Comparison to Spec

| Spec Requirement | Implementation | Match |
|-----------------|---------------|-------|
| 2 named record types (Stats, Config) | Both present with correct fields | Yes |
| Struct codegen for allocation | Working — `Glyph_Stats*`, `Glyph_Config*` | Yes |
| Struct codegen for field reads | Falls back to offset-based | **No** |
| 2 externs (time, getenv) | Only getenv in extern_; time via C wrapper | Partial |
| str_to_cstr/cstr_to_str interop | Working via getenv path | Yes |
| ~20-25 definitions | 24 (19 fn + 2 type + 3 test) | Yes |
| 3-5 tests | 3 tests, all passing | Yes |
| Verbose mode via env var | Working | Yes |
| Edge case handling | Empty, single, mixed, nonexistent | Yes |
| Output format matches spec | Exact match | Yes |

## What This Tells Us About Glyph

### LLM Implementation Quality

A fresh LLM session produced a correct, well-structured program from the spec document. The definition quality is high — proper validation, clean separation of concerns, correct edge case handling. The `scan_lines` function (combining line splitting and parsing in one recursive pass) shows the LLM understood Glyph's recursive-only control flow and optimized accordingly.

The session chose to bypass the extern system for `time` — a reasonable engineering decision (simpler, proven pattern from other examples), though it reduced the test coverage for the feature being validated.

### Gen=2 Gap Is Real but Bounded

The struct codegen gap (allocation yes, parameter reads no) is a genuine limitation of the current gen=2 system, now visible for the first time because gstats is the first program with named types. The gap is:
- **Not a correctness issue** — offset-based reads work identically
- **A readability issue** — mixed struct/offset access in generated C
- **A completeness issue** — gen=2 claims "struct access" but only delivers it for writes
- **Fixable** — field name pre-scanning on parameters (already done for gen=1 disambiguation) would close the gap

### Extern System Works for Simple Cases

The getenv path validates the full extern pipeline: table declaration → wrapper generation → C string interop. More complex extern usage (struct-passing, multi-lib linking, callback functions) remains untested by any example.

### Binary Size Is Predictable

27k for 19 functions follows the established pattern: ~1.2k per function average, dominated by the embedded C runtime (~16k). The runtime is the floor; actual function code adds incrementally.

## Recommendations

1. **Fix gen=2 field reads on parameters** — Extend `build_local_types` to pre-scan field accesses on function parameters and match against type definitions. This would make gstats (and all future programs with named types) generate fully-typed C throughout.

2. **Add time extern to gstats** — Replace the `gstats_ffi.c` wrapper with `./glyph extern gstats.glyph time time "I -> I" --lib c` and call `time(0)` directly. This completes the extern system validation.

3. **Fix pad_left usage** — Change `pad_left(int_to_str(n), 1)` to a meaningful width (e.g., `pad_left(int_to_str(n), 8)`) so the padding function actually exercises its recursion.

4. **Use gstats as the gen=2 regression test** — Add to CI/ninja to catch struct codegen regressions as the system evolves.
