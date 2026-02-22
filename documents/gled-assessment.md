# Gled Assessment: Can Glyph Build an Interactive Application?

*Analysis of building `gled` (a terminal text editor) in Glyph using ncurses FFI. February 2026.*

## The Experiment

A fresh Claude Code session was given `spec-gled.md` and asked to implement a minimal ncurses text editor. The session had access to the `glyph-dev` skill docs but no prior Glyph experience. This was run *after* the string operator fixes (`+`, `==`, `!=` now work on strings) and the SIGSEGV handler (prints function name on crash).

The spec required a 45-55 definition interactive editor with: file load/save, cursor movement, character insertion/deletion, line splitting/joining, scrolling viewport, status bar, and ncurses FFI bindings. This exercises sustained interactivity, heavy array mutation, real FFI, and string surgery on every keystroke — a fundamentally different challenge from glint's batch-mode analysis.

## Results

**It works.** 34 definitions, fully functional text editor. Opens files, navigates, edits, saves, quits. Confirmed working to spec by the user.

| Metric | Value |
|--------|-------|
| Definitions written | 34 |
| Spec estimate | 45-55 |
| C wrapper functions | 7 |
| Extern table entries | 0 |
| `glyph build` attempts | ~159 (across 3 sessions) |
| `glyph put` commands | ~62 |
| Segfaults encountered | ~41 |
| Total lines of Glyph | 317 |
| Avg lines/definition | 9 |
| Binary size | 38k |
| User corrections | Confirmed working, no corrections noted |

## What Worked

### The Event Loop Pattern Is Natural

The recursive event loop pattern — threading all state as parameters — works beautifully for interactive applications:

```
event_loop buf cx cy top filename modified quit_pressed rows cols =
  draw_screen(buf, cx, cy, top, rows, cols, filename, modified)
  key = nc_getch(0)
  dispatch_key(buf, cx, cy, top, filename, modified, quit_pressed, rows, cols, key)
```

This is the same pattern used in `life.glyph` for X11 events. Each keystroke dispatches through `dispatch_key → dispatch_key2 → dispatch_key3 → dispatch_key4`, updates state, and tail-calls back into `event_loop`. 9 parameters, 10 with the key code in dispatch. Glyph handles this naturally.

### Buffer Operations Are Clean

The buffer model (array of strings, one per line) maps directly to Glyph's arrays. Line editing via `str_slice` + `str_concat` is concise:

```
buf_insert_char buf cy cx ch =
  line = buf[cy]
  left = str_slice(line, 0, cx)
  right = str_slice(line, cx, str_len(line))
  new_line = s3(left, ch, right)
  array_set(buf, cy, new_line)
  buf
```

Every buffer operation — insert char, delete char, split line, join lines — is 5-8 lines. No algorithm bugs in any of them.

### The C Wrapper Pattern Scales

The session chose to put ALL ncurses calls in `nc_wrapper.c` (7 wrapper functions) rather than using the `extern_` table for most functions as the spec suggested. This is pragmatically smarter — it avoids the `str_to_cstr` conversion overhead for every `mvaddstr` call by handling it in C, and sidesteps questions about which ncurses functions are macros vs real functions.

The wrapper includes `nc_char_to_str` which converts an integer keycode to a Glyph string struct — a creative solution for character insertion that avoids needing a Glyph-side int-to-char function.

### String Operator Fixes Were Transparent

The generated C code uses `glyph_str_concat` (5 occurrences) with zero raw `+` on strings. The `s2`/`s3` helper pattern is used throughout. The session wrote natural concatenation code without needing to work around operator bugs — exactly the improvement the fixes were intended to provide.

### SIGSEGV Handler Was Available

Every generated function includes `_glyph_current_fn = "name";` at entry. With 41 segfaults during development, the handler would have printed the crashing function name each time, significantly aiding debugging compared to the raw exit-code-139 experience in glint.

### Fewer Definitions Than Estimated

34 definitions vs the spec's 45-55 estimate. The session was more efficient by:
- Using the C wrapper for character conversion (`nc_char_to_str`) instead of implementing it in Glyph
- Combining related logic (no separate `handle_char`/`handle_enter`/etc. — all inline in dispatch)
- Using `sb_new`/`sb_append`/`sb_build` for `join_lines` and `make_spaces` (O(n) string building)

## What Was Hard

### Significantly More Builds Than Glint

159 builds vs glint's 22 — a 7x increase. Part of this is inherent complexity (interactive app vs batch tool), but it also reflects the difficulty of debugging an ncurses application that takes over the terminal. You can't add `println` debug statements when the screen is controlled by ncurses.

### 41 Segfaults (4x Glint's 10)

