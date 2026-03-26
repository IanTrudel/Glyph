/*
 * api_ffi.c — Application-specific state singletons for the items API example.
 * Concatenated after glyph_out.c, so GVal (intptr_t) is already defined.
 */

/* api_get_items — returns a stable pointer to a Glyph array header {ptr,len,cap}
 * that persists across calls. Glyph can array_push/array_set/array_len on it. */
static long long _items_hdr[3];
static int _items_inited = 0;

GVal api_get_items(GVal dummy) {
    if (!_items_inited) {
        _items_inited = 1;
        long long* data = (long long*)malloc(16 * sizeof(long long));
        _items_hdr[0] = (long long)data;
        _items_hdr[1] = 0;
        _items_hdr[2] = 16;
    }
    return (GVal)_items_hdr;
}

/* api_get_next_id — returns current id then increments */
static long long _next_id = 1;

GVal api_get_next_id(GVal dummy) {
    return (GVal)(_next_id++);
}

/* api_swap_array — copy src array header fields into dst (for replace-in-place)
 * dst and src are Glyph array header pointers {ptr, len, cap} */
GVal api_swap_array(GVal dst, GVal src) {
    long long* d = (long long*)dst;
    long long* s = (long long*)src;
    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
    return 0;
}
