# sheet.glyph -- Command-Line Spreadsheet

A terminal-based spreadsheet application written in Glyph, featuring a formula
compiler that targets a custom bytecode VM, interactive REPL with Unicode grid
rendering, and CSV import/export. This is one of the largest and most
architecturally rich Glyph example programs.

**File:** `examples/sheet/sheet.glyph` (229 KB SQLite database, 128 KB compiled binary)

## Statistics

| Metric              | Value |
|---------------------|-------|
| Total definitions   | 107   |
| Functions (`fn`)    | 88    |
| Unit tests (`test`) | 13    |
| Property tests (`prop`) | 5 |
| Type definitions    | 1     |
| Total tokens        | 9,905 |
| Total source chars  | 29,390 |

## Library Dependencies

- **scan.glyph** (scanner combinators) -- linked into the database. The tokenizer
  uses `sc_skip_ws`, `sc_take`, `cs_digit`, `cs_alnum`, `scan_cset_union`, and
  `scan_cset_from_str` for lexical scanning.

No extern declarations or FFI C files are required.

---

## Architecture Overview

The program is structured in six layers:

```
                    +-----------+
                    |   REPL    |  main, repl_loop, handle_input
                    +-----+-----+
                          |
              +-----------+-----------+
              |                       |
        +-----+-----+          +-----+-----+
        |  Commands  |          | Cell Ops  |  sheet_set, sheet_recalc
        +-----+-----+          +-----+-----+
              |                       |
              |                 +-----+-----+
              |                 |  Formula   |  tokenize -> compile -> eval
              |                 |  Engine    |
              |                 +-----+-----+
              |                       |
        +-----+-----+          +-----+-----+
        |  Grid/CSV  |         | Bytecode   |  eval_loop, stack machine
        |  Renderer  |         |    VM      |
        +------------+         +------------+
```

---

## Data Model

### Sheet Type

```glyph
Sheet = {raw: I, types: I, vals: I}
```

Three parallel HashMaps (opaque `I` handles), all keyed by canonical cell
address string (e.g. `"A1"`, `"Z99"`):

| Field   | Stores                                                   |
|---------|----------------------------------------------------------|
| `raw`   | Original input string exactly as entered by the user     |
| `vals`  | Computed value -- `Float` for numbers/formulas, `String` for text |
| `types` | Cell type tag (integer)                                  |

### Cell Types

| Tag | Meaning  | `vals` content            |
|-----|----------|---------------------------|
| 0   | Empty    | (absent)                  |
| 1   | Number   | `Float` parsed from input |
| 2   | String   | The input string itself   |
| 3   | Formula  | `Float` result of eval    |

Type classification (`classify_val`):
- Empty string -> type 0
- Starts with `=` -> type 3 (formula); the `=` prefix is stripped before compilation
- Passes `is_number_str` validation -> type 1
- Otherwise -> type 2 (string)

### Cell Addressing

Grid spans A1 through Z99 (26 columns x 99 rows). Addresses are a single
uppercase letter followed by a 1-2 digit row number.

**Packed integer encoding:** `col * 100 + row` where col is 0-25 (A=0, Z=25).
This packs a 2D address into a single float for bytecode transport.

| Function     | Signature           | Purpose                            |
|--------------|---------------------|------------------------------------|
| `pack_addr`  | `col row -> I`      | `col * 100 + row`                  |
| `addr_col`   | `packed -> I`       | `packed / 100`                     |
| `addr_row`   | `packed -> I`       | `packed % 100`                     |
| `parse_addr` | `S -> I`            | `"D15"` -> packed int `315`        |
| `make_addr`  | `col row -> S`      | `(3, 15)` -> `"D15"`              |
| `col_to_idx` | `ch -> I`           | ASCII char code -> 0-25            |
| `idx_to_col` | `I -> S`            | 0-25 -> `"A"`..`"Z"`              |

---

## Formula Engine

The formula engine is a three-stage pipeline that compiles spreadsheet formulas
into bytecode and evaluates them on a stack machine. This design separates
parsing from execution, enabling recalculation without re-parsing.

### Pipeline

