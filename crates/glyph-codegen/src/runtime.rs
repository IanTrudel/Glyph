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
pub const RT_STR_CONCAT: &str = "glyph_str_concat";
pub const RT_INT_TO_STR: &str = "glyph_int_to_str";
pub const RT_ARRAY_BOUNDS_CHECK: &str = "glyph_array_bounds_check";
pub const RT_PANIC_STR: &str = "glyph_panic_str";
pub const RT_ARRAY_NEW: &str = "glyph_array_new";
pub const RT_ARRAY_PUSH: &str = "glyph_array_push";
pub const RT_REALLOC: &str = "glyph_realloc";
pub const RT_STR_EQ: &str = "glyph_str_eq";
pub const RT_STR_LEN: &str = "glyph_str_len";
pub const RT_STR_SLICE: &str = "glyph_str_slice";
pub const RT_STR_CHAR_AT: &str = "glyph_str_char_at";
pub const RT_READ_FILE: &str = "glyph_read_file";
pub const RT_WRITE_FILE: &str = "glyph_write_file";
pub const RT_EXIT: &str = "glyph_exit";
pub const RT_ARGS: &str = "glyph_args";
pub const RT_PRINTLN: &str = "glyph_println";
pub const RT_EPRINTLN: &str = "glyph_eprintln";
pub const RT_STR_TO_CSTR: &str = "glyph_str_to_cstr";
pub const RT_CSTR_TO_STR: &str = "glyph_cstr_to_str";
pub const RT_ARRAY_SET: &str = "glyph_array_set";
pub const RT_ARRAY_POP: &str = "glyph_array_pop";
pub const RT_STR_TO_INT: &str = "glyph_str_to_int";
pub const RT_ARRAY_LEN: &str = "glyph_array_len";
pub const RT_ARR_GET_STR: &str = "glyph_arr_get_str";
pub const RT_SYSTEM: &str = "glyph_system";
pub const RT_SB_NEW: &str = "glyph_sb_new";
pub const RT_SB_APPEND: &str = "glyph_sb_append";
pub const RT_SB_BUILD: &str = "glyph_sb_build";
pub const RT_RAW_SET: &str = "glyph_raw_set";
pub const RT_READ_LINE: &str = "glyph_read_line";
pub const RT_FLUSH: &str = "glyph_flush";
pub const RT_BITSET_NEW: &str = "glyph_bitset_new";
pub const RT_BITSET_SET: &str = "glyph_bitset_set";
pub const RT_BITSET_TEST: &str = "glyph_bitset_test";
pub const RT_ARRAY_FREEZE: &str = "glyph_array_freeze";
pub const RT_ARRAY_FROZEN: &str = "glyph_array_frozen";
pub const RT_ARRAY_THAW: &str = "glyph_array_thaw";
pub const RT_HM_NEW: &str = "glyph_hm_new";
pub const RT_HM_SET: &str = "glyph_hm_set";
pub const RT_HM_GET: &str = "glyph_hm_get";
pub const RT_HM_DEL: &str = "glyph_hm_del";
pub const RT_HM_KEYS: &str = "glyph_hm_keys";
pub const RT_HM_LEN: &str = "glyph_hm_len";
pub const RT_HM_HAS: &str = "glyph_hm_has";
pub const RT_HM_FREEZE: &str = "glyph_hm_freeze";
pub const RT_HM_FROZEN: &str = "glyph_hm_frozen";
pub const RT_HM_GET_FLOAT: &str = "glyph_hm_get_float";
pub const RT_HM_SET_FLOAT: &str = "glyph_hm_set_float";
pub const RT_REF: &str = "glyph_ref";
pub const RT_DEREF: &str = "glyph_deref";
pub const RT_SET_REF: &str = "glyph_set_ref";
pub const RT_GENERATE: &str = "glyph_generate";
pub const RT_FILE_EXISTS: &str = "glyph_file_exists";
pub const RT_OK: &str = "glyph_ok";
pub const RT_ERR: &str = "glyph_err";
pub const RT_PANIC_UNWRAP: &str = "glyph_panic_unwrap";

// SQLite wrapper functions (only linked when program uses sqlite3)
pub const RT_DB_OPEN: &str = "glyph_db_open";
pub const RT_DB_CLOSE: &str = "glyph_db_close";
pub const RT_DB_EXEC: &str = "glyph_db_exec";
pub const RT_DB_QUERY_ROWS: &str = "glyph_db_query_rows";
pub const RT_DB_QUERY_ONE: &str = "glyph_db_query_one";

