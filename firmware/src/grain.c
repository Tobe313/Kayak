/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca), Tobe313 (Kayak) */
/* Kayak's GRAIN: ChoralRoot FM-1's granular engine (https://github.com/Quixotic7/ChoralRootFM1, docs/GRAIN.md,
 * firmware/src/eng_grain.c), cut down to one part with its own knobs. What stays: the source generator unchanged
 * (grain_src.c: TYPE TONE MOTION SEED render a 743 ms buffer at 22.05 kHz, sources A and B), the grain player
 * (gp_spawn, gp_run: a pool of 16 grains shared by the voices, the Hann window, SIZE DENS POS SPRD RAND, the
 * 1/sqrt(overlap) level, every grain reading A and B mixed by MORPH, SCAN), a low-pass SVF a voice. What goes: the
 * matrix, the LFOs, ENV 2, the filter's types and drive, the presets and the patch blob, the wave band's colours.
 * What changes: a render goes to a third buffer and swaps in when it is whole (no render in place, no declick), in
 * slices from the main loop (grain_poll); the envelope and the LFO are Kayak's, the filter and the effects OberVave's.
 * (0.7) A second layer a voice beside the grains: an analog synth with its own filter, ADSR and LFO.
 *
 *   the audio side (app_audio, the interrupt): gp_audio
 *   the main loop: gp_note_on / gp_note_off, grain_poll (one slice a call), gp_p[] the knobs (one writer: the loop) */
#include "grain_dsp.h"
#include "grain_src.c"
#include "obv_filter.h"
#include "obv_fx.h"

#define GP_NV 8u                 /* voices */
#define GP_NG 16u                /* grains, shared by the voices */
#define CTL 32u                  /* a control block: the window's ramp, a grain start at most, the filter's coefficients */
#define CTL_LOG2 5
#define GP_STEP_MAX (2u << 16)   /* the rate's cap: two source samples an output sample (C1 .. C6 from one buffer) */
#define GP_RATE_Q16 ((GS_RATE << 16) / 44100u)
#define GP_VOICE 12000           /* a voice's level at full velocity */

/* the arpeggiator's modes and the note values (the arp's and the sequencer's: OberVave's) */
enum { A_OFF, A_UP, A_DOWN, A_UPDN, A_RND, A_PLAY, NARPMODE };
static const char *const ARP_NAMES[NARPMODE] = {"OFF", "UP", "DOWN", "UP/DN", "RANDOM", "PLAYED"};
enum { D_4, D_8, D_8T, D_16, D_16T, D_32, D_32T, NDIV };
static const char *const DIV_NAMES[NDIV] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
static const uint8_t DIV_PER_QUARTER[NDIV] = {1, 2, 3, 4, 6, 8, 12};

/* ---- the knobs (dinghy.c lays them out on pages). Two layers a voice, ALGORITHM picks which one the pages show: the
 * grains (their sources, the grain knobs, a filter, an ADSR, an LFO) and an analog synth (two detuned oscillators, a
 * sub and noise, its own filter, ADSR and LFO); then the layers' levels, the arpeggiator and the effects, for the
 * whole sound */
enum {
    GQ_TYPE_A, GQ_TONE_A, GQ_MOTION_A, GQ_SEED_A,   /* (a source's four knobs together: grain_poll reads them so) */
    GQ_TYPE_B, GQ_TONE_B, GQ_MOTION_B, GQ_SEED_B,
    GQ_SIZE, GQ_DENS, GQ_POS, GQ_SPRD,
    GQ_MORPH, GQ_RAND, GQ_PITCH, GQ_SCAN,
    GQ_FTYPE, GQ_CUT, GQ_RES, GQ_FDRIVE, GQ_KTRK, GQ_FMORPH,   /* OberVave's filter */
    GQ_ATK, GQ_DEC, GQ_SUS, GQ_REL,
    GQ_LRATE, GQ_LWAVE, GQ_LDEPTH, GQ_LDEST,
    GQ_OWAVE, GQ_ODET, GQ_OOCT, GQ_OPW, GQ_OSUB, GQ_ONOISE,       /* the analog layer */
    GQ_AFTYPE, GQ_ACUT, GQ_ARES, GQ_AFDRIVE, GQ_AKTRK, GQ_AFMORPH,
    GQ_AATK, GQ_ADEC, GQ_ASUS, GQ_AREL,
    GQ_ALRATE, GQ_ALWAVE, GQ_ALDEPTH, GQ_ALDEST,
    GQ_GLEV, GQ_ALEV,                            /* the layers' levels */
    GQ_AMODE, GQ_ADIV, GQ_AOCT, GQ_AGATE,        /* OberVave's arpeggiator (kayak_seq.h) */
    GQ_CRATE, GQ_CDEPTH, GQ_CMIX,                /* OberVave's effects */
    GQ_DTIME, GQ_DFB, GQ_DTONE, GQ_DMIX,
    GQ_RSIZE, GQ_RDAMP, GQ_RPRE, GQ_RMIX,
    GQ_VTYPE, GQ_VAMT, GQ_VTONE, GQ_VMIX,
    GQ_ARATCH, GQ_ACHANCE,                       /* (0.9) the arp's ratchet and chance: last, so a 0.8 sound keeps its
                                                 * order */
    GQ_N
};
#define GQ_N_V08 71u                            /* the knobs of a 0.8 sound (its user presets, its settings) */
/* how a value reads on the screen (gq_text) */
/* (labels and values of five characters at most: six of FONT_S fill a 60 px cell, they would touch the next) */
enum { GF_INT, GF_TYPE, GF_PCT, GF_SIGNED, GF_SEMI, GF_SIZE, GF_DENS, GF_CUT, GF_TIME, GF_LFOHZ, GF_LWAVE, GF_LDEST,
       GF_FTYPE, GF_VTYPE, GF_DLYMS, GF_ARP, GF_DIV, GF_CENTS, GF_OCT, GF_OWAVE, GF_ADEST };
