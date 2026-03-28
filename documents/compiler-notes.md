Does it require any compiler modifications?

● No. The assessment explicitly concludes no compiler changes are required. Everything works with
  the existing FFI infrastructure:

    1. Extern system -- extern_ table + cg_extern_wrappers already generates the right GVal wrapper functions
  2. glyph link -- already copies fn/type/const defs + extern_ rows between databases
  3. Closure representation -- heap-allocated {fn_ptr, captures...} already passes as GVal, which
  the C trampolines can unpack from GTK's user_data
  4. String conversion -- str_to_cstr / cstr_to_str already in the runtime
  5. Build script pattern -- cat gtk_ffi.c /tmp/glyph_out.c > combined.c; cc ... is the same proven
  pattern used by life, gled, asteroids, and api examples

  The one gap noted is that mcp__glyph__build/mcp__glyph__run don't currently support prepending an
  FFI C file. Section 9.4 mentions three options if that's ever wanted (--ffi flag, meta table key,
  or just keep using build scripts), but none are required -- the build script approach works today.

❯ Would compiler modifications be helpful?

● Good question. Strictly required, no. But a few targeted changes would significantly improve the
  experience. Let me think through which ones are actually worth it.

  High value, low effort:

  1. --ffi <file.c> flag on glyph build -- Prepend a C source file before the generated code.
  Eliminates the cat + manual cc build script entirely. This is maybe 10-15 lines of change in
      build_program / cmd_build. The MCP build and run tools would then just work with GTK apps.
  2. --pkg-config <name> flag (or a meta table key like pkg_config=gtk4) -- Runs pkg-config --cflags
   --libs <name> and appends the result to the cc invocation. Combined with --ffi, this eliminates
        build scripts completely:
        glyph build myapp.glyph myapp --ffi gtk_ffi.c --pkg-config gtk4

  Medium value, medium effort:

  3. C-compatible closure export -- A way to mark a closure as "C-calling-convention" so the
  compiler generates a wrapper that drops the hidden first argument. This would eliminate half the
      trampolines in gtk_ffi.c. But it touches codegen and is more complex.

  Low value (not worth it):

  - Struct FFI -- Passing C structs by value through extern_. GTK uses pointers everywhere, so
  opaque I handles already work fine.
  - Varargs support -- Only a few GTK functions use them (g_object_set), and dedicated C wrappers
  handle those cases better anyway.
  - Const externs -- GTK constants via zero-arg functions work, just slightly wasteful. Not worth a
  new mechanism.

  My recommendation: Items 1 and 2 only. They're small, they're general-purpose (benefit every FFI library, not just GTK), and they make the MCP workflow seamless. The trampoline approach for callbacks is honestly fine -- it's a fixed cost of ~50 lines of C for 8-10 shapes that never changes.

  Want me to update the assessment with this analysis, or would you rather discuss implementation of either of those flags?





