/* asteroids_ffi.c — SDL2 wrapper for Asteroids in Glyph (all long long ABI) */
#include <SDL2/SDL.h>
#include <SDL2/SDL_gamecontroller.h>
#include <math.h>
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

/* Returns 1 if the application should quit, 0 otherwise.
   Handles controller hotplug events. */
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

/* Returns 1 if the given SDL scancode is currently held */
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

/* Returns axis value scaled to -1000..1000 */
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

/* Returns a float in [0.0, 1.0) bitcast to long long (Glyph GVal format) */
long long sdl_rand_f(long long dummy) {
    double r = (double)rand() / ((double)RAND_MAX + 1.0);
    long long out;
    memcpy(&out, &r, 8);
    return out;
}

/* ── HUD text rendering (7-segment style) ────────────────────────────────── */

/*  Segment layout (each digit is 8px wide × 12px tall):
 *
 *   _
 *  | |
 *   -
 *  | |
 *   _
 *
 *  Segments: bit6=top  bit5=top-left  bit4=top-right
 *            bit3=mid  bit2=bot-left  bit1=bot-right  bit0=bot
 */
static const unsigned char SEG[10] = {
    0x77, /* 0: top tl tr bl br bot   */
    0x12, /* 1: tr br                 */
    0x5D, /* 2: top tr mid bl bot     */
    0x5B, /* 3: top tr mid br bot     */
    0x3A, /* 4: tl tr mid br          */
    0x6B, /* 5: top tl mid br bot     */
    0x6F, /* 6: top tl mid bl br bot  */
    0x52, /* 7: top tr br             */
    0x7F, /* 8: all                   */
    0x7B, /* 9: top tl tr mid br bot  */
};

static void draw_seg_digit(int d, int x, int y) {
    if (d < 0 || d > 9) return;
    unsigned char s = SEG[d];
    int W = 8, H = 6; /* half-height */
    /* top */
    if (s & 0x40) SDL_RenderDrawLine(_rend, x+1, y,     x+W-1, y);
    /* top-left */
    if (s & 0x20) SDL_RenderDrawLine(_rend, x,   y+1,   x,     y+H-1);
    /* top-right */
    if (s & 0x10) SDL_RenderDrawLine(_rend, x+W, y+1,   x+W,   y+H-1);
    /* middle */
    if (s & 0x08) SDL_RenderDrawLine(_rend, x+1, y+H,   x+W-1, y+H);
    /* bot-left */
    if (s & 0x04) SDL_RenderDrawLine(_rend, x,   y+H+1, x,     y+H*2-1);
    /* bot-right */
    if (s & 0x02) SDL_RenderDrawLine(_rend, x+W, y+H+1, x+W,   y+H*2-1);
    /* bottom */
    if (s & 0x01) SDL_RenderDrawLine(_rend, x+1, y+H*2, x+W-1, y+H*2);
}

/* Draw a non-negative integer n at pixel position (x, y).
   Color must be set by the caller. */
long long sdl_draw_int(long long n, long long x, long long y) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lld", n < 0 ? 0LL : n);
    int dx = (int)x;
    for (int i = 0; buf[i]; i++) {
        draw_seg_digit(buf[i] - '0', dx, (int)y);
        dx += 12; /* digit width + 2px gap */
    }
    return 0;
}

/* ── Game state (C-side globals) ─────────────────────────────────────────── */
/* All game state lives here to avoid Glyph's partial-record-update codegen   */

#define _MAX_AST 20
#define _MAX_BUL 10

static inline double   _gf(long long v){double   r;memcpy(&r,&v,8);return r;}
static inline long long _gv(double   v){long long r;memcpy(&r,&v,8);return r;}

/* Ship */
static double    _sx,_sy,_svx,_svy,_sang;
static long long _salive,_sinv,_scool;

/* Asteroids */
static double    _ax[_MAX_AST],_ay[_MAX_AST],_avx[_MAX_AST],_avy[_MAX_AST];
static double    _ar[_MAX_AST],_arot[_MAX_AST];
static long long _aa[_MAX_AST];

/* Bullets */
static double    _bx[_MAX_BUL],_by[_MAX_BUL],_bvx[_MAX_BUL],_bvy[_MAX_BUL];
static long long _bl[_MAX_BUL];

/* Meta */
static long long _score,_lives,_level,_pticks;

/* ── State helpers ───────────────────────────────────────────────────────── */

