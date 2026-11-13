/* The C library's math functions for the game code built with Zig's C compiler on the phone (build_game.py --zig-cc,
 * tools/phone_build.py): their declarations as C has them, with the classification macros as Android's own math.h
 * defines them (clang's builtins). The functions come from Android's libm.so at run time, and the compiler turns most
 * of them into single instructions anyway. The NDK's headers are used on a PC, as before. */
#ifndef MHGU_ANDROID_MATH_H
#define MHGU_ANDROID_MATH_H
#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define isgreater(x, y) __builtin_isgreater(x, y)
#define isgreaterequal(x, y) __builtin_isgreaterequal(x, y)
#define isless(x, y) __builtin_isless(x, y)
#define islessequal(x, y) __builtin_islessequal(x, y)
#define islessgreater(x, y) __builtin_islessgreater(x, y)
#define isunordered(x, y) __builtin_isunordered(x, y)
double sqrt(double x);
float sqrtf(float x);
double fabs(double x);
float fabsf(float x);
double trunc(double x);
float truncf(float x);
double floor(double x);
float floorf(float x);
double ceil(double x);
float ceilf(float x);
double round(double x);
float roundf(float x);
double rint(double x);
float rintf(float x);
double nearbyint(double x);
float nearbyintf(float x);
double exp(double x);
float expf(float x);
double exp2(double x);
float exp2f(float x);
double expm1(double x);
float expm1f(float x);
double log(double x);
float logf(float x);
double log2(double x);
float log2f(float x);
double log10(double x);
float log10f(float x);
double log1p(double x);
float log1pf(float x);
double sin(double x);
float sinf(float x);
double cos(double x);
float cosf(float x);
double tan(double x);
float tanf(float x);
double asin(double x);
float asinf(float x);
double acos(double x);
float acosf(float x);
double atan(double x);
float atanf(float x);
double sinh(double x);
float sinhf(float x);
double cosh(double x);
float coshf(float x);
double tanh(double x);
float tanhf(float x);
double asinh(double x);
float asinhf(float x);
double acosh(double x);
float acoshf(float x);
double atanh(double x);
float atanhf(float x);
double cbrt(double x);
float cbrtf(float x);
double erf(double x);
float erff(float x);
double erfc(double x);
float erfcf(float x);
double tgamma(double x);
float tgammaf(float x);
double lgamma(double x);
float lgammaf(float x);
double logb(double x);
float logbf(float x);
double fmin(double x, double y);
float fminf(float x, float y);
double fmax(double x, double y);
float fmaxf(float x, float y);
double copysign(double x, double y);
float copysignf(float x, float y);
double fmod(double x, double y);
float fmodf(float x, float y);
double remainder(double x, double y);
float remainderf(float x, float y);
double pow(double x, double y);
float powf(float x, float y);
double atan2(double x, double y);
float atan2f(float x, float y);
double hypot(double x, double y);
float hypotf(float x, float y);
double fdim(double x, double y);
float fdimf(float x, float y);
double nextafter(double x, double y);
float nextafterf(float x, float y);
double fma(double x, double y, double z);
float fmaf(float x, float y, float z);
double ldexp(double x, int e);
float ldexpf(float x, int e);
double scalbn(double x, int e);
float scalbnf(float x, int e);
double frexp(double x, int *e);
float frexpf(float x, int *e);
double modf(double x, double *i);
float modff(float x, float *i);
long lrint(double x);
long lrintf(float x);
long long llrint(double x);
long long llrintf(float x);
long lround(double x);
long lroundf(float x);
long long llround(double x);
long long llroundf(float x);
int ilogb(double x);
int ilogbf(float x);
#endif
