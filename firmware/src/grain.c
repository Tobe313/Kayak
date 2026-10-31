/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca), Tobe313 (Kayak) */
/* Kayak's GRAIN: ChoralRoot FM-1's granular engine (https://github.com/Quixotic7/ChoralRootFM1, docs/GRAIN.md,
 * firmware/src/eng_grain.c), cut down to one part with its own knobs. What stays: the source generator unchanged
 * (grain_src.c: TYPE TONE MOTION SEED render a 743 ms buffer at 22.05 kHz, sources A and B), the grain player
 * (gp_spawn, gp_run: a pool of 16 grains shared by the voices, the Hann window, SIZE DENS POS SPRD RAND, the
 * 1/sqrt(overlap) level, every grain reading A and B mixed by MORPH, SCAN), a low-pass SVF a voice. What goes: the
 * matrix, the LFOs, ENV 2, the filter's types and drive, the presets and the patch blob, the wave band's colours.
 * What changes: a render goes to a third buffer and swaps in when it is whole (no render in place, no declick), in
 * slices from the main loop (grain_poll); the amplitude envelope is the engine's own (ATK REL).
 *
 *   the audio side (app_audio, the interrupt): gp_audio
 *   the main loop: gp_note_on / gp_note_off, grain_poll (one slice a call), gp_p[] the knobs (one writer: the loop) */
#include "grain_dsp.h"
#include "grain_src.c"

#define GP_NV 8u                 /* voices */
#define GP_NG 16u                /* grains, shared by the voices */
#define CTL 32u                  /* a control block: the window's ramp, a grain start at most, the filter's coefficients */
#define CTL_LOG2 5
#define GP_STEP_MAX (2u << 16)   /* the rate's cap: two source samples an output sample (C1 .. C6 from one buffer) */
#define GP_RATE_Q16 ((GS_RATE << 16) / 44100u)
#define GP_VOICE 12000           /* a voice's level at full velocity */

/* ---- the knobs: 24 values, six pages of four (dinghy.c draws and turns them) */
enum {
    GQ_TYPE_A, GQ_TONE_A, GQ_MOTION_A, GQ_SEED_A,
    GQ_TYPE_B, GQ_TONE_B, GQ_MOTION_B, GQ_SEED_B,
    GQ_SIZE, GQ_DENS, GQ_POS, GQ_SPRD,
    GQ_MORPH, GQ_RAND, GQ_PITCH, GQ_SCAN,
    GQ_CUT, GQ_RES, GQ_ATK, GQ_REL,
    GQ_LRATE, GQ_LWAVE, GQ_LDEPTH, GQ_LDEST,
    GQ_N
};
/* how a value reads on the screen (gq_text) */
enum { GF_INT, GF_TYPE, GF_PCT, GF_SIGNED, GF_SEMI, GF_SIZE, GF_DENS, GF_CUT, GF_TIME, GF_LFOHZ, GF_LWAVE, GF_LDEST };
#define GL_NWAVE 5u
static const char *const GL_WAVE_NAMES[GL_NWAVE] = {"SINE", "TRI", "SAW", "SQUARE", "S&H"};
enum { GD_MORPH, GD_POS, GD_SIZE, GD_DENS, GD_SPRD, GD_PITCH, GD_CUT, GD_N };
static const char *const GL_DEST_NAMES[GD_N] = {"MORPH", "POS", "SIZE", "DENS", "SPRD", "PITCH", "CUT"};
typedef struct { const char *name; int8_t min, max, def; uint8_t fmt; } gq_desc_t;
static const gq_desc_t GQ[GQ_N] = {
    {"TYPE", 0, GS_NTYPE - 1, GT_DRONE, GF_TYPE}, {"TONE", 0, 127, 64, GF_INT}, {"MOTION", 0, 127, 64, GF_INT},
    {"SEED", 0, 127, 0, GF_INT},
    {"TYPE", 0, GS_NTYPE - 1, GT_BELL, GF_TYPE}, {"TONE", 0, 127, 64, GF_INT}, {"MOTION", 0, 127, 64, GF_INT},
    {"SEED", 0, 127, 0, GF_INT},
    {"SIZE", 0, 127, 92, GF_SIZE}, {"DENS", 0, 127, 88, GF_DENS}, {"POS", 0, 127, 50, GF_PCT}, {"SPRD", 0, 127, 40, GF_PCT},
    {"MORPH", 0, 127, 40, GF_PCT}, {"RAND", 0, 127, 14, GF_PCT}, {"PITCH", -24, 24, 0, GF_SEMI},
    {"SCAN", -64, 63, 12, GF_SIGNED},
    {"CUT", 0, 127, 100, GF_CUT}, {"RES", 0, 127, 20, GF_PCT}, {"ATK", 0, 127, 30, GF_TIME}, {"REL", 0, 127, 84, GF_TIME},
    {"RATE", 0, 127, 40, GF_LFOHZ}, {"WAVE", 0, GL_NWAVE - 1, 0, GF_LWAVE}, {"DEPTH", 0, 127, 0, GF_PCT},
    {"DEST", 0, GD_N - 1, GD_MORPH, GF_LDEST},
};
static volatile int8_t gp_p[GQ_N];
static void gp_defaults(void)
{
    uint32_t i;
    for (i = 0; i < GQ_N; i++)
        gp_p[i] = GQ[i].def;
}

