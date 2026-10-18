/* Selva1 · the parameter list. One line per parameter: the engine, the web panel (through the JSON that
 * s1_info() writes), the presets and the future plugin all read it from here.
 *   X(id, key, label, kind, min, max, def, options)
 * kinds: K_LIN linear, K_EXP exponential (min > 0), K_INT integer, K_ENUM a list (options), K_BOOL off/on,
 * K_BIP bipolar linear. Modulation works on the normalised value (0..1 across the range, exponential where
 * the kind is), so an amount means the same share of every knob. Copyright (C) 2026 DeAbrew. */
#ifndef S1_PARAMS_H
#define S1_PARAMS_H

enum { K_LIN, K_EXP, K_INT, K_ENUM, K_BOOL, K_BIP };

#define O_NONE 0
static const char *const O_ONOFF[] = {"OFF", "ON", 0};
static const char *const O_VMODE[] = {"POLY", "MONO", "LEGATO", 0};
static const char *const O_OSCMODE[] = {"WAVETABLE", "ANALOG", "FM", "NOISE", 0};
static const char *const O_AWAVE[] = {"SAW", "SQUARE", "PULSE", "TRI", "SINE", "SHARK", 0};
static const char *const O_RATIO[] = {"0.5", "1", "1.5", "2", "3", "4", "5", "6", "7", "8", "11", "13", 0};
static const float RATIO_V[] = {0.5f, 1.f, 1.5f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 11.f, 13.f};
static const char *const O_SUBW[] = {"SINE", "SQUARE", 0};
static const char *const O_SUBOCT[] = {"-1", "-2", 0};
static const char *const O_NTYPE[] = {"WHITE", "PINK", "BROWN", "INDUSTRIAL", "DIGITAL", "VINYL", 0};
static const char *const O_FTYPE[] = {"MOOG LADDER", "CLEAN SVF", 0};
static const char *const O_FSLOPE[] = {"12", "24", 0};
static const char *const O_FMODE[] = {"LP", "BP", "HP", 0};
static const char *const O_LSHAPE[] = {"SINE", "TRI", "SAW UP", "SAW DOWN", "SQUARE", "S&H", "DRIFT", 0};
static const char *const O_LSYNC[] = {"HZ", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T",
                                      "1/16T", "1/4D", "1/8D", 0};
static const float LSYNC_BEATS[] = {0, 16, 8, 4, 2, 1, 0.5f, 0.25f, 0.125f, 2.f / 3.f, 1.f / 3.f, 1.f / 6.f, 1.5f, 0.75f};
static const char *const O_LMODE[] = {"FREE", "RETRIG", "ONE SHOT", 0};
static const char *const O_DTYPE[] = {"TAPE", "TUBE", "HARD", "FOLD", "CRUSH", 0};
static const char *const O_DLYT[] = {"1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2",
                                     "1/1", 0};
