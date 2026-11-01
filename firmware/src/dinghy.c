/* SPDX-License-Identifier: GPL-3.0-only */
/* Kayak (a fork of Dinghy, the smallest FM-1 firmware to build your own on): a granular synth, ChoralRoot FM-1's
 * GRAIN engine (grain.c) on Dinghy's platform (platform.c: updates, the USB rescue, MIDI, USB audio, keys, knobs,
 * LEDs, the screen, flash storage; the FM-1 and the SMK-37), with OberVave's filter, effects, arpeggiator and
 * sequencer.
 *
 *   keys       play the sound, 8 voices of two layers each: the grains and an analog synth; the notes go out over
 *              USB-MIDI too, on the MIDI channel
 *   ALGORITHM  the layer the sound's functions show: GRAIN (turned left) or ANALOG (turned right)
 *   EDIT       GRAIN: GRN SRC A, GRN SRC B (TYPE TONE MOVE SEED), GRN GRAIN (SIZE DENS POS SPRD), GRN GRAIN+ (MORPH
 *              RAND PITCH SCAN); ANALOG: ANA OSC (WAVE DETUN OCT PW), ANA OSC+ (SUB NOISE). The band shows the layer's
 *              wave in front, the other layer's dim behind it
 *   SEL        the layer's filter (OberVave's): GRN FILT / ANA FILT (TYPE CUT RES DRIVE), + (KTRK SEM)
 *   ENV LFO    the layer's ADSR (ATK DEC SUS REL), its LFO (RATE WAVE DEPTH DEST)
 *   GLO        GLOBAL: the two layers' levels, the MIDI channel (OMNI or 1..16), CHORD: the chord a key plays (-: the
 *              one note, MAJ MIN 7 MAJ7 MIN7 SUS2 SUS4 DIM AUG POWER ADD9); PERFORM: what HOME's four knobs turn
 *   ARP        a short press: the arpeggiator on (lit) or off, and its pages (MODE DIV OCT GATE; ARP+: RATCH, the hits
 *              in a step, CHNCE, the chance it plays); held: HOLD (the notes
 *              stay after the keys are up, ARP blinks; then the keys transpose them)
 *   FX         a short press: the effects' pages and back (CHORUS, DELAY, REVERB, DRIVE: OberVave's, on the whole
 *              sound); held: FX MIX, the four effects' MIX on the four knobs
 *   SELECT     the pages of the function its button opened (a button pressed again: the next one too)
 *   KNOB 1..4  the page's four values; the faster the turn, the bigger the step
 *   SEQ        the step sequencer's screen (lit), a list of rows: SELECT goes from row to row, the knobs set the
 *              row's values: PATTERN (TEMPO STEPS DIV SWING; SEQ+: ARP, the steps play the arpeggiator), a step
 *              (NOTE CHORD VEL GATE: a note under C0 is a rest, a gate past 99 % a tie; SEQ+: RATCH CHNCE). SEQ
 *              again: the other page. REC (lit): the keys write the row's step (a chord: keys held together, or
 *              GLO's CHORD) and go to the next row, the top C# (dimly lit) writes a rest
 *   transpose  while the sequencer runs, or the arp holds notes (HOLD), a key (but while REC writes) transposes
 *              what plays, from C4, instead of playing: +-2 octaves
 *   PLAY       the sequencer runs (lit, a blink a beat) or stops; MIDI START / STOP too, a MIDI clock sets the tempo
 *   HOME       the screen after the splash, a short press too: the layers' waves, an oscilloscope of the output, over
 *              it the four performance knobs (GLO's PERFORM), which turn there; SELECT goes back to the pages
 *   PRESETS    the sound: the factory presets, then the user presets saved; MIDI program change too
 *   SAVE       save the sound: PRESETS picks the user slot (1..16), SAVE again writes it, HOME cancels
 *   OCT- OCT+  the octave
 *   MASTER     the volume (platform.c)
 *   SMK-37     the keys with velocity, the outer keys too, the pads (white keys F3..G5, lit while they sound), the
 *              pitch wheel bends +-2 semitones
 * The buttons that do something glow; they light fully while what they do is on. The sound being played, the octave,
 * the channel, the chord, PERFORM, the function and the layer are kept in flash (2 s after a change, when silent), the sequence too (when
 * stopped).
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
static void app_idle(void);
#define LCD_IDLE app_idle                       /* while the screen's DMA runs: MIDI in (platform.c) */
#include "platform.c"
#if BOARD_SMK37
#include "smk_wheel.h"
#endif
#include "grain.c"

/* ---- the sound (runs in the audio ISR: keep it short, no flash, no screen) */
static void app_audio(int32_t *out, uint32_t n)
{
    gp_audio(out, n);
}

/* ---- the pages: four knobs each (QX: none, QCH: the MIDI channel, QCHORD: the chord a key plays, the device's own).
 * The functions' buttons open their pages, SELECT turns through them; ALGORITHM picks the layer the sound's functions
 * show */
#define QX 0xFFu
#define QCH 0xFEu
#define QCHORD 0xFDu
#define QP1 0xF9u                               /* QP1 .. QP1 + 3: which value HOME's knob 1 .. 4 turns (PERFORM) */
enum { P_SRCA, P_SRCB, P_GRAIN, P_GRAIN2, P_FILT, P_FILT2, P_ENV, P_LFO, P_OSC, P_OSC2, P_AFILT, P_AFILT2, P_AENV,
       P_ALFO, P_ARP, P_CHO, P_DLY, P_REV, P_DRV, P_MIX, P_GLOBAL, P_ARP2, P_PERF, NPAGE };   /* (P_SRCA, P_SRCB: the wave of A, of B) */
typedef struct { const char *name; uint8_t q[4]; } page_t;
static const page_t PAGES[NPAGE] = {
    {"GRN SRC A", {GQ_TYPE_A, GQ_TONE_A, GQ_MOTION_A, GQ_SEED_A}},
    {"GRN SRC B", {GQ_TYPE_B, GQ_TONE_B, GQ_MOTION_B, GQ_SEED_B}},
    {"GRN GRAIN", {GQ_SIZE, GQ_DENS, GQ_POS, GQ_SPRD}}, {"GRN GRAIN+", {GQ_MORPH, GQ_RAND, GQ_PITCH, GQ_SCAN}},
    {"GRN FILT", {GQ_FTYPE, GQ_CUT, GQ_RES, GQ_FDRIVE}}, {"GRN FILT+", {GQ_KTRK, GQ_FMORPH, QX, QX}},
    {"GRN ENV", {GQ_ATK, GQ_DEC, GQ_SUS, GQ_REL}}, {"GRN LFO", {GQ_LRATE, GQ_LWAVE, GQ_LDEPTH, GQ_LDEST}},
    {"ANA OSC", {GQ_OWAVE, GQ_ODET, GQ_OOCT, GQ_OPW}}, {"ANA OSC+", {GQ_OSUB, GQ_ONOISE, QX, QX}},
    {"ANA FILT", {GQ_AFTYPE, GQ_ACUT, GQ_ARES, GQ_AFDRIVE}}, {"ANA FILT+", {GQ_AKTRK, GQ_AFMORPH, QX, QX}},
    {"ANA ENV", {GQ_AATK, GQ_ADEC, GQ_ASUS, GQ_AREL}}, {"ANA LFO", {GQ_ALRATE, GQ_ALWAVE, GQ_ALDEPTH, GQ_ALDEST}},
    {"ARP", {GQ_AMODE, GQ_ADIV, GQ_AOCT, GQ_AGATE}},
    {"FX CHORUS", {GQ_CRATE, GQ_CDEPTH, GQ_CMIX, QX}}, {"FX DELAY", {GQ_DTIME, GQ_DFB, GQ_DTONE, GQ_DMIX}},
    {"FX REVERB", {GQ_RSIZE, GQ_RDAMP, GQ_RPRE, GQ_RMIX}}, {"FX DRIVE", {GQ_VTYPE, GQ_VAMT, GQ_VTONE, GQ_VMIX}},
    {"FX MIX", {GQ_CMIX, GQ_DMIX, GQ_RMIX, GQ_VMIX}}, {"GLOBAL", {GQ_GLEV, GQ_ALEV, QCH, QCHORD}},
    {"ARP+", {GQ_ARATCH, GQ_ACHANCE, QX, QX}}, {"PERFORM", {QP1, QP1 + 1u, QP1 + 2u, QP1 + 3u}}};
/* HOME's knobs: what each turns, picked on PERFORM (GLO's second page) */
static const struct { uint8_t q; const char *name; } PERF[] = {
    {GQ_MORPH, "MORPH"}, {GQ_POS, "POS"}, {GQ_SIZE, "SIZE"}, {GQ_DENS, "DENS"}, {GQ_SPRD, "SPRD"}, {GQ_RAND, "RAND"},
    {GQ_PITCH, "PITCH"}, {GQ_SCAN, "SCAN"}, {GQ_CUT, "G.CUT"}, {GQ_RES, "G.RES"}, {GQ_ATK, "G.ATK"},
    {GQ_REL, "G.REL"}, {GQ_LDEPTH, "G.LFO"}, {GQ_ACUT, "A.CUT"}, {GQ_ARES, "A.RES"}, {GQ_ODET, "A.DET"},
    {GQ_OPW, "A.PW"}, {GQ_OSUB, "A.SUB"}, {GQ_AATK, "A.ATK"}, {GQ_AREL, "A.REL"}, {GQ_ALDEPTH, "A.LFO"},
    {GQ_GLEV, "G.LEV"}, {GQ_ALEV, "A.LEV"}, {GQ_CMIX, "CHOR"}, {GQ_DMIX, "DELAY"}, {GQ_DFB, "D.FB"},
    {GQ_RMIX, "REVB"}, {GQ_VMIX, "DRIVE"}, {GQ_VAMT, "V.AMT"}, {GQ_AGATE, "GATE"}, {GQ_ARATCH, "RATCH"},
    {GQ_ACHANCE, "CHNCE"}};