#define GL_NWAVE 5u
static const char *const GL_WAVE_NAMES[GL_NWAVE] = {"SINE", "TRI", "SAW", "SQR", "S&H"};
enum { GD_MORPH, GD_POS, GD_SIZE, GD_DENS, GD_SPRD, GD_PITCH, GD_CUT, GD_N };
static const char *const GL_DEST_NAMES[GD_N] = {"MORPH", "POS", "SIZE", "DENS", "SPRD", "PITCH", "CUT"};
enum { AD_PITCH, AD_CUT, AD_LEVEL, AD_PW, AD_N };   /* the analog LFO's destinations */
static const char *const AD_NAMES[AD_N] = {"PITCH", "CUT", "LEVEL", "PW"};
enum { OW_SAW, OW_SQR, OW_TRI, NOW };          /* the analog oscillators' wave (SQR: a pulse of PW) */
static const char *const OW_NAMES[NOW] = {"SAW", "SQR", "TRI"};
static const char *const FTYPE_NAMES[NFTYPE] = {"LP12", "HP12", "BP12", "NOTCH", "LP24", "SEM"};
static const char *const VTYPE_NAMES[NDV] = {"OVER", "DIST", "FUZZ", "CRUSH"};
typedef struct { const char *name; int8_t min, max, def; uint8_t fmt; } gq_desc_t;
static const gq_desc_t GQ[GQ_N] = {
    {"TYPE", 0, GS_NTYPE - 1, GT_DRONE, GF_TYPE}, {"TONE", 0, 127, 64, GF_INT}, {"MOVE", 0, 127, 64, GF_INT},
    {"SEED", 0, 127, 0, GF_INT},
    {"TYPE", 0, GS_NTYPE - 1, GT_BELL, GF_TYPE}, {"TONE", 0, 127, 64, GF_INT}, {"MOVE", 0, 127, 64, GF_INT},
    {"SEED", 0, 127, 0, GF_INT},
    {"SIZE", 0, 127, 92, GF_SIZE}, {"DENS", 0, 127, 88, GF_DENS}, {"POS", 0, 127, 50, GF_PCT}, {"SPRD", 0, 127, 40, GF_PCT},
    {"MORPH", 0, 127, 40, GF_PCT}, {"RAND", 0, 127, 14, GF_PCT}, {"PITCH", -24, 24, 0, GF_SEMI},
    {"SCAN", -64, 63, 12, GF_SIGNED},
    {"TYPE", 0, NFTYPE - 1, F_LP12, GF_FTYPE}, {"CUT", 0, 127, 100, GF_CUT}, {"RES", 0, 127, 20, GF_PCT},
    {"DRIVE", 0, 127, 0, GF_PCT}, {"KTRK", 0, 127, 64, GF_PCT}, {"SEM", 0, 127, 64, GF_PCT},
    {"ATK", 0, 127, 30, GF_TIME}, {"DEC", 0, 127, 64, GF_TIME}, {"SUS", 0, 127, 127, GF_PCT}, {"REL", 0, 127, 84, GF_TIME},
    {"RATE", 0, 127, 40, GF_LFOHZ}, {"WAVE", 0, GL_NWAVE - 1, 0, GF_LWAVE}, {"DEPTH", 0, 127, 0, GF_PCT},
    {"DEST", 0, GD_N - 1, GD_MORPH, GF_LDEST},
    {"WAVE", 0, NOW - 1, OW_SAW, GF_OWAVE}, {"DETUN", 0, 127, 40, GF_CENTS}, {"OCT", -3, 2, 0, GF_OCT},
    {"PW", 0, 127, 64, GF_PCT}, {"SUB", 0, 127, 0, GF_PCT}, {"NOISE", 0, 127, 0, GF_PCT},
    {"TYPE", 0, NFTYPE - 1, F_LP24, GF_FTYPE}, {"CUT", 0, 127, 70, GF_CUT}, {"RES", 0, 127, 30, GF_PCT},
    {"DRIVE", 0, 127, 0, GF_PCT}, {"KTRK", 0, 127, 64, GF_PCT}, {"SEM", 0, 127, 64, GF_PCT},
    {"ATK", 0, 127, 2, GF_TIME}, {"DEC", 0, 127, 60, GF_TIME}, {"SUS", 0, 127, 90, GF_PCT}, {"REL", 0, 127, 50, GF_TIME},
    {"RATE", 0, 127, 50, GF_LFOHZ}, {"WAVE", 0, GL_NWAVE - 1, 1, GF_LWAVE}, {"DEPTH", 0, 127, 0, GF_PCT},
    {"DEST", 0, AD_N - 1, AD_PITCH, GF_ADEST},
    {"GRAIN", 0, 127, 127, GF_PCT}, {"ANLG", 0, 127, 0, GF_PCT},
    {"MODE", 0, NARPMODE - 1, A_OFF, GF_ARP}, {"DIV", 0, NDIV - 1, D_16, GF_DIV}, {"OCT", 1, 4, 1, GF_INT},
    {"GATE", 0, 127, 64, GF_PCT},
    {"RATE", 0, 127, 30, GF_PCT}, {"DEPTH", 0, 127, 40, GF_PCT}, {"MIX", 0, 127, 0, GF_PCT},
    {"TIME", 0, 127, 80, GF_DLYMS}, {"FDBK", 0, 127, 50, GF_PCT}, {"TONE", 0, 127, 70, GF_PCT}, {"MIX", 0, 127, 0, GF_PCT},
    {"SIZE", 0, 127, 100, GF_PCT}, {"DAMP", 0, 127, 75, GF_PCT}, {"PRE", 0, 127, 30, GF_PCT}, {"MIX", 0, 127, 0, GF_PCT},
    {"TYPE", 0, NDV - 1, DV_OVERDRIVE, GF_VTYPE}, {"AMNT", 0, 127, 60, GF_PCT}, {"TONE", 0, 127, 90, GF_PCT},
    {"MIX", 0, 127, 0, GF_PCT},
    {"RATCH", 1, 4, 1, GF_INT}, {"CHNCE", 0, 127, 127, GF_PCT},
};
static volatile int8_t gp_p[GQ_N];
static void gp_defaults(void)
{
    uint32_t i;
    for (i = 0; i < GQ_N; i++)
        gp_p[i] = GQ[i].def;
}

