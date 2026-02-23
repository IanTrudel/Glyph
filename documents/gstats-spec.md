# Spec: `gstats` — Statistical Data Analyzer

*A command-line statistics tool to test Glyph's gen=2 struct codegen, extern system, and named record types.*

## Why This Test Matters

All existing example programs predate the gen=2 struct codegen system. None use named type definitions:

| Example | Externs | Named Types |
|---------|---------|-------------|
| calculator | getchar, fflush | none |
| glint | sqlite3 × 4 | none |
| gled | ncurses × 7 | none |
| life | X11 via C wrappers | none |
| **gstats** | **time, getenv** | **Stats, Config** |

`gstats` is the **first example using named record types**, which exercises the gen=2 code path:
- Type definitions in the `def` table (`kind='type'`) with record syntax
- `typedef struct { long long f1; ... } Glyph_Name;` in generated C
- Field access via `->field` instead of offset-based `((long long*)p)[N]`
- Matching: sorted field set equality between MIR records and type definitions
- Anonymous records (if any) fall back to offset-based access

## Project Location

`examples/gstats/`

## Files

| File | Purpose |
|------|---------|
| `gstats.glyph` | SQLite database with ~20-25 Glyph definitions |
| `build.sh` | Build script (standard pattern) |

No C wrapper file needed — externs are simple stdlib functions.

## Type Definitions

Two named record types with distinct sorted field sets:

```
Stats = {count: I, max_val: I, min_val: I, sum: I}
```
Sorted fields: `count`, `max_val`, `min_val`, `sum` → generates `Glyph_Stats` in C.

```
Config = {filename: S, verbose: I}
```
Sorted fields: `filename`, `verbose` → generates `Glyph_Config` in C.

Insert via:
```bash
./glyph put gstats.glyph type -b 'Stats = {count: I, max_val: I, min_val: I, sum: I}'
./glyph put gstats.glyph type -b 'Config = {filename: S, verbose: I}'
```

**Important**: These type definitions enable gen=2 struct codegen. When the compiler sees a record aggregate whose sorted fields match a type definition, it emits `Glyph_Stats` / `Glyph_Config` structs instead of anonymous offset-based records.

## Extern Declarations

Two C stdlib functions:

```bash
./glyph extern gstats.glyph time time "I -> I" --lib c
./glyph extern gstats.glyph getenv getenv "I -> I" --lib c
```

| Name | Symbol | Sig | Lib | Notes |
|------|--------|-----|-----|-------|
| `time` | `time` | `I -> I` | c | `time(NULL)` returns epoch seconds. Pass 0 for NULL. |
| `getenv` | `getenv` | `I -> I` | c | Takes C string ptr, returns C string ptr or 0 (NULL). |

These test extern wrapper generation: `long long glyph_time(long long)` and `long long glyph_getenv(long long)`. The `getenv` path also tests `str_to_cstr`/`cstr_to_str` conversion (Glyph strings ↔ C strings).

## Interface

```
./gstats <filename>                 # Analyze file of numbers
GSTATS_VERBOSE=1 ./gstats <file>   # Verbose mode (show individual values)
```

No arguments: print usage and exit.

## Input Format

Plain text file with one integer per line:

```
10
20
30
5
45
```

Blank lines and non-numeric lines should be skipped.

## Output Format

```
=== Statistics Report ===
Count:   5
Sum:     110
Min:     5
Max:     45
Mean:    22
Generated at: 1740268800
```

- Right-align values for readability.
- Mean = integer division (`sum / count`).
- Timestamp from `time(0)` — epoch seconds.
- If `GSTATS_VERBOSE=1` is set, print individual values before the report:
  ```
  Values: 10 20 30 5 45
  === Statistics Report ===
  ...
  ```

## Definition Structure (~20-25 defs)

### Types (2)
- `Stats` — `{count: I, max_val: I, min_val: I, sum: I}`
- `Config` — `{filename: S, verbose: I}`

### String helpers (2-3)
- `str_contains text pat` — substring search using `str_char_at`/`str_len` (no built-in)
- `is_digit ch` — check if char code is '0'..'9'
- `s2 a b` / `s3 a b c` — string concat helpers (optional, or use `+`)

