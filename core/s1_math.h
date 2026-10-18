/* Selva1 · fast, portable math (no libm: the same code builds freestanding for WebAssembly, for a plugin and on
 * the host). Accuracy is what a synth needs: exp2 to 1e-7 relative, sin from a 4096-point table, tanh a smooth
 * rational curve. Copyright (C) 2026 DeAbrew. */
#ifndef S1_MATH_H
#define S1_MATH_H
#include <stdint.h>

#define S1_PI 3.14159265358979f
#define S1_TAU 6.28318530717959f

static inline float s1_clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float s1_min(float a, float b) { return a < b ? a : b; }
static inline float s1_max(float a, float b) { return a > b ? a : b; }
static inline float s1_abs(float x) { return __builtin_fabsf(x); }
static inline float s1_floor(float x) { return __builtin_floorf(x); }
static inline float s1_sqrt(float x) { return __builtin_sqrtf(x); }
static inline float s1_lerp(float a, float b, float t) { return a + (b - a) * t; }

typedef union { float f; uint32_t u; int32_t i; } s1_fu;

/* 2^x: integer part in the exponent, the fraction from a 6th-order polynomial */
static inline float s1_exp2(float x)
{
    s1_fu r;
    float i, f, p;
    x = s1_clamp(x, -126.f, 126.f);
    i = s1_floor(x);
    f = x - i;
    p = 1.f + f * (0.693147182f + f * (0.240226507f + f * (0.0555041087f + f * (0.00961812911f +
                  f * (0.00133335581f + f * 0.000154035304f)))));
    r.f = p;
    r.i += (int32_t)i << 23;
    return r.f;
}
static inline float s1_exp(float x) { return s1_exp2(x * 1.44269504f); }

/* log2(x), x > 0: the exponent plus a polynomial of the mantissa (error ~1e-5) */
static inline float s1_log2(float x)
{
    s1_fu v;
    float m, e;
    if (x <= 1e-30f)
        return -100.f;
    v.f = x;
    e = (float)((int)((v.u >> 23) & 255) - 127);
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;      /* mantissa in [1, 2) */
    m = v.f - 1.5f;
    return e + (0.584962683f + m * (0.961814806f + m * (-0.320624817f + m * (0.141848194f + m * (-0.0706856324f +
                m * (0.0434313239f - 0.0248276600f * m))))));
}

/* sin of a phase in cycles (any real), from a 4096-point table, linearly interpolated (error ~3e-7) */
#define S1_SIN_N 4096
static float s1_sin_tab[S1_SIN_N + 1];
static void s1_math_init(void);
static inline float s1_sin01(float ph)
{
    float x = ph - s1_floor(ph), p = x * (float)S1_SIN_N;
    int i = (int)p;
    float t = p - (float)i;
    return s1_sin_tab[i] + (s1_sin_tab[i + 1] - s1_sin_tab[i]) * t;
}
static inline float s1_cos01(float ph) { return s1_sin01(ph + 0.25f); }

/* a smooth saturator: tanh-like (Pade 3/2, exact slope 1 at 0, reaches +-1 at |x| = 3) */
static inline float s1_tanh(float x)
{
    float x2;
    if (x > 3.f)
        return 1.f;
    if (x < -3.f)
        return -1.f;
    x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

/* tan(pi x) for x in [0, 0.49]: the filter's prewarp */
static inline float s1_tanpi(float x)
{
    x = s1_clamp(x, 1e-7f, 0.49f);
    return s1_sin01(x * 0.5f) / s1_cos01(x * 0.5f);
}

/* the table: sin by its Taylor series in double, the angle first brought into [-pi, pi] (no libm) */
static void s1_math_init(void)
{
    const double PI = 3.14159265358979323846;
    int i, k;
    for (i = 0; i <= S1_SIN_N; i++) {
        double x = 2.0 * PI * (double)i / (double)S1_SIN_N, s = 0, term;
        if (x > PI)
            x -= 2.0 * PI;
        term = x;
        for (k = 1; k < 40; k += 2) {
            s += term;
            term *= -x * x / (double)((k + 1) * (k + 2));
        }
        s1_sin_tab[i] = (float)s;
    }
}

/* xorshift32: per-voice noise and random */
static inline uint32_t s1_rand(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static inline float s1_randf(uint32_t *s) { return (float)(s1_rand(s) >> 8) * (1.f / 16777216.f); }   /* [0, 1) */
static inline float s1_randb(uint32_t *s) { return s1_randf(s) * 2.f - 1.f; }                          /* [-1, 1) */

#endif