#define NPERF (sizeof PERF / sizeof PERF[0])
static const uint8_t PERF_DEF[4] = {0, 8, 24, 26};   /* MORPH, G.CUT, DELAY, REVB */
static const char *const MIX_NAMES[4] = {"CHOR", "DELAY", "REVB", "DRIVE"};   /* (FX MIX: four MIX told apart) */
enum { FN_EDIT, FN_SEL, FN_ENV, FN_LFO, FN_ARP, FN_FX, FN_MIX, FN_GLO, NFN };
static const uint8_t FN_PAGE[NFN][2][5] = {    /* each function's pages: the grain layer's, the analog's; 0xFF ends */
    {{P_SRCA, P_SRCB, P_GRAIN, P_GRAIN2, 0xFF}, {P_OSC, P_OSC2, 0xFF}},
    {{P_FILT, P_FILT2, 0xFF}, {P_AFILT, P_AFILT2, 0xFF}},
    {{P_ENV, 0xFF}, {P_AENV, 0xFF}},
    {{P_LFO, 0xFF}, {P_ALFO, 0xFF}},
    {{P_ARP, P_ARP2, 0xFF}, {P_ARP, P_ARP2, 0xFF}},
    {{P_CHO, P_DLY, P_REV, P_DRV, 0xFF}, {P_CHO, P_DLY, P_REV, P_DRV, 0xFF}},
    {{P_MIX, 0xFF}, {P_MIX, 0xFF}},
    {{P_GLOBAL, P_PERF, 0xFF}, {P_GLOBAL, P_PERF, 0xFF}}};
static uint32_t fn_count(uint32_t fn, uint32_t layer)
{
    uint32_t n = 0;
    while (n < 5u && FN_PAGE[fn][layer][n] != 0xFFu) n++;
    return n;
}
static const char *const LAYER_NAME[2] = {"GRAIN", "ANALOG"};
enum { V_PAGES, V_HOME, V_SEQ };                /* what the screen shows */

/* ---- the presets: the factory ones (grain.c), then NUSER the user saves (blob 0) */
#define NUSER 16u
#define NPRESET (GP_NFACT + NUSER)
static struct { uint8_t used[NUSER]; int8_t p[NUSER][GQ_N]; } user;
static uint32_t preset_ok(uint32_t i) { return i < GP_NFACT || (i < NPRESET && user.used[i - GP_NFACT]); }
static void preset_load(uint32_t i)            /* a factory one: the defaults, then its pairs; a user's: its values */
{
    uint32_t k;
    if (i < GP_NFACT) {
        const int8_t *kv;
        gp_defaults();
        for (kv = GP_FACT[i]; kv[0] >= 0; kv += 2)
            gp_p[kv[0]] = (int8_t)clamp(kv[1], GQ[kv[0]].min, GQ[kv[0]].max);
        return;
    }
    for (k = 0; k < GQ_N; k++)
        gp_p[k] = (int8_t)clamp(user.p[i - GP_NFACT][k], GQ[k].min, GQ[k].max);
}
static void preset_name(uint32_t i, char *s)    /* "CLOUD PAD", "USER 3" */
{
    char *p = s;
    if (i < GP_NFACT) gq_put_str(&p, GP_FACT_NAMES[i]);
    else { gq_put_str(&p, "USER "); gq_put_dec(&p, i - GP_NFACT + 1u); }
    *p = 0;
}

/* ---- the encoders: detents a second make the step (a slow turn 1, a fast one up to 8) */
static int32_t enc_fast(uint32_t k, int32_t e)
{
    static uint32_t t_last[4];
    uint32_t dt = fm1_ms - t_last[k], a = (uint32_t)(e < 0 ? -e : e), sp;
    t_last[k] = fm1_ms;
    sp = a * 1000u / (dt ? dt : 1u);
    return e * (sp >= 40u ? 8 : sp >= 20u ? 4 : sp >= 10u ? 2 : 1);
}

/* ---- the screen: the preset, the page, the wave the grains read with the grains on it, the four values */
#define C_ACC RGB(255, 140, 26)                 /* orange, as the update loader's screen */
#define C_ACC_DIM RGB(128, 70, 13)
#define C_SRC_A RGB(70, 150, 255)
#define C_OFF RGB(90, 90, 90)
#define C_DIMBAR RGB(45, 45, 45)
#define C_KEYW RGB(210, 210, 210)
#define KEY0 53u                                /* key 0 = F3 */
#define KEY_REST 20u                            /* the top C# (C#5 at OCT 0): SEQ's rest while REC is on */
#define WV_Y 70u                                /* the wave band: 240 x 64 */
#define CELL_Y 142u                             /* the four values: 240 x 56 */
#define SCOPE_Y 140u                            /* HOME: the oscilloscope, 240 x 64 */

static int8_t wv_lo[240], wv_hi[240];          /* the band's envelope, a min / max a column (of 127) */
static void wave_take(uint32_t pg)              /* SRC A: A, SRC B: B, else the mix by MORPH */
{
    const int16_t *A = gbuf[gsel[0]], *B = gbuf[gsel[1]];
    int32_t mq = ((gp_p[GQ_MORPH] << 8) * 16513 + 8192) >> 14;
    uint32_t x, i;
    if (pg == 0u) mq = 0;
    if (pg == 1u) mq = 32768;
    for (x = 0; x < 240u; x++) {
        int32_t lo = 32767, hi = -32767;
        for (i = x * GS_N / 240u; i < (x + 1u) * GS_N / 240u; i++) {
            int32_t s = gp_mix(A, B, i, mq);
            if (s < lo) lo = s;
            if (s > hi) hi = s;
        }
        wv_lo[x] = (int8_t)(lo >> 8);
        wv_hi[x] = (int8_t)(hi >> 8);
    }
}
/* the analog layer's oscillators as they sound (WAVE PW DETUN SUB NOISE), two periods over the band; 1: they changed */
static int8_t wv_ana[240];
static uint32_t ana_take(void)
{
    static uint32_t was = ~0u;
    static int32_t s[240];
    uint32_t key = (uint32_t)(uint8_t)gp_p[GQ_OWAVE] | (uint32_t)(uint8_t)gp_p[GQ_OPW] << 4
                 | (uint32_t)(uint8_t)gp_p[GQ_OSUB] << 11 | (uint32_t)(uint8_t)gp_p[GQ_ONOISE] << 18
                 | (uint32_t)(uint8_t)gp_p[GQ_ODET] << 25;
    uint32_t inc = 0xFFFFFFFFu / 120u, pw = 214748365u + (uint32_t)clamp(gp_p[GQ_OPW], 0, 127) * 30436776u, x;
    uint32_t dd = (uint32_t)mulq16((int32_t)inc, (uint32_t)(gp_p[GQ_ODET] * 9)), i1 = inc - dd, i2 = inc + dd;
    uint32_t p1 = 0, p2 = 0, ps = 0;
    int32_t owave = gp_p[GQ_OWAVE], osub = gp_p[GQ_OSUB] * 258, onoise = gp_p[GQ_ONOISE] * 258, rng = 0x1F123BB5, pk = 1;
    if (key == was) return 0;
    was = key;
    for (x = 0; x < 240u; x++) {                /* (as grain.c's voice, the two oscillators started together) */
        int32_t v;
        if (owave == OW_SQR) v = osc_pulse(p1, i1, pw) + osc_pulse(p2, i2, pw);
        else if (owave == OW_TRI) v = osc_tri(p1) + osc_tri(p2);
        else v = osc_saw(p1, i1) + osc_saw(p2, i2);
        v >>= 1;
        if (osub) v += mulq15(ps < 0x80000000u ? 32767 : -32767, osub);
        if (onoise) v += mulq15((int32_t)(noise32(&rng) >> 16) - 32768, onoise);
        p1 += i1;
        p2 += i2;
        ps += i1 >> 1;
        s[x] = v;
        if ((v < 0 ? -v : v) > pk) pk = v < 0 ? -v : v;
    }
    for (x = 0; x < 240u; x++) wv_ana[x] = (int8_t)(s[x] * 28 / pk);
    return 1;
}
#define C_BACK RGB(50, 50, 50)                  /* the layer behind */
/* the band: the layer of the pages (fg) in front, the other dim behind it; the grains: the material they read (A on
 * SRC A, B on SRC B, else the mix), POS and SPRD, each grain sounding */