/* ---- the factory presets: what each changes from the defaults above, knob and value pairs ending in -1.
 * ChoralRoot's eight GRAIN sounds (eng_grain.c GRAIN_PP: the sources, the grain knobs, CUT RES) with Kayak's filter,
 * envelope, LFO and effects, then four with the analog layer */
#define GP_NFACT 12u
static const char *const GP_FACT_NAMES[GP_NFACT] = {
    "CLOUD PAD", "SHIMMER", "BREATH PAD", "FROZEN", "STRUM", "SWARM", "ACID DUST", "GLITCH",
    "SUB PAD", "ANA BASS", "POLY SAW", "PLUCK ARP"};
static const int8_t PF_CLOUD_PAD[] = {GQ_ATK, 40, GQ_REL, 90, GQ_LRATE, 30, GQ_LDEPTH, 50,
    GQ_CMIX, 40, GQ_DTIME, 90, GQ_DFB, 45, GQ_DTONE, 50, GQ_DMIX, 25, GQ_RSIZE, 110, GQ_RDAMP, 70, GQ_RPRE, 40,
    GQ_RMIX, 60, GQ_VAMT, 40, -1};
static const int8_t PF_SHIMMER[] = {GQ_TYPE_A, GT_BELL, GQ_TYPE_B, GT_BELL, GQ_TONE_B, 104, GQ_MOTION_B, 104,
    GQ_SEED_B, 11, GQ_SIZE, 70, GQ_DENS, 100, GQ_POS, 30, GQ_SPRD, 30, GQ_MORPH, 64, GQ_RAND, 24, GQ_PITCH, 12,
    GQ_SCAN, 8, GQ_CUT, 110, GQ_RES, 10, GQ_REL, 96, GQ_LRATE, 45, GQ_LDEPTH, 70, GQ_CMIX, 30, GQ_CRATE, 40,
    GQ_CDEPTH, 50, GQ_DTIME, 100, GQ_DFB, 55, GQ_DTONE, 60, GQ_DMIX, 35, GQ_RSIZE, 120, GQ_RDAMP, 60, GQ_RPRE, 50,
    GQ_RMIX, 70, GQ_VAMT, 40, -1};
static const int8_t PF_BREATH_PAD[] = {GQ_TYPE_A, GT_BREATH, GQ_TYPE_B, GT_VOWEL, GQ_MOTION_B, 20, GQ_SEED_B, 3,
    GQ_MORPH, 64, GQ_RAND, 10, GQ_FTYPE, F_SEM, GQ_CUT, 96, GQ_RES, 30, GQ_FMORPH, 30, GQ_ATK, 50, GQ_REL, 96,
    GQ_LRATE, 25, GQ_LWAVE, 1, GQ_LDEPTH, 40, GQ_LDEST, GD_POS, GQ_CRATE, 25, GQ_CDEPTH, 60, GQ_CMIX, 50,
    GQ_DTIME, 80, GQ_DFB, 40, GQ_DTONE, 40, GQ_RSIZE, 115, GQ_RDAMP, 80, GQ_RPRE, 40, GQ_RMIX, 55, GQ_VAMT, 40, -1};
static const int8_t PF_FROZEN[] = {GQ_TYPE_A, GT_VOWEL, GQ_TONE_A, 28, GQ_MOTION_A, 20, GQ_SEED_A, 3,
    GQ_TYPE_B, GT_BREATH, GQ_TONE_B, 28, GQ_MOTION_B, 20, GQ_SEED_B, 3, GQ_SIZE, 108, GQ_DENS, 72, GQ_POS, 40,
    GQ_SPRD, 0, GQ_MORPH, 0, GQ_RAND, 10, GQ_SCAN, 5, GQ_FTYPE, F_LP24, GQ_CUT, 92, GQ_RES, 10, GQ_ATK, 60,
    GQ_REL, 100, GQ_LRATE, 20, GQ_LWAVE, 1, GQ_LDEPTH, 30, GQ_DTIME, 110, GQ_DFB, 60, GQ_DTONE, 40, GQ_DMIX, 20,
    GQ_RSIZE, 127, GQ_RDAMP, 50, GQ_RPRE, 60, GQ_RMIX, 75, GQ_VAMT, 40, -1};
static const int8_t PF_STRUM[] = {GQ_TYPE_A, GT_PLUCK, GQ_MOTION_A, 100, GQ_TYPE_B, GT_METAL, GQ_SIZE, 60,
    GQ_DENS, 80, GQ_POS, 20, GQ_SPRD, 30, GQ_MORPH, 0, GQ_RAND, 10, GQ_SCAN, 0, GQ_CUT, 110, GQ_RES, 0,
    GQ_KTRK, 80, GQ_ATK, 2, GQ_DEC, 60, GQ_SUS, 40, GQ_REL, 70, GQ_LRATE, 50, GQ_LWAVE, 2, GQ_LDEPTH, 40,
    GQ_AMODE, A_UP, GQ_AOCT, 2, GQ_AGATE, 50, GQ_CMIX, 25, GQ_DTIME, 70, GQ_DTONE, 60, GQ_DMIX, 40, GQ_RSIZE, 90,
    GQ_RDAMP, 70, GQ_RPRE, 20, GQ_RMIX, 30, GQ_VAMT, 40, -1};