/// C source for the minimal runtime, compiled and linked into the binary.
/// Strings are represented as {ptr, len} structs (16 bytes).
pub const RUNTIME_C: &str = r#"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

__thread const char* _glyph_current_fn = "(unknown)";

static void _glyph_sigsegv(int sig) {
    fprintf(stderr, "segfault in function: %s\n", _glyph_current_fn);
    signal(sig, SIG_DFL);
    raise(sig);
}

void glyph_panic(const char* msg) {
    fprintf(stderr, "panic: %s\n", msg);
    exit(1);
}

void* glyph_alloc(unsigned long long size) {
    void* p = malloc(size);
    if (!p) glyph_panic("out of memory");
    return p;
}

void glyph_dealloc(void* ptr) {
    free(ptr);
}

/* String struct: { const char* ptr; long long len; } = 16 bytes
 * All string functions take/return pointers to these structs.
 */

/* Print a string struct to stdout. Returns the length. */
long long glyph_print(void* str_struct) {
    const char* ptr = *(const char**)str_struct;
    long long len = *(long long*)((char*)str_struct + 8);
    fwrite(ptr, 1, (size_t)len, stdout);
    fflush(stdout);
    return 0;
}

/* Concatenate two strings. Returns pointer to heap-allocated string struct. */
void* glyph_str_concat(void* a_struct, void* b_struct) {
    const char* a_ptr = *(const char**)a_struct;
    long long a_len = *(long long*)((char*)a_struct + 8);
    const char* b_ptr = *(const char**)b_struct;
    long long b_len = *(long long*)((char*)b_struct + 8);
    long long total = a_len + b_len;
    /* Allocate: 16 bytes for struct + total bytes for data */
    char* result = (char*)malloc(16 + total);
    if (!result) glyph_panic("out of memory");
    char* data = result + 16;
    memcpy(data, a_ptr, a_len);
    memcpy(data + a_len, b_ptr, b_len);
    *(const char**)result = data;
    *(long long*)(result + 8) = total;
    return result;
}

/* Convert an integer to a string. Returns pointer to heap-allocated string struct. */
void* glyph_int_to_str(long long n) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", n);
    char* result = (char*)malloc(16 + len);
    if (!result) glyph_panic("out of memory");
    char* data = result + 16;
    memcpy(data, buf, len);
    *(const char**)result = data;
    *(long long*)(result + 8) = len;
    return result;
}

/* Panic with a Glyph string struct (ptr+len). */
void glyph_panic_str(void* str_struct) {
    const char* ptr = *(const char**)str_struct;
    long long len = *(long long*)((char*)str_struct + 8);
    fprintf(stderr, "panic: %.*s\n", (int)len, ptr);
    exit(1);
}

/* Array bounds check: panics if index >= len or index < 0. */
void glyph_array_bounds_check(long long index, long long len) {
    if (index < 0 || index >= len) {
        fprintf(stderr, "panic: array index %lld out of bounds (len %lld)\n", index, len);
        exit(1);
    }
}

/* Create a new empty array header (stack-allocated by caller).
 * Returns pointer to heap-allocated data with initial capacity.
 * Header: {ptr, len, cap} caller writes to stack. */
void* glyph_array_new(long long cap) {
    if (cap <= 0) cap = 4;
    void* data = malloc(cap * 8);
    if (!data) glyph_panic("out of memory");
    return data;
}

/* Push a value onto an array. Header is {ptr, len, cap} at header_ptr.
 * Returns (possibly new) data pointer. Caller must update header. */
void* glyph_array_push(void* header_ptr, long long value) {
    long long* header = (long long*)header_ptr;
    if (header[2] < 0) glyph_panic("push on frozen array");
    long long* data_ptr_slot = &header[0];
    long long len = header[1];
    long long cap = header[2];
    long long* data = (long long*)*data_ptr_slot;

    if (len >= cap) {
        cap = cap * 2;
        if (cap < 4) cap = 4;
        long long* new_data = (long long*)malloc(cap * 8);
        if (!new_data) glyph_panic("out of memory");
        if (data) {
            memcpy(new_data, data, len * 8);
            free(data);
        }
        data = new_data;
        header[0] = (long long)data;
        header[2] = cap;
    }
    data[len] = value;
    header[1] = len + 1;
    return (void*)data;
}

/* Realloc. */
void* glyph_realloc(void* ptr, long long new_size) {
    void* p = realloc(ptr, (size_t)new_size);
    if (!p && new_size > 0) glyph_panic("out of memory");
    return p;
}