```
"=A1+B2*3"
     |
     v
tokenize_formula()     -- scan.glyph combinators
     |                    produces tagged token array
     v
["RA1", "O+", "RB2", "O*", "N3"]
     |
     v
compile_formula()      -- recursive descent, Pratt-style precedence
     |                    emits instruction triples to flat array
     v
[2,0,1, 2,1,2, 1,3,0, 5,0,0, 3,0,0]    -- bytecode (floats)
     |
     v
eval_loop()            -- stack machine interpreter
     |                    reads cells from Sheet for references
     v
9.0                    -- final result
```

### Stage 1: Tokenizer

`tokenize_formula` scans the formula string using scanner combinators from
`scan.glyph` and produces an array of tagged string tokens. The tag is a
single-character prefix:

| Prefix | Kind        | Examples                         |
|--------|-------------|----------------------------------|
| `N`    | Number      | `N42`, `N3.14`                   |
| `R`    | Reference   | `RA1`, `RZ99`                    |
| `F`    | Function    | `FSUM`, `FAVG`, `FIF`            |
| `O`    | Operator    | `O+`, `O-`, `O*`, `O/`, `O%`    |
| `C`    | Comparison  | `C>`, `C<`, `C>=`, `C<=`, `C==`, `C!=` |
| (none) | Punctuation | `(`, `)`, `,`, `:`               |

**Implementation details:**

- `tok_loop` is the main dispatch -- whitespace is skipped via `sc_skip_ws`,
  then the first character determines the token kind
- `tok_num` uses `sc_take` with a digit+dot character set to consume number literals
- `tok_id` uses `sc_take` with `cs_alnum`, then peeks ahead: if followed by `(`
  the token is tagged `F` (function), otherwise `R` (reference). All identifiers
  are uppercased
- `tok_cmp` handles two-character comparison operators with lookahead

### Stage 2: Compiler (Recursive Descent)

The compiler is a Pratt-style recursive descent parser that walks the token
array and emits bytecode instructions. Parser state is a mutable cursor
(single-element array `state[0]`) advanced by `advance()`.

**Precedence levels (low to high):**

1. Comparison operators (`>`, `<`, `>=`, `<=`, `==`, `!=`)
2. Addition / Subtraction (`+`, `-`)
3. Multiplication / Division / Modulo (`*`, `/`, `%`)
4. Unary minus (`-`)
5. Atoms: number literals, cell references, ranges, function calls, parenthesized subexpressions

**Grammar:**

```
expr     = add (CMP add)*
add      = mul (('+' | '-') mul)*
mul      = unary (('*' | '/' | '%') unary)*
unary    = '-' unary | atom
atom     = NUMBER | REF | REF ':' REF | FUNC '(' args ')' | '(' expr ')'
args     = expr (',' expr)* | empty
```

**Compiler functions:**

| Function            | Role                                         |
|---------------------|----------------------------------------------|
| `compile_formula`   | Entry point; initializes cursor, calls `compile_expr` |
| `compile_expr`      | Delegates to `compile_add`, loops via `compile_cmp_loop` |
| `compile_add`       | Delegates to `compile_mul`, loops via `compile_add_loop` |
| `compile_mul`       | Delegates to `compile_unary`, loops via `compile_mul_loop` |
| `compile_unary`     | Handles `-` prefix, otherwise calls `compile_atom` |
| `compile_atom`      | Dispatches on token kind: N, R, F, `(`       |
| `compile_args`      | Parses comma-separated argument list for function calls |

### Stage 3: Bytecode

Each instruction is a triple of floats `[opcode, val, arg]` -- 3 entries per
instruction in a flat array. Bytecode is emitted by `emit` (integer values) and
`emit_f` (float values).

