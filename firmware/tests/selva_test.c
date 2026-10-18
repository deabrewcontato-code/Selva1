/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 DeAbrew, written with Claude */
/* Selva1 (firmware/src/eng_selva.c) on the host, through the firmware's own mix (hostsim.c):
 *   build/host/selva_test [DIR]       (DIR: the demos, default build/selva)
 * 1. extremes: every pair of waves, every RANGE and SUB, RES and DRV at their tops, the cutoff at both ends, ENV
 *    +-, KTR FULL, noise, notes from 24 to 108, POLY / UNISON: the part's output stays bounded, finite, and silent
 *    once released.
 * 2. the ladder in tune: RES 127, a breath of noise, KTR OFF: the self-oscillation's pitch against CUT's Hz.
 * 3. the filter envelope: F.ATK / F.DEC shape the brightness over time (zero crossings), F.REL lets it close.
 * 4. demos: DIR/selva1-demo.wav, every factory preset on a phrase that suits it, then DIR/<preset>.wav each.
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/stat.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("selva: FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int32_t buf[CTL * 2];
static FILE *wav_all;
static uint32_t wav_all_frames;

/* one block of the mix; returns the block's peak (both channels) */
static int32_t blk(FILE *w)
{
    uint32_t i;
    int32_t pk = 0;
    mix_block(buf, CTL);
    for (i = 0; i < CTL * 2u; i++) {
        int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
        pk = a > pk ? a : pk;
    }
    if (w)
        for (i = 0; i < CTL; i++)
            wav_put(w, buf[2 * i], buf[2 * i + 1]);
    if (w == wav_all)
        wav_all_frames += w ? CTL : 0;
    return pk;
}
static int32_t run_s(double s, FILE *w)
{
    uint32_t n = (uint32_t)(s * FS / CTL), i;
    int32_t pk = 0, p;
    for (i = 0; i < n; i++) {
        p = blk(w);
        pk = p > pk ? p : pk;
    }
    return pk;
}
static int voices_on(const track_t *t)
{
    int i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}

static void selva_part(track_t *t, uint32_t pi)
{
    host_tracks_init();
    host_preset(t, ENGI_SELVA, pi);
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = t->p[P_DIST] = 0;
    t->p[P_AMODE] = 0;
}

/* the part's own render (before the sends and the master): its peak over s seconds, every sample finite */
static int32_t part_run(track_t *t, double s, int32_t *bad)
{
    static int32_t out[CTL];
    uint32_t n = (uint32_t)(s * FS / CTL), i, j;
    int32_t pk = 0;
    for (i = 0; i < n; i++) {
        events_block(CTL);
        engine_block(t);
        track_render(t, out, CTL);
        for (j = 0; j < CTL; j++) {
            int32_t a = out[j] < 0 ? -out[j] : out[j];
            pk = a > pk ? a : pk;
            if (a > 400000)
                (*bad)++;
        }
    }
    return pk;
}

static void extremes(void)
{
    track_t *t = &trk[0];
    uint32_t w1, w2, r, k, n = 0;
    int32_t worst = 0, bad = 0;
    static const uint8_t NOTES[] = {24, 36, 60, 84, 108};
    for (w1 = 0; w1 < 6u; w1++)
        for (w2 = 0; w2 < 6u; w2++)
            for (r = 0; r < 5u; r++) {
                int8_t *x = selva_x[0];
                int32_t pk;
                selva_part(t, 0);
                selva_set(0, SX_W1, (int32_t)w1);
                selva_set(0, SX_W2, (int32_t)w2);
                selva_set(0, SX_R1, (int32_t)r);
                selva_set(0, SX_R2, (int32_t)(4u - r));
                selva_set(0, SX_L1, 127);
                selva_set(0, SX_L2, 127);
                selva_set(0, SX_SUBT, (int32_t)(r & 3u));
                selva_set(0, SX_SUBL, 127);
                selva_set(0, SX_NZC, (int32_t)(r & 1u));
                selva_set(0, SX_NZL, 127);
                selva_set(0, SX_SEMI, (int32_t)(r * 6u) - 12);
                selva_set(0, SX_FINE, (int32_t)(w1 * 20u) - 50);
                selva_set(0, SX_KTR, 3);
                selva_set(0, SX_DRFT, 127);
                t->p[P_E0] = (int16_t)((w1 + w2) & 1u ? 127 : 0);
                t->p[P_E1] = 127;
                t->p[P_E2] = (int16_t)(r & 1u ? 63 : -64);
                t->p[P_E3] = 127;
                t->p[P_E4] = 0;
                t->p[P_E5] = 40;
                t->p[P_E6] = 64;
                t->p[P_E7] = 30;
                t->p[P_LD_SHP] = 127;
                t->p[P_LRATE] = 100;
                t->p[P_VOICE] = (int16_t)(r == 4u ? V_UNISON : V_POLY);
                t->p[P_DETUNE] = 127;
                t->p[P_REL] = 30;
                (void)x;
                for (k = 0; k < NELEM(NOTES); k++)
                    input_on(t, NOTES[k], 127);
                pk = part_run(t, 0.25, &bad);
                for (k = 0; k < NELEM(NOTES); k++)
                    input_off(t, NOTES[k]);
                part_run(t, 3.0, &bad);
                worst = pk > worst ? pk : worst;
                CHECK(!voices_on(t), "extremes w%u/%u r%u: %d voices still on 3 s after the release", w1, w2, r,
                      voices_on(t));
                n++;
            }
    CHECK(!bad, "extremes: %d samples beyond 400000 at the part's output", bad);
    printf("selva: extremes: %u settings (waves x waves x ranges, RES / DRV / levels at the top), part peak %d "
           "(8 voices x VOICE_FS = %d), all released\n", n, worst, 8 * VOICE_FS);
}

