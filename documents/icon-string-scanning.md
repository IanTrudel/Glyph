# Icon-Style String Scanning for Glyph

The fact that strings are represented by a ptr and len reminds me of Icon, a successor of SNOBOL, and makes me wonder if we could match similar string features and capabilities. It's extremely powerful.

That's a great connection. Icon's string scanning is one of the most elegant string processing models ever designed, and you're right that the `{ptr, len}` qualifier representation is exactly what made it efficient — substrings are just new qualifiers pointing into the same backing data (which Glyph's `str_slice` already does, except it copies).

**Icon's core string scanning model:**

```
s ? {                        # set scanning subject & position
  tab(upto(&letters))       # skip to first letter, return skipped part
  word := tab(many(&letters)) # consume run of letters
  =":" | ="="               # match literal (with alternation)
  value := tab(0)           # consume rest
}
```

The key primitives are:
- **Scanning environment**: a subject string + mutable cursor position
- **`tab(i)` / `move(n)`**: advance cursor, return the consumed substring (zero-copy in Icon)
- **Character sets (csets)**: `upto(cs)` finds next char in set, `many(cs)` matches a run, `any(cs)` matches one
- **`find(s)`**: find substring, returns position
- **`match(s)` / `=s`**: match literal at current position
- **Goal-directed evaluation**: functions can *generate* multiple positions, with automatic backtracking

**What Glyph already has:**
- `{ptr, len}` string representation — same as Icon qualifiers
- Bitsets (`glyph_bitset_new/set/test`) — foundation for csets (256-bit for byte values)
- `str_char_at`, `str_slice`, `str_index_of`, `str_starts_with` — primitive scanning ops
- Functional paradigm with records — scanning state threads naturally as `{subject, pos}`
- Match expressions — could express alternation

**What would be needed for Icon-level power:**

1. **Zero-copy slicing** — `str_slice` currently copies. Icon's qualifiers just point into the original. A `str_view(s, start, end)` that shares the backing data would make scanning allocation-free.

2. **Character sets as a type** — a 256-bit (32-byte) bitset, with constructors like `cset("aeiou")`, `cset_range('a', 'z')`, `cset_union`, `cset_diff`, and the key scanning functions:
   - `upto(cs, s, pos)` → first position where `s[pos..]` contains a char in `cs`
   - `many(cs, s, pos)` → position after longest run of chars in `cs` starting at `pos`
   - `any(cs, s, pos)` → `pos+1` if `s[pos]` is in `cs`, else fail
   - `notany(cs, s, pos)` → complement

3. **Scanning context record** — something like `{subj: S, pos: I}` threaded through combinators, where `tab` and `move` return both the consumed substring and the new context.

4. **Combinator library** — `scan_match`, `scan_tab`, `scan_upto`, `scan_many` etc. as pure functions that take and return scanning state. In Glyph's functional style this would look like:

```
# Scanning state: {subj: S, pos: I}
# Scanner result: ?{val: S, state: ScanState}  (None on failure)

scan_upto cs state =
  i = cset_upto(cs, state.subj, state.pos)
  match i < 0
    _ -> None
    _ -> Some({val: str_slice(state.subj, state.pos, i), state: state{pos: i}})
```

The really interesting question is whether Glyph could support Icon's **goal-directed evaluation** — where scanning functions are generators that can produce multiple results and backtrack. That's the deep magic of Icon. In a functional language, this maps to returning arrays of possible positions, or using continuation-passing style. Given Glyph's current feature set, the pragmatic approach would be a combinator library (like parser combinators) rather than trying to replicate Icon's built-in backtracking.

The zero-copy slice optimization + cset type + combinator functions could be a standalone `libraries/scan.glyph` that gives Glyph serious text processing power.
