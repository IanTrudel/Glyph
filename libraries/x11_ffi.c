/* x11_ffi.c — X11/Xlib FFI wrapper for Glyph
 *
 * Provides GVal-ABI wrappers for Xlib functions.
 * Prepended before Glyph-generated code via cc_prepend.
 *
 * Compile with: -lX11
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

typedef intptr_t GVal;

/* ── String conversion ─────────────────────────────────────────────────── */

/* Glyph fat string {ptr, len} -> null-terminated C string (malloc'd) */
static const char* _gs(GVal glyph_str) {
    if (!glyph_str) return strdup("");
    const char *ptr = *(const char **)glyph_str;
    long long len = *(long long *)((char *)glyph_str + 8);
    char *cstr = (char *)malloc(len + 1);
    memcpy(cstr, ptr, (size_t)len);
    cstr[len] = '\0';
    return cstr;
}

/* Null-terminated C string -> Glyph fat string {ptr, len} (heap) */
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

/* ── Global state ──────────────────────────────────────────────────────── */

static Display *_dpy;
static XEvent   _evt;
static XWindowAttributes _wattr;
static XFontStruct *_font = NULL;

/* ── Display & Screen ──────────────────────────────────────────────────── */

GVal gx_open_display(GVal dummy) {
    _dpy = XOpenDisplay(NULL);
    return (GVal)_dpy;
}

GVal gx_close_display(GVal dummy) {
    if (_dpy) XCloseDisplay(_dpy);
    return 0;
}

GVal gx_default_screen(GVal dummy) {
    return (GVal)DefaultScreen(_dpy);
}

GVal gx_root_window(GVal scr) {
    return (GVal)RootWindow(_dpy, (int)scr);
}

GVal gx_black_pixel(GVal scr) {
    return (GVal)BlackPixel(_dpy, (int)scr);
}

GVal gx_white_pixel(GVal scr) {
    return (GVal)WhitePixel(_dpy, (int)scr);
}

GVal gx_display_width(GVal scr) {
    return (GVal)DisplayWidth(_dpy, (int)scr);
}

GVal gx_display_height(GVal scr) {
    return (GVal)DisplayHeight(_dpy, (int)scr);
}

/* ── Window Creation & Management ──────────────────────────────────────── */

GVal gx_create_window(GVal parent, GVal x, GVal y,
                      GVal w, GVal h, GVal bg) {
    return (GVal)XCreateSimpleWindow(_dpy, (Window)parent,
        (int)x, (int)y, (unsigned)w, (unsigned)h, 0, 0, (unsigned long)bg);
}

GVal gx_map_window(GVal win) {
    XMapWindow(_dpy, (Window)win);
    return 0;
}

GVal gx_unmap_window(GVal win) {
    XUnmapWindow(_dpy, (Window)win);
    return 0;
}

GVal gx_destroy_window(GVal win) {
    XDestroyWindow(_dpy, (Window)win);
    return 0;
}

GVal gx_reparent_window(GVal win, GVal parent, GVal x, GVal y) {
    XReparentWindow(_dpy, (Window)win, (Window)parent, (int)x, (int)y);
    return 0;
}

GVal gx_move_window(GVal win, GVal x, GVal y) {
    XMoveWindow(_dpy, (Window)win, (int)x, (int)y);
    return 0;
}

GVal gx_resize_window(GVal win, GVal w, GVal h) {
    XResizeWindow(_dpy, (Window)win, (unsigned)w, (unsigned)h);
    return 0;
}

GVal gx_move_resize_window(GVal win, GVal x, GVal y, GVal w, GVal h) {
    XMoveResizeWindow(_dpy, (Window)win, (int)x, (int)y, (unsigned)w, (unsigned)h);
    return 0;
}

GVal gx_raise_window(GVal win) {
    XRaiseWindow(_dpy, (Window)win);
    return 0;
}