static const int8_t PF_SWARM[] = {GQ_TYPE_A, GT_CHIRP, GQ_TYPE_B, GT_SWEEP, GQ_SIZE, 48, GQ_DENS, 110,
    GQ_SPRD, 60, GQ_MORPH, 64, GQ_RAND, 60, GQ_SCAN, 20, GQ_FTYPE, F_BP12, GQ_CUT, 120, GQ_RES, 40, GQ_FDRIVE, 20,
    GQ_ATK, 20, GQ_REL, 80, GQ_LRATE, 60, GQ_LWAVE, 4, GQ_LDEPTH, 50, GQ_LDEST, GD_CUT, GQ_CRATE, 60, GQ_CDEPTH, 70,
    GQ_CMIX, 40, GQ_DTIME, 60, GQ_DFB, 60, GQ_DMIX, 40, GQ_RSIZE, 100, GQ_RDAMP, 70, GQ_RPRE, 20, GQ_RMIX, 35,
    GQ_VTYPE, DV_DIST, GQ_VAMT, 50, GQ_VTONE, 80, GQ_VMIX, 20, -1};
static const int8_t PF_ACID_DUST[] = {GQ_TYPE_A, GT_SWEEP, GQ_TONE_A, 104, GQ_MOTION_A, 104, GQ_SEED_A, 11,
    GQ_TYPE_B, GT_PLUCK, GQ_TONE_B, 104, GQ_MOTION_B, 104, GQ_SEED_B, 11, GQ_SIZE, 30, GQ_DENS, 118, GQ_SPRD, 80,
    GQ_RAND, 40, GQ_SCAN, -24, GQ_FTYPE, F_LP24, GQ_RES, 70, GQ_FDRIVE, 40, GQ_KTRK, 90, GQ_ATK, 5, GQ_DEC, 50,
    GQ_SUS, 70, GQ_REL, 60, GQ_LRATE, 70, GQ_LWAVE, 1, GQ_LDEPTH, 70, GQ_LDEST, GD_CUT, GQ_DTIME, 75, GQ_DFB, 65,
    GQ_DTONE, 50, GQ_DMIX, 45, GQ_RSIZE, 80, GQ_RDAMP, 70, GQ_RPRE, 20, GQ_RMIX, 25, GQ_VTYPE, DV_DIST,
    GQ_VTONE, 70, GQ_VMIX, 35, -1};
static const int8_t PF_GLITCH[] = {GQ_TYPE_A, GT_METAL, GQ_TYPE_B, GT_DRONE, GQ_SIZE, 24, GQ_DENS, 112, GQ_POS, 64,
    GQ_SPRD, 100, GQ_MORPH, 64, GQ_RAND, 90, GQ_SCAN, 0, GQ_FTYPE, F_HP12, GQ_CUT, 60, GQ_RES, 30, GQ_FDRIVE, 30,
    GQ_ATK, 2, GQ_DEC, 40, GQ_SUS, 80, GQ_REL, 50, GQ_LRATE, 80, GQ_LWAVE, 3, GQ_LDEPTH, 60, GQ_LDEST, GD_SIZE,
    GQ_DTIME, 40, GQ_DFB, 70, GQ_DTONE, 90, GQ_DMIX, 40, GQ_RSIZE, 70, GQ_RDAMP, 60, GQ_RPRE, 10, GQ_RMIX, 25,
    GQ_VTYPE, DV_CRUSH, GQ_VAMT, 70, GQ_VTONE, 100, GQ_VMIX, 50, -1};
static const int8_t PF_SUB_PAD[] = {GQ_ATK, 40, GQ_REL, 90, GQ_LRATE, 30, GQ_LDEPTH, 50,     /* CLOUD PAD over a square sub */
    GQ_ALEV, 70, GQ_OWAVE, OW_SQR, GQ_ODET, 20, GQ_OOCT, -1, GQ_ACUT, 55, GQ_ARES, 15, GQ_AATK, 40, GQ_ADEC, 64,
    GQ_ASUS, 127, GQ_AREL, 90, GQ_CMIX, 40, GQ_DTIME, 90, GQ_DFB, 45, GQ_DMIX, 20, GQ_RSIZE, 110, GQ_RDAMP, 70,
    GQ_RPRE, 40, GQ_RMIX, 60, -1};
static const int8_t PF_ANA_BASS[] = {GQ_GLEV, 0, GQ_ALEV, 110, GQ_OWAVE, OW_SAW, GQ_ODET, 25, GQ_OOCT, -1,   /* analog alone */
    GQ_OSUB, 60, GQ_AFTYPE, F_LP24, GQ_ACUT, 45, GQ_ARES, 50, GQ_AFDRIVE, 30, GQ_AKTRK, 90, GQ_AATK, 1, GQ_ADEC, 50,
    GQ_ASUS, 40, GQ_AREL, 30, -1};
static const int8_t PF_POLY_SAW[] = {GQ_TYPE_A, GT_BELL, GQ_TYPE_B, GT_BELL, GQ_TONE_B, 104, GQ_MOTION_B, 104,
    GQ_SEED_B, 11, GQ_MORPH, 64, GQ_PITCH, 12, GQ_GLEV, 50, GQ_ALEV, 90, GQ_OWAVE, OW_SAW, GQ_ODET, 70,
    GQ_AFTYPE, F_LP12, GQ_ACUT, 85, GQ_ARES, 20, GQ_AATK, 20, GQ_ADEC, 70, GQ_ASUS, 100, GQ_AREL, 70,
    GQ_ALRATE, 30, GQ_ALDEPTH, 20, GQ_ALDEST, AD_CUT, GQ_CMIX, 60, GQ_RSIZE, 105, GQ_RMIX, 50, -1};
static const int8_t PF_PLUCK_ARP[] = {GQ_TYPE_A, GT_PLUCK, GQ_TYPE_B, GT_PLUCK, GQ_TONE_B, 104, GQ_MORPH, 30,
    GQ_ATK, 2, GQ_DEC, 50, GQ_SUS, 30, GQ_REL, 40, GQ_GLEV, 40, GQ_ALEV, 100, GQ_OWAVE, OW_SQR, GQ_OPW, 40,
    GQ_ACUT, 60, GQ_ARES, 45, GQ_AATK, 1, GQ_ADEC, 45, GQ_ASUS, 0, GQ_AREL, 30, GQ_AMODE, A_UP, GQ_AOCT, 2,
    GQ_AGATE, 60, GQ_DTIME, 75, GQ_DFB, 55, GQ_DMIX, 45, GQ_RSIZE, 90, GQ_RMIX, 40, -1};
