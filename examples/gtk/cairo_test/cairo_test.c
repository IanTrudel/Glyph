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

GVal gtk_ffi_window_set_titlebar(GVal win, GVal titlebar) {
    gtk_window_set_titlebar(GTK_WINDOW((void *)win), GTK_WIDGET((void *)titlebar));
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

/* ── Entry (single-line text input) ─────────────────────────────────────── */

GVal gtk_ffi_entry_new(GVal _d) { (void)_d;
    return (GVal)gtk_entry_new();
}

GVal gtk_ffi_entry_get_text(GVal entry) {
    GtkEntryBuffer *buf = gtk_entry_get_buffer(GTK_ENTRY((void *)entry));
    return _gstr(gtk_entry_buffer_get_text(buf));
}

GVal gtk_ffi_entry_set_text(GVal entry, GVal text) {
    const char *t = _gs(text);
    GtkEntryBuffer *buf = gtk_entry_get_buffer(GTK_ENTRY((void *)entry));
    gtk_entry_buffer_set_text(buf, t, -1);
    free((void *)t);
    return 0;
}

GVal gtk_ffi_entry_set_placeholder(GVal entry, GVal text) {
    const char *t = _gs(text);
    gtk_entry_set_placeholder_text(GTK_ENTRY((void *)entry), t);
    free((void *)t);
    return 0;
}

GVal gtk_ffi_on_entry_activate(GVal entry, GVal closure) {
    g_signal_connect((void *)entry, "activate",
                     G_CALLBACK(_tramp_void_widget), (gpointer)closure);
    return 0;
}

/* ── TextView (multi-line text) ────────────────────────────────────────── */

GVal gtk_ffi_textview_new(GVal _d) { (void)_d;
    return (GVal)gtk_text_view_new();
}

GVal gtk_ffi_textview_get_text(GVal tv) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW((void *)tv));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    char *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    GVal result = _gstr(text);
    g_free(text);
    return result;
}

GVal gtk_ffi_textview_set_text(GVal tv, GVal text) {
    const char *t = _gs(text);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW((void *)tv));
    gtk_text_buffer_set_text(buf, t, -1);
    free((void *)t);
    return 0;
}

GVal gtk_ffi_textview_set_editable(GVal tv, GVal editable) {
    gtk_text_view_set_editable(GTK_TEXT_VIEW((void *)tv), (gboolean)editable);
    return 0;
}

GVal gtk_ffi_textview_set_wrap(GVal tv, GVal mode) {
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW((void *)tv), (GtkWrapMode)(int)mode);
    return 0;
}

GVal gtk_ffi_wrap_none(GVal _d)      { (void)_d; return (GVal)GTK_WRAP_NONE; }
GVal gtk_ffi_wrap_char(GVal _d)      { (void)_d; return (GVal)GTK_WRAP_CHAR; }
GVal gtk_ffi_wrap_word(GVal _d)      { (void)_d; return (GVal)GTK_WRAP_WORD; }
GVal gtk_ffi_wrap_word_char(GVal _d) { (void)_d; return (GVal)GTK_WRAP_WORD_CHAR; }

/* ── CheckButton ───────────────────────────────────────────────────────── */

GVal gtk_ffi_check_button_new(GVal label) {
    const char *l = _gs(label);
    GtkWidget *cb = gtk_check_button_new_with_label(l);
    free((void *)l);
    return (GVal)cb;
}

GVal gtk_ffi_check_button_get_active(GVal cb) {
    return (GVal)gtk_check_button_get_active(GTK_CHECK_BUTTON((void *)cb));
}

GVal gtk_ffi_check_button_set_active(GVal cb, GVal active) {
    gtk_check_button_set_active(GTK_CHECK_BUTTON((void *)cb), (gboolean)active);
    return 0;
}

GVal gtk_ffi_on_toggled(GVal cb, GVal closure) {
    g_signal_connect((void *)cb, "toggled",
                     G_CALLBACK(_tramp_void_widget), (gpointer)closure);
    return 0;
}

/* ── ToggleButton ──────────────────────────────────────────────────────── */

GVal gtk_ffi_toggle_button_new(GVal label) {
    const char *l = _gs(label);
    GtkWidget *tb = gtk_toggle_button_new_with_label(l);
    free((void *)l);
    return (GVal)tb;
}

GVal gtk_ffi_toggle_button_get_active(GVal tb) {
    return (GVal)gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON((void *)tb));
}

GVal gtk_ffi_toggle_button_set_active(GVal tb, GVal active) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON((void *)tb), (gboolean)active);
    return 0;
}

/* ── SpinButton ────────────────────────────────────────────────────────── */

GVal gtk_ffi_spin_button_new(GVal min_val, GVal max_val, GVal step) {
    double dmin, dmax, dstep;
    memcpy(&dmin, &min_val, sizeof(double));
    memcpy(&dmax, &max_val, sizeof(double));
    memcpy(&dstep, &step, sizeof(double));
    return (GVal)gtk_spin_button_new_with_range(dmin, dmax, dstep);
}

GVal gtk_ffi_spin_button_get_value(GVal sb) {
    double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON((void *)sb));
    GVal r;
    memcpy(&r, &v, sizeof(double));
    return r;
}

GVal gtk_ffi_spin_button_get_value_int(GVal sb) {
    return (GVal)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON((void *)sb));
}

GVal gtk_ffi_spin_button_set_value(GVal sb, GVal val) {
    double v;
    memcpy(&v, &val, sizeof(double));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON((void *)sb), v);
    return 0;
}

GVal gtk_ffi_spin_button_set_range(GVal sb, GVal min_val, GVal max_val) {
    double dmin, dmax;
    memcpy(&dmin, &min_val, sizeof(double));
    memcpy(&dmax, &max_val, sizeof(double));
    gtk_spin_button_set_range(GTK_SPIN_BUTTON((void *)sb), dmin, dmax);
    return 0;
}

GVal gtk_ffi_spin_button_set_digits(GVal sb, GVal digits) {
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON((void *)sb), (guint)digits);
    return 0;
}

/* ── Grid layout ───────────────────────────────────────────────────────── */

GVal gtk_ffi_grid_new(GVal _d) { (void)_d;
    return (GVal)gtk_grid_new();
}

GVal gtk_ffi_grid_attach(GVal grid, GVal child, GVal col, GVal row, GVal width, GVal height) {
    gtk_grid_attach(GTK_GRID((void *)grid), GTK_WIDGET((void *)child),
                    (int)col, (int)row, (int)width, (int)height);
    return 0;
}

GVal gtk_ffi_grid_set_row_spacing(GVal grid, GVal spacing) {
    gtk_grid_set_row_spacing(GTK_GRID((void *)grid), (guint)spacing);
    return 0;
}

GVal gtk_ffi_grid_set_col_spacing(GVal grid, GVal spacing) {
    gtk_grid_set_column_spacing(GTK_GRID((void *)grid), (guint)spacing);
    return 0;
}

/* ── Separator ─────────────────────────────────────────────────────────── */

GVal gtk_ffi_separator_new(GVal orientation) {
    return (GVal)gtk_separator_new((GtkOrientation)(int)orientation);
}

/* ── ScrolledWindow ────────────────────────────────────────────────────── */

GVal gtk_ffi_scrolled_window_new(GVal _d) { (void)_d;
    return (GVal)gtk_scrolled_window_new();
}

GVal gtk_ffi_scrolled_window_set_child(GVal sw, GVal child) {
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW((void *)sw),
                                  GTK_WIDGET((void *)child));
    return 0;
}

GVal gtk_ffi_scrolled_window_set_min_size(GVal sw, GVal w, GVal h) {
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW((void *)sw), (int)w);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW((void *)sw), (int)h);
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

GVal gtk_ffi_widget_queue_draw(GVal w) {
    gtk_widget_queue_draw(GTK_WIDGET((void *)w));
    return 0;
}

/* ── Tier 3: HeaderBar ─────────────────────────────────────────────────── */

GVal gtk_ffi_header_bar_new(GVal _d) { (void)_d;
    return (GVal)gtk_header_bar_new();
}

GVal gtk_ffi_header_bar_pack_start(GVal hb, GVal child) {
    gtk_header_bar_pack_start(GTK_HEADER_BAR((void *)hb),
                              GTK_WIDGET((void *)child));
    return 0;
}

GVal gtk_ffi_header_bar_pack_end(GVal hb, GVal child) {
    gtk_header_bar_pack_end(GTK_HEADER_BAR((void *)hb),
                            GTK_WIDGET((void *)child));
    return 0;
}

GVal gtk_ffi_header_bar_set_title_widget(GVal hb, GVal widget) {
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR((void *)hb),
                                    GTK_WIDGET((void *)widget));
    return 0;
}

GVal gtk_ffi_header_bar_set_show_title(GVal hb, GVal show) {
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR((void *)hb),
                                          (gboolean)show);
    return 0;
}

/* ── Tier 3: Stack + StackSwitcher ─────────────────────────────────────── */

GVal gtk_ffi_stack_new(GVal _d) { (void)_d;
    return (GVal)gtk_stack_new();
}

GVal gtk_ffi_stack_add_named(GVal stack, GVal child, GVal name) {
    const char *n = _gs(name);
    gtk_stack_add_named(GTK_STACK((void *)stack),
                        GTK_WIDGET((void *)child), n);
    free((void *)n);
    return 0;
}

GVal gtk_ffi_stack_add_titled(GVal stack, GVal child, GVal name, GVal title) {
    const char *n = _gs(name);
    const char *t = _gs(title);
    gtk_stack_add_titled(GTK_STACK((void *)stack),
                         GTK_WIDGET((void *)child), n, t);
    free((void *)n);
    free((void *)t);
    return 0;
}

GVal gtk_ffi_stack_set_visible_child_name(GVal stack, GVal name) {
    const char *n = _gs(name);
    gtk_stack_set_visible_child_name(GTK_STACK((void *)stack), n);
    free((void *)n);
    return 0;
}

GVal gtk_ffi_stack_get_visible_child_name(GVal stack) {
    const char *n = gtk_stack_get_visible_child_name(GTK_STACK((void *)stack));
    return _gstr(n);
}

GVal gtk_ffi_stack_set_transition_type(GVal stack, GVal ttype) {
    gtk_stack_set_transition_type(GTK_STACK((void *)stack),
                                  (GtkStackTransitionType)ttype);
    return 0;
}

GVal gtk_ffi_stack_switcher_new(GVal _d) { (void)_d;
    return (GVal)gtk_stack_switcher_new();
}

GVal gtk_ffi_stack_switcher_set_stack(GVal sw, GVal stack) {
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER((void *)sw),
                                GTK_STACK((void *)stack));
    return 0;
}

/* Stack transition type constants */
GVal gtk_ffi_stack_transition_none(GVal _d)       { (void)_d; return (GVal)GTK_STACK_TRANSITION_TYPE_NONE; }
GVal gtk_ffi_stack_transition_crossfade(GVal _d)  { (void)_d; return (GVal)GTK_STACK_TRANSITION_TYPE_CROSSFADE; }
GVal gtk_ffi_stack_transition_slide_right(GVal _d){ (void)_d; return (GVal)GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT; }
GVal gtk_ffi_stack_transition_slide_left(GVal _d) { (void)_d; return (GVal)GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT; }

/* ── Tier 3: Image ─────────────────────────────────────────────────────── */

GVal gtk_ffi_image_new(GVal _d) { (void)_d;
    return (GVal)gtk_image_new();
}

GVal gtk_ffi_image_new_from_file(GVal path) {
    const char *p = _gs(path);
    GtkWidget *img = gtk_image_new_from_file(p);
    free((void *)p);
    return (GVal)img;
}

GVal gtk_ffi_image_new_from_icon(GVal icon_name) {
    const char *n = _gs(icon_name);
    GtkWidget *img = gtk_image_new_from_icon_name(n);
    free((void *)n);
    return (GVal)img;
}

GVal gtk_ffi_image_set_pixel_size(GVal img, GVal size) {
    gtk_image_set_pixel_size(GTK_IMAGE((void *)img), (int)size);
    return 0;
}

/* ── Tier 3: ProgressBar ───────────────────────────────────────────────── */

GVal gtk_ffi_progress_bar_new(GVal _d) { (void)_d;
    return (GVal)gtk_progress_bar_new();
}

GVal gtk_ffi_progress_bar_set_fraction(GVal pb, GVal fraction) {
    double f;
    memcpy(&f, &fraction, sizeof(double));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR((void *)pb), f);
    return 0;
}

GVal gtk_ffi_progress_bar_get_fraction(GVal pb) {
    double f = gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR((void *)pb));
    GVal r;
    memcpy(&r, &f, sizeof(double));
    return r;
}

GVal gtk_ffi_progress_bar_set_text(GVal pb, GVal text) {
    const char *t = _gs(text);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR((void *)pb), t);
    free((void *)t);
    return 0;
}

GVal gtk_ffi_progress_bar_set_show_text(GVal pb, GVal show) {
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR((void *)pb),
                                   (gboolean)show);
    return 0;
}

GVal gtk_ffi_progress_bar_pulse(GVal pb) {
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR((void *)pb));
    return 0;
}

/* ── Tier 3: MenuButton + PopoverMenu ──────────────────────────────────── */

GVal gtk_ffi_menu_button_new(GVal _d) { (void)_d;
    return (GVal)gtk_menu_button_new();
}

GVal gtk_ffi_menu_button_set_icon_name(GVal btn, GVal name) {
    const char *n = _gs(name);
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON((void *)btn), n);
    free((void *)n);
    return 0;
}

GVal gtk_ffi_menu_button_set_label(GVal btn, GVal label) {
    const char *l = _gs(label);
    gtk_menu_button_set_label(GTK_MENU_BUTTON((void *)btn), l);
    free((void *)l);
    return 0;
}

GVal gtk_ffi_menu_button_set_menu_model(GVal btn, GVal model) {
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON((void *)btn),
                                   G_MENU_MODEL((void *)model));
    return 0;
}