static void draw_wave(uint32_t pg, uint32_t fg)
{
    uint32_t x, k;
    uint16_t col = pg == 0u ? C_SRC_A : pg == 1u ? C_ACC : C_KEYW;
    cv_begin(240, 64, C_BLACK);
    if (fg) {                                   /* the analog layer in front: its oscillators, the grains behind */
        for (x = 0; x < 240u; x++)
            cv_rect((int32_t)x, 32 - (wv_hi[x] >> 2), 1, (wv_hi[x] >> 2) - (wv_lo[x] >> 2) + 1, C_BACK);
        for (x = 1; x < 240u; x++) {
            cv_line((int32_t)x - 1, 32 - wv_ana[x - 1u], (int32_t)x, 32 - wv_ana[x], C_ACC);
            cv_line((int32_t)x - 1, 33 - wv_ana[x - 1u], (int32_t)x, 33 - wv_ana[x], C_ACC);
        }
        cv_blit(0, WV_Y);
        return;
    }
    if (pg >= 2u) {                             /* where the grains start: POS (moved by SCAN, the LFO), SPRD round it */
        int32_t c = gp_pos_view * 239 / (127 << 8), s = gp_p[GQ_SPRD] * 240 / 127;
        cv_rect(c - s / 2, 0, s, 64, C_DIMBAR);
        cv_rect(c, 0, 1, 64, C_WHITE);
    }
    for (x = 0; x < 240u; x++)
        cv_rect((int32_t)x, 32 - (wv_hi[x] >> 2), 1, (wv_hi[x] >> 2) - (wv_lo[x] >> 2) + 1, col);
    for (k = 0; k < GP_NG; k++) {               /* the grains sounding: a line where each reads, as tall as its window */
        gp_grain_t g = gpg[k];
        if (g.owner && g.pos < GS_N) {
            int32_t h = gp_win(g.wph) >> 10;    /* 0 .. 31 */
            cv_rect((int32_t)(g.pos * 240u / GS_N), 32 - h, 1, 2 * h + 1, C_WHITE);
        }
    }
    for (x = 1; x < 240u; x++)                  /* the analog layer behind, dim */
        cv_line((int32_t)x - 1, 32 - wv_ana[x - 1u], (int32_t)x, 32 - wv_ana[x], C_ACC_DIM);
    cv_blit(0, WV_Y);
}

/* HOME: the output as an oscilloscope, 240 frames (5.4 ms) from a rising zero crossing, scaled to its own peak; over it
 * the four knobs' values (perf: what each turns) */
static void draw_scope(const uint8_t *perf)
{
    static int16_t s[480];
    uint32_t w = scope_w, i, t = 0;
    int32_t pk = 2000, y0 = 32, x;
    for (i = 0; i < 480u; i++) s[i] = scope[(w - 480u + i) % SCOPE_N];
    for (i = 1; i < 240u; i++)
        if (s[i - 1u] < 0 && s[i] >= 0) { t = i; break; }
    for (i = t; i < t + 240u; i++) { int32_t a = s[i] < 0 ? -s[i] : s[i]; if (a > pk) pk = a; }
    cv_begin(240, 64, C_BLACK);
    cv_rect(0, 32, 240, 1, C_DIMBAR);
    for (x = 0; x < 240; x++) {
        int32_t y = 32 - s[t + (uint32_t)x] * 26 / pk;
        if (x) cv_line(x - 1, y0, x, y, C_ACC);
        y0 = y;
    }
    for (i = 0; i < 4u; i++) {
        uint32_t q = PERF[perf[i] % NPERF].q;
        const char *name = PERF[perf[i] % NPERF].name;
        char v[12];
        gq_text(q, gp_p[q], v);
        cv_text((int32_t)i * 60 + (60 - text_w(&FONT_S, name)) / 2, 0, &FONT_S, name, C_OFF);
        cv_text((int32_t)i * 60 + (60 - text_w(&FONT_S, v)) / 2, 46, &FONT_S, v, C_WHITE);
    }
    cv_blit(0, SCOPE_Y);
}

static void draw_cells(uint32_t pg, uint32_t ch, uint32_t chord, const uint8_t *perf)   /* ch: the MIDI channel (QCH),
                                                 * chord: QCHORD's, perf: PERFORM's */
{
    static const char *const KNOB[4] = {"KNOB1", "KNOB2", "KNOB3", "KNOB4"};
    uint32_t c;
    cv_begin(240, 56, C_BLACK);
    for (c = 0; c < 4u; c++) {
        uint32_t q = PAGES[pg].q[c];
        int32_t x0 = (int32_t)c * 60, v, w;
        char s[12], *p = s;
        const char *name;
        if (q == QX) continue;                  /* (a page with fewer knobs) */
        if (q == QCH) {                         /* the MIDI channel: OMNI or 1..16 */
            name = "MIDI";
            if (ch) gq_put_dec(&p, ch); else gq_put_str(&p, "OMNI");
            *p = 0;
            w = (int32_t)ch * 48 / 16;
        } else if (q >= QP1 && q < QP1 + 4u) {  /* what HOME's knob turns */
            name = KNOB[q - QP1];
            gq_put_str(&p, PERF[perf[q - QP1] % NPERF].name);
            *p = 0;
            w = (int32_t)(perf[q - QP1] % NPERF) * 48 / (int32_t)(NPERF - 1u);
        } else if (q == QCHORD) {               /* the chord a key plays */
            name = "CHORD";
            gq_put_str(&p, CHORD_NAMES[chord % NCHORD]);
            *p = 0;
            w = (int32_t)chord * 48 / (int32_t)(NCHORD - 1u);
        } else {
            v = gp_p[q];
            name = pg == P_MIX ? MIX_NAMES[c] : GQ[q].name;
            gq_text(q, v, s);
            w = (v - GQ[q].min) * 48 / (GQ[q].max - GQ[q].min);
        }
        cv_text(x0 + (60 - text_w(&FONT_S, name)) / 2, 2, &FONT_S, name, C_OFF);
        cv_text(x0 + (60 - text_w(&FONT_S, s)) / 2, 22, &FONT_S, s, C_WHITE);
        cv_rect(x0 + 6, 46, 48, 6, C_DIMBAR);
        cv_rect(x0 + 6, 46, w, 6, c & 1u ? C_ACC : C_SRC_A);
    }
    cv_blit(0, CELL_Y);
}

static void draw_preset(uint32_t i, uint32_t mod)   /* the sound's name; " *" once a knob changed it */
{
    char s[24], *p;
    preset_name(i, s);
    p = s;
    while (*p) p++;
    if (mod) gq_put_str(&p, " *");
    *p = 0;
    draw_text_box(0, 6, 240, &FONT_S, s, C_ACC, 1);
}

static void draw_save(uint32_t to)              /* the save screen: the slot, what is in it, the way out */
{
    char s[24], *p = s;
    gq_put_str(&p, "TO USER ");
    gq_put_dec(&p, to + 1u);
    *p = 0;
    cv_begin(240, 56, C_BLACK);
    cv_text((240 - text_w(&FONT_S, s)) / 2, 6, &FONT_S, s, C_WHITE);
    p = user.used[to] ? "REPLACES WHAT IS THERE" : "EMPTY";
    cv_text((240 - text_w(&FONT_S, p)) / 2, 30, &FONT_S, p, C_OFF);
    cv_blit(0, CELL_Y);
    draw_text_box(0, 204, 240, &FONT_S, "PRESETS: SLOT  SAVE: OK", C_ACC, 1);
    draw_text_box(0, 222, 240, &FONT_S, "HOME: CANCEL", C_OFF, 1);
}

static void put_transp(char **p, int32_t tr)    /* " T+5", none at 0 */
{
    if (!tr) return;
    gq_put_str(p, " T");
    *(*p)++ = tr < 0 ? '-' : '+';
    gq_put_dec(p, (uint32_t)(tr < 0 ? -tr : tr));
}
static void draw_status(int32_t oct, uint32_t ch, int32_t tr)   /* tr: the transposition */
{
    char s[32] = "OCT +0  MIDI OMNI", *p = s + 17;
    s[4] = oct < 0 ? '-' : '+';
    s[5] = (char)('0' + (oct < 0 ? -oct : oct));
    if (ch) {
        s[13] = 'C'; s[14] = 'H'; s[15] = (char)(ch < 10u ? ' ' : '1'); s[16] = (char)('0' + ch % 10u);
    }
    put_transp(&p, tr);
    *p = 0;
    draw_text_box(0, 204, 240, &FONT_S, s, C_OFF, 1);
}

static void draw_midi(uint32_t src, uint32_t bpm, uint32_t running)   /* where notes came from last, the clock */
{
    char s[32] = "", *p = s;
    const char *a = src == 2u ? "TRS IN" : src == 1u ? "USB IN" : "";
    while (*a) *p++ = *a++;
    if (bpm) {
        if (p != s) { *p++ = ' '; *p++ = ' '; }
        a = running ? "CLOCK " : "clock ";
        while (*a) *p++ = *a++;
        if (bpm >= 100u) *p++ = (char)('0' + bpm / 100u);
        *p++ = (char)('0' + bpm / 10u % 10u);
        *p++ = (char)('0' + bpm % 10u);
        a = " BPM";
        while (*a) *p++ = *a++;
    }
    *p = 0;
    draw_text_box(0, 222, 240, &FONT_S, s, C_OFF, 1);
}

static void note_name(uint32_t n, char **p)     /* "C4", "F#2" */
{
    static const char *const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    gq_put_str(p, NAME[n % 12u]);
    if (n < 12u) *(*p)++ = '-';
    else gq_put_dec(p, n / 12u - 1u);
}

/* HOME's line: the arp (and HOLD), the sequencer, the tempo while it fits (24 characters: 240 px of FONT_S) */
static void draw_home_line(uint32_t run, uint32_t bpm, uint32_t hold)
{
    char s[40], *p = s;
    gq_put_str(&p, "ARP ");
    gq_put_str(&p, ARP_NAMES[clamp(gp_p[GQ_AMODE], 0, NARPMODE - 1)]);
    if (hold) gq_put_str(&p, " HOLD");
    gq_put_str(&p, run ? "  PLAY" : "  STOP");
    put_transp(&p, ctl_transp);
    if (p - s <= 16) {                          /* (the tempo, then its unit, only where they fit) */
        *p++ = ' ';
        gq_put_dec(&p, bpm);
        if (p - s <= 20) gq_put_str(&p, " BPM");
    }
    *p = 0;
    draw_text_box(0, 204, 240, &FONT_S, s, C_OFF, 1);
}