static const int8_t *const GP_FACT[GP_NFACT] = {PF_CLOUD_PAD, PF_SHIMMER, PF_BREATH_PAD, PF_FROZEN, PF_STRUM, PF_SWARM,
    PF_ACID_DUST, PF_GLITCH, PF_SUB_PAD, PF_ANA_BASS, PF_POLY_SAW, PF_PLUCK_ARP};

/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (eng_grain.c's) */
static uint32_t pow2_q16(int32_t d16);
/* the LFO's rate, hundredths of a hertz: 0.05 .. 20 Hz */
static uint32_t gl_hz100(int32_t v) { return (5u * pow2_q16(clamp(v, 0, 127) * 1307 / 100)) >> 16; }
/* the delay's time in ms: 20 .. 550 */
static uint32_t gp_dly_ms(int32_t v) { return (20u * pow2_q16(clamp(v, 0, 127) * 723 / 100)) >> 16; }
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
    case GF_ADEST: gq_put_str(&p, AD_NAMES[clamp(v, 0, AD_N - 1)]); break;
    case GF_OWAVE: gq_put_str(&p, OW_NAMES[clamp(v, 0, NOW - 1)]); break;
    case GF_PCT: gq_put_dec(&p, (uint32_t)(v * 100 + 63) / 127u); *p++ = '%'; break;
    case GF_SIGNED:
    case GF_SEMI:
        if (v) *p++ = v < 0 ? '-' : '+';
        gq_put_dec(&p, (uint32_t)(v < 0 ? -v : v));
        if (GQ[q].fmt == GF_SEMI) gq_put_str(&p, "st");
        break;
    case GF_SIZE: gq_put_dec(&p, (10u * pow2_q16(v * 8)) >> 16); gq_put_str(&p, "ms"); break;   /* 441 samples: 10 ms */
    case GF_DENS:                               /* grains a second: 65536 / 2^(-DENS x 10 / 192), in tenths below 10 */
        u = 6553600u / pow2_q16(-v * 10);       /* x 100 */
        if (u < 1000u) gq_put_frac(&p, u, 0); else gq_put_dec(&p, (u + 50u) / 100u);
        gq_put_str(&p, "/s");
        break;
    case GF_FTYPE: gq_put_str(&p, FTYPE_NAMES[clamp(v, 0, NFTYPE - 1)]); break;
    case GF_VTYPE: gq_put_str(&p, VTYPE_NAMES[clamp(v, 0, NDV - 1)]); break;
    case GF_ARP: gq_put_str(&p, ARP_NAMES[clamp(v, 0, NARPMODE - 1)]); break;
    case GF_DIV: gq_put_str(&p, DIV_NAMES[clamp(v, 0, NDIV - 1)]); break;
    case GF_CENTS: gq_put_dec(&p, (uint32_t)(v * 30 + 63) / 127u); gq_put_str(&p, "ct"); break;   /* +-, the two oscillators apart */
    case GF_OCT: if (v) *p++ = v < 0 ? '-' : '+'; gq_put_dec(&p, (uint32_t)(v < 0 ? -v : v)); gq_put_str(&p, "oct"); break;
    case GF_DLYMS: gq_put_dec(&p, gp_dly_ms(v)); gq_put_str(&p, "ms"); break;
    case GF_CUT:                                /* OberVave's: MIDI note v + 8, 13 Hz .. 19.9 kHz */
        u = (440u * pow2_q16((v - 61) * 16)) >> 16;
        if (u > 19845u) u = 19845u;
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
    int32_t env;                 /* the louder layer's level, Q15: the voice sounds while it is not 0 */
    int32_t envg, enva;          /* the grains' and the analog layer's ADSR, Q15 */
    uint8_t decg, deca;          /* .. 0 the attack, 1 the decay to the sustain */
    uint8_t note;
    int32_t next;                /* samples to the next grain */
    int32_t morph;               /* MORPH as the last block played it, Q15 (ramped a fetch to this one's) */
    vflt_state_t flt, flta;      /* the grains' filter and the analog layer's (OberVave's) */
    int32_t lvl;                 /* the velocity, Q15 */
    uint32_t age;
    uint32_t ph1, ph2, phs;      /* the analog oscillators, the sub */
    int32_t nrng;                /* the analog noise */
} gp_voice_t;
static gp_grain_t gpg[GP_NG];
static gp_voice_t gpv[GP_NV];
static uint32_t gp_scan, gp_ages, gp_lph;         /* SCAN's phase, the voices' ages, the LFO's phase */
static uint32_t gp_alph;                         /* the analog layer's LFO */
static int32_t gp_alsh;
static int32_t gp_lsh;                           /* the LFO's S&H value */
static volatile int32_t gp_pos_view;             /* where the grains start (POS, SCAN, the LFO), Q8: the screen's */
static int32_t gp_rng = 0x2545F491;
static volatile uint32_t gp_bend_q16 = 65536;    /* the SMK-37's pitch wheel */

static inline uint32_t gp_scale(uint32_t n, uint32_t f16) { return (n >> 16) * f16 + (((n & 0xFFFFu) * f16) >> 16); }
/* the grain window, Hann: sin^2(pi x) = (1 - cos(2 pi x)) / 2, Q15 */
static int32_t gp_soft(int32_t x)                /* the output: as it is up to 20000, then bent towards 32767 */
{
    int32_t a = x < 0 ? -x : x, y;
    if (a <= 20000) return x;
    a = a - 20000 > 150000 ? 150000 : a - 20000;
    y = 20000 + a * 12767 / (a + 12767);
    return x < 0 ? -y : y;
}
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

