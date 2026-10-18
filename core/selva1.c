/* Selva1 · the synth engine. Portable C (float, no libc), one instance: built freestanding for WebAssembly (the web
 * panel), and on the host for tests (later the plugin wraps the same file).
 *
 * Per voice:  OSC A + OSC B + OSC C (each WAVETABLE / ANALOG / FM / NOISE, up to 8 unison voices) + SUB + NOISE
 *             -> FILTER (Moog-style zero-delay ladder 12/24 dB or a clean SVF; LP / BP / HP), stereo
 *             -> AMP (ENV 1, velocity)
 * then the voices' sum -> FX (DIST, CHORUS, DELAY, REVERB, COMP) -> LEVEL, PAN -> a soft limiter.
 * Modulation: 3 envelopes, 3 LFOs, velocity, key, mod wheel, aftertouch, bend, 4 macros, a random value per note
 * and the sequencer's four mod lanes, routed by a 16-slot matrix onto 30 destinations, every control block
 * (32 samples). The sequencer (16 steps, 8 lanes) and the arpeggiator run here too, on the engine's own clock.
 * Copyright (C) 2026 DeAbrew (deabrewmusic.com.br), written with Claude. */
#include "s1_math.h"
#include "s1_params.h"

#ifndef S1_HOST
void *memset(void *d, int c, unsigned long n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--)
        *p++ = *q++;
    return d;
}
#else
#include <string.h>
#endif

#include "s1_wt.c"

#define S1_EXPORT __attribute__((visibility("default")))
S1_EXPORT void s1_note_off(int note);
#define BLK 32                              /* the control block */
#define MAXV 16                             /* voices */
#define MAXU 8                              /* unison voices per oscillator */
#define MAXSR 96000

/* ------------------------------------------------------------------- parameters --- */
typedef struct {
    const char *key, *label;
    unsigned char kind;
    float min, max, def;
    const char *const *opts;
} pdesc_t;
#define S1_P_DESC(id, key, label, kind, mn, mx, def, opts) {key, label, kind, mn, mx, def, opts},
static const pdesc_t PD[S1_NPARAM] = {S1_PARAMS(S1_P_DESC)};
#define S1_D_TAB(id, p, name) {p, name},
static const struct { int p; const char *name; } DST[S1_NDST] = {S1_DESTS(S1_D_TAB)};
#define S1_S_TAB(id, name) name,
static const char *const SRC_NAME[S1_NSRC] = {S1_SOURCES(S1_S_TAB)};

static float pv[S1_NALL];                   /* values */
static float pn[S1_NPARAM];                 /* normalised 0..1 */
static float sr = 48000.f, isr = 1.f / 48000.f;

static float norm_of(int i, float v)
{
    const pdesc_t *d = &PD[i];
    if (d->max <= d->min)
        return 0;
    if (d->kind == K_EXP)
        return s1_clamp(s1_log2(s1_max(v, d->min) / d->min) / s1_log2(d->max / d->min), 0, 1);
    return s1_clamp((v - d->min) / (d->max - d->min), 0, 1);
}
static float val_of(int i, float n)
{
    const pdesc_t *d = &PD[i];
    n = s1_clamp(n, 0, 1);
    if (d->kind == K_EXP)
        return d->min * s1_exp2(n * s1_log2(d->max / d->min));
    return d->min + n * (d->max - d->min);
}
#define PI_(p) ((int)s1_floor(pv[p] + 0.5f)) /* an integer / enum / bool parameter (negatives too) */

/* ------------------------------------------------------------------- the matrix --- */
typedef struct { unsigned char src, dst; float amt; } slot_t;
static slot_t mods[S1_NMOD];
static int nmods;                           /* active slots, compacted */
static unsigned char dst_used[S1_NDST];
static void matrix_rebuild(void)
{
    int k;
    nmods = 0;
    for (k = 0; k < S1_NDST; k++)
        dst_used[k] = 0;
    for (k = 0; k < S1_NMOD; k++) {
        int s = PI_(P_MOD1_SRC + k * 3), d = PI_(P_MOD1_DST + k * 3);
        float a = pv[P_MOD1_AMT + k * 3];
        if (s > 0 && s < S1_NSRC && d > 0 && d < S1_NDST && a != 0) {
            mods[nmods].src = (unsigned char)s;
            mods[nmods].dst = (unsigned char)d;
            mods[nmods].amt = a;
            nmods++;
            dst_used[d] = 1;
        }
    }
}

/* ------------------------------------------------------------------- voices --- */
typedef struct {
    float v;                                /* value */
    int stage;                              /* 0 idle 1 attack 2 decay 3 sustain 4 release */
} env_t;
typedef struct {
    float ph, val, held, target, prev;      /* phase (cycles), value, the S&H / drift values */
    int done;
} lfo_t;
typedef struct { float s[4]; } ladder_t;
typedef struct { float ic1, ic2, ic3, ic4; } svf_t;

typedef struct {
    int active, gate, note, age, fade;      /* fade: samples left of a steal fade, 0 = none */
    float vel, pitch, target, rnd;          /* pitch in semitones (glides), rnd: RANDOM source */
    float acc, acc_env, slide;              /* the sequencer's ACCENT (this note, its sweep) and SLIDE time */
    float ph[3][MAXU], ph2[3][MAXU];        /* phases: oscillator, FM modulator */
    uint32_t nst[3];                        /* the oscillators' noise */
    float subph;
    uint32_t nzs;                           /* the noise section */
    float pk[7], nlp, nhp, brown, held_n, hold_t, cr_env;   /* pink, color, brown, digital and vinyl states */
    float metal[6];
    env_t env[3];
    lfo_t lfo[3];
    ladder_t lad[2];
    svf_t svf[2];
    float G0, g0;                           /* the filter's coefficient last block (interpolated) */
    float amp_prev;
    float src[S1_NSRC];
    float off[S1_NDST];
} voice_t;

static voice_t V[MAXV];
static int vage;
static float glob_lfo_ph[3], glob_lfo_val[3];
static float modwh, after, bend;            /* 0..1, 0..1, -1..1 */
static float seqmod[4];                     /* the sequencer's mod lanes now */
static int newest = -1;                     /* the last voice started (the global modulation context) */

static void env_trigger(env_t *e, int legato)
{
    if (!legato || e->stage == 0 || e->stage == 4)
        e->stage = 1;
}
static void env_release(env_t *e)
{
    if (e->stage && e->stage != 4)
        e->stage = 4;
}
/* advances an envelope by n samples; returns the value. A attack (linear, to 1), D decay and R release
 * (exponential, ~99 % of the way in their time) */
static float env_run(env_t *e, int k, float n)
{
    float a = pv[P_ENV1_A + k * 4], d = pv[P_ENV1_D + k * 4], s = pv[P_ENV1_S + k * 4], r = pv[P_ENV1_R + k * 4];
    switch (e->stage) {
    case 1:
        e->v += n / (a * sr);
        if (e->v >= 1.f) {
            e->v = 1.f;
            e->stage = 2;
        }
        break;
    case 2:
    case 3:
        e->v = s + (e->v - s) * s1_exp(-4.6f * n / (d * sr));
        break;
    case 4:
        e->v *= s1_exp(-4.6f * n / (r * sr));
        if (e->v < 1e-4f) {
            e->v = 0;
            e->stage = 0;
        }
        break;
    default:
        e->v = 0;
        break;
    }
    return e->v;
}

static float bpm(void) { return s1_clamp(pv[P_BPM], 20, 300); }
/* the LFO's rate (Hz): free, or synced to the tempo; rmod the RATE modulation (normalised offset) */
static float lfo_hz(int k, float rmod)
{
    int sy = PI_(P_LFO1_SYNC + k * 5);
    if (sy > 0 && sy < 14)
        return bpm() / 60.f / LSYNC_BEATS[sy];
    return val_of(P_LFO1_RATE + k * 5, pn[P_LFO1_RATE + k * 5] + rmod);
}
static float lfo_shape(lfo_t *l, int shape, float ph, int wrapped, uint32_t *rs)
{
    switch (shape) {
    case 0: return s1_sin01(ph);
    case 1: return ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph;
    case 2: return 2.f * ph - 1.f;
    case 3: return 1.f - 2.f * ph;
    case 4: return ph < 0.5f ? 1.f : -1.f;
    case 5:
        if (wrapped)
            l->held = s1_randb(rs);
        return l->held;
    default:                                /* DRIFT: smooth random, a new target each cycle */
        if (wrapped) {
            l->prev = l->target;
            l->target = s1_randb(rs);
        }
        {
            float t = ph * ph * (3.f - 2.f * ph);
            return s1_lerp(l->prev, l->target, t);
        }
    }
}
static uint32_t lfo_seed = 0x1234567u;
/* one LFO for one block: FREE uses the shared phase, RETRIG / ONE SHOT the voice's own */
static float lfo_run(lfo_t *l, int k, float rmod, int global)
{
    int mode = PI_(P_LFO1_MODE + k * 5), shape = PI_(P_LFO1_SHAPE + k * 5), wrapped = 0;
    float inc = lfo_hz(k, rmod) * (float)BLK * isr, v, sm;
    if (!global && mode == 0) {
        /* (a voice in FREE mode follows the shared LFO, its rate modulation aside) */
        l->ph = glob_lfo_ph[k];
        v = glob_lfo_val[k];
        l->val = v;
        return v;
    }
    if (!(mode == 2 && l->done)) {
        l->ph += inc;
        if (l->ph >= 1.f) {
            if (mode == 2) {
                l->ph = 0.999f;
                l->done = 1;
            } else {
                l->ph -= s1_floor(l->ph);
                wrapped = 1;
            }
        }
    }
    v = lfo_shape(l, shape, l->ph, wrapped, &lfo_seed);
    sm = pv[P_LFO1_SMOOTH + k * 5];
    if (sm > 0) {                            /* SMOOTH: a one-pole lag, up to half a cycle at the top */
        float period = (float)BLK * isr / s1_max(inc, 1e-7f), tau = 1e-4f + sm * sm * 0.5f * period;
        v = l->val + (v - l->val) * (1.f - s1_exp(-(float)BLK * isr / tau));
    }
    l->val = v;
    return v;
}

