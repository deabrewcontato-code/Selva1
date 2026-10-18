/* Selva1 · host tests: the engine as the web panel drives it, without the web.
 *   cc -O2 -DS1_HOST -Icore -o build/s1_test tests/s1_test.c -lm && build/s1_test build/demos
 * 1. every oscillator mode x wave / table, unison 8, the filters, the FX: finite, bounded, silent after release
 * 2. the filter in tune (the ladder's self-oscillation against CUTOFF)
 * 3. the sequencer and the arpeggiator play notes on time
 * 4. the cost: 8 voices of the heaviest patch, x real time
 * Exit status: the failures. Copyright (C) 2026 DeAbrew. */
#define S1_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "../core/selva1.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("s1: FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static float L[48000 * 20], R[48000 * 20];
static int find(const char *key)
{
    int i;
    for (i = 0; i < S1_NPARAM; i++)
        if (!strcmp(PD[i].key, key))
            return i;
    printf("no parameter %s\n", key);
    exit(2);
}
static void set(const char *key, float v) { s1_set(find(key), v); }

typedef struct { float peak, rms; int bad; } stat_t;
static stat_t render(int n)
{
    stat_t s = {0, 0, 0};
    int i, k;
    double acc = 0;
    for (i = 0; i < n; i += 128) {
        int m = n - i < 128 ? n - i : 128;
        s1_render(L + i, R + i, m);
        for (k = 0; k < m; k++) {
            float a = fabsf(L[i + k]), b = fabsf(R[i + k]);
            if (!(a == a) || !(b == b) || a > 4 || b > 4)
                s.bad++;
            s.peak = fmaxf(s.peak, fmaxf(a, b));
            acc += (double)L[i + k] * L[i + k];
        }
    }
    s.rms = (float)sqrt(acc / (n ? n : 1));
    return s;
}
static void wav(const char *path, int n)
{
    FILE *f = fopen(path, "wb");
    int i;
    unsigned v;
    unsigned short a = 1, ch = 2, ba = 4, bits = 16;
    unsigned rate = 48000, br = 48000 * 4;
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f); v = 36 + n * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = n * 4; fwrite(&v, 4, 1, f);
    for (i = 0; i < n; i++) {
        short s[2] = {(short)fmaxf(-32767, fminf(32767, L[i] * 32767)), (short)fmaxf(-32767, fminf(32767, R[i] * 32767))};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

static void reset(void)
{
    s1_init(48000);
}

static void extremes(void)
{
    int mode, w, combos = 0, bad = 0;
    float worst = 0;
    for (mode = 0; mode < 4; mode++)
        for (w = 0; w < 14; w++) {
            stat_t s;
            int k, notes[] = {24, 48, 72, 96, 108};
            reset();
            set("osc.a.mode", mode);
            set("osc.a.table", w);
            set("osc.a.wave", w % 6);
            set("osc.a.ratio", w % 12);
            set("osc.a.pos", (w % 5) / 4.f);
            set("osc.a.unison", 8);
            set("osc.a.detune", 1);
            set("osc.b.on", 1);
            set("osc.b.mode", (mode + 1) % 4);
            set("osc.b.table", 13 - w);
            set("osc.b.unison", 4);
            set("osc.c.on", 1);
            set("osc.c.mode", (mode + 2) % 4);
            set("sub.on", 1);
            set("noise.on", 1);
            set("noise.type", w % 6);
            set("flt.type", w & 1);
            set("flt.mode", w % 3);
            set("flt.slope", (w >> 1) & 1);
            set("flt.res", 1);
            set("flt.drive", 1);
            set("flt.cutoff", (w & 2) ? 20000 : 200);
            set("fx.dist.on", 1);
            set("fx.dist.type", w % 5);
            set("fx.dist.drive", 1);
            set("fx.cho.on", 1);
            set("fx.dly.on", 1);
            set("fx.dly.fb", 0.95f);
            set("fx.rev.on", 1);
            set("fx.rev.size", 1);
            set("fx.comp.on", 1);
            set("mod1.src", SRC_LFO1);
            set("mod1.dst", DST_CUT);
            set("mod1.amt", 1);
            set("mod2.src", SRC_ENV2);
            set("mod2.dst", DST_A_PITCH);
            set("mod2.amt", -1);
            set("lfo1.rate", 40);
            for (k = 0; k < 5; k++)
                s1_note_on(notes[k], 127);
            s = render(48000 / 2);
            for (k = 0; k < 5; k++)
                s1_note_off(notes[k]);
            render(48000 * 2);
            bad += s.bad;
            worst = fmaxf(worst, s.peak);
            combos++;
        }
    CHECK(!bad, "extremes: %d samples not finite or beyond 4", bad);
    CHECK(worst <= 1.0001f, "extremes: the limiter let %.3f through", worst);
    printf("s1: extremes: %d settings (all modes, unison 8, RES and DRIVE at the top, every FX), peak %.3f\n", combos, worst);
}

static void tails(void)
{
    stat_t s;
    reset();
    set("env1.r", 0.3f);
    s1_note_on(60, 100);
    render(24000);
    s1_note_off(60);
    render(48000);
    s = render(4800);
    CHECK(s.peak < 1e-4f, "release: still sounding after the release (%.6f)", s.peak);
    {
        int i, n = 0;
        for (i = 0; i < MAXV; i++)
            n += V[i].active;
        CHECK(n == 0, "release: %d voices still active", n);
    }
}

static void tuning(void)
{
    float cuts[] = {110, 440, 1760, 5000};
    int c;
    for (c = 0; c < 4; c++) {
        int i, zc = 0, n = 48000;
        float prev = 0, hz, cents;
        reset();
        set("osc.a.level", 0);
        set("noise.on", 1);
        set("noise.level", 0.05f);
        set("flt.res", 1);
        set("flt.key", 0);
        set("flt.cutoff", cuts[c]);
        set("flt.drive", 0);
        set("env1.s", 1);
        s1_note_on(60, 100);
        render(24000);
        render(n);
        for (i = 0; i < n; i++) {
            zc += (prev < 0) != (L[i] < 0);
            prev = L[i];
        }
        hz = zc / 2.f;
        cents = 1200.f * log2f(hz / cuts[c]);
        printf("s1: tuning: CUTOFF %5.0f Hz -> self-oscillation %6.0f Hz (%+.0f cents)\n", cuts[c], hz, cents);
        CHECK(fabsf(cents) < 80, "tuning: %.0f Hz for %.0f Hz", hz, cuts[c]);
    }
}

static void sequencer(void)
{
    int i, onsets = 0, n = 48000 * 2;
    float prev = 0;
    reset();
    set("global.bpm", 120);
    set("seq.rate", 2);                       /* 1/16 at 120: 125 ms */
    set("env1.a", 0.001f);
    set("env1.d", 0.05f);
    set("env1.s", 0);
    set("env1.r", 0.01f);
    for (i = 0; i < 16; i++)
        s1_set(S1_SEQ_ID(i, LANE_GATE), (i % 2) ? 0 : 0.5f);
    s1_transport(1);
    render(n);
    s1_transport(0);
    /* onsets: the envelope's rise above a threshold after silence */
    for (i = 0; i < n; i++) {
        float a = fabsf(L[i]);
        if (a > 0.05f && prev <= 0.05f)
            onsets++;
        prev = a > prev ? a : prev * 0.999f;
    }
    printf("s1: sequencer: 2 s at 120 BPM, 1/16, every other step: %d notes (8 expected)\n", onsets);
    CHECK(onsets >= 7 && onsets <= 9, "sequencer: %d notes", onsets);
}

/* 303: a SLIDE step glides into the next without a new attack; an ACCENT step is louder and brighter */
static void slide_accent(void)
{
    int i, n = 6000;                          /* 1/16 at 120 BPM: 6000 samples a step */
    float mid, a0, a1;
    reset();
    set("global.bpm", 120);
    set("seq.rate", 2);
    set("voice.mode", 1);
    set("env1.a", 0.05f);
    set("seq.slide", 0.08f);
    s1_set(S1_SEQ_ID(0, LANE_GATE), 0.5f);
    s1_set(S1_SEQ_ID(0, LANE_SLIDE), 1);
    s1_set(S1_SEQ_ID(1, LANE_GATE), 0.5f);
    s1_set(S1_SEQ_ID(1, LANE_NOTE), 12);
    set("seq.len", 2);
    s1_transport(1);
    render(n + 600);                          /* into step 2 by 600 samples */
    mid = V[0].pitch;
    CHECK(V[0].active && V[0].gate, "slide: the note did not hold into the next step");
    CHECK(mid > 48.5f && mid < 59.5f, "slide: pitch %.2f is not between the two notes (gliding)", mid);
    CHECK(V[0].env[0].stage != 1, "slide: the envelope started a new attack");
    render(n - 1200);
    CHECK(fabsf(V[0].pitch - 60.f) < 0.5f, "slide: did not arrive (%.2f)", V[0].pitch);
    s1_transport(0);
    printf("s1: slide: 600 samples after the step, the pitch is at %.2f on its way from 48 to 60, no new attack\n", mid);
    /* accent: the same two notes, the first accented; compare their loudness */
    reset();
    set("global.bpm", 120);
    set("seq.rate", 2);
    set("seq.len", 2);
    set("flt.cutoff", 400);
    set("flt.res", 0.6f);
    set("env1.d", 0.1f);
    set("env1.s", 0.3f);
    for (i = 0; i < 2; i++)
        s1_set(S1_SEQ_ID(i, LANE_GATE), 0.5f);
    s1_set(S1_SEQ_ID(0, LANE_ACC), 1);
    s1_transport(1);
    a0 = render(n).rms;
    a1 = render(n).rms;
    s1_transport(0);
    CHECK(a0 > a1 * 1.3f, "accent: the accented step is not louder (%.4f vs %.4f)", a0, a1);
    printf("s1: accent: the accented step %+.1f dB over the plain one\n", 20.f * log10f(a0 / a1));
}

static void cost(void)
{
    clock_t t0;
    int k, n = 48000 * 4;
    double secs;
    reset();
    for (k = 0; k < 3; k++) {
        char key[32];
        sprintf(key, "osc.%c.on", 'a' + k); set(key, 1);
        sprintf(key, "osc.%c.mode", 'a' + k); set(key, 0);
        sprintf(key, "osc.%c.unison", 'a' + k); set(key, 8);
        sprintf(key, "osc.%c.table", 'a' + k); set(key, k + 1);
    }
    set("sub.on", 1);
    set("noise.on", 1);
    set("fx.dist.on", 1);
    set("fx.cho.on", 1);
    set("fx.dly.on", 1);
    set("fx.rev.on", 1);
    set("fx.comp.on", 1);
    for (k = 0; k < 8; k++)
        s1_note_on(48 + k * 3, 100);
    render(4800);
    t0 = clock();
    render(n);
    secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("s1: cost: 8 voices x 3 wavetable oscillators x unison 8, sub, noise, every FX: %.1f%% of real time (host)\n",
           100.0 * secs / (n / 48000.0));
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build";
    char path[512];
    extremes();
    tails();
    tuning();
    sequencer();
    slide_accent();
    cost();
    /* a short demo: a chord through each wavetable */
    {
        int t, k, pos = 0;
        reset();
        set("osc.a.mode", 0);
        set("osc.a.unison", 3);
        set("osc.a.detune", 0.3f);
        set("fx.rev.on", 1);
        set("mod1.src", SRC_LFO1);
        set("mod1.dst", DST_A_POS);
        set("mod1.amt", 0.5f);
        set("lfo1.rate", 0.5f);
        set("osc.a.pos", 0.5f);
        for (t = 0; t < 14; t++) {
            int chord[] = {48, 55, 60, 63};
            set("osc.a.table", t);
            for (k = 0; k < 4; k++)
                s1_note_on(chord[k], 100);
            for (k = 0; k < 48000 * 0.9; k += 128)
                s1_render(L + pos + k, R + pos + k, 128);
            pos += 48000 * 9 / 10;
            for (k = 0; k < 4; k++)
                s1_note_off(chord[k]);
            for (k = 0; k < 4800; k += 128)
                s1_render(L + pos + k, R + pos + k, 128);
            pos += 4800;
        }
        snprintf(path, sizeof path, "%s/tables.wav", dir);
        wav(path, pos);
        printf("s1: demo: %s (%.1f s, every wavetable)\n", path, pos / 48000.0);
    }
    printf("s1: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