/* an LFO's value a block: SINE TRI SAW SQR S&H of phase ph (sh: the S&H's value), +-32767 */
static int32_t gp_lfo(int32_t wave, uint32_t ph, int32_t sh)
{
    switch (wave) {
    case 1: return osc_tri(ph);
    case 2: return (int32_t)(ph >> 16) - 32768;
    case 3: return ph < 0x80000000u ? 32767 : -32767;
    case 4: return sh;
    default: return sine_i(ph);
    }
}
/* an ADSR a block: from *env to its next value (returned: the old one, *de its step a sample over m) */
static int32_t gp_adsr(int32_t *env, uint8_t *dec, int32_t gate, int32_t atk, int32_t dcy, int32_t sus, int32_t rel,
                       int32_t *de, uint32_t m)
{
    int32_t e0 = *env, e1;
    if (!gate)                                  /* the release, from wherever it is */
        e1 = e0 - rel < 0 ? 0 : e0 - rel;
    else if (!*dec) {                           /* the attack, to full */
        e1 = e0 + atk;
        if (e1 >= 32767) { e1 = 32767; *dec = 1; }
    } else                                      /* the decay, to the sustain (or up to it, SUS turned up) */
        e1 = e0 > sus ? (e0 - dcy < sus ? sus : e0 - dcy) : (e0 + dcy > sus ? sus : e0 + dcy);
    *env = e1;
    *de = (e1 - e0) / (int32_t)m;
    return e0;
}

static void gp_kill(uint32_t vi)                  /* the grains of voice vi go */
{
    uint32_t i;
    for (i = 0; i < GP_NG; i++)
        if (gpg[i].owner == vi + 1u)
            gpg[i].owner = 0;
}

/* the arpeggiator, the sequencer, the keys' events (kayak_seq.h, after the voices) */
static void kayak_events(void);
static void arp_tick(void);
static void seq_tick(void);
/* the output as the oscilloscope shows it (HOME): the last SCOPE_N frames, mono */
#define SCOPE_N 512u
static int16_t scope[SCOPE_N];
static volatile uint32_t scope_w;

/* ---- the audio side: n stereo frames added to out. A voice is two layers: the grains (through their filter and
 * ADSR) and the analog oscillators (through theirs), each at its LEVEL; the effects take the sum */
/* the analog layer's output (its filter and ADSR on it, before the effects), for the ANA pages: half a second as 240
 * columns of a min and a max, ANA_COL frames each; ana_w: the columns written so far */
#define ANA_COL 92u
static volatile int16_t ana_lo[240], ana_hi[240];
static volatile uint32_t ana_w;
static int32_t ana_cmin, ana_cmax;
static uint32_t ana_cn;