/* ---- the factory presets: ChoralRoot's eight GRAIN sounds (eng_grain.c GRAIN_PP: the sources, the grain knobs,
 * CUT RES), with an envelope and an LFO of Kayak's for each */
#define GP_NFACT 8u
static const char *const GP_FACT_NAMES[GP_NFACT] = {
    "CLOUD PAD", "SHIMMER", "BREATH PAD", "FROZEN", "STRUM", "SWARM", "ACID DUST", "GLITCH"};
static const int8_t GP_FACT[GP_NFACT][GQ_N] = {
    /* A: TYPE TONE MOTION SEED | B: the same | SIZE DENS POS SPRD | MORPH RAND PITCH SCAN | CUT RES ATK REL | LFO */
    {GT_DRONE, 64, 64, 0, GT_BELL, 64, 64, 0, 92, 88, 50, 40, 40, 14, 0, 12, 100, 20, 40, 90, 30, 0, 50, GD_MORPH},
    {GT_BELL, 64, 64, 0, GT_BELL, 104, 104, 11, 70, 100, 30, 30, 64, 24, 12, 8, 110, 10, 30, 96, 45, 0, 70, GD_MORPH},
    {GT_BREATH, 64, 64, 0, GT_VOWEL, 64, 20, 3, 92, 88, 50, 40, 64, 10, 0, 12, 96, 30, 50, 96, 25, 1, 40, GD_POS},
    {GT_VOWEL, 28, 20, 3, GT_BREATH, 28, 20, 3, 108, 72, 40, 0, 0, 10, 0, 5, 92, 10, 60, 100, 20, 1, 30, GD_MORPH},
    {GT_PLUCK, 64, 100, 0, GT_METAL, 64, 64, 0, 60, 80, 20, 30, 0, 10, 0, 0, 110, 0, 2, 70, 50, 2, 40, GD_MORPH},
    {GT_CHIRP, 64, 64, 0, GT_SWEEP, 64, 64, 0, 48, 110, 50, 60, 64, 60, 0, 20, 120, 20, 20, 80, 60, 4, 50, GD_CUT},
    {GT_SWEEP, 104, 104, 11, GT_PLUCK, 104, 104, 11, 30, 118, 50, 80, 40, 40, 0, -24, 127, 40, 5, 60, 70, 1, 70, GD_CUT},
    {GT_METAL, 64, 64, 0, GT_DRONE, 64, 64, 0, 24, 112, 64, 100, 64, 90, 0, 0, 127, 0, 2, 50, 80, 3, 60, GD_SIZE},
};

