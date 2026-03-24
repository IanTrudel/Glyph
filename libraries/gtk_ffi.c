/* gtk_ffi.c — GTK4 FFI wrapper for Glyph
 *
 * Provides GVal-ABI wrappers for GTK4 functions and callback trampolines.
 * Prepended before Glyph runtime, so we define GVal ourselves.
 *
 * Compile with: $(pkg-config --cflags --libs gtk4)
 */
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

typedef intptr_t GVal;

/* ── String conversion ─────────────────────────────────────────────────── */

/* Glyph fat string {ptr, len} → null-terminated C string (malloc'd) */
static const char* _gs(GVal glyph_str) {
    if (!glyph_str) return strdup("");
    const char *ptr = *(const char **)glyph_str;
    long long len = *(long long *)((char *)glyph_str + 8);
    char *cstr = (char *)malloc(len + 1);
    memcpy(cstr, ptr, (size_t)len);
    cstr[len] = '\0';
    return cstr;
}

/* Null-terminated C string → Glyph fat string {ptr, len} (heap) */
static GVal _gstr(const char *cstr) {
    if (!cstr) cstr = "";
    long long len = (long long)strlen(cstr);
    char *r = (char *)malloc(16 + len);
    char *d = r + 16;
    memcpy(d, cstr, (size_t)len);
    *(const char **)r = d;
    *(long long *)(r + 8) = len;
    return (GVal)r;
}

/* ── Callback trampolines ──────────────────────────────────────────────── */

/* void callback(GtkWidget*, user_data)
 * Used for: "clicked", "activate", "toggled" */
static void _tramp_void_widget(GtkWidget *w, gpointer data) {
    GVal *cl = (GVal *)data;
    ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, (GVal)w);
}

/* gboolean callback(GtkWindow*, user_data)
 * Used for: "close-request" */
static gboolean _tramp_bool_widget(GtkWindow *w, gpointer data) {
    GVal *cl = (GVal *)data;
    GVal r = ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, (GVal)w);
    return (gboolean)r;
}

/* gboolean callback(Controller*, keyval, keycode, state, data)
 * Used for: "key-pressed", "key-released" */
static gboolean _tramp_key(GtkEventControllerKey *ctrl, guint keyval,
                            guint keycode, GdkModifierType state, gpointer data) {
    GVal *cl = (GVal *)data;
    GVal r = ((GVal (*)(GVal, GVal, GVal, GVal))cl[0])(
        (GVal)cl, (GVal)keyval, (GVal)keycode, (GVal)state);
    return (gboolean)r;
}

/* gboolean callback(data)
 * Used for: g_timeout_add, g_idle_add */
static gboolean _tramp_timeout(gpointer data) {
    GVal *cl = (GVal *)data;
    GVal r = ((GVal (*)(GVal))cl[0])((GVal)cl);
    return (gboolean)r;
}

/* Direct function pointer trampoline (no closure calling convention).
 * Calls fn(widget) directly — avoids monomorphization of the target. */
static void _tramp_direct(GtkWidget *w, gpointer data) {
    GVal fn = (GVal)data;
    ((GVal (*)(GVal))fn)((GVal)w);
}

/* ── Application ───────────────────────────────────────────────────────── */

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

/* Connect activate using a direct function pointer (no closure) */
GVal gtk_ffi_on_activate_fn(GVal app, GVal fn) {
    g_signal_connect((void *)app, "activate",
                     G_CALLBACK(_tramp_direct), (gpointer)fn);
    return 0;
}

GVal gtk_ffi_app_quit(GVal app) {
    g_application_quit(G_APPLICATION((void *)app));
    return 0;
}

/* ── Window ────────────────────────────────────────────────────────────── */

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

GVal gtk_ffi_window_destroy(GVal win) {
    gtk_window_destroy(GTK_WINDOW((void *)win));
    return 0;
}

GVal gtk_ffi_on_close_request(GVal win, GVal closure) {
    g_signal_connect((void *)win, "close-request",
                     G_CALLBACK(_tramp_bool_widget), (gpointer)closure);
    return 0;
}

/* ── Button ────────────────────────────────────────────────────────────── */

GVal gtk_ffi_button_new(GVal label) {
    const char *l = _gs(label);
    GtkWidget *btn = gtk_button_new_with_label(l);
    free((void *)l);
    return (GVal)btn;
}

GVal gtk_ffi_button_set_label(GVal btn, GVal label) {
    const char *l = _gs(label);
    gtk_button_set_label(GTK_BUTTON((void *)btn), l);
    free((void *)l);
    return 0;
}

GVal gtk_ffi_on_clicked(GVal btn, GVal closure) {
    g_signal_connect((void *)btn, "clicked",
                     G_CALLBACK(_tramp_void_widget), (gpointer)closure);
    return 0;
}

/* ── Label ─────────────────────────────────────────────────────────────── */

GVal gtk_ffi_label_new(GVal text) {
    const char *t = _gs(text);
    GtkWidget *lbl = gtk_label_new(t);
    free((void *)t);
    return (GVal)lbl;
}

