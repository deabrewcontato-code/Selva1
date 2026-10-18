/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 DeAbrew (deabrewmusic.com.br), written with Claude; on SLOOP (isod89) and Felucca (Leo Kuroshita) */
/* Selva1: an analog-style voice in the manner of the classic monosynths, one source for the FM-1 and the browser.
 *
 * Per voice:  OSC 1 + OSC 2 + SUB + NOISE -> MIXER (DRIVE) -> LADDER (24 dB/oct low-pass) -> amplifier
 *   OSC 1, OSC 2  six waves each, as on the classic panels: TRI, SHARK (triangle-saw), SAW, SQR, WIDE (pulse
 *                 30 %), NARROW (pulse 12 %); RANGE 32' .. 2'; OSC 2 also SEMI and FINE. ENV / LFO -> SHP move
 *                 the width of SQR, WIDE and NARROW (PWM).
 *   SUB           a square or a sine one or two octaves under OSC 1 (it follows OSC 1's RANGE).
 *   NOISE         WHITE, or PINK (white through a gentle low-pass, some white mixed back).
 *   MIXER         the four levels; DRIVE pushes the sum into the ladder's input (tanh): the mixer overdrive.
 *   LADDER        four trapezoidal one-poles in a loop with one sample of delay, a tanh where the loop closes:
 *                 RES up to self-oscillation; the pass-band loss of the resonance is half made up at the input.
 *   F.ENV         the filter's own ADSR (P_E4..P_E7), ENV its amount (- closes, + opens); KTR the keyboard
 *                 tracking (OFF, 1/3, 2/3, FULL, as the classic switches).
 *   amplifier     the track's ADSR (the ENV page), velocity, LFO -> AMP: as every engine (voice.c).
 *   DRIFT         a small random tuning per note, OSC 1 and OSC 2 each their own: they beat as old oscillators do.
 *
 * Parameters. P_E0..P_E7 (kept by projects and user presets, the FM-1's EDIT pages, HOME's knobs: CUT RES ENV
 * DRV): CUT RES ENV DRV | F.ATK F.DEC F.SUS F.REL. The oscillators and the mixer: SELVA_NX values per part in
 * selva_x[], loaded with each factory preset (selva_track_loaded) or sent by the web panel (host/preview.c
 * preview_selva). SELVA_XD describes them (labels, ranges, names) for the web panel and future FM-1 pages.
 *
 * Voice state: ph[0] OSC 1, ph[1] OSC 2, ph[2] SUB; s[0..3] the ladder's four stages, s[4] the noise,
 * s[5] F.ENV (Q24) | its stage << 28, s[6] the pink filter, s[7] the ladder's last output (the feedback). The
 * ladder's states and output are kept 8x (three bits under the signal): quiet notes at low cutoffs keep their detail.
 * Everything is per voice: a part renders the same on either core (fx.c units_split). */
enum { SX_W1, SX_R1, SX_L1, SX_W2, SX_R2, SX_SEMI, SX_FINE, SX_L2, SX_SUBT, SX_SUBL, SX_NZC, SX_NZL, SX_KTR,
       SX_DRFT, SELVA_NX };
static const char *const N_SELVA_WAVE[] = {"TRI", "SHARK", "SAW", "SQR", "WIDE", "NARROW"};
static const char *const N_SELVA_RANGE[] = {"32'", "16'", "8'", "4'", "2'"};
static const char *const N_SELVA_SUB[] = {"SQ-1", "SQ-2", "SIN-1", "SIN-2"};
static const char *const N_SELVA_NZ[] = {"WHITE", "PINK"};
static const char *const N_SELVA_KTR[] = {"OFF", "1/3", "2/3", "FULL"};
static const param_desc_t SELVA_XD[SELVA_NX] = {
    [SX_W1] = {"W1", F_ENUM, 0, 5, 2, N_SELVA_WAVE, 0},
    [SX_R1] = {"RNG1", F_ENUM, 0, 4, 2, N_SELVA_RANGE, 0},
    [SX_L1] = {"LVL1", F_PCT, 0, 127, 110, 0, 0},
    [SX_W2] = {"W2", F_ENUM, 0, 5, 2, N_SELVA_WAVE, 0},
    [SX_R2] = {"RNG2", F_ENUM, 0, 4, 2, N_SELVA_RANGE, 0},
    [SX_SEMI] = {"SEMI", F_SEMI, -12, 12, 0, 0, 0},
    [SX_FINE] = {"FINE", F_INT, -50, 50, 0, 0, "ct"},
    [SX_L2] = {"LVL2", F_PCT, 0, 127, 0, 0, 0},
    [SX_SUBT] = {"SUB", F_ENUM, 0, 3, 0, N_SELVA_SUB, 0},
    [SX_SUBL] = {"SUBL", F_PCT, 0, 127, 0, 0, 0},
    [SX_NZC] = {"NZ", F_ENUM, 0, 1, 0, N_SELVA_NZ, 0},
    [SX_NZL] = {"NZL", F_PCT, 0, 127, 0, 0, 0},
    [SX_KTR] = {"KTR", F_ENUM, 0, 3, 1, N_SELVA_KTR, 0},
    [SX_DRFT] = {"DRFT", F_PCT, 0, 127, 16, 0, 0},
};
static int8_t selva_x[NPART][SELVA_NX];             /* each part's oscillators and mixer */
static uint8_t selva_have[NPART];                 /* 1 = selva_x[part] was loaded (else the defaults, at first use) */