long long reset_state(long long d) {
    _sx=400.0;_sy=300.0;_svx=0.0;_svy=0.0;_sang=0.0;
    _salive=1;_sinv=0;_scool=0;
    for(int i=0;i<_MAX_AST;i++){
        _ax[i]=_ay[i]=_avx[i]=_avy[i]=_ar[i]=_arot[i]=0.0;_aa[i]=0;
    }
    for(int i=0;i<_MAX_BUL;i++){_bx[i]=_by[i]=_bvx[i]=_bvy[i]=0.0;_bl[i]=0;}
    _score=0;_lives=3;_level=1;_pticks=0;
    return 0;
}
long long clear_asts(long long d) {
    for(int i=0;i<_MAX_AST;i++){
        _ax[i]=_ay[i]=_avx[i]=_avy[i]=_ar[i]=_arot[i]=0.0;_aa[i]=0;
    }
    return 0;
}
long long clear_buls(long long d) {
    for(int i=0;i<_MAX_BUL;i++){_bx[i]=_by[i]=_bvx[i]=_bvy[i]=0.0;_bl[i]=0;}
    return 0;
}

/* ── Ship accessors ──────────────────────────────────────────────────────── */
long long get_sx(long long d){return _gv(_sx);}
long long set_sx(long long v){_sx=_gf(v);return 0;}
long long get_sy(long long d){return _gv(_sy);}
long long set_sy(long long v){_sy=_gf(v);return 0;}
long long get_svx(long long d){return _gv(_svx);}
long long set_svx(long long v){_svx=_gf(v);return 0;}
long long get_svy(long long d){return _gv(_svy);}
long long set_svy(long long v){_svy=_gf(v);return 0;}
long long get_sang(long long d){return _gv(_sang);}
long long set_sang(long long v){_sang=_gf(v);return 0;}
long long get_salive(long long d){return _salive;}
long long set_salive(long long v){_salive=v;return 0;}
long long get_sinv(long long d){return _sinv;}
long long set_sinv(long long v){_sinv=v;return 0;}
long long get_scool(long long d){return _scool;}
long long set_scool(long long v){_scool=v;return 0;}

/* ── Asteroid accessors ──────────────────────────────────────────────────── */
long long get_ax(long long i){if(i<0||i>=_MAX_AST)return 0;return _gv(_ax[i]);}
long long set_ax(long long i,long long v){if(i<0||i>=_MAX_AST)return 0;_ax[i]=_gf(v);return 0;}
long long get_ay(long long i){if(i<0||i>=_MAX_AST)return 0;return _gv(_ay[i]);}
long long set_ay(long long i,long long v){if(i<0||i>=_MAX_AST)return 0;_ay[i]=_gf(v);return 0;}
long long get_avx(long long i){if(i<0||i>=_MAX_AST)return 0;return _gv(_avx[i]);}
long long set_avx(long long i,long long v){if(i<0||i>=_MAX_AST)return 0;_avx[i]=_gf(v);return 0;}
long long get_avy(long long i){if(i<0||i>=_MAX_AST)return 0;return _gv(_avy[i]);}
long long set_avy(long long i,long long v){if(i<0||i>=_MAX_AST)return 0;_avy[i]=_gf(v);return 0;}
long long get_ar(long long i){if(i<0||i>=_MAX_AST)return 0;return _gv(_ar[i]);}
long long set_ar(long long i,long long v){if(i<0||i>=_MAX_AST)return 0;_ar[i]=_gf(v);return 0;}
long long get_arot(long long i){if(i<0||i>=_MAX_AST)return 0;return _gv(_arot[i]);}
long long set_arot(long long i,long long v){if(i<0||i>=_MAX_AST)return 0;_arot[i]=_gf(v);return 0;}
long long get_aa(long long i){if(i<0||i>=_MAX_AST)return 0;return _aa[i];}
long long set_aa(long long i,long long v){if(i<0||i>=_MAX_AST)return 0;_aa[i]=v;return 0;}

/* ── Bullet accessors ────────────────────────────────────────────────────── */
long long get_bx(long long i){if(i<0||i>=_MAX_BUL)return 0;return _gv(_bx[i]);}
long long set_bx(long long i,long long v){if(i<0||i>=_MAX_BUL)return 0;_bx[i]=_gf(v);return 0;}
long long get_by(long long i){if(i<0||i>=_MAX_BUL)return 0;return _gv(_by[i]);}
long long set_by(long long i,long long v){if(i<0||i>=_MAX_BUL)return 0;_by[i]=_gf(v);return 0;}
long long get_bvx(long long i){if(i<0||i>=_MAX_BUL)return 0;return _gv(_bvx[i]);}
long long set_bvx(long long i,long long v){if(i<0||i>=_MAX_BUL)return 0;_bvx[i]=_gf(v);return 0;}
long long get_bvy(long long i){if(i<0||i>=_MAX_BUL)return 0;return _gv(_bvy[i]);}
long long set_bvy(long long i,long long v){if(i<0||i>=_MAX_BUL)return 0;_bvy[i]=_gf(v);return 0;}
long long get_bl(long long i){if(i<0||i>=_MAX_BUL)return 0;return _bl[i];}
long long set_bl(long long i,long long v){if(i<0||i>=_MAX_BUL)return 0;_bl[i]=v;return 0;}