/* String equality (compares by value). Returns 1 if equal, 0 otherwise. */
long long glyph_str_eq(void* a_struct, void* b_struct) {
    const char* a_ptr = *(const char**)a_struct;
    long long a_len = *(long long*)((char*)a_struct + 8);
    const char* b_ptr = *(const char**)b_struct;
    long long b_len = *(long long*)((char*)b_struct + 8);
    if (a_len != b_len) return 0;
    return memcmp(a_ptr, b_ptr, (size_t)a_len) == 0 ? 1 : 0;
}

/* String length (returns len field). */
long long glyph_str_len(void* str_struct) {
    return *(long long*)((char*)str_struct + 8);
}

/* String slice: str[start..end]. Returns heap-allocated string struct. */
void* glyph_str_slice(void* str_struct, long long start, long long end) {
    const char* ptr = *(const char**)str_struct;
    long long len = *(long long*)((char*)str_struct + 8);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) {
        /* empty string */
        char* result = (char*)malloc(16);
        if (!result) glyph_panic("out of memory");
        *(const char**)result = "";
        *(long long*)(result + 8) = 0;
        return result;
    }
    long long slice_len = end - start;
    char* result = (char*)malloc(16 + slice_len);
    if (!result) glyph_panic("out of memory");
    char* data = result + 16;
    memcpy(data, ptr + start, (size_t)slice_len);
    *(const char**)result = data;
    *(long long*)(result + 8) = slice_len;
    return result;
}

/* Char at index (returns byte as i64, -1 if out of bounds). */
long long glyph_str_char_at(void* str_struct, long long index) {
    const char* ptr = *(const char**)str_struct;
    long long len = *(long long*)((char*)str_struct + 8);
    if (index < 0 || index >= len) return -1;
    return (long long)(unsigned char)ptr[index];
}

