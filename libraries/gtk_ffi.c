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

/* ── Float bit-cast helpers ───────────────────────────────────────────── */

static GVal _f2i(double f) { GVal v; memcpy(&v, &f, sizeof(double)); return v; }
static double _i2f(GVal v) { double f; memcpy(&f, &v, sizeof(double)); return f; }

/* ── Drawing area ─────────────────────────────────────────────────────── */

static void _tramp_draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data) {
    (void)area;
    GVal *cl = (GVal *)data;
    ((GVal (*)(GVal, GVal, GVal, GVal))cl[0])((GVal)cl, (GVal)cr, (GVal)w, (GVal)h);
}

GVal gtk_ffi_drawing_area(GVal w, GVal h, GVal closure) {
    GtkWidget *da = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(da), (int)w);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(da), (int)h);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), _tramp_draw, (gpointer)closure, NULL);
    return (GVal)da;
}

/* ── Gesture click ────────────────────────────────────────────────────── */

static void _tramp_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    (void)gesture;
    GVal *cl = (GVal *)data;
    ((GVal (*)(GVal, GVal, GVal, GVal))cl[0])((GVal)cl, (GVal)n_press, _f2i(x), _f2i(y));
}

GVal gtk_ffi_gesture_click_new(GVal _d) { (void)_d;
    return (GVal)gtk_gesture_click_new();
}

GVal gtk_ffi_gesture_click_on_pressed(GVal gesture, GVal closure) {
    g_signal_connect((void *)gesture, "pressed",
                     G_CALLBACK(_tramp_click), (gpointer)closure);
    return 0;
}

/* ── Gesture drag ─────────────────────────────────────────────────────── */

static void _tramp_drag(GtkGestureDrag *gesture, double x, double y, gpointer data) {
    (void)gesture;
    GVal *cl = (GVal *)data;
    ((GVal (*)(GVal, GVal, GVal))cl[0])((GVal)cl, _f2i(x), _f2i(y));
}

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

GVal gtk_ffi_gesture_set_button(GVal gesture, GVal button) {
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE((void *)gesture), (guint)button);
    return 0;
}

/* ── Scroll controller ────────────────────────────────────────────────── */

static gboolean _tramp_scroll(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer data) {
    (void)ctrl;
    GVal *cl = (GVal *)data;
    return (gboolean)((GVal (*)(GVal, GVal, GVal))cl[0])((GVal)cl, _f2i(dx), _f2i(dy));
}

GVal gtk_ffi_scroll_flags_vertical(GVal _d) { (void)_d;
    return (GVal)GTK_EVENT_CONTROLLER_SCROLL_VERTICAL;
}

GVal gtk_ffi_scroll_controller_new(GVal flags) {
    return (GVal)gtk_event_controller_scroll_new((GtkEventControllerScrollFlags)flags);
}

GVal gtk_ffi_scroll_on_scroll(GVal ctrl, GVal closure) {
    g_signal_connect((void *)ctrl, "scroll",
                     G_CALLBACK(_tramp_scroll), (gpointer)closure);
    return 0;
}

/* ── Paned ────────────────────────────────────────────────────────────── */

GVal gtk_ffi_paned_new(GVal orientation) {
    return (GVal)gtk_paned_new((GtkOrientation)orientation);
}

GVal gtk_ffi_paned_set_start_child(GVal paned, GVal child) {
    gtk_paned_set_start_child(GTK_PANED((void *)paned), GTK_WIDGET((void *)child));
    return 0;
}

GVal gtk_ffi_paned_set_end_child(GVal paned, GVal child) {
    gtk_paned_set_end_child(GTK_PANED((void *)paned), GTK_WIDGET((void *)child));
    return 0;
}

GVal gtk_ffi_paned_set_shrink_start(GVal paned, GVal shrink) {
    gtk_paned_set_shrink_start_child(GTK_PANED((void *)paned), (gboolean)shrink);
    return 0;
}

GVal gtk_ffi_paned_set_shrink_end(GVal paned, GVal shrink) {
    gtk_paned_set_shrink_end_child(GTK_PANED((void *)paned), (gboolean)shrink);
    return 0;
}

GVal gtk_ffi_paned_set_position(GVal paned, GVal pos) {
    gtk_paned_set_position(GTK_PANED((void *)paned), (int)pos);
    return 0;
}

/* ── Spin button signal ───────────────────────────────────────────────── */

GVal gtk_ffi_on_spin_value_changed(GVal sb, GVal closure) {
    g_signal_connect((void *)sb, "value-changed",
                     G_CALLBACK(_tramp_void_widget), (gpointer)closure);
    return 0;
}