/* SEQ: a list of rows, PATTERN then the steps; SELECT goes from row to row (row 0: PATTERN, row i: step i - 1) and
 * the knobs set the row's four values, the cells as the pages' */
static void gate_text(uint32_t g, char **p)     /* "76%", "TIE" */
{
    if (g >= GATE_TIE) { gq_put_str(p, "TIE"); return; }
    gq_put_dec(p, (g + 1u) * 100u / 128u);
    *(*p)++ = '%';
}
static void step_notes(const step_t *s, char **p)   /* "C4 MAJ7", "C4 +2" (keys held together), "REST" */
{
    uint32_t i;
    if (!s->note[0]) { gq_put_str(p, "REST"); return; }
    note_name(s->note[0], p);
    if (s->chord) { *(*p)++ = ' '; gq_put_str(p, CHORD_NAMES[s->chord % NCHORD]); return; }
    for (i = 1; i < SEQ_CHORD && s->note[i]; i++) ;
    if (i > 1u) { gq_put_str(p, " +"); gq_put_dec(p, i - 1u); }
}
/* the band: the row before, the row (lit, an orange edge), the row after, 21 px each; the step playing: an orange mark */
static void draw_seq_rows(uint32_t row, uint32_t play, uint32_t spage)   /* spage 1: a step's RATCH and CHANCE */
{
    uint32_t len = (uint32_t)clamp(pat.len, 1, SEQ_MAX), r;
    cv_begin(240, 64, C_BLACK);
    for (r = 0; r < 3u; r++) {
        int32_t i = (int32_t)row - 1 + (int32_t)r, y = (int32_t)r * 21 + 1;
        uint16_t col = r == 1u ? C_WHITE : C_OFF;
        char s[24], *p = s;
        const step_t *st;
        if (i < 0 || i > (int32_t)len) continue;
        if (r == 1u) { cv_rect(0, y, 240, 20, C_DIMBAR); cv_rect(0, y, 3, 20, C_ACC); }
        if (!i) {
            cv_text(14, y + 1, &FONT_S, "PATTERN", r == 1u ? C_ACC : C_OFF);
            if (spage) gq_put_str(&p, pat.arp ? "ARP ON" : "ARP OFF");
            else { gq_put_dec(&p, len); gq_put_str(&p, " STEPS"); }
            *p = 0;
            cv_text(234 - text_w(&FONT_S, s), y + 1, &FONT_S, s, col);
            continue;
        }
        st = &pat.st[(uint32_t)(i - 1) % SEQ_MAX];
        if ((uint32_t)(i - 1) == play) cv_rect(5, y + 6, 6, 8, C_ACC);
        if (i < 10) *p++ = '0';
        gq_put_dec(&p, (uint32_t)i);
        *p++ = ' ';
        step_notes(st, &p);
        *p = 0;
        cv_text(14, y + 1, &FONT_S, s, st->note[0] ? col : r == 1u ? C_OFF : RGB(60, 60, 60));
        if (!st->note[0]) continue;
        p = s;
        if (spage) { *p++ = 'x'; gq_put_dec(&p, st->ratch); *p++ = ' '; gq_put_dec(&p, st->chance); *p++ = '%'; }
        else { gq_put_dec(&p, st->vel); *p++ = ' '; gate_text(st->gate, &p); }
        *p = 0;
        cv_text(234 - text_w(&FONT_S, s), y + 1, &FONT_S, s, col);
    }
    cv_blit(0, WV_Y);
}
/* the cells: PATTERN: TEMPO STEPS DIV SWING (page 2: ARP); a step: NOTE CHORD VEL GATE (page 2: RATCH CHANCE) */
static void draw_seq_cells(uint32_t row, uint32_t bpm, uint32_t spage)
{
    static const char *const PN[4] = {"TEMPO", "STEPS", "DIV", "SWING"}, *const SN[4] = {"NOTE", "CHORD", "VEL", "GATE"};
    const step_t *st = &pat.st[(row ? row - 1u : 0u) % SEQ_MAX];
    uint32_t c;
    cv_begin(240, 56, C_BLACK);
    for (c = 0; c < 4u; c++) {
        int32_t x0 = (int32_t)c * 60, w;
        char s[12], *p = s;
        const char *name = row ? SN[c] : PN[c];
        if (spage) {                            /* page 2 */
            if (c >= (row ? 2u : 1u)) continue;
            if (!row) { name = "ARP"; gq_put_str(&p, pat.arp ? "ON" : "OFF"); w = pat.arp ? 48 : 0; }
            else if (!c) { name = "RATCH"; *p++ = 'x'; gq_put_dec(&p, st->ratch); w = ((int32_t)st->ratch - 1) * 16; }
            else { name = "CHNCE"; gq_put_dec(&p, st->chance); *p++ = '%'; w = (int32_t)st->chance * 48 / 100; }
        } else if (!row) {
            if (c == 0u) { gq_put_dec(&p, bpm); w = ((int32_t)bpm - 40) * 48 / 200; }
            else if (c == 1u) { gq_put_dec(&p, pat.len); w = (int32_t)pat.len * 48 / (int32_t)SEQ_MAX; }
            else if (c == 2u) { gq_put_str(&p, DIV_NAMES[pat.div % NDIV]); w = (int32_t)pat.div * 48 / (NDIV - 1); }
            else { gq_put_dec(&p, pat.swing); *p++ = '%'; w = ((int32_t)pat.swing - 50) * 48 / 25; }
        } else if (c == 0u) {
            if (st->note[0]) note_name(st->note[0], &p); else gq_put_str(&p, "REST");
            w = (int32_t)st->note[0] * 48 / 127;
        } else if (c == 1u) {
            gq_put_str(&p, st->chord ? CHORD_NAMES[st->chord % NCHORD] : st->note[1] ? "KEYS" : "-");
            w = (int32_t)(st->chord % NCHORD) * 48 / (int32_t)(NCHORD - 1u);
        } else if (c == 2u) { gq_put_dec(&p, st->vel); w = (int32_t)st->vel * 48 / 127; }
        else { gate_text(st->gate, &p); w = (int32_t)st->gate * 48 / 127; }
        *p = 0;
        w = clamp(w, 0, 48);
        cv_text(x0 + (60 - text_w(&FONT_S, name)) / 2, 2, &FONT_S, name, C_OFF);
        cv_text(x0 + (60 - text_w(&FONT_S, s)) / 2, 22, &FONT_S, s, C_WHITE);
        cv_rect(x0 + 6, 46, 48, 6, C_DIMBAR);
        cv_rect(x0 + 6, 46, w, 6, c & 1u ? C_ACC : C_SRC_A);
    }
    cv_blit(0, CELL_Y);
}
static void draw_seq_lines(uint32_t row, uint32_t bpm, uint32_t play, uint32_t rec)   /* PLAY (the step) or STOP, the
                                                 * tempo, the transposition; the row, what REC does */
{
    char s[40], *p = s;
    if (play < SEQ_MAX) { gq_put_str(&p, "PLAY "); gq_put_dec(&p, play + 1u); gq_put_str(&p, "  "); }
    else gq_put_str(&p, "STOP  ");
    gq_put_dec(&p, bpm);
    gq_put_str(&p, " BPM ");
    put_transp(&p, ctl_transp);
    *p = 0;
    draw_text_box(0, 204, 240, &FONT_S, s, C_OFF, 1);
    p = s;
    if (row) {
        gq_put_str(&p, "STEP ");
        gq_put_dec(&p, row);
        *p++ = '/';
        gq_put_dec(&p, (uint32_t)clamp(pat.len, 1, SEQ_MAX));
    } else
        gq_put_str(&p, "PATTERN");
    gq_put_str(&p, rec ? "  TOP C#: REST" : "  REC: RECORD");
    *p = 0;
    draw_text_box(0, 222, 240, &FONT_S, s, C_OFF, 1);
}

/* the knobs on a SEQ row: PATTERN's, a step's */
static void seq_knob_pat(uint32_t k, int32_t e, uint32_t spage)
{
    if (spage) { if (!k) pat.arp = e > 0; return; }   /* ARP: the steps play the arpeggiator */
    if (k == 0u) pat.tempo = (uint8_t)clamp(pat.tempo + enc_fast(0, e), 40, 240);
    else if (k == 1u) pat.len = (uint8_t)clamp(pat.len + e, 1, SEQ_MAX);
    else if (k == 2u) pat.div = (uint8_t)clamp(pat.div + e, 0, NDIV - 1);
    else pat.swing = (uint8_t)clamp(pat.swing + e, 50, 75);
}
static void step_rest(step_t *s)                /* a rest; its gate, ratchet and chance stay */
{
    uint8_t g = s->gate, r = s->ratch, c = s->chance;
    step_clear(s);
    s->gate = g, s->ratch = r, s->chance = c;
}
static void seq_knob_step(step_t *s, uint32_t k, int32_t e, uint32_t spage)
{
    uint32_t i;
    if (spage) {                                /* RATCH: hits in the step, CHANCE: % it plays */
        if (!k) s->ratch = (uint8_t)clamp(s->ratch + e, 1, 4);
        else if (k == 1u) s->chance = (uint8_t)clamp(s->chance + enc_fast(1, e), 0, 100);
        return;
    }
    if (k == 0u) {                              /* NOTE: the chord moves with it (a rest: C4 first); under C0 a rest */
        int32_t r = s->note[0], nr = r ? r + e : e > 0 ? 60 : 0;
        if (nr < 12) { step_rest(s); return; }
        nr = clamp(nr, 12, 120);
        if (s->chord || !r) chord_notes((uint32_t)nr, s->chord, s->note);
        else
            for (i = 0; i < SEQ_CHORD && s->note[i]; i++) s->note[i] = (uint8_t)clamp(s->note[i] + nr - r, 1, 127);
    } else if (k == 1u) {                       /* CHORD: round the list, the notes again from the root */
        uint32_t r = s->note[0];
        s->chord = (uint8_t)((uint32_t)((int32_t)(s->chord % NCHORD) + e % (int32_t)NCHORD + (int32_t)NCHORD) % NCHORD);
        if (r) chord_notes(r, s->chord, s->note);
    } else if (k == 2u)
        s->vel = (uint8_t)clamp(s->vel + enc_fast(2, e), 1, 127);
    else
        s->gate = (uint8_t)clamp(s->gate + enc_fast(3, e), 1, GATE_TIE);
}