/* the self-oscillation's frequency (zero crossings of the part's output) at RES 127 against CUTOFF_HZ */
static void tuning(void)
{
    track_t *t = &trk[0];
    static const uint8_t CUTS[] = {40, 60, 80, 100, 115};
    uint32_t k, i;
    for (k = 0; k < NELEM(CUTS); k++) {
        static int32_t out[CTL];
        int32_t prev = 0, bad = 0, pk = 0;
        uint32_t zc = 0, frames = 0;
        double hz, want = CUTOFF_HZ[CUTS[k]];
        selva_part(t, 0);
        selva_set(0, SX_L1, 0);
        selva_set(0, SX_NZL, 2);
        selva_set(0, SX_KTR, 0);
        selva_set(0, SX_DRFT, 0);
        t->p[P_E0] = CUTS[k];
        t->p[P_E1] = 127;
        t->p[P_E2] = 0;
        t->p[P_E3] = 0;
        t->p[P_SUS] = 127;
        input_on(t, 60, 100);
        part_run(t, 0.5, &bad);                      /* let it build up */
        for (i = 0; i < (uint32_t)(FS / CTL); i++) {    /* one second */
            uint32_t j;
            events_block(CTL);
            engine_block(t);
            track_render(t, out, CTL);
            for (j = 0; j < CTL; j++, frames++) {
                int32_t a = out[j] < 0 ? -out[j] : out[j];
                pk = a > pk ? a : pk;
                zc += (prev < 0) != (out[j] < 0);
                prev = out[j];
            }
        }
        input_off(t, 60);
        hz = zc / 2.0 * FS / frames;
        printf("selva: tuning: CUT %3u (%5.0f Hz): self-oscillation at %6.0f Hz (%+.0f cents), peak %d\n", CUTS[k], want,
               hz, 1200.0 * log2(hz / want), pk);
        CHECK(pk > 2000, "tuning: CUT %u: no self-oscillation at RES 127 (peak %d)", CUTS[k], pk);
        CHECK(fabs(1200.0 * log2(hz / want)) < 300.0, "tuning: CUT %u: %.0f Hz for %.0f Hz", CUTS[k], hz, want);
    }
}

/* the filter envelope: brightness (the energy of the first difference against the signal's, a block's worth of
 * spectral centroid) early in the note, at SUS 0 and after the key is let go */
static void fenv(void)
{
    track_t *t = &trk[0];
    static int32_t out[CTL];
    double num[3] = {0}, den[3] = {0}, br[3];
    uint32_t i, j, ph;
    int32_t prev = 0, bad = 0;
    selva_part(t, 0);
    selva_set(0, SX_W1, 2);
    selva_set(0, SX_DRFT, 0);
    t->p[P_E0] = 20;
    t->p[P_E1] = 0;
    t->p[P_E2] = 63;
    t->p[P_E4] = 0;
    t->p[P_E5] = 80;
    t->p[P_E6] = 0;
    t->p[P_E7] = 30;
    t->p[P_SUS] = 127;
    t->p[P_REL] = 90;
    input_on(t, 48, 100);
    for (ph = 0; ph < 3u; ph++) {
        if (ph == 2u)
            input_off(t, 48);
        for (i = 0; i < (uint32_t)(0.05 * FS / CTL); i++) {
            events_block(CTL);
            engine_block(t);
            track_render(t, out, CTL);
            for (j = 0; j < CTL; j++) {
                double d;
                if (!i && !j)
                    prev = out[0];                     /* (the first difference of a phase: inside it) */
                d = out[j] - prev;
                num[ph] += d * d;
                den[ph] += (double)out[j] * out[j];
                prev = out[j];
            }
        }
        br[ph] = den[ph] > 0 ? sqrt(num[ph] / den[ph]) * FS / (2 * M_PI) : 0;
        if (ph == 0u)
            part_run(t, 1.5, &bad);
    }
    printf("selva: F.ENV: brightness (Hz, first-difference estimate): attack %.0f, at SUS 0 %.0f, released %.0f; "
           "level at SUS 0 %.0f\n", br[0], br[1], br[2], sqrt(den[1] / (0.05 * FS)));
    CHECK(br[0] > 2.0 * br[1], "F.ENV: not brighter at the attack (%.0f Hz) than at SUS 0 (%.0f Hz)", br[0], br[1]);
    CHECK(den[1] > 0.05 * FS * 100.0, "F.ENV: a quiet note at a low cutoff died out (rms %.1f)", sqrt(den[1] / (0.05 * FS)));
}

