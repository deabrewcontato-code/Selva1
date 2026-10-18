/* SPDX-License-Identifier: GPL-3.0-only */
/* Browser audio adapter. The DSP and clock below are the firmware sources;
 * hardware inputs and user flash are replaced by RAM, never by Web MIDI. */
#include <stdint.h>
#include <string.h>
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "../src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
#include "felucca_tables.h"
static uint8_t user_flash[4][0x14000];
#define SMP_USER_XIP(k) (user_flash[(k) & 3u])
static struct { volatile uint32_t notes, buttons; } fm1_in;
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
#include "../src/core.h"
#include "../src/engines.c"
#include "../src/drums.c"
#include "../src/params.c"
#include "../src/voice.c"
#include "../src/slicer.c"
#include "../src/cpu1.h"                        /* (2.7) no second core in the browser: every unit renders here */
static int cpu1_run(cpu1_job_t fn, uint32_t arg) { (void)fn; (void)arg; return 0; }
static int cpu1_wait(void) { return 0; }
static int cpu1_on(void) { return 0; }
static uint32_t cpu1_ticks(void) { return 0; }
#include "../src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../src/usb.c"
#include "../src/midi_uart.c"
#include "../src/seq.c"

static uint8_t transfer[SMP_USER_SIZE];
static int32_t block[CTL * 2];
static float output[CTL * 2];

void preview_init(void)
{
    uint32_t i, k;
    memset(user_flash, 0xFF, sizeof user_flash);
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    song.g[G_SYNC] = 0;
    song.master_q12 = 4096;
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_COUNT; i++) trk[k].p[i] = TP[i].def;
        steps_clear(&trk[k]);
    }
    dsu_defaults();
    fm6_init();
}
uint8_t *preview_buffer(void) { return transfer; }
uint32_t preview_frames(void) { return CTL; }
uint32_t preview_rate(void) { return FS; }
void preview_track(uint32_t k, uint32_t engine, uint32_t preset)
{
    if (k >= NPART || engine >= NENGINES) return;
    track_t *t = &trk[k];
    if (t->eng_req != engine || t->preset != preset) sound_change(t);
    t->eng_req = (uint8_t)engine;
    t->preset = (uint8_t)preset;
    selva_track_loaded(t);                       /* Selva1: the preset's oscillators (the page may then send its own) */
}
void preview_param(uint32_t k, uint32_t id, int32_t value)
{
    if (k >= NTRK || id >= P_COUNT) return;
    track_t *t = &trk[k];
    const param_desc_t *d = is_drum(t) && id == P_E0 ? &DRUM_KIT_DESC
        : id >= P_E0 ? &ENGINES[t->eng_req % NENGINES]->edit[id - P_E0] : &TP[id];
    t->p[id] = (int16_t)clamp(value, d->min, d->max);
    /* An edit made while a lock is active changes its base too. */
    for (uint32_t i = 0; i < t->lk_n; i++)
        if (t->lk_param[i] == id) t->lk_base[i] = t->p[id];
}
void preview_global(uint32_t id, int32_t value)
{
    if (id < G_COUNT && id != G_SYNC) song.g[id] = (int16_t)clamp(value, GP[id].min, GP[id].max);
}
void preview_select(uint32_t k, uint32_t solo)
{
    song.sel = (uint8_t)(k % NTRK);
    song.solo = (uint8_t)(solo & 15u);
}
void preview_step(uint32_t k, uint32_t i)
{
    if (k >= NTRK || i >= NSTEP) return;
    if (k == TRK_DRUM) memcpy(&trk[k].dstep[i], transfer, sizeof(dstep_t));
    else {
        step_t *s = &trk[k].step[i];
        memcpy(s, transfer, sizeof *s);
        s->n = (uint8_t)clamp(s->n, 0, 4); s->time %= 3u;
        for (uint32_t j = 0; j < 4; j++) s->note[j] &= 127u;
        s->vel &= 127u; s->flags &= 3u;
    }
    trk[k].micro[i] = (int8_t)clamp((int8_t)transfer[10], MICRO_MIN, MICRO_MAX);
    uint32_t shift = (i & 3u) * 2u;
    trk[k].fill[i / 4] = (uint8_t)((trk[k].fill[i / 4] & ~(3u << shift)) | ((transfer[11] & 3u) << shift));
}
void preview_locks(uint32_t k, uint32_t count)
{
    if (k >= NTRK) return;
    track_t *t = &trk[k];
    locks_restore(t);
    for (uint32_t j = 0; j < NLOCK; j++) t->lock[j].step = LOCK_FREE;
    for (uint32_t j = 0; j < count && j < NLOCK; j++) {
        const uint8_t *v = transfer + j * 4;
        int16_t value = (int16_t)(v[2] | (v[3] << 8));
        lock_set(t, v[0], v[1], value);
    }
}
void preview_fm6(uint32_t k)
{
    if (k < NPART) {
        uint8_t voice[FP_SIZE + 1u];
        fm6_unpack(transfer, voice);
        fm6_set_patch(k, voice);
        fm6_slot[k] = (uint8_t)trk[k].p[P_E7];
    }
}
void preview_sample(uint32_t k)
{
    if (k >= SMP_USER_SLOTS) return;
    memcpy(user_flash[k], transfer, SMP_USER_SIZE);
    smp_user_scan(k);
}
void preview_kit(uint32_t k)
{
    if (k >= DSU_N) return;
    memcpy(&dsu.k[k], transfer, sizeof(dsu_kit_t));
    dsu.k[k].crush &= 127u;
    for (uint32_t l = 0; l < DS_LANES; l++) dsu_fix_sound(&dsu.k[k].s[l]);
}
void preview_hit(uint32_t kit, uint32_t lane, uint32_t velocity)
{
    if (kit >= DSU_N || lane >= DRUM_LANES) return;
    int16_t old = TDRUM->p[P_E0];
    TDRUM->p[P_E0] = (int16_t)(DRUM_SYN + kit);
    drum_on(LANE_NOTE[lane], (uint8_t)clamp(velocity, 1, 127));
    TDRUM->p[P_E0] = old;
}
void preview_note(uint32_t k, uint32_t note, uint32_t on)
{
    if (k >= NTRK || note > 127) return;
    if (on) input_on(&trk[k], note, 100);
    else input_off(&trk[k], note);
}
/* Selva1 (eng_selva.c): one value of part k's oscillators and mixer, and the descriptors the web panel reads */
void preview_selva(uint32_t k, uint32_t id, int32_t value) { selva_set(k, id, value); }
int32_t preview_selva_get(uint32_t k, uint32_t id) { return k < NPART && id < SELVA_NX ? selva_of(&trk[k])[id] : 0; }
/* a note with its velocity (the web panel's keys, MIDI) */
void preview_notev(uint32_t k, uint32_t note, uint32_t velocity)
{
    if (k >= NTRK || note > 127) return;
    if (velocity) input_on(&trk[k], note, (uint8_t)clamp(velocity, 1, 127));
    else input_off(&trk[k], note);
}
/* a parameter's range for the web panel: what 0 track (P_*), 1 Selva1's own (SX_*), 2 global (G_*); field 0 min 1 max
 * 2 default; for Selva1's P_E0.. the engine's own descriptors */
