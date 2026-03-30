/* cairo_ffi.c — Cairo 2D graphics FFI wrapper for Glyph
 *
 * Provides GVal-ABI wrappers for Cairo drawing functions.
 * Prepended before Glyph runtime, so we define GVal ourselves.
 * Cairo headers come from GTK4 pkg-config (programs must `use` gtk.glyph too).
 *
 * Compile with: $(pkg-config --cflags --libs gtk4)
 */
#include <cairo/cairo.h>
#include <string.h>
#include <stdint.h>

typedef intptr_t GVal;

/* ── Double helpers ───────────────────────────────────────────────────── */

static double _d(GVal v) { double d; memcpy(&d, &v, sizeof(double)); return d; }
static GVal _g(double d) { GVal v; memcpy(&v, &d, sizeof(double)); return v; }

/* ── Path operations ──────────────────────────────────────────────────── */

GVal cairo_ffi_move_to(GVal cr, GVal x, GVal y) {
    cairo_move_to((cairo_t *)(void *)cr, _d(x), _d(y));
    return 0;
}

GVal cairo_ffi_line_to(GVal cr, GVal x, GVal y) {
    cairo_line_to((cairo_t *)(void *)cr, _d(x), _d(y));
    return 0;
}

GVal cairo_ffi_curve_to(GVal cr, GVal x1, GVal y1, GVal x2, GVal y2, GVal x3, GVal y3) {
    cairo_curve_to((cairo_t *)(void *)cr, _d(x1), _d(y1), _d(x2), _d(y2), _d(x3), _d(y3));
    return 0;
}

GVal cairo_ffi_arc(GVal cr, GVal xc, GVal yc, GVal radius, GVal angle1, GVal angle2) {
    cairo_arc((cairo_t *)(void *)cr, _d(xc), _d(yc), _d(radius), _d(angle1), _d(angle2));
    return 0;
}

GVal cairo_ffi_rectangle(GVal cr, GVal x, GVal y, GVal w, GVal h) {
    cairo_rectangle((cairo_t *)(void *)cr, _d(x), _d(y), _d(w), _d(h));
    return 0;
}

GVal cairo_ffi_close_path(GVal cr) {
    cairo_close_path((cairo_t *)(void *)cr);
    return 0;
}

GVal cairo_ffi_new_path(GVal cr) {
    cairo_new_path((cairo_t *)(void *)cr);
    return 0;
}

GVal cairo_ffi_new_sub_path(GVal cr) {
    cairo_new_sub_path((cairo_t *)(void *)cr);
    return 0;
}

/* ── Drawing operations ───────────────────────────────────────────────── */

GVal cairo_ffi_stroke(GVal cr) {
    cairo_stroke((cairo_t *)(void *)cr);
    return 0;
}

GVal cairo_ffi_fill(GVal cr) {
    cairo_fill((cairo_t *)(void *)cr);
    return 0;
}

GVal cairo_ffi_stroke_preserve(GVal cr) {
    cairo_stroke_preserve((cairo_t *)(void *)cr);
    return 0;
}

GVal cairo_ffi_fill_preserve(GVal cr) {
    cairo_fill_preserve((cairo_t *)(void *)cr);
    return 0;
}

GVal cairo_ffi_paint(GVal cr) {
    cairo_paint((cairo_t *)(void *)cr);
    return 0;
}

/* ── Source / style ───────────────────────────────────────────────────── */

GVal cairo_ffi_set_source_rgba(GVal cr, GVal r, GVal g, GVal b, GVal a) {
    cairo_set_source_rgba((cairo_t *)(void *)cr, _d(r), _d(g), _d(b), _d(a));
    return 0;
}

GVal cairo_ffi_set_source_rgb(GVal cr, GVal r, GVal g, GVal b) {
    cairo_set_source_rgb((cairo_t *)(void *)cr, _d(r), _d(g), _d(b));
    return 0;
}

GVal cairo_ffi_set_line_width(GVal cr, GVal w) {
    cairo_set_line_width((cairo_t *)(void *)cr, _d(w));
    return 0;
}

GVal cairo_ffi_set_line_cap(GVal cr, GVal cap) {
    cairo_set_line_cap((cairo_t *)(void *)cr, (cairo_line_cap_t)(int)cap);
    return 0;
}

GVal cairo_ffi_set_line_join(GVal cr, GVal join) {
    cairo_set_line_join((cairo_t *)(void *)cr, (cairo_line_join_t)(int)join);
    return 0;
}

/* ── Dash pattern ─────────────────────────────────────────────────────── */

GVal cairo_ffi_set_dash(GVal cr, GVal on, GVal off) {
    double dashes[2] = { _d(on), _d(off) };
    cairo_set_dash((cairo_t *)(void *)cr, dashes, 2, 0.0);
    return 0;
}

GVal cairo_ffi_set_dash_none(GVal cr) {
    cairo_set_dash((cairo_t *)(void *)cr, NULL, 0, 0.0);
    return 0;
}

/* ── State save/restore ───────────────────────────────────────────────── */

GVal cairo_ffi_save(GVal cr) {
    cairo_save((cairo_t *)(void *)cr);
    return 0;
}

GVal cairo_ffi_restore(GVal cr) {
    cairo_restore((cairo_t *)(void *)cr);
    return 0;
}

/* ── Transforms ───────────────────────────────────────────────────────── */

GVal cairo_ffi_translate(GVal cr, GVal tx, GVal ty) {
    cairo_translate((cairo_t *)(void *)cr, _d(tx), _d(ty));
    return 0;
}

GVal cairo_ffi_scale(GVal cr, GVal sx, GVal sy) {
    cairo_scale((cairo_t *)(void *)cr, _d(sx), _d(sy));
    return 0;
}

GVal cairo_ffi_rotate(GVal cr, GVal angle) {
    cairo_rotate((cairo_t *)(void *)cr, _d(angle));
    return 0;
}

GVal cairo_ffi_identity_matrix(GVal cr) {
    cairo_identity_matrix((cairo_t *)(void *)cr);
    return 0;
}

/* ── Constants ────────────────────────────────────────────────────────── */

GVal cairo_ffi_line_cap_butt(GVal _d)   { (void)_d; return (GVal)CAIRO_LINE_CAP_BUTT; }
GVal cairo_ffi_line_cap_round(GVal _d)  { (void)_d; return (GVal)CAIRO_LINE_CAP_ROUND; }
GVal cairo_ffi_line_cap_square(GVal _d) { (void)_d; return (GVal)CAIRO_LINE_CAP_SQUARE; }

GVal cairo_ffi_line_join_miter(GVal _d) { (void)_d; return (GVal)CAIRO_LINE_JOIN_MITER; }
GVal cairo_ffi_line_join_round(GVal _d) { (void)_d; return (GVal)CAIRO_LINE_JOIN_ROUND; }
GVal cairo_ffi_line_join_bevel(GVal _d) { (void)_d; return (GVal)CAIRO_LINE_JOIN_BEVEL; }