/* a phrase for a preset: a bass line, held chords, a lead melody, plucked chords, by its kind */
static void play_demo(track_t *t, uint32_t kind, FILE *w)
{
    static const uint8_t BASS[] = {36, 36, 48, 36, 39, 36, 43, 34};
    static const uint8_t LEAD[] = {60, 63, 67, 70, 72, 70, 67, 63};
    static const uint8_t CH[2][4] = {{48, 55, 60, 63}, {46, 53, 58, 62}};
    uint32_t i, k;
    double beat = 60.0 / 124.0 / 2.0;                       /* eighths at 124 BPM */
    switch (kind) {
    case 0:                                                  /* bass */
        for (i = 0; i < 16u; i++) {
            input_on(t, BASS[i & 7u], i & 1u ? 90 : 120);
            run_s(beat * 0.6, w);
            input_off(t, BASS[i & 7u]);
            run_s(beat * 0.4, w);
        }
        break;
    case 1:                                                  /* chords held */
        for (k = 0; k < 2u; k++) {
            for (i = 0; i < 4u; i++)
                input_on(t, CH[k][i], 96);
            run_s(beat * 7.5, w);
            for (i = 0; i < 4u; i++)
                input_off(t, CH[k][i]);
            run_s(beat * 0.5, w);
        }
        break;
    case 2:                                                  /* stabs / plucks */
        for (i = 0; i < 8u; i++) {
            const uint8_t *c = CH[(i >> 2) & 1u];
            for (k = 0; k < 4u; k++)
                input_on(t, c[k], 110);
            run_s(beat * 0.5, w);
            for (k = 0; k < 4u; k++)
                input_off(t, c[k]);
            run_s(beat * (i & 1u ? 1.5 : 0.5), w);
        }
        break;
    default:                                                 /* lead */
        for (i = 0; i < 12u; i++) {
            input_on(t, LEAD[i % 8u], 110);
            run_s(beat * (i % 3u == 2u ? 1.6 : 0.95), w);
            input_off(t, LEAD[i % 8u]);
            run_s(beat * 0.05, w);
        }
        break;
    }
    run_s(1.2, w);
}

static FILE *wav_open(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f)
        wav_hdr(f, 0);
    return f;
}
static void wav_close(FILE *f)
{
    long n = ftell(f);
    uint32_t frames = (uint32_t)((n - 44) / 4);
    fseek(f, 0, SEEK_SET);
    wav_hdr(f, frames);
    fclose(f);
}

static void demos(const char *dir)
{
    /* kind of each factory preset: 0 bass, 1 chords held, 2 stabs / plucks, 3 lead */
    static const uint8_t KIND[] = {3, 0, 3, 0, 0, 2, 2, 1, 2, 0, 3, 0, 2, 1, 1};
    track_t *t = &trk[0];
    char path[512];
    uint32_t pi;
    _Static_assert(NELEM(KIND) == NELEM(SELVA_PRESETS), "a demo kind per Selva1 preset");
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/selva1-demo.wav", dir);
    wav_all = wav_open(path);
    for (pi = 0; pi < NELEM(SELVA_PRESETS); pi++) {
        FILE *one;
        char name[32];
        uint32_t i;
        for (i = 0; SELVA_PRESETS[pi].name[i] && i < 31u; i++)
            name[i] = SELVA_PRESETS[pi].name[i] == ' ' ? '-' : (char)(SELVA_PRESETS[pi].name[i] | 0x20);
        name[i] = 0;
        snprintf(path, sizeof path, "%s/%02u-%s.wav", dir, pi + 1u, name);
        host_tracks_init();
        host_preset(t, ENGI_SELVA, pi);
        song.g[G_BPM] = 124;
        one = wav_open(path);
        play_demo(t, KIND[pi], one);
        wav_close(one);
        host_tracks_init();                       /* again, for the long file: the same notes from the same state */
        host_preset(t, ENGI_SELVA, pi);
        song.g[G_BPM] = 124;
        play_demo(t, KIND[pi], wav_all);
    }
    wav_close(wav_all);
    printf("selva: demos: %u presets -> %s/selva1-demo.wav (%.1f s) and one file each\n", (unsigned)NELEM(SELVA_PRESETS),
           dir, wav_all_frames / (double)FS);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/selva";
    extremes();
    tuning();
    fenv();
    demos(dir);
    printf("selva: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