int32_t preview_desc(uint32_t what, uint32_t id, uint32_t field)
{
    const param_desc_t *d = what == 1 ? (id < SELVA_NX ? &SELVA_XD[id] : 0)
        : what == 2 ? (id < G_COUNT ? &GP[id] : 0)
        : id >= P_COUNT ? 0 : id >= P_E0 ? &ENG_SELVA.edit[id - P_E0] : &TP[id];
    if (!d) return 0;
    return field == 0 ? d->min : field == 1 ? d->max : d->def;
}
uint32_t preview_selva_engine(void) { return ENGI_SELVA; }
/* a factory preset of the Selva1 on part k, as the FM-1 loads it (ui.c apply_preset_to): the sound's parameters to
 * their defaults, then the preset's (EDIT, ENV, sends, extras, level trim) and its oscillators; the page then reads
 * every value back (preview_get, preview_selva_get) */
void preview_preset(uint32_t k, uint32_t pi)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    if (k >= NPART) return;
    track_t *t = &trk[k];
    const engine_t *e = &ENG_SELVA;
    const preset_t *pr;
    uint32_t i;
    pi %= e->npresets;
    pr = &e->presets[pi];
    sound_change(t);
    panic_req |= (uint8_t)(1u << k);
    t->eng_req = (uint8_t)ENGI_SELVA;
    t->preset = (uint8_t)pi;
    for (i = 0; i < P_E0; i++)
        if (i != P_LEVEL && i != P_PAN && i != P_MUTE && !(i >= P_SLEN && i <= P_SGATE))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_ED_FX] = preset_trim(ENGI_SELVA, pi);
    t->p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;
    for (i = 0; i < 4u; i++) {
        t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
        t->p[P_AMODE + i] = (int16_t)(pr->arp[i] ? pr->arp[i] - 1 : TP[P_AMODE + i].def);
    }
    preset_extras(t->p, pr);
    selva_track_loaded(t);
}
int32_t preview_get(uint32_t k, uint32_t id) { return k < NTRK && id < P_COUNT ? trk[k].p[id] : 0; }
int32_t preview_gget(uint32_t id) { return id < G_COUNT ? song.g[id] : 0; }
uint32_t preview_npresets(void) { return ENG_SELVA.npresets; }
/* the preset's name: copied into the transfer buffer, NUL-terminated; returns its length */
uint32_t preview_preset_name(uint32_t pi)
{
    const char *n = ENG_SELVA.presets[pi % ENG_SELVA.npresets].name;
    uint32_t i = 0;
    while (n[i] && i < 31u) { transfer[i] = (uint8_t)n[i]; i++; }
    transfer[i] = 0;
    return i;
}

void preview_play(uint32_t on) { transport_req = on ? 1 : 2; }
int32_t preview_position(uint32_t k)
{
    if (k >= NTRK || !song.playing || trk[k].seq_abs == SEQ_NONE) return -1;
    return trk[k].seq_idx;
}
void preview_panic(void)
{
    transport_req = 2;
    panic_req = 15;
    /* ARP HOLD must also release when the browser loses focus. */
    for (uint32_t k = 0; k < NPART; k++) {
        trk[k].nheld = trk[k].arp_phys = trk[k].latched = 0;
        trk[k].arp_note = 0;
    }
}
float *preview_render(void)
{
    fm6_poll();
    mix_block(block, CTL);
    for (uint32_t i = 0; i < CTL * 2; i++) output[i] = clamp(block[i], -32768, 32767) / 32768.0f;
    return output;
}
