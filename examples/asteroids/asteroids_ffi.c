/* asteroids_ffi.c — SDL2 wrapper for Asteroids in Glyph (all long long ABI) */
#include <SDL2/SDL.h>
#include <SDL2/SDL_gamecontroller.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

/* ── Static state ───────────────────────────────────────────────────────── */
static SDL_Window          *_win  = NULL;
static SDL_Renderer        *_rend = NULL;
static int                  _quit = 0;
static const Uint8         *_keys = NULL;
static SDL_GameController  *_ctrl = NULL;

/* ── Init / Quit ─────────────────────────────────────────────────────────── */

long long sdl_init(long long w, long long h) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    _win = SDL_CreateWindow("Asteroids",
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            (int)w, (int)h, SDL_WINDOW_SHOWN);
    _rend = SDL_CreateRenderer(_win, -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    _keys = SDL_GetKeyboardState(NULL);
    /* Open first available controller */
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            _ctrl = SDL_GameControllerOpen(i);
            break;
        }
    }
    srand((unsigned)time(NULL));
    return 0;
}

long long sdl_quit_game(long long dummy) {
    if (_ctrl) { SDL_GameControllerClose(_ctrl); _ctrl = NULL; }
    if (_rend)  SDL_DestroyRenderer(_rend);
    if (_win)   SDL_DestroyWindow(_win);
    SDL_Quit();
    return 0;
}

/* ── Frame ───────────────────────────────────────────────────────────────── */

long long sdl_begin_frame(long long dummy) {
    SDL_SetRenderDrawColor(_rend, 0, 0, 0, 255);
    SDL_RenderClear(_rend);
    return 0;
}

long long sdl_end_frame(long long dummy) {
    SDL_RenderPresent(_rend);
    return 0;
}

/* ── Drawing ─────────────────────────────────────────────────────────────── */

long long sdl_set_color(long long r, long long g, long long b, long long a) {
    SDL_SetRenderDrawColor(_rend, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
    return 0;
}

long long sdl_draw_line(long long x1, long long y1, long long x2, long long y2) {
    SDL_RenderDrawLine(_rend, (int)x1, (int)y1, (int)x2, (int)y2);
    return 0;
}

long long sdl_fill_rect(long long x, long long y, long long w, long long h) {
    SDL_Rect r = { (int)x, (int)y, (int)w, (int)h };
    SDL_RenderFillRect(_rend, &r);
    return 0;
}

/* ── Input ───────────────────────────────────────────────────────────────── */

long long sdl_poll_events(long long dummy) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            _quit = 1;
        }
        if (e.type == SDL_KEYDOWN &&
            e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            _quit = 1;
        }
        if (e.type == SDL_CONTROLLERDEVICEADDED && !_ctrl) {
            _ctrl = SDL_GameControllerOpen(e.cdevice.which);
        }
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && _ctrl) {
            SDL_GameController *c =
                SDL_GameControllerFromInstanceID(e.cdevice.which);
            if (c == _ctrl) {
                SDL_GameControllerClose(_ctrl);
                _ctrl = NULL;
            }
        }
    }
    return _quit;
}

long long sdl_key_held(long long scancode) {
    return (_keys && _keys[scancode]) ? 1 : 0;
}

/* ── Timing ──────────────────────────────────────────────────────────────── */

long long sdl_ticks(long long dummy) {
    return (long long)SDL_GetTicks();
}

/* ── Controller ──────────────────────────────────────────────────────────── */

long long sdl_has_ctrl(long long dummy) {
    return _ctrl ? 1 : 0;
}

long long sdl_ctrl_axis(long long axis) {
    if (!_ctrl) return 0;
    int v = SDL_GameControllerGetAxis(_ctrl, (SDL_GameControllerAxis)(int)axis);
    return (long long)(v * 1000 / 32767);
}

long long sdl_ctrl_button(long long btn) {
    if (!_ctrl) return 0;
    return (long long)SDL_GameControllerGetButton(
        _ctrl, (SDL_GameControllerButton)(int)btn);
}

/* ── Random ──────────────────────────────────────────────────────────────── */

long long sdl_rand_f(long long dummy) {
    double r = (double)rand() / ((double)RAND_MAX + 1.0);
    long long out;
    memcpy(&out, &r, 8);
    return out;
}

/* ── HUD text rendering (7-segment style) ────────────────────────────────── */

static const unsigned char SEG[10] = {
    0x77, 0x12, 0x5D, 0x5B, 0x3A,
    0x6B, 0x6F, 0x52, 0x7F, 0x7B,
};

static void draw_seg_digit(int d, int x, int y) {
    if (d < 0 || d > 9) return;
    unsigned char s = SEG[d];
    int W = 8, H = 6;
    if (s & 0x40) SDL_RenderDrawLine(_rend, x+1, y,     x+W-1, y);
    if (s & 0x20) SDL_RenderDrawLine(_rend, x,   y+1,   x,     y+H-1);
    if (s & 0x10) SDL_RenderDrawLine(_rend, x+W, y+1,   x+W,   y+H-1);
    if (s & 0x08) SDL_RenderDrawLine(_rend, x+1, y+H,   x+W-1, y+H);
    if (s & 0x04) SDL_RenderDrawLine(_rend, x,   y+H+1, x,     y+H*2-1);
    if (s & 0x02) SDL_RenderDrawLine(_rend, x+W, y+H+1, x+W,   y+H*2-1);
    if (s & 0x01) SDL_RenderDrawLine(_rend, x+1, y+H*2, x+W-1, y+H*2);
}

long long sdl_draw_int(long long n, long long x, long long y) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lld", n < 0 ? 0LL : n);
    int dx = (int)x;
    for (int i = 0; buf[i]; i++) {
        draw_seg_digit(buf[i] - '0', dx, (int)y);
        dx += 12;
    }
    return 0;
}