#define SELVA_PW_MIN 2622                         /* pulse width limits, Q16 of the cycle: 4 % .. 96 % */
#define SELVA_PW_MAX 62914
static const uint16_t SELVA_PW[6] = {0, 0, 0, 32768, 19661, 7864};   /* SQR 50 %, WIDE 30 %, NARROW 12 % (Q16) */
#define SELVA_K_MAX 17800                         /* the ladder's feedback at RES 127, Q12 (4.35: self-oscillates) */
#define SELVA_PINK_K 5150                         /* the pink filter's one-pole, Q15: about 1.2 kHz */

/* the oscillator: w a wave, ph its phase, inc its increment, pw the pulse width (Q32) of SQR / WIDE / NARROW */
static inline int32_t selva_osc(uint32_t w, uint32_t ph, uint32_t inc, uint32_t pw)
{
    switch (w) {
    case 0:
        return osc_tri(ph);
    case 1:
        return (osc_tri(ph) + osc_saw(ph, inc)) >> 1;   /* the triangle-saw ("shark tooth") */
    case 2:
        return osc_saw(ph, inc);
    default:
        return osc_pulse(ph, inc, pw);
    }
}

/* the increment of an oscillator RANGE r (0 = 32' .. 4 = 2'; 2 = 8', the key's pitch), kept below Nyquist */
static inline uint32_t selva_range(uint32_t inc, uint32_t r)
{
    if (r < 2u)
        return inc >> (2u - r);
    r -= 2u;
    return inc > (0x73000000u >> r) ? 0x73000000u : inc << r;
}
/* inc * (1 + f / 4096): fine tuning in 1/4096 (1 cent = 2.367) */
static inline uint32_t selva_fine(uint32_t inc, int32_t f) { return f ? inc + (uint32_t)((int32_t)(inc >> 12) * f) : inc; }

/* a pulse width (Q32) from the wave's own and the SHP modulation (64 << 8 = none) */
static inline uint32_t selva_pw(uint32_t w, int32_t shape)
{
    int32_t d = clamp(shape - (64 << 8), -16384, 16384) * 2;   /* +-50 % of the cycle at full modulation */
    return (uint32_t)clamp(SELVA_PW[w < 6u ? w : 3u] + d, SELVA_PW_MIN, SELVA_PW_MAX) << 16;
}

/* the ladder's one-pole coefficient G = g / (1 + g), Q16, for a cutoff 0..127 << 8. g = tan(pi fc / FS), raised by
 * 1 + 1.06 G + 1.3 G^2 + 1.3 G^3: the loop's one sample of delay pulls the resonance flat as fc rises (-640 cents at 8.8 kHz without it;
 * tests/selva_test.c measures the self-oscillation against CUT) */
