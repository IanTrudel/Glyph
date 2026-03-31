/* math_ffi.c — Standard math functions for Glyph
 *
 * Prepended before Glyph runtime via cc_prepend.
 * All functions take/return GVal with float values bitcast via memcpy.
 */
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef intptr_t GVal;

static double _d(GVal v) { double d; memcpy(&d, &v, sizeof(double)); return d; }
static GVal   _g(double d) { GVal v; memcpy(&v, &d, sizeof(double)); return v; }

/* Trigonometry */
GVal math_sin(GVal x)            { return _g(sin(_d(x))); }
GVal math_cos(GVal x)            { return _g(cos(_d(x))); }
GVal math_tan(GVal x)            { return _g(tan(_d(x))); }
GVal math_asin(GVal x)           { return _g(asin(_d(x))); }
GVal math_acos(GVal x)           { return _g(acos(_d(x))); }
GVal math_atan2(GVal y, GVal x)  { return _g(atan2(_d(y), _d(x))); }

/* Powers and roots */
GVal math_sqrt(GVal x)           { return _g(sqrt(_d(x))); }
GVal math_pow(GVal b, GVal e)    { return _g(pow(_d(b), _d(e))); }
GVal math_exp(GVal x)            { return _g(exp(_d(x))); }
GVal math_log(GVal x)            { return _g(log(_d(x))); }
GVal math_log2(GVal x)           { return _g(log2(_d(x))); }
GVal math_log10(GVal x)          { return _g(log10(_d(x))); }

/* Rounding */
GVal math_floor(GVal x)          { return _g(floor(_d(x))); }
GVal math_ceil(GVal x)           { return _g(ceil(_d(x))); }
GVal math_round(GVal x)          { return _g(round(_d(x))); }
GVal math_fabs(GVal x)           { return _g(fabs(_d(x))); }
