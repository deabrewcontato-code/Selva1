/* Selva1 · the engine's C interface, as the plugin sees it. Copyright (C) 2026 DeAbrew. GPL-3.0-or-later. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int s1_ctx_size(void);
void s1_bind(void *ctx);
void s1_prepare(void);
void s1_init(float rate);
void s1_set(int id, float v);
float s1_get(int id);
int s1_nparams(void);
int s1_nall(void);
void s1_note_on(int note, int vel);
void s1_note_off(int note);
void s1_cc(int cc, int val);
void s1_bend(float b);
void s1_aftertouch(float a);
void s1_panic(void);
void s1_transport(int play);
void s1_transport_from(double beats);
void s1_host_tempo(float bpm);
void s1_render(float *l, float *r, int n);
void s1_ui_state(float *out);
const char *s1_info(void);
#ifdef __cplusplus
}
#endif