| Opcode | Name       | Val           | Arg          | Stack Effect           |
|--------|------------|---------------|--------------|------------------------|
| 1      | PUSH_NUM   | float literal | -            | -> value               |
| 2      | PUSH_REF   | col           | row          | -> cell value          |
| 3      | ADD        | -             | -            | a b -> a+b             |
| 4      | SUB        | -             | -            | a b -> a-b             |
| 5      | MUL        | -             | -            | a b -> a*b             |
| 6      | DIV        | -             | -            | a b -> a/b (0 if b==0) |
| 7      | MOD        | -             | -            | a b -> a%b (truncated) |
| 8      | NEG        | -             | -            | a -> -a                |
| 9      | CALL_FN    | fn_id         | argc         | args.. -> result       |
| 10     | PUSH_RANGE | packed addr1  | packed addr2 | -> addr1 addr2         |
| 11     | CMP        | cmp_type      | -            | a b -> 0.0 or 1.0      |

**Comparison types** (opcode 11, val field):

| Val | Operator |
|-----|----------|
| 1   | `>`      |
| 2   | `<`      |
| 3   | `>=`     |
| 4   | `<=`     |
| 5   | `==`     |
| 6   | `!=`     |

### Built-in Functions

| fn_id | Name    | Args              | Description                      |
|-------|---------|-------------------|----------------------------------|
| 1     | SUM     | range             | Sum of numeric cells in range    |
| 2     | AVG     | range             | Average (sum/count), 0 if empty  |
| 3     | MIN     | range             | Minimum value in range           |
| 4     | MAX     | range             | Maximum value in range           |
| 5     | COUNT   | range             | Count of numeric/formula cells   |
| 6     | ABS     | single value      | Absolute value                   |
| 7     | IF      | cond, then, else  | Conditional (nonzero = true)     |

### Evaluation (Stack Machine)

`eval_formula` orchestrates the full pipeline: tokenize, compile, then run
`eval_loop` on the bytecode with a pre-allocated float stack (capacity 16).

`eval_loop` iterates by program counter (`pc`), reading instruction triples.
It dispatches through `eval_op` -> `eval_op2` for arithmetic and control,
`eval_cmp` for comparison operators, and `eval_fn_op` for function calls.

**Key implementation patterns:**

- Stack operations: pop two operands for binary ops, push result, recurse with `pc + 1`
- Cell reference resolution: `get_cell_float(sheet, make_addr(col, row))` reads
  the computed value from the sheet's `vals` map
- Division by zero: returns 0.0 silently
- Range functions: `eval_range_fn` unpacks two addresses and delegates to
  specialized 2D iteration functions (column-major outer loop, row-major inner):
  - `eval_range_sum` / `_c` / `_r` -- accumulate sum
  - `eval_range_count` / `_c` / `_r` -- count numeric cells (type 1 or 3)
  - `eval_range_min` / `_c` / `_r` -- track minimum
  - `eval_range_max` / `_c` / `_r` -- track maximum

### Recalculation

`sheet_recalc` iterates all keys in `raw`, and for every formula cell (type 3),
strips the leading `=`, runs `eval_formula`, and stores the result in `vals`.

This is a brute-force full recalc with no dependency graph -- simple and correct
for acyclic formulas. Circular references silently produce stale or zero values.

---

## Sheet Operations

| Function               | Purpose                                      |
|------------------------|----------------------------------------------|
| `sheet_new`            | Create empty Sheet with 3 fresh HashMaps     |
| `sheet_set`            | Classify value, store in all 3 maps, trigger recalc |
| `sheet_set_no_recalc`  | Set without recalc (used during CSV load)    |
| `sheet_recalc`         | Full recompile/re-evaluate of all formulas   |
| `sheet_clear_cell`     | Delete a single cell from all 3 maps         |
| `sheet_clear_all`      | Clear entire sheet (iterates all keys)       |
| `get_cell_float`       | Read Float value (0.0 if absent)             |
| `get_cell_str`         | Read display string (formatted float or raw) |
| `format_val`           | Float to string, strips trailing zeros       |

---

## REPL Interface

### Entry Point

```glyph
main =
  sheet = sheet_new
  println("sheet.glyph - type .help for commands")
  repl_loop(sheet)
```

### Input Classification

`handle_input` classifies each line by its first character and structure:

| Input Shape            | Detection               | Action                    |
|------------------------|-------------------------|---------------------------|
| `.command args`        | First char is `.` (46)  | `handle_dot` dispatches   |
| `ADDR = value`         | Contains ` = `          | `handle_assign`           |
| `ADDR` (bare)          | Everything else         | `handle_query`            |