/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (eng_grain.c's) */
static uint32_t pow2_q16(int32_t d16);
/* the LFO's rate, hundredths of a hertz: 0.05 .. 20 Hz */
static uint32_t gl_hz100(int32_t v) { return (5u * pow2_q16(clamp(v, 0, 127) * 1307 / 100)) >> 16; }
/* an envelope time in ms: 2 ms .. 3.1 s */
static uint32_t gp_env_ms(int32_t p) { return (2u * pow2_q16(clamp(p, 0, 127) * 16)) >> 16; }

/* value v of knob q as the screen shows it, into s (12 bytes) */
static void gq_put_dec(char **p, uint32_t u)
{
    char t[10];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) *(*p)++ = t[--n];
}
static void gq_put_str(char **p, const char *a) { while (*a) *(*p)++ = *a++; }
static void gq_put_frac(char **p, uint32_t x100, int two)   /* hundredths as 1.23 (two) or 1.2 */
{
    gq_put_dec(p, x100 / 100u);
    *(*p)++ = '.';
    *(*p)++ = (char)('0' + x100 / 10u % 10u);
    if (two) *(*p)++ = (char)('0' + x100 % 10u);
}
static void gq_text(uint32_t q, int32_t v, char *s)
{
    char *p = s;
    uint32_t u;
    switch (GQ[q].fmt) {
    case GF_TYPE: gq_put_str(&p, GS_TYPE_NAMES[clamp(v, 0, GS_NTYPE - 1)]); break;
    case GF_LWAVE: gq_put_str(&p, GL_WAVE_NAMES[clamp(v, 0, GL_NWAVE - 1)]); break;
    case GF_LDEST: gq_put_str(&p, GL_DEST_NAMES[clamp(v, 0, GD_N - 1)]); break;
    case GF_PCT: gq_put_dec(&p, (uint32_t)(v * 100 + 63) / 127u); *p++ = '%'; break;
    case GF_SIGNED:
    case GF_SEMI:
        if (v) *p++ = v < 0 ? '-' : '+';
        gq_put_dec(&p, (uint32_t)(v < 0 ? -v : v));
        if (GQ[q].fmt == GF_SEMI) gq_put_str(&p, " st");
        break;
    case GF_SIZE: gq_put_dec(&p, (10u * pow2_q16(v * 8)) >> 16); gq_put_str(&p, "ms"); break;   /* 441 samples: 10 ms */
    case GF_DENS:                               /* grains a second: 65536 / 2^(-DENS x 10 / 192), in tenths below 10 */
        u = 6553600u / pow2_q16(-v * 10);       /* x 100 */
        if (u < 1000u) gq_put_frac(&p, u, 0); else gq_put_dec(&p, (u + 50u) / 100u);
        gq_put_str(&p, "/s");
        break;
    case GF_CUT:                                /* 30 Hz .. 16 kHz: 30 x 2^(v x 9.06 / 127) */
        u = (30u * pow2_q16(v * 13696 / 1000)) >> 16;
        if (u < 1000u) { gq_put_dec(&p, u); gq_put_str(&p, "Hz"); }
        else { gq_put_frac(&p, u / 10u, 0); *p++ = 'k'; }
        break;
    case GF_TIME:
        u = gp_env_ms(v);
        if (u < 1000u) { gq_put_dec(&p, u); gq_put_str(&p, "ms"); }
        else { gq_put_frac(&p, u / 10u, 0); *p++ = 's'; }
        break;
    case GF_LFOHZ:
        u = gl_hz100(v);
        if (u < 1000u) gq_put_frac(&p, u, 1); else gq_put_frac(&p, u, 0);
        gq_put_str(&p, "Hz");
        break;
    default: gq_put_dec(&p, (uint32_t)(v < 0 ? -v : v)); break;
    }
    *p = 0;
}

