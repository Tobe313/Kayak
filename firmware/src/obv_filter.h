/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 zvenson (OberVave: https://github.com/zvenson/obervave, firmware/src/obv_dsp.h) */
/* OberVave's voice filter, taken into Kayak: an input stage (DRIVE's gain into a tanh), then a trapezoidal SVF
 * whose band-pass state saturates softly (LP12 HP12 BP12 NOTCH, and SEM: LP .. notch .. HP by one knob) or a 24 dB
 * ladder with a tanh in its loop (LP24), then a soft knee. Cutoff in Q8 semitones (0 = MIDI 0, 8.2 Hz .. 136, capped
 * at 0.45 FS): OBV_SVF_G (firmware/gen/grain_tables.h). The code is OberVave's, with Kayak's clamp() for its
 * clampi() and grain_dsp.h's softclip(), the same function. */
#pragma once

enum { F_LP12, F_HP12, F_BP12, F_NOTCH, F_LP24, F_SEM, NFTYPE };   /* SEM: LP .. notch .. HP by MORPH */

/* a soft knee: linear to +-32768, tanh above, +-65535 at most */
static inline __attribute__((always_inline)) int32_t sat_knee(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    if (a <= 32768) return x;
    a = 32768 + softclip(a - 32768);
    return x < 0 ? -a : a;
}
/* tanh(x / 98304) * 98301: the filter's input stage (unity below ~30000) */
static inline __attribute__((always_inline)) int32_t sat_in(int32_t x) { return softclip((x * 85) >> 8) * 3; }
/* tanh(x / 65536) * 65534: the ladder's loop */
static inline __attribute__((always_inline)) int32_t sat_tanh(int32_t x) { return softclip(x >> 1) << 1; }

/* ---- trapezoidal state-variable filter (A. Simper), coefficients per block in Q13 */
typedef struct { int32_t a1, a2, a3, k; } svf_coef_t;
typedef struct { int32_t ic1, ic2; } svf_state_t;

/* cut: Q8 semitones (0..136 << 8); k: damping Q12 (2.0 = 8192; ~0 self-oscillates) */
static inline void svf_coef(svf_coef_t *c, int32_t cut, int32_t k)
{
    int32_t i, g, den;
    cut = clamp(cut, 0, 136 << 8);
    i = cut >> 8;
    g = OBV_SVF_G[i];
    if (i < 136)
        g += ((OBV_SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    if (den < 64) den = 64;
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
    c->k = k;
}

/* one sample; *lp, *bp, *hp out. The band-pass state passes a soft saturation: resonance stays warm and bounded
 * (self-oscillation settles instead of running away). Signals within +-150000 keep the products in 32 bits. */
static inline __attribute__((always_inline)) void svf_step(const svf_coef_t *c, svf_state_t *s, int32_t in, int32_t *lp, int32_t *bp, int32_t *hp)
{
    int32_t v3 = in - s->ic2;
    int32_t v1 = (c->a1 * s->ic1 + c->a2 * v3) >> 13;
    int32_t v2 = s->ic2 + ((c->a2 * s->ic1 + c->a3 * v3) >> 13);
    int32_t n1 = 2 * v1 - s->ic1;
    s->ic1 = (uint32_t)(n1 + 24576) < 49152u ? n1 : softclip(n1 >> 1) << 1;   /* linear below +-0.75, then soft */
    s->ic2 = 2 * v2 - s->ic2;
    *lp = v2;
    *bp = v1;
    *hp = in - ((c->k * v1) >> 12) - v2;
}

/* ---- the 24 dB ladder: four trapezoidal one-poles with the resonance fed back around all four, a tanh in the loop
 * so the resonance compresses and self-oscillation stays round, the input raised with the resonance (1 + k/2) so the
 * bass does not fall away. The feedback is solved for the linear loop (zero delay), the tanh then sits on the first
 * stage's input */
typedef struct { int32_t G, G2, G3, G4, k, inv, gin; } lad_coef_t;   /* G.. Q12, k Q10 (4.0 = 4096 self-oscillates), inv gin Q12 */
typedef struct { int32_t s[4]; } lad_state_t;

static inline void lad_coef(lad_coef_t *c, int32_t cut, int32_t k10)
{
    int32_t i, g, G;
    cut = clamp(cut, 0, 136 << 8);
    i = cut >> 8;
    g = OBV_SVF_G[i];
    if (i < 136)
        g += ((OBV_SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    G = (int32_t)(((uint32_t)g << 12) / (uint32_t)(4096 + g));
    c->G = G;
    c->G2 = (G * G) >> 12;
    c->G3 = (c->G2 * G) >> 12;
    c->G4 = (c->G3 * G) >> 12;
    c->k = k10;
    c->inv = (int32_t)((4096u << 12) / (uint32_t)(4096 + ((k10 * c->G4) >> 10)));
    c->gin = 4096 + 2 * k10;                   /* 1 + k/2: the bass stays, the loop is pushed harder as the resonance rises */
}

static inline __attribute__((always_inline)) int32_t lad_step(const lad_coef_t *c, lad_state_t *s, int32_t in)
{
    int32_t G = c->G, *st = s->s, v, y, u;
    int32_t S1 = st[0] - ((G * st[0]) >> 12), S2 = st[1] - ((G * st[1]) >> 12);
    int32_t S3 = st[2] - ((G * st[2]) >> 12), S4 = st[3] - ((G * st[3]) >> 12);
    int32_t x = (in * c->gin) >> 12;
    int32_t acc = ((c->G3 * S1 + c->G2 * S2 + G * S3) >> 12) + S4;
    int32_t y4 = (int32_t)(((int64_t)(((c->G4 * x) >> 12) + acc) * c->inv) >> 12);   /* the loop's output, linear */
    u = sat_tanh(x - ((c->k * clamp(y4, -131072, 131071)) >> 10));
    v = ((u - st[0]) * G) >> 12; y = v + st[0]; st[0] = y + v;
    v = ((y - st[1]) * G) >> 12; y = v + st[1]; st[1] = y + v;
    v = ((y - st[2]) * G) >> 12; y = v + st[2]; st[2] = y + v;
    v = ((y - st[3]) * G) >> 12; y = v + st[3]; st[3] = y + v;
    return y;
}

/* a voice's filter as Kayak runs it: the coefficients of one type, the state of both */
typedef struct { svf_state_t f; lad_state_t lad; } vflt_state_t;
typedef struct { svf_coef_t c; lad_coef_t lc; int32_t ft, morph, drv; } vflt_t;
/* cut Q8 semitones, res 0..127, morph 0..127 (SEM), drv Q12 (1 .. 16) */
static inline void vflt_set(vflt_t *f, int32_t ft, int32_t cut, int32_t res, int32_t morph, int32_t drv)
{
    int32_t reso_k = 8192 - res * 8400 / 127;
    f->ft = ft;
    f->morph = morph;
    f->drv = drv;
    if (ft == F_LP24) lad_coef(&f->lc, cut, (8192 - reso_k) >> 1);
    else svf_coef(&f->c, cut, reso_k);
}
static inline __attribute__((always_inline)) int32_t vflt_step(const vflt_t *f, vflt_state_t *s, int32_t x)
{
    int32_t lp, bp, hp, y;
    x = sat_in(((x >> 3) * f->drv) >> 9);       /* the input stage: DRIVE's gain, then a tanh (never a hard clip) */
    if (f->ft == F_LP24)
        y = lad_step(&f->lc, &s->lad, x);
    else {
        svf_step(&f->c, &s->f, x, &lp, &bp, &hp);
        switch (f->ft) {
        case F_HP12: y = hp; break;
        case F_BP12: y = bp; break;
        case F_NOTCH: y = hp + lp; break;
        case F_SEM: y = lp + (((hp - lp) * f->morph) >> 7); break;   /* the SEM's LP .. notch .. HP pot */
        default: y = lp; break;
        }
    }
    return sat_knee(y);
}
