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