/* ── Tier 3: GMenu (declarative menu model) ────────────────────────────── */

GVal gtk_ffi_menu_new(GVal _d) { (void)_d;
    return (GVal)g_menu_new();
}

GVal gtk_ffi_menu_append(GVal menu, GVal label, GVal action) {
    const char *l = _gs(label);
    const char *a = _gs(action);
    g_menu_append(G_MENU((void *)menu), l, a);
    free((void *)l);
    free((void *)a);
    return 0;
}

GVal gtk_ffi_menu_append_section(GVal menu, GVal label, GVal section) {
    const char *l = label ? _gs(label) : NULL;
    g_menu_append_section(G_MENU((void *)menu), l,
                          G_MENU_MODEL((void *)section));
    if (l) free((void *)l);
    return 0;
}

GVal gtk_ffi_menu_append_submenu(GVal menu, GVal label, GVal submenu) {
    const char *l = _gs(label);
    g_menu_append_submenu(G_MENU((void *)menu), l,
                          G_MENU_MODEL((void *)submenu));
    free((void *)l);
    return 0;
}

/* ── Tier 3: GSimpleAction ─────────────────────────────────────────────── */

/* Trampoline for action "activate" signal */
static void _tramp_action_activate(GSimpleAction *action,
                                    GVariant *param, gpointer data) {
    (void)action; (void)param;
    GVal *cl = (GVal *)data;
    ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, (GVal)0);
}

GVal gtk_ffi_action_new(GVal name) {
    const char *n = _gs(name);
    GSimpleAction *a = g_simple_action_new(n, NULL);
    free((void *)n);
    return (GVal)a;
}

GVal gtk_ffi_action_on_activate(GVal action, GVal closure) {
    g_signal_connect((void *)action, "activate",
                     G_CALLBACK(_tramp_action_activate), (gpointer)closure);
    return 0;
}

GVal gtk_ffi_app_add_action(GVal app, GVal action) {
    g_action_map_add_action(G_ACTION_MAP((void *)app),
                            G_ACTION((void *)action));
    return 0;
}

GVal gtk_ffi_window_add_action(GVal win, GVal action) {
    g_action_map_add_action(G_ACTION_MAP((void *)win),
                            G_ACTION((void *)action));
    return 0;
}

/* ── Tier 3: idle_add ──────────────────────────────────────────────────── */

GVal gtk_ffi_idle_add(GVal closure) {
    return (GVal)g_idle_add(_tramp_timeout, (gpointer)closure);
}

/* ── Tier 5: File Dialogs (GTK 4.10+) ─────────────────────────────────── */

/* Glyph string helper (defined later in glyph runtime, forward-declare) */
extern GVal glyph_cstr_to_str(GVal s);

/* Async callback for file_dialog_open */
static void _cb_file_open(GObject *src, GAsyncResult *res, gpointer data) {
    GVal *cl = (GVal *)data;
    GError *err = NULL;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
    if (file) {
        char *path = g_file_get_path(file);
        GVal gpath = glyph_cstr_to_str((GVal)path);
        ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, gpath);
        g_free(path);
        g_object_unref(file);
    } else {
        /* User cancelled or error — call with empty string */
        GVal empty = glyph_cstr_to_str((GVal)"");
        ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, empty);
        if (err) g_error_free(err);
    }
}

/* Async callback for file_dialog_save */
static void _cb_file_save(GObject *src, GAsyncResult *res, gpointer data) {
    GVal *cl = (GVal *)data;
    GError *err = NULL;
    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
    if (file) {
        char *path = g_file_get_path(file);
        GVal gpath = glyph_cstr_to_str((GVal)path);
        ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, gpath);
        g_free(path);
        g_object_unref(file);
    } else {
        GVal empty = glyph_cstr_to_str((GVal)"");
        ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, empty);
        if (err) g_error_free(err);
    }
}

GVal gtk_ffi_file_dialog_open(GVal win, GVal closure) {
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_open(dlg, GTK_WINDOW((void *)win), NULL,
                         _cb_file_open, (gpointer)closure);
    return 0;
}

GVal gtk_ffi_file_dialog_save(GVal win, GVal closure) {
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_save(dlg, GTK_WINDOW((void *)win), NULL,
                         _cb_file_save, (gpointer)closure);
    return 0;
}

/* ── Tier 5: Alert Dialog ──────────────────────────────────────────────── */

/* Async callback for alert_dialog_choose */
static void _cb_alert_choose(GObject *src, GAsyncResult *res, gpointer data) {
    GVal *cl = (GVal *)data;
    GError *err = NULL;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, &err);
    if (err) { g_error_free(err); choice = -1; }
    ((GVal (*)(GVal, GVal))cl[0])((GVal)cl, (GVal)choice);
}

GVal gtk_ffi_alert_dialog_new(GVal message) {
    const char *m = _gs(message);
    GtkAlertDialog *dlg = gtk_alert_dialog_new("%s", m);
    free((void *)m);
    return (GVal)dlg;
}

GVal gtk_ffi_alert_dialog_set_detail(GVal dlg, GVal detail) {
    const char *d = _gs(detail);
    gtk_alert_dialog_set_detail(GTK_ALERT_DIALOG((void *)dlg), d);
    free((void *)d);
    return 0;
}

GVal gtk_ffi_alert_dialog_set_buttons(GVal dlg, GVal btn1, GVal btn2, GVal btn3) {
    const char *b1 = _gs(btn1);
    const char *b2 = btn2 ? _gs(btn2) : NULL;
    const char *b3 = btn3 ? _gs(btn3) : NULL;
    const char *buttons[4] = { b1, b2, b3, NULL };
    /* Count actual buttons */
    int n = 1;
    if (b2) n = 2;
    if (b3) n = 3;
    const char *btns[4];
    btns[0] = b1;
    if (n >= 2) btns[1] = b2;
    if (n >= 3) btns[2] = b3;
    btns[n] = NULL;
    gtk_alert_dialog_set_buttons(GTK_ALERT_DIALOG((void *)dlg), btns);
    free((void *)b1);
    if (b2) free((void *)b2);
    if (b3) free((void *)b3);
    return 0;
}

GVal gtk_ffi_alert_dialog_choose(GVal dlg, GVal win, GVal closure) {
    gtk_alert_dialog_choose(GTK_ALERT_DIALOG((void *)dlg),
                            GTK_WINDOW((void *)win), NULL,
                            _cb_alert_choose, (gpointer)closure);
    return 0;
}

GVal gtk_ffi_alert_dialog_show(GVal dlg, GVal win) {
    gtk_alert_dialog_show(GTK_ALERT_DIALOG((void *)dlg),
                          GTK_WINDOW((void *)win));
    return 0;
}

/* ── Double helpers ───────────────────────────────────────────────────── */

static double _dv(GVal v) { double d; memcpy(&d, &v, sizeof(double)); return d; }
static GVal _gv(double d) { GVal v; memcpy(&v, &d, sizeof(double)); return v; }

/* ── Trampolines: DrawingArea, Click, Motion, Scroll, Drag ────────────── */

/* void draw_func(GtkDrawingArea*, cairo_t*, int width, int height, gpointer)
 * Passes cairo_t* as GVal, width/height as GVal ints */
static void _tramp_draw_func(GtkDrawingArea *area, cairo_t *cr,
                              int width, int height, gpointer data) {
    (void)area;
    GVal *cl = (GVal *)data;
    ((GVal (*)(GVal, GVal, GVal, GVal))cl[0])(
        (GVal)cl, (GVal)cr, (GVal)width, (GVal)height);
}

/* void pressed(GtkGestureClick*, int n_press, double x, double y, gpointer)
 * Doubles bitcast to GVal via memcpy */
static void _tramp_click(GtkGestureClick *gesture, int n_press,
                          double x, double y, gpointer data) {
    (void)gesture;
    GVal *cl = (GVal *)data;
    GVal gx, gy;
    memcpy(&gx, &x, sizeof(double));
    memcpy(&gy, &y, sizeof(double));
    ((GVal (*)(GVal, GVal, GVal, GVal))cl[0])(
        (GVal)cl, (GVal)n_press, gx, gy);
}

/* void motion(GtkEventControllerMotion*, double x, double y, gpointer) */
static void _tramp_motion(GtkEventControllerMotion *ctrl,
                           double x, double y, gpointer data) {
    (void)ctrl;
    GVal *cl = (GVal *)data;
    GVal gx, gy;
    memcpy(&gx, &x, sizeof(double));
    memcpy(&gy, &y, sizeof(double));
    ((GVal (*)(GVal, GVal, GVal))cl[0])((GVal)cl, gx, gy);
}

/* gboolean scroll(GtkEventControllerScroll*, double dx, double dy, gpointer) */
static gboolean _tramp_scroll(GtkEventControllerScroll *ctrl,
                               double dx, double dy, gpointer data) {
    (void)ctrl;
    GVal *cl = (GVal *)data;
    GVal gdx, gdy;
    memcpy(&gdx, &dx, sizeof(double));
    memcpy(&gdy, &dy, sizeof(double));
    GVal r = ((GVal (*)(GVal, GVal, GVal))cl[0])((GVal)cl, gdx, gdy);
    return (gboolean)r;
}

/* void drag(GtkGestureDrag*, double x, double y, gpointer) */
static void _tramp_drag(GtkGestureDrag *gesture,
                         double x, double y, gpointer data) {
    (void)gesture;
    GVal *cl = (GVal *)data;
    GVal gx, gy;
    memcpy(&gx, &x, sizeof(double));
    memcpy(&gy, &y, sizeof(double));
    ((GVal (*)(GVal, GVal, GVal))cl[0])((GVal)cl, gx, gy);
}

/* ── DrawingArea ──────────────────────────────────────────────────────── */

GVal gtk_ffi_drawing_area_new(GVal _d) { (void)_d;
    return (GVal)gtk_drawing_area_new();
}

GVal gtk_ffi_drawing_area_set_draw_func(GVal area, GVal closure) {
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA((void *)area),
                                   _tramp_draw_func, (gpointer)closure, NULL);
    return 0;
}

GVal gtk_ffi_drawing_area_set_content_width(GVal area, GVal w) {
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA((void *)area), (int)w);
    return 0;
}

GVal gtk_ffi_drawing_area_set_content_height(GVal area, GVal h) {
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA((void *)area), (int)h);
    return 0;
}

/* ── GestureClick ─────────────────────────────────────────────────────── */

GVal gtk_ffi_gesture_click_new(GVal _d) { (void)_d;
    return (GVal)gtk_gesture_click_new();
}

GVal gtk_ffi_gesture_click_on_pressed(GVal gesture, GVal closure) {
    g_signal_connect((void *)gesture, "pressed",
                     G_CALLBACK(_tramp_click), (gpointer)closure);
    return 0;
}

GVal gtk_ffi_gesture_click_on_released(GVal gesture, GVal closure) {
    g_signal_connect((void *)gesture, "released",
                     G_CALLBACK(_tramp_click), (gpointer)closure);
    return 0;
}

/* ── EventControllerMotion ────────────────────────────────────────────── */

GVal gtk_ffi_motion_controller_new(GVal _d) { (void)_d;
    return (GVal)gtk_event_controller_motion_new();
}

GVal gtk_ffi_motion_on_motion(GVal ctrl, GVal closure) {
    g_signal_connect((void *)ctrl, "motion",
                     G_CALLBACK(_tramp_motion), (gpointer)closure);
    return 0;
}

GVal gtk_ffi_motion_on_enter(GVal ctrl, GVal closure) {
    g_signal_connect((void *)ctrl, "enter",
                     G_CALLBACK(_tramp_motion), (gpointer)closure);
    return 0;
}

GVal gtk_ffi_motion_on_leave(GVal ctrl, GVal closure) {
    /* leave has no x,y — use a void trampoline */
    g_signal_connect((void *)ctrl, "leave",
                     G_CALLBACK(_tramp_void_widget), (gpointer)closure);
    return 0;
}

/* ── EventControllerScroll ────────────────────────────────────────────── */

GVal gtk_ffi_scroll_controller_new(GVal flags) {
    return (GVal)gtk_event_controller_scroll_new(
        (GtkEventControllerScrollFlags)(int)flags);
}

GVal gtk_ffi_scroll_on_scroll(GVal ctrl, GVal closure) {
    g_signal_connect((void *)ctrl, "scroll",
                     G_CALLBACK(_tramp_scroll), (gpointer)closure);
    return 0;
}

GVal gtk_ffi_scroll_flags_vertical(GVal _d)   { (void)_d; return (GVal)GTK_EVENT_CONTROLLER_SCROLL_VERTICAL; }
GVal gtk_ffi_scroll_flags_horizontal(GVal _d) { (void)_d; return (GVal)GTK_EVENT_CONTROLLER_SCROLL_HORIZONTAL; }
GVal gtk_ffi_scroll_flags_both(GVal _d)       { (void)_d; return (GVal)GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES; }

/* ── GestureDrag ──────────────────────────────────────────────────────── */

GVal gtk_ffi_gesture_drag_new(GVal _d) { (void)_d;
    return (GVal)gtk_gesture_drag_new();
}

GVal gtk_ffi_gesture_drag_on_begin(GVal gesture, GVal closure) {
    g_signal_connect((void *)gesture, "drag-begin",
                     G_CALLBACK(_tramp_drag), (gpointer)closure);
    return 0;
}

GVal gtk_ffi_gesture_drag_on_update(GVal gesture, GVal closure) {
    g_signal_connect((void *)gesture, "drag-update",
                     G_CALLBACK(_tramp_drag), (gpointer)closure);
    return 0;
}

GVal gtk_ffi_gesture_drag_on_end(GVal gesture, GVal closure) {
    g_signal_connect((void *)gesture, "drag-end",
                     G_CALLBACK(_tramp_drag), (gpointer)closure);
    return 0;
}

/* ── Cursor ───────────────────────────────────────────────────────────── */

