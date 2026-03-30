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

GVal glyph_gx_open_display(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_open_display))(_0); }
GVal glyph_gx_close_display(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_close_display))(_0); }
GVal glyph_gx_default_screen(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_default_screen))(_0); }
GVal glyph_gx_root_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_root_window))(_0); }
GVal glyph_gx_black_pixel(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_black_pixel))(_0); }
GVal glyph_gx_white_pixel(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_white_pixel))(_0); }
GVal glyph_gx_display_width(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_display_width))(_0); }
GVal glyph_gx_display_height(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_display_height))(_0); }
GVal glyph_gx_create_window(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal, GVal))(gx_create_window))(_0, _1, _2, _3, _4, _5); }
GVal glyph_gx_map_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_map_window))(_0); }
GVal glyph_gx_unmap_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_unmap_window))(_0); }
GVal glyph_gx_destroy_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_destroy_window))(_0); }
GVal glyph_gx_reparent_window(GVal _0, GVal _1, GVal _2, GVal _3) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal))(gx_reparent_window))(_0, _1, _2, _3); }
GVal glyph_gx_move_window(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gx_move_window))(_0, _1, _2); }
GVal glyph_gx_resize_window(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gx_resize_window))(_0, _1, _2); }
GVal glyph_gx_move_resize_window(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal))(gx_move_resize_window))(_0, _1, _2, _3, _4); }
GVal glyph_gx_raise_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_raise_window))(_0); }
GVal glyph_gx_configure_window(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7, GVal _8) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal, GVal, GVal, GVal, GVal))(gx_configure_window))(_0, _1, _2, _3, _4, _5, _6, _7, _8); }
GVal glyph_gx_set_window_border(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gx_set_window_border))(_0, _1); }
GVal glyph_gx_set_window_border_width(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gx_set_window_border_width))(_0, _1); }
GVal glyph_gx_add_to_save_set(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_add_to_save_set))(_0); }
GVal glyph_gx_remove_from_save_set(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_remove_from_save_set))(_0); }
GVal glyph_gx_select_input(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gx_select_input))(_0, _1); }
GVal glyph_gx_next_event(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_next_event))(_0); }
GVal glyph_gx_check_event(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_check_event))(_0); }
GVal glyph_gx_pending(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_pending))(_0); }
GVal glyph_gx_event_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_window))(_0); }
GVal glyph_gx_event_x(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_x))(_0); }
GVal glyph_gx_event_y(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_y))(_0); }
GVal glyph_gx_event_x_root(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_x_root))(_0); }
GVal glyph_gx_event_y_root(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_y_root))(_0); }
GVal glyph_gx_event_button(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_button))(_0); }
GVal glyph_gx_event_state(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_state))(_0); }
GVal glyph_gx_event_subwindow(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_subwindow))(_0); }
GVal glyph_gx_event_keycode(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_keycode))(_0); }
GVal glyph_gx_event_width(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_width))(_0); }
GVal glyph_gx_event_height(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_height))(_0); }
GVal glyph_gx_event_border(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_border))(_0); }
GVal glyph_gx_event_value_mask(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_value_mask))(_0); }
GVal glyph_gx_event_above(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_above))(_0); }
GVal glyph_gx_event_detail(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_detail))(_0); }
GVal glyph_gx_event_atom(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_atom))(_0); }
GVal glyph_gx_event_cfg_x(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_cfg_x))(_0); }
GVal glyph_gx_event_cfg_y(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_cfg_y))(_0); }
GVal glyph_gx_lookup_keysym(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_lookup_keysym))(_0); }
GVal glyph_gx_keysym_to_keycode(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_keysym_to_keycode))(_0); }
GVal glyph_gx_keysym_tab(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_keysym_tab))(_0); }
GVal glyph_gx_keysym_return(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_keysym_return))(_0); }
GVal glyph_gx_keysym_escape(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_keysym_escape))(_0); }
GVal glyph_gx_keysym_f4(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_keysym_f4))(_0); }
GVal glyph_gx_keysym_q(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_keysym_q))(_0); }
GVal glyph_gx_set_input_focus(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_set_input_focus))(_0); }
GVal glyph_gx_grab_button(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gx_grab_button))(_0, _1, _2); }
GVal glyph_gx_ungrab_button(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gx_ungrab_button))(_0, _1, _2); }
GVal glyph_gx_grab_key(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gx_grab_key))(_0, _1, _2); }
GVal glyph_gx_ungrab_key(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gx_ungrab_key))(_0, _1, _2); }
GVal glyph_gx_grab_pointer(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gx_grab_pointer))(_0, _1); }
GVal glyph_gx_ungrab_pointer(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_ungrab_pointer))(_0); }
GVal glyph_gx_intern_atom(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gx_intern_atom))(_0, _1); }
GVal glyph_gx_get_window_attributes(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_get_window_attributes))(_0); }
GVal glyph_gx_wattr_x(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_wattr_x))(_0); }
GVal glyph_gx_wattr_y(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_wattr_y))(_0); }
GVal glyph_gx_wattr_width(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_wattr_width))(_0); }
GVal glyph_gx_wattr_height(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_wattr_height))(_0); }
GVal glyph_gx_wattr_border_width(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_wattr_border_width))(_0); }
GVal glyph_gx_wattr_override_redirect(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_wattr_override_redirect))(_0); }
GVal glyph_gx_wattr_map_state(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_wattr_map_state))(_0); }
GVal glyph_gx_create_gc(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_create_gc))(_0); }
GVal glyph_gx_set_fg(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gx_set_fg))(_0, _1); }
GVal glyph_gx_set_bg(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gx_set_bg))(_0, _1); }
GVal glyph_gx_fill_rect(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal, GVal))(gx_fill_rect))(_0, _1, _2, _3, _4, _5); }
GVal glyph_gx_draw_rect(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal, GVal))(gx_draw_rect))(_0, _1, _2, _3, _4, _5); }
GVal glyph_gx_draw_line(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal, GVal))(gx_draw_line))(_0, _1, _2, _3, _4, _5); }
GVal glyph_gx_clear_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_clear_window))(_0); }
GVal glyph_gx_alloc_color(GVal _0, GVal _1, GVal _2, GVal _3) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal))(gx_alloc_color))(_0, _1, _2, _3); }
GVal glyph_gx_load_font(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_load_font))(_0); }
GVal glyph_gx_set_font(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_set_font))(_0); }
GVal glyph_gx_text_width(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_text_width))(_0); }
GVal glyph_gx_draw_string(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal))(gx_draw_string))(_0, _1, _2, _3, _4); }
GVal glyph_gx_store_name(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gx_store_name))(_0, _1); }
GVal glyph_gx_fetch_name(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_fetch_name))(_0); }
GVal glyph_gx_flush(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_flush))(_0); }
GVal glyph_gx_sync(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_sync))(_0); }
GVal glyph_gx_grab_server(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_grab_server))(_0); }
GVal glyph_gx_ungrab_server(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_ungrab_server))(_0); }
GVal glyph_gx_usleep(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_usleep))(_0); }
GVal glyph_gx_connection_number(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_connection_number))(_0); }
GVal glyph_gx_install_error_handler(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_install_error_handler))(_0); }
GVal glyph_gx_query_tree(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_query_tree))(_0); }
GVal glyph_gx_send_delete(GVal _0, GVal _1, GVal _2) { return (GVal)((GVal(*)(GVal, GVal, GVal))(gx_send_delete))(_0, _1, _2); }
GVal glyph_gx_supports_delete(GVal _0, GVal _1) { return (GVal)((GVal(*)(GVal, GVal))(gx_supports_delete))(_0, _1); }
GVal glyph_gx_send_configure_notify(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5) { return (GVal)((GVal(*)(GVal, GVal, GVal, GVal, GVal, GVal))(gx_send_configure_notify))(_0, _1, _2, _3, _4, _5); }
GVal glyph_gx_event_map_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_map_window))(_0); }
GVal glyph_gx_event_cfg_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_cfg_window))(_0); }
GVal glyph_gx_event_destroy_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_destroy_window))(_0); }
GVal glyph_gx_event_unmap_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_unmap_window))(_0); }
GVal glyph_gx_event_property_window(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_event_property_window))(_0); }
GVal glyph_gx_set_default_cursor(GVal _0) { return (GVal)((GVal(*)(GVal))(gx_set_default_cursor))(_0); }


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