static inline int32_t selva_g_at(uint32_t i)
{   /* g in Q16: from the cutoff in Hz while tan(x) ~ x (below about 1 kHz, where SVF_G's Q12 is coarse), else SVF_G */
    return i < 72u ? (int32_t)(CUTOFF_HZ[i] * 4781u >> 10) : (int32_t)SVF_G[i] << 4;
}
static inline int32_t selva_coef(int32_t cut)
{
    uint32_t i, g, g0;
    cut = clamp(cut, 0, 127 << 8);
    i = (uint32_t)cut >> 8;
    g = (uint32_t)selva_g_at(i);
    if (i < 127u)
        g = (uint32_t)((int32_t)g + (((selva_g_at(i + 1u) - (int32_t)g) * (cut & 255)) >> 8));
    g0 = (g << 10) / ((65536u + g) >> 6);               /* G before the correction, Q16 */
    g += (uint32_t)mulq16((int32_t)g, (g0 * 1086u >> 10) + (g0 * g0 >> 16) * 1300u / 1000u + ((g0 * g0 >> 16) * g0 >> 16) * 1300u / 1000u);
                                                        /* g (1 + 1.06 G + 1.3 G^2 + 1.3 G^3) */
    return (int32_t)((g << 10) / ((65536u + g) >> 6));   /* g 65536 / (65536 + g) */
}

/* a * k / 65536 rounded to nearest (mulq16 floors: in the ladder's integrators at a low cutoff that bias builds
 * up into a DC offset) */
static inline int32_t selva_mul(int32_t a, uint32_t k)
{
    return (a >> 16) * (int32_t)k + (int32_t)((((uint32_t)a & 0xFFFFu) * k + 0x8000u) >> 16);
}

static int8_t *selva_of(const track_t *t);

static void selva_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->s[4])
        v->s[4] = 0x2468ACE + (int32_t)v->age;           /* noise state */
    if (!v->env && !v->env_out) {                      /* a fresh voice: the oscillators from a known phase, */
        v->ph[0] = 0;                                    /* the ladder empty */
        v->ph[1] = 0x55555555u;
        v->ph[2] = 0;
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = v->s[6] = v->s[7] = 0;
        v->s[5] = 0;
    }
    v->s[5] = (v->s[5] & 0x0FFFFFFF) | (1 << 28);       /* F.ENV: attack, from where it is (a retrigger: no click) */
}

/* one control tick of the filter's ADSR (P_E4..P_E7); returns Q15 */
static inline int32_t selva_fenv(const int16_t *p, voice_t *v)
{
    int32_t e = v->s[5] & 0x0FFFFFFF;
    uint32_t st = (uint32_t)v->s[5] >> 28;
    if (!v->gate && st && st < 3u)                      /* the key let go: release */
        st = 3;
    switch (st) {
    case 1:
        e += (int32_t)ENV_LIN[p[P_E4] & 127];
        if (e >= (1 << 24)) {
            e = 1 << 24;
            st = 2;
        }
        break;
    case 2:
        e += mulq16(((int32_t)p[P_E6] << 17) - e, ENV_EXP[p[P_E5] & 127]);
        break;
    case 3:
        e -= mulq16(e, ENV_EXP[p[P_E7] & 127]);
        if (e < (1 << 10)) {
            e = 0;
            st = 0;
        }
        break;
    default:
        e = 0;
        break;
    }
    v->s[5] = (int32_t)((uint32_t)clamp(e, 0, 1 << 24) | st << 28);
    return e >> 9;
}