static void gp_audio(int32_t *out, uint32_t n)
{
    const int16_t *A = gbuf[gsel[0]], *B = gbuf[gsel[1]];
    int32_t acc[CTL], mono[CTL], amono[CTL], atk = gp_env_step(gp_p[GQ_ATK]), rel = gp_env_step(gp_p[GQ_REL]);
    int32_t dcy = gp_env_step(gp_p[GQ_DEC]), sus = gp_p[GQ_SUS] * 258;   /* the decay's step, the sustain level Q15 */
    int32_t aatk = gp_env_step(gp_p[GQ_AATK]), arel = gp_env_step(gp_p[GQ_AREL]), adcy = gp_env_step(gp_p[GQ_ADEC]);
    int32_t asus = gp_p[GQ_ASUS] * 258, glev = gp_p[GQ_GLEV] * 258, alev = gp_p[GQ_ALEV] * 258;
    int32_t scan = gp_scan_inc(gp_p[GQ_SCAN]), ft = gp_p[GQ_FTYPE], res = gp_p[GQ_RES], fm = gp_p[GQ_FMORPH];
    int32_t ldep = gp_p[GQ_LDEPTH], ldst = gp_p[GQ_LDEST], lwave = gp_p[GQ_LWAVE], d = gp_p[GQ_FDRIVE], drv, ktrk;
    int32_t aft = gp_p[GQ_AFTYPE], ares = gp_p[GQ_ARES], afm = gp_p[GQ_AFMORPH], adrv, aktrk = gp_p[GQ_AKTRK] * 2;
    int32_t aldep = gp_p[GQ_ALDEPTH], aldst = gp_p[GQ_ALDEST], alwave = gp_p[GQ_ALWAVE];
    int32_t owave = gp_p[GQ_OWAVE], odet = gp_p[GQ_ODET] * 9, ooct = gp_p[GQ_OOCT], osub = gp_p[GQ_OSUB] * 258;
    int32_t onoise = gp_p[GQ_ONOISE] * 258;
    uint32_t b, i, v, k, nact = 0, iv, share, linc = gl_hz100(gp_p[GQ_LRATE]) * 31166u;   /* a block's turn of the LFO:
                                                  * 2^32 x 32 / (44100 x 100) a hundredth of a hertz */
    uint32_t alinc = gl_hz100(gp_p[GQ_ALRATE]) * 31166u;
    gp_eff_t e;
    vflt_t f, fa;
    static int32_t dry[256];                    /* the dry sound, mono: the effects' send */
    static uint8_t fx_ready;
    if (!fx_ready) {
        fx_init();
        fx_ready = 1;
    }
    if (n > 256u) n = 256u;                     /* (the platform's block: 256 frames) */
    kayak_events();                             /* the keys since the last call: to the arp or the voices */
    drv = 4096 + d * d * 15 * 4096 / 16129;     /* DRIVE into the filter: 1 .. 16 (OberVave's) */
    d = gp_p[GQ_AFDRIVE];
    adrv = 4096 + d * d * 15 * 4096 / 16129;
    ktrk = gp_p[GQ_KTRK] * 2;                   /* the cutoff follows the note: 127 = a semitone a semitone */
    e.rnd = gp_p[GQ_RAND];
    for (v = 0; v < GP_NV; v++)
        nact += gpv[v].gate || gpv[v].env;
    share = nact > 1u ? (GP_NG / nact > 2u ? GP_NG / nact : 2u) : GP_NG;
    for (b = 0; b < n; b += CTL) {
        uint32_t m = n - b < CTL ? n - b : CTL, old = gp_lph, pw;
        int32_t lm = 0, lpit, alm = 0, ax = 0, apit, amod = 32767;
        arp_tick();                             /* the arp, then the sequencer (OberVave's order): their notes this block */
        seq_tick();
        gp_lph += linc;                         /* the grains' LFO: one value a block, +-63 knob steps at full DEPTH */
        if (gp_lph < old)
            gp_lsh = (int32_t)(noise32(&gp_rng) >> 17) * 2 - 32767;
        if (ldep)
            lm = (gp_lfo(lwave, gp_lph, gp_lsh) * ldep) >> 16;
        old = gp_alph;                          /* the analog layer's LFO */
        gp_alph += alinc;
        if (gp_alph < old)
            gp_alsh = (int32_t)(noise32(&gp_rng) >> 17) * 2 - 32767;
        if (aldep) {
            ax = gp_lfo(alwave, gp_alph, gp_alsh);
            alm = (ax * aldep) >> 16;
            if (aldst == AD_LEVEL)              /* a tremolo: down from full, DEPTH 127 to nearly nothing */
                amod = 32767 - ((32767 - ax) * aldep >> 8);
        }
        apit = aldst == AD_PITCH ? alm * 3 : 0; /* +-12 st */
        pw = 214748365u + (uint32_t)clamp(gp_p[GQ_OPW] + (aldst == AD_PW ? alm : 0), 0, 127) * 30436776u;   /* 5 .. 95 % */
        e.size = clamp(gp_p[GQ_SIZE] + (ldst == GD_SIZE ? lm : 0), 0, 127);
        e.dens = clamp(gp_p[GQ_DENS] + (ldst == GD_DENS ? lm : 0), 0, 127);
        e.sprd = clamp(gp_p[GQ_SPRD] + (ldst == GD_SPRD ? lm : 0), 0, 127);
        e.morph = ((clamp(gp_p[GQ_MORPH] + (ldst == GD_MORPH ? lm : 0), 0, 127) << 8) * 16513 + 8192) >> 14;   /* B: 32768 */
        lpit = ldst == GD_PITCH ? lm * 3 : 0;  /* +-12 st */
        iv = (44100u * pow2_q16(-e.dens * 10)) >> 16;       /* the next grain: DENS a second, 1 .. 98 /s */
        gp_scan += (uint32_t)scan;
        e.pos_q8 = (int32_t)(((uint32_t)clamp((gp_p[GQ_POS] + (ldst == GD_POS ? lm : 0)) * 256, 0, 127 << 8)
                              + (gp_scan >> 17)) % (127u * 256u + 1u));
        gp_pos_view = e.pos_q8;
        for (i = 0; i < m; i++)
            mono[i] = amono[i] = 0;
        for (v = 0; v < GP_NV; v++) {
            gp_voice_t *V = &gpv[v];
            int32_t e0, de;
            if (!V->gate && !V->env)
                continue;
            /* the grains: their ADSR, the cloud, their filter */
            e0 = gp_adsr(&V->envg, &V->decg, V->gate, atk, dcy, sus, rel, &de, m);
            if (glev && (V->gate || V->envg || e0)) {
                int32_t m0 = V->morph;
                uint32_t mine = 0;
                V->morph = e.morph;
                e.pitch16 = (int32_t)V->note * 16 + gp_p[GQ_PITCH] * 16 + lpit;
                for (i = 0; i < m; i++)
                    acc[i] = 0;
                for (k = 0; k < GP_NG; k++)
                    if (gpg[k].owner == v + 1u)
                        mine += (uint32_t)gp_run(&gpg[k], acc, m, m0, e.morph, A, B);
                V->next -= (int32_t)m;
                if (V->next <= 0) {                      /* (in the release too: the cloud fades with the envelope) */
                    uint32_t r = noise32(&gp_rng);
                    int32_t j = (((int32_t)(r & 0xFFFFu) - 32768) * e.sprd) / 254;   /* +-SPRD / 2, Q15 */
                    V->next += (int32_t)iv + mulq15((int32_t)iv, j);
                    if (V->next < 0)
                        V->next = 0;
                    if (mine < share)
                        gp_spawn(v, iv, &e, A, B);
                }
                /* OberVave's filter, a voice: CUT (+ the LFO) as MIDI note CUT + 8, round C4 by KTRK */
                vflt_set(&f, ft, (gp_p[GQ_CUT] + 8 + (ldst == GD_CUT ? lm : 0)) * 256 + ((int32_t)V->note - 60) * ktrk,
                         res, fm, drv);
                {
                    int32_t g = mulq15(V->lvl, glev);
                    for (i = 0; i < m; i++) {
                        int32_t y = vflt_step(&f, &V->flt, clamp(acc[i], -65535, 65535));
                        mono[i] += mulq15(mulq15(clamp(y, -32767, 32767), e0), g);
                        e0 += de;
                    }
                }
            }
            if (!V->gate && !V->envg)                    /* the grains' release ended */
                gp_kill(v);
            /* the analog layer: two oscillators detuned apart, OCT from the note, a square an octave under, noise;
             * its own filter and ADSR */
            e0 = gp_adsr(&V->enva, &V->deca, V->gate, aatk, adcy, asus, arel, &de, m);
            if (alev && (V->gate || V->enva || e0)) {
                uint32_t inc = pitch_inc((uint32_t)clamp((int32_t)V->note * 16 + ooct * 192 + apit, 0, 2047));
                uint32_t p1 = V->ph1, p2 = V->ph2, ps = V->phs, i1, i2;
                int32_t dd, g = mulq15(mulq15(V->lvl, alev), amod) * 2;   /* (x 2: as loud as a voice of grains) */
                inc = (inc >> 8) * (gp_bend_q16 >> 8);
                dd = mulq16((int32_t)inc, (uint32_t)odet);
                i1 = inc - (uint32_t)dd;
                i2 = inc + (uint32_t)dd;
                vflt_set(&fa, aft, (gp_p[GQ_ACUT] + 8 + (aldst == AD_CUT ? alm : 0)) * 256 + ((int32_t)V->note - 60) * aktrk,
                         ares, afm, adrv);
                for (i = 0; i < m; i++) {
                    int32_t s, y;
                    if (owave == OW_SQR) s = osc_pulse(p1, i1, pw) + osc_pulse(p2, i2, pw);
                    else if (owave == OW_TRI) s = osc_tri(p1) + osc_tri(p2);
                    else s = osc_saw(p1, i1) + osc_saw(p2, i2);
                    s >>= 1;
                    if (osub) s += mulq15(ps < 0x80000000u ? 32767 : -32767, osub);
                    if (onoise) s += mulq15((int32_t)(noise32(&V->nrng) >> 16) - 32768, onoise);
                    p1 += i1;
                    p2 += i2;
                    ps += i1 >> 1;
                    y = vflt_step(&fa, &V->flta, clamp(s, -65535, 65535));
                    y = mulq15(mulq15(clamp(y, -32767, 32767), e0), g);
                    mono[i] += y;
                    amono[i] += y;
                    e0 += de;
                }
                V->ph1 = p1;
                V->ph2 = p2;
                V->phs = ps;
            }
            V->env = V->envg > V->enva ? V->envg : V->enva;   /* (the voice sounds while either layer does) */
        }
        for (i = 0; i < m; i++) {
            out[2u * (b + i)] += mono[i];
            out[2u * (b + i) + 1u] += mono[i];
            dry[b + i] = mono[i];
            if (amono[i] < ana_cmin) ana_cmin = amono[i];   /* (the ANA pages' picture) */
            if (amono[i] > ana_cmax) ana_cmax = amono[i];
            if (++ana_cn >= ANA_COL) {
                ana_lo[ana_w % 240u] = (int16_t)clamp(ana_cmin, -32767, 32767);
                ana_hi[ana_w % 240u] = (int16_t)clamp(ana_cmax, -32767, 32767);
                ana_w++;
                ana_cn = 0;
                ana_cmin = 32767;
                ana_cmax = -32767;
            }
        }
    }
    /* OberVave's effects on the whole sound: the drive beside it, then the chorus, the delay and the reverb */
    fx_block(gp_p[GQ_CRATE], gp_p[GQ_CDEPTH], gp_p[GQ_CMIX], (int32_t)gp_dly_ms(gp_p[GQ_DTIME]), gp_p[GQ_DFB],
             gp_p[GQ_DTONE], gp_p[GQ_DMIX], gp_p[GQ_RSIZE], gp_p[GQ_RDAMP], gp_p[GQ_RPRE], gp_p[GQ_RMIX], n);
    drive_process(out, dry, n, (uint32_t)gp_p[GQ_VTYPE], (uint32_t)gp_p[GQ_VAMT], (uint32_t)gp_p[GQ_VTONE],
                  (uint32_t)gp_p[GQ_VMIX]);
    fx_process(out, dry, dry, dry, n);
    for (i = 0; i < n; i++) {                   /* a soft knee over the top (chords on a loud sound); the scope's ring */
        out[2u * i] = gp_soft(out[2u * i]);
        out[2u * i + 1u] = gp_soft(out[2u * i + 1u]);
        scope[(scope_w + i) % SCOPE_N] = (int16_t)((out[2u * i] + out[2u * i + 1u]) >> 1);
    }
    scope_w += n;
}