/* ---- the buffers: sources A and B and a third the next render goes into; gsel says which one each source reads */
static int16_t gbuf[3][GS_N] __attribute__((section(".pool")));
static volatile uint8_t gsel[2] = {0, 1};
static volatile int32_t groot16 = GS_ROOT16;     /* A's root: one read head, one root (ChoralRoot's rule) */

static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}
/* grain level by overlap (grains sounding at once on average): 1 / sqrt(n), Q15 */
static const int16_t GP_NORM[GP_NG + 1] = {32767, 32767, 23170, 18918, 16384, 14654, 13377, 12385, 11585, 10922,
                                           10362, 9880, 9459, 9088, 8757, 8460, 8192};
typedef struct {
    uint32_t pos;                /* forward: the next source sample to fetch; reverse: the index of a */
    int32_t frac;                /* Q16 between a and b */
    uint32_t step;               /* Q16 source samples an output sample */
    uint32_t wph, winc, left;    /* window phase (one turn = the grain), output samples to go */
    int32_t a, b, gain;          /* the two samples it interpolates (A and B mixed as fetched), its level */
    uint8_t owner, rev;          /* voice + 1 (0 = free), reversed */
} gp_grain_t;
typedef struct {
    int32_t gate;                /* 1 held */
    int32_t env;                 /* the amplitude, Q15 */
    int32_t next;                /* samples to the next grain */
    int32_t morph;               /* MORPH as the last block played it, Q15 (ramped a fetch to this one's) */
    int32_t ic1, ic2;            /* the filter */
    int32_t lvl;                 /* the velocity, Q15 */
    uint32_t age;
    uint8_t note;
} gp_voice_t;
static gp_grain_t gpg[GP_NG];
static gp_voice_t gpv[GP_NV];
static uint32_t gp_scan, gp_ages, gp_lph;         /* SCAN's phase, the voices' ages, the LFO's phase */
static int32_t gp_lsh;                           /* the LFO's S&H value */
static volatile int32_t gp_pos_view;             /* where the grains start (POS, SCAN, the LFO), Q8: the screen's */
static int32_t gp_rng = 0x2545F491;
static volatile uint32_t gp_bend_q16 = 65536;    /* the SMK-37's pitch wheel */

static inline uint32_t gp_scale(uint32_t n, uint32_t f16) { return (n >> 16) * f16 + (((n & 0xFFFFu) * f16) >> 16); }
/* the grain window, Hann: sin^2(pi x) = (1 - cos(2 pi x)) / 2, Q15 */
static inline int32_t gp_win(uint32_t wph) { return (32767 - sine_i(wph + 0x40000000u)) >> 1; }
/* A and B at source sample i, mixed by MORPH mq (Q15, 0 .. 32768) */
static inline int32_t gp_mix(const int16_t *A, const int16_t *B, uint32_t i, int32_t mq)
{
    int32_t x = A[i];
    return x + (((B[i] - x) * mq) >> 15);
}