/* ---- the splash: the kayaker drawn smooth with a close shadow, KAYAK under it (firmware/gen/kayak_splash.h, made
 * by tools/make_splash.ps1: RGB565 run-length coded), then the version line */
#include "kayak_splash.h"
static void splash(void)
{
    uint32_t y0, i = 0, left = 0;
    uint16_t col = 0;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    for (y0 = 0; y0 < SPLASH_H; y0 += 64u) {   /* strips of 64 rows (240 x 64: the canvas) */
        uint32_t h = SPLASH_H - y0 < 64u ? SPLASH_H - y0 : 64u, k;
        cv_begin(240, h, C_BLACK);
        for (k = 0; k < 240u * h; k++) {
            if (!left) { left = SPLASH_RLE[i]; col = SPLASH_RLE[i + 1u]; i += 2u; }
            cv_px[k] = swap16(col);
            left--;
        }
        cv_blit(0, y0);
    }
    draw_text_box(0, 214, 240, &FONT_S, "GRAIN SYNTH  v" APP_VER, C_ACC_DIM, 1);
}

/* ---- the LEDs: a button that does something glows, it lights while what it does is on */
static void led_fn(uint32_t id, uint32_t on)
{
    fm1_led_key(id, on);
    fm1_led_key_glow(id, !on);
}

/* ---- MIDI in: from the main loop, and from app_idle while the screen draws (idle_ok) */
static struct { uint8_t oct, ch, fn, layer, cur, mod, chord, perf[4]; int8_t p[GQ_N]; } set;   /* what survives a
                                                 * power-off */
static void knob_turn(uint32_t q, uint32_t k, int32_t e)   /* a sound's value: a list round, a step a detent; else
                                                 * the faster the bigger */
{
    int32_t v, lo = GQ[q].min, hi = GQ[q].max, fmt = GQ[q].fmt;
    if (fmt == GF_TYPE || fmt == GF_LWAVE || fmt == GF_LDEST || fmt == GF_FTYPE || fmt == GF_VTYPE
        || fmt == GF_ARP || fmt == GF_DIV || fmt == GF_OWAVE || fmt == GF_ADEST)
        v = (int32_t)(((uint32_t)(gp_p[q] + e % (hi + 1) + (hi + 1))) % (uint32_t)(hi + 1));
    else
        v = gp_p[q] + enc_fast(k, e);
    gp_p[q] = (int8_t)(v < lo ? lo : v > hi ? hi : v);
}

/* ---- the settings as a journal in their sector: each save goes after the last one ([KLOG][length][crc32] then the
 * bytes, a word-aligned record), and the sector is erased only when it is full (~50 saves). A save then programs
 * ~70 bytes, the interrupts off for well under 1 ms, instead of erasing 4 KiB (~50 ms: the LED scan stops and the
 * buttons' LEDs jump). The last record whose CRC holds is the settings; a sector in platform.c's format (before
 * 0.6) is read by settings_load, the first journal save erases it */
#define LOG_MAGIC 0x474F4C4Bu                   /* "KLOG" */
#ifdef DINGHY_HOST
static int log_load(void *p, uint32_t n) { return settings_load(p, n); }   /* (the host's store: in RAM) */
static int log_save(const void *p, uint32_t n) { return settings_save(p, n); }
#else
static uint32_t log_len(uint32_t n) { return (12u + n + 3u) & ~3u; }
/* the journal's records: the last valid one of length n into p (0: there is one); *end: where the next one goes
 * (0x1000: none fits or the sector is not a journal) */
static int log_scan(void *p, uint32_t n, uint32_t *end)
{
    static uint8_t buf[SECTOR_MAX];
    uint32_t off = 0, h[3];
    int found = -1;
    *end = 0x1000u;
    if (!flash_ok || n > sizeof buf)
        return -1;
    while (off + 12u <= 0x1000u && !st_read(SETTINGS_AT + off, h, sizeof h)) {
        if (h[0] == 0xFFFFFFFFu) { *end = off; break; }   /* the erased rest: the next record goes here */
        if (h[0] != LOG_MAGIC || h[1] > SECTOR_MAX || off + log_len(h[1]) > 0x1000u)
            break;                              /* not a journal (platform.c's format), or a broken one: erase first */
        if (h[1] == n && !st_read(SETTINGS_AT + off + 12u, buf, n) && crc32(buf, n) == h[2]) {
            uint32_t i;
            for (i = 0; i < n; i++) ((uint8_t *)p)[i] = buf[i];
            found = 0;
        }
        off += log_len(h[1]);
    }
    return found;
}
static int log_load(void *p, uint32_t n)
{
    uint32_t end;
    return log_scan(p, n, &end);
}
static int log_save(const void *p, uint32_t n)
{
    static uint8_t rec[SECTOR_MAX + 12u], chk[64];
    uint32_t end, len = log_len(n), i, took, h[3] = {LOG_MAGIC, n, 0};
    if (!flash_ok || n > SECTOR_MAX)
        return -1;
    log_scan(rec, n, &end);                     /* (rec: a scratch here) */
    for (i = end; i + 64u <= 0x1000u && i < end + len; i += 64u) {   /* the place must still be erased */
        uint32_t j;
        if (st_read(SETTINGS_AT + i, chk, 64)) return -1;
        for (j = 0; j < 64u; j++) if (chk[j] != 0xFFu) end = 0x1000u;
    }
    if (end + len > 0x1000u) {                  /* full (or not a journal yet): erase, the one LED blink in ~50 saves */
        fm1_wdt_feed();
#if !BOARD_SMK37
        fm1__led_lines(0);
#endif
        if (fl_erase4k(SETTINGS_AT, &took)) return -1;
        end = 0;
    }
    h[2] = crc32((const uint8_t *)p, n);
    for (i = 0; i < 12u; i++) rec[i] = ((const uint8_t *)h)[i];
    for (i = 0; i < n; i++) rec[12u + i] = ((const uint8_t *)p)[i];
    for (; i + 12u < len; i++) rec[12u + i] = 0xFFu;
    return fl_write(SETTINGS_AT + end, rec, len);   /* (a cut write: its CRC fails, the next save goes after it) */
}
#endif
static uint32_t idle_ok, midi_src, clk_n, clk_t0, clk_bpm, clk_run, clk_beat_ms, pc_want = ~0u;

static void midi_in(void)
{
    uint8_t st, d1, d2;
    int src;
    while ((src = midi_get(&st, &d1, &d2)) != 0) {   /* 1 USB, 2 the TRS jack */
        uint32_t ty = st & 0xF0u;
        if (st == 0xF8u) {                      /* clock: 24 a quarter note */
            if (clk_n % 24u == 0u) {
                if (clk_n) {
                    uint32_t ms = fm1_ms - clk_t0;   /* a quarter's length */
                    clk_bpm = ms ? (60000u + ms / 2u) / ms : 0;
                }
                clk_t0 = fm1_ms;
                clk_beat_ms = fm1_ms;
            }
            clk_n++;
            continue;
        }
        if (st == 0xFAu) { clk_run = 1; clk_n = 0; ctl_seq_restart = 1; ctl_seq_run = 1; continue; }   /* START */
        if (st == 0xFBu) { clk_run = 1; ctl_seq_run = 1; continue; }             /* CONTINUE */
        if (st == 0xFCu) { clk_run = 0; ctl_seq_run = 0; continue; }             /* STOP */
        if (set.ch && (st & 15u) != set.ch - 1u) continue;      /* not our channel */
        if (ty == 0x90u && d2) { gp_note_on(d1, d2); midi_src = (uint32_t)src; }
        else if (ty == 0x80u || ty == 0x90u) gp_note_off(d1);
        else if (ty == 0xC0u) pc_want = d1;     /* a program change: the preset, from the main loop */
    }
}
static void app_idle(void)
{
    if (idle_ok) midi_in();
}