static void selva_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const int16_t KTR[4] = {0, 100, 199, 299};  /* cutoff (<< 8) per 1/16 semitone, /16: 0, 1/3, 2/3, 1 */
    const int16_t *p = t->p;
    const int8_t *x = selva_of(t);
    uint32_t w1 = (uint32_t)x[SX_W1] % 6u, w2 = (uint32_t)x[SX_W2] % 6u, subt = (uint32_t)x[SX_SUBT] & 3u, i;
    int32_t g1 = x[SX_L1] * 258, g2 = x[SX_L2] * 258, gs = x[SX_SUBL] * 258, gn = x[SX_NZL] * 258;
    int32_t pink = x[SX_NZC] != 0, fe, cut, G, k, ig, og;
    /* DRIFT: a per-note offset from the voice's age, OSC 1 and OSC 2 their own, up to +-8 cents */
    uint32_t h = v->age * 2654435761u;
    int32_t dmax = x[SX_DRFT] * 19 / 127;
    int32_t d1 = ((int32_t)(h >> 16) - 32768) * dmax >> 15, d2 = ((int32_t)(h & 0xFFFFu) - 32768) * dmax >> 15;
    /* OSC 2: SEMI, FINE (whole 1/16 semitones in its pitch, the rest as a fine factor), its RANGE */
    int32_t fine = x[SX_FINE], f16 = fine * 16 / 100, frem = (fine * 16 - f16 * 100) * 2367 / 16000;
    uint32_t inc1 = selva_range(selva_fine(m->inc, d1), (uint32_t)x[SX_R1] % 5u);
    uint32_t inc2 = selva_range(selva_fine(pitch_inc((uint32_t)clamp(m->pitch16 + x[SX_SEMI] * 16 + f16, 0, 2047)),
                                       m->fine + frem + d2), (uint32_t)x[SX_R2] % 5u);
    uint32_t incs = inc1 >> (1u + (subt & 1u));        /* SUB: one or two octaves under OSC 1 */
    uint32_t pw1 = selva_pw(w1, m->shape), pw2 = selva_pw(w2, m->shape);
    uint32_t ph0, ph1, ph2;
    int32_t s0, s1, s2, s3, y4, nst, pst;
    /* the cutoff this block: CUT, the track's modulation (LFO / ENV -> FLT, accent), F.ENV x ENV, the keyboard */
    fe = selva_fenv(p, v);
    cut = (p[P_E0] << 8) + m->cutoff + ((fe * p[P_E2]) >> 6) + ((m->pitch16 - 60 * 16) * KTR[x[SX_KTR] & 3] >> 4);
    G = selva_coef(cut);
    k = p[P_E1] * SELVA_K_MAX / 127;                      /* the feedback, Q12 */
    /* the ladder's input gain (Q12): DRIVE 1x .. 4x, times 1 + k / 2 (half the resonance's pass-band loss) */
    ig = ((4096 + p[P_E3] * 97) * (8192 + (k >> 1))) >> 13;
    og = 16384 - p[P_E3] * 77;                          /* .. and the level after it: 4x .. 1.6x (Q12) */
    ph0 = v->ph[0];                                     /* state in locals: out[] may alias v->s[] */
    ph1 = v->ph[1];
    ph2 = v->ph[2];
    s0 = v->s[0];
    s1 = v->s[1];
    s2 = v->s[2];
    s3 = v->s[3];
    nst = v->s[4];
    pst = v->s[6];
    y4 = v->s[7];
    for (i = 0; i < n; i++) {
        int32_t mix = mulq15(selva_osc(w1, ph0, inc1, pw1), g1), u, a, b;
        if (g2)
            mix += mulq15(selva_osc(w2, ph1, inc2, pw2), g2);
        if (gs)
            mix += mulq15(subt < 2u ? osc_pulse(ph2, incs, 0x80000000u) : sine_i(ph2), gs);
        if (gn) {                                       /* (at half its level, doubled after the gain: no overflow) */
            int32_t wn = (int32_t)(noise32(&nst) >> 17) - 16384;
            if (pink) {
                pst += ((wn - pst) * SELVA_PINK_K) >> 15;
                wn = pst * 3 + (wn >> 2);               /* about as loud as the white */
            }
            mix += mulq15(wn, gn) << 1;
        }
        ph0 += inc1;
        ph1 += inc2;
        ph2 += incs;
        /* the ladder: a quarter of the mixer times DRIVE, made up for the resonance; the feedback; a tanh */
        u = ((mix >> 2) * ig) >> 12;
        u = softclip(u - ((k * (y4 >> 3)) >> 12)) << 3;   /* the ladder runs 8x (3 bits under the signal) */
        a = selva_mul(u - s0, (uint32_t)G);               /* four trapezoidal one-poles: v = (x - s) G, y = v + s */
        b = a + s0;
        s0 = b + a;
        a = selva_mul(b - s1, (uint32_t)G);
        u = a + s1;
        s1 = u + a;
        a = selva_mul(u - s2, (uint32_t)G);
        b = a + s2;
        s2 = b + a;
        a = selva_mul(b - s3, (uint32_t)G);
        y4 = clamp(a + s3, -480000, 480000);
        s3 = y4 + a;
        out[i] += voice_amp(soft_knee(((y4 >> 3) * og) >> 12, 26000), m, i);
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = s0;
    v->s[1] = s1;
    v->s[2] = s2;
    v->s[3] = s3;
    v->s[4] = nst;
    v->s[6] = pst;
    v->s[7] = y4;
}

/* the factory sounds. {CUT, RES, ENV, DRV, F.ATK, F.DEC, F.SUS, F.REL}, {A D S R}, fenv (0: F.ENV does it), mono;
 * SELVA_XP below holds each one's oscillators and mixer, in the same order */
