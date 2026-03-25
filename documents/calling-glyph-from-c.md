# Calling Glyph Code from C (or Rust)

## Feasibility

The architecture makes this quite natural since Glyph compiles to C functions with a uniform `GVal` (i.e., `intptr_t`) calling convention. Every Glyph function becomes a regular C function like:

```c
GVal my_func(GVal arg1, GVal arg2) { ... }
```

## What Already Works Today (hackish)

You could `glyph build` to get the intermediate C file (`/tmp/glyph_out.c`), compile it as an object/library instead of an executable, and call those functions directly from C. The main obstacle is that the runtime needs initialization (args setup, GC init, signal handlers).

## What a Clean "Compile as Library" Mode Would Need

1. **`--emit=lib` flag** — skip generating `main()`, emit just the functions + runtime init
2. **Header generation** — produce a `.h` file declaring exported functions with their `GVal` signatures (or better, with real C types if we have type info)
3. **Runtime init function** — something like `glyph_runtime_init()` that the host calls once before using any Glyph functions
4. **Type marshaling** — the caller needs to know that strings are `{ptr, len}` structs, arrays are `{ptr, len, cap}`, etc. Could provide helper macros/functions for conversion

From Rust, you'd go through `extern "C"` — same as any C library.

## Design Questions

- **Granularity**: Export the whole `.glyph` DB as a library, or select specific functions to expose?
- **Type safety**: Generate typed wrappers (e.g., `int64_t add(int64_t a, int64_t b)`) using the type checker's inferred types, or leave everything as `GVal`?
- **Bidirectional**: Should Glyph be able to call back into the host's functions? (This already works via the extern system)

The bidirectional case is the most compelling — you could write performance-critical code in C/Rust and glue logic in Glyph, or vice versa. The extern system already handles Glyph→C; this would complete the C→Glyph direction.

## Implementation Approach

The simplest starting point would be a `--lib` build flag that emits a `.c` + `.h` pair without `main()`.

---

## Investigation: Current Generated C Structure

### File Layout

A generated C file (`/tmp/glyph_out.c`, ~4,600 lines for the self-hosted compiler) has this structure:

```
┌──────────────────────────────────────────┐
│  1. Runtime C code (embedded)            │  ~190 lines
│     - #includes, GC macros, GVal typedef │
│     - Signal handlers, panic, alloc      │
│     - String ops, array ops, I/O         │
│     - HashMap, bitset, math, builder     │
├──────────────────────────────────────────┤
│  2. Preamble (extern declarations)       │  ~60 lines
│     - extern declarations for runtime    │
│     - extern wrappers for user externs   │
├──────────────────────────────────────────┤
│  3. Gen=2 struct typedefs                │  Few lines
│     typedef struct { GVal f1; } Name;    │
├──────────────────────────────────────────┤
│  4. Forward declarations                 │  ~100 lines
│     GVal func_name(GVal _0, GVal _1);    │
├──────────────────────────────────────────┤
│  5. Function implementations             │  ~4,200 lines
│     GVal func(GVal _0) { ... }           │
├──────────────────────────────────────────┤
│  6. main() wrapper                       │  6 lines
│     GC_INIT + signals + glyph_main()     │
└──────────────────────────────────────────┘
```

### Concrete Examples from Generated Code

**Simple function** (`add x y = x + y`):
```c
GVal add(GVal _0, GVal _1) {
  _glyph_current_fn = "add";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "add";
  _glyph_call_depth++;
#endif
  GVal _2 = 0;
bb_0:
  _2 = _0 + _1;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _2;
}
```

**Recursive function** (`factorial n = match n / 0 -> 1 / _ -> n * factorial(n - 1)`):
```c
GVal factorial(GVal _0) {
  _glyph_current_fn = "factorial";
  // ... debug instrumentation ...
  GVal _1 = 0; GVal _2 = 0; GVal _3 = 0; GVal _4 = 0; GVal _5 = 0;
bb_0:
  _2 = _0 == (GVal)0;
  if (_2) goto bb_2; else goto bb_3;
bb_1:
  return _1;
bb_2:
  _1 = (GVal)1;
  goto bb_1;
bb_3:
  _3 = _0 - (GVal)1;
  _4 = factorial(_3);
  // ...
}
```

**Gen=2 structs**:
```c
typedef struct { GVal fst; GVal snd; } Glyph_Pair;
typedef struct { GVal x; GVal y; } Glyph_Point;
typedef struct { GVal x; GVal y; GVal z; } Glyph_Vector;
```

**Main wrapper**:
```c
extern void glyph_set_args(int argc, char** argv);
int main(int argc, char** argv) {
  GC_INIT();
  signal(SIGSEGV, _glyph_sigsegv);
  signal(SIGFPE, _glyph_sigfpe);
  glyph_set_args(argc, argv);
  return (int)glyph_main();
}
```

### Runtime Initialization Requirements

The main wrapper does 4 things that a library consumer would need:

| Step | What | Why | Library mode? |
|------|------|-----|---------------|
| `GC_INIT()` | Initialize Boehm GC | All allocation goes through GC | Must be called once by host |
| `signal(SIGSEGV, ...)` | Crash handler | Nice error messages | Optional for library |
| `signal(SIGFPE, ...)` | Division-by-zero handler | Nice error messages | Optional for library |
| `glyph_set_args(argc, argv)` | Store argc/argv | For `glyph_args()` runtime fn | Only if library uses `args()` |

### Type Representations (for marshaling)

| Glyph Type | C Representation | Size | Layout |
|------------|-----------------|------|--------|
| `I` (Int) | `GVal` (intptr_t) | 8 bytes | Direct value |
| `B` (Bool) | `GVal` | 8 bytes | 0 or 1 |
| `F` (Float) | `GVal` | 8 bytes | Bitcast via `_glyph_i2f`/`_glyph_f2i` |
| `S` (Str) | `GVal` (pointer) | 8 bytes | Points to `{char* ptr, int64_t len}` (16 bytes) |
| `[T]` (Array) | `GVal` (pointer) | 8 bytes | Points to `{int64_t* data, int64_t len, int64_t cap}` (24 bytes) |
| Enum/Option | `GVal` (pointer) | 8 bytes | Points to `{int64_t tag, int64_t payload...}` |
| Record | `GVal` (pointer) | 8 bytes | Points to `GVal[]` (fields in alpha order) |
| Gen=2 struct | `GVal` (pointer) | 8 bytes | Points to `typedef struct { ... }` |

### What Needs to Change for `--lib` Mode

**In `cg_program`** (the top-level assembly function):
- Currently: `s7(preamble, typedefs, forward_decls, "\n", functions, "\n", main_wrapper)`
- Library mode: `s7(preamble, typedefs, forward_decls, "\n", functions, "\n", init_function)`
- Replace `cg_main_wrapper()` with `cg_lib_init()` that generates:

```c
void glyph_lib_init(void) {
  GC_INIT();
  signal(SIGSEGV, _glyph_sigsegv);
  signal(SIGFPE, _glyph_sigfpe);
}
```

**Header generation** (new `cg_header` function):
- Emit `#pragma once`, GVal typedef, struct typedefs
- Emit `void glyph_lib_init(void);`
- Emit forward declarations for all user functions (or just exported ones)
- Emit marshaling helpers:

```c
// String helpers
static inline GVal glyph_str_from_cstr(const char* s) { return glyph_cstr_to_str(s); }
static inline const char* glyph_str_to_cstr_copy(GVal s) { return glyph_str_to_cstr(s); }

// Float helpers
static inline GVal glyph_float(double d) { GVal v; memcpy(&v, &d, 8); return v; }
static inline double glyph_to_float(GVal v) { double d; memcpy(&d, &v, 8); return d; }
```

**Compilation**:
- Executable: `cc -o prog prog.c -lgc` (current)
- Library: `cc -c -fPIC -o libmylib.o mylib.c -lgc && ar rcs libmylib.a libmylib.o`
- Or shared: `cc -shared -fPIC -o libmylib.so mylib.c -lgc`

### Usage from C

```c
#include "mylib.h"

int main(void) {
    glyph_lib_init();

    // Call a pure integer function directly
    GVal result = add(42, 58);
    printf("add: %lld\n", (long long)result);

    // Call a function taking strings
    GVal greeting = glyph_cstr_to_str("hello");
    GVal upper = to_upper(greeting);
    printf("upper: %s\n", glyph_str_to_cstr(upper));

    // Call a function returning a struct
    GVal pt = make_point(10, 20);
    GVal x = ((GVal*)pt)[0];  // field 'x' at offset 0 (alphabetical)

    return 0;
}
```

### Usage from Rust

```rust
// build.rs: cc::Build::new().file("mylib.c").compile("mylib");

extern "C" {
    fn glyph_lib_init();
    fn add(a: isize, b: isize) -> isize;
    fn glyph_cstr_to_str(s: *const std::ffi::c_char) -> isize;
    fn glyph_str_to_cstr(s: isize) -> *mut std::ffi::c_char;
}

fn main() {
    unsafe {
        glyph_lib_init();
        let result = add(42, 58);
        println!("add: {result}");
    }
}
```

### Complexity Assessment

| Component | Effort | Notes |
|-----------|--------|-------|
| Skip main wrapper | Trivial | Conditional in `cg_program` |
| Generate init function | Trivial | 4 lines of C |
| Header generation | Small | Forward decls already exist, just wrap in header guards |
| Marshaling helpers | Small | Inline functions in header |
| `--lib` CLI flag | Small | New flag in `cmd_build` |
| Compile as .a/.so | Small | Change cc invocation |
| Export selection (optional) | Medium | Needs annotation system or naming convention |
| Typed headers (optional) | Medium | Needs type checker integration for C type mapping |

**Minimum viable: ~6 definitions changed/added.** The core codegen is already library-compatible — it's really just about controlling whether `main()` is emitted and generating a header.