/* Check if a file exists. Returns 1 if exists, 0 otherwise. */
long long glyph_file_exists(void* path_struct) {
    const char* path_ptr = *(const char**)path_struct;
    long long path_len = *(long long*)((char*)path_struct + 8);
    char* cpath = (char*)malloc(path_len + 1);
    if (!cpath) glyph_panic("out of memory");
    memcpy(cpath, path_ptr, path_len);
    cpath[path_len] = '\0';
    FILE* f = fopen(cpath, "rb");
    free(cpath);
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Read entire file into a string struct. Returns heap-allocated string struct.
 * On failure, returns a struct with ptr=NULL, len=-1. */
void* glyph_read_file(void* path_struct) {
    const char* path_ptr = *(const char**)path_struct;
    long long path_len = *(long long*)((char*)path_struct + 8);

    /* Null-terminate the path */
    char* cpath = (char*)malloc(path_len + 1);
    if (!cpath) glyph_panic("out of memory");
    memcpy(cpath, path_ptr, path_len);
    cpath[path_len] = '\0';

    FILE* f = fopen(cpath, "rb");
    free(cpath);
    if (!f) {
        /* Return error: {NULL, -1} */
        char* result = (char*)malloc(16);
        if (!result) glyph_panic("out of memory");
        *(const char**)result = NULL;
        *(long long*)(result + 8) = -1;
        return result;
    }
    fseek(f, 0, SEEK_END);
    long long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* result = (char*)malloc(16 + fsize);
    if (!result) { fclose(f); glyph_panic("out of memory"); }
    char* data = result + 16;
    fread(data, 1, (size_t)fsize, f);
    fclose(f);
    *(const char**)result = data;
    *(long long*)(result + 8) = fsize;
    return result;
}

/* Write string to file. Returns 0 on success, -1 on failure. */
long long glyph_write_file(void* path_struct, void* content_struct) {
    const char* path_ptr = *(const char**)path_struct;
    long long path_len = *(long long*)((char*)path_struct + 8);
    const char* data_ptr = *(const char**)content_struct;
    long long data_len = *(long long*)((char*)content_struct + 8);

    char* cpath = (char*)malloc(path_len + 1);
    if (!cpath) glyph_panic("out of memory");
    memcpy(cpath, path_ptr, path_len);
    cpath[path_len] = '\0';

    FILE* f = fopen(cpath, "wb");
    free(cpath);
    if (!f) return -1;
    size_t written = fwrite(data_ptr, 1, (size_t)data_len, f);
    fclose(f);
    return (written == (size_t)data_len) ? 0 : -1;
}

/* Exit with code. */
void glyph_exit(long long code) {
    exit((int)code);
}

/* Global argc/argv set by main wrapper. */
static int g_argc = 0;
static char** g_argv = NULL;

void glyph_set_args(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
}

/* Get command-line args as array of string structs.
 * Returns a heap-allocated array header {ptr, len, cap}. */
void* glyph_args(void) {
    long long count = (long long)g_argc;
    /* Each element is a pointer to a 16-byte string struct */
    long long* data = (long long*)malloc(count * 8);
    if (!data) glyph_panic("out of memory");
    for (int i = 0; i < g_argc; i++) {
        long long slen = (long long)strlen(g_argv[i]);
        char* s = (char*)malloc(16);
        if (!s) glyph_panic("out of memory");
        *(const char**)s = g_argv[i];
        *(long long*)(s + 8) = slen;
        data[i] = (long long)s;
    }
    /* Return array header: {ptr, len, cap} */
    long long* header = (long long*)malloc(24);
    if (!header) glyph_panic("out of memory");
    header[0] = (long long)data;
    header[1] = count;
    header[2] = count;
    return (void*)header;
}

/* Print string with newline to stdout. */
long long glyph_println(void* str_struct) {
    const char* ptr = *(const char**)str_struct;
    long long len = *(long long*)((char*)str_struct + 8);
    fwrite(ptr, 1, (size_t)len, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    return 0;
}

/* Print string with newline to stderr. */
long long glyph_eprintln(void* str_struct) {
    const char* ptr = *(const char**)str_struct;
    long long len = *(long long*)((char*)str_struct + 8);
    fwrite(ptr, 1, (size_t)len, stderr);
    fputc('\n', stderr);
    fflush(stderr);
    return 0;
}

/* Convert Glyph string to null-terminated C string (heap-allocated). */
char* glyph_str_to_cstr(void* str_struct) {
    const char* ptr = *(const char**)str_struct;
    long long len = *(long long*)((char*)str_struct + 8);
    char* cstr = (char*)malloc(len + 1);
    if (!cstr) glyph_panic("out of memory");
    memcpy(cstr, ptr, (size_t)len);
    cstr[len] = '\0';
    return cstr;
}

/* Set element in array. Header is {ptr, len, cap}. Returns 0. */
long long glyph_array_set(void* header_ptr, long long index, long long value) {
    long long* header = (long long*)header_ptr;
    if (header[2] < 0) glyph_panic("set on frozen array");
    long long* data = (long long*)header[0];
    long long len = header[1];
    if (index < 0 || index >= len) {
        fprintf(stderr, "panic: array set index %lld out of bounds (len %lld)\n", index, len);
        exit(1);
    }
    data[index] = value;
    return 0;
}

/* Raw memory set: directly sets ((long long*)ptr)[idx] = val. */
long long glyph_raw_set(long long ptr, long long idx, long long val) {
    ((long long*)ptr)[idx] = val;
    return 0;
}

/* Pop last element from array. Returns the removed element. */
long long glyph_array_pop(void* header_ptr) {
    long long* header = (long long*)header_ptr;
    if (header[2] < 0) glyph_panic("pop on frozen array");
    long long* data = (long long*)header[0];
    long long len = header[1];
    if (len <= 0) {
        fprintf(stderr, "panic: array pop on empty array\n");
        exit(1);
    }
    header[1] = len - 1;
    return data[len - 1];
}

/* Get array length from header {ptr, len, cap}. */
long long glyph_array_len(void* header_ptr) {
    long long* header = (long long*)header_ptr;
    return header[1];
}

/* Get array element as a raw GVal (for heterogeneous arrays). */
long long glyph_arr_get_str(void* header_ptr, long long index) {
    long long* header = (long long*)header_ptr;
    long long* data = (long long*)header[0];
    return data[index];
}

/* Convert string to integer. Returns the parsed integer or 0 on failure. */
long long glyph_str_to_int(void* str_struct) {
    const char* ptr = *(const char**)str_struct;
    long long len = *(long long*)((char*)str_struct + 8);
    long long result = 0;
    int sign = 1;
    long long i = 0;
    if (i < len && ptr[i] == '-') { sign = -1; i++; }
    while (i < len && ptr[i] >= '0' && ptr[i] <= '9') {
        result = result * 10 + (ptr[i] - '0');
        i++;
    }
    return result * sign;
}

/* Convert null-terminated C string to Glyph string struct. */
void* glyph_cstr_to_str(const char* cstr) {
    if (!cstr) {
        char* result = (char*)malloc(16);
        if (!result) glyph_panic("out of memory");
        *(const char**)result = "";
        *(long long*)(result + 8) = 0;
        return result;
    }
    long long len = (long long)strlen(cstr);
    char* result = (char*)malloc(16 + len);
    if (!result) glyph_panic("out of memory");
    char* data = result + 16;
    memcpy(data, cstr, (size_t)len);
    *(const char**)result = data;
    *(long long*)(result + 8) = len;
    return result;
}

/* Execute a shell command. Returns exit code (0 = success). */
long long glyph_system(void* cmd_struct) {
    char* cmd = glyph_str_to_cstr(cmd_struct);
    int rc = system(cmd);
    free(cmd);
    if (rc == -1) return -1;
    return (long long)((rc >> 8) & 0xFF);
}

/* StringBuilder: {buf, len, cap} — mutable string buffer for O(n) building. */
void* glyph_sb_new(void) {
    long long cap = 64;
    long long* sb = (long long*)malloc(24);
    if (!sb) glyph_panic("out of memory");
    char* buf = (char*)malloc(cap);
    if (!buf) glyph_panic("out of memory");
    sb[0] = (long long)buf;
    sb[1] = 0;     /* len */
    sb[2] = cap;
    return (void*)sb;
}

void* glyph_sb_append(void* sb_ptr, void* str_struct) {
    long long* sb = (long long*)sb_ptr;
    const char* ptr = *(const char**)str_struct;
    long long slen = *(long long*)((char*)str_struct + 8);
    long long len = sb[1];
    long long cap = sb[2];
    long long need = len + slen;
    if (need > cap) {
        while (cap < need) cap *= 2;
        char* newbuf = (char*)malloc(cap);
        if (!newbuf) glyph_panic("out of memory");
        memcpy(newbuf, (char*)sb[0], (size_t)len);
        free((char*)sb[0]);
        sb[0] = (long long)newbuf;
        sb[2] = cap;
    }
    memcpy((char*)sb[0] + len, ptr, (size_t)slen);
    sb[1] = need;
    return sb_ptr;
}

void* glyph_sb_build(void* sb_ptr) {
    long long* sb = (long long*)sb_ptr;
    long long len = sb[1];
    char* result = (char*)malloc(16 + len);
    if (!result) glyph_panic("out of memory");
    char* data = result + 16;
    memcpy(data, (char*)sb[0], (size_t)len);
    *(const char**)result = data;
    *(long long*)(result + 8) = len;
    free((char*)sb[0]);
    free(sb);
    return result;
}

/* Read one line from stdin, return as Glyph string. Returns empty string on EOF. */
void* glyph_read_line(long long dummy) {
    char buf[65536];
    if (!fgets(buf, sizeof(buf), stdin)) {
        void* s = malloc(16);
        char* e = malloc(1); e[0] = 0;
        *(const char**)s = e;
        *(long long*)((char*)s + 8) = 0;
        return s;
    }
    long long len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') len--;
    char* data = malloc(len + 1);
    memcpy(data, buf, len);
    data[len] = 0;
    void* s = malloc(16);
    *(const char**)s = data;
    *(long long*)((char*)s + 8) = len;
    return s;
}

/* Flush stdout. */
long long glyph_flush(long long dummy) {
    fflush(stdout);
    return 0;
}

/* Bitset: calloc-based bit array for O(1) integer set membership. */
long long glyph_bitset_new(long long capacity) {
    long long words = (capacity + 63) / 64;
    if (words < 1) words = 1;
    unsigned long long* bits = (unsigned long long*)calloc(words, 8);
    if (!bits) glyph_panic("out of memory");
    return (long long)bits;
}
long long glyph_bitset_set(long long bs, long long idx) {
    ((unsigned long long*)bs)[idx / 64] |= (1ULL << (idx % 64));
    return 0;
}
long long glyph_bitset_test(long long bs, long long idx) {
    return (((unsigned long long*)bs)[idx / 64] >> (idx % 64)) & 1;
}

/* === Array/Map Freeze (immutability bit in cap sign bit) === */
long long glyph_array_freeze(long long hdr) {
    long long* h = (long long*)hdr;
    if (h[2] >= 0) h[2] = h[2] | (1LL << 63);
    return hdr;
}
long long glyph_array_frozen(long long hdr) {
    return ((long long*)hdr)[2] < 0 ? 1 : 0;
}
long long glyph_array_thaw(long long hdr) {
    long long* h = (long long*)hdr;
    long long len = h[1];
    long long cap = h[2] & 0x7FFFFFFFFFFFFFFFLL;
    if (cap < len) cap = len;
    long long* nh = (long long*)malloc(24);
    long long* nd = (long long*)malloc(cap * 8);
    memcpy(nd, (void*)h[0], len * 8);
    nh[0] = (long long)nd; nh[1] = len; nh[2] = cap;
    return (long long)nh;
}
/* === Hash Map (open addressing, FNV-1a) === */
static long long glyph_hm_hash(long long k) {
    unsigned char* s = (unsigned char*)((long long*)k)[0];
    long long len = ((long long*)k)[1];
    unsigned long long h = 14695981039346656037ULL;
    long long i; for (i = 0; i < len; i++) { h ^= s[i]; h *= 1099511628211ULL; }
    return (long long)(h & 0x7FFFFFFFFFFFFFFFLL);
}
static long long glyph_hm_keq(long long a, long long b) { return glyph_str_eq((void*)a, (void*)b); }
long long glyph_hm_new() {
    long long cap = 8; long long* h = (long long*)malloc(24);
    long long* d = (long long*)malloc(cap*24); memset(d, 0, cap*24);
    h[0] = (long long)d; h[1] = 0; h[2] = cap; return (long long)h;
}
long long glyph_hm_len(long long m) { return ((long long*)m)[1]; }
long long glyph_hm_has(long long m, long long k) {
    long long* h = (long long*)m; long long cap = h[2] & 0x7FFFFFFFFFFFFFFFLL; long long* d = (long long*)h[0];
    long long idx = glyph_hm_hash(k) % cap;
    long long i; for (i = 0; i < cap; i++) {
        long long s = ((idx+i) % cap) * 3;
        if (d[s+2] == 0) return 0;
        if (d[s+2] == 1 && glyph_hm_keq(d[s], k)) return 1;
    }
    return 0;
}
long long glyph_hm_get(long long m, long long k) {
    long long* h = (long long*)m; long long cap = h[2] & 0x7FFFFFFFFFFFFFFFLL; long long* d = (long long*)h[0];
    long long idx = glyph_hm_hash(k) % cap;
    long long i; for (i = 0; i < cap; i++) {
        long long s = ((idx+i) % cap) * 3;
        if (d[s+2] == 0) return 0;
        if (d[s+2] == 1 && glyph_hm_keq(d[s], k)) return d[s+1];
    }
    return 0;
}
static void glyph_hm_resize(long long* h) {
    long long oc = h[2] & 0x7FFFFFFFFFFFFFFFLL; long long* od = (long long*)h[0];
    long long nc = oc * 2; long long* nd = (long long*)malloc(nc*24); memset(nd, 0, nc*24);
    long long i; for (i = 0; i < oc; i++) {
        long long os = i * 3;
        if (od[os+2] == 1) {
            long long idx = glyph_hm_hash(od[os]) % nc;
            long long j; for (j = 0; j < nc; j++) {
                long long ns = ((idx+j) % nc) * 3;
                if (nd[ns+2] == 0) { nd[ns] = od[os]; nd[ns+1] = od[os+1]; nd[ns+2] = 1; break; }
            }
        }
    }
    h[0] = (long long)nd; h[2] = nc;
}
long long glyph_hm_set(long long m, long long k, long long v) {
    long long* h = (long long*)m;
    if (h[2] < 0) { fprintf(stderr, "set on frozen map\n"); exit(1); }
    long long cap = h[2]; long long* d = (long long*)h[0];
    long long idx = glyph_hm_hash(k) % cap; long long tomb = -1;
    long long i; for (i = 0; i < cap; i++) {
        long long s = ((idx+i) % cap) * 3;
        if (d[s+2] == 0) {
            if (tomb >= 0) s = tomb;
            d[s] = k; d[s+1] = v; d[s+2] = 1; h[1]++;
            if (h[1]*10 > cap*7) glyph_hm_resize(h); return m;
        }
        if (d[s+2] == 2 && tomb < 0) tomb = s;
        if (d[s+2] == 1 && glyph_hm_keq(d[s], k)) { d[s+1] = v; return m; }
    }
    if (tomb >= 0) {
        d[tomb] = k; d[tomb+1] = v; d[tomb+2] = 1; h[1]++;
        if (h[1]*10 > cap*7) glyph_hm_resize(h); return m;
    }
    glyph_hm_resize(h); return glyph_hm_set(m, k, v);
}
long long glyph_hm_del(long long m, long long k) {
    long long* h = (long long*)m;
    if (h[2] < 0) { fprintf(stderr, "del on frozen map\n"); exit(1); }
    long long cap = h[2]; long long* d = (long long*)h[0];
    long long idx = glyph_hm_hash(k) % cap;
    long long i; for (i = 0; i < cap; i++) {
        long long s = ((idx+i) % cap) * 3;
        if (d[s+2] == 0) return m;
        if (d[s+2] == 1 && glyph_hm_keq(d[s], k)) { d[s+2] = 2; h[1]--; return m; }
    }
    return m;
}
long long glyph_hm_keys(long long m) {
    long long* h = (long long*)m; long long n = h[1]; long long cap = h[2] & 0x7FFFFFFFFFFFFFFFLL; long long* d = (long long*)h[0];
    long long* kd = (long long*)malloc((n+1)*8);
    long long* kh = (long long*)malloc(24);
    long long i, j = 0;
    for (i = 0; i < cap; i++) { long long s = i*3; if (d[s+2] == 1) kd[j++] = d[s]; }
    kh[0] = (long long)kd; kh[1] = j; kh[2] = j | (1LL << 63); return (long long)kh;
}
long long glyph_hm_get_float(long long m, long long k) { return glyph_hm_get(m, k); }
long long glyph_hm_set_float(long long m, long long k, long long v) { return glyph_hm_set(m, k, v); }
long long glyph_hm_freeze(long long hdr) {
    long long* h = (long long*)hdr;
    if (h[2] >= 0) h[2] = h[2] | (1LL << 63);
    return hdr;
}
long long glyph_hm_frozen(long long hdr) {
    return ((long long*)hdr)[2] < 0 ? 1 : 0;
}

/* === Ref type (mutable cell, 8 bytes) === */
long long glyph_ref(long long val) {
    long long* r = (long long*)malloc(8);
    r[0] = val;
    return (long long)r;
}
long long glyph_deref(long long r) {
    return ((long long*)r)[0];
}
long long glyph_set_ref(long long r, long long val) {
    ((long long*)r)[0] = val;
    return 0;
}

/* === Array generate (declarative construction, result is frozen) === */
long long glyph_generate(long long n, long long fn) {
    long long len = n;
    long long* hdr = (long long*)malloc(24);
    long long* data = (long long*)malloc(len * 8);
    long long i;
    for (i = 0; i < len; i++) {
        long long (*fp)(long long, long long) = (long long(*)(long long,long long))((long long*)fn)[0];
        data[i] = fp(fn, i);
    }
    hdr[0] = (long long)data;
    hdr[1] = len;
    hdr[2] = len | (1LL << 63);
    return (long long)hdr;
}

/* === Result type (ok/err) === */
long long glyph_ok(long long val) {
    long long* r = (long long*)malloc(16); r[0] = 0; r[1] = val; return (long long)r;
}
long long glyph_err(long long msg) {
    long long* r = (long long*)malloc(16); r[0] = 1; r[1] = msg; return (long long)r;
}
long long glyph_panic_unwrap(long long variant, long long fn_name) {
    long long* v = (long long*)variant; long long payload = v[1];
    const char* fn = *(const char**)fn_name;
    int fn_len = (int)*(long long*)((char*)fn_name+8);
    if (payload > (long long)4096) {
        long long plen = *(long long*)((char*)payload+8);
        if (plen > 0 && plen < 100000) {
            const char* ps = *(const char**)payload;
            fprintf(stderr, "panic: unwrap failed: %.*s (in %.*s)\n", (int)plen, ps, fn_len, fn);
            exit(1);
        }
    }
    fprintf(stderr, "panic: unwrap failed in %.*s\n", fn_len, fn);
    exit(1); return 0;
}
long long glyph_try_read_file(long long path) {
    long long result = (long long)glyph_read_file((void*)path);
    if (*(long long*)((char*)result+8) < 0) {
        const char* m = "file read failed";
        char* s = (char*)malloc(16); *(const char**)s = m; *(long long*)(s+8) = 16;
        return glyph_err((long long)s);
    }
    return glyph_ok(result);
}
long long glyph_try_write_file(long long path, long long content) {
    long long result = glyph_write_file((void*)path, (void*)content);
    if (result < 0) {
        const char* m = "file write failed";
        char* s = (char*)malloc(16); *(const char**)s = m; *(long long*)(s+8) = 17;
        return glyph_err((long long)s);
    }
    return glyph_ok(0);
}

/* Entry point wrapper: captures argc/argv, then calls glyph_main. */
extern long long glyph_main(void);
int main(int argc, char** argv) {
    signal(SIGSEGV, _glyph_sigsegv);
    glyph_set_args(argc, argv);
    return (int)glyph_main();
}
"#;

/// SQLite runtime — only compiled+linked when a program uses glyph_db_* functions.
/// Requires linking with -lsqlite3.
pub const RUNTIME_SQLITE_C: &str = r#"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

/* Forward declarations from the main runtime. */
extern void glyph_panic(const char* msg);
extern void* glyph_cstr_to_str(const char* cstr);
extern char* glyph_str_to_cstr(void* str_struct);

/* Open a database. Returns opaque handle (i64). */
long long glyph_db_open(void* path_struct) {
    char* cpath = glyph_str_to_cstr(path_struct);
    sqlite3* db = NULL;
    int rc = sqlite3_open(cpath, &db);
    free(cpath);
    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return 0; /* null = error */
    }
    return (long long)db;
}

/* Close a database handle. */
void glyph_db_close(long long db_handle) {
    if (db_handle) sqlite3_close((sqlite3*)db_handle);
}

/* Execute SQL (no result rows). Returns 0 on success, -1 on error. */
long long glyph_db_exec(long long db_handle, void* sql_struct) {
    sqlite3* db = (sqlite3*)db_handle;
    char* sql = glyph_str_to_cstr(sql_struct);
    char* errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    free(sql);
    if (errmsg) {
        fprintf(stderr, "sqlite error: %s\n", errmsg);
        sqlite3_free(errmsg);
    }
    return (rc == SQLITE_OK) ? 0 : -1;
}

/* Query returning rows as an array of arrays of strings.
 * Returns: array header {ptr, len, cap} where each element is
 * an array header {ptr, len, cap} of string struct pointers.
 * This gives: [[S]] — array of rows, each row is array of column values. */
void* glyph_db_query_rows(long long db_handle, void* sql_struct) {
    sqlite3* db = (sqlite3*)db_handle;
    char* sql = glyph_str_to_cstr(sql_struct);

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    free(sql);

    /* Result array (of row arrays) */
    long long row_cap = 16;
    long long row_count = 0;
    long long* rows = (long long*)malloc(row_cap * 8);
    if (!rows) glyph_panic("out of memory");

    if (rc != SQLITE_OK) {
        /* Return empty array on error */
        long long* header = (long long*)malloc(24);
        if (!header) glyph_panic("out of memory");
        header[0] = (long long)rows;
        header[1] = 0;
        header[2] = row_cap;
        return (void*)header;
    }

    int col_count = sqlite3_column_count(stmt);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        /* Build row: array of string structs */
        long long* col_data = (long long*)malloc(col_count * 8);
        if (!col_data) glyph_panic("out of memory");

        for (int c = 0; c < col_count; c++) {
            int type = sqlite3_column_type(stmt, c);
            if (type == SQLITE_NULL) {
                col_data[c] = (long long)glyph_cstr_to_str("");
            } else if (type == SQLITE_INTEGER) {
                long long val = sqlite3_column_int64(stmt, c);
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", val);
                col_data[c] = (long long)glyph_cstr_to_str(buf);
            } else {
                const char* text = (const char*)sqlite3_column_text(stmt, c);
                col_data[c] = (long long)glyph_cstr_to_str(text ? text : "");
            }
        }

        /* Build row header */
        long long* row_header = (long long*)malloc(24);
        if (!row_header) glyph_panic("out of memory");
        row_header[0] = (long long)col_data;
        row_header[1] = (long long)col_count;
        row_header[2] = (long long)col_count;

        /* Grow rows array if needed */
        if (row_count >= row_cap) {
            row_cap *= 2;
            long long* new_rows = (long long*)malloc(row_cap * 8);
            if (!new_rows) glyph_panic("out of memory");
            memcpy(new_rows, rows, row_count * 8);
            free(rows);
            rows = new_rows;
        }
        rows[row_count++] = (long long)row_header;
    }

    sqlite3_finalize(stmt);

    /* Build outer header */
    long long* header = (long long*)malloc(24);
    if (!header) glyph_panic("out of memory");
    header[0] = (long long)rows;
    header[1] = row_count;
    header[2] = row_cap;
    return (void*)header;
}

/* Query returning a single string value (first column of first row).
 * Returns string struct pointer, or empty string if no result. */
void* glyph_db_query_one(long long db_handle, void* sql_struct) {
    sqlite3* db = (sqlite3*)db_handle;
    char* sql = glyph_str_to_cstr(sql_struct);

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    free(sql);

    if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
        if (stmt) sqlite3_finalize(stmt);
        return glyph_cstr_to_str("");
    }

    const char* text = (const char*)sqlite3_column_text(stmt, 0);
    void* result = glyph_cstr_to_str(text ? text : "");
    sqlite3_finalize(stmt);
    return result;
}
"#;