GVal gtk_ffi_cursor_new(GVal name) {
    const char *n = _gs(name);
    GdkCursor *cur = gdk_cursor_new_from_name(n, NULL);
    free((void *)n);
    return (GVal)cur;
}

GVal gtk_ffi_widget_set_cursor(GVal widget, GVal cursor) {
    gtk_widget_set_cursor(GTK_WIDGET((void *)widget),
                          cursor ? GDK_CURSOR((void *)cursor) : NULL);
    return 0;
}

/* ── Paned ────────────────────────────────────────────────────────────── */

GVal gtk_ffi_paned_new(GVal orientation) {
    return (GVal)gtk_paned_new((GtkOrientation)(int)orientation);
}

GVal gtk_ffi_paned_set_start_child(GVal paned, GVal child) {
    gtk_paned_set_start_child(GTK_PANED((void *)paned),
                              GTK_WIDGET((void *)child));
    return 0;
}

GVal gtk_ffi_paned_set_end_child(GVal paned, GVal child) {
    gtk_paned_set_end_child(GTK_PANED((void *)paned),
                            GTK_WIDGET((void *)child));
    return 0;
}

GVal gtk_ffi_paned_set_position(GVal paned, GVal pos) {
    gtk_paned_set_position(GTK_PANED((void *)paned), (int)pos);
    return 0;
}

GVal gtk_ffi_paned_set_shrink_start(GVal paned, GVal shrink) {
    gtk_paned_set_shrink_start_child(GTK_PANED((void *)paned),
                                     (gboolean)shrink);
    return 0;
}

GVal gtk_ffi_paned_set_shrink_end(GVal paned, GVal shrink) {
    gtk_paned_set_shrink_end_child(GTK_PANED((void *)paned),
                                   (gboolean)shrink);
    return 0;
}

GVal gtk_ffi_paned_set_wide_handle(GVal paned, GVal wide) {
    gtk_paned_set_wide_handle(GTK_PANED((void *)paned), (gboolean)wide);
    return 0;
}

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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <gc/gc.h>
#define malloc(sz) GC_malloc(sz)
#define realloc(p,sz) GC_realloc(p,sz)
#define calloc(n,sz) GC_malloc((n)*(sz))
#define free(p) GC_free(p)
typedef intptr_t GVal;

__thread const char* _glyph_current_fn = "(unknown)";

#ifdef GLYPH_DEBUG
__thread const char* _glyph_call_stack[256];
__thread int _glyph_call_depth = 0;
static void _glyph_print_stack(void) {
  int i;
  fprintf(stderr, "--- stack trace ---\n");
  for (i = _glyph_call_depth - 1; i >= 0; i--)
    fprintf(stderr, "  %s\n", _glyph_call_stack[i]);
}
#endif

static void _glyph_sigsegv(int sig) {
  fprintf(stderr, "\nsegfault in %s\n", _glyph_current_fn);
#ifdef GLYPH_DEBUG
  _glyph_print_stack();
#endif
  signal(sig, SIG_DFL); raise(sig);
}
static void _glyph_sigfpe(int sig) {
  fprintf(stderr, "\ndivision by zero in %s\n", _glyph_current_fn);
#ifdef GLYPH_DEBUG
  _glyph_print_stack();
#endif
  signal(sig, SIG_DFL); raise(sig);
}

#ifdef GLYPH_DEBUG
void _glyph_null_check(GVal ptr, const char* ctx) {
  if (ptr == 0) {
    fprintf(stderr, "\nnull pointer in %s: %s\n", _glyph_current_fn, ctx);
    _glyph_print_stack(); abort();
  }
}
#endif

void _glyph_panic_internal(const char* msg) {
  fprintf(stderr, "panic in %s: %s\n", _glyph_current_fn, msg);
#ifdef GLYPH_DEBUG
  _glyph_print_stack();
#endif
  exit(1);
}
GVal glyph_panic(GVal msg) {
  fprintf(stderr, "panic in %s: ", _glyph_current_fn);
  if (msg) {
    const char* p = *(const char**)msg;
    long long l = *(long long*)((char*)msg+8);
    fwrite(p, 1, (size_t)l, stderr);
  }
  fputc('\n', stderr);
#ifdef GLYPH_DEBUG
  _glyph_print_stack();
#endif
  exit(1); return 0;
}
GVal glyph_alloc(GVal size) { void* p = malloc((size_t)size); if (!p) _glyph_panic_internal("out of memory"); return (GVal)p; }
GVal glyph_realloc(GVal ptr, GVal size) { void* p = realloc((void*)ptr, (size_t)size); if (!p) _glyph_panic_internal("realloc failed"); return (GVal)p; }
void glyph_dealloc(GVal ptr) { free((void*)ptr); }

GVal glyph_str_eq(GVal a, GVal b) {
  if (!a || !b) return (a == b) ? 1 : 0;
  long long la = *(long long*)((char*)a+8), lb = *(long long*)((char*)b+8);
  if (la != lb) return 0;
  return memcmp(*(char**)a, *(char**)b, (size_t)la) == 0 ? 1 : 0;
}
GVal glyph_str_len(GVal s) { if (!(void*)s) return 0; return *(long long*)((char*)s + 8); }
GVal glyph_str_char_at(GVal s, GVal i) {
  const char* p = *(const char**)s;
  long long len = *(long long*)((char*)s + 8);
  if (i < 0 || i >= len) return -1;
  return (GVal)(unsigned char)p[i];
}
GVal glyph_str_slice(GVal s, GVal start, GVal end) {
  const char* p = *(const char**)s;
  long long slen = *(long long*)((char*)s + 8);
  if (start < 0) start = 0;
  if (end > slen) end = slen;
  if (end <= start) {
    char* r = (char*)malloc(16); *(const char**)r = ""; *(long long*)(r+8) = 0; return (GVal)r;
  }
  long long len = end - start;
  char* r = (char*)malloc(16 + len); char* d = r + 16;
  memcpy(d, p + start, (size_t)len);
  *(const char**)r = d; *(long long*)(r+8) = len;
  return (GVal)r;
}

GVal glyph_str_concat(GVal a, GVal b) {
  if (!a) return b; if (!b) return a;
  long long la = *(long long*)((char*)a+8), lb = *(long long*)((char*)b+8);
  long long tl = la+lb;
  char* r = (char*)malloc(16+tl); char* d = r+16;
  memcpy(d, *(char**)a, (size_t)la);
  memcpy(d+la, *(char**)b, (size_t)lb);
  *(const char**)r = d; *(long long*)(r+8) = tl;
  return (GVal)r;
}
GVal glyph_int_to_str(GVal n) {
  char buf[32];
  int len = snprintf(buf, 32, "%lld", (long long)n);
  char* r = (char*)malloc(16+len); char* d = r+16;
  memcpy(d, buf, len);
  *(const char**)r = d; *(long long*)(r+8) = (long long)len;
  return (GVal)r;
}
GVal glyph_cstr_to_str(GVal s) {
  const char* cs = (const char*)(void*)s;
  if (!cs) { char* r=(char*)malloc(16); *(const char**)r=""; *(long long*)(r+8)=0; return (GVal)r; }
  long long l = (long long)strlen(cs);
  char* r = (char*)malloc(16+l); char* d = r+16;
  memcpy(d, cs, (size_t)l);
  *(const char**)r = d; *(long long*)(r+8) = l;
  return (GVal)r;
}

GVal glyph_println(GVal s) {
  if (!(void*)s) { fprintf(stdout, "(null)\n"); fflush(stdout); return 0; }
  const char* p = *(const char**)s;
  long long l = *(long long*)((char*)s+8);
  fwrite(p, 1, (size_t)l, stdout); fputc(10, stdout); fflush(stdout);
  return 0;
}
GVal glyph_eprintln(GVal s) {
  const char* p = *(const char**)s;
  long long l = *(long long*)((char*)s+8);
  fwrite(p, 1, (size_t)l, stderr); fputc(10, stderr); fflush(stderr);
  return 0;
}
void glyph_array_bounds_check(GVal idx, GVal len) {
  if (idx < 0 || idx >= len) {
    fprintf(stderr, "panic in %s: index %lld out of bounds (len %lld)\n", _glyph_current_fn, (long long)idx, (long long)len);
#ifdef GLYPH_DEBUG
    _glyph_print_stack();
#endif
    exit(1);
  }
}

GVal glyph_array_push(GVal hdr, GVal val) {
  long long* h = (long long*)hdr;
  if (h[2] < 0) _glyph_panic_internal("push on frozen array");
  long long* data = (long long*)h[0];
  long long len = h[1], cap = h[2];
  if (len >= cap) {
    cap = cap < 4 ? 4 : cap*2;
    data = (long long*)realloc(data, cap*8);
    if (!data) _glyph_panic_internal("array push oom");
    h[0] = (long long)data; h[2] = cap;
  }
  data[len] = val; h[1] = len+1;
  return hdr;
}
GVal glyph_array_len(GVal hdr) { return ((long long*)hdr)[1]; }
GVal glyph_array_set(GVal hdr, GVal i, GVal v) {
  long long* h = (long long*)hdr;
  if (h[2] < 0) _glyph_panic_internal("set on frozen array");
  glyph_array_bounds_check(i, h[1]);
  ((long long*)h[0])[i] = v;
  return 0;
}
GVal glyph_array_pop(GVal hdr) {
  long long* h = (long long*)hdr;
  if (h[2] < 0) _glyph_panic_internal("pop on frozen array");
  if (h[1] <= 0) _glyph_panic_internal("pop on empty array");
  h[1]--;
  return ((long long*)h[0])[h[1]];
}

GVal glyph_exit(GVal code) { exit((int)code); return 0; }
GVal glyph_str_to_int(GVal s) {
  const char* p = *(const char**)s;
  long long l = *(long long*)((char*)s+8), r = 0, i = 0, sg = 1;
  if (i < l && p[i] == 45) { sg = -1; i++; }
  while (i < l && p[i] >= 48 && p[i] <= 57) { r = r*10 + (p[i]-48); i++; }
  return r*sg;
}
static int g_argc = 0;
static char** g_argv = 0;
void glyph_set_args(int argc, char** argv) { GC_INIT(); g_argc = argc; g_argv = argv; }
GVal glyph_args(void) { long long c=(long long)g_argc; long long* d=(long long*)malloc(c*8); int i; for(i=0;i<g_argc;i++) { long long sl=(long long)strlen(g_argv[i]); char* s=(char*)malloc(16); *(const char**)s=g_argv[i]; *(long long*)(s+8)=sl; d[i]=(long long)s; } long long* h=(long long*)malloc(24); h[0]=(long long)d; h[1]=c; h[2]=c; return (GVal)h; }

GVal glyph_sb_new(void) {
  long long* sb = (long long*)malloc(24);
  char* buf = (char*)malloc(64);
  sb[0] = (long long)buf; sb[1] = 0; sb[2] = 64;
  return (GVal)sb;
}
GVal glyph_sb_append(GVal sp, GVal ss) {
  long long* sb = (long long*)(void*)sp;
  const char* p = *(const char**)ss;
  long long sl = *(long long*)((char*)ss+8);
  long long nl = sb[1]+sl;
  if(nl > sb[2]) {
    long long c = sb[2];
    while(c < nl) c *= 2;
    char* nb = (char*)malloc(c);
    memcpy(nb, (char*)sb[0], (size_t)sb[1]);
    free((char*)sb[0]);
    sb[0] = (long long)nb;
    sb[2] = c;
  }
  memcpy((char*)sb[0]+sb[1], p, (size_t)sl);
  sb[1] = nl;
  return sp;
}
GVal glyph_sb_build(GVal sp) {
  long long* sb = (long long*)(void*)sp;
  long long l = sb[1];
  char* r = (char*)malloc(16+l);
  char* d = r+16;
  memcpy(d, (char*)sb[0], (size_t)l);
  *(const char**)r = d;
  *(long long*)(r+8) = l;
  free((char*)sb[0]);
  free(sb);
  return (GVal)r;
}
GVal glyph_raw_set(GVal ptr, GVal idx, GVal val) { ((long long*)ptr)[idx] = val; return 0; }