GVal mod_super();
GVal mod_shift();
GVal mod_control();
GVal mod_alt();
GVal mask_substructure_redirect();
GVal mask_substructure_notify();
GVal mask_structure_notify();
GVal mask_property_change();
GVal mask_pointer_motion();
GVal mask_key_release();
GVal mask_key_press();
GVal mask_focus_change();
GVal mask_exposure();
GVal mask_enter_window();
GVal mask_button_release();
GVal mask_button_press();
GVal evt_unmap_notify();
GVal evt_reparent_notify();
GVal evt_property_notify();
GVal evt_motion_notify();
GVal evt_map_request();
GVal evt_map_notify();
GVal evt_key_release();
GVal evt_key_press();
GVal evt_focus_out();
GVal evt_focus_in();
GVal evt_expose();
GVal evt_enter_notify();
GVal evt_destroy_notify();
GVal evt_create_notify();
GVal evt_configure_request();
GVal evt_configure_notify();
GVal evt_client_message();
GVal evt_button_release();
GVal evt_button_press();
GVal cw_y();
GVal cw_x();
GVal cw_width();
GVal cw_stack_mode();
GVal cw_sibling();
GVal cw_height();
GVal cw_border_width();
GVal step_row(GVal _0, GVal _1, GVal _2, GVal _3);
GVal step_grid(GVal _0, GVal _1, GVal _2);
GVal step_cell(GVal _0, GVal _1, GVal _2, GVal _3);
GVal s3(GVal _0, GVal _1, GVal _2);
GVal s2(GVal _0, GVal _1);
GVal redraw(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7);
GVal randomize_grid(GVal _0, GVal _1, GVal _2);
GVal make_grid();
GVal glyph_main();
GVal lcg_next(GVal _0);
GVal key_action(GVal _0);
GVal handle_action(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7, GVal _8, GVal _9, GVal _10);
GVal grid_set(GVal _0, GVal _1, GVal _2, GVal _3);
GVal grid_get(GVal _0, GVal _1, GVal _2);
GVal fill_grid(GVal _0, GVal _1);
GVal event_loop(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7, GVal _8, GVal _9);
GVal draw_row(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7);
GVal draw_grid(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6);
GVal draw_cell(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7);
GVal do_step(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7);
GVal count_neighbors(GVal _0, GVal _1, GVal _2);
GVal copy_grid(GVal _0, GVal _1, GVal _2);
GVal clear_grid(GVal _0, GVal _1);
GVal clear_grid__any(GVal _0, GVal _1);
GVal copy_grid__any_any(GVal _0, GVal _1, GVal _2);
GVal clear_grid__arr_int(GVal _0, GVal _1);
GVal copy_grid__int_arr_int(GVal _0, GVal _1, GVal _2);
GVal redraw__any_any_any_any_any_int_int_any(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7);
GVal draw_cell__any_any_any_any_any_int_any(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7);
GVal key_action__any(GVal _0);
GVal handle_action__int_any_any_any_any_any(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7, GVal _8, GVal _9, GVal _10);
GVal grid_set__arr_int_int_any(GVal _0, GVal _1, GVal _2, GVal _3);
GVal redraw__any_any_any_any_any_int_int_int(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7);
GVal s3__str_any_str_any(GVal _0, GVal _1, GVal _2);
GVal s2__any_any_any(GVal _0, GVal _1);
GVal grid_set__any_int_any(GVal _0, GVal _1, GVal _2, GVal _3);
GVal step_cell__arr_int_any(GVal _0, GVal _1, GVal _2, GVal _3);

GVal mod_super() {
  _glyph_current_fn = "mod_super";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mod_super";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)64;
}

GVal mod_shift() {
  _glyph_current_fn = "mod_shift";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mod_shift";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)1;
}

GVal mod_control() {
  _glyph_current_fn = "mod_control";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mod_control";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)4;
}

GVal mod_alt() {
  _glyph_current_fn = "mod_alt";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mod_alt";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)8;
}

GVal mask_substructure_redirect() {
  _glyph_current_fn = "mask_substructure_redirect";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_substructure_redirect";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)1048576;
}

GVal mask_substructure_notify() {
  _glyph_current_fn = "mask_substructure_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_substructure_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)524288;
}

GVal mask_structure_notify() {
  _glyph_current_fn = "mask_structure_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_structure_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)131072;
}

GVal mask_property_change() {
  _glyph_current_fn = "mask_property_change";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_property_change";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)4194304;
}

GVal mask_pointer_motion() {
  _glyph_current_fn = "mask_pointer_motion";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_pointer_motion";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)64;
}

GVal mask_key_release() {
  _glyph_current_fn = "mask_key_release";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_key_release";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)2;
}

GVal mask_key_press() {
  _glyph_current_fn = "mask_key_press";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_key_press";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)1;
}

GVal mask_focus_change() {
  _glyph_current_fn = "mask_focus_change";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_focus_change";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)2097152;
}

GVal mask_exposure() {
  _glyph_current_fn = "mask_exposure";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_exposure";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)32768;
}

GVal mask_enter_window() {
  _glyph_current_fn = "mask_enter_window";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_enter_window";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)16;
}

GVal mask_button_release() {
  _glyph_current_fn = "mask_button_release";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_button_release";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)8;
}

GVal mask_button_press() {
  _glyph_current_fn = "mask_button_press";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "mask_button_press";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)4;
}