/* ------------------------------------------------------------------- oscillators --- */
static inline float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.f;
    }
    if (t > 1.f - dt) {
        t = (t - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0;
}
static inline float osc_saw(float ph, float dt) { return 2.f * ph - 1.f - blep(ph, dt); }
static inline float osc_pulse(float ph, float dt, float pw)
{
    float p2 = ph + 1.f - pw;
    p2 -= s1_floor(p2);
    return (osc_saw(ph, dt) - osc_saw(p2, dt)) * 0.5f;
}
static inline float osc_tri(float ph) { return ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph; }

/* the oscillator settings of one block, after modulation */
typedef struct {
    int on, mode, uni, wave, level_zero;
    const float *tab;
    int fa;
    float ff, pos, ratio, level, pan, width;
    float inc[MAXU], gl[MAXU], gr[MAXU];
    int L[MAXU];
} oscblk_t;

static float unison_spread(int u, int n) { return n < 2 ? 0.f : (float)u / (float)(n - 1) * 2.f - 1.f; }

static void osc_setup(voice_t *v, int o, oscblk_t *b, float pitch_semis)
{
    int base = P_OSCA_ON + o * 14, u;
    float det, pan, lvl, width, pitch, gnorm;
    b->on = PI_(base);
    if (!b->on)
        return;
    b->mode = PI_(base + 1);
    b->uni = PI_(base + 9);
    if (b->uni < 1)
        b->uni = 1;
    if (b->uni > MAXU)
        b->uni = MAXU;
    b->wave = PI_(base + 4);
    b->ratio = RATIO_V[PI_(base + 5) % 12];
    b->pos = val_of(base + 3, pn[base + 3] + v->off[DST_A_POS + o * 5]);
    det = val_of(base + 10, pn[base + 10] + v->off[DST_A_DET + o * 5]);
    pan = val_of(base + 12, pn[base + 12] + v->off[DST_A_PAN + o * 5]);
    lvl = val_of(base + 13, pn[base + 13] + v->off[DST_A_LEVEL + o * 5]);
    width = pv[base + 11];
    b->level = lvl * lvl;                   /* (a level knob feels right squared) */
    b->level_zero = b->level < 1e-6f;
    pitch = pitch_semis + (float)PI_(base + 6) * 12.f + (float)PI_(base + 7) + pv[base + 8] * 0.01f +
            v->off[DST_A_PITCH + o * 5] * 24.f;
    gnorm = 1.f / s1_sqrt((float)b->uni);
    det = det * det * 0.6f;                 /* DETUNE: up to +-0.6 semitone at the edges */
    if (b->mode == 0) {
        float fp = b->pos * (float)(WT_FRAMES - 1);
        b->tab = wt_table(PI_(base + 2));
        b->fa = (int)fp;
        if (b->fa > WT_FRAMES - 2)
            b->fa = WT_FRAMES - 2;
        b->ff = fp - (float)b->fa;
    }
    for (u = 0; u < b->uni; u++) {
        float sp = unison_spread(u, b->uni), p = s1_clamp(pan + sp * width, -1.f, 1.f);
        float f = 440.f * s1_exp2((pitch + sp * det - 69.f) / 12.f);
        b->inc[u] = s1_clamp(f * isr, 0.f, 0.45f);
        b->L[u] = wt_level(b->inc[u] * (b->mode == 2 ? 1.f + b->pos * 4.f : 1.f));
        /* equal-power pan */
        b->gl[u] = s1_cos01((p + 1.f) * 0.125f) * gnorm;
        b->gr[u] = s1_sin01((p + 1.f) * 0.125f) * gnorm;
    }
}

/* adds one oscillator's block into l, r (n samples) */
static void osc_render(voice_t *v, int o, const oscblk_t *b, float *l, float *r, int n)
{
    int u, i;
    for (u = 0; u < b->uni; u++) {
        float ph = v->ph[o][u], ph2 = v->ph2[o][u], inc = b->inc[u], gl = b->gl[u] * b->level, gr = b->gr[u] * b->level;
        switch (b->mode) {
        case 0: {                           /* WAVETABLE */
            const float *t = b->tab;
            int fa = b->fa, L = b->L[u];
            float ff = b->ff;
            for (i = 0; i < n; i++) {
                float s = wt_read(t, fa, ff, L, ph);
                ph += inc;
                if (ph >= 1.f)
                    ph -= 1.f;
                l[i] += s * gl;
                r[i] += s * gr;
            }
            break;
        }
        case 1: {                           /* ANALOG */
            int w = b->wave;
            float pw = 0.5f - 0.47f * b->pos, drive = 1.f + 5.f * b->pos;
            if (w == 1)
                pw = 0.5f - 0.2f * b->pos;
            for (i = 0; i < n; i++) {
                float s;
                switch (w) {
                case 0: s = osc_saw(ph, inc); break;
                case 1:
                case 2: s = osc_pulse(ph, inc, pw); break;
                case 3: s = osc_tri(ph); break;
                case 4: s = s1_sin01(ph); break;
                default: s = 0.5f * (osc_tri(ph) + osc_saw(ph, inc)); break;   /* SHARK: triangle-saw */
                }
                if (w == 0 || w >= 3)       /* POS = drive on the waves without a width */
                    s = b->pos > 0.001f ? s1_tanh(s * drive) / s1_tanh(drive) : s;
                ph += inc;
                if (ph >= 1.f)
                    ph -= 1.f;
                l[i] += s * gl;
                r[i] += s * gr;
            }
            break;
        }
        case 2: {                           /* FM: a sine modulated by a sine at RATIO, POS = the index */
            float I = b->pos * b->pos * 1.6f, inc2 = inc * b->ratio;
            for (i = 0; i < n; i++) {
                float m = s1_sin01(ph2), s = s1_sin01(ph + I * m);
                ph += inc;
                if (ph >= 1.f)
                    ph -= 1.f;
                ph2 += inc2;
                ph2 -= s1_floor(ph2);
                l[i] += s * gl;
                r[i] += s * gr;
            }
            break;
        }
        default: {                          /* NOISE: white to dark by POS */
            float c = 1.f - b->pos * 0.97f;
            for (i = 0; i < n; i++) {
                float w = s1_randb(&v->nst[o]);
                ph2 += (w - ph2) * c;       /* (ph2: the low-pass state) */
                l[i] += ph2 * gl;
                r[i] += ph2 * gr;
            }
            break;
        }
        }
        v->ph[o][u] = ph;
        v->ph2[o][u] = ph2;
    }
}

/* the noise section: one sample of the chosen colour */
static inline float noise_sample(voice_t *v, int type)
{
    float w = s1_randb(&v->nzs);
    switch (type) {
    case 1: {                               /* pink (Paul Kellet's economy filter) */
        v->pk[0] = 0.99765f * v->pk[0] + w * 0.0990460f;
        v->pk[1] = 0.96300f * v->pk[1] + w * 0.2965164f;
        v->pk[2] = 0.57000f * v->pk[2] + w * 1.0526913f;
        return (v->pk[0] + v->pk[1] + v->pk[2] + w * 0.1848f) * 0.25f;
    }
    case 2:                                 /* brown */
        v->brown = (v->brown + w * 0.04f) * 0.996f;
        return v->brown * 2.5f;
    case 3: {                               /* industrial: metal (six inharmonic squares) under dust */
        static const float MF[6] = {317.f, 449.f, 571.f, 778.f, 1104.f, 1471.f};
        float m = 0;
        int k;
        for (k = 0; k < 6; k++) {
            v->metal[k] += MF[k] * isr;
            if (v->metal[k] >= 1.f)
                v->metal[k] -= 1.f;
            m += v->metal[k] < 0.5f ? 1.f : -1.f;
        }
        v->nhp += (m - v->nhp) * 0.3f;
        m -= v->nhp;                        /* (a high-pass: the metal's shimmer) */
        if (s1_randf(&v->nzs) < 0.0008f)
            v->cr_env = 1.f;
        v->cr_env *= 0.995f;
        return 0.18f * m * (0.4f + 0.6f * s1_abs(w)) + v->cr_env * w * 0.8f;
    }
    case 4:                                 /* digital: 1-bit noise, held */
        v->hold_t += 0.15f;
        if (v->hold_t >= 1.f) {
            v->hold_t -= 1.f;
            v->held_n = (s1_rand(&v->nzs) & 1) ? 0.6f : -0.6f;
        }
        return v->held_n;
    case 5: {                               /* vinyl: crackle and a soft hiss */
        float c = 0;
        if (s1_randf(&v->nzs) < 0.0012f)
            c = s1_randb(&v->nzs) * 1.4f;
        v->nlp += (w - v->nlp) * 0.08f;
        return c + v->nlp * 0.5f;
    }
    default:
        return w * 0.7f;
    }
}

/* ------------------------------------------------------------------- filters --- */
/* the ladder: four zero-delay one-poles in a loop solved without delay (Zavalishin), a tanh at its input */
static inline float ladder_tick(ladder_t *f, float x, float G, float k, int mode, int slope)
{
    float G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
    float b1 = (1.f - G) * f->s[0], b2 = (1.f - G) * f->s[1], b3 = (1.f - G) * f->s[2], b4 = (1.f - G) * f->s[3];
    float S = G3 * b1 + G2 * b2 + G * b3 + b4;
    float u = s1_tanh((x - k * S) / (1.f + k * G4));
    float y1 = G * u + b1, y2 = G * y1 + b2, y3 = G * y2 + b3, y4 = G * y3 + b4;
    f->s[0] = 2.f * y1 - f->s[0];
    f->s[1] = 2.f * y2 - f->s[1];
    f->s[2] = 2.f * y3 - f->s[2];
    f->s[3] = 2.f * y4 - f->s[3];
    if (mode == 0)
        return slope ? y4 : y2;
    if (mode == 1)
        return slope ? 4.f * (y2 - 2.f * y3 + y4) : 2.f * (y1 - y2);
    return slope ? u - 4.f * y1 + 6.f * y2 - 4.f * y3 + y4 : u - 2.f * y1 + y2;
}
/* the clean state-variable filter (Simper), 12 dB, or two in a row for 24 */
static inline float svf_tick(svf_t *f, float x, float g, float k, int mode, int slope)
{
    float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    float v3 = x - f->ic2, v1 = a1 * f->ic1 + a2 * v3, v2 = f->ic2 + a2 * f->ic1 + a3 * v3, y;
    f->ic1 = 2.f * v1 - f->ic1;
    f->ic2 = 2.f * v2 - f->ic2;
    y = mode == 0 ? v2 : mode == 1 ? v1 * k : x - k * v1 - v2;
    if (slope) {
        float w3 = y - f->ic4, w1 = a1 * f->ic3 + a2 * w3, w2 = f->ic4 + a2 * f->ic3 + a3 * w3;
        f->ic3 = 2.f * w1 - f->ic3;
        f->ic4 = 2.f * w2 - f->ic4;
        y = mode == 0 ? w2 : mode == 1 ? w1 * k : y - k * w1 - w2;
    }
    return y;
}

/* ------------------------------------------------------------------- one voice, one block --- */
static float vbuf_fl[BLK], vbuf_fr[BLK], vbuf_dl[BLK], vbuf_dr[BLK];

static void voice_sources(voice_t *v)
{
    int k;
    float *s = v->src;
    s[SRC_NONE] = 0;
    s[SRC_VELO] = v->vel;
    s[SRC_KEY] = ((float)v->note - 60.f) / 48.f;
    s[SRC_MODWH] = modwh;
    s[SRC_AFTER] = after;
    s[SRC_BEND] = bend;
    for (k = 0; k < 4; k++) {
        s[SRC_MAC1 + k] = pv[P_MAC1 + k];
        s[SRC_SEQ1 + k] = seqmod[k];
    }
    s[SRC_RAND] = v->rnd;
    s[SRC_ACCENT] = v->acc_env * pv[P_SEQ_ACC];
}
static void voice_offsets(voice_t *v)
{
    int k;
    for (k = 0; k < S1_NDST; k++)
        v->off[k] = 0;
    for (k = 0; k < nmods; k++)
        v->off[mods[k].dst] += mods[k].amt * v->src[mods[k].src];
}

static void voice_block(voice_t *v, float *outl, float *outr, int n)
{
    int k, i, o, ftype, fmode, fslope, fon, route[5];
    oscblk_t ob[3];
    float e1prev, e1, cut, res, drv, G, g, kk, gain, amp, apan, gl_, gr_, glide, sub_lvl, nz_lvl, nz_col, key;
    /* modulation sources first: the envelopes 2 and 3, the LFOs (their rates modulated by last block's offsets) */
    v->acc_env *= s1_exp(-(float)n / (0.16f * sr));   /* the accent sweep fades in ~0.5 s */
    voice_sources(v);
    e1prev = v->env[0].v;
    v->src[SRC_ENV2] = env_run(&v->env[1], 1, (float)n);
    v->src[SRC_ENV3] = env_run(&v->env[2], 2, (float)n);
    for (k = 0; k < 3; k++)
        v->src[SRC_LFO1 + k] = lfo_run(&v->lfo[k], k, v->off[DST_L1RATE + k], 0);
    v->src[SRC_ENV1] = e1prev;
    voice_offsets(v);
    /* pitch: glide toward the note */
    glide = v->slide > 0 ? v->slide : pv[P_GLIDE];
    if (glide > 0.0011f)
        v->pitch = v->target + (v->pitch - v->target) * s1_exp(-(float)n / (glide * sr * 0.4f));
    else
        v->pitch = v->target;
    {
        float p = v->pitch + v->off[DST_PITCH] * 24.f + bend * pv[P_BEND] + pv[P_TUNE] * 0.01f;
        for (o = 0; o < 3; o++)
            osc_setup(v, o, &ob[o], p);
    }
    for (i = 0; i < n; i++)
        vbuf_fl[i] = vbuf_fr[i] = vbuf_dl[i] = vbuf_dr[i] = 0;
    route[0] = PI_(P_FLT_A);
    route[1] = PI_(P_FLT_B);
    route[2] = PI_(P_FLT_C);
    route[3] = PI_(P_FLT_SUB);
    route[4] = PI_(P_FLT_NOISE);
    fon = PI_(P_FLT_ON);
    for (o = 0; o < 3; o++)
        if (ob[o].on && !ob[o].level_zero)
            osc_render(v, o, &ob[o], route[o] && fon ? vbuf_fl : vbuf_dl, route[o] && fon ? vbuf_fr : vbuf_dr, n);
    if (PI_(P_SUB_ON)) {                    /* SUB: under the note, not under an oscillator */
        float lv = val_of(P_SUB_LEVEL, pn[P_SUB_LEVEL] + v->off[DST_SUB_LEVEL]);
        float f = 440.f * s1_exp2((v->pitch + bend * pv[P_BEND] - 69.f) / 12.f - (float)(1 + PI_(P_SUB_OCT)));
        float inc = s1_clamp(f * isr, 0, 0.45f), ph = v->subph, *dl = route[3] && fon ? vbuf_fl : vbuf_dl,
              *dr = route[3] && fon ? vbuf_fr : vbuf_dr;
        int sq = PI_(P_SUB_WAVE);
        sub_lvl = lv * lv * 0.8f;
        for (i = 0; i < n; i++) {
            float s = sq ? osc_pulse(ph, inc, 0.5f) : s1_sin01(ph);
            ph += inc;
            if (ph >= 1.f)
                ph -= 1.f;
            dl[i] += s * sub_lvl;
            dr[i] += s * sub_lvl;
        }
        v->subph = ph;
    }
    if (PI_(P_NOISE_ON)) {                  /* NOISE: its type, a tilt by COLOR */
        int t = PI_(P_NOISE_TYPE);
        float lv = val_of(P_NOISE_LEVEL, pn[P_NOISE_LEVEL] + v->off[DST_NOISE_LEVEL]);
        float col = val_of(P_NOISE_COLOR, pn[P_NOISE_COLOR] + v->off[DST_NOISE_COLOR]);
        float c = 0.05f + 0.9f * (col * 0.5f + 0.5f), *dl = route[4] && fon ? vbuf_fl : vbuf_dl,
              *dr = route[4] && fon ? vbuf_fr : vbuf_dr;
        nz_lvl = lv * lv * 0.9f;
        nz_col = col;
        for (i = 0; i < n; i++) {
            float s = noise_sample(v, t);
            v->nlp += (s - v->nlp) * c;     /* (c: darker below 0, brighter above) */
            s = col < 0 ? v->nlp : s * (0.5f + 0.5f * col) + v->nlp * (0.5f - 0.5f * col);
            dl[i] += s * nz_lvl;
            dr[i] += s * nz_lvl;
        }
        (void)nz_col;
    }
    /* FILTER: the cutoff with its modulation and the keyboard, interpolated over the block */
    ftype = PI_(P_FLT_TYPE);
    fmode = PI_(P_FLT_MODE);
    fslope = PI_(P_FLT_SLOPE);
    key = pv[P_FLT_KEY] * (v->pitch - 60.f) / 12.f;
    res = val_of(P_FLT_RES, pn[P_FLT_RES] + v->off[DST_RES]);
    cut = val_of(P_FLT_CUT, pn[P_FLT_CUT] + v->off[DST_CUT] + pv[P_SEQ_ACC] * v->acc_env * (0.14f + 0.22f * res)) *
          s1_exp2(key);
    cut = s1_clamp(cut, 8.f, 0.45f * sr);
    drv = val_of(P_FLT_DRIVE, pn[P_FLT_DRIVE] + v->off[DST_FDRIVE]);
    g = s1_tanpi(cut * isr);
    G = g / (1.f + g);
    if (v->G0 <= 0) {
        v->G0 = G;
        v->g0 = g;
    }
    if (fon) {
        float dg = 1.f + drv * drv * 8.f, comp, G0 = v->G0, g0 = v->g0;
        if (ftype == 0) {
            kk = res * res * 4.3f;           /* self-oscillates a little before the end */
            comp = fmode == 0 ? 1.f + 0.5f * kk : 1.f;
            for (i = 0; i < n; i++) {
                float t = (float)(i + 1) / (float)n, Gi = G0 + (G - G0) * t;
                vbuf_fl[i] = ladder_tick(&v->lad[0], vbuf_fl[i] * dg * 0.5f * comp, Gi, kk, fmode, fslope) * 2.f / s1_sqrt(dg);
                vbuf_fr[i] = ladder_tick(&v->lad[1], vbuf_fr[i] * dg * 0.5f * comp, Gi, kk, fmode, fslope) * 2.f / s1_sqrt(dg);
            }
        } else {
            kk = 2.f - 1.96f * res;
            for (i = 0; i < n; i++) {
                float t = (float)(i + 1) / (float)n, gi = g0 + (g - g0) * t;
                float xl = s1_tanh(vbuf_fl[i] * dg * 0.5f) * 2.f / s1_sqrt(dg), xr = s1_tanh(vbuf_fr[i] * dg * 0.5f) * 2.f / s1_sqrt(dg);
                vbuf_fl[i] = svf_tick(&v->svf[0], xl, gi, kk, fmode, fslope);
                vbuf_fr[i] = svf_tick(&v->svf[1], xr, gi, kk, fmode, fslope);
            }
        }
    }
    v->G0 = G;
    v->g0 = g;
    /* AMP: ENV 1 sample by sample, velocity, LEVEL and PAN modulation */
    {
        float vs = pv[P_VELSENS], velg = (1.f - vs) + vs * v->vel * v->vel;
        float lvl = val_of(P_AMP_LEVEL, pn[P_AMP_LEVEL] + v->off[DST_AMP]);
        apan = val_of(P_AMP_PAN, pn[P_AMP_PAN] + v->off[DST_APAN]);
        gain = velg * lvl * lvl * 2.6f;            /* (+8 dB: one saw at INIT peaks near -7 dBFS) */
        gain *= 1.f + pv[P_SEQ_ACC] * v->acc;          /* ACCENT: up to +6 dB */
        gl_ = s1_cos01((apan + 1.f) * 0.125f) * 1.41421f;
        gr_ = s1_sin01((apan + 1.f) * 0.125f) * 1.41421f;
    }
    for (i = 0; i < n; i++) {
        float fadeg = 1.f;
        amp = env_run(&v->env[0], 0, 1.f);
        if (v->fade) {
            fadeg = (float)v->fade / 256.f;
            if (--v->fade == 0) {
                v->active = 0;
                v->env[0].stage = 0;
            }
        }
        amp *= gain * fadeg;
        outl[i] += (vbuf_fl[i] + vbuf_dl[i]) * amp * gl_;
        outr[i] += (vbuf_fr[i] + vbuf_dr[i]) * amp * gr_;
    }
    v->src[SRC_ENV1] = v->env[0].v;
    if (v->env[0].stage == 0 && !v->fade)
        v->active = 0;
}

/* ------------------------------------------------------------------- FX --- */
#define DLY_MAX (MAXSR * 2 + 8)
static float dly_l[DLY_MAX], dly_r[DLY_MAX];
static int dly_w;
static float dly_time_s, dly_lp_l, dly_lp_r;
#define CHO_MAX 4096
static float cho_l[CHO_MAX], cho_r[CHO_MAX];
static int cho_w;
static float cho_ph;
/* reverb: an 8-line feedback delay network, 4 diffusers in front */
#define REV_MAXL 12000
static float rev_buf[8][REV_MAXL];
static int rev_len[8], rev_w[8];
static float rev_lp[8];
#define AP_MAX 1200
static float ap_buf[4][AP_MAX];
static int ap_len[4], ap_w[4];
static float pre_buf[4800];
static int pre_w;
static const float REV_MS[8] = {29.7f, 37.1f, 41.1f, 43.7f, 47.5f, 53.3f, 59.9f, 67.1f};
static const float AP_MS[4] = {4.77f, 3.59f, 12.73f, 9.30f};
static float rev_size_now = -1;
static float dist_lp_l, dist_lp_r, dist_dc_l, dist_dc_r, dist_dcx_l, dist_dcx_r, crush_hold_l, crush_hold_r, crush_t;
static float comp_env;
static float meter_l, meter_r;

static void rev_setup(float size)
{
    int k;
    float sc = 0.55f + size * 1.1f;
    for (k = 0; k < 8; k++) {
        int L = (int)(REV_MS[k] * sc * 0.001f * sr);
        rev_len[k] = L < 16 ? 16 : L > REV_MAXL ? REV_MAXL : L;
        if (rev_w[k] >= rev_len[k])
            rev_w[k] = 0;
    }
    for (k = 0; k < 4; k++) {
        int L = (int)(AP_MS[k] * 0.001f * sr);
        ap_len[k] = L < 8 ? 8 : L > AP_MAX ? AP_MAX : L;
        if (ap_w[k] >= ap_len[k])
            ap_w[k] = 0;
    }
    rev_size_now = size;
}

static inline float dist_shape(int type, float x, float drive, float *hold, int *count)
{
    switch (type) {
    case 0: return s1_tanh(x * drive + 0.15f * drive * x * x * 0.2f);              /* TAPE: soft, a little uneven */
    case 1: { float y = x * drive; return s1_tanh(y + 0.25f * y * y) ; }           /* TUBE: asymmetric */
    case 2: { float y = x * drive; return y > 1.f ? 1.f : y < -1.f ? -1.f : y; }   /* HARD */
    case 3: return s1_sin01(0.25f * x * drive);                                   /* FOLD */
    default: {                                                                    /* CRUSH: bits and rate */
        float bits = 16.f - drive * 0.5f, q = s1_exp2(s1_clamp(bits, 2.f, 16.f));
        (void)count;
        *hold = s1_floor(x * q + 0.5f) / q;
        return *hold;
    }
    }
}

static void fx_block(float *l, float *r, int n, const float *goff)
{
    int i;
    /* DIST */
    if (PI_(P_DIST_ON)) {
        int type = PI_(P_DIST_TYPE);
        float dn = val_of(P_DIST_DRIVE, pn[P_DIST_DRIVE] + goff[DST_DDRIVE]);
        float drive = 1.f + dn * dn * 30.f, mix = pv[P_DIST_MIX];
        float tone = 600.f * s1_exp2(pv[P_DIST_TONE] * 5.f), c = 1.f - s1_exp(-S1_TAU * tone * isr);
        float comp = 1.f / (1.f + dn * 0.8f);
        int cnt = 0;
        float rate = 1.f - (type == 4 ? dn * 0.9f : 0.f);
        for (i = 0; i < n; i++) {
            float yl, yr;
            if (type == 4) {
                crush_t += rate;
                if (crush_t >= 1.f) {
                    crush_t -= 1.f;
                    dist_shape(4, l[i], 4.f + dn * 24.f, &crush_hold_l, &cnt);
                    dist_shape(4, r[i], 4.f + dn * 24.f, &crush_hold_r, &cnt);
                }
                yl = crush_hold_l;
                yr = crush_hold_r;
            } else {
                yl = dist_shape(type, l[i], drive, 0, &cnt);
                yr = dist_shape(type, r[i], drive, 0, &cnt);
            }
            /* DC blocker (the asymmetric shapes), then TONE */
            dist_dc_l = yl - dist_dcx_l + 0.995f * dist_dc_l;
            dist_dcx_l = yl;
            dist_dc_r = yr - dist_dcx_r + 0.995f * dist_dc_r;
            dist_dcx_r = yr;
            dist_lp_l += (dist_dc_l - dist_lp_l) * c;
            dist_lp_r += (dist_dc_r - dist_lp_r) * c;
            l[i] += (dist_lp_l * comp - l[i]) * mix;
            r[i] += (dist_lp_r * comp - r[i]) * mix;
        }
    }
    /* CHORUS: two modulated delays, a quarter cycle apart */
    if (PI_(P_CHO_ON)) {
        float rate = pv[P_CHO_RATE], depth = pv[P_CHO_DEPTH], mix = pv[P_CHO_MIX];
        float base = 0.011f * sr, dep = (0.0015f + depth * 0.0055f) * sr;
        for (i = 0; i < n; i++) {
            float dl, dr, rl, rr;
            int a, b2;
            float fr;
            cho_l[cho_w] = l[i];
            cho_r[cho_w] = r[i];
            cho_ph += rate * isr;
            if (cho_ph >= 1.f)
                cho_ph -= 1.f;
            dl = base + dep * (0.5f + 0.5f * s1_sin01(cho_ph));
            dr = base + dep * (0.5f + 0.5f * s1_sin01(cho_ph + 0.25f));
            {
                float p = (float)cho_w - dl;
                while (p < 0)
                    p += CHO_MAX;
                a = (int)p;
                fr = p - (float)a;
                b2 = (a + 1) & (CHO_MAX - 1);
                rl = cho_l[a] + (cho_l[b2] - cho_l[a]) * fr;
            }
            {
                float p = (float)cho_w - dr;
                while (p < 0)
                    p += CHO_MAX;
                a = (int)p;
                fr = p - (float)a;
                b2 = (a + 1) & (CHO_MAX - 1);
                rr = cho_r[a] + (cho_r[b2] - cho_r[a]) * fr;
            }
            cho_w = (cho_w + 1) & (CHO_MAX - 1);
            l[i] = l[i] * (1.f - mix * 0.5f) + rl * mix;
            r[i] = r[i] * (1.f - mix * 0.5f) + rr * mix;
        }
    }
    /* DELAY: tempo-synced, damped feedback, ping-pong */
    if (PI_(P_DLY_ON)) {
        float want = s1_min(DLYT_BEATS[PI_(P_DLY_TIME) % 12] * 60.f / bpm(), 1.95f) * sr;
        float fb = pv[P_DLY_FB], mix = val_of(P_DLY_MIX, pn[P_DLY_MIX] + goff[DST_DLYMIX]);
        float tone = 800.f * s1_exp2(pv[P_DLY_TONE] * 4.5f), c = 1.f - s1_exp(-S1_TAU * tone * isr);
        int ping = PI_(P_DLY_PING);
        if (dly_time_s <= 0)
            dly_time_s = want;
        for (i = 0; i < n; i++) {
            float p, fr, ol, orr, inl = l[i], inr = r[i];
            int a, b2;
            dly_time_s += (want - dly_time_s) * 0.0005f;   /* a time change glides (a tape-like pitch bend) */
            p = (float)dly_w - dly_time_s;
            while (p < 0)
                p += DLY_MAX;
            a = (int)p;
            fr = p - (float)a;
            b2 = a + 1 >= DLY_MAX ? 0 : a + 1;
            ol = dly_l[a] + (dly_l[b2] - dly_l[a]) * fr;
            orr = dly_r[a] + (dly_r[b2] - dly_r[a]) * fr;
            dly_lp_l += (ol - dly_lp_l) * c;
            dly_lp_r += (orr - dly_lp_r) * c;
            if (ping) {
                dly_l[dly_w] = (inl + inr) * 0.5f + dly_lp_r * fb;
                dly_r[dly_w] = dly_lp_l * fb;
            } else {
                dly_l[dly_w] = inl + dly_lp_l * fb;
                dly_r[dly_w] = inr + dly_lp_r * fb;
            }
            dly_w = dly_w + 1 >= DLY_MAX ? 0 : dly_w + 1;
            l[i] = inl + ol * mix;
            r[i] = inr + orr * mix;
        }
    }
    /* REVERB */
    if (PI_(P_REV_ON)) {
        float size = pv[P_REV_SIZE], mix = val_of(P_REV_MIX, pn[P_REV_MIX] + goff[DST_REVMIX]);
        float rt = 0.4f + size * size * 7.f, damp = 0.15f + pv[P_REV_DAMP] * 0.8f;
        float gk[8];
        int k, pre = (int)(0.012f * sr);
        if (s1_abs(size - rev_size_now) > 0.02f)
            rev_setup(size);
        for (k = 0; k < 8; k++)
            gk[k] = s1_exp2(-3.f * 3.3219281f * (float)rev_len[k] / (rt * sr));   /* 10^(-3 L / (RT60 fs)) */
        for (i = 0; i < n; i++) {
            float x = (l[i] + r[i]) * 0.35f, y[8], sum = 0, ol, orr;
            int pr = pre_w - pre;
            if (pr < 0)
                pr += 4800;
            pre_buf[pre_w] = x;
            pre_w = pre_w + 1 >= 4800 ? 0 : pre_w + 1;
            x = pre_buf[pr];
            for (k = 0; k < 4; k++) {           /* diffusion */
                float d = ap_buf[k][ap_w[k]], o = -0.7f * x + d;
                ap_buf[k][ap_w[k]] = x + 0.7f * o;
                ap_w[k] = ap_w[k] + 1 >= ap_len[k] ? 0 : ap_w[k] + 1;
                x = o;
            }
            for (k = 0; k < 8; k++) {
                float d = rev_buf[k][rev_w[k]];
                rev_lp[k] += (d - rev_lp[k]) * (1.f - damp);
                y[k] = rev_lp[k] * gk[k];
                sum += y[k];
            }
            sum *= 0.25f;                        /* Householder: y - 2/N sum */
            for (k = 0; k < 8; k++) {
                rev_buf[k][rev_w[k]] = y[k] - sum + ((k & 1) ? -x : x);
                rev_w[k] = rev_w[k] + 1 >= rev_len[k] ? 0 : rev_w[k] + 1;
            }
            ol = (y[0] + y[2] + y[4] + y[6]) * 0.5f;
            orr = (y[1] + y[3] + y[5] + y[7]) * 0.5f;
            l[i] = l[i] * (1.f - mix * 0.4f) + ol * mix;
            r[i] = r[i] * (1.f - mix * 0.4f) + orr * mix;
        }
    }
    /* COMP: a feed-forward glue compressor, linked, auto make-up */
    if (PI_(P_COMP_ON)) {
        float amt = pv[P_COMP_AMT], mix = pv[P_COMP_MIX];
        float thr = -2.f - amt * 30.f, ratio = 4.f, mk = -thr * (1.f - 1.f / ratio) * 0.55f;
        float at = s1_exp(-1.f / (0.004f * sr)), rl = s1_exp(-1.f / (0.15f * sr));
        for (i = 0; i < n; i++) {
            float pk = s1_max(s1_abs(l[i]), s1_abs(r[i])), db, gr, g2;
            comp_env = pk > comp_env ? pk + (comp_env - pk) * at : pk + (comp_env - pk) * rl;
            db = 6.0206f * s1_log2(comp_env + 1e-9f);
            gr = db > thr ? (db - thr) * (1.f - 1.f / ratio) : 0;
            g2 = s1_exp2((mk - gr) / 6.0206f);
            l[i] += (l[i] * g2 - l[i]) * mix;
            r[i] += (r[i] * g2 - r[i]) * mix;
        }
    }
}

/* ------------------------------------------------------------------- notes, voices --- */
static int held[32], held_vel[32], nheld;    /* the keys down, in order */
static int sustain_on, sustained[128];

static int voices_max(void) { return PI_(P_VMODE) ? 1 : (int)s1_clamp(pv[P_VOICES], 1, MAXV); }

static void voice_start(voice_t *v, int note, float vel, int legato)
{
    int o, u, k;
    uint32_t sd = 0xA5A5u + (uint32_t)(++vage) * 2654435761u;
    float prev = newest >= 0 && V[newest].active ? V[newest].pitch : (float)note;
    if (!legato) {
        if (!v->active) {                   /* a fresh voice: everything from zero */
            memset(v, 0, sizeof *v);
            for (o = 0; o < 3; o++) {
                v->nst[o] = sd * (uint32_t)(o + 3) | 1u;
                for (u = 0; u < MAXU; u++) {
                    /* WAVETABLE / FM start in phase (punch), unison voices spread; ANALOG free-running */
                    int mode = PI_(P_OSCA_MODE + o * 14);
                    v->ph[o][u] = (mode == 1 || u > 0) ? s1_randf(&sd) : 0.f;
                    v->ph2[o][u] = 0;
                }
            }
            v->nzs = sd | 1u;
            for (k = 0; k < 6; k++)
                v->metal[k] = s1_randf(&sd);
            v->pitch = pv[P_GLIDE] > 0.0011f ? prev : (float)note;
        }
        for (k = 0; k < 3; k++) {
            env_trigger(&v->env[k], 0);
            if (PI_(P_LFO1_MODE + k * 5) != 0) {   /* RETRIG / ONE SHOT start over */
                v->lfo[k].ph = 0;
                v->lfo[k].done = 0;
            }
        }
        v->rnd = s1_randb(&sd);
    }
    v->slide = 0;                           /* (a SLIDE is set by the sequencer after this) */
    v->acc = 0;
    v->active = 1;
    v->gate = 1;
    v->fade = 0;
    v->note = note;
    v->target = (float)note;
    v->vel = vel;
    v->age = vage;
    newest = (int)(v - V);
}
static void voice_on(int note, float vel)
{
    int i, best = -1, nmax = voices_max(), mono = PI_(P_VMODE), active = 0;
    if (mono) {
        voice_t *v = &V[0];
        int legato = mono == 2 && v->active && v->gate;
        for (i = 1; i < MAXV; i++)
            if (V[i].active)
                V[i].fade = V[i].fade ? V[i].fade : 256;
        if (v->active && !legato) {          /* mono retrigger: from where it is (no click) */
            int k;
            v->note = note;
            v->target = (float)note;
            v->vel = vel;
            v->gate = 1;
            v->fade = 0;
            for (k = 0; k < 3; k++)
                v->env[k].stage = 1;
            v->slide = 0;
            v->acc = 0;
            if (pv[P_GLIDE] <= 0.0011f)
                v->pitch = (float)note;
            newest = 0;
            return;
        }
        voice_start(v, note, vel, legato);
        return;
    }
    for (i = 0; i < MAXV; i++)
        active += V[i].active && !V[i].fade;
    for (i = 0; i < MAXV; i++)               /* the same note again: reuse its voice */
        if (V[i].active && V[i].note == note && !V[i].fade) {
            best = i;
            break;
        }
    if (best < 0 && active < nmax)
        for (i = 0; i < MAXV; i++)
            if (!V[i].active) {
                best = i;
                break;
            }
    if (best < 0) {                          /* steal: the oldest released, else the oldest */
        int oldest = -1;
        for (i = 0; i < MAXV; i++)
            if (V[i].active && !V[i].fade && !V[i].gate && (oldest < 0 || V[i].age < V[oldest].age))
                oldest = i;
        if (oldest < 0)
            for (i = 0; i < MAXV; i++)
                if (V[i].active && !V[i].fade && (oldest < 0 || V[i].age < V[oldest].age))
                    oldest = i;
        if (oldest >= 0)
            V[oldest].fade = 256;            /* ~5 ms out */
        for (i = 0; i < MAXV; i++)
            if (!V[i].active) {
                best = i;
                break;
            }
        if (best < 0)
            best = oldest >= 0 ? oldest : 0;
        if (V[best].active && V[best].fade) {
            V[best].active = 0;              /* (no free slot: take the fading one at once) */
        }
    }
    voice_start(&V[best], note, vel, 0);
}
static void voice_off(int note)
{
    int i, k;
    if (PI_(P_VMODE)) {                      /* mono: back to the last key still held */
        voice_t *v = &V[0];
        if (v->note != note)
            return;
        if (nheld > 0) {
            int n2 = held[nheld - 1];
            v->note = n2;
            v->target = (float)n2;
            if (PI_(P_VMODE) == 1 && pv[P_GLIDE] <= 0.0011f)
                v->pitch = (float)n2;
            return;
        }
        v->gate = 0;
        for (k = 0; k < 3; k++)
            env_release(&v->env[k]);
        return;
    }
    for (i = 0; i < MAXV; i++)
        if (V[i].active && V[i].gate && V[i].note == note) {
            V[i].gate = 0;
            for (k = 0; k < 3; k++)
                env_release(&V[i].env[k]);
        }
}

/* ------------------------------------------------------------------- sequencer, arp --- */
static int seq_playing, seq_step = -1, seq_dir = 1, seq_note_on = -1;
static double seq_clock;                     /* samples into the current step */
static double seq_step_len, seq_gate_len;
static int seq_root = 48, seq_slide_from;
/* the voice sounding a note now (the newest if several) */
static voice_t *voice_of(int note)
{
    voice_t *best = 0;
    int i;
    for (i = 0; i < MAXV; i++)
        if (V[i].active && V[i].gate && !V[i].fade && V[i].note == note && (!best || V[i].age > best->age))
            best = &V[i];
    return best;
}
static uint32_t seq_seed = 0xBEEF123u;
static int arp_idx, arp_note_on = -1, arp_dir = 1, arp_count;
static double arp_clock, arp_len, arp_gate;
static int arp_latched[32], narp;
static int arp_chord[8], narp_chord;
static void arp_release(void)
{
    int i;
    if (arp_note_on >= 0)
        voice_off(arp_note_on);
    for (i = 0; i < narp_chord; i++)
        voice_off(arp_chord[i]);
    narp_chord = 0;
    arp_note_on = -1;
}

static float steplane(int s, int lane) { return pv[S1_SEQ_ID(s, lane)]; }

static void seq_advance(void)
{
    int len = PI_(P_SEQ_LEN), dir = PI_(P_SEQ_DIR);
    if (len < 1)
        len = 1;
    if (seq_step < 0) {
        seq_step = dir == 1 ? len - 1 : 0;
        seq_dir = 1;
        return;
    }
    switch (dir) {
    case 1:
        seq_step = seq_step <= 0 ? len - 1 : seq_step - 1;
        break;
    case 2:
        if (len == 1) {
            seq_step = 0;
            break;
        }
        if (seq_step + seq_dir >= len || seq_step + seq_dir < 0)
            seq_dir = -seq_dir;
        seq_step += seq_dir;
        break;
    case 3:
        seq_step = (int)(s1_randf(&seq_seed) * (float)len) % len;
        break;
    default:
        seq_step = (seq_step + 1) % len;
        break;
    }
}
/* the sequencer for n samples: at a step boundary, its note (the root: the last key held, else C3) */
static void seq_run(int n)
{
    double beat = 60.0 / (double)bpm() * (double)sr;
    double base = (double)SEQRATE_BEATS[PI_(P_SEQ_RATE) % 6] * beat;
    if (!seq_playing)
        return;
    seq_clock += n;
    if (seq_note_on >= 0 && seq_clock >= seq_gate_len) {
        voice_off(seq_note_on);
        seq_note_on = -1;
    }
    if (seq_step < 0 || seq_clock >= seq_step_len) {
        float gate, prob, velo, acc;
        int note, k, slide;
        if (seq_step >= 0)
            seq_clock -= seq_step_len;
        else
            seq_clock = 0;
        seq_advance();
        /* swing: even steps longer, odd shorter */
        seq_step_len = base * (1.0 + ((seq_step & 1) ? -1.0 : 1.0) * (double)pv[P_SEQ_SWING]);
        gate = steplane(seq_step, LANE_GATE);
        prob = steplane(seq_step, LANE_PROB);
        velo = steplane(seq_step, LANE_VELO);
        acc = steplane(seq_step, LANE_ACC) > 0.5f ? 1.f : 0.f;
        slide = steplane(seq_step, LANE_SLIDE) > 0.5f;
        for (k = 0; k < 4; k++)
            seqmod[k] = steplane(seq_step, LANE_M1 + k);
        if (gate > 0.01f && s1_randf(&seq_seed) < prob) {
            int root = nheld ? held[nheld - 1] : seq_root;
            voice_t *sv = seq_slide_from && seq_note_on >= 0 ? voice_of(seq_note_on) : 0;
            note = root + (int)steplane(seq_step, LANE_NOTE) + PI_(P_SEQ_OCT) * 12;
            note = note < 0 ? 0 : note > 127 ? 127 : note;
            if (sv) {                        /* SLIDE from the step before: the same voice glides, no attack */
                sv->note = note;
                sv->target = (float)note;
                sv->slide = pv[P_SEQ_SLIDE];
                sv->vel = s1_clamp(velo, 0.05f, 1.f);
                newest = (int)(sv - V);
            } else {
                if (seq_note_on >= 0)
                    voice_off(seq_note_on);
                voice_on(note, s1_clamp(velo, 0.05f, 1.f));
            }
            if (newest >= 0) {
                voice_t *v = &V[newest];
                v->acc = acc;
                if (acc > 0)                 /* accents in a row push the sweep higher */
                    v->acc_env = s1_min(1.6f, v->acc_env * 0.6f + 1.f);
            }
            seq_note_on = note;
            /* a SLIDE step holds its note until the next step takes it over */
            seq_gate_len = slide ? 1e18 : seq_step_len * (double)(gate >= 0.99f ? 1.02f : gate);
            seq_slide_from = slide;
        } else {
            if (seq_note_on >= 0)
                voice_off(seq_note_on);
            seq_note_on = -1;
            seq_slide_from = 0;
        }
    }
}

static int arp_notes(int *out)
{
    int src[32], ns = 0, i, j, k, oct = PI_(P_ARP_OCT), n = 0;
    if (PI_(P_ARP_HOLD) && narp)
        for (i = 0; i < narp; i++)
            src[ns++] = arp_latched[i];
    else
        for (i = 0; i < nheld; i++)
            src[ns++] = held[i];
    if (PI_(P_ARP_MODE) != 4)                 /* sorted, except ORDER */
        for (i = 1; i < ns; i++)
            for (j = i; j > 0 && src[j - 1] > src[j]; j--) {
                int t = src[j];
                src[j] = src[j - 1];
                src[j - 1] = t;
            }
    for (k = 0; k < oct; k++)
        for (i = 0; i < ns && n < 128; i++)
            out[n++] = src[i] + 12 * k;
    return n;
}
static void arp_run(int n)
{
    static int notes[128];
    int nn, mode;
    double beat = 60.0 / (double)bpm() * (double)sr;
    if (!PI_(P_ARP_ON) || seq_playing) {
        if (arp_note_on >= 0 || narp_chord)
            arp_release();
        return;
    }
    nn = arp_notes(notes);
    arp_clock += n;
    if ((arp_note_on >= 0 || narp_chord) && arp_clock >= arp_gate)
        arp_release();
    if (!nn) {
        arp_idx = 0;
        arp_count = 0;
        return;
    }
    if (arp_len <= 0 || arp_clock >= arp_len) {
        double base = (double)ARPRATE_BEATS[PI_(P_ARP_RATE) % 6] * beat;
        int note;
        if (arp_len > 0)
            arp_clock -= arp_len;
        else
            arp_clock = 0;
        arp_len = base * (1.0 + ((arp_count & 1) ? -1.0 : 1.0) * (double)pv[P_ARP_SWING]);
        mode = PI_(P_ARP_MODE);
        arp_release();
        if (mode == 5) {                     /* CHORD: every note at once */
            int i;
            if (s1_randf(&seq_seed) < pv[P_ARP_CHANCE])
                for (i = 0; i < nn && i < 8; i++) {
                    voice_on(notes[i], 0.8f);
                    arp_chord[narp_chord++] = notes[i];
                }
            arp_gate = arp_len * (double)s1_min(pv[P_ARP_GATE], 0.98f);
            arp_count++;
            return;
        }
        switch (mode) {
        case 1: arp_idx = nn - 1 - (arp_count % nn); break;
        case 2: {
            int cyc = nn > 1 ? 2 * nn - 2 : 1, p = arp_count % cyc;
            arp_idx = p < nn ? p : cyc - p;
            break;
        }
        case 3: arp_idx = (int)(s1_randf(&seq_seed) * (float)nn) % nn; break;
        default: arp_idx = arp_count % nn; break;
        }
        arp_count++;
        note = notes[arp_idx];
        if (s1_randf(&seq_seed) < pv[P_ARP_CHANCE]) {
            voice_on(note, 0.85f);
            arp_note_on = note;
        }
        arp_gate = arp_len * (double)s1_min(pv[P_ARP_GATE], 0.98f);
    }
}

/* ------------------------------------------------------------------- the API --- */
static int inited;
static float out_l[BLK], out_r[BLK];

S1_EXPORT void s1_init(float rate)
{
    int i;
    s1_math_init();
    sr = s1_clamp(rate, 8000.f, (float)MAXSR);
    isr = 1.f / sr;
    if (!inited)
        wt_init();
    for (i = 0; i < S1_NPARAM; i++) {
        pv[i] = PD[i].def;
        pn[i] = norm_of(i, pv[i]);
    }
    for (i = 0; i < S1_STEPS; i++) {
        int l;
        for (l = 0; l < S1_LANES; l++)
            pv[S1_SEQ_ID(i, l)] = LANE_DEF[l];
    }
    memset(V, 0, sizeof V);
    /* the playing state too: no keys held, sequencer and arp stopped */
    nheld = 0;
    sustain_on = 0;
    memset(sustained, 0, sizeof sustained);
    modwh = after = bend = 0;
    seq_playing = 0;
    seq_step = -1;
    seq_note_on = arp_note_on = -1;
    seq_slide_from = 0;
    narp = narp_chord = 0;
    newest = -1;
    for (i = 0; i < 4; i++)
        seqmod[i] = 0;
    matrix_rebuild();
    rev_setup(pv[P_REV_SIZE]);
    inited = 1;
}

/* a value, by id (a parameter, or a sequencer step lane) */
S1_EXPORT void s1_set(int id, float v)
{
    if (id < 0 || id >= S1_NALL)
        return;
    if (id >= S1_NPARAM) {
        int lane = (id - S1_NPARAM) % S1_LANES;
        v = lane == LANE_NOTE ? s1_clamp(s1_floor(v + 0.5f), -24, 24) : s1_clamp(v, 0, 1);
        pv[id] = v;
        return;
    }
    if (PD[id].kind == K_INT || PD[id].kind == K_ENUM || PD[id].kind == K_BOOL)
        v = s1_floor(v + 0.5f);
    v = s1_clamp(v, PD[id].min, PD[id].max);
    pv[id] = v;
    pn[id] = norm_of(id, v);
    if (id >= P_MOD1_SRC && id <= P_MOD16_AMT)
        matrix_rebuild();
    if (id == P_VMODE) {                      /* a mode change: release everything */
        int i;
        for (i = 0; i < MAXV; i++)
            if (V[i].active)
                V[i].fade = 256;
    }
}
S1_EXPORT float s1_get(int id) { return id >= 0 && id < S1_NALL ? pv[id] : 0; }
S1_EXPORT int s1_nparams(void) { return S1_NPARAM; }
S1_EXPORT int s1_nall(void) { return S1_NALL; }

S1_EXPORT void s1_note_on(int note, int vel)
{
    int i;
    float v = s1_clamp((float)vel / 127.f, 0.01f, 1.f);
    if (note < 0 || note > 127)
        return;
    if (vel <= 0) {
        s1_note_off(note);
        return;
    }
    for (i = 0; i < nheld; i++)
        if (held[i] == note)
            break;
    if (i == nheld && nheld < 32) {
        held_vel[nheld] = vel;
        held[nheld++] = note;
    }
    sustained[note] = 0;
    if (PI_(P_ARP_ON) && !seq_playing) {
        if (PI_(P_ARP_HOLD)) {               /* a new chord after all keys were up replaces the latched one */
            if (nheld == 1)
                narp = 0;
            if (narp < 32)
                arp_latched[narp++] = note;
        }
        return;
    }
    if (seq_playing)
        return;                              /* the key transposes the sequence */
    voice_on(note, v);
}
S1_EXPORT void s1_note_off(int note)
{
    int i, j;
    for (i = 0, j = 0; i < nheld; i++)
        if (held[i] != note) {
            held_vel[j] = held_vel[i];
            held[j++] = held[i];
        }
    nheld = j;
    if (PI_(P_ARP_ON) && !seq_playing)
        return;
    if (seq_playing)
        return;
    if (sustain_on) {
        sustained[note & 127] = 1;
        return;
    }
    voice_off(note);
}
S1_EXPORT void s1_cc(int cc, int val)
{
    float v = s1_clamp((float)val / 127.f, 0, 1);
    int i;
    if (cc == 1)
        modwh = v;
    else if (cc == 64) {
        sustain_on = val >= 64;
        if (!sustain_on)
            for (i = 0; i < 128; i++)
                if (sustained[i]) {
                    sustained[i] = 0;
                    voice_off(i);
                }
    } else if (cc == 74)
        s1_set(P_FLT_CUT, val_of(P_FLT_CUT, v));
    else if (cc == 123 || cc == 120) {
        for (i = 0; i < MAXV; i++)
            if (V[i].active)
                V[i].fade = 256;
        nheld = 0;
    }
}
S1_EXPORT void s1_bend(float b) { bend = s1_clamp(b, -1, 1); }
S1_EXPORT void s1_aftertouch(float a) { after = s1_clamp(a, 0, 1); }
S1_EXPORT void s1_panic(void)
{
    int i;
    for (i = 0; i < MAXV; i++)
        if (V[i].active)
            V[i].fade = 256;
    nheld = 0;
    narp = 0;
    seq_note_on = arp_note_on = -1;
    narp_chord = 0;
    for (i = 0; i < 128; i++)
        sustained[i] = 0;
}
S1_EXPORT void s1_transport(int play)
{
    if (play && !seq_playing) {
        seq_playing = 1;
        seq_step = -1;
        seq_clock = 0;
        seq_step_len = 0;
        arp_release();
    } else if (!play && seq_playing) {
        seq_playing = 0;
        if (seq_note_on >= 0)
            voice_off(seq_note_on);
        seq_note_on = -1;
        seq_step = -1;
        seq_slide_from = 0;
        { int k; for (k = 0; k < 4; k++) seqmod[k] = 0; }
    }
}

/* renders n samples (any n) into l, r */
S1_EXPORT void s1_render(float *l, float *r, int n)
{
    static int pos = BLK;
    int i;
    for (i = 0; i < n; i++) {
        if (pos >= BLK) {
            int k, vi;
            float goff[S1_NDST];
            for (k = 0; k < BLK; k++)
                out_l[k] = out_r[k] = 0;
            seq_run(BLK);
            arp_run(BLK);
            /* the shared LFOs (FREE mode, and the FX's context) */
            for (k = 0; k < 3; k++) {
                static lfo_t g[3];
                glob_lfo_val[k] = lfo_run(&g[k], k, 0, 1);
                glob_lfo_ph[k] = g[k].ph;
            }
            for (vi = 0; vi < MAXV; vi++)
                if (V[vi].active)
                    voice_block(&V[vi], out_l, out_r, BLK);
            /* the FX's modulation: the newest voice's sources, or the shared ones at rest */
            for (k = 0; k < S1_NDST; k++)
                goff[k] = 0;
            {
                float srcs[S1_NSRC];
                if (newest >= 0 && V[newest].active)
                    for (k = 0; k < S1_NSRC; k++)
                        srcs[k] = V[newest].src[k];
                else {
                    for (k = 0; k < S1_NSRC; k++)
                        srcs[k] = 0;
                    srcs[SRC_MODWH] = modwh;
                    srcs[SRC_AFTER] = after;
                    srcs[SRC_BEND] = bend;
                    for (k = 0; k < 4; k++) {
                        srcs[SRC_MAC1 + k] = pv[P_MAC1 + k];
                        srcs[SRC_SEQ1 + k] = seqmod[k];
                    }
                    for (k = 0; k < 3; k++)
                        srcs[SRC_LFO1 + k] = glob_lfo_val[k];
                }
                for (k = 0; k < nmods; k++)
                    goff[mods[k].dst] += mods[k].amt * srcs[mods[k].src];
            }
            fx_block(out_l, out_r, BLK, goff);
            /* LEVEL is in the voices; the master: a soft limiter, the meter */
            for (k = 0; k < BLK; k++) {
                float a = out_l[k], b = out_r[k], aa = s1_abs(a), bb = s1_abs(b);
                if (aa > 0.7f)
                    a = (a > 0 ? 1.f : -1.f) * (0.7f + 0.3f * s1_tanh((aa - 0.7f) / 0.3f));
                if (bb > 0.7f)
                    b = (b > 0 ? 1.f : -1.f) * (0.7f + 0.3f * s1_tanh((bb - 0.7f) / 0.3f));
                out_l[k] = a;
                out_r[k] = b;
                meter_l = s1_max(meter_l * 0.9995f, s1_abs(a));
                meter_r = s1_max(meter_r * 0.9995f, s1_abs(b));
            }
            pos = 0;
        }
        l[i] = out_l[pos];
        r[i] = out_r[pos];
        pos++;
    }
}

/* the panel's view of the engine: out[0] step, [1] playing, [2] meter L, [3] meter R, [4] voices sounding,
 * [5..7] LFO 1-3 (newest voice, else shared), [8..10] ENV 1-3 (newest voice), [11..13] LFO phases */
S1_EXPORT void s1_ui_state(float *out)
{
    int k, n = 0;
    voice_t *v = newest >= 0 && V[newest].active ? &V[newest] : 0;
    for (k = 0; k < MAXV; k++)
        n += V[k].active;
    out[0] = (float)seq_step;
    out[1] = (float)seq_playing;
    out[2] = meter_l;
    out[3] = meter_r;
    out[4] = (float)n;
    for (k = 0; k < 3; k++) {
        out[5 + k] = v ? v->src[SRC_LFO1 + k] : glob_lfo_val[k];
        out[8 + k] = v ? v->src[SRC_ENV1 + k] : 0;
        out[11 + k] = v ? v->lfo[k].ph : glob_lfo_ph[k];
    }
}

/* one frame of a wavetable for the display (level 0, n points) */
S1_EXPORT void s1_wt_frame(int table, int frame, float *out, int n)
{
    const float *t = wt_table(table);
    int i;
    if (frame < 0)
        frame = 0;
    if (frame >= WT_FRAMES)
        frame = WT_FRAMES - 1;
    for (i = 0; i < n; i++)
        out[i] = t ? t[frame * WT_FRAME_FLOATS + (i * 2048) / n] : 0;
}
S1_EXPORT int s1_wt_count(void) { return WTK_COUNT; }
S1_EXPORT int s1_wt_frames(void) { return WT_FRAMES; }

/* ------------------------------------------------------------------- the panel's description --- */
static char info[60000];
static int ip;
static void put(const char *s)
{
    while (*s && ip < (int)sizeof info - 1)
        info[ip++] = *s++;
}
static void putn(float v)
{
    char b[40], t[24];
    int i = 0, k = 0, d;
    long long q = (long long)((v < 0 ? -v : v) * 1000000.f + 0.5f), ip_ = q / 1000000, fr = q % 1000000;
    if (v < 0 && q)
        b[i++] = '-';
    do {
        t[k++] = (char)('0' + (int)(ip_ % 10));
        ip_ /= 10;
    } while (ip_ && k < 20);
    while (k)
        b[i++] = t[--k];
    if (fr) {
        b[i++] = '.';
        for (d = 100000; d; d /= 10) {
            b[i++] = (char)('0' + (int)(fr / d % 10));
        }
        while (b[i - 1] == '0')
            i--;
    }
    b[i] = 0;
    put(b);
}
static void putq(const char *s)
{
    put("\"");
    put(s);
    put("\"");
}
/* JSON: the parameters (key, label, kind, min, max, def, options), the sources, the destinations (with their
 * parameter key, or "pitch"), the tables, the number of steps and lanes */
S1_EXPORT const char *s1_info(void)
{
    int i, k;
    ip = 0;
    put("{\"params\":[");
    for (i = 0; i < S1_NPARAM; i++) {
        const pdesc_t *d = &PD[i];
        if (i)
            put(",");
        put("{\"id\":");
        putn((float)i);
        put(",\"key\":");
        putq(d->key);
        put(",\"label\":");
        putq(d->label);
        put(",\"kind\":");
        putn((float)d->kind);
        put(",\"min\":");
        putn(d->min);
        put(",\"max\":");
        putn(d->max);
        put(",\"def\":");
        putn(d->def);
        if (d->opts) {
            put(",\"opts\":[");
            for (k = 0; d->opts[k]; k++) {
                if (k)
                    put(",");
                putq(d->opts[k]);
            }
            put("]");
        }
        put("}");
    }
    put("],\"sources\":[");
    for (i = 0; i < S1_NSRC; i++) {
        if (i)
            put(",");
        putq(SRC_NAME[i]);
    }
    put("],\"dests\":[");
    for (i = 0; i < S1_NDST; i++) {
        if (i)
            put(",");
        put("{\"name\":");
        putq(DST[i].name);
        put(",\"key\":");
        putq(DST[i].p >= 0 ? PD[DST[i].p].key : DST[i].p == -2 ? "pitch" : "");
        put("}");
    }
    put("],\"tables\":[");
    for (i = 0; i < WTK_COUNT; i++) {
        if (i)
            put(",");
        putq(WT_LABEL[i]);
    }
    put("],\"steps\":");
    putn((float)S1_STEPS);
    put(",\"lanes\":");
    putn((float)S1_LANES);
    put(",\"seqBase\":");
    putn((float)S1_NPARAM);
    put("}");
    info[ip] = 0;
    return info;
}

/* buffers the web panel writes into / reads from */
static float io_l[1024], io_r[1024], io_tmp[2048];
S1_EXPORT float *s1_buf_l(void) { return io_l; }
S1_EXPORT float *s1_buf_r(void) { return io_r; }
S1_EXPORT float *s1_buf_tmp(void) { return io_tmp; }
S1_EXPORT void s1_render_io(int n) { s1_render(io_l, io_r, n > 1024 ? 1024 : n); }