GVal glyph_str_to_cstr(GVal s) {
  const char* p = *(const char**)s; long long l = *(long long*)((char*)s+8);
  char* c = (char*)malloc(l+1); memcpy(c, p, (size_t)l); c[l] = 0; return (GVal)(intptr_t)c;
}
GVal glyph_file_exists(GVal ps) {
  char* cp = (char*)(void*)glyph_str_to_cstr(ps); FILE* f = fopen(cp, "rb"); free(cp);
  if (!f) return 0; fclose(f); return 1;
}
GVal glyph_read_file(GVal ps) {
  char* cp = (char*)(void*)glyph_str_to_cstr(ps); FILE* f = fopen(cp, "rb"); free(cp);
  if (!f) { char* r = (char*)malloc(16); *(const char**)r = 0; *(long long*)(r+8) = -1; return (GVal)r; }
  fseek(f, 0, SEEK_END); long long sz = ftell(f); fseek(f, 0, SEEK_SET);
  char* r = (char*)malloc(16+sz); char* d = r+16;
  size_t nr = fread(d, 1, (size_t)sz, f); fclose(f);
  *(const char**)r = d; *(long long*)(r+8) = (long long)nr; return (GVal)r;
}
GVal glyph_write_file(GVal ps, GVal cs) {
  char* cp = (char*)(void*)glyph_str_to_cstr(ps);
  const char* dp = *(const char**)cs; long long dl = *(long long*)((char*)cs+8);
  FILE* f = fopen(cp, "wb"); free(cp);
  if (!f) return -1;
  size_t w = fwrite(dp, 1, (size_t)dl, f); fclose(f);
  return w == (size_t)dl ? 0 : -1;
}
GVal glyph_read_stdin(void) {
  size_t cap = 4096, len = 0;
  char* buf = (char*)malloc(cap);
  size_t nr;
  while ((nr = fread(buf+len, 1, cap-len, stdin)) > 0) {
    len += nr;
    if (len == cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
  }
  char* r = (char*)malloc(16+len); char* d = r+16;
  memcpy(d, buf, len); free(buf);
  *(const char**)r = d; *(long long*)(r+8) = (long long)len;
  return (GVal)r;
}
GVal glyph_system(GVal cs) {
  char* c = (char*)(void*)glyph_str_to_cstr(cs); int rc = system(c); free(c);
  if (rc == -1) return -1;
  if (WIFEXITED(rc)) return (long long)WEXITSTATUS(rc);
  if (WIFSIGNALED(rc)) { fprintf(stderr, "process killed by signal %d\n", WTERMSIG(rc)); return 128 + WTERMSIG(rc); }
  return -1;
}
GVal glyph_print(GVal s) { const char* p=*(const char**)s; long long l=*(long long*)((char*)s+8); fwrite(p,1,(size_t)l,stdout); fflush(stdout); return 0; }
GVal glyph_array_new(GVal cap) { if(cap<=0) cap=4; long long* d=(long long*)malloc(cap*8); if(!d) _glyph_panic_internal("array_new oom"); long long* h=(long long*)malloc(24); h[0]=(long long)d; h[1]=0; h[2]=cap; return (GVal)h; }


static inline double _glyph_i2f(GVal v) { double d; memcpy(&d, &v, 8); return d; }
static inline GVal _glyph_f2i(double d) { GVal v; memcpy(&v, &d, 8); return v; }
GVal glyph_float_to_str(GVal v) {
  char buf[32]; int len = snprintf(buf, 32, "%g", _glyph_i2f(v));
  char* r = (char*)malloc(16+len); char* d2 = r+16;
  memcpy(d2, buf, len);
  *(const char**)r = d2; *(long long*)(r+8) = len; return (GVal)r;
}
GVal glyph_str_to_float(GVal s) {
  const char* p = *(const char**)s; long long l = *(long long*)((char*)s+8);
  char buf[64]; if (l > 63) l = 63; memcpy(buf, p, (size_t)l); buf[l] = 0;
  return _glyph_f2i(atof(buf));
}
GVal glyph_int_to_float(GVal n) { return _glyph_f2i((double)n); }
GVal glyph_float_to_int(GVal v) { return (GVal)(long long)_glyph_i2f(v); }

extern double sin(double); extern double cos(double); extern double sqrt(double);
extern double atan2(double,double); extern double fabs(double); extern double pow(double,double);
extern double floor(double); extern double ceil(double);
GVal glyph_sin(GVal v) { return _glyph_f2i(sin(_glyph_i2f(v))); }
GVal glyph_cos(GVal v) { return _glyph_f2i(cos(_glyph_i2f(v))); }
GVal glyph_sqrt(GVal v) { return _glyph_f2i(sqrt(_glyph_i2f(v))); }
GVal glyph_atan2(GVal y, GVal x) { return _glyph_f2i(atan2(_glyph_i2f(y), _glyph_i2f(x))); }
GVal glyph_fabs(GVal v) { return _glyph_f2i(fabs(_glyph_i2f(v))); }
GVal glyph_pow(GVal b, GVal e) { return _glyph_f2i(pow(_glyph_i2f(b), _glyph_i2f(e))); }
GVal glyph_floor(GVal v) { return _glyph_f2i(floor(_glyph_i2f(v))); }
GVal glyph_ceil(GVal v) { return _glyph_f2i(ceil(_glyph_i2f(v))); }

GVal glyph_ok(GVal val) {
  long long* r = (long long*)glyph_alloc(16); r[0] = 0; r[1] = val; return (long long)r;
}
GVal glyph_err(GVal msg) {
  long long* r = (long long*)glyph_alloc(16); r[0] = 1; r[1] = msg; return (long long)r;
}
GVal glyph_try_read_file(GVal path) {
  GVal result = glyph_read_file(path);
  if (*(long long*)((char*)result+8) < 0) {
    const char* m = "file read failed";
    char* s = (char*)malloc(16); *(const char**)s = m; *(long long*)(s+8) = 16;
    return glyph_err((GVal)s);
  }
  return glyph_ok(result);
}
GVal glyph_try_write_file(GVal path, GVal content) {
  long long result = glyph_write_file(path, content);
  if (result < 0) {
    const char* m = "file write failed";
    char* s = (char*)malloc(16); *(const char**)s = m; *(long long*)(s+8) = 17;
    return glyph_err((GVal)s);
  }
  return glyph_ok(0);
}
GVal glyph_panic_unwrap(GVal variant, GVal fn_name) {
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
GVal glyph_read_line(GVal dummy) { char buf[65536]; if(!fgets(buf,sizeof(buf),stdin)){ void* s=malloc(16); char* e=malloc(1); e[0]=0; *(const char**)s=e; *(long long*)((char*)s+8)=0; return (GVal)s; } long long len=strlen(buf); if(len>0 && buf[len-1]==10) len--; char* data=malloc(len+1); memcpy(data,buf,len); data[len]=0; void* s=malloc(16); *(const char**)s=data; *(long long*)((char*)s+8)=len; return (GVal)s; }
GVal glyph_flush(GVal dummy) { fflush(stdout); return 0; }
GVal glyph_bitset_new(GVal cap) { GVal words = (cap + 63) / 64; if (words < 1) words = 1; unsigned long long* bits = (unsigned long long*)calloc(words, 8); if (!bits) _glyph_panic_internal("out of memory"); return (GVal)bits; }
GVal glyph_bitset_set(GVal bs, GVal idx) { ((unsigned long long*)bs)[idx / 64] |= (1ULL << (idx % 64)); return 0; }
GVal glyph_bitset_test(GVal bs, GVal idx) { return (((unsigned long long*)bs)[idx / 64] >> (idx % 64)) & 1; }


GVal glyph_arr_get_str(GVal arr, GVal idx) {
  GVal* data = (GVal*)((long long*)arr)[0];
  return data[(long long)idx];
}
GVal glyph_array_reverse(GVal hdr) {
  long long* h = (long long*)hdr; long long* d = (long long*)h[0]; long long n = h[1], i;
  if (h[2] < 0) _glyph_panic_internal("reverse on frozen array");
  for (i = 0; i < n/2; i++) { long long tmp = d[i]; d[i] = d[n-1-i]; d[n-1-i] = tmp; }
  return hdr;
}
GVal glyph_array_slice(GVal hdr, GVal start, GVal end) {
  long long* h = (long long*)hdr; long long* sd = (long long*)h[0]; long long n = h[1];
  if (start < 0) start = 0; if (end > n) end = n;
  if (end <= start) {
    long long* nh = (long long*)glyph_alloc(24);
    nh[0] = (long long)glyph_alloc(32); nh[1] = 0; nh[2] = 4 | (1LL << 63);
    return (GVal)nh;
  }
  long long len = end - start;
  long long* nd = (long long*)malloc((size_t)(len*8));
  long long i; for (i = 0; i < len; i++) nd[i] = sd[start+i];
  long long* nh = (long long*)glyph_alloc(24);
  nh[0] = (long long)nd; nh[1] = len; nh[2] = len | (1LL << 63);
  return (GVal)nh;
}
GVal glyph_array_index_of(GVal hdr, GVal val) {
  long long* h = (long long*)hdr; long long* d = (long long*)h[0]; long long n = h[1], i;
  for (i = 0; i < n; i++) if (d[i] == val) return i;
  return -1;
}

/* === Hash Map (open addressing, FNV-1a) === */
static GVal glyph_hm_hash(GVal k) {
  unsigned char* s = (unsigned char*)((GVal*)k)[0];
  GVal len = ((GVal*)k)[1];
  unsigned long long h = 14695981039346656037ULL;
  GVal i; for (i = 0; i < len; i++) { h ^= s[i]; h *= 1099511628211ULL; }
  return (GVal)(h & 0x7FFFFFFFFFFFFFFFLL);
}
static GVal glyph_hm_keq(GVal a, GVal b) { return glyph_str_eq(a, b); }
GVal glyph_hm_new() {
  GVal cap = 8; GVal* h = (GVal*)glyph_alloc(24);
  GVal* d = (GVal*)glyph_alloc(cap*24); memset(d, 0, cap*24);
  h[0] = (GVal)d; h[1] = 0; h[2] = cap; return (GVal)h;
}
GVal glyph_hm_len(GVal m) { return ((GVal*)m)[1]; }
GVal glyph_hm_has(GVal m, GVal k) {
  GVal* h = (GVal*)m; GVal cap = h[2] & 0x7FFFFFFFFFFFFFFFLL; GVal* d = (GVal*)h[0];
  GVal idx = glyph_hm_hash(k) % cap;
  GVal i; for (i = 0; i < cap; i++) {
    GVal s = ((idx+i) % cap) * 3;
    if (d[s+2] == 0) return 0;
    if (d[s+2] == 1 && glyph_hm_keq(d[s], k)) return 1;
  }
  return 0;
}
GVal glyph_hm_get(GVal m, GVal k) {
  GVal* h = (GVal*)m; GVal cap = h[2] & 0x7FFFFFFFFFFFFFFFLL; GVal* d = (GVal*)h[0];
  GVal idx = glyph_hm_hash(k) % cap;
  GVal i; for (i = 0; i < cap; i++) {
    GVal s = ((idx+i) % cap) * 3;
    if (d[s+2] == 0) return 0;
    if (d[s+2] == 1 && glyph_hm_keq(d[s], k)) return d[s+1];
  }
  return 0;
}
static void glyph_hm_resize(GVal* h) {
  GVal oc = h[2] & 0x7FFFFFFFFFFFFFFFLL; GVal* od = (GVal*)h[0];
  GVal nc = oc * 2; GVal* nd = (GVal*)glyph_alloc(nc*24); memset(nd, 0, nc*24);
  GVal i; for (i = 0; i < oc; i++) {
    GVal os = i * 3;
    if (od[os+2] == 1) {
      GVal idx = glyph_hm_hash(od[os]) % nc;
      GVal j; for (j = 0; j < nc; j++) {
        GVal ns = ((idx+j) % nc) * 3;
        if (nd[ns+2] == 0) { nd[ns] = od[os]; nd[ns+1] = od[os+1]; nd[ns+2] = 1; break; }
      }
    }
  }
  h[0] = (GVal)nd; h[2] = nc;
}
GVal glyph_hm_set(GVal m, GVal k, GVal v) {
  GVal* h = (GVal*)m;
  if (h[2] < 0) _glyph_panic_internal("set on frozen map");
  GVal cap = h[2]; GVal* d = (GVal*)h[0];
  GVal idx = glyph_hm_hash(k) % cap; GVal tomb = -1;
  GVal i; for (i = 0; i < cap; i++) {
    GVal s = ((idx+i) % cap) * 3;
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
GVal glyph_hm_del(GVal m, GVal k) {
  GVal* h = (GVal*)m;
  if (h[2] < 0) _glyph_panic_internal("del on frozen map");
  GVal cap = h[2]; GVal* d = (GVal*)h[0];
  GVal idx = glyph_hm_hash(k) % cap;
  GVal i; for (i = 0; i < cap; i++) {
    GVal s = ((idx+i) % cap) * 3;
    if (d[s+2] == 0) return m;
    if (d[s+2] == 1 && glyph_hm_keq(d[s], k)) { d[s+2] = 2; h[1]--; return m; }
  }
  return m;
}
GVal glyph_hm_keys(GVal m) {
  GVal* h = (GVal*)m; GVal n = h[1]; GVal cap = h[2] & 0x7FFFFFFFFFFFFFFFLL; GVal* d = (GVal*)h[0];
  GVal* kd = (GVal*)glyph_alloc((n+1)*8);
  GVal* kh = (GVal*)glyph_alloc(24);
  GVal i, j = 0;
  for (i = 0; i < cap; i++) { GVal s = i*3; if (d[s+2] == 1) kd[j++] = d[s]; }
  kh[0] = (GVal)kd; kh[1] = j; kh[2] = j | (1LL << 63); return (GVal)kh;
}
GVal glyph_hm_get_float(GVal m, GVal k) { return glyph_hm_get(m, k); }
GVal glyph_hm_set_float(GVal m, GVal k, GVal v) { return glyph_hm_set(m, k, v); }

GVal glyph_str_index_of(GVal h, GVal n) {
  const char* hp = *(const char**)h; long long hl = *(long long*)((char*)h+8);
  const char* np = *(const char**)n; long long nl = *(long long*)((char*)n+8);
  if (nl == 0) return 0; if (nl > hl) return -1;
  long long i; for (i = 0; i <= hl-nl; i++) { if (memcmp(hp+i, np, (size_t)nl) == 0) return i; }
  return -1;
}
GVal glyph_str_starts_with(GVal s, GVal p) {
  long long sl = *(long long*)((char*)s+8), pl = *(long long*)((char*)p+8);
  if (pl > sl) return 0;
  return memcmp(*(const char**)s, *(const char**)p, (size_t)pl) == 0 ? 1 : 0;
}
GVal glyph_str_ends_with(GVal s, GVal x) {
  const char* sp = *(const char**)s;
  long long sl = *(long long*)((char*)s+8), xl = *(long long*)((char*)x+8);
  if (xl > sl) return 0;
  return memcmp(sp+(sl-xl), *(const char**)x, (size_t)xl) == 0 ? 1 : 0;
}
GVal glyph_str_trim(GVal s) {
  const char* p = *(const char**)s; long long l = *(long long*)((char*)s+8);
  long long i = 0, j = l;
  while (i < l && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) i++;
  while (j > i && (p[j-1] == ' ' || p[j-1] == '\t' || p[j-1] == '\n' || p[j-1] == '\r')) j--;
  return glyph_str_slice(s, i, j);
}
GVal glyph_str_to_upper(GVal s) {
  const char* p = *(const char**)s; long long l = *(long long*)((char*)s+8);
  char* buf = (char*)malloc((size_t)(l+1));
  long long i; for (i = 0; i < l; i++) buf[i] = (p[i] >= 'a' && p[i] <= 'z') ? (p[i]-32) : p[i];
  GVal r = glyph_alloc(16); *(char**)r = buf; *(long long*)((char*)r+8) = l; return r;
}
GVal glyph_str_split(GVal s, GVal sep) {
  const char* sp = *(const char**)s; long long sl = *(long long*)((char*)s+8);
  const char* dp = *(const char**)sep; long long dl = *(long long*)((char*)sep+8);
  GVal arr = glyph_array_new(4);
  if (dl == 0) {
    long long i; for (i = 0; i < sl; i++) {
      char* buf = (char*)malloc(2); buf[0] = sp[i]; buf[1] = 0;
      GVal r = glyph_alloc(16); *(char**)r = buf; *(long long*)((char*)r+8) = 1;
      glyph_array_push(arr, r);
    }
    return arr;
  }
  long long start = 0, i;
  for (i = 0; i <= sl-dl; i++) {
    if (memcmp(sp+i, dp, (size_t)dl) == 0) {
      glyph_array_push(arr, glyph_str_slice(s, start, i)); start = i+dl; i = start-1;
    }
  }
  glyph_array_push(arr, glyph_str_slice(s, start, sl)); return arr;
}
GVal glyph_str_from_code(GVal code) {
  char* buf = (char*)malloc(2); buf[0] = (char)(unsigned char)code; buf[1] = 0;
  GVal r = glyph_alloc(16); *(char**)r = buf; *(long long*)((char*)r+8) = 1; return r;
}

/* === Array/Map Freeze (immutability bit in cap sign bit) === */
GVal glyph_array_freeze(GVal hdr) {
    long long* h = (long long*)hdr;
    if (h[2] >= 0) h[2] = h[2] | (1LL << 63);
    return hdr;
}
GVal glyph_array_frozen(GVal hdr) {
    return ((long long*)hdr)[2] < 0 ? 1 : 0;
}
GVal glyph_array_thaw(GVal hdr) {
    long long* h = (long long*)hdr;
    long long len = h[1];
    long long cap = h[2] & 0x7FFFFFFFFFFFFFFFLL;
    if (cap < len) cap = len;
    long long* nh = (long long*)malloc(24);
    long long* nd = (long long*)malloc(cap * 8);
    memcpy(nd, (void*)h[0], len * 8);
    nh[0] = (long long)nd; nh[1] = len; nh[2] = cap;
    return (GVal)nh;
}
GVal glyph_hm_freeze(GVal hdr) {
    long long* h = (long long*)hdr;
    if (h[2] >= 0) h[2] = h[2] | (1LL << 63);
    return hdr;
}
GVal glyph_hm_frozen(GVal hdr) {
    return ((long long*)hdr)[2] < 0 ? 1 : 0;
}
GVal glyph_generate(GVal n, GVal fn) {
    long long len = (long long)n;
    long long* hdr = (long long*)malloc(24);
    long long* data = (long long*)malloc(len * 8);
    long long i;
    for (i = 0; i < len; i++) {
        GVal (*fp)(GVal, GVal) = (GVal(*)(GVal,GVal))((GVal*)fn)[0];
        data[i] = fp((GVal)fn, i);
    }
    hdr[0] = (long long)data;
    hdr[1] = len;
    hdr[2] = len | (1LL << 63);
    return (GVal)hdr;
}

/* === Ref type (mutable cell, 8 bytes) === */
GVal glyph_ref(GVal val) {
    GVal* r = (GVal*)malloc(8);
    r[0] = val;
    return (GVal)r;
}
GVal glyph_deref(GVal r) {
    return ((GVal*)r)[0];
}
GVal glyph_set_ref(GVal r, GVal val) {
    ((GVal*)r)[0] = val;
    return 0;
}

GVal glyph_gtk_app_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_app_new))(_0); }
GVal glyph_gtk_app_run(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_app_run))(_0); }
GVal glyph_gtk_app_quit(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_app_quit))(_0); }
GVal glyph_gtk_on_activate(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_on_activate))(_0, _1); }
GVal glyph_gtk_window_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_window_new))(_0); }
GVal glyph_gtk_window_set_title(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_window_set_title))(_0, _1); }
GVal glyph_gtk_window_set_size(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gtk_ffi_window_set_size))(_0, _1, _2); }
GVal glyph_gtk_window_set_child(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_window_set_child))(_0, _1); }
GVal glyph_gtk_window_present(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_window_present))(_0); }
GVal glyph_gtk_window_destroy(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_window_destroy))(_0); }
GVal glyph_gtk_on_close_request(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_on_close_request))(_0, _1); }
GVal glyph_gtk_button_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_button_new))(_0); }
GVal glyph_gtk_button_set_label(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_button_set_label))(_0, _1); }
GVal glyph_gtk_on_clicked(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_on_clicked))(_0, _1); }
GVal glyph_gtk_label_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_label_new))(_0); }
GVal glyph_gtk_label_set_text(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_label_set_text))(_0, _1); }
GVal glyph_gtk_label_set_markup(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_label_set_markup))(_0, _1); }
GVal glyph_gtk_box_new(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_box_new))(_0, _1); }
GVal glyph_gtk_box_append(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_box_append))(_0, _1); }
GVal glyph_gtk_widget_set_margin(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal))(gtk_ffi_widget_set_margin))(_0, _1, _2, _3, _4); }
GVal glyph_gtk_widget_set_hexpand(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_widget_set_hexpand))(_0, _1); }
GVal glyph_gtk_widget_set_vexpand(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_widget_set_vexpand))(_0, _1); }
GVal glyph_gtk_widget_set_halign(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_widget_set_halign))(_0, _1); }
GVal glyph_gtk_widget_set_valign(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_widget_set_valign))(_0, _1); }
GVal glyph_gtk_widget_set_sensitive(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_widget_set_sensitive))(_0, _1); }
GVal glyph_gtk_widget_set_visible(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_widget_set_visible))(_0, _1); }
GVal glyph_gtk_orientation_horizontal(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_orientation_horizontal))(_0); }
GVal glyph_gtk_orientation_vertical(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_orientation_vertical))(_0); }
GVal glyph_gtk_align_fill(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_align_fill))(_0); }
GVal glyph_gtk_align_start(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_align_start))(_0); }
GVal glyph_gtk_align_end(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_align_end))(_0); }
GVal glyph_gtk_align_center(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_align_center))(_0); }
GVal glyph_gtk_timeout_add(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_timeout_add))(_0, _1); }
GVal glyph_gtk_key_controller_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_key_controller_new))(_0); }
GVal glyph_gtk_widget_add_controller(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_widget_add_controller))(_0, _1); }
GVal glyph_gtk_on_key_pressed(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_on_key_pressed))(_0, _1); }
GVal glyph_gtk_widget_add_css_class(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_widget_add_css_class))(_0, _1); }
GVal glyph_gtk_css_load(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_css_load))(_0); }
GVal glyph_gtk_on_activate_fn(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_on_activate_fn))(_0, _1); }
GVal glyph_gtk_entry_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_entry_new))(_0); }
GVal glyph_gtk_entry_get_text(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_entry_get_text))(_0); }
GVal glyph_gtk_entry_set_text(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_entry_set_text))(_0, _1); }
GVal glyph_gtk_entry_set_placeholder(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_entry_set_placeholder))(_0, _1); }
GVal glyph_gtk_on_entry_activate(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_on_entry_activate))(_0, _1); }
GVal glyph_gtk_textview_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_textview_new))(_0); }
GVal glyph_gtk_textview_get_text(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_textview_get_text))(_0); }
GVal glyph_gtk_textview_set_text(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_textview_set_text))(_0, _1); }
GVal glyph_gtk_textview_set_editable(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_textview_set_editable))(_0, _1); }
GVal glyph_gtk_textview_set_wrap(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_textview_set_wrap))(_0, _1); }
GVal glyph_gtk_wrap_none(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_wrap_none))(_0); }
GVal glyph_gtk_wrap_char(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_wrap_char))(_0); }
GVal glyph_gtk_wrap_word(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_wrap_word))(_0); }
GVal glyph_gtk_wrap_word_char(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_wrap_word_char))(_0); }
GVal glyph_gtk_check_button_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_check_button_new))(_0); }
GVal glyph_gtk_check_button_get_active(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_check_button_get_active))(_0); }
GVal glyph_gtk_check_button_set_active(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_check_button_set_active))(_0, _1); }
GVal glyph_gtk_on_toggled(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_on_toggled))(_0, _1); }
GVal glyph_gtk_toggle_button_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_toggle_button_new))(_0); }
GVal glyph_gtk_toggle_button_get_active(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_toggle_button_get_active))(_0); }
GVal glyph_gtk_toggle_button_set_active(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_toggle_button_set_active))(_0, _1); }
GVal glyph_gtk_spin_button_new(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gtk_ffi_spin_button_new))(_0, _1, _2); }
GVal glyph_gtk_spin_button_get_value(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_spin_button_get_value))(_0); }
GVal glyph_gtk_spin_button_get_value_int(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_spin_button_get_value_int))(_0); }
GVal glyph_gtk_spin_button_set_value(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_spin_button_set_value))(_0, _1); }
GVal glyph_gtk_spin_button_set_range(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gtk_ffi_spin_button_set_range))(_0, _1, _2); }
GVal glyph_gtk_spin_button_set_digits(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_spin_button_set_digits))(_0, _1); }
GVal glyph_gtk_grid_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_grid_new))(_0); }
GVal glyph_gtk_grid_attach(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal, GVal))(gtk_ffi_grid_attach))(_0, _1, _2, _3, _4, _5); }
GVal glyph_gtk_grid_set_row_spacing(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_grid_set_row_spacing))(_0, _1); }
GVal glyph_gtk_grid_set_col_spacing(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_grid_set_col_spacing))(_0, _1); }
GVal glyph_gtk_separator_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_separator_new))(_0); }
GVal glyph_gtk_scrolled_window_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_scrolled_window_new))(_0); }
GVal glyph_gtk_scrolled_window_set_child(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_scrolled_window_set_child))(_0, _1); }
GVal glyph_gtk_scrolled_window_set_min_size(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gtk_ffi_scrolled_window_set_min_size))(_0, _1, _2); }
GVal glyph_gtk_header_bar_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_header_bar_new))(_0); }
GVal glyph_gtk_header_bar_pack_start(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_header_bar_pack_start))(_0, _1); }
GVal glyph_gtk_header_bar_pack_end(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_header_bar_pack_end))(_0, _1); }
GVal glyph_gtk_header_bar_set_title_widget(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_header_bar_set_title_widget))(_0, _1); }
GVal glyph_gtk_header_bar_set_show_title(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_header_bar_set_show_title))(_0, _1); }
GVal glyph_gtk_stack_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_stack_new))(_0); }
GVal glyph_gtk_stack_add_named(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gtk_ffi_stack_add_named))(_0, _1, _2); }
GVal glyph_gtk_stack_add_titled(GVal _0, GVal _1, GVal _2, GVal _3) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal))(gtk_ffi_stack_add_titled))(_0, _1, _2, _3); }
GVal glyph_gtk_stack_set_visible_child_name(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_stack_set_visible_child_name))(_0, _1); }
GVal glyph_gtk_stack_get_visible_child_name(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_stack_get_visible_child_name))(_0); }
GVal glyph_gtk_stack_set_transition_type(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_stack_set_transition_type))(_0, _1); }
GVal glyph_gtk_stack_switcher_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_stack_switcher_new))(_0); }
GVal glyph_gtk_stack_switcher_set_stack(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_stack_switcher_set_stack))(_0, _1); }
GVal glyph_gtk_stack_transition_none(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_stack_transition_none))(_0); }
GVal glyph_gtk_stack_transition_crossfade(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_stack_transition_crossfade))(_0); }
GVal glyph_gtk_stack_transition_slide_right(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_stack_transition_slide_right))(_0); }
GVal glyph_gtk_stack_transition_slide_left(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_stack_transition_slide_left))(_0); }
GVal glyph_gtk_image_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_image_new))(_0); }
GVal glyph_gtk_image_new_from_file(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_image_new_from_file))(_0); }
GVal glyph_gtk_image_new_from_icon(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_image_new_from_icon))(_0); }
GVal glyph_gtk_image_set_pixel_size(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_image_set_pixel_size))(_0, _1); }
GVal glyph_gtk_progress_bar_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_progress_bar_new))(_0); }
GVal glyph_gtk_progress_bar_set_fraction(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_progress_bar_set_fraction))(_0, _1); }
GVal glyph_gtk_progress_bar_get_fraction(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_progress_bar_get_fraction))(_0); }
GVal glyph_gtk_progress_bar_set_text(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_progress_bar_set_text))(_0, _1); }
GVal glyph_gtk_progress_bar_set_show_text(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_progress_bar_set_show_text))(_0, _1); }
GVal glyph_gtk_progress_bar_pulse(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_progress_bar_pulse))(_0); }
GVal glyph_gtk_menu_button_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_menu_button_new))(_0); }
GVal glyph_gtk_menu_button_set_icon_name(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_menu_button_set_icon_name))(_0, _1); }
GVal glyph_gtk_menu_button_set_label(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_menu_button_set_label))(_0, _1); }
GVal glyph_gtk_menu_button_set_menu_model(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_menu_button_set_menu_model))(_0, _1); }
GVal glyph_gtk_menu_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_menu_new))(_0); }
GVal glyph_gtk_menu_append(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gtk_ffi_menu_append))(_0, _1, _2); }
GVal glyph_gtk_menu_append_section(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gtk_ffi_menu_append_section))(_0, _1, _2); }
GVal glyph_gtk_menu_append_submenu(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gtk_ffi_menu_append_submenu))(_0, _1, _2); }
GVal glyph_gtk_action_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_action_new))(_0); }
GVal glyph_gtk_action_on_activate(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_action_on_activate))(_0, _1); }
GVal glyph_gtk_app_add_action(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_app_add_action))(_0, _1); }
GVal glyph_gtk_window_add_action(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_window_add_action))(_0, _1); }
GVal glyph_gtk_idle_add(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_idle_add))(_0); }
GVal glyph_gtk_widget_queue_draw(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_widget_queue_draw))(_0); }
GVal glyph_gtk_file_dialog_open(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_file_dialog_open))(_0, _1); }
GVal glyph_gtk_file_dialog_save(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_file_dialog_save))(_0, _1); }
GVal glyph_gtk_alert_dialog_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_alert_dialog_new))(_0); }
GVal glyph_gtk_alert_dialog_set_detail(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_alert_dialog_set_detail))(_0, _1); }
GVal glyph_gtk_alert_dialog_set_buttons(GVal _0, GVal _1, GVal _2, GVal _3) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal))(gtk_ffi_alert_dialog_set_buttons))(_0, _1, _2, _3); }
GVal glyph_gtk_alert_dialog_choose(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gtk_ffi_alert_dialog_choose))(_0, _1, _2); }
GVal glyph_gtk_alert_dialog_show(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_alert_dialog_show))(_0, _1); }
GVal glyph_gtk_window_set_titlebar(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_window_set_titlebar))(_0, _1); }
GVal glyph_gtk_drawing_area_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_drawing_area_new))(_0); }
GVal glyph_gtk_drawing_area_set_draw_func(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_drawing_area_set_draw_func))(_0, _1); }
GVal glyph_gtk_drawing_area_set_content_width(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_drawing_area_set_content_width))(_0, _1); }
GVal glyph_gtk_drawing_area_set_content_height(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_drawing_area_set_content_height))(_0, _1); }
GVal glyph_gtk_gesture_click_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_gesture_click_new))(_0); }
GVal glyph_gtk_gesture_click_on_pressed(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_gesture_click_on_pressed))(_0, _1); }
GVal glyph_gtk_gesture_click_on_released(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_gesture_click_on_released))(_0, _1); }
GVal glyph_gtk_motion_controller_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_motion_controller_new))(_0); }
GVal glyph_gtk_motion_on_motion(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_motion_on_motion))(_0, _1); }
GVal glyph_gtk_motion_on_enter(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_motion_on_enter))(_0, _1); }
GVal glyph_gtk_motion_on_leave(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_motion_on_leave))(_0, _1); }
GVal glyph_gtk_scroll_controller_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_scroll_controller_new))(_0); }
GVal glyph_gtk_scroll_on_scroll(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_scroll_on_scroll))(_0, _1); }
GVal glyph_gtk_scroll_flags_vertical(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_scroll_flags_vertical))(_0); }
GVal glyph_gtk_scroll_flags_horizontal(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_scroll_flags_horizontal))(_0); }
GVal glyph_gtk_scroll_flags_both(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_scroll_flags_both))(_0); }
GVal glyph_gtk_gesture_drag_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_gesture_drag_new))(_0); }
GVal glyph_gtk_gesture_drag_on_begin(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_gesture_drag_on_begin))(_0, _1); }
GVal glyph_gtk_gesture_drag_on_update(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_gesture_drag_on_update))(_0, _1); }
GVal glyph_gtk_gesture_drag_on_end(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_gesture_drag_on_end))(_0, _1); }
GVal glyph_gtk_cursor_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_cursor_new))(_0); }
GVal glyph_gtk_widget_set_cursor(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_widget_set_cursor))(_0, _1); }
GVal glyph_gtk_paned_new(GVal _0) { return (GVal)((GVal(*)(GVal))(gtk_ffi_paned_new))(_0); }
GVal glyph_gtk_paned_set_start_child(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_paned_set_start_child))(_0, _1); }
GVal glyph_gtk_paned_set_end_child(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_paned_set_end_child))(_0, _1); }
GVal glyph_gtk_paned_set_position(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_paned_set_position))(_0, _1); }
GVal glyph_gtk_paned_set_shrink_start(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_paned_set_shrink_start))(_0, _1); }
GVal glyph_gtk_paned_set_shrink_end(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_paned_set_shrink_end))(_0, _1); }
GVal glyph_gtk_paned_set_wide_handle(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gtk_ffi_paned_set_wide_handle))(_0, _1); }
GVal glyph_cr_move_to(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(cairo_ffi_move_to))(_0, _1, _2); }
GVal glyph_cr_line_to(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(cairo_ffi_line_to))(_0, _1, _2); }
GVal glyph_cr_curve_to(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal, GVal, GVal))(cairo_ffi_curve_to))(_0, _1, _2, _3, _4, _5, _6); }
GVal glyph_cr_arc(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal, GVal))(cairo_ffi_arc))(_0, _1, _2, _3, _4, _5); }
GVal glyph_cr_rectangle(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal))(cairo_ffi_rectangle))(_0, _1, _2, _3, _4); }
GVal glyph_cr_close_path(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_close_path))(_0); }
GVal glyph_cr_new_path(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_new_path))(_0); }
GVal glyph_cr_new_sub_path(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_new_sub_path))(_0); }
GVal glyph_cr_stroke(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_stroke))(_0); }
GVal glyph_cr_fill(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_fill))(_0); }
GVal glyph_cr_stroke_preserve(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_stroke_preserve))(_0); }
GVal glyph_cr_fill_preserve(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_fill_preserve))(_0); }
GVal glyph_cr_paint(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_paint))(_0); }
GVal glyph_cr_set_source_rgba(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal))(cairo_ffi_set_source_rgba))(_0, _1, _2, _3, _4); }
GVal glyph_cr_set_source_rgb(GVal _0, GVal _1, GVal _2, GVal _3) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal))(cairo_ffi_set_source_rgb))(_0, _1, _2, _3); }
GVal glyph_cr_set_line_width(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(cairo_ffi_set_line_width))(_0, _1); }
GVal glyph_cr_set_line_cap(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(cairo_ffi_set_line_cap))(_0, _1); }
GVal glyph_cr_set_line_join(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(cairo_ffi_set_line_join))(_0, _1); }
GVal glyph_cr_set_dash(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(cairo_ffi_set_dash))(_0, _1, _2); }
GVal glyph_cr_set_dash_none(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_set_dash_none))(_0); }
GVal glyph_cr_save(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_save))(_0); }
GVal glyph_cr_restore(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_restore))(_0); }
GVal glyph_cr_translate(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(cairo_ffi_translate))(_0, _1, _2); }
GVal glyph_cr_scale(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(cairo_ffi_scale))(_0, _1, _2); }
GVal glyph_cr_rotate(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(cairo_ffi_rotate))(_0, _1); }
GVal glyph_cr_identity_matrix(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_identity_matrix))(_0); }
GVal glyph_cr_line_cap_butt(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_line_cap_butt))(_0); }
GVal glyph_cr_line_cap_round(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_line_cap_round))(_0); }
GVal glyph_cr_line_cap_square(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_line_cap_square))(_0); }
GVal glyph_cr_line_join_miter(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_line_join_miter))(_0); }
GVal glyph_cr_line_join_round(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_line_join_round))(_0); }
GVal glyph_cr_line_join_bevel(GVal _0) { return (GVal)((GVal(*)(GVal))(cairo_ffi_line_join_bevel))(_0); }


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
typedef intptr_t GVal;