GVal evt_unmap_notify() {
  _glyph_current_fn = "evt_unmap_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_unmap_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)18;
}

GVal evt_reparent_notify() {
  _glyph_current_fn = "evt_reparent_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_reparent_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)21;
}

GVal evt_property_notify() {
  _glyph_current_fn = "evt_property_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_property_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)28;
}

GVal evt_motion_notify() {
  _glyph_current_fn = "evt_motion_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_motion_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)6;
}

GVal evt_map_request() {
  _glyph_current_fn = "evt_map_request";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_map_request";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)20;
}

GVal evt_map_notify() {
  _glyph_current_fn = "evt_map_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_map_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)19;
}

GVal evt_key_release() {
  _glyph_current_fn = "evt_key_release";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_key_release";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)3;
}

GVal evt_key_press() {
  _glyph_current_fn = "evt_key_press";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_key_press";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)2;
}

GVal evt_focus_out() {
  _glyph_current_fn = "evt_focus_out";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_focus_out";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)10;
}

GVal evt_focus_in() {
  _glyph_current_fn = "evt_focus_in";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_focus_in";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)9;
}

GVal evt_expose() {
  _glyph_current_fn = "evt_expose";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_expose";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)12;
}

GVal evt_enter_notify() {
  _glyph_current_fn = "evt_enter_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_enter_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)7;
}

GVal evt_destroy_notify() {
  _glyph_current_fn = "evt_destroy_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_destroy_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)17;
}

GVal evt_create_notify() {
  _glyph_current_fn = "evt_create_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_create_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)16;
}

GVal evt_configure_request() {
  _glyph_current_fn = "evt_configure_request";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_configure_request";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)23;
}

GVal evt_configure_notify() {
  _glyph_current_fn = "evt_configure_notify";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_configure_notify";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)22;
}

GVal evt_client_message() {
  _glyph_current_fn = "evt_client_message";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_client_message";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)33;
}

GVal evt_button_release() {
  _glyph_current_fn = "evt_button_release";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_button_release";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)5;
}

GVal evt_button_press() {
  _glyph_current_fn = "evt_button_press";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "evt_button_press";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)4;
}

GVal cw_y() {
  _glyph_current_fn = "cw_y";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cw_y";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)2;
}

GVal cw_x() {
  _glyph_current_fn = "cw_x";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cw_x";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)1;
}

GVal cw_width() {
  _glyph_current_fn = "cw_width";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cw_width";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)4;
}

GVal cw_stack_mode() {
  _glyph_current_fn = "cw_stack_mode";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cw_stack_mode";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)64;
}

GVal cw_sibling() {
  _glyph_current_fn = "cw_sibling";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cw_sibling";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)32;
}

GVal cw_height() {
  _glyph_current_fn = "cw_height";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cw_height";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)8;
}

GVal cw_border_width() {
  _glyph_current_fn = "cw_border_width";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "cw_border_width";
  _glyph_call_depth++;
#endif
bb_0:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return (GVal)16;
}

GVal step_row(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "step_row";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "step_row";
  _glyph_call_depth++;
#endif
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
bb_0:
  _4 = _2 < (GVal)64;
  _6 = _4 == 1;
  if (_6) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _5;
bb_2:
  _7 = step_cell__arr_int_any(_0, _1, _2, _3);
  _8 = _2 + (GVal)1;
  _10 = _0;
  _11 = _1;
  _12 = _8;
  _13 = _3;
  _0 = _10;
  _1 = _11;
  _2 = _12;
  _3 = _13;
  goto bb_0;
bb_3:
  _5 = (GVal)0;
  goto bb_1;
}

GVal step_grid(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "step_grid";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "step_grid";
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
  _3 = _2 < (GVal)48;
  _5 = _3 == 1;
  if (_5) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
bb_2:
  _6 = step_row(_0, _1, (GVal)0, _2);
  _7 = _2 + (GVal)1;
  _9 = _0;
  _10 = _1;
  _11 = _7;
  _0 = _9;
  _1 = _10;
  _2 = _11;
  goto bb_0;
bb_3:
  _4 = (GVal)0;
  goto bb_1;
}

GVal step_cell(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "step_cell";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "step_cell";
  _glyph_call_depth++;
#endif
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
bb_0:
  _4 = grid_get(_0, _2, _3);
  _5 = _4;
  _6 = count_neighbors(_0, _2, _3);
  _7 = _6;
  _8 = _5 == (GVal)1;
  _10 = _8 == 1;
  if (_10) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _9;
bb_2:
  _11 = _7 == (GVal)2;
  _12 = _7 == (GVal)3;
  _13 = _11 || _12;
  _15 = _13 == 1;
  if (_15) goto bb_5; else goto bb_6;
bb_3:
  _18 = _7 == (GVal)3;
  _20 = _18 == 1;
  if (_20) goto bb_8; else goto bb_9;
bb_4:
  _9 = _14;
  goto bb_1;
bb_5:
  _16 = grid_set__any_int_any(_1, _2, _3, (GVal)1);
  _14 = _16;
  goto bb_4;
bb_6:
  _17 = grid_set__any_int_any(_1, _2, _3, (GVal)0);
  _14 = _17;
  goto bb_4;
bb_7:
  _9 = _19;
  goto bb_1;
bb_8:
  _21 = grid_set__any_int_any(_1, _2, _3, (GVal)1);
  _19 = _21;
  goto bb_7;
bb_9:
  _22 = grid_set__any_int_any(_1, _2, _3, (GVal)0);
  _19 = _22;
  goto bb_7;
}

GVal s3(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "s3";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "s3";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
bb_0:
  _3 = s2__any_any_any(_0, _1);
  _4 = s2__any_any_any(_3, _2);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
}

GVal s2(GVal _0, GVal _1) {
  _glyph_current_fn = "s2";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "s2";
  _glyph_call_depth++;
#endif
  GVal _2 = 0;
bb_0:
  _2 = glyph_str_concat(_0, _1);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _2;
}

GVal redraw(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7) {
  _glyph_current_fn = "redraw";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "redraw";
  _glyph_call_depth++;
#endif
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
bb_0:
  _8 = draw_grid(_0, _1, _2, _3, _4, _5, (GVal)0);
  _9 = _7 == (GVal)1;
  _11 = _9 == 1;
  if (_11) goto bb_2; else goto bb_3;
bb_1:
  _18 = glyph_gx_flush((GVal)0);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _18;
bb_2:
  _12 = glyph_int_to_str(_6);
  _13 = s3__str_any_str_any((GVal)glyph_cstr_to_str((GVal)"Life gen "), _12, (GVal)glyph_cstr_to_str((GVal)" running"));
  _14 = glyph_gx_store_name(_1, _13);
  _10 = _14;
  goto bb_1;
bb_3:
  _15 = glyph_int_to_str(_6);
  _16 = s3__str_any_str_any((GVal)glyph_cstr_to_str((GVal)"Life gen "), _15, (GVal)glyph_cstr_to_str((GVal)" paused"));
  _17 = glyph_gx_store_name(_1, _16);
  _10 = _17;
  goto bb_1;
}