### Parsing (3-4)
- `parse_int line` — parse a line into integer (skip non-numeric lines)
- `read_numbers filename` — read file, split into lines, parse each into numbers array
- `split_lines text` — split string on newline characters into array
- `parse_int_loop line i acc sign` — recursive integer parsing helper

### Statistics (2-3)
- `compute_stats nums` — iterate array, return Stats record with count/sum/min/max
- `stats_loop nums i st` — recursive iteration building Stats
- `min_val a b` / `max_val a b` — integer min/max helpers

### Config (2-3)
- `read_config filename` — build Config record from args + environment
- `check_verbose u` — read GSTATS_VERBOSE env var via `getenv` extern
- `get_env_str name` — helper: `str_to_cstr` → `getenv` → `cstr_to_str` with NULL check

### Formatting and output (3-4)
- `format_report stats timestamp` — build output string from Stats fields
- `print_report stats timestamp` — print formatted report
- `print_values nums` — print values line for verbose mode
- `pad_label label width` — right-pad label for alignment

### Main (1-2)
- `main` — parse args, read config, load file, compute stats, format output
- `print_usage u` — usage message (dummy param for side effects)

### Tests (3-5)
- `test_compute_stats` — compute stats on known array, verify fields
- `test_parse_int` — parse various strings, verify results
- `test_min_max` — verify min/max helpers
- `test_split_lines` — split a multi-line string, verify array
- `test_check_verbose` — verify env var reading (optional)

## Build Process

```bash
#!/bin/sh
cd "$(dirname "$0")"
../../glyph build gstats.glyph gstats
echo "Built gstats"
```

No C wrapper concatenation needed — `time` and `getenv` are simple externs handled by the compiler's extern wrapper system.

## Verification

### Setup
```bash
echo -e "10\n20\n30\n5\n45" > /tmp/test_data.txt
```

### Build and run
```bash
cd examples/gstats
chmod +x build.sh && ./build.sh
./gstats /tmp/test_data.txt
```

### Expected output
```
=== Statistics Report ===
Count:   5
Sum:     110
Min:     5
Max:     45
Mean:    22
Generated at: <epoch timestamp>
```

### Verbose mode
```bash
GSTATS_VERBOSE=1 ./gstats /tmp/test_data.txt
```
```
Values: 10 20 30 5 45
=== Statistics Report ===
Count:   5
Sum:     110
Min:     5
Max:     45
Mean:    22
Generated at: <epoch timestamp>
```

### Edge cases
```bash
echo "" > /tmp/empty.txt
./gstats /tmp/empty.txt
# Should handle gracefully (0 count, no crash)

echo -e "42" > /tmp/single.txt
./gstats /tmp/single.txt
# Count: 1, Sum/Min/Max all 42, Mean: 42
```

### Tests
```bash
../../glyph test gstats.glyph
# Should show PASS for all test definitions
```

## What This Tests

| Feature | Validation |
|---------|-----------|
| **Named record types (gen=2)** | Stats and Config records generate `typedef struct` in C, field access uses `->` |
| **Extern wrapper generation** | `time` and `getenv` wrappers auto-generated from `extern_` table |
| **C string conversion** | `getenv` path requires `str_to_cstr` → extern → `cstr_to_str` |
| **Record construction** | `{count: n, sum: s, min_val: mn, max_val: mx}` creates typed struct |
| **Field access** | `stats.count`, `stats.sum`, `config.verbose` use struct `->` access |
| **Array processing** | Iterate array of numbers for statistics computation |
| **String processing** | File reading, line splitting, number parsing from strings |
| **Test framework** | Unit tests verify stat computation and parsing logic |
| **Standard workflow** | `init` → `put type` → `put fn` → `extern` → `build` → `run` → `test` |

## Constraints

- Do NOT hardcode input data. The tool must work on arbitrary files.
- Use `str_to_int` for parsing integers from lines (returns 0 on failure — treat 0 as a valid value if the line is "0", skip if line is empty/non-numeric).
- The `getenv` extern returns a raw pointer (0 for NULL). Check for 0 before calling `cstr_to_str`.
- Named type definitions must be inserted as `kind='type'` — the compiler matches these against record aggregates in MIR.
- Target: roughly 20-25 definitions. This is a focused tool, not a framework.
