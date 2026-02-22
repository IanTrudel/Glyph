/* nc_wrapper.c — Thin ncurses wrappers for Glyph (all long long ABI) */
#include <ncurses.h>
#include <stdlib.h>

long long nc_init(long long dummy) {
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    return 0;
}

long long nc_cleanup(long long dummy) {
    endwin();
    return 0;
}

long long nc_lines(long long dummy) {
    return (long long)LINES;
}

long long nc_cols(long long dummy) {
    return (long long)COLS;
}

long long nc_attr_rev(long long dummy) {
    return (long long)A_REVERSE;
}

long long nc_attr_bold(long long dummy) {
    return (long long)A_BOLD;
}

/* Convert int char code to a Glyph string {char* ptr, long long len} */
long long nc_char_to_str(long long ch) {
    char *data = (char *)malloc(2);
    data[0] = (char)ch;
    data[1] = '\0';
    long long *str = (long long *)malloc(16);
    str[0] = (long long)data;
    str[1] = 1;
    return (long long)str;
}

/* Wrappers for ncurses functions that are real functions (not macros) */
long long nc_clear(long long dummy) {
    return (long long)clear();
}

long long nc_refresh(long long dummy) {
    return (long long)refresh();
}

long long nc_getch(long long dummy) {
    return (long long)getch();
}

long long nc_mvaddstr(long long y, long long x, long long s) {
    return (long long)mvaddstr((int)y, (int)x, (const char *)s);
}

long long nc_attron(long long attr) {
    return (long long)attron((int)attr);
}

long long nc_attroff(long long attr) {
    return (long long)attroff((int)attr);
}

long long nc_move_cur(long long y, long long x) {
    return (long long)move((int)y, (int)x);
}