GVal randomize_grid(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "randomize_grid";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "randomize_grid";
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
bb_0:
  _3 = _2 < (GVal)3072;
  _5 = _3 == 1;
  if (_5) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
bb_2:
  _6 = lcg_next(_1);
  _7 = _6;
  _8 = _7 / (GVal)65536;
  _9 = _8 % (GVal)4;
  _10 = _9;
  _11 = _10 == (GVal)0;
  _13 = _11 == 1;
  if (_13) goto bb_5; else goto bb_6;
bb_3:
  _4 = _1;
  goto bb_1;
bb_4:
  _16 = _2 + (GVal)1;
  _18 = _0;
  _19 = _7;
  _20 = _16;
  _0 = _18;
  _1 = _19;
  _2 = _20;
  goto bb_0;
bb_5:
  _14 = glyph_array_set(_0, _2, (GVal)1);
  _12 = _14;
  goto bb_4;
bb_6:
  _15 = glyph_array_set(_0, _2, (GVal)0);
  _12 = _15;
  goto bb_4;
}

GVal make_grid() {
  _glyph_current_fn = "make_grid";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "make_grid";
  _glyph_call_depth++;
#endif
  GVal _0 = 0;
  GVal _1 = 0;
  GVal _2 = 0;
bb_0:
  _0 = glyph_array_new((GVal)0);
  _1 = _0;
  _2 = fill_grid(_1, (GVal)0);
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
  _0 = glyph_gx_open_display((GVal)0);
  _1 = _0;
  _2 = glyph_gx_default_screen((GVal)0);
  _3 = _2;
  _4 = glyph_gx_root_window(_3);
  _5 = _4;
  _6 = glyph_gx_black_pixel(_3);
  _7 = _6;
  _8 = glyph_gx_create_window(_5, (GVal)0, (GVal)0, (GVal)768, (GVal)576, _7);
  _9 = _8;
  _10 = glyph_gx_create_gc(_9);
  _11 = _10;
  _12 = glyph_gx_select_input(_9, (GVal)163845);
  _13 = glyph_gx_map_window(_9);
  _14 = glyph_gx_alloc_color(_3, (GVal)0, (GVal)65535, (GVal)0);
  _15 = _14;
  _16 = glyph_gx_alloc_color(_3, (GVal)13107, (GVal)13107, (GVal)13107);
  _17 = _16;
  _18 = make_grid();
  _19 = _18;
  _20 = make_grid();
  _21 = _20;
  _22 = randomize_grid(_19, (GVal)42, (GVal)0);
  _23 = _22;
  _24 = redraw__any_any_any_any_any_int_int_any(_1, _9, _11, _19, _15, _17, (GVal)0, (GVal)0);
  _25 = event_loop(_1, _9, _11, _19, _21, _15, _17, (GVal)0, (GVal)0, _23);
  _26 = glyph_gx_close_display((GVal)0);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _26;
}

GVal lcg_next(GVal _0) {
  _glyph_current_fn = "lcg_next";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "lcg_next";
  _glyph_call_depth++;
#endif
  GVal _1 = 0;
  GVal _2 = 0;
  GVal _3 = 0;
bb_0:
  _1 = _0 * (GVal)1103515245;
  _2 = _1 + (GVal)12345;
  _3 = _2 % (GVal)2147483648;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _3;
}

GVal key_action(GVal _0) {
  _glyph_current_fn = "key_action";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "key_action";
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
bb_0:
  _1 = _0 == (GVal)113;
  _3 = _1 == 1;
  if (_3) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _2;
bb_2:
  _2 = (GVal)5;
  goto bb_1;
bb_3:
  _4 = _0 == (GVal)32;
  _6 = _4 == 1;
  if (_6) goto bb_5; else goto bb_6;
bb_4:
  _2 = _5;
  goto bb_1;
bb_5:
  _5 = (GVal)1;
  goto bb_4;
bb_6:
  _7 = _0 == (GVal)99;
  _9 = _7 == 1;
  if (_9) goto bb_8; else goto bb_9;
bb_7:
  _5 = _8;
  goto bb_4;
bb_8:
  _8 = (GVal)2;
  goto bb_7;
bb_9:
  _10 = _0 == (GVal)114;
  _12 = _10 == 1;
  if (_12) goto bb_11; else goto bb_12;
bb_10:
  _8 = _11;
  goto bb_7;
bb_11:
  _11 = (GVal)3;
  goto bb_10;
bb_12:
  _13 = _0 == (GVal)110;
  _15 = _13 == 1;
  if (_15) goto bb_14; else goto bb_15;
bb_13:
  _11 = _14;
  goto bb_10;
bb_14:
  _14 = (GVal)4;
  goto bb_13;
bb_15:
  _14 = (GVal)0;
  goto bb_13;
}

GVal handle_action(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7, GVal _8, GVal _9, GVal _10) {
  _glyph_current_fn = "handle_action";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "handle_action";
  _glyph_call_depth++;
#endif
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
bb_0:
  _11 = _0 == (GVal)5;
  _13 = _11 == 1;
  if (_13) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _12;
bb_2:
  _12 = (GVal)0;
  goto bb_1;
bb_3:
  _14 = _0 == (GVal)1;
  _16 = _14 == 1;
  if (_16) goto bb_5; else goto bb_6;
bb_4:
  _12 = _15;
  goto bb_1;
bb_5:
  _17 = _8 == (GVal)1;
  _19 = _17 == 1;
  if (_19) goto bb_8; else goto bb_9;
bb_6:
  _23 = _0 == (GVal)2;
  _25 = _23 == 1;
  if (_25) goto bb_11; else goto bb_12;
bb_7:
  _20 = _18;
  _21 = redraw__any_any_any_any_any_int_int_any(_1, _2, _3, _4, _6, _7, _9, _20);
  _22 = event_loop(_1, _2, _3, _4, _5, _6, _7, _20, _9, _10);
  _15 = _22;
  goto bb_4;
bb_8:
  _18 = (GVal)0;
  goto bb_7;
bb_9:
  _18 = (GVal)1;
  goto bb_7;
bb_10:
  _15 = _24;
  goto bb_4;
bb_11:
  _26 = clear_grid__arr_int(_4, (GVal)0);
  _27 = redraw__any_any_any_any_any_int_int_any(_1, _2, _3, _4, _6, _7, (GVal)0, (GVal)0);
  _28 = event_loop(_1, _2, _3, _4, _5, _6, _7, (GVal)0, (GVal)0, _10);
  _24 = _28;
  goto bb_10;
bb_12:
  _29 = _0 == (GVal)3;
  _31 = _29 == 1;
  if (_31) goto bb_14; else goto bb_15;
bb_13:
  _24 = _30;
  goto bb_10;
bb_14:
  _32 = randomize_grid(_4, _10, (GVal)0);
  _33 = _32;
  _34 = redraw__any_any_any_any_any_int_int_any(_1, _2, _3, _4, _6, _7, (GVal)0, (GVal)0);
  _35 = event_loop(_1, _2, _3, _4, _5, _6, _7, (GVal)0, (GVal)0, _33);
  _30 = _35;
  goto bb_13;
bb_15:
  _36 = _0 == (GVal)4;
  _38 = _36 == 1;
  if (_38) goto bb_17; else goto bb_18;
bb_16:
  _30 = _37;
  goto bb_13;
bb_17:
  _39 = do_step(_1, _2, _3, _4, _5, _6, _7, _9);
  _40 = _39;
  _41 = event_loop(_1, _2, _3, _4, _5, _6, _7, (GVal)0, _40, _10);
  _37 = _41;
  goto bb_16;
bb_18:
  _42 = event_loop(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10);
  _37 = _42;
  goto bb_16;
}