GVal gx_configure_window(GVal win, GVal mask,
                         GVal x, GVal y, GVal w, GVal h,
                         GVal border, GVal sibling, GVal stack_mode) {
    XWindowChanges wc;
    wc.x = (int)x;
    wc.y = (int)y;
    wc.width = (int)w;
    wc.height = (int)h;
    wc.border_width = (int)border;
    wc.sibling = (Window)sibling;
    wc.stack_mode = (int)stack_mode;
    XConfigureWindow(_dpy, (Window)win, (unsigned)mask, &wc);
    return 0;
}

GVal gx_set_window_border(GVal win, GVal pixel) {
    XSetWindowBorder(_dpy, (Window)win, (unsigned long)pixel);
    return 0;
}

GVal gx_set_window_border_width(GVal win, GVal width) {
    XSetWindowBorderWidth(_dpy, (Window)win, (unsigned)width);
    return 0;
}

GVal gx_add_to_save_set(GVal win) {
    XAddToSaveSet(_dpy, (Window)win);
    return 0;
}

GVal gx_remove_from_save_set(GVal win) {
    XRemoveFromSaveSet(_dpy, (Window)win);
    return 0;
}

/* ── Input Selection & Events ──────────────────────────────────────────── */

GVal gx_select_input(GVal win, GVal mask) {
    XSelectInput(_dpy, (Window)win, (long)mask);
    return 0;
}

GVal gx_next_event(GVal dummy) {
    XNextEvent(_dpy, &_evt);
    return (GVal)_evt.type;
}

GVal gx_check_event(GVal dummy) {
    if (XPending(_dpy)) {
        XNextEvent(_dpy, &_evt);
        return (GVal)_evt.type;
    }
    return 0;
}

GVal gx_pending(GVal dummy) {
    return (GVal)XPending(_dpy);
}

/* ── Event Field Extractors ────────────────────────────────────────────── */

/* xany.window = the window that selected the event (often root for WM) */
GVal gx_event_window(GVal dummy) {
    return (GVal)_evt.xany.window;
}

/* For substructure redirect/notify: the actual subject window */
GVal gx_event_map_window(GVal d)      { return (GVal)_evt.xmaprequest.window; }
GVal gx_event_cfg_window(GVal d)      { return (GVal)_evt.xconfigurerequest.window; }
GVal gx_event_destroy_window(GVal d)  { return (GVal)_evt.xdestroywindow.window; }
GVal gx_event_unmap_window(GVal d)    { return (GVal)_evt.xunmap.window; }
GVal gx_event_property_window(GVal d) { return (GVal)_evt.xproperty.window; }

GVal gx_event_x(GVal dummy) {
    return (GVal)_evt.xbutton.x;
}

GVal gx_event_y(GVal dummy) {
    return (GVal)_evt.xbutton.y;
}

GVal gx_event_x_root(GVal dummy) {
    return (GVal)_evt.xbutton.x_root;
}

GVal gx_event_y_root(GVal dummy) {
    return (GVal)_evt.xbutton.y_root;
}

GVal gx_event_button(GVal dummy) {
    return (GVal)_evt.xbutton.button;
}

GVal gx_event_state(GVal dummy) {
    return (GVal)_evt.xbutton.state;
}

GVal gx_event_subwindow(GVal dummy) {
    return (GVal)_evt.xbutton.subwindow;
}

GVal gx_event_keycode(GVal dummy) {
    return (GVal)_evt.xkey.keycode;
}

GVal gx_event_width(GVal dummy) {
    return (GVal)_evt.xconfigurerequest.width;
}

GVal gx_event_height(GVal dummy) {
    return (GVal)_evt.xconfigurerequest.height;
}

GVal gx_event_border(GVal dummy) {
    return (GVal)_evt.xconfigurerequest.border_width;
}

GVal gx_event_value_mask(GVal dummy) {
    return (GVal)_evt.xconfigurerequest.value_mask;
}

