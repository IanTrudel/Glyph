# sheet.glyph -- Command-Line Spreadsheet

A terminal spreadsheet application written in Glyph. Supports numeric and string
cells, formulas with cell references and ranges, CSV import/export, and an
interactive REPL with Unicode box-drawing grid display.

**Stats:** 97 definitions (1 type, 83 functions, 13 tests)

---

## Data Model

### Sheet Structure

```
Sheet = {raw: HashMap, vals: HashMap, types: HashMap}
```

Three parallel hashmaps keyed by canonical address string (e.g. `"A1"`, `"Z99"`):

| Map     | Stores                                                   |
|---------|----------------------------------------------------------|
| `raw`   | Original input string exactly as entered by the user     |
| `vals`  | Computed value -- `Float` for numbers/formulas, `String` for text |
| `types` | Cell type tag (integer)                                  |

### Cell Types

| Tag | Meaning  | `vals` content         |
|-----|----------|------------------------|
| 0   | Empty    | (absent)               |
| 1   | Number   | `Float` parsed from input |
| 2   | String   | The input string itself |
| 3   | Formula  | `Float` result of evaluation |

Type is inferred on assignment:
- Empty string -> type 0
- Starts with `=` -> type 3 (formula); the `=` prefix is stripped, remaining text is compiled and evaluated
- Passes `is_number_str` (digits, optional leading `-`, optional `.`) -> type 1
- Otherwise -> type 2 (string)

### Cell Addressing

Addresses are single uppercase letter A-Z (column) followed by row number 1-99.

```
Grid: A1 .. Z99   (26 columns x 99 rows)
```

**Packed integer encoding:** `col * 100 + row` where `col` is 0-25 (A-Z).
This allows address pairs to be passed through the bytecode as two floats.

| Function     | Purpose                            |
|--------------|------------------------------------|
| `pack_addr`  | `col * 100 + row`                  |
| `addr_col`   | `packed / 100`                     |
| `addr_row`   | `packed % 100`                     |
| `parse_addr` | String `"D15"` -> packed int `315` |
| `make_addr`  | `(col=3, row=15)` -> `"D15"`       |
| `col_to_idx` | ASCII char -> 0-25                 |
| `idx_to_col` | 0-25 -> `"A"`..`"Z"`              |

---

## Formula Engine

Formulas are compiled to a flat bytecode array (floats), then evaluated on a
stack machine. This separates parsing from execution and allows recalculation
without re-parsing.

### Pipeline

```
Input string  -->  tokenize_formula  -->  token array
                                            |
                                     compile_formula  -->  bytecode (Float array)
                                                             |
                                                      eval_formula  -->  Float result
```

### Tokenizer

`tokenize_formula` scans a formula string and produces an array of tagged string
tokens. Each token is a string with a single-character prefix denoting its kind:

| Prefix | Kind       | Examples                       |
|--------|------------|--------------------------------|
| `N`    | Number     | `N42`, `N3.14`                 |
| `R`    | Reference  | `RA1`, `RZ99`                  |
| `F`    | Function   | `FSUM`, `FAVG`, `FIF`          |
| `O`    | Operator   | `O+`, `O-`, `O*`, `O/`, `O%`  |
| `C`    | Comparison | `C>`, `C<`, `C>=`, `C<=`, `C==`, `C!=` |
| (none) | Punctuation| `(`, `)`, `,`, `:`             |

Identifiers followed by `(` are classified as functions (`F`); otherwise as
references (`R`). All identifiers are uppercased.

### Compiler (Recursive Descent)

The compiler is a Pratt-style recursive descent parser that emits bytecode.
Parser state is a mutable cursor (`state[0]`) into the token array.

**Precedence (low to high):**

1. Comparison operators (`>`, `<`, `>=`, `<=`, `==`, `!=`)
2. Addition / Subtraction (`+`, `-`)
3. Multiplication / Division / Modulo (`*`, `/`, `%`)
4. Unary minus (`-`)
5. Atoms: number literals, cell references, function calls, parenthesized expressions

Grammar:

```
expr     = add (CMP add)*
add      = mul (('+' | '-') mul)*
mul      = unary (('*' | '/' | '%') unary)*
unary    = '-' unary | atom
atom     = NUMBER | REF | REF ':' REF | FUNC '(' args ')' | '(' expr ')'
args     = expr (',' expr)* | empty
```

### Bytecode

Each instruction is a triple of floats `[opcode, val, arg]` (3 entries per
instruction in the flat array).

| Opcode | Name       | Val           | Arg          | Stack effect           |
|--------|------------|---------------|--------------|------------------------|
| 1      | PUSH_NUM   | float literal | -            | -> value               |
| 2      | PUSH_REF   | col           | row          | -> cell value          |
| 3      | ADD        | -             | -            | a b -> a+b             |
| 4      | SUB        | -             | -            | a b -> a-b             |
| 5      | MUL        | -             | -            | a b -> a*b             |
| 6      | DIV        | -             | -            | a b -> a/b (0 if b==0) |
| 7      | MOD        | -             | -            | a b -> a%b (integer)   |
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

