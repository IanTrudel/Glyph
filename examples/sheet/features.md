# sheet.glyph -- Feature Ideas

## Low-hanging fruit

- **More functions** -- ROUND, FLOOR, CEIL, POW, SQRT, LEN (string length), CONCAT. Just new arms in `eval_fn_op` and `compile_atom`
- **Undo/redo** -- Keep a history stack of `(addr, old_raw)` pairs. `.undo` pops and restores
- **Cell formatting** -- Right now floats show raw; add fixed decimal places (e.g. `=ROUND(A1, 2)`)
- **Proper CSV quoting** -- Quote fields containing commas, handle quoted input on load

## Medium effort

- **Named ranges** -- `.name expenses A1:A5` then use `expenses` in formulas. A new HashMap from name to address pair
- **Sorting** -- `.sort A1:A5` or `.sort A1:C5 by B` to reorder rows by column value
- **Copy/fill** -- `.copy A1 B1:B10` to replicate a formula across a range (would need relative reference adjustment or a new `=R[-1]C` syntax)
- ~~**Column width auto-sizing** -- Scan values to compute per-column widths instead of fixed 10-char~~
- **Search/replace** -- `.find text` to locate cells, `.replace old new` across the sheet

## Bigger features

- **Dependency graph + incremental recalc** -- Track which cells reference which, topological sort, only recompute dirty cells. Would also enable circular reference detection with clear error messages
- **String formulas** -- Allow formulas to return strings, add CONCAT, LEFT, RIGHT, MID, UPPER, LOWER. Requires the VM to handle mixed float/string stack values
- **Conditional formatting** -- Color output based on cell values (ANSI escape codes). e.g. negative numbers in red
- **Multi-sheet tabs** -- `.sheet Sales`, `.sheet Expenses`, cross-sheet references like `Sales!A1`
- **Persistent storage** -- Save/load the sheet as a `.glyph` database table rather than CSV, preserving formulas and types natively

## Ambitious / showcase

- **Sparklines** -- `.spark A1:A12` renders a mini bar chart in the terminal using Unicode block characters
- **Pivot tables** -- `.pivot A1:D20 rows=A cols=B values=SUM(C)` for basic data summarization
- **Macro recording** -- Record a sequence of commands as a named macro, replay with `.run macro_name`
