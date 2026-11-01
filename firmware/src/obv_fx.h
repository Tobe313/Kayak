/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 zvenson (OberVave: https://github.com/zvenson/obervave, firmware/src/obv_fx.h) */
/* OberVave's effects, taken into Kayak: a ping-pong delay, a plate reverb (J. Dattorro's figure-of-eight tank, 1997)
 * and a parallel drive (OVERDRIVE DIST FUZZ CRUSH); OberVave's chorus is replaced by Kayak's stereo phaser (1.1).
 * All lines are int16 in one .pool buffer; the signals are Q15. Integers only. Kayak's changes: the knobs are its
 * own (fx_block takes them as values 0..127), the delay's time is in milliseconds (20 .. 550: Kayak has no tempo) and its line 557 ms a side
 * (OberVave's 743 ms with the grains' buffers would not fit the pool), the send of every effect is the dry sound. */
#define REV_DIV 2u                               /* (1.3) the reverb at half the rate, as OberVave's: half its time */
#define RVL(x) ((uint32_t)((x) * 44100u / 29761u / REV_DIV))   /* the paper's 29761 Hz lengths at our rate */
#define DLY_MAX 24576u                          /* a side of the delay: 557 ms */
#define REV_PRE_N (8192u / REV_DIV)             /* up to 120 ms of pre-delay */
#define RV_IN1 RVL(142)
#define RV_IN2 RVL(107)
#define RV_IN3 RVL(379)
#define RV_IN4 RVL(277)
#define RV_AP1 RVL(672)
#define RV_D1 RVL(4453)
#define RV_AP2 RVL(1800)
#define RV_D2 RVL(3720)
#define RV_AP3 RVL(908)
#define RV_D3 RVL(4217)
#define RV_AP4 RVL(2656)
#define RV_D4 RVL(3163)
#define REV_N (REV_PRE_N + RV_IN1 + RV_IN2 + RV_IN3 + RV_IN4 + RV_AP1 + 16u + RV_D1 + RV_AP2 + RV_D2 + RV_AP3 + 16u + RV_D3 + RV_AP4 + RV_D4)
#define RV_BW (REV_DIV == 1u ? 16700 : 24900)   /* the input's one-pole: ~5 kHz at either rate */
#define FX_MEM_N (2u * DLY_MAX + REV_N)

typedef struct { int16_t *b; uint32_t len, w; } line_t;   /* w: where the next sample goes */
static int16_t fx_mem[FX_MEM_N] __attribute__((section(".pool")));
static uint32_t fx_used;

static inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }
static void line_init(line_t *l, uint32_t len) { l->b = fx_mem + fx_used; l->len = len; l->w = 0; fx_used += len; }
static inline __attribute__((always_inline)) int32_t line_read(const line_t *l, uint32_t back)   /* 1 <= back <= len */
{
    uint32_t i = l->w + l->len - back;
    if (i >= l->len) i -= l->len;
    return l->b[i];
}
static inline __attribute__((always_inline)) void line_write(line_t *l, int32_t v)
{
    l->b[l->w] = (int16_t)sat16(v);
    if (++l->w >= l->len) l->w = 0;
}
/* Dattorro's allpass: the line holds the inner node, the output is tapped from it. g Q15, signed */
static inline __attribute__((always_inline)) int32_t ap_step(line_t *l, int32_t in, int32_t g)
{
    int32_t d = line_read(l, l->len), v = in - ((d * g) >> 15);
    line_write(l, v);
    return d + ((v * g) >> 15);
}
/* the same with the delay swaying by e (Q8 samples, |e| <= 8 << 8) around base; the line is base + 16 long */
static inline __attribute__((always_inline)) int32_t ap_mod(line_t *l, int32_t in, int32_t g, uint32_t base, int32_t e)
{
    uint32_t back = (uint32_t)((int32_t)base + (e >> 8));
    int32_t f = e & 255, a = line_read(l, back), b = line_read(l, back + 1u), d = a + (((b - a) * f) >> 8), v;
    v = in - ((d * g) >> 15);
    line_write(l, v);
    return d + ((v * g) >> 15);
}
static inline int32_t tri_q(uint32_t ph)        /* a triangle -2048..2047 (so Q8 = +-8 samples) */
{
    int32_t t = (int32_t)(ph >> 19);            /* 0..8191 */
    return t < 4096 ? t - 2048 : 6143 - t;
}

