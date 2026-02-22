# Spec: gled — A Terminal Text Editor in Glyph

*A minimal ncurses text editor to test Glyph's extern system, mutable state management, and real-time interactive I/O.*

## Why This Test Matters

Glint proved an LLM can build a batch-mode analysis tool in Glyph. An interactive text editor is a fundamentally different challenge:

- **Sustained interactivity**: the program runs indefinitely, processing one keystroke at a time
- **Heavy mutation**: every keystroke modifies the buffer (array of strings), cursor, and viewport
- **Real extern usage**: 10-15 ncurses FFI declarations — far more than calculator (2) or glint (4)
- **String surgery**: inserting a character mid-line requires `str_slice` + `str_concat` on every keypress
- **State threading**: cursor position, scroll offset, modified flag, and buffer must all flow through a recursive event loop

This is not a compiler tool. If Glyph can express a text editor, it's a real language.

## Project Location

`examples/gled/`

## Files

| File | Purpose |
|------|---------|
| `gled.glyph` | SQLite database with ~45-55 Glyph definitions |
| `nc_wrapper.c` | Small C wrapper (~6 functions) for ncurses macros/globals |
| `build.sh` | Build script |
| `gled.sql` | SQL dump for version control |

## Why a Small C Wrapper Is Needed

Most ncurses functions (`getch`, `mvaddstr`, `clear`, `refresh`, `attron`, etc.) are real functions and work directly via the `extern_` table. However, a few critical things are **C macros or global variables** that can't be called via FFI:

- `stdscr` — global `WINDOW*`, needed for `keypad(stdscr, TRUE)`
- `LINES`, `COLS` — terminal dimensions, set by `initscr()`
- `A_REVERSE`, `A_BOLD` — attribute constants (may be macros)

The wrapper should expose these as functions. Approximately 6 functions:

```c
#include <ncurses.h>
long long nc_init(void)      { initscr(); raw(); noecho(); keypad(stdscr, TRUE); start_color(); return 0; }
long long nc_cleanup(void)   { endwin(); return 0; }
long long nc_lines(void)     { return LINES; }
long long nc_cols(void)      { return COLS; }
long long nc_attr_rev(void)  { return A_REVERSE; }
long long nc_attr_bold(void) { return A_BOLD; }
```

All other ncurses functions go in the `extern_` table as regular externs.

## Build Script

Same concatenation pattern as `examples/life/`:

```bash
#!/bin/sh
cd "$(dirname "$0")"
../../glyph build gled.glyph /tmp/gled_dummy 2>/dev/null
cat nc_wrapper.c /tmp/glyph_out.c > /tmp/gled_full.c
cc -w -O2 /tmp/gled_full.c -o gled -no-pie -lncurses
echo "Built gled"
```

## Extern Declarations

These ncurses functions should be declared in the `extern_` table:

```bash
# Drawing
./glyph extern gled.glyph clear clear "-> I" --lib ncurses
./glyph extern gled.glyph refresh refresh "-> I" --lib ncurses
./glyph extern gled.glyph mvaddstr mvaddstr "I I I -> I" --lib ncurses
./glyph extern gled.glyph attron attron "I -> I" --lib ncurses
./glyph extern gled.glyph attroff attroff "I -> I" --lib ncurses

# Input
./glyph extern gled.glyph getch getch "-> I" --lib ncurses

# Cursor
./glyph extern gled.glyph nc_move move "I I -> I" --lib ncurses
```

**Important**: `mvaddstr` takes a C string (`const char*`), not a Glyph string. The third parameter should be passed through `str_to_cstr()` in Glyph code before calling the extern. The sig uses `I` (not `S`) for this reason.

The `nc_*` wrapper functions (nc_init, nc_cleanup, nc_lines, nc_cols, nc_attr_rev, nc_attr_bold) are NOT in the extern table — they're linked directly via source concatenation, like the `gx_*` pattern in the life example.

## Features

### File Operations
- **Open**: `./gled filename.txt` — loads file into buffer. No argument = empty buffer.
- **Save**: `Ctrl-S` — writes buffer back to file. Shows "Saved" in status bar.
- **Quit**: `Ctrl-Q` — exits. If modified, shows "Unsaved changes! Ctrl-Q again to quit."

### Navigation
- **Arrow keys**: move cursor (up/down/left/right)
- **Home / End**: move to beginning/end of line
- **Page Up / Page Down**: scroll by screen height

### Editing
- **Printable characters**: insert at cursor position
- **Enter**: split current line at cursor, move cursor to start of new line
- **Backspace**: delete character before cursor. At column 0, join with previous line.
- **Delete**: delete character at cursor. At end of line, join with next line.

### Display
- **Line numbers**: left gutter showing line numbers (4-5 chars wide)
- **Status bar**: bottom row, reverse video: `filename [modified] | Line Y, Col X | gled`
- **Cursor**: positioned at editing location after each redraw

## Data Model

All state flows through the recursive event loop as parameters:

```
event_loop buf cx cy top filename modified quit_pressed rows cols
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `buf` | `[S]` | Array of strings, one per line |
| `cx` | `I` | Cursor column (0-indexed) |
| `cy` | `I` | Cursor row in buffer (0-indexed, absolute) |
| `top` | `I` | First visible line (scroll offset) |
| `filename` | `S` | File path |
| `modified` | `I` | 1 if buffer modified since last save |
| `quit_pressed` | `I` | 1 if Ctrl-Q pressed once (for confirm) |
| `rows` | `I` | Terminal height |
| `cols` | `I` | Terminal width |

## Key Constants

ncurses key codes the program needs:

| Key | Code | Notes |
|-----|------|-------|
| KEY_UP | 259 | Arrow up |
| KEY_DOWN | 258 | Arrow down |
| KEY_LEFT | 260 | Arrow left |
| KEY_RIGHT | 261 | Arrow right |
| KEY_HOME | 262 | Home |
| KEY_END | 360 | End |
| KEY_PPAGE | 339 | Page up |
| KEY_NPAGE | 338 | Page down |
| KEY_BACKSPACE | 263 | Backspace (also check 127) |
| KEY_DC | 330 | Delete |
| Ctrl-S | 19 | Save (raw mode) |
| Ctrl-Q | 17 | Quit (raw mode) |
| Enter | 10 | Newline |

## Definition Structure (~45-55 defs)

### String helpers (2-3)
- `s2`, `s3` — nested `str_concat` (NOT `+`, though `+` now works on strings)
- `str_insert str pos ch` — insert character into string at position

### Buffer operations (8-10)
- `buf_new` — create buffer with one empty line
- `buf_load filename` — read file, split into lines
- `buf_save buf filename` — join lines with newlines, write file
- `split_lines text` — split string on newline characters into array
- `join_lines buf` — join array of strings with newline separator
- `buf_insert_char buf cy cx ch` — insert char into line, return updated buffer
- `buf_delete_char buf cy cx` — delete char from line, return updated buffer
- `buf_split_line buf cy cx` — split line at cursor (Enter key)
- `buf_join_prev buf cy` — join line with previous (Backspace at col 0)
- `buf_join_next buf cy` — join line with next (Delete at end)

### Cursor and viewport (4-5)
- `clamp_cx buf cy cx` — clamp cursor column to line length
- `clamp_cy buf cy` — clamp cursor row to buffer length
- `scroll_view cy top rows` — adjust viewport to keep cursor visible
- `move_cursor buf cx cy top rows cols key` — dispatch arrow/home/end/pgup/pgdn

### Rendering (5-7)
- `draw_line y line_text cols gutter_w` — draw one line with truncation
- `draw_lines buf top rows cols gutter_w` — draw visible buffer lines
- `draw_gutter y line_num gutter_w` — draw line number in gutter
- `draw_status filename modified cy cx cols` — draw status bar (reverse video)
- `draw_screen buf cx cy top rows cols filename modified` — full redraw
- `lpad_num n width` — left-pad number for line gutter

### Event handling (5-7)
- `handle_char buf cx cy ch` — insert printable character
- `handle_enter buf cx cy` — split line
- `handle_backspace buf cx cy` — delete backward
- `handle_delete buf cx cy` — delete forward
- `handle_save buf filename` — write file
- `dispatch_key buf cx cy top filename modified quit_pressed rows cols key` — main key dispatcher
- `event_loop buf cx cy top filename modified quit_pressed rows cols` — recursive main loop

### Main (1)
- `main` — parse args, init ncurses, load file, enter event loop, cleanup

## Expected Behavior

```bash
$ cd examples/gled
$ chmod +x build.sh && ./build.sh
Built gled

$ ./gled test.txt          # Edit existing file
$ ./gled                   # New empty buffer
```

The editor should:
1. Display file contents with line numbers
2. Allow cursor movement with arrow keys
3. Allow typing, backspace, delete, enter
4. Show filename, line:col, and [modified] in status bar
5. Save with Ctrl-S, quit with Ctrl-Q
6. Handle files of 100+ lines without issues

## Potential Issues

- **No GC**: Every keystroke allocates new strings (modified line via str_slice + str_concat). For short editing sessions this is fine. A 1000-keystroke session allocates ~50KB of unreclaimable strings — acceptable.
- **str_to_cstr overhead**: Every `mvaddstr` call needs `str_to_cstr()` conversion. Could be optimized with a scratch buffer, but the naive approach works.
- **Large files**: Loading a 10,000-line file into an array of strings is fine. Rendering only draws visible lines.
- **Terminal resize**: Not handled (would need SIGWINCH). Fixed size from startup is acceptable.
- **Unicode**: Not supported. ASCII only. This is fine for a proof-of-concept.

## Verification

1. `./build.sh` succeeds
2. `./gled gled.sql` opens the SQL dump and displays it correctly
3. Navigate to a line, type some text, see it appear
4. Backspace and delete work
5. Enter splits the line
6. Ctrl-S saves (verify with `cat`)
7. Ctrl-Q quits
8. Status bar shows correct line:col throughout