typedef struct { int32_t size, dens, sprd, rnd, pitch16, morph, pos_q8; } gp_eff_t;
/* start a grain of voice vi at its interval iv (for the overlap's level) */
static void gp_spawn(uint32_t vi, uint32_t iv, const gp_eff_t *e, const int16_t *A, const int16_t *B)
{
    gp_grain_t *g = 0;
    uint32_t i, n = GS_N, len, step, span, c, sp, r1, r2, ov, rev;
    int32_t d16, amt, pos;
    for (i = 0; i < GP_NG && !g; i++)
        if (!gpg[i].owner)
            g = &gpg[i];
    if (!g)
        return;
    r1 = noise32(&gp_rng);
    r2 = noise32(&gp_rng);
    len = (441u * pow2_q16(e->size * 8)) >> 16;             /* 10 .. 393 ms */
    amt = e->rnd * e->rnd * 192 / (127 * 127);
    d16 = e->pitch16 - groot16 + (((int32_t)(r1 & 0xFFFFu) - 32768) * amt >> 15);
    step = (pow2_q16(clamp(d16, -1536, 576)) >> 8) * (GP_RATE_Q16 >> 8);
    step = (step >> 8) * (gp_bend_q16 >> 8);
    step = step > GP_STEP_MAX ? GP_STEP_MAX : step < 256u ? 256u : step;
    rev = ((r1 >> 16) & 255u) < (uint32_t)e->rnd;           /* RAND 127: half of them */
    span = (len * (step >> 4)) >> 12;
    if (span + 3u > n) {
        span = n - 3u;
        len = (span << 8) / (step >> 8);
    }
    if (len < 16u)
        return;
    c = gp_scale(n - 1u, (uint32_t)clamp(e->pos_q8, 0, 127 << 8) * 65535u / (127u << 8));
    sp = gp_scale(n, (uint32_t)e->sprd * 516u);
    pos = (int32_t)c + (int32_t)gp_scale(sp, r2 & 0xFFFFu) - (int32_t)(sp >> 1);
    pos = rev ? clamp(pos, (int32_t)span, (int32_t)n - 2) : clamp(pos, 0, (int32_t)(n - 3u - span));
    g->step = step;
    g->frac = 0;
    g->wph = 0;
    g->winc = 0xFFFFFFFFu / len;
    g->left = len;
    g->rev = (uint8_t)rev;
    ov = len / (iv ? iv : 1u);
    g->gain = GP_NORM[ov < 1u ? 1u : ov > GP_NG ? GP_NG : ov];
    g->owner = (uint8_t)(vi + 1u);
    g->a = gp_mix(A, B, (uint32_t)pos, e->morph);
    g->b = gp_mix(A, B, (uint32_t)pos + 1u, e->morph);
    g->pos = rev ? (uint32_t)pos : (uint32_t)pos + 2u;
}

/* 32768 / n rounded down, n = 0 .. 2 CTL + 1 (the fetches a grain makes in a block at most): MORPH's step a fetch */
static const uint16_t GP_RCP[2 * CTL + 2] = {
    0, 32768, 16384, 10922, 8192, 6553, 5461, 4681, 4096, 3640, 3276, 2978, 2730, 2520, 2340, 2184, 2048, 1927, 1820,
    1724, 1638, 1560, 1489, 1424, 1365, 1310, 1260, 1213, 1170, 1129, 1092, 1057, 1024, 992, 963, 936, 910, 885, 862,
    840, 819, 799, 780, 762, 744, 728, 712, 697, 682, 668, 655, 642, 630, 618, 606, 595, 585, 574, 564, 555, 546, 537,
    528, 520, 512, 504
};
_Static_assert(GP_STEP_MAX == 2u << 16 && CTL == 32u, "GP_RCP's length");
/* one grain into acc over n samples, MORPH going from m0 to m1 over them (Q15); returns 0 when it ended */
static int gp_run(gp_grain_t *g, int32_t *acc, uint32_t n, int32_t m0, int32_t m1, const int16_t *A, const int16_t *B)
{
    uint32_t m = n < g->left ? n : g->left, step = g->step, pos = g->pos, nf;
    int32_t a = g->a, b = g->b, frac = g->frac, w0, w1, wq, dq, mq = m0, dmq, *o = acc, *oe;
    w0 = mulq15(gp_win(g->wph), g->gain);
    g->wph += g->winc * m;
    g->left -= m;
    w1 = g->left ? mulq15(gp_win(g->wph), g->gain) : 0;
    wq = w0 * 1024;
    dq = m == CTL ? ((w1 - w0) * 1024) >> CTL_LOG2 : m ? (w1 - w0) * 1024 / (int32_t)m : 0;
    nf = g->rev ? (m * step + 65535u - (uint32_t)frac) >> 16 : ((uint32_t)frac + m * step) >> 16;   /* the fetches */
    dmq = ((m1 - m0) * GP_RCP[nf < 2u * CTL + 2u ? nf : 2u * CTL + 1u]) >> 15;
    oe = acc + m;
    if (!g->rev) {
        const int16_t *pa = A + pos, *pb = B + pos, *pe = A + GS_N;
        for (; o < oe; o++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            *o += (s * (wq >> 10)) >> 15;
            wq += dq;
            frac += (int32_t)step;
            while (frac >= 65536) {
                frac -= 65536;
                a = b;
                if (pa < pe) {
                    int32_t x = *pa++;
                    mq += dmq;
                    b = x + (((*pb++ - x) * mq) >> 15);
                }
            }
        }
        pos = (uint32_t)(pa - A);
    } else {
        const int16_t *pa = A + pos, *pb = B + pos;
        for (; o < oe; o++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            *o += (s * (wq >> 10)) >> 15;
            wq += dq;
            frac -= (int32_t)step;
            while (frac < 0) {
                frac += 65536;
                b = a;
                if (pa > A) {
                    int32_t x = *--pa;
                    mq += dmq;
                    a = x + (((*--pb - x) * mq) >> 15);
                }
            }
        }
        pos = (uint32_t)(pa - A);
    }
    g->pos = pos;
    g->a = a;
    g->b = b;
    g->frac = frac;
    if (!g->left)
        g->owner = 0;
    return g->left != 0;
}