extern __thread const char* _glyph_current_fn;
extern void _glyph_sigsegv(int sig);
extern void _glyph_sigfpe(int sig);
#ifdef GLYPH_DEBUG
extern __thread const char* _glyph_call_stack[256];
extern __thread int _glyph_call_depth;
extern void _glyph_null_check(GVal ptr, const char* ctx);
#endif
extern GVal glyph_alloc(GVal size);
extern GVal glyph_cstr_to_str(GVal s);
extern GVal glyph_str_concat(GVal a, GVal b);
extern GVal glyph_str_eq(GVal a, GVal b);
extern GVal glyph_int_to_str(GVal n);
extern GVal glyph_println(GVal s);
extern GVal glyph_print(GVal s);
extern void glyph_array_bounds_check(GVal idx, GVal len);
extern GVal glyph_array_push(GVal hdr, GVal val);
extern GVal glyph_array_len(GVal hdr);
extern GVal glyph_array_set(GVal hdr, GVal idx, GVal val);
extern GVal glyph_array_pop(GVal hdr);
extern GVal glyph_array_new(GVal cap);
extern GVal glyph_str_len(GVal s);
extern GVal glyph_str_char_at(GVal s, GVal idx);
extern GVal glyph_str_to_cstr(GVal s);
extern GVal glyph_file_exists(GVal ps);
extern GVal glyph_read_file(GVal ps);
extern GVal glyph_write_file(GVal ps, GVal cs);
extern GVal glyph_system(GVal cs);
extern GVal glyph_db_open(GVal ps);
extern GVal glyph_db_close(GVal h);
extern GVal glyph_db_exec(GVal h, GVal ss);
extern GVal glyph_db_query_rows(GVal h, GVal ss);
extern GVal glyph_db_query_one(GVal h, GVal ss);
extern GVal glyph_raw_set(GVal ptr, GVal idx, GVal val);
extern GVal glyph_str_slice(GVal s, GVal start, GVal end);
extern GVal glyph_exit(GVal code);
extern GVal glyph_sb_new(void);
extern GVal glyph_sb_append(GVal sb, GVal str);
extern GVal glyph_sb_build(GVal sb);
extern GVal glyph_str_index_of(GVal h, GVal n);
extern GVal glyph_str_starts_with(GVal s, GVal p);
extern GVal glyph_str_ends_with(GVal s, GVal x);
extern GVal glyph_str_trim(GVal s);
extern GVal glyph_str_to_upper(GVal s);
extern GVal glyph_str_split(GVal s, GVal sep);
extern GVal glyph_str_from_code(GVal code);
extern GVal glyph_array_reverse(GVal hdr);
extern GVal glyph_array_slice(GVal hdr, GVal start, GVal end);
extern GVal glyph_array_index_of(GVal hdr, GVal val);
extern GVal glyph_array_freeze(GVal hdr);
extern GVal glyph_array_frozen(GVal hdr);
extern GVal glyph_array_thaw(GVal hdr);
extern GVal glyph_hm_freeze(GVal hdr);
extern GVal glyph_hm_frozen(GVal hdr);
extern GVal glyph_ref(GVal val);
extern GVal glyph_deref(GVal r);
extern GVal glyph_set_ref(GVal r, GVal val);
extern GVal glyph_generate(GVal n, GVal fn);