GVal gx_event_above(GVal dummy) {
    return (GVal)_evt.xconfigurerequest.above;
}

GVal gx_event_detail(GVal dummy) {
    return (GVal)_evt.xconfigurerequest.detail;
}

GVal gx_event_atom(GVal dummy) {
    return (GVal)_evt.xproperty.atom;
}

/* ConfigureRequest also has x,y — use separate extractors */
GVal gx_event_cfg_x(GVal dummy) {
    return (GVal)_evt.xconfigurerequest.x;
}

GVal gx_event_cfg_y(GVal dummy) {
    return (GVal)_evt.xconfigurerequest.y;
}

/* ── Keyboard ──────────────────────────────────────────────────────────── */

GVal gx_lookup_keysym(GVal keycode) {
    return (GVal)XKeycodeToKeysym(_dpy, (KeyCode)keycode, 0);
}

GVal gx_keysym_to_keycode(GVal keysym) {
    return (GVal)XKeysymToKeycode(_dpy, (KeySym)keysym);
}

/* Keysym constants */
GVal gx_keysym_tab(GVal d)    { return (GVal)XK_Tab; }
GVal gx_keysym_return(GVal d) { return (GVal)XK_Return; }
GVal gx_keysym_escape(GVal d) { return (GVal)XK_Escape; }
GVal gx_keysym_f4(GVal d)     { return (GVal)XK_F4; }
GVal gx_keysym_q(GVal d)      { return (GVal)XK_q; }

/* ── Focus & Grabs ─────────────────────────────────────────────────────── */

GVal gx_set_input_focus(GVal win) {
    XSetInputFocus(_dpy, (Window)win, RevertToParent, CurrentTime);
    return 0;
}