/* SCAN: a round of the buffer in 110 s (|SCAN| 1) .. 0.47 s (64), halving every 8; the phase a block; 0 off */
static int32_t gp_scan_inc(int32_t scan)
{
    int32_t s = scan < 0 ? -scan : scan, inc;
    if (!s)
        return 0;
    inc = (int32_t)(((25972u >> 4) * (pow2_q16(s * 24) >> 4)) >> 8);   /* 2^32 x 32 / (44100 x 120 s) x 2^(s / 8) */
    return scan < 0 ? -inc : inc;
}
/* an envelope time 2 ms .. 3.1 s as the amplitude's step a block, Q15 */
static int32_t gp_env_step(int32_t p)
{
    uint32_t n = gp_env_ms(p) * 441u / 10u;
    return (int32_t)(32767u * CTL / (n ? n : 1u)) + 1;
}

static void gp_kill(uint32_t vi)                  /* the grains of voice vi go */
{
    uint32_t i;
    for (i = 0; i < GP_NG; i++)
        if (gpg[i].owner == vi + 1u)
            gpg[i].owner = 0;
}

/* ---- the audio side: n stereo frames added to out */
static void gp_audio(int32_t *out, uint32_t n)
{
    const int16_t *A = gbuf[gsel[0]], *B = gbuf[gsel[1]];
    int32_t acc[CTL], mono[CTL], atk = gp_env_step(gp_p[GQ_ATK]), rel = gp_env_step(gp_p[GQ_REL]);
    int32_t kd = 8192 - gp_p[GQ_RES] * 60, scan = gp_scan_inc(gp_p[GQ_SCAN]);
    int32_t ldep = gp_p[GQ_LDEPTH], ldst = gp_p[GQ_LDEST], lwave = gp_p[GQ_LWAVE];
    uint32_t b, i, v, k, nact = 0, iv, share, linc = gl_hz100(gp_p[GQ_LRATE]) * 31166u;   /* a block's turn of the LFO:
                                                  * 2^32 x 32 / (44100 x 100) a hundredth of a hertz */
    gp_eff_t e;
    tsvf_t c;
    e.rnd = gp_p[GQ_RAND];
    for (v = 0; v < GP_NV; v++)
        nact += gpv[v].gate || gpv[v].env;
    share = nact > 1u ? (GP_NG / nact > 2u ? GP_NG / nact : 2u) : GP_NG;
    for (b = 0; b < n; b += CTL) {
        uint32_t m = n - b < CTL ? n - b : CTL, old = gp_lph;
        int32_t lm = 0, lpit;
        gp_lph += linc;                         /* the LFO: one value a block, +-63 knob steps at full DEPTH */
        if (gp_lph < old)
            gp_lsh = (int32_t)(noise32(&gp_rng) >> 17) * 2 - 32767;
        if (ldep) {
            int32_t x;
            switch (lwave) {
            case 1: x = osc_tri(gp_lph); break;
            case 2: x = (int32_t)(gp_lph >> 16) - 32768; break;
            case 3: x = gp_lph < 0x80000000u ? 32767 : -32767; break;
            case 4: x = gp_lsh; break;
            default: x = sine_i(gp_lph); break;
            }
            lm = (x * ldep) >> 16;
        }
        e.size = clamp(gp_p[GQ_SIZE] + (ldst == GD_SIZE ? lm : 0), 0, 127);
        e.dens = clamp(gp_p[GQ_DENS] + (ldst == GD_DENS ? lm : 0), 0, 127);
        e.sprd = clamp(gp_p[GQ_SPRD] + (ldst == GD_SPRD ? lm : 0), 0, 127);
        e.morph = ((clamp(gp_p[GQ_MORPH] + (ldst == GD_MORPH ? lm : 0), 0, 127) << 8) * 16513 + 8192) >> 14;   /* B: 32768 */
        lpit = ldst == GD_PITCH ? lm * 3 : 0;  /* +-12 st */
        iv = (44100u * pow2_q16(-e.dens * 10)) >> 16;       /* the next grain: DENS a second, 1 .. 98 /s */
        tsvf_coef_k(&c, (gp_p[GQ_CUT] + (ldst == GD_CUT ? lm : 0)) * 256, kd);
        gp_scan += (uint32_t)scan;
        e.pos_q8 = (int32_t)(((uint32_t)clamp((gp_p[GQ_POS] + (ldst == GD_POS ? lm : 0)) * 256, 0, 127 << 8)
                              + (gp_scan >> 17)) % (127u * 256u + 1u));
        gp_pos_view = e.pos_q8;
        for (i = 0; i < m; i++)
            mono[i] = 0;
        for (v = 0; v < GP_NV; v++) {
            gp_voice_t *V = &gpv[v];
            int32_t e0 = V->env, e1, de, ic1 = V->ic1, ic2 = V->ic2, m0;
            uint32_t mine = 0;
            if (!V->gate && !V->env)
                continue;
            e1 = V->gate ? (e0 + atk > 32767 ? 32767 : e0 + atk) : (e0 - rel < 0 ? 0 : e0 - rel);
            V->env = e1;
            de = (e1 - e0) / (int32_t)m;
            m0 = V->morph;
            V->morph = e.morph;
            e.pitch16 = (int32_t)V->note * 16 + gp_p[GQ_PITCH] * 16 + lpit;
            for (i = 0; i < m; i++)
                acc[i] = 0;
            for (k = 0; k < GP_NG; k++)
                if (gpg[k].owner == v + 1u)
                    mine += (uint32_t)gp_run(&gpg[k], acc, m, m0, e.morph, A, B);
            V->next -= (int32_t)m;
            if (V->next <= 0) {                          /* (in the release too: the cloud fades with the envelope) */
                uint32_t r = noise32(&gp_rng);
                int32_t j = (((int32_t)(r & 0xFFFFu) - 32768) * e.sprd) / 254;   /* +-SPRD / 2, Q15 */
                V->next += (int32_t)iv + mulq15((int32_t)iv, j);
                if (V->next < 0)
                    V->next = 0;
                if (mine < share)
                    gp_spawn(v, iv, &e, A, B);
            }
            for (i = 0; i < m; i++) {
                int32_t x = clamp(acc[i], -65535, 65535) >> 1, y = tsvf_lp(&c, x, &ic1, &ic2);
                mono[i] += mulq15(mulq15(clamp(y * 2, -32767, 32767), e0), V->lvl);
                e0 += de;
            }
            V->ic1 = ic1;
            V->ic2 = ic2;
            if (!V->gate && !V->env)                     /* the release ended */
                gp_kill(v);
        }
        for (i = 0; i < m; i++) {
            out[2u * (b + i)] += mono[i];
            out[2u * (b + i) + 1u] += mono[i];
        }
    }
}