### Dot Commands

`handle_dot` splits on the first space to extract command and argument, then
matches:

| Command              | Description                              |
|----------------------|------------------------------------------|
| `.help`              | Show usage help                          |
| `.show`              | Display grid of all occupied cells       |
| `.show A1:C5`        | Display grid for a specific region       |
| `.clear`             | Clear all cells                          |
| `.clear A1`          | Clear a single cell                      |
| `.save file.csv`     | Export occupied region to CSV            |
| `.load file.csv`     | Import CSV (clears sheet first)          |
| `.sum A1:A5`         | Print sum of range                       |
| `.avg A1:A5`         | Print average of range                   |
| `.min A1:A5`         | Print minimum of range                   |
| `.max A1:A5`         | Print maximum of range                   |
| `.count A1:A5`       | Print count of numeric cells             |
| `.quit` / `.exit`    | Exit the program                         |

### Grid Renderer

`show_grid` draws a Unicode box-drawing table using `sb_new`/`sb_append`/`sb_build`
for efficient string construction:

```
+------------+------------+------------+
|            |          A |          B |
+------------+------------+------------+
|          1 |         10 |      hello |
|          2 |         20 |            |
+------------+------------+------------+
```

(Actual output uses Unicode box-drawing characters: `+` = corner/junction glyphs,
`-` = `─`, `|` = `│`)

**Rendering functions:**

| Function           | Role                                          |
|--------------------|-----------------------------------------------|
| `show_grid`        | Entry: builds separator/header/rows/footer    |
| `render_sep`       | Horizontal separator with configurable corners |
| `render_sep_loop`  | Column iteration for separator cells          |
| `render_header`    | Column labels (A, B, C...)                    |
| `render_hdr_cols`  | Column iteration for header labels            |
| `render_rows`      | Row iteration loop                            |
| `render_row`       | Single row with row number and cell values    |
| `render_row_cols`  | Column iteration within a row                 |
| `find_bounds`      | Auto-detect bounding rectangle of occupied cells |
| `find_bounds_loop` | Iteration over all keys to find min/max col/row |

**Display rules:**
- Fixed cell width: 10 characters
- Numbers and formula results: right-aligned (`pad_left`)
- Strings: left-aligned (`pad_right`)
- Empty cells: blank

---

## CSV Import/Export

### Save (`.save filename.csv`)

`csv_save` finds the bounding rectangle via `find_bounds`, then iterates
rows and columns, writing comma-separated display values using `get_cell_str`.
Output is assembled with a StringBuilder and written with `write_file`.

### Load (`.load filename.csv`)

`csv_load` reads the entire file, clears the sheet, splits by newlines then
by commas. Each non-empty field is assigned to the corresponding cell using
`sheet_set_no_recalc` (skipping per-cell recalc for performance). A single
`sheet_recalc` runs after all cells are loaded.

Row 1 maps to CSV line 1, column A maps to field 0. Formula cells (values
starting with `=`) are re-evaluated on load.

### Sample Data

The included `sample.csv` demonstrates a personal budget tracker:

```
Item,Jan,Feb,Mar,Total
Rent,1200,1200,1200,=SUM(B2:D2)
Food,350,410,380,=SUM(B3:D3)
...
Total,=SUM(B2:B5),=SUM(C2:C5),=SUM(D2:D5),=SUM(E2:E5)
Average,=AVG(B2:B5),=AVG(C2:C5),=AVG(D2:D5),=E6/4
```

This exercises SUM, AVG, MIN, MAX, COUNT range functions and cross-cell
formula references.

---

## Utility Functions

| Function           | Purpose                                           |
|--------------------|---------------------------------------------------|
| `is_number_str`    | Validate numeric string (optional `-`, digits, optional `.`) |
| `is_number_loop`   | Character-by-character validation loop            |
| `classify_val`     | Determine cell type tag from input string         |
| `format_val`       | Float to string with trailing zero removal        |
| `strip_zeros`      | Entry point for trailing zero stripping           |
| `strip_zeros_loop` | Iterative scan from end of string                 |
| `parse_range_arg`  | Parse `"A1:C5"` into `[c1, r1, c2, r2]` array    |