/* ---- the phaser (Kayak): two chains of PH_N first-order allpasses, left and right, their corners swept together by
 * a triangle LFO a quarter turn apart (160 Hz .. 5 kHz by DEPTH), the last stage fed back into the first (FDBK); its
 * return added to the dry sound makes the moving notches. It runs at half the rate (1.3: half its time) */
#define PH_N 6u
static uint32_t pow2_q16(int32_t d16);          /* (grain.c) */
static int32_t ph_x1[2][PH_N], ph_y1[2][PH_N], ph_last[2], ph_a[2];
static uint32_t ph_ph, ph_inc;
static int32_t ph_depth_k, ph_fb, ph_mix, ph_out[2];
static int32_t ph_coef(uint32_t ph)             /* the allpasses' coefficient, Q15, for the LFO at ph */
{
    int32_t d16 = ((tri_q(ph) + 2048) * ph_depth_k) >> 12;   /* 1/16 semitones over 160 Hz */
    uint32_t fc = (160u * pow2_q16(d16)) >> 16, w, tn;
    if (fc > 5000u) fc = 5000u;
    w = fc * 9338u / 1000u;                     /* pi fc / (fs / 2), Q16 */
    tn = w + (((w * w) >> 16) * w >> 16) / 3u;  /* ~tan */
    return (int32_t)(((65536u - tn) << 15) / (65536u + tn));
}
static inline __attribute__((always_inline)) int32_t ph_run(uint32_t c, int32_t x)
{
    uint32_t k;
    int32_t a = ph_a[c];
    x = sat16(x + ((ph_last[c] * ph_fb + 16384) >> 15));
    for (k = 0; k < PH_N; k++) {                /* y = a (y1 - x) + x1, rounded: a truncation would leave a DC */
        int32_t y = sat16(((a * (ph_y1[c][k] - x) + 16384) >> 15) + ph_x1[c][k]);
        ph_x1[c][k] = x;
        ph_y1[c][k] = y;
        x = y;
    }
    ph_last[c] = x;
    return x;
}

/* ---- the state */
static line_t dl, dr, rv_pre, rv_in[4], rv_ap1, rv_d1, rv_ap2, rv_d2, rv_ap3, rv_d3, rv_ap4, rv_d4;
static uint32_t rv_ph1, rv_ph2;
static int32_t dly_mix, rev_mix, dly_fb, dly_tone_k, rev_decay, rev_damp_k;
static int32_t dly_time, dly_cur;               /* Q16 samples: the target and where the read is (it slews like a tape) */
static int32_t dly_lpl, dly_lpr, rv_lp_in, rv_lpl, rv_lpr, rv_yl, rv_yr;
static uint32_t rev_pre_n = 1, dly_active, rev_active;

static void fx_init(void)
{
    line_init(&dl, DLY_MAX);
    line_init(&dr, DLY_MAX);
    line_init(&rv_pre, REV_PRE_N);
    line_init(&rv_in[0], RV_IN1); line_init(&rv_in[1], RV_IN2); line_init(&rv_in[2], RV_IN3); line_init(&rv_in[3], RV_IN4);
    line_init(&rv_ap1, RV_AP1 + 16u); line_init(&rv_d1, RV_D1); line_init(&rv_ap2, RV_AP2); line_init(&rv_d2, RV_D2);
    line_init(&rv_ap3, RV_AP3 + 16u); line_init(&rv_d3, RV_D3); line_init(&rv_ap4, RV_AP4); line_init(&rv_d4, RV_D4);
    dly_cur = dly_time = 11025 << 16;
}