/* ---- notes, from the main loop */
static void gp_note_on(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    if (note > 127u || !vel)
        return;
    for (v = 0; v < GP_NV; v++) {               /* the same note, else a free voice, else the quietest, else the oldest */
        uint32_t score = gpv[v].note == note && gpv[v].gate ? 0u
                       : gpv[v].gate ? 0x80000000u + gpv[v].age : (uint32_t)gpv[v].env + 1u;
        if (score < best) { best = score; pick = v; }
    }
    fm1_irq_off();
    if (!gpv[pick].env) {                       /* a fresh voice: no grains from before, the filter clear */
        gp_kill(pick);
        gpv[pick].ic1 = gpv[pick].ic2 = 0;
        gpv[pick].morph = ((gp_p[GQ_MORPH] << 8) * 16513 + 8192) >> 14;
    }
    gpv[pick].note = (uint8_t)note;
    gpv[pick].age = ++gp_ages;
    gpv[pick].lvl = (int32_t)(vel * GP_VOICE / 127u);
    gpv[pick].next = 0;                         /* the first grain at once */
    gpv[pick].gate = 1;
    fm1_irq_on();
}
static void gp_note_off(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < GP_NV; v++)
        if (gpv[v].note == note)
            gpv[v].gate = 0;
}
static int gp_quiet(void)                         /* nothing sounds, not even a release */
{
    uint32_t v;
    for (v = 0; v < GP_NV; v++)
        if (gpv[v].gate || gpv[v].env) return 0;
    return 1;
}