---

## Test Suite

### Unit Tests (13)

| Test                   | Covers                                           |
|------------------------|--------------------------------------------------|
| `test_parse_addr`      | Address parsing: A1, Z99, C10                    |
| `test_make_addr`       | Index-to-string address construction             |
| `test_addr_roundtrip`  | parse -> unpack -> make round-trip identity       |
| `test_tokenize`        | Simple expression: `A1+42` -> `[RA1, O+, N42]`  |
| `test_tokenize_fn`     | Function call: `SUM(A1:A3)` -> `[FSUM, (, RA1, :, RA3, )]` |
| `test_eval_num`        | Bare literal: `=42` evaluates to 42.0            |
| `test_eval_arith`      | Cell reference arithmetic: `=A1+A2`              |
| `test_eval_mul`        | Operator precedence: `=2+3*4` -> 14.0            |
| `test_eval_float`      | Float arithmetic, division, AVG function          |
| `test_eval_ref`        | Cross-cell reference: `=A1*2`                    |
| `test_eval_sum`        | Range function: `SUM(A1:A3)` over 3 cells        |
| `test_eval_if`         | Conditional: `IF(A1>40, 1, 0)`                   |
| `test_recalc`          | Changing A1 propagates to dependent formula in A2 |

### Property-Based Tests (5)

Property tests use `seed_range` and `seed_next` for deterministic random value
generation across multiple iterations:

| Property                 | Invariant                                       |
|--------------------------|-------------------------------------------------|
| `prop_pack_roundtrip`    | `addr_col(pack_addr(c,r)) == c && addr_row(pack_addr(c,r)) == r` |
| `prop_addr_roundtrip`    | `parse_addr(make_addr(c,r)) == pack_addr(c,r)`  |
| `prop_tokenize_nonempty` | Tokenizing any integer string produces at least one token |
| `prop_eval_numeric`      | Evaluating a bare integer formula returns that integer |
| `prop_add_commutative`   | `eval(a+b) == eval(b+a)` for random a, b        |

---

## Complete Function Inventory

### Data Model (16 functions)

| Function           | Tokens | Description                              |
|--------------------|--------|------------------------------------------|
| `sheet_new`        | 25     | Create empty Sheet with 3 HashMaps       |
| `sheet_set`        | 157    | Set cell value with auto-classification and recalc |
| `sheet_set_no_recalc` | 95  | Set cell without triggering recalc       |
| `sheet_recalc`     | 26     | Full recalculation of all formula cells  |
| `recalc_loop`      | 135    | Iteration over keys for recalc           |
| `sheet_clear_cell` | 37     | Delete one cell from all maps            |
| `sheet_clear_all`  | 27     | Clear entire sheet                       |
| `sheet_clear_loop` | 80     | Key iteration for clear_all              |
| `get_cell_float`   | 60     | Read numeric value (0.0 default)         |
| `get_cell_str`     | 69     | Read display string                      |
| `classify_val`     | 58     | Determine cell type from input           |
| `pack_addr`        | 10     | Encode col,row as single int             |
| `addr_col`         | 9      | Extract column from packed address       |
| `addr_row`         | 9      | Extract row from packed address          |
| `parse_addr`       | 62     | String address to packed int             |
| `make_addr`        | 20     | Col,row to string address                |

### Tokenizer (6 functions)

| Function            | Tokens | Description                             |
|---------------------|--------|-----------------------------------------|
| `tokenize_formula`  | 29     | Entry: create token array, start loop   |
| `tok_loop`          | 270    | Main dispatch on character class        |
| `tok_num`           | 93     | Consume number literal with scanner     |
| `tok_id`            | 132    | Consume identifier, classify F vs R     |
| `tok_cmp`           | 294    | Handle comparison operators with lookahead |
| `col_to_idx`        | 21     | ASCII to column index                   |

### Compiler (8 functions)