/* the knobs' numbers, once per ISR call of n frames (values 0..127; dly_ms the delay's time) */
static void fx_block(int32_t ph_rate, int32_t ph_depth, int32_t ph_fbk, int32_t ph_m, int32_t dly_ms, int32_t dly_fbk,
                     int32_t dly_tone, int32_t dly_m, int32_t rev_size, int32_t rev_damp, int32_t rev_pre, int32_t rev_m,
                     uint32_t n)
{
    int32_t d, t;
    uint32_t samples;
    d = ph_rate; ph_inc = (uint32_t)(5 + ((d * d * 395) >> 14)) * 974u;   /* 0.05 .. 4 Hz */
    ph_depth_k = ph_depth * 9;                  /* the sweep: up to 1143 / 192 = 6 octaves */
    ph_fb = ph_fbk * 220;                       /* up to 0.85 */
    ph_mix = ph_m * 2;                          /* Q8: up to the dry's own level, the deepest notches */
    dly_mix = (dly_m * 180) >> 7;               /* wet up to 0.7 of the dry (Q8): the sum stays in range */
    rev_mix = (rev_m * 180) >> 7;
    dly_fb = dly_fbk * 250;                     /* up to 0.97 */
    t = dly_tone; dly_tone_k = clamp(((t * t) >> 6) + 8, 8, 256);
    d = rev_size; rev_decay = 12000 + ((d * d * 19500) >> 14);   /* 0.37 .. 0.96: the top is a near-freeze */
    d = rev_damp; rev_damp_k = 32768 - d * d * 2;   /* the one-pole's k at the half rate */
    if (REV_DIV == 1u) {                        /* the same corner at the full rate: k' = 1 - sqrt(1 - k) */
        uint32_t x = (uint32_t)(32768 - rev_damp_k) << 15, r = 0, bit = 1u << 15;
        for (; bit; bit >>= 1)
            if ((r + bit) * (r + bit) <= x) r += bit;
        rev_damp_k = 32768 - (int32_t)r;
    }
    rev_pre_n = 1u + (uint32_t)rev_pre * (41u / REV_DIV);
    samples = (uint32_t)dly_ms * 441u / 10u;
    if (samples > DLY_MAX - 4u) samples = DLY_MAX - 4u;
    if (samples < 64u) samples = 64u;
    dly_time = (int32_t)(samples << 16);
    if (dly_mix) dly_active = 12u * FS; else if (dly_active > n) dly_active -= n; else dly_active = 0;
    if (rev_mix) rev_active = 24u * FS; else if (rev_active > n) rev_active -= n; else rev_active = 0;
}

static inline __attribute__((always_inline)) int32_t line_read_q16(const line_t *l, int32_t back_q16)
{
    uint32_t b = (uint32_t)back_q16 >> 16;
    int32_t f = (back_q16 >> 8) & 255, a = line_read(l, b), c = line_read(l, b + 1u);
    return a + (((c - a) * f) >> 8);
}
/* 4-point Hermite for a tap that is always moving: no treble loss between the samples */
static inline __attribute__((always_inline)) int32_t line_read_q16_h(const line_t *l, int32_t back_q16)
{
    uint32_t b = (uint32_t)back_q16 >> 16;
    int32_t f = (back_q16 >> 8) & 255, p0 = line_read(l, b - 1u), p1 = line_read(l, b), p2 = line_read(l, b + 1u), p3 = line_read(l, b + 2u);
    int32_t c1 = (p2 - p0) >> 1, c2 = p0 - ((5 * p1) >> 1) + 2 * p2 - (p3 >> 1), c3 = ((p3 - p0) >> 1) + ((3 * (p1 - p2)) >> 1);
    return p1 + ((f * (c1 + ((f * (c2 + ((f * c3) >> 8))) >> 8))) >> 8);
}