/* a key (a pad) plays its note, or GLO's chord on it: nt keeps what it plays, for its note-off */
static void play_on(uint8_t *nt, uint32_t root, uint32_t vel, uint32_t ch, uint32_t chord)
{
    uint32_t i, n = chord_notes(root, chord, nt);
    for (i = 0; i < n; i++) { gp_note_on(nt[i], vel); midi_send(0x90u | ch, nt[i], vel); }
}
static void play_off(uint8_t *nt, uint32_t ch)
{
    uint32_t i;
    for (i = 0; i < SEQ_CHORD; i++)
        if (nt[i]) { gp_note_off(nt[i]); midi_send(0x80u | ch, nt[i], 0); nt[i] = 0; }
}
static int32_t transp_of(uint32_t note) { return clamp((int32_t)note - 60, -24, 24); }   /* from C4 */

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, status_drawn = ~0u, midi_drawn = ~0u, preset_drawn = ~0u;
    uint32_t page_drawn = ~0u, cells_drawn = ~0u, wave_page = ~0u, wave_renders = ~0u, wave_morph = ~0u;
    uint32_t wave_t = 0, wave_live = 1, wave_pos = ~0u, saving = 0, save_to = 0, save_drawn = ~0u;
    uint32_t fn = FN_EDIT, layer = 0, sound_fn = FN_EDIT, fn_idx[NFN][2] = {{0}};   /* the function, the layer, the
                                                 * function FX goes back to, the page each function was left on */
    uint32_t t_dn[2] = {0, 0}, fired[2] = {1, 1};   /* ARP, FX: when pressed, a long press already done */
    uint32_t view = V_HOME, view_drawn = ~0u, t_home = 0, scope_t = 0, line_drawn = ~0u, seq_drawn = ~0u, seq_t = 0;
    uint32_t srow = 1, spage = 0, rec = 0, wave_fg = ~0u, step_in = 0, pat_dirty = 0, arp_last = A_UP, beat_seen = 0, beat_t = 0;   /* srow: SEQ's row */
    uint8_t held[27][SEQ_CHORD] = {{0}};        /* the notes each key plays */
#if BOARD_SMK37
    static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* white key -> key */
    uint8_t pad_note[16][SEQ_CHORD] = {{0}}, ext_note[FM1_NKEY][SEQ_CHORD] = {{0}};
    smk_pw_t wheel = {0};
    smk_pad_events = 1;                         /* the pads as events (smk_pad_take): they play here, lit while they sound */
#endif
    if (blob_load(0, &user, sizeof user)) {     /* the user presets (0.8's: without the knobs 0.9 added) */
        static struct { uint8_t used[NUSER]; int8_t p[NUSER][GQ_N_V08]; } u8;
        uint32_t j;
        for (k = 0; k < NUSER; k++) user.used[k] = 0;
        if (!blob_load(0, &u8, sizeof u8))
            for (k = 0; k < NUSER; k++) {
                user.used[k] = u8.used[k];
                for (j = 0; j < GQ_N; j++) user.p[k][j] = j < GQ_N_V08 ? u8.p[k][j] : GQ[j].def;
            }
    }
    if (blob_load(1, &pat, sizeof pat) || pat.len < 1u || pat.len > SEQ_MAX || pat.div >= NDIV) {   /* the sequence */
        static uint8_t p8[4u + SEQ_MAX * 7u];   /* (0.8's: steps of 7 bytes, no RATCH CHANCE, no ARP) */
        uint32_t j;
        pat_init(&pat);
        if (!blob_load(1, p8, sizeof p8) && p8[0] >= 1u && p8[0] <= SEQ_MAX && p8[1] < NDIV) {
            pat.len = p8[0], pat.div = p8[1], pat.swing = p8[2], pat.tempo = p8[3];
            for (k = 0; k < SEQ_MAX; k++) {
                const uint8_t *o = p8 + 4u + k * 7u;
                for (j = 0; j < SEQ_CHORD; j++) pat.st[k].note[j] = o[j];
                pat.st[k].vel = o[4], pat.st[k].gate = o[5], pat.st[k].chord = o[6];
            }
        }
    }
    {                                           /* the settings (0.8's: no PERFORM, fewer knobs) */
        static struct { uint8_t oct, ch, fn, layer, cur, mod, chord; int8_t p[GQ_N_V08]; } s8;
        int ok = !log_load(&set, sizeof set);
        if (!ok && !log_load(&s8, sizeof s8)) {
            set.oct = s8.oct, set.ch = s8.ch, set.fn = s8.fn, set.layer = s8.layer, set.cur = s8.cur;
            set.mod = s8.mod, set.chord = s8.chord;
            for (k = 0; k < 4u; k++) set.perf[k] = PERF_DEF[k];
            for (k = 0; k < GQ_N; k++) set.p[k] = k < GQ_N_V08 ? s8.p[k] : GQ[k].def;
            ok = 1;
        }
        for (k = 0; k < 4u; k++) if (set.perf[k] >= NPERF) set.perf[k] = PERF_DEF[k];
        if (!ok) set.fn = 0xFF;                 /* (nothing: the defaults below) */
    }
    if ((int8_t)set.oct < -2 || (int8_t)set.oct > 2 || set.ch > 16u || set.fn >= NFN
        || set.layer > 1u || set.chord >= NCHORD || !preset_ok(set.cur)) {
        set.oct = 0, set.ch = 0, set.fn = FN_EDIT, set.layer = 0, set.cur = 0, set.mod = 0, set.chord = 0;
        for (k = 0; k < 4u; k++) set.perf[k] = PERF_DEF[k];
        preset_load(0);
    } else
        for (k = 0; k < GQ_N; k++)              /* (a value out of its range: its default) */
            gp_p[k] = set.p[k] < GQ[k].min || set.p[k] > GQ[k].max ? GQ[k].def : set.p[k];
    fn = set.fn;
    layer = set.layer;

    splash();                                   /* 1.5 s, while the two sources render */
    for (k = fm1_ms; fm1_ms - k < 1500u;) {
        platform_poll();
        grain_poll();
    }
    lcd_fill(0, 0, 240, 240, C_BLACK);

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, out_ch = set.ch ? set.ch - 1u : 0u, st, pg, bpm, transp, hold_on;
        int32_t oct = (int8_t)set.oct, e;
        uint32_t base = (uint32_t)((int32_t)KEY0 + 12 * oct);
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        /* the transposition: while the sequencer runs or the arp holds notes (HOLD), a key moves what plays instead of
         * playing (SEQ's screen: the keys write steps). A key sounding: the keys join its chord (HOLD's notes) */
        hold_on = gp_p[GQ_AMODE] != A_OFF && ctl_hold && ui_nheld;
        if (!ctl_seq_run && !hold_on) ctl_transp = 0;
        transp = !(view == V_SEQ && rec) && !saving && (ctl_seq_run || hold_on);
        for (k = 0; k < 27u; k++) if (held[k][0]) transp = 0;
#if BOARD_SMK37
        for (k = 0; k < 16u; k++) if (pad_note[k][0]) transp = 0;
        for (k = 0; k < FM1_NKEY; k++) if (ext_note[k][0]) transp = 0;
#endif
        for (k = 0; k < 27u; k++) {                 /* (a key stops the notes it started: the octave may change meanwhile) */
#if BOARD_SMK37
            uint32_t vel = fm1_in.key_vel[SMK_KEY_F3 + k];   /* the SMK's keys feel the velocity */
#else
            uint32_t vel = 100;
#endif
            if (down >> k & 1u) {
                uint32_t note = base + k;
                if (view == V_SEQ && rec && !saving && k == KEY_REST) {   /* REC: the top C# writes a rest */
                    if (!step_in) {
                        uint32_t len = (uint32_t)clamp(pat.len, 1, SEQ_MAX);
                        if (!srow) srow = 1;
                        step_rest(&pat.st[(srow - 1u) % SEQ_MAX]);
                        srow = srow % len + 1u;
                        pat_dirty = 1;
                        t_change = fm1_ms;
                    }
                    continue;
                }
                if (transp) { ctl_transp = transp_of(note); continue; }
                play_on(held[k], note, vel, out_ch, set.chord);
                if (view == V_SEQ && rec && !saving) {   /* SEQ, REC: the row's step; keys held together, or GLO's
                                                 * CHORD, a chord */
                    step_t *s;
                    uint32_t i;
                    if (!srow) srow = 1;
                    s = &pat.st[(srow - 1u) % SEQ_MAX];
                    if (!step_in || s->chord || set.chord) step_rest(s);   /* (the gate, ratchet, chance stay) */
                    step_in = 1;
                    if (set.chord) { chord_notes(note, set.chord, s->note); s->chord = (uint8_t)set.chord; }
                    else {
                        for (i = 0; i < SEQ_CHORD && s->note[i] && s->note[i] != note; i++) ;
                        if (i < SEQ_CHORD && !s->note[i]) s->note[i] = (uint8_t)note;
                    }
                    s->vel = (uint8_t)vel;
                    pat_dirty = 1;
                    t_change = fm1_ms;
                }
            }
            if (up >> k & 1u) play_off(held[k], out_ch);
        }
        if (step_in && !keys) {                 /* every key of the chord up: the next row */
            step_in = 0;
            srow = srow % (uint32_t)clamp(pat.len, 1, SEQ_MAX) + 1u;
        }