So the actual state is:

  ┌──────────────────────────┬─────────────────────────────────┬───────────────────────────────┐
  │         Scenario         │             Output              │            Verdict            │
  ├──────────────────────────┼─────────────────────────────────┼───────────────────────────────┤
  │ assert_eq(42, 99)        │ "42 != 99"                      │ Already good                  │
  ├──────────────────────────┼─────────────────────────────────┼───────────────────────────────┤
  │ assert_str_eq("foo",     │ "strings differ"                │ Missing the actual strings    │
  │ "bar")                   │                                 │                               │
  ├──────────────────────────┼─────────────────────────────────┼───────────────────────────────┤
  │ Err("connection          │ "panic: unwrap failed in        │ Discards the error payload    │
  │ refused")!               │ test_unwrap_err"                │                               │
  ├──────────────────────────┼─────────────────────────────────┼───────────────────────────────┤
  │ None!                    │ Passes (!)                      │ Unwrap on None doesn't panic  │
  │                          │                                 │ — likely a bug                │
  └──────────────────────────┴─────────────────────────────────┴───────────────────────────────┘

  I was wrong about assert_eq — it already shows values. The remaining gaps are:

  1. assert_str_eq — should show "'foo' != 'bar'" instead of just "strings differ"
  2. Err(msg)! — should include the payload: "unwrap failed: connection refused"
  3. None! — doesn't panic at all, which seems like a correctness bug

  Want me to dig into any of these?









Now I have a clear picture. Here are my thoughts:

---
  Current state of the 5 libraries:

  ┌───────────────┬─────┬───────┬─────────┬───────────────┬──────────────────────┐
  │    Library    │ Fns │ Tests │ Externs │  FFI C file   │ Testable standalone? │
  ├───────────────┼─────┼───────┼─────────┼───────────────┼──────────────────────┤
  │ stdlib.glyph  │ 34  │ 22    │ 0       │ —             │ Yes                  │
  ├───────────────┼─────┼───────┼─────────┼───────────────┼──────────────────────┤
  │ json.glyph    │ 59  │ 22    │ 0       │ —             │ Yes                  │
  ├───────────────┼─────┼───────┼─────────┼───────────────┼──────────────────────┤
  │ web.glyph     │ 51  │ 9     │ 3       │ web_ffi.c     │ Yes (with FFI)       │
  ├───────────────┼─────┼───────┼─────────┼───────────────┼──────────────────────┤
  │ network.glyph │ 15  │ 0     │ 8       │ network_ffi.c │ No tests             │
  ├───────────────┼─────┼───────┼─────────┼───────────────┼──────────────────────┤
  │ gtk.glyph     │ 12  │ 0     │ 123     │ —             │ No (needs GTK4 libs) │
  └───────────────┴─────┴───────┴─────────┴───────────────┴──────────────────────┘

  Three distinct problems:

  1. glyph test in CI — straightforward for pure libraries, tricky for FFI ones

  stdlib and json are trivially testable. web.glyph needs its web_ffi.c compiled in (the cc_prepend
  meta handles this). network.glyph has zero tests. gtk.glyph can't run in CI without GTK4 installed
   (and even then, tests would need headless/mock patterns that don't exist).

  The release workflow should add: ./glyph test libraries/stdlib.glyph, ./glyph test
  libraries/json.glyph, ./glyph test libraries/web.glyph. That's the easy part.

  2. Packaging libraries in the release tarball

  Currently the package step copies glyph, documentation.glyph, and skills/. Libraries and their FFI
   C files aren't included. This needs:
  cp libraries/*.glyph dist/libraries/
  cp libraries/*_ffi.c dist/libraries/
  And the tarball needs to include libraries/.

  3. "glyph test is not enough" — the harder problem

  You're right that unit tests inside a library don't prove the library works end-to-end. A
  library's tests verify its internal logic, but not:
  - That glyph use + glyph link actually produce a working program
  - That FFI files get found and linked correctly after installation
  - That the library's API is coherent from a consumer's perspective

  What I'd suggest — integration test programs:

  For each library that has real functionality, keep a minimal integration test program alongside it
   (or in examples/). The CI step would be:

  # Unit tests
  ./glyph test libraries/stdlib.glyph
  ./glyph test libraries/json.glyph
  ./glyph test libraries/web.glyph

  # Integration tests — build (and optionally run) example programs that USE the libraries
  ./glyph build examples/web-api/app.glyph /tmp/web-api-test
  ./glyph build examples/api/api.glyph /tmp/api-test
  # For programs that exit cleanly:
  ./glyph run examples/some-json-consumer/app.glyph

  This tests the full chain: library linking, FFI resolution, codegen, and runtime behavior. The key
   constraint is that integration test programs must be non-interactive and exit on their own (no
  servers waiting for connections). You could either:

  - Have dedicated examples/*/test.glyph programs that exercise the library and exit with 0/1
  - Or add a --smoke flag convention where programs do a quick self-test and exit

  For network/web specifically, you can't actually bind ports in CI reliably. But you can test
  everything except the socket calls — json serialization, route matching, request parsing, response
   building. The web.glyph tests already do this (9 tests covering routing logic). That's probably
  sufficient for CI.

  For gtk.glyph, it's essentially untestable in CI without a display server. I'd skip it and
  document it as "manual verification only."

  Summary of proposed changes:
  1. Add ./glyph test for stdlib, json, web to the release workflow
  2. Add ./glyph build for key examples as integration smoke tests
  3. Package libraries/ in the tarball
  4. Accept that some libraries (gtk, parts of network) need manual verification

  Want to start implementing any of this?







 48 +      - name: Test libraries
 49 +        run: |
 50 +          ./glyph test libraries/stdlib.glyph
 51 +          ./glyph test libraries/json.glyph
 52 +          ./glyph test libraries/web.glyph
 53 +
 54 +      - name: Integration smoke tests
 55 +        run: |
 56 +          ./glyph run examples/hello/hello.glyph
 57 +          ./glyph run examples/fibonacci/fibonacci.glyph
 58 +          ./glyph run examples/countdown/countdown.glyph
 59 +          ./glyph build examples/benchmark/benchmark.glyph /tmp/ci-benchmark











glyph.glyph Compiler Statistics

  ┌─────────────────────┬────────┐
  │       Metric        │ Value  │
  ├─────────────────────┼────────┤
  │ Total definitions   │ 1,651  │
  ├─────────────────────┼────────┤
  │ Total tokens        │ 75,020 │
  ├─────────────────────┼────────┤
  │ Total source bytes  │ 562 KB │
  ├─────────────────────┼────────┤
  │ Extern declarations │ 23     │
  └─────────────────────┴────────┘

  By Kind

  ┌──────┬───────┬────────┐
  │ Kind │ Count │ Tokens │
  ├──────┼───────┼────────┤
  │ fn   │ 1,324 │ 56,871 │
  ├──────┼───────┼────────┤
  │ test │ 315   │ 18,053 │
  ├──────┼───────┼────────┤
  │ type │ 12    │ 96     │
  └──────┴───────┴────────┘

  Top Namespaces (by tokens)

  ┌───────────┬─────┬────────┐
  │ Namespace │ Fns │ Tokens │
  ├───────────┼─────┼────────┤
  │ codegen   │ 107 │ 8,789  │
  ├───────────┼─────┼────────┤
  │ parser    │ 148 │ 8,498  │
  ├───────────┼─────┼────────┤
  │ lower     │ 90  │ 6,552  │
  ├───────────┼─────┼────────┤
  │ typeck    │ 148 │ 5,365  │
  ├───────────┼─────┼────────┤
  │ tokenizer │ 86  │ 4,955  │
  ├───────────┼─────┼────────┤
  │ mcp       │ 55  │ 4,475  │
  ├───────────┼─────┼────────┤
  │ cli       │ 44  │ 4,165  │
  ├───────────┼─────┼────────┤
  │ json      │ 56  │ 3,125  │
  ├───────────┼─────┼────────┤
  │ build     │ 51  │ 3,020  │
  ├───────────┼─────┼────────┤
  │ mir       │ 103 │ 2,750  │
  ├───────────┼─────┼────────┤
  │ util      │ 52  │ 2,573  │
  └───────────┴─────┴────────┘

  Largest Functions

  ┌───────────────────┬────────┐
  │     Function      │ Tokens │
  ├───────────────────┼────────┤
  │ cg_runtime_sqlite │ 830    │
  ├───────────────────┼────────┤
  │ tok_one           │ 658    │
  ├───────────────────┼────────┤
  │ tok_one4          │ 652    │
  ├───────────────────┼────────┤
  │ tok_one2          │ 587    │
  ├───────────────────┼────────┤
  │ tok_loop          │ 568    │
  ├───────────────────┼────────┤
  │ parse_match_arms  │ 501    │
  └───────────────────┴────────┘

  The whole compiler fits in 75K tokens — well within a single LLM context window. For comparison,
  that's roughly equivalent to ~2,000 lines of conventional code, or about 15 pages of dense text.







How about a Glyph definition linter — a static analysis tool that reads a .glyph database and reports code quality issues?

  - Checks: unused locals, overly long functions (token count), deeply nested matches, missing test
    coverage, orphan definitions (no callers), naming convention violations
  - Self-referential: it analyzes Glyph programs, including itself
  - Test-heavy: each lint rule is a pure function that can be tested against known-bad snippets
  - Cross-function types: lint results, severity levels, report aggregation all share record types —
      good check_all exercise
  - Uses SQLite FFI: reads the target .glyph database directly, queries the dep table
  - Practical: actually useful for maintaining the compiler and examples

  ~20-30 functions, 2-3 types, 8-12 tests. Bulk-insertable in logical groups (tokenizer analysis,
  AST analysis, dep graph analysis, reporter).