/* one reverb sample: in -> pre-delay -> bandwidth -> 4 diffusers -> the two tank halves */
static inline void rev_tick(int32_t in)
{
    int32_t x, a, b, e1 = tri_q(rv_ph1), e2 = tri_q(rv_ph2), dec = rev_decay, dk = rev_damp_k;
    rv_ph1 += 97391u * REV_DIV;                 /* ~1.0 Hz and ~0.7 Hz: the tank's sway against metallic rings */
    rv_ph2 += 68174u * REV_DIV;
    line_write(&rv_pre, in);
    x = line_read(&rv_pre, rev_pre_n);
    rv_lp_in += ((x - rv_lp_in) * RV_BW) >> 15;
    x = ap_step(&rv_in[0], rv_lp_in, 24576);
    x = ap_step(&rv_in[1], x, 24576);
    x = ap_step(&rv_in[2], x, 20480);
    x = ap_step(&rv_in[3], x, 20480);
    /* left half, fed by the right's end */
    a = x + ((line_read(&rv_d4, RV_D4) * dec) >> 15);
    a = ap_mod(&rv_ap1, a, -22938, RV_AP1, e1);
    line_write(&rv_d1, a);
    b = line_read(&rv_d1, RV_D1);
    rv_lpl += ((b - rv_lpl) * dk) >> 15;
    b = ap_step(&rv_ap2, (rv_lpl * dec) >> 15, 16384);
    line_write(&rv_d2, b);
    /* right half, fed by the left's end */
    a = x + ((line_read(&rv_d2, RV_D2) * dec) >> 15);
    a = ap_mod(&rv_ap3, a, -22938, RV_AP3, e2);
    line_write(&rv_d3, a);
    b = line_read(&rv_d3, RV_D3);
    rv_lpr += ((b - rv_lpr) * dk) >> 15;
    b = ap_step(&rv_ap4, (rv_lpr * dec) >> 15, 16384);
    line_write(&rv_d4, b);
    /* the output taps (the paper's), x 0.6 */
    a = line_read(&rv_d3, RVL(266)) + line_read(&rv_d3, RVL(2974)) - line_read(&rv_ap4, RVL(1913)) + line_read(&rv_d4, RVL(1996))
      - line_read(&rv_d1, RVL(1990)) - line_read(&rv_ap2, RVL(187)) - line_read(&rv_d2, RVL(1066));
    b = line_read(&rv_d1, RVL(353)) + line_read(&rv_d1, RVL(3627)) - line_read(&rv_ap2, RVL(1228)) + line_read(&rv_d2, RVL(2673))
      - line_read(&rv_d3, RVL(2111)) - line_read(&rv_ap4, RVL(335)) - line_read(&rv_d4, RVL(121));
    rv_yl = (a * 19661) >> 15;
    rv_yr = (b * 19661) >> 15;
}

/* DRIVE: the dry sound distorted, its return beside it (parallel: the dry keeps its punch). Types OVERDRIVE (soft),
 * DIST (harder, uneven), FUZZ (square), CRUSH (fewer bits, held samples); TONE a low-pass after it, MIX the return.
 * Fixed point: tanh by the softclip table */
enum { DV_OVERDRIVE, DV_DIST, DV_FUZZ, DV_CRUSH, NDV };
static int32_t dv_lp, dv_hold;                  /* Q15 */
static uint32_t dv_n;
static void drive_process(int32_t *out, const int32_t *vs, uint32_t n, uint32_t ty, uint32_t amt, uint32_t tone, uint32_t mix)
{
    uint32_t i;
    int32_t pre = 256 + (int32_t)(24u * 256u * amt / 127u);   /* Q8: 1 .. 25 */
    int32_t k = 1311 + (int32_t)(31457u * tone * tone / 16129u);   /* Q15: 0.04 .. 1 */
    int32_t ret = (int32_t)(mix * 4000u / 127u);   /* (MIX full: the return about as loud as the dry sound) */
    if (!mix) return;
    ty %= NDV;
    for (i = 0; i < n; i++) {
        int32_t x = (vs[i] * pre) >> 9, y;      /* softclip(x) = tanh(the drive's x), Q15 */
        if (ty == DV_OVERDRIVE) y = softclip(x);
        else if (ty == DV_DIST) y = x > 0 ? softclip(x * 8 / 5) : (softclip(x * 7 / 10) * 4) / 5;
        else if (ty == DV_FUZZ) y = clamp(x * 40, -32767, 32767);   /* (20 x the drive's x, square beyond 1/20) */
        else {                                  /* CRUSH: 16 steps, a sample held for 2..12 */
            if (!dv_n--) { dv_n = 2u + amt * 10u / 127u; dv_hold = softclip(x) * 8 / 32768 * 4096; }
            y = dv_hold;
        }
        dv_lp += ((y - dv_lp) * k) >> 15;
        {
            int32_t v = (dv_lp * ret + 16384) >> 15;   /* (rounded: the one-pole's -1 it settles on stays silent) */
            out[2u * i] = sat16(out[2u * i] + v);
            out[2u * i + 1u] = sat16(out[2u * i + 1u] + v);
        }
    }
}