/* ── Meta accessors ──────────────────────────────────────────────────────── */
long long get_score(long long d){return _score;}
long long set_score(long long v){_score=v;return 0;}
long long get_lives(long long d){return _lives;}
long long set_lives(long long v){_lives=v;return 0;}
long long get_level(long long d){return _level;}
long long set_level(long long v){_level=v;return 0;}
long long get_pticks(long long d){return _pticks;}
long long set_pticks(long long v){_pticks=v;return 0;}

/* ── Float arithmetic helpers (all called from Glyph, do C-side floats) ── */
/* Glyph's type system doesn't track float-returning FFI functions, causing
   spurious int_to_float coercions on float bitcasts.  All float math lives
   here instead.                                                              */

#define _TAU 6.283185307179586

static double _wrapf(double v, double hi){
    v=fmod(v,hi); if(v<0.0)v+=hi; return v;
}

/* Delta-time: compute from ticks, store pticks, return float GVal */
long long sdl_get_dt(long long ticks){
    double dt=(_pticks>0)?(double)(ticks-_pticks)*0.001:0.016;
    if(dt>0.05)dt=0.05;
    _pticks=ticks;
    return _gv(dt);
}

/* Rotation: turn=-1/0/+1 integer, dt is float GVal */
long long apply_rotation_c(long long turn,long long dt_gval){
    if(!turn)return 0;
    _sang+=(double)(int)turn*2.5*_gf(dt_gval);
    return 0;
}

/* Thrust: thrust=0/1 integer, dt is float GVal */
long long apply_thrust_c(long long thrust,long long dt_gval){
    if(!thrust)return 0;
    double dt=_gf(dt_gval);
    _svx+=cos(_sang)*150.0*dt;
    _svy+=sin(_sang)*150.0*dt;
    double spd=sqrt(_svx*_svx+_svy*_svy);
    if(spd>300.0){double s=300.0/spd;_svx*=s;_svy*=s;}
    return 0;
}

/* Fire bullet from current ship position/angle */
long long fire_bullet_c(long long dummy){
    int slot=-1;
    for(int i=0;i<_MAX_BUL;i++){if(_bl[i]==0){slot=i;break;}}
    if(slot<0)return 0;
    _bx[slot]=_sx+cos(_sang)*14.0;
    _by[slot]=_sy+sin(_sang)*14.0;
    _bvx[slot]=cos(_sang)*400.0;
    _bvy[slot]=sin(_sang)*400.0;
    _bl[slot]=90;
    return 0;
}

/* Ship position integration + sinv/scool decrement */
long long update_ship_c(long long dt_gval){
    double dt=_gf(dt_gval);
    _sx=_wrapf(_sx+_svx*dt,800.0);
    _sy=_wrapf(_sy+_svy*dt,600.0);
    if(_sinv>0)_sinv--;
    if(_scool>0)_scool--;
    return 0;
}

/* Respawn countdown; called each frame when ship is dead */
long long respawn_ship_c(long long dummy){
    if(_sinv>0){_sinv--;return 0;}
    /* sinv reached 0 → respawn */
    _sx=400.0;_sy=300.0;_svx=0.0;_svy=0.0;_sang=0.0;
    _salive=1;_sinv=180;
    return 0;
}

/* Update all active asteroids */
long long update_asts_c(long long dt_gval){
    double dt=_gf(dt_gval);
    for(int i=0;i<_MAX_AST;i++){
        if(!_aa[i])continue;
        _ax[i]=_wrapf(_ax[i]+_avx[i]*dt,800.0);
        _ay[i]=_wrapf(_ay[i]+_avy[i]*dt,600.0);
        _arot[i]+=0.5*dt;
    }
    return 0;
}

/* Update all active bullets */
long long update_buls_c(long long dt_gval){
    double dt=_gf(dt_gval);
    for(int i=0;i<_MAX_BUL;i++){
        if(_bl[i]<=0)continue;
        _bx[i]=_wrapf(_bx[i]+_bvx[i]*dt,800.0);
        _by[i]=_wrapf(_by[i]+_bvy[i]*dt,600.0);
        _bl[i]--;
    }
    return 0;
}