| Function            | Tokens | Description                             |
|---------------------|--------|-----------------------------------------|
| `compile_formula`   | 51     | Entry: init cursor, delegate to expr    |
| `compile_expr`      | 33     | Top-level: add with comparison loop     |
| `compile_add`       | 33     | Addition level: mul with +/- loop       |
| `compile_add_loop`  | 125    | Left-associative + and - emission       |
| `compile_mul`       | 33     | Multiplication level: unary with */% loop |
| `compile_mul_loop`  | 174    | Left-associative *, /, % emission       |
| `compile_cmp_loop`  | 321    | Comparison operator emission            |
| `compile_unary`     | 78     | Unary minus handling                    |
| `compile_atom`      | 400    | Number/ref/range/function/paren atoms   |
| `compile_args`      | 93     | Comma-separated argument list           |

### Bytecode Emission (2 functions)

| Function | Tokens | Description                                |
|----------|--------|--------------------------------------------|
| `emit`   | 45     | Emit instruction triple (integer values)   |
| `emit_f` | 41     | Emit instruction triple (float val)        |

### Parser State (2 functions)

| Function  | Tokens | Description                               |
|-----------|--------|-------------------------------------------|
| `cur_tok` | 34     | Read current token at cursor position     |
| `advance` | 18     | Increment cursor in state array           |

### VM / Evaluation (7 functions)

| Function       | Tokens | Description                              |
|----------------|--------|------------------------------------------|
| `eval_formula`  | 59    | Full pipeline: tokenize -> compile -> eval |
| `eval_loop`     | 109   | Main bytecode interpreter loop           |
| `eval_op`       | 331   | Opcodes 1-5 (push_num, push_ref, add, sub, mul) |
| `eval_op2`      | 443   | Opcodes 6-11 (div, mod, neg, call_fn, push_range, cmp) |
| `eval_cmp`      | 113   | Comparison dispatch (6 operators)        |
| `eval_fn_op`    | 298   | Function call dispatch (ABS, IF, range fns) |
| `eval_range_fn` | 170   | Range function dispatch (SUM, AVG, MIN, MAX, COUNT) |

### Range Iteration (12 functions)

| Function              | Tokens | Description                        |
|-----------------------|--------|------------------------------------|
| `eval_range_sum`      | 29     | SUM entry                          |
| `eval_range_sum_c`    | 68     | SUM column loop                    |
| `eval_range_sum_r`    | 64     | SUM row loop (accumulate)          |
| `eval_range_count`    | 30     | COUNT entry                        |
| `eval_range_count_c`  | 68     | COUNT column loop                  |
| `eval_range_count_r`  | 103    | COUNT row loop (type check)        |
| `eval_range_min`      | 46     | MIN entry (init to large value)    |
| `eval_range_min_c`    | 68     | MIN column loop                    |
| `eval_range_min_r`    | 80     | MIN row loop                       |
| `eval_range_max`      | 46     | MAX entry (init to small value)    |
| `eval_range_max_c`    | 68     | MAX column loop                    |
| `eval_range_max_r`    | 80     | MAX row loop                       |

### REPL & Commands (9 functions)

| Function        | Tokens | Description                             |
|-----------------|--------|-----------------------------------------|
| `main`          | 28     | Entry point: create sheet, start REPL   |
| `repl_loop`     | 84     | Read-eval-print loop with prompt        |
| `handle_input`  | 148    | Classify input as dot/assign/query      |
| `handle_assign` | 74     | Parse address, set cell value           |
| `handle_query`  | 154    | Look up and display cell value          |
| `handle_dot`    | 232    | Dispatch dot-commands                   |
| `cmd_show`      | 99     | `.show` with optional range argument    |
| `cmd_clear`     | 79     | `.clear` with optional cell address     |
| `cmd_range`     | 74     | `.sum`/`.avg`/`.min`/`.max`/`.count`    |
| `cmd_help`      | 213    | Print usage help text                   |

### Grid Rendering (10 functions)

| Function           | Tokens | Description                          |
|--------------------|--------|--------------------------------------|
| `show_grid`        | 152    | Build and print complete grid        |
| `render_sep`       | 59     | Horizontal separator line            |
| `render_sep_loop`  | 60     | Column iteration for separator       |
| `render_header`    | 64     | Column label row                     |
| `render_hdr_cols`  | 75     | Column iteration for header          |
| `render_rows`      | 57     | Row iteration loop                   |
| `render_row`       | 73     | Single data row                      |
| `render_row_cols`  | 141    | Column iteration within row          |
| `find_bounds`      | 139    | Detect bounding rectangle            |
| `find_bounds_loop` | 202    | Key iteration for bounds detection   |