/* ---- the voices, from the audio side (the keys' events, the arpeggiator, the sequencer: kayak_seq.h) */
static void gp_voice_on(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    if (note > 127u || !vel)
        return;
    for (v = 0; v < GP_NV; v++) {               /* the same note, else a free voice, else the quietest, else the oldest */
        uint32_t score = gpv[v].note == note && gpv[v].gate ? 0u
                       : gpv[v].gate ? 0x80000000u + gpv[v].age : (uint32_t)gpv[v].env + 1u;
        if (score < best) { best = score; pick = v; }
    }
    if (!gpv[pick].env) {                       /* a fresh voice: no grains from before, the filters clear */
        gp_kill(pick);
        memset(&gpv[pick].flt, 0, sizeof gpv[pick].flt);
        memset(&gpv[pick].flta, 0, sizeof gpv[pick].flta);
        gpv[pick].morph = ((gp_p[GQ_MORPH] << 8) * 16513 + 8192) >> 14;
        gpv[pick].ph1 = 0;
        gpv[pick].ph2 = 0x40000000u;            /* (the second oscillator a quarter on: no doubled edge at the start) */
        gpv[pick].phs = 0;
        if (!gpv[pick].nrng) gpv[pick].nrng = (int32_t)(0x1F123BB5u + pick * 0x9E3779B9u);
    }
    gpv[pick].note = (uint8_t)note;
    gpv[pick].age = ++gp_ages;
    gpv[pick].lvl = (int32_t)(vel * GP_VOICE / 127u);
    gpv[pick].next = 0;                         /* the first grain at once */
    gpv[pick].decg = gpv[pick].deca = 0;        /* both attacks again, from where the levels are */
    gpv[pick].gate = 1;
}
static void gp_voice_off(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < GP_NV; v++)
        if (gpv[v].note == note)
            gpv[v].gate = 0;
}

/* ---- notes, from the main loop: a queue the audio side takes at its next call (kayak_events) */
#define EVQ_N 64u
static volatile uint8_t evq[EVQ_N][2];          /* note, velocity (0: off) */
static volatile uint32_t evq_w, evq_r;
static void gp_note_post(uint32_t note, uint32_t vel)
{
    if (note > 127u || evq_w - evq_r >= EVQ_N)
        return;
    evq[evq_w % EVQ_N][0] = (uint8_t)note;
    evq[evq_w % EVQ_N][1] = (uint8_t)vel;
    __asm__ volatile("" ::: "memory");          /* (the entry before the index: the ISR reads in that order) */
    evq_w++;
}
static void gp_note_on(uint32_t note, uint32_t vel) { if (vel) gp_note_post(note, vel > 127u ? 127u : vel); }
static void gp_note_off(uint32_t note) { gp_note_post(note, 0); }
#include "kayak_seq.h"
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
