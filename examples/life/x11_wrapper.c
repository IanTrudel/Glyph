/* x11_wrapper.c — Thin X11 wrappers for Glyph (all long long ABI) */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <unistd.h>

static Display *_dpy;
static XEvent   _evt;

long long gx_open_display(long long dummy) {
    _dpy = XOpenDisplay(NULL);
    return (long long)_dpy;
}

long long gx_close_display(long long dummy) {
    XCloseDisplay(_dpy);
    return 0;
}

long long gx_default_screen(long long dummy) {
    return (long long)DefaultScreen(_dpy);
}

long long gx_root_window(long long scr) {
    return (long long)RootWindow(_dpy, (int)scr);
}

long long gx_black_pixel(long long scr) {
    return (long long)BlackPixel(_dpy, (int)scr);
}

long long gx_create_window(long long parent, long long x, long long y,
                           long long w, long long h, long long bg) {
    return (long long)XCreateSimpleWindow(_dpy, (Window)parent,
        (int)x, (int)y, (unsigned)w, (unsigned)h, 0, 0, (unsigned long)bg);
}

long long gx_create_gc(long long win) {
    return (long long)XCreateGC(_dpy, (Drawable)win, 0, NULL);
}

long long gx_map_window(long long win) {
    XMapWindow(_dpy, (Window)win);
    return 0;
}

long long gx_select_input(long long win, long long mask) {
    XSelectInput(_dpy, (Window)win, (long)mask);
    return 0;
}

/* Read Glyph string struct: {char* ptr, long long len} */
long long gx_store_name(long long win, long long glyph_str) {
    char *ptr = *(char **)glyph_str;
    long long len = *(long long *)(glyph_str + 8);
    char buf[256];
    if (len > 255) len = 255;
    for (int i = 0; i < (int)len; i++) buf[i] = ptr[i];
    buf[len] = '\0';
    XStoreName(_dpy, (Window)win, buf);
    return 0;
}

long long gx_alloc_color(long long scr, long long r, long long g, long long b) {
    XColor c;
    c.red   = (unsigned short)r;
    c.green = (unsigned short)g;
    c.blue  = (unsigned short)b;
    c.flags = DoRed | DoGreen | DoBlue;
    Colormap cmap = DefaultColormap(_dpy, (int)scr);
    XAllocColor(_dpy, cmap, &c);
    return (long long)c.pixel;
}

/* Returns event type, or 0 if no event pending */
long long gx_check_event(long long dummy) {
    if (XPending(_dpy)) {
        XNextEvent(_dpy, &_evt);
        return (long long)_evt.type;
    }
    return 0;
}

long long gx_event_x(long long dummy) {
    return (long long)_evt.xbutton.x;
}

long long gx_event_y(long long dummy) {
    return (long long)_evt.xbutton.y;
}

long long gx_event_keycode(long long dummy) {
    return (long long)_evt.xkey.keycode;
}

long long gx_lookup_keysym(long long keycode) {
    return (long long)XKeycodeToKeysym(_dpy, (KeyCode)keycode, 0);
}

long long gx_set_fg(long long gc, long long color) {
    XSetForeground(_dpy, (GC)gc, (unsigned long)color);
    return 0;
}

long long gx_fill_rect(long long win, long long gc,
                       long long x, long long y, long long w, long long h) {
    XFillRectangle(_dpy, (Drawable)win, (GC)gc, (int)x, (int)y, (unsigned)w, (unsigned)h);
    return 0;
}

long long gx_draw_rect(long long win, long long gc,
                       long long x, long long y, long long w, long long h) {
    XDrawRectangle(_dpy, (Drawable)win, (GC)gc, (int)x, (int)y, (unsigned)w, (unsigned)h);
    return 0;
}

long long gx_flush(long long dummy) {
    XFlush(_dpy);
    return 0;
}

long long gx_usleep(long long us) {
    usleep((useconds_t)us);
    return 0;
}