static const preset_t SELVA_PRESETS[] = {
    {"INIT", {100, 0, 0, 0, 0, 70, 127, 60}, {0, 70, 127, 60}, 0, 0, FX(0, 0, 0, 0)},
    {"MINI BASS", {48, 36, 46, 30, 0, 74, 30, 40}, {0, 70, 110, 40}, 0, 1, FX(0, 0, 0, 4)},
    {"FAT LEAD", {78, 28, 22, 40, 2, 70, 90, 40}, {2, 70, 115, 50}, 0, 1, FX(0, 0, 26, 20), XP(P_GLIDE + 1, 34)},
    {"ACID LINE", {30, 104, 54, 60, 0, 70, 0, 40}, {0, 64, 100, 40}, 0, 1, FX(28, 0, 22, 10),
     XP(P_GLIDE + 1, 30, P_GLMODE + 1, 1)},
    {"RAW RUMBLE", {34, 20, 34, 110, 0, 76, 10, 50}, {0, 84, 50, 50}, 0, 1, FX(40, 0, 10, 50)},
    {"TECHNO STB", {58, 34, 50, 70, 0, 70, 0, 50}, {0, 84, 0, 62}, 0, 0, FX(10, 0, 40, 40)},
    {"BRASS", {40, 10, 40, 20, 44, 80, 70, 40}, {28, 70, 112, 34}, 0, 0, FX(0, 20, 10, 30)},
    {"WARM PAD", {54, 12, 14, 0, 90, 90, 100, 90}, {84, 90, 118, 92}, 0, 0, FX(0, 50, 20, 60),
     XP(P_LRATE + 1, 24, P_LD_FLT + 1, 6)},
    {"PLUCK", {44, 30, 60, 20, 0, 72, 0, 50}, {0, 88, 0, 64}, 0, 0, FX(0, 10, 30, 30)},
    {"SUB 808", {64, 0, 10, 60, 0, 70, 40, 40}, {0, 112, 0, 52}, 0, 1, FX(0, 0, 0, 0),
     XP(P_GLIDE + 1, 59, P_GLMODE + 1, 1, P_ED_PIT + 1, 18)},
    {"PWM LEAD", {82, 20, 20, 10, 0, 70, 80, 30}, {2, 70, 110, 50}, 0, 1, FX(0, 10, 26, 22),
     XP(P_LRATE + 1, 60, P_LD_SHP + 1, 40, P_GLIDE + 1, 24)},
    {"SHARK BASS", {56, 44, 40, 70, 0, 72, 20, 40}, {0, 70, 100, 40}, 0, 1, FX(6, 0, 0, 4)},
    {"SQR KEYS", {60, 10, 30, 10, 0, 70, 30, 40}, {0, 80, 60, 60}, 0, 0, FX(0, 20, 16, 30)},
    {"WIND", {50, 112, 0, 0, 0, 64, 127, 40}, {90, 90, 127, 100}, 0, 0, FX(0, 0, 20, 70),
     XP(P_LRATE + 1, 20, P_LD_FLT + 1, 40)},
    {"DRONE", {36, 70, 0, 90, 0, 64, 127, 60}, {100, 90, 127, 110}, 0, 0, FX(30, 0, 30, 80),
     XP(P_LRATE + 1, 12, P_LD_FLT + 1, 18)},
};
/* {W1, RNG1, LVL1, W2, RNG2, SEMI, FINE, LVL2, SUB, SUBL, NZ, NZL, KTR, DRFT}: waves 0 TRI 1 SHARK 2 SAW 3 SQR
 * 4 WIDE 5 NARROW; ranges 0 32' 1 16' 2 8' 3 4' 4 2'; SUB 0 SQ-1 1 SQ-2 2 SIN-1 3 SIN-2; NZ 0 WHITE 1 PINK */
