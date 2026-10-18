/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP boot splash: the logo (tools/gen_logo.py -> sloop_logo.h: 16 colours, RLE runs of
 * (run - 1) << 4 | colour), drawn into the canvas in bands of at most 120 rows. */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
#ifndef FELUCCA_LOGO_TEXT       /* Selva1: the boot logo as text, "SELVA" and a red "1" (also 4.6 KB of flash less than the
                                 * SLOOP picture) */
#define FELUCCA_LOGO_TEXT 1
#endif
#if FELUCCA_LOGO_TEXT
UI_SMALL static void sloop_logo_draw(uint32_t y0)
{
    static const char L[6] = {'S', 'E', 'L', 'V', 'A', '1'};
    uint32_t i, w = text_w(&FONT_L, "SELVA1"), k = w * 3u <= 232u ? 3u : 2u, h = 32u, off, y, x;
    int32_t px = 0;
    char c[2] = {0, 0};
    cv_begin(240, h * k, C_BLACK);
    for (i = 0; i < 6u; i++) {                          /* small, at the top left: SELVA cream, the 1 red (DeAbrew) */
        c[0] = L[i];
        px = cv_text(px, 0, &FONT_L, c, i == 5u ? RGB(198, 63, 62) : RGB(242, 233, 213));
    }
    off = (240u - w * k) / 2u;
    for (y = h * k; y-- > 0;)                           /* then k times as big, in place (from the end: nothing read is
                                                         * overwritten before) */
        for (x = w * k; x-- > 0;)
            cv_px[y * 240u + off + x] = cv_px[(y / k) * 240u + x / k];
    for (y = 0; y < h * k; y++)                         /* (the small one's left-over columns) */
        for (x = 0; x < off; x++)
            cv_px[y * 240u + x] = swap16(C_BLACK);
    cv_blit(0, y0 + 60u);
}
#else
#include "sloop_logo.h"
UI_SMALL static void sloop_logo_draw(uint32_t y0)
{
    uint32_t r0;
    for (r0 = 0; r0 < SLOOP_SPLASH_H; r0 += 120u) {
        uint32_t rows = SLOOP_SPLASH_H - r0 > 120u ? 120u : SLOOP_SPLASH_H - r0;
        uint32_t lo = r0 * SLOOP_SPLASH_W, hi = (r0 + rows) * SLOOP_SPLASH_W, pos = 0, i;
        cv_begin(SLOOP_SPLASH_W, rows, C_BLACK);
        for (i = 0; i < sizeof SLOOP_SPLASH_RLE && pos < hi; i++) {
            uint32_t run = (SLOOP_SPLASH_RLE[i] >> 4) + 1u, c = SLOOP_SPLASH_RLE[i] & 15u, p;
            if (c && pos + run > lo)
                for (p = pos < lo ? lo : pos; p < pos + run && p < hi; p++)
                    cv_px[p - lo] = swap16(SLOOP_SPLASH_PAL[c]);
            pos += run;
        }
        cv_blit((240u - SLOOP_SPLASH_W) / 2u, y0 + r0);
    }
}
#endif

/* the boot screen: the logo, the version, where it comes from */
UI_SMALL static void sloop_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sloop_logo_draw(10);
    const char *v = FELUCCA_VERSION;
    if (v[0] == 'S' && v[1] == 'E' && v[6] == ' ')
        v += 7;                                         /* "SELVA1 0.1" -> "0.1" under the wordmark */
    cv_begin(240, 36, C_BLACK);
    cv_text(120 - text_w(&FONT_S, v) / 2, 0, &FONT_S, v, RGB(196, 196, 204));
    cv_text(120 - text_w(&FONT_S, "deabrew / sloop / felucca") / 2, 18, &FONT_S, "deabrew / sloop / felucca", RGB(96, 96, 104));
    cv_blit(0, 202);
}
