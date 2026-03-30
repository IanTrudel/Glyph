/* vie_ffi.c — Math + array helpers for vie vector editor
 *
 * Prepended before Glyph runtime.
 */
#include <math.h>
#include <string.h>
#include <stdint.h>

typedef intptr_t GVal;

/* Double <-> GVal */
static double _vd(GVal v) { double d; memcpy(&d, &v, sizeof(double)); return d; }
static GVal _vg(double d) { GVal v; memcpy(&v, &d, sizeof(double)); return v; }

/* ── Math ─────────────────────────────────────────────────────────────── */

GVal vie_ffi_sqrt(GVal x)          { return _vg(sqrt(_vd(x))); }
GVal vie_ffi_sin(GVal x)           { return _vg(sin(_vd(x))); }
GVal vie_ffi_cos(GVal x)           { return _vg(cos(_vd(x))); }
GVal vie_ffi_atan2(GVal y, GVal x) { return _vg(atan2(_vd(y), _vd(x))); }
GVal vie_ffi_fabs(GVal x)          { return _vg(fabs(_vd(x))); }

/* ── Array helpers ── layout: {GVal* data, int64_t len, int64_t cap} ── */

GVal vie_ffi_array_len(GVal arr) {
    if (!arr) return 0;
    return *(int64_t *)((char *)(void *)arr + 8);
}

GVal vie_ffi_array_clear(GVal arr) {
    if (!arr) return 0;
    *(int64_t *)((char *)(void *)arr + 8) = 0;
    return 0;
}

GVal vie_ffi_array_pop(GVal arr) {
    if (!arr) return 0;
    int64_t *len_p = (int64_t *)((char *)(void *)arr + 8);
    if (*len_p <= 0) return 0;
    GVal *data = *(GVal **)(void *)arr;
    *len_p -= 1;
    return data[*len_p];
}