static const float DLYT_BEATS[] = {0.125f, 1.f / 6.f, 0.25f, 0.375f, 1.f / 3.f, 0.5f, 0.75f, 2.f / 3.f, 1.f, 1.5f, 2.f, 4.f};
static const char *const O_SEQRATE[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4", 0};
static const float SEQRATE_BEATS[] = {0.125f, 1.f / 6.f, 0.25f, 1.f / 3.f, 0.5f, 1.f};
static const char *const O_SEQDIR[] = {"FWD", "BWD", "PING-PONG", "RANDOM", 0};
static const char *const O_ARPMODE[] = {"UP", "DOWN", "UP-DOWN", "RANDOM", "ORDER", "CHORD", 0};
static const char *const O_ARPRATE[] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", 0};
static const float ARPRATE_BEATS[] = {1.f, 0.5f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f};

/* the modulation sources */
#define S1_SOURCES(S) \
    S(SRC_NONE, "NONE") S(SRC_ENV1, "ENV 1") S(SRC_ENV2, "ENV 2") S(SRC_ENV3, "ENV 3") \
    S(SRC_LFO1, "LFO 1") S(SRC_LFO2, "LFO 2") S(SRC_LFO3, "LFO 3") S(SRC_VELO, "VELO") S(SRC_KEY, "KEY") \
    S(SRC_MODWH, "MOD WH") S(SRC_AFTER, "AFTERTOUCH") S(SRC_BEND, "BEND") \
    S(SRC_MAC1, "MACRO 1") S(SRC_MAC2, "MACRO 2") S(SRC_MAC3, "MACRO 3") S(SRC_MAC4, "MACRO 4") \
    S(SRC_RAND, "RANDOM") S(SRC_SEQ1, "SEQ MOD 1") S(SRC_SEQ2, "SEQ MOD 2") S(SRC_SEQ3, "SEQ MOD 3") S(SRC_SEQ4, "SEQ MOD 4")

#define S1_OSC_PARAMS(X, L, l, DON, DMODE, DWAVE) \
    X(P_OSC##L##_ON, "osc." l ".on", "OSC " #L, K_BOOL, 0, 1, DON, O_ONOFF) \
    X(P_OSC##L##_MODE, "osc." l ".mode", "MODE", K_ENUM, 0, 3, DMODE, O_OSCMODE) \
    X(P_OSC##L##_TABLE, "osc." l ".table", "TABLE", K_INT, 0, 63, 0, O_NONE) \
    X(P_OSC##L##_POS, "osc." l ".pos", "POS", K_LIN, 0, 1, 0, O_NONE) \
    X(P_OSC##L##_WAVE, "osc." l ".wave", "WAVE", K_ENUM, 0, 5, DWAVE, O_AWAVE) \
    X(P_OSC##L##_RATIO, "osc." l ".ratio", "RATIO", K_ENUM, 0, 11, 1, O_RATIO) \
    X(P_OSC##L##_OCT, "osc." l ".oct", "OCT", K_INT, -3, 3, 0, O_NONE) \
    X(P_OSC##L##_SEMI, "osc." l ".semi", "SEMI", K_INT, -12, 12, 0, O_NONE) \
    X(P_OSC##L##_FINE, "osc." l ".fine", "FINE", K_BIP, -100, 100, 0, O_NONE) \
    X(P_OSC##L##_UNI, "osc." l ".unison", "UNISON", K_INT, 1, 8, 1, O_NONE) \
    X(P_OSC##L##_DET, "osc." l ".detune", "DETUNE", K_LIN, 0, 1, 0.25f, O_NONE) \
    X(P_OSC##L##_WIDTH, "osc." l ".width", "WIDTH", K_LIN, 0, 1, 0.7f, O_NONE) \
    X(P_OSC##L##_PAN, "osc." l ".pan", "PAN", K_BIP, -1, 1, 0, O_NONE) \
    X(P_OSC##L##_LEVEL, "osc." l ".level", "LEVEL", K_LIN, 0, 1, 0.75f, O_NONE)

#define S1_ENV_PARAMS(X, N, n, DA, DD, DS, DR) \
    X(P_ENV##N##_A, "env" n ".a", "ATTACK", K_EXP, 0.0005f, 20, DA, O_NONE) \
    X(P_ENV##N##_D, "env" n ".d", "DECAY", K_EXP, 0.001f, 20, DD, O_NONE) \
    X(P_ENV##N##_S, "env" n ".s", "SUSTAIN", K_LIN, 0, 1, DS, O_NONE) \
    X(P_ENV##N##_R, "env" n ".r", "RELEASE", K_EXP, 0.001f, 20, DR, O_NONE)

#define S1_LFO_PARAMS(X, N, n) \
    X(P_LFO##N##_SHAPE, "lfo" n ".shape", "SHAPE", K_ENUM, 0, 6, 0, O_LSHAPE) \
    X(P_LFO##N##_RATE, "lfo" n ".rate", "RATE", K_EXP, 0.02f, 40, 2, O_NONE) \
    X(P_LFO##N##_SYNC, "lfo" n ".sync", "SYNC", K_ENUM, 0, 13, 0, O_LSYNC) \
    X(P_LFO##N##_MODE, "lfo" n ".mode", "MODE", K_ENUM, 0, 2, 0, O_LMODE) \
    X(P_LFO##N##_SMOOTH, "lfo" n ".smooth", "SMOOTH", K_LIN, 0, 1, 0, O_NONE)

#define S1_MOD_PARAMS(X, N, n) \
    X(P_MOD##N##_SRC, "mod" n ".src", "SOURCE", K_ENUM, 0, 20, 0, O_NONE) \
    X(P_MOD##N##_DST, "mod" n ".dst", "DEST", K_ENUM, 0, 63, 0, O_NONE) \
    X(P_MOD##N##_AMT, "mod" n ".amt", "AMOUNT", K_BIP, -1, 1, 0, O_NONE)

#define S1_PARAMS(X) \
    X(P_VMODE, "voice.mode", "MODE", K_ENUM, 0, 2, 0, O_VMODE) \
    X(P_VOICES, "voice.count", "VOICES", K_INT, 1, 16, 8, O_NONE) \
    X(P_GLIDE, "voice.glide", "GLIDE", K_EXP, 0.0005f, 2, 0.0005f, O_NONE) \
    X(P_BEND, "voice.bend", "BEND", K_INT, 1, 24, 2, O_NONE) \
    X(P_VELSENS, "voice.velo", "VELOCITY", K_LIN, 0, 1, 0.6f, O_NONE) \
    X(P_TUNE, "global.tune", "TUNE", K_BIP, -100, 100, 0, O_NONE) \
    X(P_BPM, "global.bpm", "BPM", K_INT, 40, 240, 124, O_NONE) \
    X(P_AMP_LEVEL, "amp.level", "LEVEL", K_LIN, 0, 1, 0.7f, O_NONE) \
    X(P_AMP_PAN, "amp.pan", "PAN", K_BIP, -1, 1, 0, O_NONE) \
    S1_OSC_PARAMS(X, A, "a", 1, 1, 0) \
    S1_OSC_PARAMS(X, B, "b", 0, 0, 0) \
    S1_OSC_PARAMS(X, C, "c", 0, 1, 1) \
    X(P_SUB_ON, "sub.on", "SUB", K_BOOL, 0, 1, 0, O_ONOFF) \
    X(P_SUB_WAVE, "sub.wave", "WAVE", K_ENUM, 0, 1, 0, O_SUBW) \
    X(P_SUB_OCT, "sub.oct", "OCT", K_ENUM, 0, 1, 0, O_SUBOCT) \
    X(P_SUB_LEVEL, "sub.level", "LEVEL", K_LIN, 0, 1, 0.6f, O_NONE) \
    X(P_NOISE_ON, "noise.on", "NOISE", K_BOOL, 0, 1, 0, O_ONOFF) \
    X(P_NOISE_TYPE, "noise.type", "TYPE", K_ENUM, 0, 5, 0, O_NTYPE) \
    X(P_NOISE_LEVEL, "noise.level", "LEVEL", K_LIN, 0, 1, 0.3f, O_NONE) \
    X(P_NOISE_COLOR, "noise.color", "COLOR", K_BIP, -1, 1, 0, O_NONE) \
    X(P_FLT_ON, "flt.on", "FILTER", K_BOOL, 0, 1, 1, O_ONOFF) \
    X(P_FLT_TYPE, "flt.type", "TYPE", K_ENUM, 0, 1, 0, O_FTYPE) \
    X(P_FLT_SLOPE, "flt.slope", "SLOPE", K_ENUM, 0, 1, 1, O_FSLOPE) \
    X(P_FLT_MODE, "flt.mode", "MODE", K_ENUM, 0, 2, 0, O_FMODE) \
    X(P_FLT_CUT, "flt.cutoff", "CUTOFF", K_EXP, 20, 20000, 20000, O_NONE) \
    X(P_FLT_RES, "flt.res", "RESONANCE", K_LIN, 0, 1, 0, O_NONE) \
    X(P_FLT_DRIVE, "flt.drive", "DRIVE", K_LIN, 0, 1, 0.1f, O_NONE) \
    X(P_FLT_KEY, "flt.key", "KEY", K_LIN, 0, 1, 0.5f, O_NONE) \
    X(P_FLT_A, "flt.route.a", "A", K_BOOL, 0, 1, 1, O_ONOFF) \
    X(P_FLT_B, "flt.route.b", "B", K_BOOL, 0, 1, 1, O_ONOFF) \
    X(P_FLT_C, "flt.route.c", "C", K_BOOL, 0, 1, 1, O_ONOFF) \
    X(P_FLT_SUB, "flt.route.sub", "SUB", K_BOOL, 0, 1, 1, O_ONOFF) \
    X(P_FLT_NOISE, "flt.route.noise", "NOISE", K_BOOL, 0, 1, 1, O_ONOFF) \
    S1_ENV_PARAMS(X, 1, "1", 0.002f, 0.4f, 0.85f, 0.25f) \
    S1_ENV_PARAMS(X, 2, "2", 0.002f, 0.5f, 0.3f, 0.4f) \
    S1_ENV_PARAMS(X, 3, "3", 0.3f, 1.0f, 0.5f, 1.0f) \
    S1_LFO_PARAMS(X, 1, "1") \
    S1_LFO_PARAMS(X, 2, "2") \
    S1_LFO_PARAMS(X, 3, "3") \
    S1_MOD_PARAMS(X, 1, "1") S1_MOD_PARAMS(X, 2, "2") S1_MOD_PARAMS(X, 3, "3") S1_MOD_PARAMS(X, 4, "4") \
    S1_MOD_PARAMS(X, 5, "5") S1_MOD_PARAMS(X, 6, "6") S1_MOD_PARAMS(X, 7, "7") S1_MOD_PARAMS(X, 8, "8") \
    S1_MOD_PARAMS(X, 9, "9") S1_MOD_PARAMS(X, 10, "10") S1_MOD_PARAMS(X, 11, "11") S1_MOD_PARAMS(X, 12, "12") \
    S1_MOD_PARAMS(X, 13, "13") S1_MOD_PARAMS(X, 14, "14") S1_MOD_PARAMS(X, 15, "15") S1_MOD_PARAMS(X, 16, "16") \
    X(P_MAC1, "macro1", "MACRO 1", K_LIN, 0, 1, 0, O_NONE) \
    X(P_MAC2, "macro2", "MACRO 2", K_LIN, 0, 1, 0, O_NONE) \
    X(P_MAC3, "macro3", "MACRO 3", K_LIN, 0, 1, 0, O_NONE) \
    X(P_MAC4, "macro4", "MACRO 4", K_LIN, 0, 1, 0, O_NONE) \
    X(P_DIST_ON, "fx.dist.on", "DIST", K_BOOL, 0, 1, 0, O_ONOFF) \
    X(P_DIST_TYPE, "fx.dist.type", "TYPE", K_ENUM, 0, 4, 0, O_DTYPE) \
    X(P_DIST_DRIVE, "fx.dist.drive", "DRIVE", K_LIN, 0, 1, 0.35f, O_NONE) \
    X(P_DIST_TONE, "fx.dist.tone", "TONE", K_LIN, 0, 1, 0.7f, O_NONE) \
    X(P_DIST_MIX, "fx.dist.mix", "MIX", K_LIN, 0, 1, 1, O_NONE) \
    X(P_CHO_ON, "fx.cho.on", "CHORUS", K_BOOL, 0, 1, 0, O_ONOFF) \
    X(P_CHO_RATE, "fx.cho.rate", "RATE", K_EXP, 0.05f, 5, 0.6f, O_NONE) \
    X(P_CHO_DEPTH, "fx.cho.depth", "DEPTH", K_LIN, 0, 1, 0.5f, O_NONE) \
    X(P_CHO_MIX, "fx.cho.mix", "MIX", K_LIN, 0, 1, 0.45f, O_NONE) \
    X(P_DLY_ON, "fx.dly.on", "DELAY", K_BOOL, 0, 1, 0, O_ONOFF) \
    X(P_DLY_TIME, "fx.dly.time", "TIME", K_ENUM, 0, 11, 6, O_DLYT) \
    X(P_DLY_FB, "fx.dly.fb", "FEEDBACK", K_LIN, 0, 0.95f, 0.38f, O_NONE) \
    X(P_DLY_TONE, "fx.dly.tone", "TONE", K_LIN, 0, 1, 0.55f, O_NONE) \
    X(P_DLY_PING, "fx.dly.ping", "PING-PONG", K_BOOL, 0, 1, 1, O_ONOFF) \
    X(P_DLY_MIX, "fx.dly.mix", "MIX", K_LIN, 0, 1, 0.25f, O_NONE) \
    X(P_REV_ON, "fx.rev.on", "REVERB", K_BOOL, 0, 1, 0, O_ONOFF) \
    X(P_REV_SIZE, "fx.rev.size", "SIZE", K_LIN, 0, 1, 0.6f, O_NONE) \
    X(P_REV_DAMP, "fx.rev.damp", "DAMP", K_LIN, 0, 1, 0.5f, O_NONE) \
    X(P_REV_MIX, "fx.rev.mix", "MIX", K_LIN, 0, 1, 0.25f, O_NONE) \
    X(P_COMP_ON, "fx.comp.on", "COMP", K_BOOL, 0, 1, 0, O_ONOFF) \
    X(P_COMP_AMT, "fx.comp.amount", "AMOUNT", K_LIN, 0, 1, 0.4f, O_NONE) \
    X(P_COMP_MIX, "fx.comp.mix", "MIX", K_LIN, 0, 1, 1, O_NONE) \
    X(P_SEQ_RATE, "seq.rate", "RATE", K_ENUM, 0, 5, 2, O_SEQRATE) \
    X(P_SEQ_LEN, "seq.len", "LENGTH", K_INT, 1, 16, 16, O_NONE) \
    X(P_SEQ_DIR, "seq.dir", "DIRECTION", K_ENUM, 0, 3, 0, O_SEQDIR) \
    X(P_SEQ_OCT, "seq.oct", "OCT", K_INT, -2, 2, 0, O_NONE) \
    X(P_SEQ_SWING, "seq.swing", "SWING", K_LIN, 0, 0.6f, 0, O_NONE) \
    X(P_ARP_ON, "arp.on", "ARP", K_BOOL, 0, 1, 0, O_ONOFF) \
    X(P_ARP_MODE, "arp.mode", "MODE", K_ENUM, 0, 5, 0, O_ARPMODE) \
    X(P_ARP_RATE, "arp.rate", "RATE", K_ENUM, 0, 5, 3, O_ARPRATE) \
    X(P_ARP_OCT, "arp.oct", "OCT", K_INT, 1, 4, 1, O_NONE) \
    X(P_ARP_GATE, "arp.gate", "GATE", K_LIN, 0.05f, 1, 0.6f, O_NONE) \
    X(P_ARP_SWING, "arp.swing", "SWING", K_LIN, 0, 0.75f, 0, O_NONE) \
    X(P_ARP_CHANCE, "arp.chance", "CHANCE", K_LIN, 0, 1, 1, O_NONE) \
    X(P_ARP_HOLD, "arp.hold", "HOLD", K_BOOL, 0, 1, 0, O_ONOFF)

#define S1_P_ENUM(id, key, label, kind, mn, mx, def, opts) id,
enum { S1_PARAMS(S1_P_ENUM) S1_NPARAM };

/* the sequencer's steps: 16 x 8 lanes after the parameters (ids S1_NPARAM ..) */
#define S1_STEPS 16
enum { LANE_GATE, LANE_NOTE, LANE_VELO, LANE_PROB, LANE_M1, LANE_M2, LANE_M3, LANE_M4, S1_LANES };
#define S1_SEQ_ID(step, lane) (S1_NPARAM + (step) * S1_LANES + (lane))
#define S1_NALL (S1_NPARAM + S1_STEPS * S1_LANES)

/* the modulation destinations: a parameter (normalised), or a virtual one (PITCH: semitones, +-24 at full) */
#define S1_DESTS(D) \
    D(DST_NONE, -1, "NONE") \
    D(DST_A_POS, P_OSCA_POS, "OSC A POS") D(DST_A_PITCH, -2, "OSC A PITCH") D(DST_A_DET, P_OSCA_DET, "OSC A DETUNE") \
    D(DST_A_PAN, P_OSCA_PAN, "OSC A PAN") D(DST_A_LEVEL, P_OSCA_LEVEL, "OSC A LEVEL") \
    D(DST_B_POS, P_OSCB_POS, "OSC B POS") D(DST_B_PITCH, -2, "OSC B PITCH") D(DST_B_DET, P_OSCB_DET, "OSC B DETUNE") \
    D(DST_B_PAN, P_OSCB_PAN, "OSC B PAN") D(DST_B_LEVEL, P_OSCB_LEVEL, "OSC B LEVEL") \
    D(DST_C_POS, P_OSCC_POS, "OSC C POS") D(DST_C_PITCH, -2, "OSC C PITCH") D(DST_C_DET, P_OSCC_DET, "OSC C DETUNE") \
    D(DST_C_PAN, P_OSCC_PAN, "OSC C PAN") D(DST_C_LEVEL, P_OSCC_LEVEL, "OSC C LEVEL") \
    D(DST_PITCH, -2, "PITCH ALL") D(DST_SUB_LEVEL, P_SUB_LEVEL, "SUB LEVEL") \
    D(DST_NOISE_LEVEL, P_NOISE_LEVEL, "NOISE LEVEL") D(DST_NOISE_COLOR, P_NOISE_COLOR, "NOISE COLOR") \
    D(DST_CUT, P_FLT_CUT, "FILTER CUTOFF") D(DST_RES, P_FLT_RES, "FILTER RES") D(DST_FDRIVE, P_FLT_DRIVE, "FILTER DRIVE") \
    D(DST_AMP, P_AMP_LEVEL, "AMP LEVEL") D(DST_APAN, P_AMP_PAN, "AMP PAN") \
    D(DST_L1RATE, P_LFO1_RATE, "LFO 1 RATE") D(DST_L2RATE, P_LFO2_RATE, "LFO 2 RATE") D(DST_L3RATE, P_LFO3_RATE, "LFO 3 RATE") \
    D(DST_DDRIVE, P_DIST_DRIVE, "DIST DRIVE") D(DST_DLYMIX, P_DLY_MIX, "DELAY MIX") D(DST_REVMIX, P_REV_MIX, "REVERB MIX")

#define S1_D_ENUM(id, p, name) id,
enum { S1_DESTS(S1_D_ENUM) S1_NDST };
#define S1_S_ENUM(id, name) id,
enum { S1_SOURCES(S1_S_ENUM) S1_NSRC };
#define S1_NMOD 16

#endif
