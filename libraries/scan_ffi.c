/* scan_ffi.c — Icon-style string scanning FFI for Glyph
 *
 * Character sets (csets): 256-bit bitsets for O(1) byte membership.
 * Scanning primitives: positional matching returning integer positions.
 *
 * Prepended before Glyph runtime via cc_prepend.
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <gc/gc.h>

typedef intptr_t GVal;

/* ── String helpers ─────────────────────────────────────────────────── */

static inline const char* _str_ptr(GVal s) {
    return *(const char**)s;
}

static inline long long _str_len(GVal s) {
    return *(long long*)((char*)s + 8);
}

/* ── Character Set Operations ───────────────────────────────────────── */
/* Csets are 32 bytes: 4 unsigned long long words = 256 bits.           */

GVal scan_cset_new(GVal dummy) {
    unsigned long long* cs = (unsigned long long*)GC_malloc(32);
    memset(cs, 0, 32);
    return (GVal)cs;
}

GVal scan_cset_from_str(GVal s) {
    unsigned long long* cs = (unsigned long long*)GC_malloc(32);
    memset(cs, 0, 32);
    const char* p = _str_ptr(s);
    long long len = _str_len(s);
    for (long long i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        cs[c / 64] |= (1ULL << (c % 64));
    }
    return (GVal)cs;
}

GVal scan_cset_range(GVal lo, GVal hi) {
    unsigned long long* cs = (unsigned long long*)GC_malloc(32);
    memset(cs, 0, 32);
    for (long long i = lo; i <= hi && i < 256; i++) {
        cs[i / 64] |= (1ULL << (i % 64));
    }
    return (GVal)cs;
}

GVal scan_cset_union(GVal a, GVal b) {
    unsigned long long* cs = (unsigned long long*)GC_malloc(32);
    unsigned long long* ap = (unsigned long long*)a;
    unsigned long long* bp = (unsigned long long*)b;
    cs[0] = ap[0] | bp[0]; cs[1] = ap[1] | bp[1];
    cs[2] = ap[2] | bp[2]; cs[3] = ap[3] | bp[3];
    return (GVal)cs;
}

GVal scan_cset_inter(GVal a, GVal b) {
    unsigned long long* cs = (unsigned long long*)GC_malloc(32);
    unsigned long long* ap = (unsigned long long*)a;
    unsigned long long* bp = (unsigned long long*)b;
    cs[0] = ap[0] & bp[0]; cs[1] = ap[1] & bp[1];
    cs[2] = ap[2] & bp[2]; cs[3] = ap[3] & bp[3];
    return (GVal)cs;
}

GVal scan_cset_compl(GVal a) {
    unsigned long long* cs = (unsigned long long*)GC_malloc(32);
    unsigned long long* ap = (unsigned long long*)a;
    cs[0] = ~ap[0]; cs[1] = ~ap[1];
    cs[2] = ~ap[2]; cs[3] = ~ap[3];
    return (GVal)cs;
}

GVal scan_cset_test(GVal cs, GVal byte) {
    unsigned char b = (unsigned char)byte;
    return (GVal)((((unsigned long long*)cs)[b / 64] >> (b % 64)) & 1);
}

GVal scan_cset_add(GVal cs, GVal byte) {
    unsigned char b = (unsigned char)byte;
    ((unsigned long long*)cs)[b / 64] |= (1ULL << (b % 64));
    return cs;
}

/* ── Scanning Primitives ────────────────────────────────────────────── */
/* All return new position on success, -1 on failure.                   */

/* Match single char in cset at position. */
GVal scan_any(GVal cs, GVal s, GVal pos) {
    const char* p = _str_ptr(s);
    long long len = _str_len(s);
    if (pos < 0 || pos >= len) return -1;
    unsigned char c = (unsigned char)p[pos];
    unsigned long long* csp = (unsigned long long*)cs;
    if ((csp[c / 64] >> (c % 64)) & 1)
        return pos + 1;
    return -1;
}

/* Consume longest run of chars in cset. Returns pos after run. */
GVal scan_many(GVal cs, GVal s, GVal pos) {
    const char* p = _str_ptr(s);
    long long len = _str_len(s);
    long long i = pos;
    unsigned long long* csp = (unsigned long long*)cs;
    while (i < len) {
        unsigned char c = (unsigned char)p[i];
        if (!((csp[c / 64] >> (c % 64)) & 1)) break;
        i++;
    }
    return (GVal)i;
}

/* Find first char in cset starting from pos. Returns position or -1. */
GVal scan_upto(GVal cs, GVal s, GVal pos) {
    const char* p = _str_ptr(s);
    long long len = _str_len(s);
    unsigned long long* csp = (unsigned long long*)cs;
    for (long long i = pos; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        if ((csp[c / 64] >> (c % 64)) & 1) return (GVal)i;
    }
    return -1;
}

/* Match single char NOT in cset at position. */
GVal scan_notany(GVal cs, GVal s, GVal pos) {
    const char* p = _str_ptr(s);
    long long len = _str_len(s);
    if (pos < 0 || pos >= len) return -1;
    unsigned char c = (unsigned char)p[pos];
    unsigned long long* csp = (unsigned long long*)cs;
    if (!((csp[c / 64] >> (c % 64)) & 1))
        return pos + 1;
    return -1;
}

/* Match literal string at position. Returns pos + literal_len or -1. */
GVal scan_match_lit(GVal lit, GVal s, GVal pos) {
    const char* lp = _str_ptr(lit);
    long long ll = _str_len(lit);
    const char* sp = _str_ptr(s);
    long long sl = _str_len(s);
    if (pos + ll > sl) return -1;
    if (memcmp(sp + pos, lp, (size_t)ll) == 0) return pos + ll;
    return -1;
}

/* Match balanced delimiters. Returns position after closing delimiter or -1. */
GVal scan_bal(GVal open_byte, GVal close_byte, GVal s, GVal pos) {
    const char* p = _str_ptr(s);
    long long len = _str_len(s);
    if (pos < 0 || pos >= len) return -1;
    if ((unsigned char)p[pos] != (unsigned char)open_byte) return -1;
    long long depth = 1;
    long long i = pos + 1;
    while (i < len && depth > 0) {
        unsigned char c = (unsigned char)p[i];
        if (c == (unsigned char)open_byte) depth++;
        else if (c == (unsigned char)close_byte) depth--;
        i++;
    }
    return depth == 0 ? (GVal)i : (GVal)-1;
}