GVal cr_tau();
GVal cr_set_color(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4);
GVal cr_pi();
GVal cr_draw_rect(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4);
GVal cr_draw_line(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4);
GVal cr_draw_circle(GVal _0, GVal _1, GVal _2, GVal _3);
GVal gtk_vsep();
GVal gtk_vbox(GVal _0);
GVal gtk_stack_page(GVal _0, GVal _1, GVal _2, GVal _3);
GVal gtk_scrolled(GVal _0, GVal _1, GVal _2);
GVal gtk_paned_v(GVal _0, GVal _1, GVal _2);
GVal gtk_paned_h(GVal _0, GVal _1, GVal _2);
GVal gtk_padded(GVal _0, GVal _1);
GVal gtk_make_window(GVal _0, GVal _1, GVal _2, GVal _3);
GVal gtk_icon_button(GVal _0);
GVal gtk_hsep();
GVal gtk_header_bar_title(GVal _0);
GVal gtk_hbox(GVal _0);
GVal gtk_grid_spaced(GVal _0, GVal _1);
GVal gtk_drawing_area(GVal _0, GVal _1, GVal _2);
GVal gtk_action(GVal _0, GVal _1, GVal _2);
GVal on_activate_lam_0(GVal _0, GVal _1, GVal _2, GVal _3);
GVal on_activate_lam_1(GVal _0, GVal _1, GVal _2, GVal _3);
GVal on_activate_lam_2(GVal _0, GVal _1, GVal _2);
GVal on_activate(GVal _0);
GVal main_lam_0(GVal _0, GVal _1);
GVal glyph_main();
GVal draw(GVal _0, GVal _1, GVal _2);
GVal cr_draw_rect__any_float_float_float_float(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4);
GVal cr_draw_circle__any_float_float_float(GVal _0, GVal _1, GVal _2, GVal _3);
GVal cr_draw_line__any_float_float_float_float(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4);
GVal on_activate__any_any_lam_0(GVal _0, GVal _1, GVal _2, GVal _3);
GVal on_activate__any_any_lam_1(GVal _0, GVal _1, GVal _2, GVal _3);
GVal on_activate__any_any_lam_2(GVal _0, GVal _1, GVal _2);
GVal on_activate__any_any(GVal _0);
GVal draw__any_any_any(GVal _0, GVal _1, GVal _2);
GVal gtk_drawing_area__int_int_fn_any_fn_any_fn_any_int_any(GVal _0, GVal _1, GVal _2);