#if BOARD_SMK37
        while ((e = smk_extkey_take()) >= 0) {  /* the keys left and right of the FM-1's 27: key << 8 | velocity */
            uint32_t key = (uint32_t)e >> 8, vel = (uint32_t)e & 0x7Fu;
            if (key >= FM1_NKEY) continue;
            if (vel && transp) ctl_transp = transp_of(base + key - SMK_KEY_F3);
            else if (vel) { play_off(ext_note[key], out_ch); play_on(ext_note[key], base + key - SMK_KEY_F3, vel, out_ch, set.chord); }
            else play_off(ext_note[key], out_ch);
        }
        while ((e = smk_pad_take()) >= 0) {     /* a pad: hardware pad << 8 | velocity (0 = let go) */
            uint32_t pad = ((uint32_t)e >> 8) & 15u, vel = (uint32_t)e & 0x7Fu;
            uint32_t w = pad < 8u ? pad + 8u : pad - 8u;   /* bottom row = white keys 1-8, top row 9-16 */
            if (vel && transp) ctl_transp = transp_of(base + WHITE[w]);
            else if (vel) { play_off(pad_note[pad], out_ch); play_on(pad_note[pad], base + WHITE[w], vel, out_ch, set.chord); }
            else play_off(pad_note[pad], out_ch);
        }
        {
            int32_t bd = smk_pw_bend(&wheel, fm1_in.wheel[0], fm1_ms);   /* 1/4096 semitone */
            gp_bend_q16 = (uint32_t)(65536 + (bd * 3785 >> 12) + (((bd * bd) >> 12) * 109 >> 12));   /* ~2^(s/12) */
        }
#endif

        b = fm1_input_edges(&rel);              /* buttons pressed since the last call (bit = button id) */
        if (b & (1u << BTN_OCT_DOWN) && oct > -2) { set.oct = (uint8_t)(oct - 1); dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && oct < 2) { set.oct = (uint8_t)(oct + 1); dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_HOME)) t_home = fm1_ms;
        if (b & (1u << BTN_PLAY)) {             /* the sequencer: from the top, or stop */
            if (ctl_seq_run) ctl_seq_run = 0;
            else { ctl_seq_restart = 1; ctl_seq_run = 1; }
        }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0)   /* the layer: GRAIN (left), ANALOG (right) */
            layer = e > 0 ? 1u : 0u;
        pg = FN_PAGE[fn][layer][fn_idx[fn][layer] % fn_count(fn, layer)];
        e = fm1_enc_take(ENC_PRESETS);
        if (saving) {                           /* the save screen: PRESETS the slot, SAVE writes, HOME cancels */
            save_to = (uint32_t)clamp((int32_t)save_to + e, 0, (int32_t)NUSER - 1);
            if (b & (1u << BTN_SAVE)) {
                for (k = 0; k < GQ_N; k++) user.p[save_to][k] = gp_p[k];
                user.used[save_to] = 1;
                blob_save(0, &user, sizeof user);   /* (an erase holds the sound for some 50 ms) */
                set.cur = (uint8_t)(GP_NFACT + save_to);
                set.mod = 0;
                saving = 0;
                dirty = 1;
                t_change = fm1_ms;
            } else if (rel & (1u << BTN_HOME))
                saving = 0;
            if (!saving) view_drawn = ~0u;      /* back: everything again */
        } else {
            if (e) {                            /* the next preset there is, one a detent */
                int32_t i = (int32_t)set.cur, d = e < 0 ? -1 : 1, n = e < 0 ? -e : e;
                while (n--) {
                    int32_t j = i + d;
                    while (j >= 0 && j < (int32_t)NPRESET && !preset_ok((uint32_t)j)) j += d;
                    if (j < 0 || j >= (int32_t)NPRESET) break;
                    i = j;
                }
                if (i != (int32_t)set.cur) pc_want = (uint32_t)i;   /* (at either end: the sound stays as it is) */
            }
            if (pc_want != ~0u) {
                if (preset_ok(pc_want)) {
                    preset_load(pc_want);
                    set.cur = (uint8_t)pc_want;
                    set.mod = 0;
                    dirty = 1;
                    t_change = fm1_ms;
                }
                pc_want = ~0u;
            }
            if (b & (1u << BTN_SAVE)) {         /* the save screen: the slot of this sound, else the first empty one */
                saving = 1;
                save_drawn = ~0u;
                view_drawn = ~0u;
                save_to = 0;
                if (set.cur >= GP_NFACT) save_to = set.cur - GP_NFACT;
                else while (save_to < NUSER - 1u && user.used[save_to]) save_to++;
            }
            if (rel & (1u << BTN_HOME) && fm1_ms - t_home < 500u)   /* a short press: the wave and the scope */
                view = V_HOME;
            if (b & (1u << BTN_SEQ)) {          /* SEQ's screen; pressed again: its other page */
                if (view == V_SEQ) spage ^= 1u;
                view = V_SEQ;
            }
            for (k = 0; k < 2u; k++) {          /* ARP, FX: a short press on release, a long one (half a second) held */
                uint32_t id = k ? BTN_FX : BTN_ARP;
                if (b & (1u << id)) { t_dn[k] = fm1_ms; fired[k] = 0; }
                if (!fired[k] && (fm1_in.buttons >> id & 1u) && fm1_ms - t_dn[k] >= 500u) {
                    fired[k] = 1;
                    if (!k)                     /* ARP held: HOLD on or off */
                        ctl_hold = !ctl_hold;
                    else {                      /* FX held: the four effects' MIX */
                        if (fn != FN_FX && fn != FN_MIX) sound_fn = fn;
                        fn = FN_MIX;
                        view = V_PAGES;
                    }
                }
                if (rel & (1u << id) && !fired[k]) {
                    fired[k] = 1;
                    if (!k) {                   /* ARP: the arp on (as it was last) or off, and its page */
                        if (gp_p[GQ_AMODE] != A_OFF) { arp_last = (uint32_t)gp_p[GQ_AMODE]; gp_p[GQ_AMODE] = A_OFF; }
                        else gp_p[GQ_AMODE] = (int8_t)arp_last;
                        fn = FN_ARP;
                        set.mod = 1;
                        dirty = 1;
                        t_change = fm1_ms;
                    } else if (view == V_PAGES && (fn == FN_FX || fn == FN_MIX))
                        fn = sound_fn;          /* FX: back to the sound */
                    else {
                        if (fn != FN_FX && fn != FN_MIX) sound_fn = fn;
                        fn = FN_FX;             /* .. or to the effects */
                    }
                    view = V_PAGES;
                }
            }
            /* (a page changed is not saved by itself: a save erases a flash sector with the interrupts off for some
             * 50 ms, and the LED scan stops on one column meanwhile, a visible jump. The page goes with the next save) */
            {                                   /* EDIT SEL ENV LFO GLO: their function; pressed again: its next page */
                static const uint8_t FB[5][2] = {{BTN_EDIT, FN_EDIT}, {BTN_SEL, FN_SEL}, {BTN_ENV, FN_ENV},
                                                 {BTN_LFO, FN_LFO}, {BTN_GLO, FN_GLO}};
                for (k = 0; k < 5u; k++)
                    if (b & (1u << FB[k][0])) {
                        uint32_t f = FB[k][1];
                        if (view == V_PAGES && fn == f)
                            fn_idx[f][layer] = (fn_idx[f][layer] + 1u) % fn_count(f, layer);
                        fn = f;
                        view = V_PAGES;
                    }
            }
            pg = FN_PAGE[fn][layer][fn_idx[fn][layer] % fn_count(fn, layer)];
            if (view == V_SEQ) {                /* SEQ: SELECT the row, the knobs its values, REC the keys write */
                uint32_t len = (uint32_t)clamp(pat.len, 1, SEQ_MAX), n = len + 1u;
                if (srow > len) srow = len;
                if ((e = fm1_enc_take(ENC_SELECT)) != 0 && !step_in)
                    srow = (uint32_t)((int32_t)srow + e % (int32_t)n + (int32_t)n) % n;
                if (b & (1u << BTN_REC)) rec = !rec;
                for (k = 0; k < 4u; k++)
                    if ((e = fm1_enc_take(ENC_KNOB1 + k)) != 0) {
                        if (srow) seq_knob_step(&pat.st[(srow - 1u) % SEQ_MAX], k, e, spage);
                        else seq_knob_pat(k, e, spage);
                        pat_dirty = 1;
                        t_change = fm1_ms;
                    }
            } else {
                if ((e = fm1_enc_take(ENC_SELECT)) != 0) {    /* the function's pages, round (from HOME: back first) */
                    int32_t c = (int32_t)fn_count(fn, layer);
                    if (view == V_PAGES)
                        fn_idx[fn][layer] = (uint32_t)(((int32_t)fn_idx[fn][layer] + e % c + c) % c);
                    pg = FN_PAGE[fn][layer][fn_idx[fn][layer] % (uint32_t)c];
                    view = V_PAGES;
                }
                for (k = 0; k < 4u; k++)        /* the page's values (HOME: PERFORM's) */
                    if ((e = fm1_enc_take(ENC_KNOB1 + k)) != 0) {
                        uint32_t q = PAGES[pg].q[k];
                        if (view == V_HOME) {
                            knob_turn(PERF[set.perf[k] % NPERF].q, k, e);
                            set.mod = 1;
                            dirty = 1;
                            t_change = fm1_ms;
                            continue;
                        }
                        view = V_PAGES;
                        if (q == QX) continue;
                        if (q >= QP1 && q < QP1 + 4u) {   /* PERFORM: what HOME's knob turns, round the list */
                            uint8_t *pp = &set.perf[q - QP1];
                            *pp = (uint8_t)((uint32_t)((int32_t)(*pp % NPERF) + e % (int32_t)NPERF + (int32_t)NPERF) % NPERF);
                            dirty = 1;
                            t_change = fm1_ms;
                            continue;
                        }
                        if (q == QCH) {         /* the MIDI channel: the device's, not the sound's */
                            set.ch = (uint8_t)clamp((int32_t)set.ch + e, 0, 16);
                            dirty = 1;
                            t_change = fm1_ms;
                            continue;
                        }
                        if (q == QCHORD) {      /* the chord a key plays: the device's too; round the list */
                            set.chord = (uint8_t)((uint32_t)((int32_t)set.chord + e % (int32_t)NCHORD + (int32_t)NCHORD) % NCHORD);
                            dirty = 1;
                            t_change = fm1_ms;
                            continue;
                        }
                        knob_turn(q, k, e);
                        set.mod = 1;
                        dirty = 1;
                        t_change = fm1_ms;
                    }
            }
        }

        if (view != V_SEQ) rec = 0;             /* (REC: on SEQ's screen only) */
        midi_in();
        if (clk_bpm && fm1_ms - clk_t0 > 2000u) clk_bpm = 0, clk_run = 0;   /* the clock has stopped coming */
        ctl_tempo_ext = clk_bpm >= 40u && clk_bpm <= 240u ? clk_bpm : 0u;   /* a MIDI clock sets the tempo */
        bpm = ctl_tempo_ext ? ctl_tempo_ext : pat.tempo;
        grain_poll();                           /* a slice of a render, if one is due */
        if (ui_beat != beat_seen) { beat_seen = ui_beat; beat_t = fm1_ms; }

        oct = (int8_t)set.oct;
        base = (uint32_t)((int32_t)KEY0 + 12 * oct);
        for (k = 0; k < GP_NV; k++)             /* what sounds, on the keys of this octave */
            if (gpv[k].gate && gpv[k].note >= base && gpv[k].note < base + 27u)
                lit |= 1u << (gpv[k].note - base);
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key_glow(KEY_LED(KEY_REST), view == V_SEQ && rec && !saving && !(lit >> KEY_REST & 1u));   /* the rest key */
        led_fn(BTN_OCT_DOWN, oct < 0);
        led_fn(BTN_OCT_UP, oct > 0);
        led_fn(BTN_SAVE, saving);
        led_fn(BTN_HOME, view == V_HOME && !saving);
        led_fn(BTN_FX, view == V_PAGES && (fn == FN_FX || fn == FN_MIX));
        led_fn(BTN_EDIT, view == V_PAGES && fn == FN_EDIT);
        led_fn(BTN_SEL, view == V_PAGES && fn == FN_SEL);
        led_fn(BTN_ENV, view == V_PAGES && fn == FN_ENV);
        led_fn(BTN_LFO, view == V_PAGES && fn == FN_LFO);
        led_fn(BTN_GLO, view == V_PAGES && fn == FN_GLO);
        led_fn(BTN_ARP, ctl_hold ? (fm1_ms / 300u) & 1u : gp_p[GQ_AMODE] != A_OFF);   /* HOLD: it blinks */
        led_fn(BTN_SEQ, view == V_SEQ);
        led_fn(BTN_PLAY, ctl_seq_run ? fm1_ms - beat_t >= 80u : clk_bpm && fm1_ms - clk_beat_ms < 80u);   /* a blink a beat */
        if (view == V_SEQ) led_fn(BTN_REC, rec);   /* (REC: the keys write, on the SEQ screen only) */
        else { fm1_led_key(BTN_REC, 0); fm1_led_key_glow(BTN_REC, 0); }