The higher segfault count reflects:
1. More complex data flow (9-parameter recursive loop vs glint's simpler iteration)
2. Array mutation at scale (shift_up/shift_down for line insertion/deletion)
3. FFI boundary crossings (Glyph string structs ↔ C char pointers)
4. The inherent difficulty of testing interactive programs incrementally

### The Dispatch Chain Is Deep

Key handling required 4 chained functions (`dispatch_key` through `dispatch_key4`) with 10 parameters each. This is a consequence of Glyph's match-based control flow — each match branch can only hold so much nesting before readability collapses. The largest definition (`dispatch_key2`) is 48 lines.

In a language with `switch` statements or a flat pattern-match syntax, this would be one function. In Glyph, the nested `match` chains force splitting across multiple definitions. This is a real ergonomic cost for any program with many dispatch cases.

### No Extern Table Usage

The spec suggested declaring most ncurses functions in the `extern_` table with only macro-dependent ones in the C wrapper. The session put everything in the wrapper instead. This suggests the extern system, while functional, may be less convenient than direct C wrappers for large FFI surfaces. The wrapper approach is simpler: write C once, call the functions by name.

This is a valid architectural choice, but it means the extern system wasn't stress-tested as intended.

## Comparison With Glint

| Dimension | Glint | Gled |
|-----------|-------|------|
| Definitions | 26 | 34 |
| Purpose | Batch analysis | Interactive editor |
| I/O pattern | Read DB → print report | Continuous keystroke loop |
| FFI | 4 SQLite externs | 7 C wrapper functions |
| String ops | Heavy (formatting, searching) | Heavy (line editing) |
| Array ops | Read-only iteration | Mutation on every keystroke |
| Build attempts | 22 | ~159 |
| Segfaults | 10 | ~41 |
| Largest def | 22 lines | 48 lines |
| User corrections | 0 | 0 (confirmed working) |

Glint was harder to get *correct* (string operator traps burned 40% of time). Gled was harder to get *working* (more moving parts, harder to debug interactively).

## Code Quality

### Strengths

- **Clean separation**: buffer ops, rendering, cursor logic, and event handling are well-separated
- **Correct algorithms**: shift_up/shift_down for array element shifting, split_lines/join_lines with proper boundary handling
- **Smart use of string builder**: `join_lines_loop` and `make_spaces_loop` use `sb_new`/`sb_append`/`sb_build` instead of repeated `str_concat`
- **Proper viewport management**: `scroll_view` correctly keeps the cursor visible with `rows - 2` visible lines (reserving status bar)
- **Quit confirmation**: modified-file check with two-press Ctrl-Q exit

### Issues

- **No search feature**: spec included Ctrl-F search, not implemented. Acceptable scope reduction for a proof-of-concept.
- **No SQL dump**: `gled.sql` not generated (spec listed it as a deliverable)
- **Memory pressure**: every keystroke allocates new strings for the modified line via `str_slice` + `str_concat`. The `nc_char_to_str` wrapper also allocates 18 bytes per character typed. No GC means these accumulate — estimated ~100 bytes/keystroke, so ~1MB after 10,000 keystrokes. Acceptable for editing sessions under an hour.

## Verdict

### Does It Prove Glyph Is Real?

**Yes.** A text editor is fundamentally different from a code analyzer or a calculator. It's interactive, stateful, mutation-heavy, and FFI-dependent. The fact that a fresh session built one in 34 definitions — fewer than estimated — demonstrates that Glyph can express non-trivial interactive applications.

### What Did It Prove Beyond Glint?

1. **Sustained interactivity works**: The recursive event loop with 9 parameters handles indefinite keystroke processing
2. **Array mutation works at scale**: `array_set`, `shift_up`, `shift_down` handle real data structure manipulation
3. **FFI scales**: 7 wrapper functions for a real C library (ncurses) with proper string interop
4. **The compiler improvements helped**: Zero string operator issues (vs glint's 10), SIGSEGV handler available for the 41 crashes

### What Remains Unproven?

1. **The extern system at scale**: The session bypassed it entirely in favor of C wrappers. A future test should force extern-table-only FFI.
2. **Records and enums**: Gled uses only arrays, strings, and integers. No custom types. A JSON parser or Lisp interpreter would test the type system.
3. **Error handling patterns**: No error recovery in gled (file not found, write failure). A robust tool would need these.
4. **Programs beyond ~50 definitions**: Both glint (26) and gled (34) are moderate-sized. The compiler itself is 583 definitions — can an LLM build something at 100+ definitions?

### The Bigger Picture

Two experiments, two different domains, both successful:

| | Glint | Gled |
|---|---|---|
| Domain | Dev tooling | Interactive app |
| Key challenge | String processing + SQL | FFI + state management |
| Outcome | 26 defs, working | 34 defs, working |
| Human help needed | 0 corrections | 0 corrections |

Glyph is not a gimmick. It's a language that an LLM can pick up from documentation and use to build working software across different domains. The functional style (recursion, immutable data flow, pattern matching) genuinely helps LLMs write correct code — buffer operations, viewport logic, and event dispatch were all correct algorithmically. The remaining friction is all at the FFI boundary and in debugging interactive programs.