/* the three effects side by side, each fed by its send (Kayak: the dry sound); out gets their returns */
static void fx_process(int32_t *out, const int32_t *cs, const int32_t *ds, const int32_t *rs, uint32_t n)
{
    uint32_t i, do_dly = dly_active != 0, do_rev = rev_active != 0, quiet = 1;
    int32_t pm = ph_mix, dm = dly_mix, rm = rev_mix;
    for (i = 0; i < n && pm; i++) quiet &= cs[i] == 0;
    if (pm && quiet && ph_last[0] < 64 && ph_last[0] > -64 && ph_last[1] < 64 && ph_last[1] > -64) {
        uint32_t k;                             /* the phaser's send silent, what rings in it near nothing: let it go */
        for (k = 0; k < PH_N; k++) ph_x1[0][k] = ph_y1[0][k] = ph_x1[1][k] = ph_y1[1][k] = 0;
        ph_last[0] = ph_last[1] = 0;
    }
    for (i = 0; i < n; i++) {
        int32_t l = out[2u * i], r = out[2u * i + 1u];
        if (pm) {                               /* the phaser: a step every other frame (the frame on it: halfway from
                                                 * the last), the corners again every 16 frames */
            int32_t pl = ph_out[0], pr = ph_out[1];
            if (!(i & 1u)) {
                int32_t x = (cs[i] + cs[i + 1u < n ? i + 1u : i]) >> 1;
                if (!(i & 15u)) {
                    ph_a[0] = ph_coef(ph_ph);
                    ph_a[1] = ph_coef(ph_ph + 0x40000000u);
                }
                ph_ph += ph_inc * 2u;
                ph_out[0] = ph_run(0, x);
                ph_out[1] = ph_run(1, x);
                pl = (pl + ph_out[0]) >> 1;
                pr = (pr + ph_out[1]) >> 1;
            }
            l += (pl * pm) >> 8;
            r += (pr * pm) >> 8;
        }
        if (do_dly) {                           /* ping-pong: the input enters left, the feedback crosses */
            int32_t wl, wr;
            if (dly_cur < dly_time) { dly_cur += 1 << 15; if (dly_cur > dly_time) dly_cur = dly_time; }
            else if (dly_cur > dly_time) { dly_cur -= 1 << 15; if (dly_cur < dly_time) dly_cur = dly_time; }
            wl = line_read_q16(&dl, dly_cur);
            wr = line_read_q16(&dr, dly_cur);
            dly_lpl += ((wr - dly_lpl) * dly_tone_k) >> 8;
            dly_lpr += ((wl - dly_lpr) * dly_tone_k) >> 8;
            line_write(&dl, ds[i] + ((dly_lpl * dly_fb) >> 15));
            line_write(&dr, (dly_lpr * dly_fb) >> 15);
            l += (wl * dm) >> 8;
            r += (wr * dm) >> 8;
        }
        if (do_rev) {                           /* the reverb: a step every other frame, the frame on it halfway */
            int32_t yl = rv_yl, yr = rv_yr;
            if (!(i & 1u)) {
                rev_tick((rs[i] + rs[i + 1u < n ? i + 1u : i]) >> 1);
                yl = (yl + rv_yl) >> 1;
                yr = (yr + rv_yr) >> 1;
            }
            l += (yl * rm) >> 8;
            r += (yr * rm) >> 8;
        }
        out[2u * i] = sat16(l);
        out[2u * i + 1u] = sat16(r);
    }
}