GVal grid_set(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "grid_set";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "grid_set";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
bb_0:
  _4 = _2 * (GVal)64;
  _5 = _4 + _1;
  _6 = glyph_array_set(_0, _5, _3);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _6;
}

GVal grid_get(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "grid_get";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "grid_get";
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
bb_0:
  _3 = _1 >= (GVal)0;
  _4 = _1 < (GVal)64;
  _5 = _3 && _4;
  _6 = _2 >= (GVal)0;
  _7 = _5 && _6;
  _8 = _2 < (GVal)48;
  _9 = _7 && _8;
  _11 = _9 == 1;
  if (_11) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _10;
bb_2:
  _12 = _2 * (GVal)64;
  _13 = _12 + _1;
#ifdef GLYPH_DEBUG
  _glyph_null_check((GVal)_0, "array index");
#endif
  { GVal* __hdr = (GVal*)_0; GVal __idx = _13; glyph_array_bounds_check(__idx, __hdr[1]); _14 = ((GVal*)__hdr[0])[__idx]; }
  _10 = _14;
  goto bb_1;
bb_3:
  _10 = (GVal)0;
  goto bb_1;
}

GVal fill_grid(GVal _0, GVal _1) {
  _glyph_current_fn = "fill_grid";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "fill_grid";
  _glyph_call_depth++;
#endif
  GVal _2 = 0;
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
bb_0:
  _2 = _1 < (GVal)3072;
  _4 = _2 == 1;
  if (_4) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _3;
bb_2:
  _5 = glyph_array_push(_0, (GVal)0);
  _6 = _1 + (GVal)1;
  _8 = _0;
  _9 = _6;
  _0 = _8;
  _1 = _9;
  goto bb_0;
bb_3:
  _3 = _0;
  goto bb_1;
}

GVal event_loop(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7, GVal _8, GVal _9) {
  _glyph_current_fn = "event_loop";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "event_loop";
  _glyph_call_depth++;
#endif
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
  _10 = glyph_gx_check_event((GVal)0);
  _11 = _10;
  _12 = _11 == (GVal)2;
  _14 = _12 == 1;
  if (_14) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _13;
bb_2:
  _15 = glyph_gx_event_keycode((GVal)0);
  _16 = _15;
  _17 = glyph_gx_lookup_keysym(_16);
  _18 = _17;
  _19 = key_action__any(_18);
  _20 = _19;
  _21 = handle_action__int_any_any_any_any_any(_20, _0, _1, _2, _3, _4, _5, _6, _7, _8, _9);
  _13 = _21;
  goto bb_1;
bb_3:
  _22 = _11 == (GVal)4;
  _24 = _22 == 1;
  if (_24) goto bb_5; else goto bb_6;
bb_4:
  _13 = _23;
  goto bb_1;
bb_5:
  _25 = glyph_gx_event_x((GVal)0);
  _26 = _25;
  _27 = glyph_gx_event_y((GVal)0);
  _28 = _27;
  _29 = _26 / (GVal)12;
  _30 = _29;
  _31 = _28 / (GVal)12;
  _32 = _31;
  _33 = _30 >= (GVal)0;
  _34 = _30 < (GVal)64;
  _35 = _33 && _34;
  _36 = _32 >= (GVal)0;
  _37 = _35 && _36;
  _38 = _32 < (GVal)48;
  _39 = _37 && _38;
  _41 = _39 == 1;
  if (_41) goto bb_8; else goto bb_9;
bb_6:
  _51 = _11 == (GVal)12;
  _53 = _51 == 1;
  if (_53) goto bb_14; else goto bb_15;
bb_7:
  _50 = event_loop(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9);
  _23 = _50;
  goto bb_4;
bb_8:
  _42 = grid_get(_3, _30, _32);
  _43 = _42;
  _44 = _43 == (GVal)0;
  _46 = _44 == 1;
  if (_46) goto bb_11; else goto bb_12;
bb_9:
  _40 = (GVal)0;
  goto bb_7;
bb_10:
  _49 = redraw__any_any_any_any_any_int_int_int(_0, _1, _2, _3, _5, _6, _8, _7);
  _40 = _49;
  goto bb_7;
bb_11:
  _47 = grid_set__arr_int_int_any(_3, _30, _32, (GVal)1);
  _45 = _47;
  goto bb_10;
bb_12:
  _48 = grid_set__arr_int_int_any(_3, _30, _32, (GVal)0);
  _45 = _48;
  goto bb_10;
bb_13:
  _23 = _52;
  goto bb_4;
bb_14:
  _54 = redraw__any_any_any_any_any_int_int_any(_0, _1, _2, _3, _5, _6, _8, _7);
  _55 = event_loop(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9);
  _52 = _55;
  goto bb_13;
bb_15:
  _56 = _7 == (GVal)1;
  _58 = _56 == 1;
  if (_58) goto bb_17; else goto bb_18;
bb_16:
  _52 = _57;
  goto bb_13;
bb_17:
  _59 = do_step(_0, _1, _2, _3, _4, _5, _6, _8);
  _60 = _59;
  _61 = glyph_gx_usleep((GVal)100000);
  _62 = event_loop(_0, _1, _2, _3, _4, _5, _6, (GVal)1, _60, _9);
  _57 = _62;
  goto bb_16;
bb_18:
  _63 = glyph_gx_usleep((GVal)10000);
  _64 = event_loop(_0, _1, _2, _3, _4, _5, _6, (GVal)0, _8, _9);
  _57 = _64;
  goto bb_16;
}