/* ---- the renders, from the main loop: a source whose knobs changed renders into the free buffer, a slice a call;
 * whole, it takes the source's place (the grains read the new material from their next block). Latest wins at the
 * end of a render: knobs turned meanwhile render next */
static const uint16_t GRAIN_SLICE_N[GS_NTYPE] = {1024, 256, 512, 512, 512, 256, 256, 512};   /* a slice: under ~1 ms */
static struct {
    gs_t g;
    uint8_t busy, which, last;
    uint8_t cur[2][4];           /* the knobs each source's buffer holds; 0xFF: nothing yet */
    uint32_t renders;            /* renders landed (the screen redraws the wave) */
} grs = {.cur = {{0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}}};

static int grain_poll(void)                       /* 1 = a render is under way */
{
    uint32_t k, tgt;
    if (!grs.busy) {
        int changed[2];
        for (k = 0; k < 2u; k++) {
            uint32_t q = k ? GQ_TYPE_B : GQ_TYPE_A, j;
            changed[k] = 0;
            for (j = 0; j < 4u; j++)
                changed[k] |= grs.cur[k][j] != (uint8_t)gp_p[q + j];
        }
        if (!changed[0] && !changed[1])
            return 0;
        k = changed[0] && changed[1] ? 1u - grs.last : changed[1];   /* both: the one not rendered last goes first */
        for (tgt = 0; tgt < 4u; tgt++)
            grs.cur[k][tgt] = (uint8_t)gp_p[(k ? GQ_TYPE_B : GQ_TYPE_A) + tgt];
        gs_begin(&grs.g, grs.cur[k][0], grs.cur[k][1], grs.cur[k][2], grs.cur[k][3]);
        grs.which = (uint8_t)k;
        grs.busy = 1;
    }
    k = grs.which;
    tgt = 3u - gsel[0] - gsel[1];                 /* the buffer no source reads */
    if (gs_step(&grs.g, gbuf[tgt], GRAIN_SLICE_N[grs.g.type]) >= GS_N) {
        gs_finish(gbuf[tgt]);
        if (!k)
            groot16 = grs.g.root16;
        gsel[k] = (uint8_t)tgt;
        grs.last = (uint8_t)k;
        grs.busy = 0;
        grs.renders++;
    }
    return 1;
}