#if BOARD_SMK37
        for (k = 0; k < 16u; k++) {             /* a pad glows orange while its key sounds */
            uint32_t w = k < 8u ? k + 8u : k - 8u, on = lit >> WHITE[w] & 1u;
            padled_set(k, on ? 128 : 8, on ? 70 : 4, on ? 10 : 1);
        }
        padled_flush();                         /* (sends only what changed) */
#endif

        idle_ok = 1;                            /* the drawing below may read MIDI meanwhile (LCD_IDLE) */
        st = saving ? 9u : view;
        if (st != view_drawn) {                 /* another screen: everything under the name again */
            view_drawn = st;
            lcd_fill(0, 28, 240, 212, C_BLACK);
            page_drawn = cells_drawn = status_drawn = midi_drawn = line_drawn = seq_drawn = wave_page = ~0u;
        }
        if ((st = set.cur | (uint32_t)set.mod << 8) != preset_drawn) { preset_drawn = st; draw_preset(set.cur, set.mod); }
        if (saving) {
            if (page_drawn != ~1u) { page_drawn = ~1u; draw_text_box(0, 28, 240, &FONT_L, "SAVE", C_ACC, 1); }
            if ((st = save_to | (uint32_t)user.used[save_to] << 8) != save_drawn) { save_drawn = st; draw_save(save_to); }
        } else if (view == V_SEQ) {
            uint32_t len = (uint32_t)clamp(pat.len, 1, SEQ_MAX), row = srow > len ? len : srow;
            uint32_t play = ctl_seq_run ? ui_seq_step : ~0u, sum = row | play << 8, i;
            if (page_drawn != 200u + spage + rec * 2u) {   /* SEQ, SEQ+ (its second page); REC */
                static const char *const T[4] = {"SEQ", "SEQ+", "SEQ  REC", "SEQ+ REC"};
                page_drawn = 200u + spage + rec * 2u;
                draw_text_box(0, 28, 240, &FONT_L, T[spage + rec * 2u], rec ? C_ACC : C_WHITE, 1);
            }
            for (i = 0; i < SEQ_MAX; i++) {
                const step_t *s = &pat.st[i];
                sum = sum * 31u + s->note[0] + s->note[1] * 7u + s->note[2] * 13u + s->chord * 17u + s->vel * 19u + s->gate * 23u
                    + s->ratch * 29u + s->chance * 37u;
            }
            sum = sum * 31u + len + (uint32_t)pat.div * 977u + (uint32_t)pat.swing * 7919u + bpm * 104729u + pat.arp * 3u
                + spage * 1299709u;
            if (sum != seq_drawn && fm1_ms - seq_t >= 30u) {
                seq_drawn = sum;
                seq_t = fm1_ms;
                draw_seq_rows(row, play, spage);
                draw_seq_cells(row, bpm, spage);
            }
            if ((st = (bpm | (uint32_t)(ctl_transp + 32) << 8 | row << 14 | len << 21) * 131u + play + rec * 0x80000000u) != line_drawn) {
                line_drawn = st;
                draw_seq_lines(row, bpm, play, rec);
            }
        } else {
            uint32_t sum = pg, live = 0, wpg = view == V_HOME ? 2u : pg;
            if (view == V_HOME) {
                if (page_drawn != 100u + layer) { page_drawn = 100u + layer; draw_text_box(0, 28, 240, &FONT_L, LAYER_NAME[layer], C_ACC, 1); }
                if (fm1_ms - scope_t >= 40u) { scope_t = fm1_ms; draw_scope(set.perf); }   /* 25 frames a second */
                if ((st = (uint32_t)gp_p[GQ_AMODE] | ctl_seq_run << 4 | ctl_hold << 5 | bpm << 8 | (uint32_t)(ctl_transp + 32) << 16) != line_drawn) { line_drawn = st; draw_home_line(ctl_seq_run, bpm, ctl_hold); }
            } else {
                if (pg != page_drawn) { page_drawn = pg; draw_text_box(0, 28, 240, &FONT_L, PAGES[pg].name, C_WHITE, 1); }
                for (k = 0; k < 4u; k++) {
                    uint32_t q = PAGES[pg].q[k];
                    sum = sum * 131u + (q == QX ? 0u : q == QCH ? set.ch : q == QCHORD ? set.chord
                                        : q >= QP1 && q < QP1 + 4u ? set.perf[q - QP1] : (uint8_t)gp_p[q]);
                }
                if (sum != cells_drawn) { cells_drawn = sum; draw_cells(pg, set.ch, set.chord, set.perf); }
                if ((st = (uint32_t)(oct + 2) | (uint32_t)set.ch << 4 | (uint32_t)(ctl_transp + 32) << 9) != status_drawn) { status_drawn = st; draw_status(oct, set.ch, ctl_transp); }
            }
            if ((st = midi_src | clk_bpm << 2 | clk_run << 12) != midi_drawn) { midi_drawn = st; draw_midi(midi_src, clk_bpm, clk_run); }
            for (k = 0; k < GP_NG; k++) live |= gpg[k].owner;
            st = (uint32_t)(uint8_t)gp_p[GQ_MORPH];
            if (wpg != wave_page || grs.renders != wave_renders || (wpg >= 2u && st != wave_morph)) {
                wave_page = wpg; wave_renders = grs.renders; wave_morph = st;
                wave_take(wpg);
                wave_live = 1;
            }
            st = (uint32_t)gp_pos_view | (uint32_t)(uint8_t)gp_p[GQ_SPRD] << 16;
            if (wpg >= 2u && st != wave_pos) wave_pos = st, wave_live |= fm1_ms - wave_t >= 50u;
            if (live) wave_live |= fm1_ms - wave_t >= 50u;   /* the grains move: 20 frames a second */
            if (ana_take() | (layer != wave_fg)) { wave_fg = layer; wave_live = 1; }   /* the analog layer's, the
                                                 * layer in front */
            if (wave_live) { wave_live = 0; wave_t = fm1_ms; draw_wave(wpg, layer); }
        }
        idle_ok = 0;

        if (dirty && gp_quiet() && !grs.busy && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent */
            for (k = 0; k < GQ_N; k++) set.p[k] = gp_p[k];
            set.fn = (uint8_t)fn;
            set.layer = (uint8_t)layer;
            log_save(&set, sizeof set);
            dirty = 0;
        }
        if (pat_dirty && !ctl_seq_run && !step_in && gp_quiet() && fm1_ms - t_change > 2000u) {   /* the sequence */
            blob_save(1, &pat, sizeof pat);
            pat_dirty = 0;
        }
    }
}