GVal gtk_ffi_label_set_text(GVal lbl, GVal text) {
    const char *t = _gs(text);
    gtk_label_set_label(GTK_LABEL((void *)lbl), t);
    free((void *)t);
    return 0;
}

GVal gtk_ffi_label_set_markup(GVal lbl, GVal markup) {
    const char *m = _gs(markup);
    gtk_label_set_markup(GTK_LABEL((void *)lbl), m);
    free((void *)m);
    return 0;
}

/* ── Box layout ────────────────────────────────────────────────────────── */

GVal gtk_ffi_box_new(GVal orientation, GVal spacing) {
    return (GVal)gtk_box_new((GtkOrientation)(int)orientation, (int)spacing);
}

GVal gtk_ffi_box_append(GVal box, GVal child) {
    gtk_box_append(GTK_BOX((void *)box), GTK_WIDGET((void *)child));
    return 0;
}

/* ── Widget common ─────────────────────────────────────────────────────── */

GVal gtk_ffi_widget_set_margin(GVal w, GVal top, GVal end, GVal bottom, GVal start) {
    GtkWidget *widget = GTK_WIDGET((void *)w);
    gtk_widget_set_margin_top(widget, (int)top);
    gtk_widget_set_margin_end(widget, (int)end);
    gtk_widget_set_margin_bottom(widget, (int)bottom);
    gtk_widget_set_margin_start(widget, (int)start);
    return 0;
}

GVal gtk_ffi_widget_set_hexpand(GVal w, GVal expand) {
    gtk_widget_set_hexpand(GTK_WIDGET((void *)w), (gboolean)expand);
    return 0;
}

GVal gtk_ffi_widget_set_vexpand(GVal w, GVal expand) {
    gtk_widget_set_vexpand(GTK_WIDGET((void *)w), (gboolean)expand);
    return 0;
}

GVal gtk_ffi_widget_set_halign(GVal w, GVal align) {
    gtk_widget_set_halign(GTK_WIDGET((void *)w), (GtkAlign)(int)align);
    return 0;
}

GVal gtk_ffi_widget_set_valign(GVal w, GVal align) {
    gtk_widget_set_valign(GTK_WIDGET((void *)w), (GtkAlign)(int)align);
    return 0;
}

GVal gtk_ffi_widget_set_sensitive(GVal w, GVal sensitive) {
    gtk_widget_set_sensitive(GTK_WIDGET((void *)w), (gboolean)sensitive);
    return 0;
}

GVal gtk_ffi_widget_set_visible(GVal w, GVal visible) {
    gtk_widget_set_visible(GTK_WIDGET((void *)w), (gboolean)visible);
    return 0;
}

/* ── Constants ─────────────────────────────────────────────────────────── */

GVal gtk_ffi_orientation_horizontal(GVal _d) { (void)_d; return (GVal)GTK_ORIENTATION_HORIZONTAL; }
GVal gtk_ffi_orientation_vertical(GVal _d)   { (void)_d; return (GVal)GTK_ORIENTATION_VERTICAL; }
GVal gtk_ffi_align_fill(GVal _d)    { (void)_d; return (GVal)GTK_ALIGN_FILL; }
GVal gtk_ffi_align_start(GVal _d)   { (void)_d; return (GVal)GTK_ALIGN_START; }
GVal gtk_ffi_align_end(GVal _d)     { (void)_d; return (GVal)GTK_ALIGN_END; }
GVal gtk_ffi_align_center(GVal _d)  { (void)_d; return (GVal)GTK_ALIGN_CENTER; }

/* ── Timer ─────────────────────────────────────────────────────────────── */

GVal gtk_ffi_timeout_add(GVal ms, GVal closure) {
    return (GVal)g_timeout_add((guint)ms, _tramp_timeout, (gpointer)closure);
}

/* ── Key events ────────────────────────────────────────────────────────── */

GVal gtk_ffi_key_controller_new(GVal _d) { (void)_d;
    return (GVal)gtk_event_controller_key_new();
}

GVal gtk_ffi_widget_add_controller(GVal widget, GVal ctrl) {
    gtk_widget_add_controller(GTK_WIDGET((void *)widget),
                              GTK_EVENT_CONTROLLER((void *)ctrl));
    return 0;
}

GVal gtk_ffi_on_key_pressed(GVal ctrl, GVal closure) {
    g_signal_connect((void *)ctrl, "key-pressed",
                     G_CALLBACK(_tramp_key), (gpointer)closure);
    return 0;
}

/* ── CSS styling ───────────────────────────────────────────────────────── */

GVal gtk_ffi_widget_add_css_class(GVal w, GVal cls) {
    const char *c = _gs(cls);
    gtk_widget_add_css_class(GTK_WIDGET((void *)w), c);
    free((void *)c);
    return 0;
}

GVal gtk_ffi_css_load(GVal css_str) {
    const char *css = _gs(css_str);
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(prov, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    free((void *)css);
    return (GVal)prov;
}
