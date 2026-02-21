/// Minimal runtime stubs that get compiled into every Glyph binary.
/// These provide panic handling, basic allocation, etc.
///
/// The runtime functions are declared as extern "C" so they can be called
/// from generated code, then linked in via the object file.

/// Runtime function names that codegen emits calls to.
pub const RT_PANIC: &str = "glyph_panic";
pub const RT_ALLOC: &str = "glyph_alloc";
pub const RT_DEALLOC: &str = "glyph_dealloc";
pub const RT_PRINT: &str = "glyph_print";

/// C source for the minimal runtime, compiled and linked into the binary.
pub const RUNTIME_C: &str = r#"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void glyph_panic(const char* msg) {
    fprintf(stderr, "panic: %s\n", msg);
    exit(1);
}

void* glyph_alloc(unsigned long size) {
    void* p = malloc(size);
    if (!p) glyph_panic("out of memory");
    return p;
}

void glyph_dealloc(void* ptr) {
    free(ptr);
}

void glyph_print(const char* msg, long len) {
    fwrite(msg, 1, (size_t)len, stdout);
}
"#;