/* Draw ship triangle (color must be set by caller) */
long long draw_ship_c(long long dummy){
    int nx=(int)(_sx+cos(_sang)*14.0);
    int ny=(int)(_sy+sin(_sang)*14.0);
    int lx=(int)(_sx+cos(_sang+2.5)*10.0);
    int ly=(int)(_sy+sin(_sang+2.5)*10.0);
    int rx=(int)(_sx+cos(_sang-2.5)*10.0);
    int ry=(int)(_sy+sin(_sang-2.5)*10.0);
    SDL_RenderDrawLine(_rend,nx,ny,lx,ly);
    SDL_RenderDrawLine(_rend,nx,ny,rx,ry);
    SDL_RenderDrawLine(_rend,lx,ly,rx,ry);
    return 0;
}

/* Draw asteroid polygon (color must be set by caller) */
long long draw_ast_c(long long ii){
    int i=(int)ii;
    if(i<0||i>=_MAX_AST||!_aa[i])return 0;
    double cx=_ax[i],cy=_ay[i],r=_ar[i],rot=_arot[i];
    for(int k=0;k<10;k++){
        double a0=k*(_TAU/10.0)+rot;
        double a1=(k+1)*(_TAU/10.0)+rot;
        int x0=(int)(cx+cos(a0)*r),y0=(int)(cy+sin(a0)*r);
        int x1=(int)(cx+cos(a1)*r),y1=(int)(cy+sin(a1)*r);
        SDL_RenderDrawLine(_rend,x0,y0,x1,y1);
    }
    return 0;
}

/* Internal: spawn a fragment at (x,y) with given velocity+radius */
static void _spawn_frag(double x,double y,double vx,double vy,double r){
    for(int i=0;i<_MAX_AST;i++){
        if(_aa[i])continue;
        _ax[i]=x;_ay[i]=y;_avx[i]=vx;_avy[i]=vy;
        _ar[i]=r;_arot[i]=0.0;_aa[i]=1;
        return;
    }
}

/* Internal: scoring by radius */
static long long _score_r(double r){
    if(r>30.0)return 100;
    if(r>18.0)return 200;
    return 500;
}

/* Bullet vs asteroid: returns 1 if hit (handles split + score) */
long long check_bul_ast_c(long long bi,long long ai){
    if(_bl[bi]<=0||!_aa[ai])return 0;
    double dx=_bx[bi]-_ax[ai],dy=_by[bi]-_ay[ai];
    if(dx*dx+dy*dy>=_ar[ai]*_ar[ai])return 0;
    _score+=_score_r(_ar[ai]);
    _bl[bi]=0;
    /* split */
    double nr=_ar[ai]*0.5;
    _aa[ai]=0;
    if(nr>=12.0){
        double t1=((double)rand()/(RAND_MAX+1.0))*_TAU;
        double spd=40.0+((double)rand()/(RAND_MAX+1.0))*40.0;
        _spawn_frag(_ax[ai],_ay[ai], cos(t1)*spd,sin(t1)*spd,nr);
        _spawn_frag(_ax[ai],_ay[ai],-cos(t1)*spd,-sin(t1)*spd,nr);
    }
    return 1;
}

/* Ship vs asteroid: returns 1 if hit */
long long check_ship_ast_c(long long ai){
    if(!_aa[ai]||!_salive||_sinv>0)return 0;
    double dx=_sx-_ax[ai],dy=_sy-_ay[ai];
    double hr=_ar[ai]+8.0;
    if(dx*dx+dy*dy>=hr*hr)return 0;
    _salive=0;_lives--;_sinv=90;
    return 1;
}

/* Spawn random asteroid at screen edge with given radius */
long long spawn_rand_ast_c(long long r_gval){
    int slot=-1;
    for(int i=0;i<_MAX_AST;i++){if(!_aa[i]){slot=i;break;}}
    if(slot<0)return 0;
    double r=_gf(r_gval);
    int edge=(int)(((double)rand()/(RAND_MAX+1.0))*4.0);
    double t=(double)rand()/(RAND_MAX+1.0);
    double x,y;
    switch(edge){
        case 0:x=t*800.0;y=0.0;break;
        case 1:x=800.0;y=t*600.0;break;
        case 2:x=t*800.0;y=600.0;break;
        default:x=0.0;y=t*600.0;break;
    }
    double ang=((double)rand()/(RAND_MAX+1.0))*_TAU;
    double spd=40.0+((double)rand()/(RAND_MAX+1.0))*40.0;
    _ax[slot]=x;_ay[slot]=y;
    _avx[slot]=cos(ang)*spd;_avy[slot]=sin(ang)*spd;
    _ar[slot]=r;_arot[slot]=0.0;_aa[slot]=1;
    return 0;
}