GVal draw_row(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7) {
  _glyph_current_fn = "draw_row";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "draw_row";
  _glyph_call_depth++;
#endif
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
bb_0:
  _8 = _6 < (GVal)64;
  _10 = _8 == 1;
  if (_10) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _9;
bb_2:
  _11 = _7 * (GVal)64;
  _12 = _11 + _6;
#ifdef GLYPH_DEBUG
  _glyph_null_check((GVal)_3, "array index");
#endif
  { GVal* __hdr = (GVal*)_3; GVal __idx = _12; glyph_array_bounds_check(__idx, __hdr[1]); _13 = ((GVal*)__hdr[0])[__idx]; }
  _14 = draw_cell__any_any_any_any_any_int_any(_0, _1, _2, _4, _5, _6, _7, _13);
  _15 = _6 + (GVal)1;
  _17 = _0;
  _18 = _1;
  _19 = _2;
  _20 = _3;
  _21 = _4;
  _22 = _5;
  _23 = _15;
  _24 = _7;
  _0 = _17;
  _1 = _18;
  _2 = _19;
  _3 = _20;
  _4 = _21;
  _5 = _22;
  _6 = _23;
  _7 = _24;
  goto bb_0;
bb_3:
  _9 = (GVal)0;
  goto bb_1;
}

GVal draw_grid(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6) {
  _glyph_current_fn = "draw_grid";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "draw_grid";
  _glyph_call_depth++;
#endif
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
bb_0:
  _7 = _6 < (GVal)48;
  _9 = _7 == 1;
  if (_9) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _8;
bb_2:
  _10 = draw_row(_0, _1, _2, _3, _4, _5, (GVal)0, _6);
  _11 = _6 + (GVal)1;
  _13 = _0;
  _14 = _1;
  _15 = _2;
  _16 = _3;
  _17 = _4;
  _18 = _5;
  _19 = _11;
  _0 = _13;
  _1 = _14;
  _2 = _15;
  _3 = _16;
  _4 = _17;
  _5 = _18;
  _6 = _19;
  goto bb_0;
bb_3:
  _8 = (GVal)0;
  goto bb_1;
}

GVal draw_cell(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7) {
  _glyph_current_fn = "draw_cell";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "draw_cell";
  _glyph_call_depth++;
#endif
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
bb_0:
  _8 = _5 * (GVal)12;
  _9 = _8;
  _10 = _6 * (GVal)12;
  _11 = _10;
  _12 = _7 == (GVal)1;
  _14 = _12 == 1;
  if (_14) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _13;
bb_2:
  _15 = glyph_gx_set_fg(_2, _3);
  _16 = glyph_gx_fill_rect(_1, _2, _9, _11, (GVal)11, (GVal)11);
  _13 = _16;
  goto bb_1;
bb_3:
  _17 = glyph_gx_set_fg(_2, _4);
  _18 = glyph_gx_fill_rect(_1, _2, _9, _11, (GVal)11, (GVal)11);
  _13 = _18;
  goto bb_1;
}

GVal do_step(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7) {
  _glyph_current_fn = "do_step";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "do_step";
  _glyph_call_depth++;
#endif
  GVal _8 = 0;
  GVal _9 = 0;
  GVal _10 = 0;
  GVal _11 = 0;
  GVal _12 = 0;
  GVal _13 = 0;
bb_0:
  _8 = clear_grid__arr_int(_4, (GVal)0);
  _9 = step_grid(_3, _4, (GVal)0);
  _10 = copy_grid__int_arr_int(_4, _3, (GVal)0);
  _11 = _7 + (GVal)1;
  _12 = redraw__any_any_any_any_any_int_int_any(_0, _1, _2, _3, _5, _6, _11, (GVal)1);
  _13 = _7 + (GVal)1;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _13;
}

GVal count_neighbors(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "count_neighbors";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "count_neighbors";
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
bb_0:
  _3 = _1 - (GVal)1;
  _4 = _2 - (GVal)1;
  _5 = grid_get(_0, _3, _4);
  _6 = _2 - (GVal)1;
  _7 = grid_get(_0, _1, _6);
  _8 = _5 + _7;
  _9 = _1 + (GVal)1;
  _10 = _2 - (GVal)1;
  _11 = grid_get(_0, _9, _10);
  _12 = _8 + _11;
  _13 = _1 - (GVal)1;
  _14 = grid_get(_0, _13, _2);
  _15 = _12 + _14;
  _16 = _1 + (GVal)1;
  _17 = grid_get(_0, _16, _2);
  _18 = _15 + _17;
  _19 = _1 - (GVal)1;
  _20 = _2 + (GVal)1;
  _21 = grid_get(_0, _19, _20);
  _22 = _18 + _21;
  _23 = _2 + (GVal)1;
  _24 = grid_get(_0, _1, _23);
  _25 = _22 + _24;
  _26 = _1 + (GVal)1;
  _27 = _2 + (GVal)1;
  _28 = grid_get(_0, _26, _27);
  _29 = _25 + _28;
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _29;
}

GVal copy_grid(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "copy_grid";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "copy_grid";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
bb_0:
  _3 = _2 < (GVal)3072;
  _5 = _3 == 1;
  if (_5) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
bb_2:
#ifdef GLYPH_DEBUG
  _glyph_null_check((GVal)_0, "array index");
#endif
  { GVal* __hdr = (GVal*)_0; GVal __idx = _2; glyph_array_bounds_check(__idx, __hdr[1]); _6 = ((GVal*)__hdr[0])[__idx]; }
  _7 = glyph_array_set(_1, _2, _6);
  _8 = _2 + (GVal)1;
  _9 = copy_grid__any_any(_0, _1, _8);
  _4 = _9;
  goto bb_1;
bb_3:
  _4 = (GVal)0;
  goto bb_1;
}

GVal clear_grid(GVal _0, GVal _1) {
  _glyph_current_fn = "clear_grid";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "clear_grid";
  _glyph_call_depth++;
#endif
  GVal _2 = 0;
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
bb_0:
  _2 = _1 < (GVal)3072;
  _4 = _2 == 1;
  if (_4) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _3;
bb_2:
  _5 = glyph_array_set(_0, _1, (GVal)0);
  _6 = _1 + (GVal)1;
  _7 = clear_grid__any(_0, _6);
  _3 = _7;
  goto bb_1;
bb_3:
  _3 = (GVal)0;
  goto bb_1;
}

GVal clear_grid__any(GVal _0, GVal _1) {
  _glyph_current_fn = "clear_grid__any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "clear_grid__any";
  _glyph_call_depth++;
#endif
  GVal _2 = 0;
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
bb_0:
  _2 = _1 < (GVal)3072;
  _4 = _2 == 1;
  if (_4) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _3;
bb_2:
  _5 = glyph_array_set(_0, _1, (GVal)0);
  _6 = _1 + (GVal)1;
  _7 = clear_grid(_0, _6);
  _3 = _7;
  goto bb_1;
bb_3:
  _3 = (GVal)0;
  goto bb_1;
}