### CSV I/O (6 functions)

| Function           | Tokens | Description                          |
|--------------------|--------|--------------------------------------|
| `csv_load`         | 62     | Load CSV: read, clear, parse, recalc |
| `csv_load_lines`   | 93     | Line iteration loop                  |
| `csv_load_fields`  | 99     | Field iteration within a line        |
| `csv_save`         | 96     | Save CSV: find bounds, write         |
| `csv_save_rows`    | 69     | Row iteration for save               |
| `csv_save_cols`    | 86     | Column iteration for save            |

### Utilities (7 functions)

| Function           | Tokens | Description                          |
|--------------------|--------|--------------------------------------|
| `is_number_str`    | 77     | Validate numeric string format       |
| `is_number_loop`   | 109    | Character iteration for validation   |
| `format_val`       | 38     | Float to display string              |
| `strip_zeros`      | 20     | Strip trailing zeros entry           |
| `strip_zeros_loop` | 88     | Iterative scan from end              |
| `idx_to_col`       | 12     | Column index to letter               |
| `parse_range_arg`  | 223    | Parse "A1:C5" to coordinate array    |

---

## Design Decisions

1. **Three-stage formula engine.** Tokenizing, compiling, and evaluating are
   independent phases. This lets recalculation re-run bytecode without
   re-tokenizing, and makes each phase independently testable.

2. **Bytecode VM over tree-walking.** A flat float array is simpler to
   construct and iterate than an AST. The 3-float instruction format wastes
   some space (unused fields) but keeps the interpreter loop trivial.

3. **Three parallel HashMaps.** Storing raw input, computed values, and type
   tags separately avoids variant types. The tradeoff is three synchronized
   map operations per cell update, but the code is straightforward.

4. **Packed address encoding.** Encoding `(col, row)` as `col * 100 + row`
   lets addresses pass through bytecode as single floats. The 100-multiplier
   provides room for up to 99 rows per column.

5. **Brute-force recalculation.** Every formula is recompiled and re-evaluated
   on every cell change. No dependency tracking means no topological sort,
   no cycle detection, and no incremental update -- but also no bugs from
   stale dependency graphs. Correct for the expected sheet sizes.

6. **Scanner combinators for tokenization.** Reusing `scan.glyph` (character
   sets, `sc_take`, `sc_skip_ws`) instead of writing custom character-level
   scanning reduces code and demonstrates library composition.

7. **Recursive descent + iterative loops.** The compiler uses recursion for
   precedence levels and iterative tail calls for left-associative operator
   chains (`compile_add_loop`, `compile_mul_loop`). This avoids deep recursion
   for long expressions.

8. **StringBuilder for rendering.** Grid output is assembled in a StringBuilder
   rather than printed line-by-line, producing a single `print` call. This
   eliminates visual tearing and is more efficient.

---

## Limitations

- **Single-letter columns only:** A-Z (26 columns max)
- **No cycle detection:** Circular formula references silently produce incorrect values
- **Brute-force recalc:** O(n) full recompile on every change; no dependency graph
- **No relative/absolute references:** All references are absolute (no `$` notation)
- **No string formulas:** Formulas always produce floats; no string operations
- **Simple CSV:** No quoting or escaping; embedded commas break import/export
- **Integer modulo:** `%` operator truncates operands to integer before remainder
- **Stack depth:** Fixed stack capacity of 16 floats; deeply nested expressions
  could overflow

---

## Building and Running

```bash
# Build (requires scan.glyph linked into sheet.glyph)
./glyph build examples/sheet/sheet.glyph examples/sheet/sheet

# Run
./examples/sheet/sheet

# Run tests
./glyph test examples/sheet/sheet.glyph

# Interactive session example:
> A1 = 10
> A2 = 20
> A3 = =A1+A2
> A3
30
> .show
> .load sample.csv
> .show
> .quit
```
