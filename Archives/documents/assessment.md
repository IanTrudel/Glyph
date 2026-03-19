# Glyph: Technical Assessment

*Written by Claude (Opus 4.6) after building the self-hosted compiler, extern system, calculator REPL, and Game of Life GUI across multiple sessions. February 2026.*

## What Glyph Has Proven

The core thesis — programs as SQLite databases, token-minimal syntax, LLM-native workflow — is not just a concept. It works. The evidence:

- **577 definitions** in `glyph.glyph` implement a complete compiler (lexer, parser, HM type inference with row polymorphism, MIR lowering, C codegen, 16-command CLI)
- **3-stage bootstrap chain** produces identical binaries: glyph0 (Rust/Cranelift) → glyph (C-codegen, 153k) → glyph (self-build, 153k)
- **Two real programs** beyond the compiler itself: an expression calculator REPL and an interactive X11 Game of Life with mouse/keyboard input
- **73 Rust tests + 14 self-hosted tests** all passing
- The same codebase expressed in Rust is ~10,000 lines across 6 crates. In Glyph it's ~3,500 lines of source text. The token-minimal syntax delivers real compression.

The SQL-as-module-system idea is the sleeper hit. `SELECT body FROM def WHERE name LIKE 'lower_%'` to find all lowering functions, `SELECT COUNT(*) FROM def WHERE compiled=0` to check build status — the database IS the IDE. No imports, no file organization decisions, no module boundaries. Just definitions and queries.

## Honest Limitations

### 1. Everything-is-i64 Type Erasure

Every value at the machine level is `long long` (8 bytes). Strings are pointers cast to i64. Arrays are pointers cast to i64. Records are pointers cast to i64. This means:

- The C runtime uses `void*` parameters that receive `long long` arguments, requiring `-Wno-int-conversion -Wno-incompatible-pointer-types` on every build
- The type system exists only during compilation — it completely evaporates at the machine level
- Bugs like the field offset ambiguity (TyNode vs AstNode sharing field names `n1`, `n2`, `ns`, `sval`) are fundamentally caused by this: without runtime type information, the codegen must guess which concrete type a polymorphic variable has

**Concrete damage**: The field offset bug caused silent memory corruption (reading offset 16 instead of 0 for `.n1`), producing garbage values with no error message. It took 35+ definitions (`build_type_reg`, `coll_local_acc`, `resolve_fld_off`, `fix_all_field_offsets`) to mitigate — not fix — the problem. The mitigation (prefer the type with the most matching fields) is a heuristic, not a proof.

**Potential solutions**:

- **Runtime type tags**: Add a hidden tag word to every heap allocation. Cost: 8 bytes per object, one comparison per field access. This eliminates the ambiguity entirely — you always know the concrete type.
- **Monomorphization**: Duplicate functions for each concrete type they're called with (like Rust generics). Eliminates runtime cost but increases code size. The MIR layer already stores enough type information to do this.
- **Uniform field layout**: Sort all record fields alphabetically (already done) but also use a global field name → offset registry at compile time, assigning each unique field name a globally unique slot. Records become sparse arrays indexed by field ID. Wastes space but eliminates ambiguity.
- **Practical compromise**: Add an optional `#[concrete(TypeName)]` annotation on function parameters where ambiguity is detected. The compiler already detects ambiguity (it's what `find_best_type` does) — it could emit a warning suggesting the annotation.

### 2. Recursion Without TCO Guarantees

Glyph has no loops. Every iteration is recursion: `fill_grid` recurses 3,072 times to initialize a grid, `draw_row` recurses 64 times per row, the parser recurses per token. This works because:

- `-O2` in gcc/clang enables tail-call optimization for functions in tail position
- Most Glyph recursive calls ARE in tail position (the `match` arms return the recursive call directly)

But TCO is not guaranteed. It depends on the C compiler's optimization level, the calling convention, and whether the call is truly in tail position. One missed optimization = stack overflow. At default 8MB stack with ~80 bytes per frame, that's ~100,000 recursion depth before crash — enough for current programs, but not safe for arbitrary input sizes.

**Concrete risk**: The self-hosted compiler processes 577 definitions. Each tokenization recurses per character (~50 chars average = ~28,000 total recursive calls just for lexing). If gcc decides not to TCO one of those paths, the compiler crashes on its own source code.

**Potential solutions**:

- **Trampoline transformation**: The C codegen could detect tail calls and emit trampoline-style code: return a thunk instead of recursing, let a while loop in the caller unwrap it. This guarantees O(1) stack regardless of recursion depth. Cost: one heap allocation per "iteration" (the thunk), plus the trampoline loop. Pattern:
  ```c
  // Instead of: return f(x+1);
  // Emit: return MAKE_THUNK(f, x+1);
  // Caller: while (IS_THUNK(result)) result = EVAL_THUNK(result);
  ```
- **MIR loop lowering**: Detect self-recursive tail calls at the MIR level and lower them to actual loops. This is the cleanest solution — the recursion never reaches C. The MIR already has `Terminator::Goto` for unconditional jumps; a self-tail-call is just a goto to the function entry with updated arguments.
- **Iteration syntax**: Add a `loop` or `for` construct to the language. This contradicts the "pattern matching as the only control flow" philosophy, but it's the most pragmatic fix. Even Haskell has `forM_`.
- **Stack size annotation**: Least invasive — emit `__attribute__((optimize("O2")))` on recursive C functions, or use `-Wl,-z,stacksize=67108864` (64MB) at link time. Doesn't fix the problem, just moves the crash further away.

### 3. The Runtime Function Chain

The self-hosted compiler identifies runtime functions (functions provided by the C runtime, not user code) via a chain of 6 functions: `is_runtime_fn` → `fn2` → `fn3` → `fn4` → `fn5` → `fn6`. Each checks 3-5 names via nested `match` expressions, then delegates to the next link. Total: ~35 runtime functions checked via ~30 string comparisons in the worst case.

This exists because Glyph lacks hash maps, large match expressions, or any O(1) lookup structure. It's a linear scan disguised as a call chain.

**Why it matters**: It's not a performance issue (35 comparisons is nothing). It's a maintainability issue. Adding a new runtime function requires:
1. Finding the last link in the chain
2. Checking if it has room for another match arm (the ~7-nested-s2 stack overflow limit)
3. Possibly creating `is_runtime_fn7`
4. Updating the chain in the previous link

This is the kind of accidental complexity that compounds. Every new feature (string builder, raw_set, array_new) added another link.

**Potential solutions**:

- **Hash set in the runtime**: Add `glyph_set_new()`, `glyph_set_add()`, `glyph_set_has()` to the C runtime. Initialize a set of runtime function names at startup. One O(1) lookup replaces the entire chain. Cost: ~50 lines of C (open-addressing hash table).
- **Sorted array + binary search**: Store runtime names in a sorted array, use binary search. Glyph can express this today — just need `str_compare` (lexicographic ordering) in the runtime. O(log n) for 35 names = 5 comparisons max.
- **Compile-time table**: Since the set of runtime functions is fixed at compile time, encode it as a C `switch` on the first character + length, then strcmp for collisions. This is what real compilers do for keyword recognition.
- **Accept it**: At 35 functions and 6 chain links, this is ugly but functional. If Glyph never grows beyond ~50 runtime functions, the chain works fine. The real fix is hash maps in the language, which would solve this and many other problems.

### 4. Closure Support Gap

The Rust/Cranelift backend fully supports closures: heap-allocated `{fn_ptr, captures...}` structs, indirect calls through the function pointer, hidden closure parameter in the calling convention. The self-hosted C codegen does not — `rv_make_closure = 9` is defined as a constant but no code generates closure structs or indirect calls.

This means the self-hosted compiler can't compile programs that use lambdas or higher-order functions with captured variables. No `map`, no `filter`, no `fold`. Function references work (passing a named function as a value), but closures that capture local variables do not.

**Impact**: This is the single biggest expressiveness gap between Glyph-compiled-by-Rust and Glyph-compiled-by-itself. It means the self-hosted compiler can only compile programs written in a first-order functional style — no anonymous functions with captured state.

**Potential solutions**:

- **Lambda lifting**: Transform closures into top-level functions with explicit environment parameters at the MIR level. The MIR lowering already knows which variables are captured (it builds the capture list for `MakeClosure`). Instead of creating a closure struct, add the captured variables as extra function parameters and rewrite all call sites. This eliminates closures entirely — the C codegen never sees them. Rust's Cranelift backend already has `lifted_fns` infrastructure for this.
- **C closure structs**: Generate the same pattern the Cranelift backend uses, but in C: `malloc(8 * (1 + ncaptures))`, store fn_ptr at offset 0, captures at 8+. Indirect call via `((long long(*)(long long,...))closure[0])(closure, args...)`. This is ~30 lines of C codegen code.
- **Defunctionalization**: Replace closures with tagged unions — each lambda gets a unique tag, and a dispatch function switches on the tag to call the right code with the right captures. Avoids function pointers entirely. More code but simpler C output.

### 5. Error Experience

The Rust compiler has decent error reporting: `format_diagnostic()` converts byte offsets to `file:line:col` with source context and a caret pointing at the error. Type errors are collected (multiple errors reported per build). Parse errors include span information.

The self-hosted compiler has none of this. A type error prints a message to stderr and stops. A parse error might crash. A runtime error is a segfault (exit 139) or silent wrong value. No stack traces, no source locations, no "did you mean X?" suggestions.

Debugging the self-hosted compiler required encoding intermediate values as exit codes (`main = result * 100 + other_thing`, limited to 0-255 modulo 256) and binary-searching for the broken definition by commenting out halves of the code.

**Potential solutions**:

- **Source maps in generated C**: Emit `#line N "defname"` directives in the generated C code. When the program segfaults, gdb/lldb can show which Glyph definition caused it. Cost: one line per function, negligible.
- **Runtime bounds checking**: The array bounds check exists (`glyph_array_bounds_check`) but record field access is unchecked. Add a debug mode that validates field offsets against a runtime type tag (ties into solution 1 above).
- **Error accumulation in self-hosted typechecker**: Instead of `eprintln` + stop, push errors onto an array and continue. Report all errors at the end. This is how the Rust typechecker works — the self-hosted version just needs to follow the same pattern.
- **Signal handler**: Register a SIGSEGV handler in the C runtime that prints "segfault in function X at address Y" using the function name table (which the codegen already builds for the C output). Cost: ~20 lines of C, massive debugging improvement.

### 6. The "LLM-Native" Question

Glyph's stated purpose is that only LLMs use it. But the honest assessment is that so far, a human (with LLM assistance) is the primary user. The `glyph put` / `glyph get` / `glyph build` loop works well for the human-LLM collaboration pattern, but the true test — a fresh LLM session writing a useful Glyph program from scratch with only the skill docs — is unproven at scale.

The skill documentation (`.claude/skills/glyph-dev/`) exists and covers syntax, runtime functions, and examples. But writing a Glyph program requires understanding:
- That `+` on strings doesn't concatenate (it does integer addition on the pointer)
- That `{` in strings triggers interpolation
- That zero-arg functions with side effects need dummy parameters
- That record fields are sorted alphabetically in memory regardless of source order
- That `match` is the only control flow (no if/else/for/while)

These are learnable, but they're sharp edges that an LLM will hit repeatedly without clear error messages.

**Potential solutions**:

- **Automated test suite for LLM onboarding**: Create a set of 20 progressively harder tasks ("write a function that returns the larger of two numbers", "write a recursive fibonacci", "write a program that reads a file and counts lines") and measure how many a fresh Claude session gets right on first attempt with only the skill docs. This gives a concrete success rate to optimize against.
- **Better error messages for common LLM mistakes**: Detect `str + str` and suggest `str_concat`. Detect `if` keyword and suggest `match`. Detect `{` in string without `\` and warn about interpolation. The parser already has the information to do this.
- **REPL mode**: Let the LLM test expressions interactively without the full build cycle. `glyph eval db.glyph "1 + 2"` → `3`. This would dramatically speed up the write-test loop and reduce the cost of mistakes.

## Overall Verdict

Glyph is a working proof-of-concept with genuine innovation (programs-as-databases, SQL-as-module-system, token-minimal syntax) and genuine limitations (type erasure costs, no TCO guarantees, incomplete closure support in self-hosted path). The bootstrap chain is the strongest evidence that the core design works. The two example programs (calculator, Game of Life) show it can produce real software.

The path from here is about choosing which limitations to fix first. My ranking by impact-to-effort ratio:

1. **MIR-level tail-call-to-loop lowering** — eliminates the stack overflow risk for all programs, pure compiler change, no language design decisions needed
2. **Source maps in C codegen** (`#line` directives) — near-zero effort, massive debugging improvement
3. **Closure codegen in self-hosted compiler** — unlocks higher-order programming, ~30 definitions, pattern already exists in Cranelift backend
4. **Runtime hash set for function lookup** — replaces the is_runtime_fn chain, enables O(1) lookup for any future dispatch tables
5. **LLM onboarding test suite** — validates the core thesis, guides all future design decisions

The most interesting question isn't whether Glyph works (it does). It's whether the LLM-native workflow — SQL queries to read context, INSERT to write code, incremental compilation via content hashing — is genuinely better than giving an LLM a conventional language with good tooling. That question requires empirical data from LLM sessions, not more compiler features.