GVal copy_grid__any_any(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "copy_grid__any_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "copy_grid__any_any";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
bb_0:
  _3 = _2 < (GVal)3072;
  _5 = _3 == 1;
  if (_5) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
bb_2:
#ifdef GLYPH_DEBUG
  _glyph_null_check((GVal)_0, "array index");
#endif
  { GVal* __hdr = (GVal*)_0; GVal __idx = _2; glyph_array_bounds_check(__idx, __hdr[1]); _6 = ((GVal*)__hdr[0])[__idx]; }
  _7 = glyph_array_set(_1, _2, _6);
  _8 = _2 + (GVal)1;
  _9 = copy_grid(_0, _1, _8);
  _4 = _9;
  goto bb_1;
bb_3:
  _4 = (GVal)0;
  goto bb_1;
}

GVal clear_grid__arr_int(GVal _0, GVal _1) {
  _glyph_current_fn = "clear_grid__arr_int";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "clear_grid__arr_int";
  _glyph_call_depth++;
#endif
  GVal _2 = 0;
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
bb_0:
  _2 = _1 < (GVal)3072;
  _4 = _2 == 1;
  if (_4) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _3;
bb_2:
  _5 = glyph_array_set(_0, _1, (GVal)0);
  _6 = _1 + (GVal)1;
  _7 = clear_grid(_0, _6);
  _3 = _7;
  goto bb_1;
bb_3:
  _3 = (GVal)0;
  goto bb_1;
}

GVal copy_grid__int_arr_int(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "copy_grid__int_arr_int";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "copy_grid__int_arr_int";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
  GVal _7 = 0;
  GVal _8 = 0;
  GVal _9 = 0;
bb_0:
  _3 = _2 < (GVal)3072;
  _5 = _3 == 1;
  if (_5) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
bb_2:
#ifdef GLYPH_DEBUG
  _glyph_null_check((GVal)_0, "array index");
#endif
  { GVal* __hdr = (GVal*)_0; GVal __idx = _2; glyph_array_bounds_check(__idx, __hdr[1]); _6 = ((GVal*)__hdr[0])[__idx]; }
  _7 = glyph_array_set(_1, _2, _6);
  _8 = _2 + (GVal)1;
  _9 = copy_grid(_0, _1, _8);
  _4 = _9;
  goto bb_1;
bb_3:
  _4 = (GVal)0;
  goto bb_1;
}

GVal redraw__any_any_any_any_any_int_int_any(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7) {
  _glyph_current_fn = "redraw__any_any_any_any_any_int_int_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "redraw__any_any_any_any_any_int_int_any";
  _glyph_call_depth++;
#endif
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
bb_0:
  _8 = draw_grid(_0, _1, _2, _3, _4, _5, (GVal)0);
  _9 = _7 == (GVal)1;
  _11 = _9 == 1;
  if (_11) goto bb_2; else goto bb_3;
bb_1:
  _18 = glyph_gx_flush((GVal)0);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _18;
bb_2:
  _12 = glyph_int_to_str(_6);
  _13 = s3((GVal)glyph_cstr_to_str((GVal)"Life gen "), _12, (GVal)glyph_cstr_to_str((GVal)" running"));
  _14 = glyph_gx_store_name(_1, _13);
  _10 = _14;
  goto bb_1;
bb_3:
  _15 = glyph_int_to_str(_6);
  _16 = s3((GVal)glyph_cstr_to_str((GVal)"Life gen "), _15, (GVal)glyph_cstr_to_str((GVal)" paused"));
  _17 = glyph_gx_store_name(_1, _16);
  _10 = _17;
  goto bb_1;
}

GVal draw_cell__any_any_any_any_any_int_any(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7) {
  _glyph_current_fn = "draw_cell__any_any_any_any_any_int_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "draw_cell__any_any_any_any_any_int_any";
  _glyph_call_depth++;
#endif
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
bb_0:
  _8 = _5 * (GVal)12;
  _9 = _8;
  _10 = _6 * (GVal)12;
  _11 = _10;
  _12 = _7 == (GVal)1;
  _14 = _12 == 1;
  if (_14) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _13;
bb_2:
  _15 = glyph_gx_set_fg(_2, _3);
  _16 = glyph_gx_fill_rect(_1, _2, _9, _11, (GVal)11, (GVal)11);
  _13 = _16;
  goto bb_1;
bb_3:
  _17 = glyph_gx_set_fg(_2, _4);
  _18 = glyph_gx_fill_rect(_1, _2, _9, _11, (GVal)11, (GVal)11);
  _13 = _18;
  goto bb_1;
}

GVal key_action__any(GVal _0) {
  _glyph_current_fn = "key_action__any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "key_action__any";
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
bb_0:
  _1 = _0 == (GVal)113;
  _3 = _1 == 1;
  if (_3) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _2;
bb_2:
  _2 = (GVal)5;
  goto bb_1;
bb_3:
  _4 = _0 == (GVal)32;
  _6 = _4 == 1;
  if (_6) goto bb_5; else goto bb_6;
bb_4:
  _2 = _5;
  goto bb_1;
bb_5:
  _5 = (GVal)1;
  goto bb_4;
bb_6:
  _7 = _0 == (GVal)99;
  _9 = _7 == 1;
  if (_9) goto bb_8; else goto bb_9;
bb_7:
  _5 = _8;
  goto bb_4;
bb_8:
  _8 = (GVal)2;
  goto bb_7;
bb_9:
  _10 = _0 == (GVal)114;
  _12 = _10 == 1;
  if (_12) goto bb_11; else goto bb_12;
bb_10:
  _8 = _11;
  goto bb_7;
bb_11:
  _11 = (GVal)3;
  goto bb_10;
bb_12:
  _13 = _0 == (GVal)110;
  _15 = _13 == 1;
  if (_15) goto bb_14; else goto bb_15;
bb_13:
  _11 = _14;
  goto bb_10;
bb_14:
  _14 = (GVal)4;
  goto bb_13;
bb_15:
  _14 = (GVal)0;
  goto bb_13;
}