static const int8_t SELVA_XP[][SELVA_NX] = {
    {2, 2, 110, 2, 2, 0, 0, 0, 0, 0, 0, 0, 1, 0},          /* INIT: one saw, the filter open */
    {2, 1, 110, 3, 0, 0, 4, 80, 0, 0, 0, 0, 1, 20},        /* MINI BASS */
    {2, 2, 110, 2, 2, 0, 9, 100, 0, 40, 0, 0, 1, 30},      /* FAT LEAD */
    {2, 1, 127, 2, 1, 0, 0, 0, 0, 0, 0, 0, 1, 10},         /* ACID LINE */
    {3, 0, 110, 2, 1, 0, -12, 70, 2, 90, 1, 12, 0, 40},    /* RAW RUMBLE */
    {2, 2, 110, 2, 2, 7, 3, 90, 0, 0, 0, 0, 1, 20},        /* TECHNO STB: a fifth */
    {2, 2, 110, 2, 2, 0, 7, 100, 0, 0, 0, 0, 1, 35},       /* BRASS */
    {0, 2, 110, 2, 2, 0, 12, 80, 0, 0, 0, 0, 1, 50},       /* WARM PAD */
    {3, 2, 110, 2, 3, 0, 5, 90, 0, 0, 0, 0, 2, 20},        /* PLUCK */
    {0, 2, 40, 2, 2, 0, 0, 0, 2, 127, 0, 0, 0, 0},         /* SUB 808: the sine an octave under */
    {4, 2, 110, 5, 2, 0, 6, 80, 0, 0, 0, 0, 1, 25},        /* PWM LEAD */
    {1, 1, 120, 1, 1, 0, 5, 70, 0, 0, 0, 0, 1, 20},        /* SHARK BASS */
    {3, 2, 110, 0, 3, 0, 0, 60, 0, 0, 0, 0, 1, 15},        /* SQR KEYS */
    {2, 2, 0, 2, 2, 0, 0, 0, 0, 0, 1, 127, 3, 0},          /* WIND: pink noise, the resonance follows the keys */
    {2, 0, 100, 2, 1, 0, 4, 90, 1, 60, 1, 20, 0, 40},      /* DRONE */
};
_Static_assert(NELEM(SELVA_XP) == NELEM(SELVA_PRESETS), "a SELVA_XP row per Selva1 preset");

static void selva_defaults(int8_t *x)
{
    uint32_t i;
    for (i = 0; i < SELVA_NX; i++)
        x[i] = (int8_t)SELVA_XD[i].def;
}
/* the part's oscillators and mixer (the defaults until a preset or the web panel sets them) */
static int8_t *selva_of(const track_t *t)
{
    uint32_t k = (uint32_t)(t - trk) % NPART;
    if (!selva_have[k]) {
        selva_defaults(selva_x[k]);
        selva_have[k] = 1;
    }
    return selva_x[k];
}
/* a factory preset loaded on t (ui.c apply_preset_to, project and user preset loads: engines.c
 * eng_track_loaded): its oscillators and mixer */
static void selva_track_loaded(const track_t *t)
{
    uint32_t k = (uint32_t)(t - trk);
    if (k < NPART && t->eng_req == ENGI_SELVA) {
        memcpy(selva_x[k], SELVA_XP[t->preset % NELEM(SELVA_XP)], SELVA_NX);
        selva_have[k] = 1;
    }
}
/* one value of the part's oscillators and mixer, kept in range (the web panel, a future FM-1 page) */
static void selva_set(uint32_t k, uint32_t id, int32_t v)
{
    if (k < NPART && id < SELVA_NX) {
        if (!selva_have[k]) {
            selva_defaults(selva_x[k]);
            selva_have[k] = 1;
        }
        selva_x[k][id] = (int8_t)clamp(v, SELVA_XD[id].min, SELVA_XD[id].max);
    }
}

static const engine_t ENG_SELVA = {
    "SELVA1", {"FLT", "FEG"},
    {
        {"CUT", F_CUTOFF, 0, 127, 72, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"ENV", F_BIPCT, -64, 63, 32, 0, 0},
        {"DRV", F_PCT, 0, 127, 16, 0, 0},
        {"F.AT", F_TIME, 0, 127, 0, 0, 0},
        {"F.DC", F_TIME, 0, 127, 60, 0, 0},
        {"F.SU", F_PCT, 0, 127, 40, 0, 0},
        {"F.RL", F_TIME, 0, 127, 40, 0, 0},
    },
    SELVA_PRESETS, sizeof(SELVA_PRESETS) / sizeof(SELVA_PRESETS[0]), 0, selva_note_on, selva_render,
    0xC1C5, {P_E0, P_E1, P_E2, P_E3},           /* DeAbrew's red; HOME: CUT RES ENV DRV */
};
