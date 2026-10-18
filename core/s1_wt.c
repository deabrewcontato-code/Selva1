/* Selva1 · wavetables. Every table is made by code, so nothing has to be downloaded or licensed: each frame is a
 * single cycle drawn in the time domain at 8192 points (or written directly as a spectrum), taken to the
 * frequency domain, and written back as ten band-limited copies (mipmaps), each with half the harmonics of the
 * one before: a note plays the copy whose harmonics all sit under Nyquist, so nothing aliases.
 * A table is built the first time an oscillator (or the panel) asks for it. Copyright (C) 2026 DeAbrew. */

#define WT_FRAMES 32
#define WT_GEN 8192                      /* the drawing resolution */
#define WT_LEVELS 10                     /* harmonics per level: 1023, 511, 255, 127, 63, 31, 15, 7, 3, 1 */
static const int WT_SIZE[WT_LEVELS] = {2048, 1024, 512, 256, 256, 256, 256, 256, 256, 256};
static const int WT_HMAX[WT_LEVELS] = {1023, 511, 255, 127, 63, 31, 15, 7, 3, 1};
#define WT_FRAME_FLOATS (2048 + 1024 + 512 + 256 * 7)   /* 5376: one frame, every level */
#define WT_TABLE_FLOATS (WT_FRAMES * WT_FRAME_FLOATS)
#define WT_MAX_LOADED 18

typedef struct {
    const char *name;
    int kind;
} wt_def_t;
enum { WTK_BASIC, WTK_SAOPAULO, WTK_VIADUTO, WTK_CONCRETO, WTK_METRO, WTK_GAROA, WTK_NEON, WTK_ACO, WTK_ASFALTO,
       WTK_SIRENE, WTK_BUZINA, WTK_MINHOCAO, WTK_PAULISTA, WTK_TIETE, WTK_COUNT };
static const wt_def_t WT_DEFS[WTK_COUNT] = {
    {"BASICA", WTK_BASIC},       {"SAO PAULO", WTK_SAOPAULO}, {"VIADUTO", WTK_VIADUTO}, {"CONCRETO", WTK_CONCRETO},
    {"METRO", WTK_METRO},        {"GAROA", WTK_GAROA},        {"NEON", WTK_NEON},       {"ACO", WTK_ACO},
    {"ASFALTO", WTK_ASFALTO},    {"SIRENE", WTK_SIRENE},      {"BUZINA", WTK_BUZINA},   {"MINHOCAO", WTK_MINHOCAO},
    {"PAULISTA", WTK_PAULISTA},  {"TIETE", WTK_TIETE},
};
/* display names (UTF-8) for the panel */
static const char *const WT_LABEL[WTK_COUNT] = {"BÁSICA", "SÃO PAULO", "VIADUTO", "CONCRETO", "METRÔ", "GAROA", "NEON",
                                               "AÇO", "ASFALTO", "SIRENE", "BUZINA", "MINHOCÃO", "PAULISTA", "TIETÊ"};

static float wt_arena[WT_MAX_LOADED][WT_TABLE_FLOATS];   /* 18 x 688 KB */
static signed char wt_slot[WTK_COUNT];                   /* the arena slot of each table, -1 = not built */
static int wt_used;