GVal handle_action__int_any_any_any_any_any(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7, GVal _8, GVal _9, GVal _10) {
  _glyph_current_fn = "handle_action__int_any_any_any_any_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "handle_action__int_any_any_any_any_any";
  _glyph_call_depth++;
#endif
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
bb_0:
  _11 = _0 == (GVal)5;
  _13 = _11 == 1;
  if (_13) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _12;
bb_2:
  _12 = (GVal)0;
  goto bb_1;
bb_3:
  _14 = _0 == (GVal)1;
  _16 = _14 == 1;
  if (_16) goto bb_5; else goto bb_6;
bb_4:
  _12 = _15;
  goto bb_1;
bb_5:
  _17 = _8 == (GVal)1;
  _19 = _17 == 1;
  if (_19) goto bb_8; else goto bb_9;
bb_6:
  _23 = _0 == (GVal)2;
  _25 = _23 == 1;
  if (_25) goto bb_11; else goto bb_12;
bb_7:
  _20 = _18;
  _21 = redraw(_1, _2, _3, _4, _6, _7, _9, _20);
  _22 = event_loop(_1, _2, _3, _4, _5, _6, _7, _20, _9, _10);
  _15 = _22;
  goto bb_4;
bb_8:
  _18 = (GVal)0;
  goto bb_7;
bb_9:
  _18 = (GVal)1;
  goto bb_7;
bb_10:
  _15 = _24;
  goto bb_4;
bb_11:
  _26 = clear_grid(_4, (GVal)0);
  _27 = redraw(_1, _2, _3, _4, _6, _7, (GVal)0, (GVal)0);
  _28 = event_loop(_1, _2, _3, _4, _5, _6, _7, (GVal)0, (GVal)0, _10);
  _24 = _28;
  goto bb_10;
bb_12:
  _29 = _0 == (GVal)3;
  _31 = _29 == 1;
  if (_31) goto bb_14; else goto bb_15;
bb_13:
  _24 = _30;
  goto bb_10;
bb_14:
  _32 = randomize_grid(_4, _10, (GVal)0);
  _33 = _32;
  _34 = redraw(_1, _2, _3, _4, _6, _7, (GVal)0, (GVal)0);
  _35 = event_loop(_1, _2, _3, _4, _5, _6, _7, (GVal)0, (GVal)0, _33);
  _30 = _35;
  goto bb_13;
bb_15:
  _36 = _0 == (GVal)4;
  _38 = _36 == 1;
  if (_38) goto bb_17; else goto bb_18;
bb_16:
  _30 = _37;
  goto bb_13;
bb_17:
  _39 = do_step(_1, _2, _3, _4, _5, _6, _7, _9);
  _40 = _39;
  _41 = event_loop(_1, _2, _3, _4, _5, _6, _7, (GVal)0, _40, _10);
  _37 = _41;
  goto bb_16;
bb_18:
  _42 = event_loop(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10);
  _37 = _42;
  goto bb_16;
}

GVal grid_set__arr_int_int_any(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "grid_set__arr_int_int_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "grid_set__arr_int_int_any";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
bb_0:
  _4 = _2 * (GVal)64;
  _5 = _4 + _1;
  _6 = glyph_array_set(_0, _5, _3);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _6;
}

GVal redraw__any_any_any_any_any_int_int_int(GVal _0, GVal _1, GVal _2, GVal _3, GVal _4, GVal _5, GVal _6, GVal _7) {
  _glyph_current_fn = "redraw__any_any_any_any_any_int_int_int";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "redraw__any_any_any_any_any_int_int_int";
  _glyph_call_depth++;
#endif
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
bb_0:
  _8 = draw_grid(_0, _1, _2, _3, _4, _5, (GVal)0);
  _9 = _7 == (GVal)1;
  _11 = _9 == 1;
  if (_11) goto bb_2; else goto bb_3;
bb_1:
  _18 = glyph_gx_flush((GVal)0);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _18;
bb_2:
  _12 = glyph_int_to_str(_6);
  _13 = s3((GVal)glyph_cstr_to_str((GVal)"Life gen "), _12, (GVal)glyph_cstr_to_str((GVal)" running"));
  _14 = glyph_gx_store_name(_1, _13);
  _10 = _14;
  goto bb_1;
bb_3:
  _15 = glyph_int_to_str(_6);
  _16 = s3((GVal)glyph_cstr_to_str((GVal)"Life gen "), _15, (GVal)glyph_cstr_to_str((GVal)" paused"));
  _17 = glyph_gx_store_name(_1, _16);
  _10 = _17;
  goto bb_1;
}

GVal s3__str_any_str_any(GVal _0, GVal _1, GVal _2) {
  _glyph_current_fn = "s3__str_any_str_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "s3__str_any_str_any";
  _glyph_call_depth++;
#endif
  GVal _3 = 0;
  GVal _4 = 0;
bb_0:
  _3 = s2(_0, _1);
  _4 = s2(_3, _2);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _4;
}

GVal s2__any_any_any(GVal _0, GVal _1) {
  _glyph_current_fn = "s2__any_any_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "s2__any_any_any";
  _glyph_call_depth++;
#endif
  GVal _2 = 0;
bb_0:
  _2 = glyph_str_concat(_0, _1);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _2;
}

GVal grid_set__any_int_any(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "grid_set__any_int_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "grid_set__any_int_any";
  _glyph_call_depth++;
#endif
  GVal _4 = 0;
  GVal _5 = 0;
  GVal _6 = 0;
bb_0:
  _4 = _2 * (GVal)64;
  _5 = _4 + _1;
  _6 = glyph_array_set(_0, _5, _3);
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _6;
}

GVal step_cell__arr_int_any(GVal _0, GVal _1, GVal _2, GVal _3) {
  _glyph_current_fn = "step_cell__arr_int_any";
#ifdef GLYPH_DEBUG
  if (_glyph_call_depth < 256) _glyph_call_stack[_glyph_call_depth] = "step_cell__arr_int_any";
  _glyph_call_depth++;
#endif
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
bb_0:
  _4 = grid_get(_0, _2, _3);
  _5 = _4;
  _6 = count_neighbors(_0, _2, _3);
  _7 = _6;
  _8 = _5 == (GVal)1;
  _10 = _8 == 1;
  if (_10) goto bb_2; else goto bb_3;
bb_1:
#ifdef GLYPH_DEBUG
  _glyph_call_depth--;
#endif
  return _9;
bb_2:
  _11 = _7 == (GVal)2;
  _12 = _7 == (GVal)3;
  _13 = _11 || _12;
  _15 = _13 == 1;
  if (_15) goto bb_5; else goto bb_6;
bb_3:
  _18 = _7 == (GVal)3;
  _20 = _18 == 1;
  if (_20) goto bb_8; else goto bb_9;
bb_4:
  _9 = _14;
  goto bb_1;
bb_5:
  _16 = grid_set(_1, _2, _3, (GVal)1);
  _14 = _16;
  goto bb_4;
bb_6:
  _17 = grid_set(_1, _2, _3, (GVal)0);
  _14 = _17;
  goto bb_4;
bb_7:
  _9 = _19;
  goto bb_1;
bb_8:
  _21 = grid_set(_1, _2, _3, (GVal)1);
  _19 = _21;
  goto bb_7;
bb_9:
  _22 = grid_set(_1, _2, _3, (GVal)0);
  _19 = _22;
  goto bb_7;
}



extern void glyph_set_args(int argc, char** argv);
int main(int argc, char** argv) {
  GC_INIT();
  signal(SIGSEGV, _glyph_sigsegv);
  signal(SIGFPE, _glyph_sigfpe);
  glyph_set_args(argc, argv);
  return (int)glyph_main();
}