| fn_id | Name    | Args              | Description                  |
|-------|---------|-------------------|------------------------------|
| 1     | SUM     | range             | Sum of cells in range        |
| 2     | AVG     | range             | Average (sum/count), 0 if empty |
| 3     | MIN     | range             | Minimum value in range       |
| 4     | MAX     | range             | Maximum value in range       |
| 5     | COUNT   | range             | Count of numeric/formula cells |
| 6     | ABS     | single value      | Absolute value               |
| 7     | IF      | cond, then, else  | Conditional (nonzero = true) |

Range functions (SUM, AVG, MIN, MAX, COUNT) iterate over a rectangular region
defined by two corner addresses. They only read cells of type 1 (number) or
type 3 (formula); string and empty cells contribute 0.0 / are skipped by COUNT.

### Evaluation

`eval_formula` runs the bytecode on a stack machine:
- Maintains a float stack (pre-allocated capacity 16)
- Iterates instructions by program counter (`pc`)
- Each instruction reads/pops operands from the stack and pushes its result
- Final result is the top of stack (or 0.0 if stack is empty)

### Recalculation

`sheet_recalc` iterates all keys in `raw`, recompiling and re-evaluating every
formula cell (type 3). This is a brute-force full recalc with no dependency
tracking -- simple but correct for acyclic graphs. Circular references will
produce stale or zero values (no cycle detection).

---

## REPL Interface

### Input Parsing

The REPL (`repl_loop`) reads lines and classifies them:

| Input shape            | Action                                            |
|------------------------|---------------------------------------------------|
| `.command ...`         | Dot-command (see below)                           |
| `ADDR = value`         | Assignment: parse address, set cell, recalc       |
| `ADDR` (bare)          | Query: print the cell's display value             |
| anything else          | "Unknown command" error                           |

Assignment uses ` = ` (space-equals-space) as the delimiter. The address part
is uppercased and parsed; the value part is passed to `sheet_set`.

### Dot Commands

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
| `.count A1:A5`       | Print count of numeric cells in range    |
| `.quit` / `.exit`    | Exit the program                         |

### Grid Display

The grid renderer draws a Unicode box-drawing table:

```
┌────────────┬────────────┬────────────┐
│            │          A │          B │
├────────────┼────────────┼────────────┤
│          1 │         10 │      hello │
│          2 │         20 │            │
└────────────┴────────────┴────────────┘
```

- Fixed cell width of 10 characters
- Numbers and formula results are right-aligned
- Strings are left-aligned
- Empty cells display as blank
- `.show` with no argument auto-detects bounds by scanning all occupied keys
- Uses `sb_new`/`sb_append`/`sb_build` string builder for efficient output

### CSV Import/Export

**Save** (`.save`): finds bounding rectangle of occupied cells, writes one row
per line with comma-separated display values (via `get_cell_str`).

**Load** (`.load`): clears the sheet, reads the file, splits by newlines then
commas. Each non-empty value is assigned to the corresponding cell via
`sheet_set` (so formulas starting with `=` are re-evaluated on load). Row 1 maps
to CSV line 1, column A maps to field 0.

---

## Utility Functions

| Function           | Purpose                                |
|--------------------|----------------------------------------|
| `is_alpha`         | ASCII letter check (A-Z, a-z)         |
| `is_digit`         | ASCII digit check (0-9)               |
| `is_number_str`    | Validate numeric string (optional `-`, digits, optional `.`) |
| `to_upper`         | Lowercase to uppercase ASCII           |
| `pad_left`         | Right-align string to width            |
| `pad_right`        | Left-align string to width             |
| `repeat_str`       | Repeat a string N times                |
| `format_val`       | Float to string via `float_to_str`     |

---

## Test Suite

13 tests covering all major subsystems:

| Test                   | Covers                                      |
|------------------------|---------------------------------------------|
| `test_parse_addr`      | A1, Z99, C10 parsing                        |
| `test_make_addr`       | Index-to-string address construction         |
| `test_addr_roundtrip`  | parse -> unpack -> make round-trip           |
| `test_tokenize`        | `A1+42` -> `[RA1, O+, N42]`                 |
| `test_tokenize_fn`     | `SUM(A1:A3)` -> `[FSUM, (, RA1, :, RA3, )]` |
| `test_eval_num`        | Bare formula `=42` evaluates to 42.0         |
| `test_eval_arith`      | Cell reference arithmetic `=A1+A2`           |
| `test_eval_mul`        | Operator precedence `=2+3*4` -> 14.0         |
| `test_eval_float`      | Float arithmetic, division, AVG              |
| `test_eval_ref`        | Cross-cell reference `=A1*2`                 |
| `test_eval_sum`        | `SUM(A1:A3)` over range                      |
| `test_eval_if`         | `IF(A1>40, 1, 0)` conditional                |
| `test_recalc`          | Changing A1 propagates to formula in A2       |

---

## Limitations

- **Single-letter columns only:** A-Z (26 columns max)
- **No cycle detection:** circular references silently produce incorrect values
- **Brute-force recalc:** every formula is recompiled and re-evaluated on every change; no dependency graph
- **No relative/absolute references:** all references are absolute (no `$` notation, no copy-adjust)
- **No string formulas:** formulas always produce floats; string concatenation or lookup not supported
- **Simple CSV:** no quoting, no escaped commas; fields containing commas will break import/export
- **Integer modulo:** `%` operator truncates to integer before computing remainder
