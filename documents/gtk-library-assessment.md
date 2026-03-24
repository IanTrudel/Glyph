# GTK Library for Glyph: Design and Implementation Assessment

**Date:** 2026-03-24
**Status:** Assessment / Pre-implementation
**Author:** Generated via comprehensive analysis of Glyph compiler internals, existing FFI examples, and GTK4 C API

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current State of Glyph FFI](#2-current-state-of-glyph-ffi)
3. [GTK4 API Surface Analysis](#3-gtk4-api-surface-analysis)
4. [Architecture Design](#4-architecture-design)
5. [The Callback Problem](#5-the-callback-problem)
6. [Implementation Plan](#6-implementation-plan)
7. [C Wrapper Layer Specification](#7-c-wrapper-layer-specification)
8. [Glyph API Design](#8-glyph-api-design)
9. [Build Integration](#9-build-integration)
10. [Effort Estimate](#10-effort-estimate)
11. [Risks and Mitigations](#11-risks-and-mitigations)
12. [Alternatives Considered](#12-alternatives-considered)
13. [Appendices](#13-appendices)

---

## 1. Executive Summary

Building a GTK4 library for Glyph is **feasible** using the same architecture that powers existing FFI examples (X11/life, SDL2/asteroids, ncurses/gled), but requires solving one fundamental challenge: **adapting Glyph's closure calling convention to GTK's signal/callback system**.

The library is a **standalone distributable** -- a `gtk.glyph` SQLite database plus a companion `gtk_ffi.c` file. Applications consume it via `glyph link gtk.glyph myapp.glyph` (or the MCP `link` tool), which copies all definitions and extern declarations into the application database. No changes to the Glyph compiler are required.

The implementation consists of three layers:

1. **C wrapper layer** (`gtk_ffi.c`, ~800-1200 lines) -- Bridges GTK4's C API to Glyph's `GVal` ABI, handles callback trampolines, string conversion, and type casting. Distributed alongside `gtk.glyph`.
2. **Glyph library** (`gtk.glyph`, ~150-250 definitions) -- Self-contained SQLite database with extern declarations pointing at `gtk_ffi.c` symbols, plus higher-level Glyph wrapper functions. Consumed via `glyph link` or MCP.
3. **Build integration** (`build.sh` template per application) -- Compilation with `pkg-config --cflags --libs gtk4`, concatenating `gtk_ffi.c` with the generated C.

**Distribution model:** The `gtk.glyph` + `gtk_ffi.c` pair can live anywhere on disk. An application author runs `glyph link /path/to/gtk.glyph myapp.glyph` once to incorporate the library, then builds with a script that includes `gtk_ffi.c`. This follows the same pattern as the existing `network.glyph` + `network_ffi.c` library used by the API example.

**Estimated effort:** 3-5 sessions for a functional Tier 1 (basic widgets + signals), plus 2-3 sessions per additional tier.

**Key finding:** The hardest problem is not GTK's size -- it's the callback trampoline mechanism needed because Glyph closures pass a hidden `closure` first argument that GTK callbacks don't expect. The solution is a C-side callback registry that stores Glyph closures in a lookup table and provides fixed-arity C trampolines.

---

## 2. Current State of Glyph FFI

### 2.1 How Extern FFI Works Today

Glyph's extern system operates through the `extern_` table in the `.glyph` SQLite database:

```sql
CREATE TABLE extern_ (
  id     INTEGER PRIMARY KEY,
  name   TEXT NOT NULL,       -- Glyph function name
  symbol TEXT NOT NULL,       -- C symbol to call
  lib    TEXT,                -- Library for -l flag
  sig    TEXT NOT NULL,       -- Type signature: "I -> S -> I"
  conv   TEXT DEFAULT 'C',    -- Calling convention
  UNIQUE(name)
)
```

During compilation, the compiler:
1. Reads all extern declarations
2. Generates C wrapper functions: `GVal glyph_NAME(GVal _0, GVal _1, ...) { return (GVal)(SYMBOL)(args); }`
3. Renames MIR function references from `foo` to `glyph_foo` (via `fix_extern_calls`)
4. Collects `-l` flags from the `lib` column (via `collect_libs`)
5. Links everything into a single C translation unit

### 2.2 The GVal Type System

All values in Glyph's C codegen are `typedef intptr_t GVal` (64-bit on x86_64):

| Glyph Type | C Representation | Size |
|------------|-----------------|------|
| `I` (Int64) | `GVal` (raw integer) | 8 bytes |
| `U` (UInt64) | `GVal` (raw unsigned) | 8 bytes |
| `F` (Float64) | `GVal` (bitcast via memcpy) | 8 bytes |
| `S` (String) | `GVal` pointer to `{char* ptr, i64 len}` | 16 bytes on heap |
| `B` (Bool) | `GVal` (0 or 1) | 8 bytes |
| `V` (Void) | `GVal` (return 0) | 8 bytes |
| Records | `GVal` pointer to `GVal[]` array | N*8 bytes on heap |
| Arrays | `GVal` pointer to `{GVal* data, i64 len, i64 cap}` | 24-byte header on heap |
| Enum variants | `GVal` pointer to `{i64 tag, GVal payload...}` | (1+N)*8 bytes on heap |

**Key implication for GTK:** All C pointers (GTK widget pointers, GObject pointers) fit directly in `GVal` since pointers are 64-bit on x86_64. This means widget handles can be passed around as opaque `GVal` integers -- exactly the same pattern used by the X11 wrapper.

### 2.3 Proven FFI Patterns

The existing examples demonstrate four increasingly complex FFI patterns:

**Pattern 1: Simple wrappers** (life.glyph / X11)
- 127 lines of C wrapping 20 X11 functions
- All parameters are `long long` (GVal)
- Static global state (`Display *_dpy`, `XEvent _evt`)
- Direct cast between pointer types and `long long`

**Pattern 2: Macro-safe wrappers** (gled.glyph / ncurses)
- 72 lines of C
- Wraps ncurses functions that are C macros (can't be called through function pointers)
- Creates Glyph string structs from single characters

**Pattern 3: Stateful C globals + accessor functions** (asteroids.glyph / SDL2)
- 498 lines of C (the largest existing FFI wrapper)
- All mutable game state lives in C static globals
- `get_sx()` / `set_sx(v)` accessor pattern for each state variable
- Float bitcasting helpers: `_gf()` (GVal -> double), `_gv()` (double -> GVal)
- Physics/math calculations done in C to avoid Glyph's float limitations

**Pattern 4: Heap-allocated opaque structs** (api.glyph / network)
- 247 lines of C
- Allocates `NetRequest` structs on the heap, returns as `GVal` pointer
- Accessor functions extract fields: `net_req_method(req)`, `net_req_path(req)`
- Complex C logic (HTTP parsing) encapsulated behind simple GVal interface

### 2.4 Extern Signature Limitations

The extern_ signature system supports only primitive types:
- `I`, `U`, `F`, `S`, `B`, `V`
- No struct types, no function pointer types, no varargs
- Array types (`[I]`, `[S]`) work at runtime but not in extern sig declarations
- Maximum ~6-8 parameters per extern function (no hard limit, but practical constraint)

### 2.5 Include Header Limitation

The generated C code only includes standard headers: `<stdlib.h>`, `<stdio.h>`, `<string.h>`, `<signal.h>`. GTK requires `<gtk/gtk.h>` which is not available through the auto-generated extern wrappers. This means **a separate C wrapper file is mandatory** (same pattern as all existing GUI examples).

---

## 3. GTK4 API Surface Analysis

### 3.1 GTK4 Architecture Overview

GTK4 uses an application-centric model built on GLib/GObject:

```
GtkApplication (manages lifecycle, main loop)
  -> "activate" signal -> create windows
    GtkApplicationWindow
      -> set_child() -> widget tree
        GtkBox / GtkGrid (layout)
          GtkButton, GtkLabel, GtkEntry, ... (leaf widgets)
    Event Controllers (key, mouse, gesture)
      -> signals -> callbacks
```

Key characteristics:
- **All objects are GObjects** with reference counting and signal system
- **Floating references** for widgets -- containers take ownership of children
- **Event controllers** (not event signals) for input handling
- **NULL-terminated C strings** throughout (vs Glyph's fat pointer strings)
- **Variadic property setters** (`g_object_set(..., NULL)`)
- **Cast macros** (`GTK_WINDOW()`, `GTK_BOX()`) for type safety

### 3.2 Core API Functions Needed

A minimal but useful GTK4 binding needs approximately:

| Category | Functions | Count |
|----------|-----------|-------|
| **Application** | new, run, quit | 3 |
| **Window** | new, set_title, set_default_size, set_child, present, destroy, close | 7 |
| **Button** | new_with_label, set_label, get_label | 3 |
| **Label** | new, set_text, get_text, set_markup | 4 |
| **Box** | new, append, prepend, remove | 4 |
| **Grid** | new, attach, remove | 3 |
| **Entry** | new, get_buffer, get_text, set_text | 4 |
| **TextView** | new, get_buffer, set_text, get_text | 4 |
| **Image** | new_from_file, new_from_icon_name | 2 |
| **DrawingArea** | new, set_content_width/height, set_draw_func | 4 |
| **ScrolledWindow** | new, set_child, set_policy | 3 |
| **Stack** | new, add_named, set_visible_child_name | 3 |
| **HeaderBar** | new, pack_start, pack_end, set_title_widget | 4 |
| **Widget common** | set_halign/valign, set_hexpand/vexpand, margins, visible, sensitive, css_class, queue_draw | ~12 |
| **Signals** | connect (with trampoline variants) | 5-8 |
| **Event controllers** | key, click, motion, scroll + attach | ~8 |
| **Object** | ref, unref | 2 |
| **CSS** | provider_new, load_from_string, add_for_display | 3 |
| **Timers** | timeout_add, idle_add | 2 |
| **Dialogs** | alert_new, alert_show, file_dialog_new, file_dialog_open | 4 |
| **Menu** | menu_new, menu_append, action_new, add_action | 4 |
| **Cairo** (for DrawingArea) | set_source_rgb, move_to, line_to, stroke, fill, rectangle, arc, set_line_width | ~10 |
| **Totals** | | **~100** |

### 3.3 Signal Callback Signatures

GTK4 uses varying callback signatures for different signals. The most common ones:

```c
// Zero extra args (button "clicked", application "activate")
void (*)(GtkWidget *widget, gpointer user_data);

// Boolean return (window "close-request")
gboolean (*)(GtkWindow *window, gpointer user_data);

// Key events (4 extra args)
gboolean (*)(GtkEventControllerKey *self, guint keyval, guint keycode,
             GdkModifierType state, gpointer user_data);

// Mouse click (3 extra args including doubles)
void (*)(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);

// Mouse motion (2 extra args, doubles)
void (*)(GtkEventControllerMotion *self, double x, double y, gpointer user_data);

// Drawing area (3 extra args + cairo context)
void (*)(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data);

// Timer/idle (no extra args)
gboolean (*)(gpointer user_data);
```

Each distinct signature shape needs its own C trampoline function.

---

## 4. Architecture Design

### 4.1 Three-Layer Architecture

```
+--------------------------------------------------+
|  Glyph Application Code                          |
|    app = gtk_app_new("com.example.app")           |
|    gtk_on_activate(app, \app ->                   |
|      win = gtk_window_new(app)                    |
|      btn = gtk_button_new("Click me")             |
|      gtk_on_clicked(btn, \btn -> println("Hi"))   |
|      gtk_window_set_child(win, btn)               |
|      gtk_window_present(win))                     |
|    gtk_app_run(app)                               |
+--------------------------------------------------+
|  gtk.glyph Library (Glyph definitions)            |
|    - Extern declarations for C wrappers           |
|    - Higher-level Glyph API functions              |
|    - Linked into app via `glyph link`             |
+--------------------------------------------------+
|  gtk_ffi.c (C wrapper layer)                      |
|    - Callback trampoline system                   |
|    - String conversion helpers                    |
|    - GTK API wrappers (GVal ABI)                  |
|    - Type cast embedding                          |
+--------------------------------------------------+
|  GTK4 / GLib / GObject / Cairo (system libs)      |
+--------------------------------------------------+
```

### 4.2 Data Flow for a Button Click

```
User clicks button
  -> GTK4 emits "clicked" signal
  -> C trampoline function called:
     void _gtk_trampoline_clicked(GtkButton *btn, gpointer data) {
         GVal* closure = (GVal*)data;
         GVal fn_ptr = closure[0];
         GVal result = ((GVal(*)(GVal, GVal))fn_ptr)(
             (GVal)closure,   // closure env as hidden first arg
             (GVal)btn        // widget pointer as GVal
         );
     }
  -> Glyph closure body executes
  -> Glyph code calls more gtk_* functions through externs
  -> C wrappers translate GVal -> GTK C types
  -> GTK4 performs the operations
```

### 4.3 Widget Handle Model

All GTK widgets are represented as opaque `GVal` integers (pointer values):

```glyph
-- Widgets are just I (Int64) values -- opaque pointers
win = gtk_window_new(app)    -- returns GVal holding GtkWindow*
btn = gtk_button_new("Hi")   -- returns GVal holding GtkButton*
gtk_window_set_child(win, btn)  -- passes both as GVal
```

This is identical to how the X11 wrapper handles `Window`, `GC`, and `Display` -- all are opaque `long long` values.

---

## 5. The Callback Problem

This is the single most important design challenge for the GTK binding.

### 5.1 The Mismatch

**Glyph closure calling convention:**
```c
// Glyph compiles: \btn -> println("clicked")
// Into a closure struct: [fn_ptr, captured_var_1, ...]
// Called as: fn_ptr(closure, btn)
//                   ^^^^^^ hidden first argument
```

**GTK callback convention:**
```c
// GTK expects: void callback(GtkButton *btn, gpointer user_data)
//              No hidden first argument
```

The Glyph closure passes `closure` as the first argument. GTK passes the widget as the first argument and `user_data` as the last. These are incompatible.

### 5.2 Solution: Trampoline Registry

The C wrapper layer maintains a **callback registry** -- a table mapping signal IDs to Glyph closures. When GTK fires a signal, a C trampoline function:

1. Receives the GTK-standard callback arguments
2. Extracts the Glyph closure pointer from `user_data`
3. Calls the Glyph closure with the correct calling convention (closure as first arg)

```c
// C trampoline for "clicked" signal (widget + user_data -> closure call)
static void _trampoline_void_widget(GtkWidget *widget, gpointer data) {
    GVal *closure = (GVal *)data;
    GVal fn = closure[0];
    ((GVal (*)(GVal, GVal))fn)((GVal)closure, (GVal)widget);
}

// Glyph-callable: connect a closure to a "clicked" signal
GVal gtk_on_clicked(GVal widget, GVal closure) {
    g_signal_connect(GTK_WIDGET((void*)widget), "clicked",
                     G_CALLBACK(_trampoline_void_widget), (gpointer)closure);
    return 0;
}
```

### 5.3 Trampoline Shapes Needed

Each distinct callback signature needs one C trampoline:

| Trampoline | GTK Signature | Glyph Call | Used By |
|-----------|---------------|------------|---------|
| `void_widget` | `(Widget*, data)` | `fn(closure, widget)` | button clicked, activate |
| `bool_widget` | `(Widget*, data) -> gboolean` | `fn(closure, widget)` | close-request |
| `key_pressed` | `(Controller*, keyval, keycode, state, data) -> gboolean` | `fn(closure, keyval, keycode, state)` | key-pressed |
| `click_pressed` | `(Gesture*, n, x, y, data)` | `fn(closure, n, x_bits, y_bits)` | gesture pressed |
| `motion` | `(Controller*, x, y, data)` | `fn(closure, x_bits, y_bits)` | mouse motion |
| `draw_func` | `(DrawingArea*, cairo_t*, w, h, data)` | `fn(closure, cairo, w, h)` | drawing area |
| `timeout` | `(data) -> gboolean` | `fn(closure)` | g_timeout_add |
| `notify` | `(Object*, ParamSpec*, data)` | `fn(closure, object)` | property change |

**Total: ~8-10 trampolines** cover the vast majority of GTK4 use cases.

### 5.4 Float Parameter Handling in Callbacks

Mouse coordinates (`x`, `y`) arrive as `double` in GTK callbacks. For Glyph, these must be bitcast to `GVal`:

```c
static void _trampoline_click(GtkGestureClick *gesture, int n_press,
                               double x, double y, gpointer data) {
    GVal *closure = (GVal *)data;
    GVal fn = closure[0];
    // Bitcast doubles to GVal for Glyph
    GVal xv, yv;
    memcpy(&xv, &x, sizeof(double));
    memcpy(&yv, &y, sizeof(double));
    ((GVal (*)(GVal, GVal, GVal, GVal))fn)(
        (GVal)closure, (GVal)n_press, xv, yv);
}
```

### 5.5 Closure Lifetime

**Critical concern:** Glyph closures are heap-allocated and not reference-counted. If a closure is garbage-collected (or goes out of scope) while GTK still holds a reference to it via `user_data`, we get a use-after-free.

**Mitigation strategies:**
1. **Don't free closures** -- Glyph doesn't have a garbage collector; heap allocations persist for the program lifetime. This is safe for signal handlers which typically live as long as the widget.
2. **Registry with explicit cleanup** -- Store closures in a C-side array; on widget destroy, remove entries. This adds complexity but is necessary for long-running apps that create/destroy many widgets.
3. **Weak references** -- Use `g_signal_connect_data` with a `GClosureNotify` destroy callback that could clean up the Glyph closure. Most robust but requires tracking allocations.

**Recommended approach for v1:** Option 1 (don't free). Glyph programs don't have a GC, so closures naturally persist. Signal handler closures are typically set up once during widget creation and live for the widget's lifetime. This matches GTK's own ownership model.

---

## 6. Implementation Plan

### 6.1 Tiered Delivery

**Tier 1: Hello World** (minimum viable)
- Application lifecycle (new, run, quit)
- Window (new, title, size, present, destroy)
- Button (new, label)
- Label (new, set_text)
- Box layout (new, append)
- Signal connection (clicked, activate, close-request)
- String conversion helpers
- Build script template
- **~40 C wrapper functions, ~30 Glyph definitions, ~400 lines C**

**Tier 2: Forms and Input**
- Entry (text input)
- TextView (multi-line)
- CheckButton, ToggleButton
- Grid layout
- SpinButton
- Margin/alignment/expand widget properties
- Key event controller
- **~30 additional C wrappers, ~25 Glyph definitions, ~250 lines C**

**Tier 3: Rich Widgets**
- HeaderBar
- Stack + StackSwitcher
- ScrolledWindow
- Image
- ProgressBar
- MenuButton + PopoverMenu + Actions
- CSS styling
- Timers (timeout_add, idle_add)
- **~30 additional C wrappers, ~30 Glyph definitions, ~300 lines C**

**Tier 4: Custom Drawing**
- DrawingArea with Cairo
- Cairo primitives (line, rect, arc, fill, stroke, text)
- Mouse gesture controllers (click, motion, scroll)
- Animation via timeout/queue_draw
- **~25 C wrappers, ~20 Glyph definitions, ~200 lines C**

**Tier 5: Dialogs and File I/O** (GTK 4.10+ async API)
- AlertDialog
- FileDialog (open, save)
- AboutDialog
- Message dialogs
- **~15 C wrappers, ~15 Glyph definitions, ~150 lines C**

### 6.2 File Structure

The library itself is just two files. Example applications are separate.

```
libraries/
  gtk.glyph              -- Standalone library database (extern decls + Glyph wrappers)
  gtk_ffi.c              -- Companion C wrapper (trampolines + GTK API wrappers)

examples/
  gtk_hello/
    hello.glyph          -- Minimal GTK hello world (has gtk.glyph linked in)
    build.sh             -- Build script
  gtk_calculator/
    calc.glyph           -- Calculator with entry + buttons
    build.sh
  gtk_drawing/
    draw.glyph           -- Custom drawing with Cairo
    build.sh
  gtk_editor/
    editor.glyph         -- Text editor with menu bar
    build.sh
```

### 6.3 How Applications Consume the Library

**Step 1: Create application database**
```bash
glyph init myapp.glyph
```

**Step 2: Link the GTK library into it**
```bash
glyph link libraries/gtk.glyph myapp.glyph
```
This copies all fn/type/const definitions and all `extern_` rows from `gtk.glyph` into `myapp.glyph`. The application database is now self-contained -- it has the GTK wrapper functions and extern declarations baked in.

Alternatively via MCP:
```
mcp__glyph__link(lib="libraries/gtk.glyph", target="myapp.glyph")
```

**Step 3: Write application code** (via MCP or CLI)
```
mcp__glyph__put_def(db="myapp.glyph", name="main", kind="fn",
  body="main =\n  app = gtk_app_new(\"com.example.hello\")\n  ...")
```

**Step 4: Build with the companion C file**
```bash
glyph build myapp.glyph myapp 2>/dev/null || true
cat libraries/gtk_ffi.c /tmp/glyph_out.c > /tmp/myapp_full.c
cc -O2 -w $(pkg-config --cflags gtk4) /tmp/myapp_full.c -o myapp $(pkg-config --libs gtk4)
```

### 6.4 What Lives in gtk.glyph

The `gtk.glyph` database contains:

| Kind | Content | Example |
|------|---------|---------|
| `extern_` rows | C FFI declarations pointing at `gtk_ffi.c` symbols | `name=gtk_button_new, symbol=gtk_ffi_button_new, sig=S -> I` |
| `fn` definitions | Glyph convenience wrappers | `gtk_vbox`, `gtk_hbox`, `gtk_make_window` |
| `type` definitions | Named record types (if needed) | `GtkApp = {handle: I}` (optional, opaque I may suffice) |
| `const` definitions | GTK constants exposed as Glyph values | (via zero-arg extern functions instead -- Glyph has no const FFI) |

The extern_ `lib` column is left empty because linking is handled by the build script's `pkg-config` output, not by the compiler's `collect_libs` mechanism.

### 6.5 Implementation Order

1. Write `gtk_ffi.c` with trampoline system + Tier 1 wrappers
2. Create `gtk.glyph` with extern declarations + Glyph helpers
3. Build and verify hello world example (link + build + run)
4. Iterate through tiers, testing each with an example program

---

## 7. C Wrapper Layer Specification

### 7.1 File Header

```c
/* gtk_ffi.c -- GTK4 FFI wrapper for Glyph
 *
 * Provides GVal-ABI wrappers for GTK4 functions and callback trampolines.
 * Must be compiled with: $(pkg-config --cflags gtk4)
 * Must be linked with: $(pkg-config --libs gtk4)
 */
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>

typedef intptr_t GVal;
```

### 7.2 String Conversion Helpers

```c
/* Convert Glyph fat string {ptr, len} to null-terminated C string.
 * Returns malloc'd string -- GTK will NOT free this, but many GTK
 * functions only need the string transiently (during the call). */
static const char* _gs(GVal glyph_str) {
    if (!glyph_str) return "";
    const char *ptr = *(const char **)glyph_str;
    long long len = *(long long *)((char *)glyph_str + 8);
    char *cstr = (char *)malloc(len + 1);
    memcpy(cstr, ptr, (size_t)len);
    cstr[len] = '\0';
    return cstr;
}

/* Convert null-terminated C string to Glyph fat string.
 * Allocates 16 + strlen bytes on heap. */
static GVal _gstr(const char *cstr) {
    if (!cstr) { cstr = ""; }
    long long len = (long long)strlen(cstr);
    char *r = (char *)malloc(16 + len);
    char *d = r + 16;
    memcpy(d, cstr, (size_t)len);
    *(const char **)r = d;
    *(long long *)(r + 8) = len;
    return (GVal)r;
}
```

### 7.3 Trampoline Functions

```c
/* Trampoline: void callback(Widget*, user_data)
 * Used for: "clicked", "activate", "toggled" */
static void _tramp_void_widget(GtkWidget *w, gpointer data) {
    GVal *cl = (GVal *)data;
    ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, (GVal)w);
}

/* Trampoline: gboolean callback(Window*, user_data)
 * Used for: "close-request" */
static gboolean _tramp_bool_widget(GtkWindow *w, gpointer data) {
    GVal *cl = (GVal *)data;
    GVal r = ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, (GVal)w);
    return (gboolean)r;
}

/* Trampoline: gboolean callback(Controller*, keyval, keycode, state, data)
 * Used for: "key-pressed", "key-released" */
static gboolean _tramp_key(GtkEventControllerKey *ctrl, guint keyval,
                            guint keycode, GdkModifierType state, gpointer data) {
    GVal *cl = (GVal *)data;
    GVal r = ((GVal (*)(GVal, GVal, GVal, GVal))cl[0])(
        (GVal)cl, (GVal)keyval, (GVal)keycode, (GVal)state);
    return (gboolean)r;
}

/* Trampoline: void callback(Gesture*, n_press, x, y, data)
 * Used for: "pressed", "released" */
static void _tramp_click(GtkGestureClick *g, int n, double x, double y, gpointer data) {
    GVal *cl = (GVal *)data;
    GVal xv, yv;
    memcpy(&xv, &x, sizeof(double));
    memcpy(&yv, &y, sizeof(double));
    ((GVal (*)(GVal, GVal, GVal, GVal))cl[0])((GVal)cl, (GVal)n, xv, yv);
}

/* Trampoline: void callback(Controller*, x, y, data)
 * Used for: "motion" */
static void _tramp_motion(GtkEventControllerMotion *ctrl, double x, double y, gpointer data) {
    GVal *cl = (GVal *)data;
    GVal xv, yv;
    memcpy(&xv, &x, sizeof(double));
    memcpy(&yv, &y, sizeof(double));
    ((GVal (*)(GVal, GVal, GVal))cl[0])((GVal)cl, xv, yv);
}

/* Trampoline: void draw_func(DrawingArea*, cairo_t*, w, h, data)
 * Used for: gtk_drawing_area_set_draw_func */
static void _tramp_draw(GtkDrawingArea *area, cairo_t *cr,
                         int w, int h, gpointer data) {
    GVal *cl = (GVal *)data;
    ((GVal (*)(GVal, GVal, GVal, GVal))cl[0])(
        (GVal)cl, (GVal)cr, (GVal)w, (GVal)h);
}

/* Trampoline: gboolean callback(data)
 * Used for: g_timeout_add, g_idle_add */
static gboolean _tramp_timeout(gpointer data) {
    GVal *cl = (GVal *)data;
    GVal r = ((GVal (*)(GVal))cl[0])((GVal)cl);
    return (gboolean)r;
}
```

### 7.4 GTK API Wrappers (Tier 1 Examples)

```c
/* Application */
GVal gtk_ffi_app_new(GVal app_id) {
    const char *id = _gs(app_id);
    GtkApplication *app = gtk_application_new(id, G_APPLICATION_DEFAULT_FLAGS);
    free((void *)id);
    return (GVal)app;
}

GVal gtk_ffi_app_run(GVal app) {
    return (GVal)g_application_run(G_APPLICATION((void *)app), 0, NULL);
}

GVal gtk_ffi_on_activate(GVal app, GVal closure) {
    g_signal_connect((void *)app, "activate",
                     G_CALLBACK(_tramp_void_widget), (gpointer)closure);
    return 0;
}

/* Window */
GVal gtk_ffi_window_new(GVal app) {
    return (GVal)gtk_application_window_new(GTK_APPLICATION((void *)app));
}

GVal gtk_ffi_window_set_title(GVal win, GVal title) {
    const char *t = _gs(title);
    gtk_window_set_title(GTK_WINDOW((void *)win), t);
    free((void *)t);
    return 0;
}

GVal gtk_ffi_window_set_size(GVal win, GVal w, GVal h) {
    gtk_window_set_default_size(GTK_WINDOW((void *)win), (int)w, (int)h);
    return 0;
}

GVal gtk_ffi_window_set_child(GVal win, GVal child) {
    gtk_window_set_child(GTK_WINDOW((void *)win), GTK_WIDGET((void *)child));
    return 0;
}

GVal gtk_ffi_window_present(GVal win) {
    gtk_window_present(GTK_WINDOW((void *)win));
    return 0;
}

/* Button */
GVal gtk_ffi_button_new(GVal label) {
    const char *l = _gs(label);
    GtkWidget *btn = gtk_button_new_with_label(l);
    free((void *)l);
    return (GVal)btn;
}

GVal gtk_ffi_on_clicked(GVal widget, GVal closure) {
    g_signal_connect((void *)widget, "clicked",
                     G_CALLBACK(_tramp_void_widget), (gpointer)closure);
    return 0;
}

/* Label */
GVal gtk_ffi_label_new(GVal text) {
    const char *t = _gs(text);
    GtkWidget *lbl = gtk_label_new(t);
    free((void *)t);
    return (GVal)lbl;
}

GVal gtk_ffi_label_set_text(GVal label, GVal text) {
    const char *t = _gs(text);
    gtk_label_set_text(GTK_LABEL((void *)label), t);
    free((void *)t);
    return 0;
}

/* Box Layout */
GVal gtk_ffi_box_new(GVal orientation, GVal spacing) {
    return (GVal)gtk_box_new((GtkOrientation)orientation, (int)spacing);
}

GVal gtk_ffi_box_append(GVal box, GVal child) {
    gtk_box_append(GTK_BOX((void *)box), GTK_WIDGET((void *)child));
    return 0;
}

/* Widget properties */
GVal gtk_ffi_widget_set_margin(GVal widget, GVal top, GVal end, GVal bottom, GVal start) {
    GtkWidget *w = GTK_WIDGET((void *)widget);
    gtk_widget_set_margin_top(w, (int)top);
    gtk_widget_set_margin_end(w, (int)end);
    gtk_widget_set_margin_bottom(w, (int)bottom);
    gtk_widget_set_margin_start(w, (int)start);
    return 0;
}

GVal gtk_ffi_widget_add_css_class(GVal widget, GVal class_name) {
    const char *c = _gs(class_name);
    gtk_widget_add_css_class(GTK_WIDGET((void *)widget), c);
    free((void *)c);
    return 0;
}

/* Object lifecycle */
GVal gtk_ffi_unref(GVal obj) {
    g_object_unref((gpointer)obj);
    return 0;
}
```

### 7.5 Constants

```c
/* Orientation constants */
GVal gtk_ffi_horizontal(GVal d) { return (GVal)GTK_ORIENTATION_HORIZONTAL; }
GVal gtk_ffi_vertical(GVal d)   { return (GVal)GTK_ORIENTATION_VERTICAL; }

/* Alignment constants */
GVal gtk_ffi_align_fill(GVal d)   { return (GVal)GTK_ALIGN_FILL; }
GVal gtk_ffi_align_start(GVal d)  { return (GVal)GTK_ALIGN_START; }
GVal gtk_ffi_align_end(GVal d)    { return (GVal)GTK_ALIGN_END; }
GVal gtk_ffi_align_center(GVal d) { return (GVal)GTK_ALIGN_CENTER; }

/* Key constants (subset) */
GVal gtk_ffi_key_escape(GVal d)   { return (GVal)GDK_KEY_Escape; }
GVal gtk_ffi_key_return(GVal d)   { return (GVal)GDK_KEY_Return; }
GVal gtk_ffi_key_tab(GVal d)      { return (GVal)GDK_KEY_Tab; }
GVal gtk_ffi_key_backspace(GVal d){ return (GVal)GDK_KEY_BackSpace; }
GVal gtk_ffi_key_delete(GVal d)   { return (GVal)GDK_KEY_Delete; }
```

---

## 8. Glyph API Design

### 8.1 Extern Declarations

The `gtk.glyph` database's `extern_` table contains rows mapping Glyph function names to `gtk_ffi.c` symbols. These are inserted during library creation via `glyph extern`:

```bash
# Building gtk.glyph -- these populate the extern_ table
glyph init gtk.glyph
glyph extern gtk.glyph gtk_app_new     gtk_ffi_app_new     "" "S -> I"
glyph extern gtk.glyph gtk_app_run     gtk_ffi_app_run     "" "I -> I"
glyph extern gtk.glyph gtk_on_activate gtk_ffi_on_activate "" "I -> I -> I"
glyph extern gtk.glyph gtk_window_new       gtk_ffi_window_new       "" "I -> I"
glyph extern gtk.glyph gtk_window_set_title gtk_ffi_window_set_title "" "I -> S -> I"
glyph extern gtk.glyph gtk_window_set_size  gtk_ffi_window_set_size  "" "I -> I -> I -> I"
glyph extern gtk.glyph gtk_window_set_child gtk_ffi_window_set_child "" "I -> I -> I"
glyph extern gtk.glyph gtk_window_present   gtk_ffi_window_present   "" "I -> I"
glyph extern gtk.glyph gtk_button_new  gtk_ffi_button_new  "" "S -> I"
glyph extern gtk.glyph gtk_on_clicked  gtk_ffi_on_clicked  "" "I -> I -> I"
glyph extern gtk.glyph gtk_label_new      gtk_ffi_label_new      "" "S -> I"
glyph extern gtk.glyph gtk_label_set_text gtk_ffi_label_set_text "" "I -> S -> I"
glyph extern gtk.glyph gtk_box_new    gtk_ffi_box_new    "" "I -> I -> I"
glyph extern gtk.glyph gtk_box_append gtk_ffi_box_append "" "I -> I -> I"
glyph extern gtk.glyph GTK_HORIZONTAL gtk_ffi_horizontal "" "I -> I"
glyph extern gtk.glyph GTK_VERTICAL   gtk_ffi_vertical   "" "I -> I"
```

When an application runs `glyph link gtk.glyph myapp.glyph`, all these `extern_` rows are copied into `myapp.glyph`. The application code then calls `gtk_app_new(...)` etc. directly -- the compiler sees them in the extern_ table and generates the appropriate `glyph_gtk_ffi_*` wrapper calls.

### 8.2 Higher-Level Glyph API Functions

Beyond raw externs, the library can provide idiomatic Glyph wrappers:

```
-- Convenience: create a vertical box with spacing
gtk_vbox spacing =
  gtk_box_new(GTK_VERTICAL(0), spacing)

-- Convenience: create a horizontal box with spacing
gtk_hbox spacing =
  gtk_box_new(GTK_HORIZONTAL(0), spacing)

-- Convenience: create window with title and size
gtk_make_window app title w h =
  win = gtk_window_new(app)
  gtk_window_set_title(win, title)
  gtk_window_set_size(win, w, h)
  win

-- Convenience: add multiple children to a box
gtk_box_add_all box children =
  match glyph_array_len(children) > 0
    true ->
      gtk_box_append(box, children[0])
      gtk_box_add_all(box, array_tail(children))
    _ -> box
```

### 8.3 Example: Hello World

```
main =
  app = gtk_app_new("com.example.hello")
  gtk_on_activate(app, \app ->
    win = gtk_make_window(app, "Hello Glyph", 400, 300)
    box = gtk_vbox(10)
    label = gtk_label_new("Hello from Glyph + GTK4!")
    btn = gtk_button_new("Click Me")
    count = [0]
    gtk_on_clicked(btn, \btn ->
      count[0] := count[0] + 1
      gtk_label_set_text(label, "Clicked {count[0]} times"))
    gtk_box_append(box, label)
    gtk_box_append(box, btn)
    gtk_window_set_child(win, box)
    gtk_window_present(win))
  gtk_app_run(app)
```

### 8.4 Example: Drawing

```
main =
  app = gtk_app_new("com.example.draw")
  gtk_on_activate(app, \app ->
    win = gtk_make_window(app, "Drawing", 600, 400)
    da = gtk_drawing_area_new(0)
    gtk_drawing_area_set_size(da, 600, 400)
    gtk_set_draw_func(da, \cr w h ->
      cairo_set_source_rgb(cr, 0.2, 0.4, 0.8)
      cairo_rectangle(cr, 50, 50, 200, 150)
      cairo_fill(cr)
      cairo_set_source_rgb(cr, 0.9, 0.1, 0.1)
      cairo_arc(cr, 400, 200, 80, 0.0, 6.28)
      cairo_fill(cr))
    gtk_window_set_child(win, da)
    gtk_window_present(win))
  gtk_app_run(app)
```

---

## 9. Build Integration

### 9.1 Compiler Enhancement: `meta` Table Build Configuration

The recommended approach stores build configuration in the `.glyph` database's existing `meta` table. This is Glyph-native -- the program *is* the database, so its build instructions belong there too.

**New `meta` keys:**

| Key | Value | Effect |
|-----|-------|--------|
| `cc_prepend` | File path (relative or absolute) | Prepend this C source file before generated code |
| `cc_args` | Arbitrary string | Append to the `cc` command line |

**How `gtk.glyph` uses this:**

```sql
-- Set during gtk.glyph library creation
INSERT INTO meta (key, value) VALUES ('cc_prepend', 'gtk_ffi.c');
INSERT INTO meta (key, value) VALUES ('cc_args', '$(pkg-config --cflags --libs gtk4)');
```

When `glyph link gtk.glyph myapp.glyph` copies definitions, it also copies these `meta` keys. Then `glyph build myapp.glyph myapp` reads them and handles everything -- no build script needed.

**Compiler changes required:**
1. `build_program` reads `cc_prepend` from `meta`, prepends that file's contents before the generated C
2. `build_program` reads `cc_args` from `meta`, appends to the `cc` invocation string
3. `cmd_link` copies `cc_prepend` and `cc_args` meta keys from source to target (appending if target already has values, to support linking multiple libraries)

This is a small, general-purpose change (~20-30 lines across `build_program` and `cmd_link`).

### 9.2 CLI Override: `--cc-args`

In addition to the `meta` table mechanism, a CLI flag provides ad-hoc overrides:

```bash
glyph build myapp.glyph myapp --cc-args "-fsanitize=address -g"
```

CLI `--cc-args` appends *after* the meta-derived args. Use cases:
- Debug flags for a single build
- Platform-specific tweaks without modifying the database
- Testing with different compilers or optimization levels
- CI environments with non-standard paths

### 9.3 End-to-End Workflow (With Enhancement)

```bash
# 1. Create application
glyph init myapp.glyph

# 2. Link the GTK library (copies defs, externs, AND meta build config)
glyph link /path/to/gtk.glyph myapp.glyph

# 3. Write application code
glyph put myapp.glyph fn -f /tmp/main.gl

# 4. Build -- just works, no build script needed
glyph build myapp.glyph myapp
```

Step 4 reads `cc_prepend=gtk_ffi.c` and `cc_args=$(pkg-config --cflags --libs gtk4)` from the `meta` table, prepends the FFI C file, and passes the right flags to `cc`.

### 9.4 MCP Workflow

With the `meta` table enhancement, the MCP workflow is seamless:

```
mcp__glyph__init(db="myapp.glyph")
mcp__glyph__link(lib="libraries/gtk.glyph", target="myapp.glyph")
mcp__glyph__put_def(db="myapp.glyph", name="main", kind="fn", body="main = ...")
mcp__glyph__build(db="myapp.glyph")
mcp__glyph__run(db="myapp.glyph")
```

No special handling. The database carries its own build instructions.

### 9.5 Fallback: Build Script (No Compiler Changes)

If the compiler enhancement is deferred, the existing build script pattern still works:

```bash
#!/bin/sh
glyph build myapp.glyph myapp 2>/dev/null || true
cat gtk_ffi.c /tmp/glyph_out.c > /tmp/myapp_full.c
cc -O2 -w $(pkg-config --cflags gtk4) /tmp/myapp_full.c -o myapp $(pkg-config --libs gtk4)
```

### 9.6 Meta Key Merging on Link

When linking multiple libraries that each contribute `meta` keys:

```bash
glyph link gtk.glyph myapp.glyph      # cc_prepend=gtk_ffi.c, cc_args=$(pkg-config ...)
glyph link sqlite.glyph myapp.glyph   # cc_args=-lsqlite3
```

The `cc_prepend` values are concatenated (space-separated file list), and `cc_args` values are appended. The result in `myapp.glyph`:

```sql
cc_prepend = 'gtk_ffi.c sqlite_ffi.c'
cc_args = '$(pkg-config --cflags --libs gtk4) -lsqlite3'
```

This generalizes to any number of linked libraries, each contributing their own build requirements.

---

## 10. Effort Estimate

### 10.1 Per-Tier Breakdown

| Tier | C Wrapper | Glyph Defs | Example | Testing | Total |
|------|-----------|------------|---------|---------|-------|
| **Tier 1: Hello World** | ~400 LOC | ~30 defs | hello.glyph | Verify display | 1-2 sessions |
| **Tier 2: Forms** | ~250 LOC | ~25 defs | calculator.glyph | Input/output | 1-2 sessions |
| **Tier 3: Rich Widgets** | ~300 LOC | ~30 defs | editor.glyph | Menus, styling | 1-2 sessions |
| **Tier 4: Drawing** | ~200 LOC | ~20 defs | drawing.glyph | Cairo rendering | 1 session |
| **Tier 5: Dialogs** | ~150 LOC | ~15 defs | file_browser.glyph | Async dialogs | 1 session |
| **Total** | **~1300 LOC** | **~120 defs** | **5 examples** | | **5-9 sessions** |

### 10.2 Comparison with Existing FFI Projects

| Project | C Wrapper LOC | Glyph Defs | FFI Functions | Complexity |
|---------|--------------|------------|---------------|------------|
| life (X11) | 127 | 23 | 20 | Low |
| gled (ncurses) | 72 | 34 | 15 | Low-Medium |
| asteroids (SDL2) | 498 | 87 | ~40 | High |
| api (network) | 247 | 45 | 8 | Medium |
| **GTK Tier 1** | **~400** | **~30** | **~25** | **Medium** |
| **GTK Full** | **~1300** | **~120** | **~100** | **High** |

The GTK library at Tier 1 is comparable in complexity to the asteroids example. The full library would be roughly 2.5x the asteroids wrapper.

### 10.3 Compiler Enhancement (Prerequisite)

| Task | Scope | Effort |
|------|-------|--------|
| Read `cc_prepend` from `meta`, prepend file contents | `build_program` | ~10 lines |
| Read `cc_args` from `meta`, append to cc invocation | `build_program` | ~5 lines |
| Parse `--cc-args` CLI flag | `cmd_build` | ~10 lines |
| Copy `cc_prepend`/`cc_args` meta keys on link (with merge) | `cmd_link` | ~15 lines |
| Same for `build_test_program` | `build_test_program` | ~10 lines |
| **Total** | | **~50 lines, 1 session** |

This is a general-purpose enhancement that benefits all FFI libraries, not just GTK.

### 10.4 Prerequisites

Before implementation begins:

1. **GTK4 development packages installed** -- `gtk4` and `pkg-config` available
2. **Working Glyph compiler** -- `./glyph build` and `./glyph link` functional
3. **Verify closure FFI** -- Test that Glyph closures can be passed as `GVal` and called back from C (this is the core risk; test with a simple C callback before building the full GTK wrapper)

---

## 11. Risks and Mitigations

### 11.1 High Risk: Closure Calling Convention

**Risk:** The trampoline approach might not work correctly if the Glyph compiler's closure representation changes or if there are edge cases with captured variables.

**Mitigation:** Write a minimal test case first:
1. Create a Glyph function that creates a closure capturing a variable
2. Pass that closure to a C function
3. Have the C function call back into the closure via the trampoline pattern
4. Verify the captured variable is accessible

If this works, the GTK binding is feasible. If not, the closure representation needs investigation before proceeding.

### 11.2 Medium Risk: String Memory Leaks

**Risk:** Every string passed from Glyph to GTK goes through `_gs()` which mallocs a new C string. If these aren't freed, long-running GTK apps will leak memory.

**Mitigation:** The wrapper functions shown in Section 7.4 already call `free()` after each GTK call that takes a string. GTK functions that return strings (like `gtk_entry_buffer_get_text`) return `const char*` that must NOT be freed -- wrap them in `_gstr()` only.

### 11.3 Medium Risk: Float Precision in Callbacks

**Risk:** Mouse coordinates arrive as `double` in GTK callbacks. Bitcasting to `GVal` (`long long`) preserves the bits but Glyph must use `float_to_int`/`int_to_float` bitcast functions to recover the value.

**Mitigation:** Already proven in the asteroids example. The `_glyph_i2f` / `_glyph_f2i` bitcast helpers handle this correctly.

### 11.4 Low Risk: GTK Main Loop Blocking

**Risk:** `g_application_run()` blocks until the application quits. If Glyph needs to do work outside the GTK main loop, there's no way to interleave.

**Mitigation:** This is standard GTK behavior. Use `g_timeout_add()` and `g_idle_add()` (both have trampolines in the design) for periodic Glyph work. This is the intended GTK pattern.

### 11.5 Low Risk: GTK Version Compatibility

**Risk:** GTK4 API has evolved; some functions (like `GtkFileDialog`) require GTK 4.10+.

**Mitigation:** Target the GTK4 API available on the system (currently 4.20.3 on Arch Linux). Gate newer features behind version checks in `gtk_ffi.c` if portability is needed.

### 11.6 Low Risk: Compile Time

**Risk:** Including `<gtk/gtk.h>` pulls in hundreds of headers. Combined with the generated Glyph C code, compilation might be slow.

**Mitigation:** Single-TU compilation is already the pattern. `cc -O2` with GTK headers typically takes 1-3 seconds, which is acceptable.

---

## 12. Alternatives Considered

### 12.1 Raw X11 (Current Approach)

**Pros:** Already working (life.glyph), minimal wrapper, no dependencies beyond Xlib.
**Cons:** Extremely low-level (no widgets, no text rendering, manual event loop, manual layout), not practical for real applications.
**Verdict:** Good for games/visualizations, inadequate for applications with forms/menus/dialogs.

### 12.2 SDL2

**Pros:** Already working (asteroids.glyph), good for games and drawing, cross-platform.
**Cons:** No native widgets (buttons, text entry, menus), must build everything from scratch.
**Verdict:** Good for games, wrong tool for application UIs.

### 12.3 libui / nuklear / raygui

**Pros:** Simpler than GTK, fewer functions to wrap.
**Cons:** Less capable, smaller communities, fewer widgets, may not be packaged on all systems.
**Verdict:** Possible alternative for simpler needs, but GTK is more capable and better supported on Linux.

### 12.4 GTK3 Instead of GTK4

**Pros:** More tutorials/examples available, wider system compatibility.
**Cons:** Deprecated, uses old event model (signals instead of controllers), `GtkContainer` base class (more complex for containers).
**Verdict:** GTK4 is the right choice -- cleaner API, active development, and modern patterns.

### 12.5 Web UI (HTTP server + browser)

**Pros:** Already have network FFI, HTML/CSS for UI, no native toolkit needed.
**Cons:** Indirect (requires browser), no native feel, latency, complex state sync.
**Verdict:** Interesting for tools/dashboards but not a substitute for native GUI.

### 12.6 Compiler-Level Callback Support

**Pros:** Would eliminate the need for C trampolines entirely.
**Cons:** Major compiler change (extern signatures would need function pointer types, calling convention would need C-compatible mode), touches parser, type checker, MIR lowering, and codegen.
**Verdict:** Too much work for the current goal. The trampoline approach solves the problem at the FFI layer without compiler changes.

---

## 13. Appendices

### Appendix A: GTK4 pkg-config Output (Arch Linux)

```
$ pkg-config --cflags gtk4
-I/usr/include/gtk-4.0 -I/usr/include/pango-1.0 -I/usr/include/glib-2.0
-I/usr/lib/glib-2.0/include -I/usr/include/harfbuzz -I/usr/include/freetype2
-I/usr/include/libpng16 -I/usr/include/libmount -I/usr/include/blkid
-I/usr/include/fribidi -I/usr/include/cairo -I/usr/include/pixman-1
-I/usr/include/gdk-pixbuf-2.0 -I/usr/include/graphene-1.0
-I/usr/lib/graphene-1.0/include -mfpmath=sse -msse -msse2

$ pkg-config --libs gtk4
-lgtk-4 -lpangocairo-1.0 -lpango-1.0 -lharfbuzz -lgdk_pixbuf-2.0
-lcairo-gobject -lcairo -lvulkan -lgraphene-1.0 -lgio-2.0
-lgobject-2.0 -lglib-2.0
```

### Appendix B: Callback Trampoline Shape Reference

| Signal | Source Widget | Extra Args | Return | Trampoline |
|--------|-------------|------------|--------|------------|
| `activate` | GtkApplication | - | void | `_tramp_void_widget` |
| `clicked` | GtkButton | - | void | `_tramp_void_widget` |
| `toggled` | GtkToggleButton | - | void | `_tramp_void_widget` |
| `activate` | GtkEntry | - | void | `_tramp_void_widget` |
| `close-request` | GtkWindow | - | gboolean | `_tramp_bool_widget` |
| `key-pressed` | EventControllerKey | keyval, keycode, state | gboolean | `_tramp_key` |
| `key-released` | EventControllerKey | keyval, keycode, state | gboolean | `_tramp_key` |
| `pressed` | GtkGestureClick | n_press, x, y | void | `_tramp_click` |
| `released` | GtkGestureClick | n_press, x, y | void | `_tramp_click` |
| `motion` | EventControllerMotion | x, y | void | `_tramp_motion` |
| `enter` | EventControllerMotion | x, y | void | `_tramp_motion` |
| draw_func | GtkDrawingArea | cairo_t*, w, h | void | `_tramp_draw` |
| timeout | g_timeout_add | - | gboolean | `_tramp_timeout` |
| idle | g_idle_add | - | gboolean | `_tramp_timeout` |

### Appendix C: Glyph Type Mapping for GTK

| GTK C Type | Glyph Type | Notes |
|------------|-----------|-------|
| `GtkWidget*` | `I` | Opaque pointer as GVal |
| `GtkWindow*` | `I` | Cast handled in C wrapper |
| `GtkApplication*` | `I` | Cast handled in C wrapper |
| `const char*` | `S` | Converted via `_gs()` / `_gstr()` |
| `gboolean` | `I` | 0 = FALSE, 1 = TRUE |
| `int` | `I` | Direct cast |
| `guint` | `I` | Direct cast (unsigned fits in 64-bit) |
| `double` | `F` | Bitcast via memcpy |
| `GdkModifierType` | `I` | Bitmask, direct cast |
| `cairo_t*` | `I` | Opaque pointer |
| `GtkOrientation` | `I` | Enum value (0=HORIZONTAL, 1=VERTICAL) |
| `GtkAlign` | `I` | Enum value (0=FILL, 1=START, 2=END, 3=CENTER) |
| `void` | `V` | Return 0 |

### Appendix D: Minimal Closure FFI Test

Before building the full GTK wrapper, verify the trampoline pattern with this minimal test:

**test_callback_ffi.c:**
```c
#include <stdio.h>
typedef long long GVal;

/* Simulate a C library that calls back */
typedef void (*Callback)(int value, void *data);
static Callback _cb;
static void *_cb_data;

GVal register_callback(GVal closure) {
    _cb_data = (void *)closure;
    _cb = (Callback)_trampoline;
    return 0;
}

static void _trampoline(int value, void *data) {
    GVal *cl = (GVal *)data;
    GVal fn = cl[0];
    ((GVal (*)(GVal, GVal))fn)((GVal)cl, (GVal)value);
}

GVal fire_callback(GVal value) {
    if (_cb) _cb((int)value, _cb_data);
    return 0;
}
```

**test_callback.glyph:**
```
main =
  counter = [0]
  register_callback(\val ->
    counter[0] := counter[0] + val
    println("Callback fired: {counter[0]}"))
  fire_callback(10)
  fire_callback(20)
  fire_callback(30)
  println("Final: {counter[0]}")
```

Expected output:
```
Callback fired: 10
Callback fired: 30
Callback fired: 60
Final: 60
```

---

## Summary

A GTK4 library for Glyph is practical and follows naturally from the project's existing FFI architecture. The core innovation is the **callback trampoline system** -- a set of ~8-10 C functions that bridge GTK's signal system with Glyph's closure calling convention. The rest is straightforward wrapper code.

The recommended approach is:
1. **Validate** the closure trampoline pattern with a minimal test (Appendix D)
2. **Implement Tier 1** (application, window, button, label, box, signals) with a hello world example
3. **Iterate** through Tiers 2-5 based on the needs of example programs
4. **Ship as a library** (`gtk.glyph` + `gtk_ffi.c`) that can be linked into any Glyph application

The total C wrapper would be ~1,300 lines (comparable to 2.5x the asteroids FFI wrapper), the Glyph library ~120 definitions, and the build integration follows the established `build.sh` pattern used by all existing GUI examples.