GVal gx_grab_button(GVal button, GVal modifiers, GVal win) {
    XGrabButton(_dpy, (unsigned)button, (unsigned)modifiers, (Window)win,
                True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
    return 0;
}

GVal gx_ungrab_button(GVal button, GVal modifiers, GVal win) {
    XUngrabButton(_dpy, (unsigned)button, (unsigned)modifiers, (Window)win);
    return 0;
}

GVal gx_grab_key(GVal keysym, GVal modifiers, GVal win) {
    KeyCode kc = XKeysymToKeycode(_dpy, (KeySym)keysym);
    if (kc) {
        XGrabKey(_dpy, kc, (unsigned)modifiers, (Window)win,
                 True, GrabModeAsync, GrabModeAsync);
    }
    return 0;
}

GVal gx_ungrab_key(GVal keysym, GVal modifiers, GVal win) {
    KeyCode kc = XKeysymToKeycode(_dpy, (KeySym)keysym);
    if (kc) {
        XUngrabKey(_dpy, kc, (unsigned)modifiers, (Window)win);
    }
    return 0;
}

GVal gx_grab_pointer(GVal win, GVal mask) {
    return (GVal)XGrabPointer(_dpy, (Window)win, True, (unsigned)mask,
                              GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
}

GVal gx_ungrab_pointer(GVal dummy) {
    XUngrabPointer(_dpy, CurrentTime);
    return 0;
}

/* ── Cursor ─────────────────────────────────────────────────────────────── */

GVal gx_set_default_cursor(GVal win) {
    Cursor c = XCreateFontCursor(_dpy, 68); /* XC_left_ptr */
    XDefineCursor(_dpy, (Window)win, c);
    return 0;
}

/* ── Atoms & Properties ────────────────────────────────────────────────── */

GVal gx_intern_atom(GVal name, GVal only_if_exists) {
    const char *n = _gs(name);
    Atom a = XInternAtom(_dpy, n, (Bool)only_if_exists);
    free((void *)n);
    return (GVal)a;
}

/* ── Window Attributes ─────────────────────────────────────────────────── */

GVal gx_get_window_attributes(GVal win) {
    return (GVal)XGetWindowAttributes(_dpy, (Window)win, &_wattr);
}

GVal gx_wattr_x(GVal d)                  { return (GVal)_wattr.x; }
GVal gx_wattr_y(GVal d)                  { return (GVal)_wattr.y; }
GVal gx_wattr_width(GVal d)              { return (GVal)_wattr.width; }
GVal gx_wattr_height(GVal d)             { return (GVal)_wattr.height; }
GVal gx_wattr_border_width(GVal d)       { return (GVal)_wattr.border_width; }
GVal gx_wattr_override_redirect(GVal d)  { return (GVal)_wattr.override_redirect; }
GVal gx_wattr_map_state(GVal d)          { return (GVal)_wattr.map_state; }

/* ── Graphics Context & Drawing ────────────────────────────────────────── */

GVal gx_create_gc(GVal win) {
    return (GVal)XCreateGC(_dpy, (Drawable)win, 0, NULL);
}

GVal gx_set_fg(GVal gc, GVal color) {
    XSetForeground(_dpy, (GC)gc, (unsigned long)color);
    return 0;
}

GVal gx_set_bg(GVal gc, GVal color) {
    XSetBackground(_dpy, (GC)gc, (unsigned long)color);
    return 0;
}

GVal gx_fill_rect(GVal win, GVal gc,
                  GVal x, GVal y, GVal w, GVal h) {
    XFillRectangle(_dpy, (Drawable)win, (GC)gc,
                   (int)x, (int)y, (unsigned)w, (unsigned)h);
    return 0;
}

GVal gx_draw_rect(GVal win, GVal gc,
                  GVal x, GVal y, GVal w, GVal h) {
    XDrawRectangle(_dpy, (Drawable)win, (GC)gc,
                   (int)x, (int)y, (unsigned)w, (unsigned)h);
    return 0;
}

GVal gx_draw_line(GVal win, GVal gc,
                  GVal x1, GVal y1, GVal x2, GVal y2) {
    XDrawLine(_dpy, (Drawable)win, (GC)gc,
              (int)x1, (int)y1, (int)x2, (int)y2);
    return 0;
}

GVal gx_clear_window(GVal win) {
    XClearWindow(_dpy, (Window)win);
    return 0;
}

/* ── Color ─────────────────────────────────────────────────────────────── */

GVal gx_alloc_color(GVal scr, GVal r, GVal g, GVal b) {
    XColor c;
    c.red   = (unsigned short)r;
    c.green = (unsigned short)g;
    c.blue  = (unsigned short)b;
    c.flags = DoRed | DoGreen | DoBlue;
    Colormap cmap = DefaultColormap(_dpy, (int)scr);
    XAllocColor(_dpy, cmap, &c);
    return (GVal)c.pixel;
}

/* ── Font & Text ───────────────────────────────────────────────────────── */

GVal gx_load_font(GVal dummy) {
    _font = XLoadQueryFont(_dpy, "-misc-fixed-medium-r-*--13-*");
    if (!_font) _font = XLoadQueryFont(_dpy, "fixed");
    if (!_font) return 0;
    return (GVal)_font->ascent;
}

GVal gx_set_font(GVal gc) {
    if (_font) XSetFont(_dpy, (GC)gc, _font->fid);
    return 0;
}

GVal gx_text_width(GVal glyph_str) {
    if (!_font) return 0;
    const char *s = _gs(glyph_str);
    int w = XTextWidth(_font, s, (int)strlen(s));
    free((void *)s);
    return (GVal)w;
}

GVal gx_draw_string(GVal win, GVal gc, GVal x, GVal y, GVal glyph_str) {
    const char *s = _gs(glyph_str);
    XDrawString(_dpy, (Drawable)win, (GC)gc, (int)x, (int)y, s, (int)strlen(s));
    free((void *)s);
    return 0;
}

/* ── Window Name ───────────────────────────────────────────────────────── */

GVal gx_store_name(GVal win, GVal glyph_str) {
    const char *s = _gs(glyph_str);
    XStoreName(_dpy, (Window)win, s);
    free((void *)s);
    return 0;
}

GVal gx_fetch_name(GVal win) {
    char *name = NULL;
    if (XFetchName(_dpy, (Window)win, &name) && name) {
        GVal s = _gstr(name);
        XFree(name);
        return s;
    }
    return _gstr("");
}

/* ── Sync & Flush ──────────────────────────────────────────────────────── */

GVal gx_flush(GVal dummy) {
    XFlush(_dpy);
    return 0;
}

GVal gx_sync(GVal discard) {
    XSync(_dpy, (Bool)discard);
    return 0;
}

GVal gx_grab_server(GVal dummy) {
    XGrabServer(_dpy);
    return 0;
}

GVal gx_ungrab_server(GVal dummy) {
    XUngrabServer(_dpy);
    return 0;
}

/* ── Utility ───────────────────────────────────────────────────────────── */

GVal gx_usleep(GVal us) {
    usleep((useconds_t)us);
    return 0;
}

GVal gx_connection_number(GVal dummy) {
    return (GVal)ConnectionNumber(_dpy);
}

/* ── WM Helpers ────────────────────────────────────────────────────────── */

/* Install a lenient X error handler that suppresses BadWindow etc. */
static int _wm_error_handler(Display *dpy, XErrorEvent *e) {
    (void)dpy;
    (void)e;
    return 0;
}

GVal gx_install_error_handler(GVal dummy) {
    XSetErrorHandler(_wm_error_handler);
    return 0;
}

/* XQueryTree -> Glyph array of child Window IDs */
GVal gx_query_tree(GVal win) {
    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;
    XQueryTree(_dpy, (Window)win, &root_ret, &parent_ret, &children, &nchildren);
    long long *hdr = (long long *)malloc(3 * sizeof(long long));
    long long *data = (long long *)malloc((nchildren ? nchildren : 1) * sizeof(long long));
    for (unsigned int i = 0; i < nchildren; i++)
        data[i] = (long long)children[i];
    hdr[0] = (long long)data;
    hdr[1] = (long long)nchildren;
    hdr[2] = (long long)nchildren;
    if (children) XFree(children);
    return (GVal)hdr;
}

/* Send WM_DELETE_WINDOW client message */
GVal gx_send_delete(GVal win, GVal wm_protocols, GVal wm_delete) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = (Window)win;
    ev.xclient.message_type = (Atom)wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)wm_delete;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(_dpy, (Window)win, False, NoEventMask, &ev);
    return 0;
}

/* Check if window supports WM_DELETE_WINDOW protocol */
GVal gx_supports_delete(GVal win, GVal wm_delete_atom) {
    Atom *protocols = NULL;
    int count = 0;
    if (!XGetWMProtocols(_dpy, (Window)win, &protocols, &count))
        return 0;
    long long found = 0;
    for (int i = 0; i < count; i++) {
        if (protocols[i] == (Atom)wm_delete_atom) { found = 1; break; }
    }
    XFree(protocols);
    return (GVal)found;
}

/* Send a synthetic ConfigureNotify to a client window */
GVal gx_send_configure_notify(GVal win, GVal x, GVal y, GVal w, GVal h, GVal border) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xconfigure.type = ConfigureNotify;
    ev.xconfigure.event = (Window)win;
    ev.xconfigure.window = (Window)win;
    ev.xconfigure.x = (int)x;
    ev.xconfigure.y = (int)y;
    ev.xconfigure.width = (int)w;
    ev.xconfigure.height = (int)h;
    ev.xconfigure.border_width = (int)border;
    ev.xconfigure.above = None;
    ev.xconfigure.override_redirect = False;
    XSendEvent(_dpy, (Window)win, False, StructureNotifyMask, &ev);
    return 0;
}