/* ------------------------------------------------------------------- FFT (radix 2, in place) --- */
static float fft_re[WT_GEN], fft_im[WT_GEN];
static void s1_fft(float *re, float *im, int n, int inverse)
{
    int i, j, k, len;
    for (i = 1, j = 0; i < n; i++) {                /* bit reversal */
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        float ang = (inverse ? 1.f : -1.f) / (float)len;   /* in cycles */
        float wr = s1_cos01(ang), wi = s1_sin01(ang);
        for (i = 0; i < n; i += len) {
            float cr = 1.f, ci = 0.f;
            for (k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr, nr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

/* ------------------------------------------------------------------- the frames --- */
static float spec_re[1025], spec_im[1025];        /* the frame's harmonics 0..1024 */

static inline float frac01(float x) { return x - s1_floor(x); }
static float gauss(float x, float c, float w) { float d = (x - c) / w; return s1_exp(-d * d); }

/* a vowel's formants (Hz): a e i o u */
static const float VOWEL_F[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410},
                                    {300, 870, 2240}};

/* fills spec_re/spec_im for frame t (0..1) of table k. Either draws a cycle (time domain) or writes harmonics. */
static void wt_frame_spectrum(int k, float t)
{
    int i, h;
    int draw = 1;
    uint32_t seed = 0x9E3779B9u ^ (uint32_t)(k * 7919);
    for (h = 0; h <= 1024; h++)
        spec_re[h] = spec_im[h] = 0;
    switch (k) {
    case WTK_SAOPAULO: {                      /* two formants sweeping across a bright, rounded spectrum */
        float f1 = 2.f + 22.f * t, f2 = 9.f + 60.f * (1.f - t) * (1.f - t) + 20.f * t;
        draw = 0;
        for (h = 1; h <= 1023; h++) {
            float a = 1.f / s1_exp2(0.7f * s1_log2((float)h));
            a *= 0.25f + 1.2f * gauss((float)h, f1, 2.f + 3.f * t) + 0.9f * gauss((float)h, f2, 4.f + 6.f * t);
            spec_im[h] = -a;                  /* sine phase */
        }
        break;
    }
    case WTK_CONCRETO: {                      /* dense, rough, seeded random harmonics: concrete and rebar */
        float tilt = 1.6f - 1.2f * t, dens = 0.15f + 0.85f * t;
        draw = 0;
        for (h = 1; h <= 1023; h++) {
            float r = s1_randf(&seed), ph = s1_randf(&seed), keep = s1_randf(&seed) < dens || h < 4 ? 1.f : 0.f;
            float a = keep * (0.3f + r) / s1_exp2(tilt * s1_log2((float)h));
            spec_re[h] = a * s1_cos01(ph);
            spec_im[h] = a * s1_sin01(ph);
        }
        break;
    }
    case WTK_GAROA: {                         /* soft rain: a sine, odd harmonics creeping in */
        draw = 0;
        spec_im[1] = -1.f;
        for (h = 3; h <= 1023; h += 2)
            spec_im[h] = -s1_exp2(((float)(h - 1) / 4.f) * s1_log2(s1_max(t, 1e-4f))) / s1_exp2(1.5f * s1_log2((float)h));
        break;
    }
    case WTK_NEON: {                          /* a saw through a sweeping comb */
        draw = 0;
        for (h = 1; h <= 1023; h++) {
            float c = s1_cos01((float)h * (0.02f + 0.18f * t));
            spec_im[h] = -(1.f / (float)h) * (0.35f + 0.65f * c * c);
        }
        break;
    }
    case WTK_SIRENE: {                        /* a voice: a e i o u along the table */
        float v = t * 4.f;
        int a = (int)v;
        float fr = v - (float)a;
        float F[3];
        draw = 0;
        if (a > 3) {
            a = 3;
            fr = 1.f;
        }
        for (i = 0; i < 3; i++)
            F[i] = s1_lerp(VOWEL_F[a][i], VOWEL_F[a + 1][i], fr) / 110.f;   /* in harmonics of A2 */
        for (h = 1; h <= 1023; h++) {
            float g = gauss((float)h, F[0], 1.2f) + 0.6f * gauss((float)h, F[1], 1.8f) + 0.3f * gauss((float)h, F[2], 2.5f);
            spec_im[h] = -(0.05f + g) / s1_sqrt((float)h);
        }
        break;
    }
    case WTK_MINHOCAO: {                      /* a growl: a saw with a resonant peak climbing */
        float c = 2.f + 70.f * t * t;
        draw = 0;
        for (h = 1; h <= 1023; h++)
            spec_im[h] = -(1.f / (float)h) * (0.3f + 3.f * gauss((float)h, c, 1.5f + 2.f * t));
        break;
    }
    default:
        break;
    }
    if (!draw)
        return;
    for (i = 0; i < WT_GEN; i++) {           /* the time-domain shapes */
        float x = (float)i / (float)WT_GEN, s = 0;
        switch (k) {
        case WTK_BASIC: {                     /* sine -> triangle -> saw -> square */
            float sn = s1_sin01(x), tr = 1.f - 4.f * s1_abs(x - 0.5f) - 0.f, sw = 1.f - 2.f * x, sq = x < 0.5f ? 1.f : -1.f;
            float u = t * 3.f;
            tr = x < 0.25f ? 4.f * x : x < 0.75f ? 2.f - 4.f * x : 4.f * x - 4.f;
            s = u < 1.f ? s1_lerp(sn, tr, u) : u < 2.f ? s1_lerp(tr, sw, u - 1.f) : s1_lerp(sw, sq, u - 2.f);
            break;
        }
        case WTK_VIADUTO: {                   /* hard sync: a saw restarted by the master, its ratio rising */
            float r = 1.f + 7.f * t;
            s = 1.f - 2.f * frac01(x * r);
            s *= 1.f - 0.6f * x;              /* (a slope over the cycle softens the jump) */
            break;
        }
        case WTK_METRO: {                     /* pulse width 50 % -> 3 % */
            float w = 0.5f - 0.47f * t;
            s = x < w ? 1.f : -1.f;
            break;
        }
        case WTK_ACO: {                       /* steel: two-operator FM, the index rising */
            float I = 0.3f + 9.f * t;
            s = s1_sin01(x + I / S1_TAU * s1_sin01(3.f * x));
            break;
        }
        case WTK_ASFALTO: {                   /* a sine folded harder and harder */
            float g = 1.f + 9.f * t;
            s = s1_sin01(0.25f * g * s1_sin01(x));
            break;
        }
        case WTK_BUZINA: {                    /* a horn: square and its octave, driven */
            float sq = x < 0.5f ? 1.f : -1.f, sq2 = frac01(x * 2.f + 0.25f) < 0.5f ? 1.f : -1.f;
            s = s1_tanh((1.f + 4.f * t) * (sq + (0.2f + 0.5f * t) * sq2));
            break;
        }
        case WTK_PAULISTA: {                  /* a saw, bit by bit coarser */
            float levels = s1_exp2(7.f - 6.f * t), sw = 1.f - 2.f * x;
            s = s1_floor(sw * levels + 0.5f) / levels;
            break;
        }
        case WTK_TIETE: {                     /* the river: a sine growing a dirty low end */
            float sn = s1_sin01(x), sn2 = s1_sin01(2.f * x + 0.1f);
            s = s1_tanh((1.f + 5.f * t) * (sn + 0.35f * t * sn2)) ;
            break;
        }
        default:
            s = s1_sin01(x);
            break;
        }
        fft_re[i] = s;
        fft_im[i] = 0;
    }
    s1_fft(fft_re, fft_im, WT_GEN, 0);
    for (h = 1; h <= 1024; h++) {
        spec_re[h] = fft_re[h] * (2.f / WT_GEN);
        spec_im[h] = fft_im[h] * (2.f / WT_GEN);
    }
}

/* writes frame f of table k at every level into dst (WT_FRAME_FLOATS), loudness-normalised: every frame
 * as loud as a saw (RMS 0.55), its peak never above 1.6, so a sweep through a table keeps its level */
static void wt_build_frame(int k, int f, float *dst)
{
    int L, h, i, off = 0;
    float peak = 0, g, ms = 0;
    wt_frame_spectrum(k, (float)f / (float)(WT_FRAMES - 1));
    for (L = 0; L < WT_LEVELS; L++) {
        int n = WT_SIZE[L], hm = WT_HMAX[L];
        for (i = 0; i < n; i++)
            fft_re[i] = fft_im[i] = 0;
        for (h = 1; h <= hm && h < n / 2; h++) {   /* a real signal: X[n - h] = conj(X[h]) */
            fft_re[h] = spec_re[h] * 0.5f;
            fft_im[h] = spec_im[h] * 0.5f;
            fft_re[n - h] = spec_re[h] * 0.5f;
            fft_im[n - h] = -spec_im[h] * 0.5f;
        }
        s1_fft(fft_re, fft_im, n, 1);
        for (i = 0; i < n; i++)
            dst[off + i] = fft_re[i];
        if (L == 0)
            for (i = 0; i < n; i++) {
                peak = s1_max(peak, s1_abs(dst[i]));
                ms += dst[i] * dst[i];
            }
        off += n;
    }
    ms = s1_sqrt(ms / (float)WT_SIZE[0]);
    g = peak > 1e-6f ? s1_min(0.55f / s1_max(ms, 1e-6f), 1.6f / peak) : 0;
    for (i = 0; i < WT_FRAME_FLOATS; i++)
        dst[i] *= g;
}

/* the table k, built on first use; 0 if the arena is full (then the oscillator plays BASICA) */
static const float *wt_table(int k)
{
    int f;
    if (k < 0 || k >= WTK_COUNT)
        k = 0;
    if (wt_slot[k] < 0) {
        if (wt_used >= WT_MAX_LOADED)
            return k ? wt_table(0) : 0;
        wt_slot[k] = (signed char)wt_used++;
        for (f = 0; f < WT_FRAMES; f++)
            wt_build_frame(k, f, &wt_arena[wt_slot[k]][f * WT_FRAME_FLOATS]);
    }
    return wt_arena[wt_slot[k]];
}
static void wt_init(void)
{
    int k;
    for (k = 0; k < WTK_COUNT; k++)
        wt_slot[k] = -1;
    wt_used = 0;
}
/* every table, built now (a plugin does this once, before its audio runs) */
static void wt_build_all(void)
{
    int k;
    for (k = 0; k < WTK_COUNT; k++)
        wt_table(k);
}

/* the level whose harmonics all sit under Nyquist for an increment of inc cycles a sample */
static inline int wt_level(float inc)
{
    float hmax = 0.5f / s1_max(inc, 1e-7f);
    int L = 0;
    while (L < WT_LEVELS - 1 && (float)WT_HMAX[L] > hmax)
        L++;
    return L;
}
static const int WT_OFF[WT_LEVELS] = {0, 2048, 3072, 3584, 3840, 4096, 4352, 4608, 4864, 5120};

/* one sample: frames fa and fa + 1 crossfaded by ff, level L, phase ph (cycles) */
static inline float wt_read(const float *tab, int fa, float ff, int L, float ph)
{
    const float *a = tab + fa * WT_FRAME_FLOATS + WT_OFF[L], *b = a + (fa < WT_FRAMES - 1 ? WT_FRAME_FLOATS : 0);
    int n = WT_SIZE[L];
    float p = ph * (float)n;
    int i = (int)p, j;
    float fr = p - (float)i, va, vb;
    i &= n - 1;
    j = (i + 1) & (n - 1);
    va = a[i] + (a[j] - a[i]) * fr;
    vb = b[i] + (b[j] - b[i]) * fr;
    return va + (vb - va) * ff;
}