GVal cr_tau() {
  _glyph_current_fn = "cr_tau";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cr_tau";
  _glyph_call_depth++;
#endif
  GVal _0 = 0;
bb_0:
  _0 = _glyph_f2i(6.28318530717958647692);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal cr_set_color(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4) {
  _glyph_current_fn = "cr_set_color";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cr_set_color";
  _glyph_call_depth++;
#endif
  GVal _5 = 0;
  GVal _6 = 0;
bb_0:
  _5 = glyph_cr_set_source_rgba(_0, _1, _2, _3, _4);
  _6 = _5;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal cr_pi() {
  _glyph_current_fn = "cr_pi";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cr_pi";
  _glyph_call_depth++;
#endif
  GVal _0 = 0;
bb_0:
  _0 = _glyph_f2i(3.14159265358979323846);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal cr_draw_rect(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4) {
  _glyph_current_fn = "cr_draw_rect";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cr_draw_rect";
  _glyph_call_depth++;
#endif
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
bb_0:
  _5 = glyph_cr_rectangle(_0, _1, _2, _3, _4);
  _6 = _5;
  _7 = glyph_cr_stroke(_0);
  _8 = _7;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal cr_draw_line(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4) {
  _glyph_current_fn = "cr_draw_line";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cr_draw_line";
  _glyph_call_depth++;
#endif
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
bb_0:
  _5 = glyph_cr_move_to(_0, _1, _2);
  _6 = _5;
  _7 = glyph_cr_line_to(_0, _3, _4);
  _8 = _7;
  _9 = glyph_cr_stroke(_0);
  _10 = _9;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal cr_draw_circle(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "cr_draw_circle";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cr_draw_circle";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
bb_0:
  _4 = _glyph_f2i(0.0);
  _5 = cr_tau();
  _6 = glyph_cr_arc(_0, _1, _2, _3, _4, _5);
  _7 = _6;
  _8 = glyph_cr_stroke(_0);
  _9 = _8;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal gtk_vsep() {
  _glyph_current_fn = "gtk_vsep";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_vsep";
  _glyph_call_depth++;
#endif
  GVal _0 = 0;
  GVal _1 = 0;
bb_0:
  _0 = glyph_gtk_orientation_vertical((GVal)0);
  _1 = glyph_gtk_separator_new(_0);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _1;
}

GVal gtk_vbox(GVal _0) {
  _glyph_current_fn = "gtk_vbox";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_vbox";
  _glyph_call_depth++;
#endif
  GVal _1 = 0;
  GVal _2 = 0;
bb_0:
  _1 = glyph_gtk_orientation_vertical((GVal)0);
  _2 = glyph_gtk_box_new(_1, _0);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _2;
}

GVal gtk_stack_page(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "gtk_stack_page";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_stack_page";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
  GVal _5 = 0;
bb_0:
  _4 = glyph_gtk_stack_add_titled(_0, _3, _1, _2);
  _5 = _4;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal gtk_scrolled(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "gtk_scrolled";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_scrolled";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
bb_0:
  _3 = glyph_gtk_scrolled_window_new((GVal)0);
  _4 = _3;
  _5 = glyph_gtk_scrolled_window_set_child(_4, _0);
  _6 = _5;
  _7 = glyph_gtk_scrolled_window_set_min_size(_4, _1, _2);
  _8 = _7;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
}

GVal gtk_paned_v(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "gtk_paned_v";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_paned_v";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
  GVal _11 = 0;
bb_0:
  _3 = glyph_gtk_orientation_vertical((GVal)0);
  _4 = glyph_gtk_paned_new(_3);
  _5 = _4;
  _6 = glyph_gtk_paned_set_start_child(_5, _0);
  _7 = _6;
  _8 = glyph_gtk_paned_set_end_child(_5, _1);
  _9 = _8;
  _10 = glyph_gtk_paned_set_position(_5, _2);
  _11 = _10;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _5;
}

GVal gtk_paned_h(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "gtk_paned_h";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_paned_h";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
  GVal _11 = 0;
bb_0:
  _3 = glyph_gtk_orientation_horizontal((GVal)0);
  _4 = glyph_gtk_paned_new(_3);
  _5 = _4;
  _6 = glyph_gtk_paned_set_start_child(_5, _0);
  _7 = _6;
  _8 = glyph_gtk_paned_set_end_child(_5, _1);
  _9 = _8;
  _10 = glyph_gtk_paned_set_position(_5, _2);
  _11 = _10;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _5;
}

GVal gtk_padded(GVal _0, GVal _1) {
  _glyph_current_fn = "gtk_padded";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_padded";
  _glyph_call_depth++;
#endif
  GVal _2 = 0;
  GVal _3 = 0;
bb_0:
  _2 = glyph_gtk_widget_set_margin(_0, _1, _1, _1, _1);
  _3 = _2;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal gtk_make_window(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "gtk_make_window";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_make_window";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
bb_0:
  _4 = glyph_gtk_window_new(_0);
  _5 = _4;
  _6 = glyph_gtk_window_set_title(_5, _1);
  _7 = _6;
  _8 = glyph_gtk_window_set_size(_5, _2, _3);
  _9 = _8;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _5;
}

GVal gtk_icon_button(GVal _0) {
  _glyph_current_fn = "gtk_icon_button";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_icon_button";
  _glyph_call_depth++;
#endif
  GVal _1 = 0;
  GVal _2 = 0;
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
bb_0:
  _1 = glyph_gtk_button_new((GVal)glyph_cstr_to_str((GVal)""));
  _2 = _1;
  _3 = glyph_gtk_image_new_from_icon(_0);
  _4 = _3;
  _5 = glyph_gtk_button_set_label(_2, (GVal)glyph_cstr_to_str((GVal)""));
  _6 = _5;
  _7 = glyph_gtk_window_set_child(_2, _4);
  _8 = _7;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _2;
}

GVal gtk_hsep() {
  _glyph_current_fn = "gtk_hsep";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_hsep";
  _glyph_call_depth++;
#endif
  GVal _0 = 0;
  GVal _1 = 0;
bb_0:
  _0 = glyph_gtk_orientation_horizontal((GVal)0);
  _1 = glyph_gtk_separator_new(_0);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _1;
}

GVal gtk_header_bar_title(GVal _0) {
  _glyph_current_fn = "gtk_header_bar_title";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_header_bar_title";
  _glyph_call_depth++;
#endif
  GVal _1 = 0;
  GVal _2 = 0;
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
bb_0:
  _1 = glyph_gtk_header_bar_new((GVal)0);
  _2 = _1;
  _3 = glyph_gtk_label_new(_0);
  _4 = _3;
  _5 = glyph_gtk_header_bar_set_title_widget(_2, _4);
  _6 = _5;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _2;
}

GVal gtk_hbox(GVal _0) {
  _glyph_current_fn = "gtk_hbox";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_hbox";
  _glyph_call_depth++;
#endif
  GVal _1 = 0;
  GVal _2 = 0;
bb_0:
  _1 = glyph_gtk_orientation_horizontal((GVal)0);
  _2 = glyph_gtk_box_new(_1, _0);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _2;
}

GVal gtk_grid_spaced(GVal _0, GVal _1) {
  _glyph_current_fn = "gtk_grid_spaced";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_grid_spaced";
  _glyph_call_depth++;
#endif
  GVal _2 = 0;
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
bb_0:
  _2 = glyph_gtk_grid_new((GVal)0);
  _3 = _2;
  _4 = glyph_gtk_grid_set_row_spacing(_3, _0);
  _5 = _4;
  _6 = glyph_gtk_grid_set_col_spacing(_3, _1);
  _7 = _6;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _3;
}

GVal gtk_drawing_area(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "gtk_drawing_area";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_drawing_area";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
bb_0:
  _3 = glyph_gtk_drawing_area_new((GVal)0);
  _4 = _3;
  _5 = glyph_gtk_drawing_area_set_content_width(_4, _0);
  _6 = _5;
  _7 = glyph_gtk_drawing_area_set_content_height(_4, _1);
  _8 = _7;
  _9 = glyph_gtk_drawing_area_set_draw_func(_4, _2);
  _10 = _9;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
}

GVal gtk_action(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "gtk_action";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_action";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
bb_0:
  _3 = glyph_gtk_action_new(_1);
  _4 = _3;
  _5 = glyph_gtk_action_on_activate(_4, _2);
  _6 = _5;
  _7 = glyph_gtk_app_add_action(_0, _4);
  _8 = _7;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
}

GVal on_activate_lam_0(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "on_activate_lam_0";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "on_activate_lam_0";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
bb_0:
  _4 = draw__any_any_any(_1, _2, _3);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
}

GVal on_activate_lam_1(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "on_activate_lam_1";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "on_activate_lam_1";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
bb_0:
  _4 = glyph_int_to_str(_1);
  _5 = glyph_int_to_str(_2);
  _6 = glyph_int_to_str(_3);
  { GVal __sb = glyph_sb_new(); glyph_sb_append(__sb, (GVal)glyph_cstr_to_str((GVal)"click: n=")); glyph_sb_append(__sb, _4); glyph_sb_append(__sb, (GVal)glyph_cstr_to_str((GVal)" x=")); glyph_sb_append(__sb, _5); glyph_sb_append(__sb, (GVal)glyph_cstr_to_str((GVal)" y=")); glyph_sb_append(__sb, _6); _7 = (GVal)glyph_sb_build(__sb); }
  _8 = glyph_println(_7);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _8;
}

GVal on_activate_lam_2(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "on_activate_lam_2";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "on_activate_lam_2";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
bb_0:
  _3 = glyph_int_to_str(_1);
  _4 = glyph_int_to_str(_2);
  { GVal __sb = glyph_sb_new(); glyph_sb_append(__sb, (GVal)glyph_cstr_to_str((GVal)"motion: x=")); glyph_sb_append(__sb, _3); glyph_sb_append(__sb, (GVal)glyph_cstr_to_str((GVal)" y=")); glyph_sb_append(__sb, _4); _5 = (GVal)glyph_sb_build(__sb); }
  _6 = glyph_println(_5);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _6;
}

GVal on_activate(GVal _0) {
  _glyph_current_fn = "on_activate";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "on_activate";
  _glyph_call_depth++;
#endif
  GVal _1 = 0;
  GVal _2 = 0;
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
  GVal _11 = 0;
  GVal _12 = 0;
  GVal _13 = 0;
  GVal _14 = 0;
  GVal _15 = 0;
  GVal _16 = 0;
  GVal _17 = 0;
  GVal _18 = 0;
  GVal _19 = 0;
  GVal _20 = 0;
  GVal _21 = 0;
  GVal _22 = 0;
  GVal _23 = 0;
  GVal _24 = 0;
  GVal _25 = 0;
  GVal _26 = 0;
bb_0:
  _1 = glyph_gtk_window_new(_0);
  _2 = _1;
  _3 = glyph_gtk_window_set_title(_2, (GVal)glyph_cstr_to_str((GVal)"Cairo Test"));
  _4 = _3;
  _5 = glyph_gtk_window_set_size(_2, (GVal)600, (GVal)350);
  _6 = _5;
  { GVal* __c = (GVal*)glyph_alloc(8); __c[0] = (GVal)&on_activate_lam_0; _7 = (GVal)__c; }
  _8 = gtk_drawing_area__int_int_fn_any_fn_any_fn_any_int_any((GVal)600, (GVal)350, _7);
  _9 = _8;
  _10 = glyph_gtk_gesture_click_new((GVal)0);
  _11 = _10;
  { GVal* __c = (GVal*)glyph_alloc(8); __c[0] = (GVal)&on_activate_lam_1; _12 = (GVal)__c; }
  _13 = glyph_gtk_gesture_click_on_pressed(_11, _12);
  _14 = _13;
  _15 = glyph_gtk_widget_add_controller(_9, _11);
  _16 = _15;
  _17 = glyph_gtk_motion_controller_new((GVal)0);
  _18 = _17;
  { GVal* __c = (GVal*)glyph_alloc(8); __c[0] = (GVal)&on_activate_lam_2; _19 = (GVal)__c; }
  _20 = glyph_gtk_motion_on_motion(_18, _19);
  _21 = _20;
  _22 = glyph_gtk_widget_add_controller(_9, _18);
  _23 = _22;
  _24 = glyph_gtk_window_set_child(_2, _9);
  _25 = _24;
  _26 = glyph_gtk_window_present(_2);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _26;
}

GVal main_lam_0(GVal _0, GVal _1) {
  _glyph_current_fn = "main_lam_0";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "main_lam_0";
  _glyph_call_depth++;
#endif
  GVal _2 = 0;
bb_0:
  _2 = on_activate__any_any(_1);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _2;
}

GVal glyph_main() {
  _glyph_current_fn = "main";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "main";
  _glyph_call_depth++;
#endif
  GVal _0 = 0;
  GVal _1 = 0;
  GVal _2 = 0;
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
bb_0:
  _0 = glyph_gtk_app_new((GVal)glyph_cstr_to_str((GVal)"com.glyph.cairo_test"));
  _1 = _0;
  { GVal* __c = (GVal*)glyph_alloc(8); __c[0] = (GVal)&main_lam_0; _2 = (GVal)__c; }
  _3 = glyph_gtk_on_activate(_1, _2);
  _4 = _3;
  _5 = glyph_gtk_app_run(_1);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _5;
}

GVal draw(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "draw";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "draw";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
  GVal _11 = 0;
  GVal _12 = 0;
  GVal _13 = 0;
  GVal _14 = 0;
  GVal _15 = 0;
  GVal _16 = 0;
  GVal _17 = 0;
  GVal _18 = 0;
  GVal _19 = 0;
  GVal _20 = 0;
  GVal _21 = 0;
  GVal _22 = 0;
  GVal _23 = 0;
  GVal _24 = 0;
  GVal _25 = 0;
  GVal _26 = 0;
  GVal _27 = 0;
  GVal _28 = 0;
  GVal _29 = 0;
  GVal _30 = 0;
  GVal _31 = 0;
  GVal _32 = 0;
  GVal _33 = 0;
  GVal _34 = 0;
  GVal _35 = 0;
  GVal _36 = 0;
  GVal _37 = 0;
  GVal _38 = 0;
  GVal _39 = 0;
  GVal _40 = 0;
  GVal _41 = 0;
  GVal _42 = 0;
  GVal _43 = 0;
  GVal _44 = 0;
  GVal _45 = 0;
  GVal _46 = 0;
  GVal _47 = 0;
  GVal _48 = 0;
  GVal _49 = 0;
  GVal _50 = 0;
  GVal _51 = 0;
  GVal _52 = 0;
  GVal _53 = 0;
  GVal _54 = 0;
  GVal _55 = 0;
  GVal _56 = 0;
  GVal _57 = 0;
  GVal _58 = 0;
  GVal _59 = 0;
  GVal _60 = 0;
  GVal _61 = 0;
  GVal _62 = 0;
  GVal _63 = 0;
  GVal _64 = 0;
bb_0:
  _3 = _glyph_f2i(0.9);
  _4 = _glyph_f2i(0.9);
  _5 = _glyph_f2i(0.9);
  _6 = glyph_cr_set_source_rgb(_0, _3, _4, _5);
  _7 = _6;
  _8 = glyph_cr_paint(_0);
  _9 = _8;
  _10 = _glyph_f2i(0.2);
  _11 = _glyph_f2i(0.4);
  _12 = _glyph_f2i(0.8);
  _13 = _glyph_f2i(1.0);
  _14 = glyph_cr_set_source_rgba(_0, _10, _11, _12, _13);
  _15 = _14;
  _16 = _glyph_f2i(3.0);
  _17 = glyph_cr_set_line_width(_0, _16);
  _18 = _17;
  _19 = _glyph_f2i(50.0);
  _20 = _glyph_f2i(50.0);
  _21 = _glyph_f2i(200.0);
  _22 = _glyph_f2i(100.0);
  _23 = cr_draw_rect__any_float_float_float_float(_0, _19, _20, _21, _22);
  _24 = _23;
  _25 = _glyph_f2i(0.8);
  _26 = _glyph_f2i(0.2);
  _27 = _glyph_f2i(0.2);
  _28 = _glyph_f2i(1.0);
  _29 = glyph_cr_set_source_rgba(_0, _25, _26, _27, _28);
  _30 = _29;
  _31 = _glyph_f2i(400.0);
  _32 = _glyph_f2i(150.0);
  _33 = _glyph_f2i(60.0);
  _34 = cr_draw_circle__any_float_float_float(_0, _31, _32, _33);
  _35 = _34;
  _36 = _glyph_f2i(0.2);
  _37 = _glyph_f2i(0.7);
  _38 = _glyph_f2i(0.3);
  _39 = _glyph_f2i(1.0);
  _40 = glyph_cr_set_source_rgba(_0, _36, _37, _38, _39);
  _41 = _40;
  _42 = _glyph_f2i(2.0);
  _43 = glyph_cr_set_line_width(_0, _42);
  _44 = _43;
  _45 = _glyph_f2i(100.0);
  _46 = _glyph_f2i(250.0);
  _47 = _glyph_f2i(500.0);
  _48 = _glyph_f2i(250.0);
  _49 = cr_draw_line__any_float_float_float_float(_0, _45, _46, _47, _48);
  _50 = _49;
  _51 = _glyph_f2i(0.6);
  _52 = _glyph_f2i(0.3);
  _53 = _glyph_f2i(0.8);
  _54 = _glyph_f2i(0.7);
  _55 = glyph_cr_set_source_rgba(_0, _51, _52, _53, _54);
  _56 = _55;
  _57 = _glyph_f2i(300.0);
  _58 = _glyph_f2i(50.0);
  _59 = _glyph_f2i(80.0);
  _60 = _glyph_f2i(80.0);
  _61 = glyph_cr_rectangle(_0, _57, _58, _59, _60);
  _62 = _61;
  _63 = glyph_cr_fill(_0);
  _64 = _63;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)0;
}

GVal cr_draw_rect__any_float_float_float_float(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4) {
  _glyph_current_fn = "cr_draw_rect__any_float_float_float_float";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cr_draw_rect__any_float_float_float_float";
  _glyph_call_depth++;
#endif
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
bb_0:
  _5 = glyph_cr_rectangle(_0, _1, _2, _3, _4);
  _6 = _5;
  _7 = glyph_cr_stroke(_0);
  _8 = _7;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal cr_draw_circle__any_float_float_float(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "cr_draw_circle__any_float_float_float";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cr_draw_circle__any_float_float_float";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
bb_0:
  _4 = _glyph_f2i(0.0);
  _5 = cr_tau();
  _6 = glyph_cr_arc(_0, _1, _2, _3, _4, _5);
  _7 = _6;
  _8 = glyph_cr_stroke(_0);
  _9 = _8;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal cr_draw_line__any_float_float_float_float(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4) {
  _glyph_current_fn = "cr_draw_line__any_float_float_float_float";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cr_draw_line__any_float_float_float_float";
  _glyph_call_depth++;
#endif
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
bb_0:
  _5 = glyph_cr_move_to(_0, _1, _2);
  _6 = _5;
  _7 = glyph_cr_line_to(_0, _3, _4);
  _8 = _7;
  _9 = glyph_cr_stroke(_0);
  _10 = _9;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _0;
}

GVal on_activate__any_any_lam_0(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "on_activate__any_any_lam_0";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "on_activate__any_any_lam_0";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
bb_0:
  _4 = draw(_1, _2, _3);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
}

GVal on_activate__any_any_lam_1(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "on_activate__any_any_lam_1";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "on_activate__any_any_lam_1";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
bb_0:
  _4 = glyph_int_to_str(_1);
  _5 = glyph_int_to_str(_2);
  _6 = glyph_int_to_str(_3);
  { GVal __sb = glyph_sb_new(); glyph_sb_append(__sb, (GVal)glyph_cstr_to_str((GVal)"click: n=")); glyph_sb_append(__sb, _4); glyph_sb_append(__sb, (GVal)glyph_cstr_to_str((GVal)" x=")); glyph_sb_append(__sb, _5); glyph_sb_append(__sb, (GVal)glyph_cstr_to_str((GVal)" y=")); glyph_sb_append(__sb, _6); _7 = (GVal)glyph_sb_build(__sb); }
  _8 = glyph_println(_7);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _8;
}

GVal on_activate__any_any_lam_2(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "on_activate__any_any_lam_2";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "on_activate__any_any_lam_2";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
bb_0:
  _3 = glyph_int_to_str(_1);
  _4 = glyph_int_to_str(_2);
  { GVal __sb = glyph_sb_new(); glyph_sb_append(__sb, (GVal)glyph_cstr_to_str((GVal)"motion: x=")); glyph_sb_append(__sb, _3); glyph_sb_append(__sb, (GVal)glyph_cstr_to_str((GVal)" y=")); glyph_sb_append(__sb, _4); _5 = (GVal)glyph_sb_build(__sb); }
  _6 = glyph_println(_5);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _6;
}

GVal on_activate__any_any(GVal _0) {
  _glyph_current_fn = "on_activate__any_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "on_activate__any_any";
  _glyph_call_depth++;
#endif
  GVal _1 = 0;
  GVal _2 = 0;
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
  GVal _11 = 0;
  GVal _12 = 0;
  GVal _13 = 0;
  GVal _14 = 0;
  GVal _15 = 0;
  GVal _16 = 0;
  GVal _17 = 0;
  GVal _18 = 0;
  GVal _19 = 0;
  GVal _20 = 0;
  GVal _21 = 0;
  GVal _22 = 0;
  GVal _23 = 0;
  GVal _24 = 0;
  GVal _25 = 0;
  GVal _26 = 0;
bb_0:
  _1 = glyph_gtk_window_new(_0);
  _2 = _1;
  _3 = glyph_gtk_window_set_title(_2, (GVal)glyph_cstr_to_str((GVal)"Cairo Test"));
  _4 = _3;
  _5 = glyph_gtk_window_set_size(_2, (GVal)600, (GVal)350);
  _6 = _5;
  { GVal* __c = (GVal*)glyph_alloc(8); __c[0] = (GVal)&on_activate__any_any_lam_0; _7 = (GVal)__c; }
  _8 = gtk_drawing_area((GVal)600, (GVal)350, _7);
  _9 = _8;
  _10 = glyph_gtk_gesture_click_new((GVal)0);
  _11 = _10;
  { GVal* __c = (GVal*)glyph_alloc(8); __c[0] = (GVal)&on_activate__any_any_lam_1; _12 = (GVal)__c; }
  _13 = glyph_gtk_gesture_click_on_pressed(_11, _12);
  _14 = _13;
  _15 = glyph_gtk_widget_add_controller(_9, _11);
  _16 = _15;
  _17 = glyph_gtk_motion_controller_new((GVal)0);
  _18 = _17;
  { GVal* __c = (GVal*)glyph_alloc(8); __c[0] = (GVal)&on_activate__any_any_lam_2; _19 = (GVal)__c; }
  _20 = glyph_gtk_motion_on_motion(_18, _19);
  _21 = _20;
  _22 = glyph_gtk_widget_add_controller(_9, _18);
  _23 = _22;
  _24 = glyph_gtk_window_set_child(_2, _9);
  _25 = _24;
  _26 = glyph_gtk_window_present(_2);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _26;
}

GVal draw__any_any_any(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "draw__any_any_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "draw__any_any_any";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
  GVal _11 = 0;
  GVal _12 = 0;
  GVal _13 = 0;
  GVal _14 = 0;
  GVal _15 = 0;
  GVal _16 = 0;
  GVal _17 = 0;
  GVal _18 = 0;
  GVal _19 = 0;
  GVal _20 = 0;
  GVal _21 = 0;
  GVal _22 = 0;
  GVal _23 = 0;
  GVal _24 = 0;
  GVal _25 = 0;
  GVal _26 = 0;
  GVal _27 = 0;
  GVal _28 = 0;
  GVal _29 = 0;
  GVal _30 = 0;
  GVal _31 = 0;
  GVal _32 = 0;
  GVal _33 = 0;
  GVal _34 = 0;
  GVal _35 = 0;
  GVal _36 = 0;
  GVal _37 = 0;
  GVal _38 = 0;
  GVal _39 = 0;
  GVal _40 = 0;
  GVal _41 = 0;
  GVal _42 = 0;
  GVal _43 = 0;
  GVal _44 = 0;
  GVal _45 = 0;
  GVal _46 = 0;
  GVal _47 = 0;
  GVal _48 = 0;
  GVal _49 = 0;
  GVal _50 = 0;
  GVal _51 = 0;
  GVal _52 = 0;
  GVal _53 = 0;
  GVal _54 = 0;
  GVal _55 = 0;
  GVal _56 = 0;
  GVal _57 = 0;
  GVal _58 = 0;
  GVal _59 = 0;
  GVal _60 = 0;
  GVal _61 = 0;
  GVal _62 = 0;
  GVal _63 = 0;
  GVal _64 = 0;
bb_0:
  _3 = _glyph_f2i(0.9);
  _4 = _glyph_f2i(0.9);
  _5 = _glyph_f2i(0.9);
  _6 = glyph_cr_set_source_rgb(_0, _3, _4, _5);
  _7 = _6;
  _8 = glyph_cr_paint(_0);
  _9 = _8;
  _10 = _glyph_f2i(0.2);
  _11 = _glyph_f2i(0.4);
  _12 = _glyph_f2i(0.8);
  _13 = _glyph_f2i(1.0);
  _14 = glyph_cr_set_source_rgba(_0, _10, _11, _12, _13);
  _15 = _14;
  _16 = _glyph_f2i(3.0);
  _17 = glyph_cr_set_line_width(_0, _16);
  _18 = _17;
  _19 = _glyph_f2i(50.0);
  _20 = _glyph_f2i(50.0);
  _21 = _glyph_f2i(200.0);
  _22 = _glyph_f2i(100.0);
  _23 = cr_draw_rect(_0, _19, _20, _21, _22);
  _24 = _23;
  _25 = _glyph_f2i(0.8);
  _26 = _glyph_f2i(0.2);
  _27 = _glyph_f2i(0.2);
  _28 = _glyph_f2i(1.0);
  _29 = glyph_cr_set_source_rgba(_0, _25, _26, _27, _28);
  _30 = _29;
  _31 = _glyph_f2i(400.0);
  _32 = _glyph_f2i(150.0);
  _33 = _glyph_f2i(60.0);
  _34 = cr_draw_circle(_0, _31, _32, _33);
  _35 = _34;
  _36 = _glyph_f2i(0.2);
  _37 = _glyph_f2i(0.7);
  _38 = _glyph_f2i(0.3);
  _39 = _glyph_f2i(1.0);
  _40 = glyph_cr_set_source_rgba(_0, _36, _37, _38, _39);
  _41 = _40;
  _42 = _glyph_f2i(2.0);
  _43 = glyph_cr_set_line_width(_0, _42);
  _44 = _43;
  _45 = _glyph_f2i(100.0);
  _46 = _glyph_f2i(250.0);
  _47 = _glyph_f2i(500.0);
  _48 = _glyph_f2i(250.0);
  _49 = cr_draw_line(_0, _45, _46, _47, _48);
  _50 = _49;
  _51 = _glyph_f2i(0.6);
  _52 = _glyph_f2i(0.3);
  _53 = _glyph_f2i(0.8);
  _54 = _glyph_f2i(0.7);
  _55 = glyph_cr_set_source_rgba(_0, _51, _52, _53, _54);
  _56 = _55;
  _57 = _glyph_f2i(300.0);
  _58 = _glyph_f2i(50.0);
  _59 = _glyph_f2i(80.0);
  _60 = _glyph_f2i(80.0);
  _61 = glyph_cr_rectangle(_0, _57, _58, _59, _60);
  _62 = _61;
  _63 = glyph_cr_fill(_0);
  _64 = _63;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)0;
}

GVal gtk_drawing_area__int_int_fn_any_fn_any_fn_any_int_any(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "gtk_drawing_area__int_int_fn_any_fn_any_fn_any_int_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "gtk_drawing_area__int_int_fn_any_fn_any_fn_any_int_any";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
bb_0:
  _3 = glyph_gtk_drawing_area_new((GVal)0);
  _4 = _3;
  _5 = glyph_gtk_drawing_area_set_content_width(_4, _0);
  _6 = _5;
  _7 = glyph_gtk_drawing_area_set_content_height(_4, _1);
  _8 = _7;
  _9 = glyph_gtk_drawing_area_set_draw_func(_4, _2);
  _10 = _9;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
}



extern void glyph_set_args(int argc, char** argv);
int main(int argc, char** argv) {
  GC_INIT();
  signal(SIGSEGV, _glyph_sigsegv);
  signal(SIGFPE, _glyph_sigfpe);
  glyph_set_args(argc, argv);
  return (int)glyph_main();
}
