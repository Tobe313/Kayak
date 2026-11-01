/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 zvenson (OberVave: https://github.com/zvenson/obervave, obv_engine.h's arpeggiator and held-key
 * lists, obv_seq.h's step sequencer), Tobe313 (Kayak) */
/* Kayak's arpeggiator and step sequencer, OberVave's cut down to one sound and one track. Both run on the audio side,
 * a control block (32 frames, 0.73 ms) at a time, on one tempo (the pattern's, or a MIDI clock's): the arp on the
 * sequencer's grid while it runs, as OberVave's. The arp: UP DOWN UP/DN RANDOM PLAYED over the keys held, 1..4
 * octaves, a note value, a gate. The sequencer: one track of up to SEQ_MAX steps, each a chord of up to four notes
 * with a velocity and a gate (or a tie into the next step), a note value, swing. What goes from OberVave's: the
 * four tracks, the ratchets, the chance, the nudge, the knob lanes, the patterns' swap on the bar, the count-in, the
 * click. */
#define NHELD 16u
#define SEQ_MAX 64u
#define SEQ_CHORD 4u
#define GATE_TIE 127u                           /* a gate of 127: the notes hold into the next step */
typedef struct { uint8_t note[SEQ_CHORD], vel, gate, chord, ratch, chance; } step_t;   /* note 0 = none; chord: CHORDS' (0: the
                                                 * notes as written); ratch: hits in the step (1..4), chance: %
                                                 * it plays */

/* ---- the chords (Kayak): the notes a key plays (GLO's CHORD), a step's (the sequencer's CHORD), as semitones over
 * the root. 0 (-): the one note */
#define NCHORD 12u
static const char *const CHORD_NAMES[NCHORD] = {"-", "MAJ", "MIN", "7", "MAJ7", "MIN7", "SUS2", "SUS4", "DIM", "AUG",
                                                "POWER", "ADD9"};
static const int8_t CHORDS[NCHORD][SEQ_CHORD] = {   /* -1: none */
    {0, -1, -1, -1}, {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 2, 7, -1},
    {0, 5, 7, -1}, {0, 3, 6, -1}, {0, 4, 8, -1}, {0, 7, 12, -1}, {0, 4, 7, 14}};
static uint32_t chord_notes(uint32_t root, uint32_t chord, uint8_t *out)   /* root + the chord into out[4]; how many */
{
    uint32_t i, n = 0;
    for (i = 0; i < SEQ_CHORD; i++) out[i] = 0;
    for (i = 0; i < SEQ_CHORD && CHORDS[chord % NCHORD][i] >= 0; i++)
        if (root + (uint32_t)CHORDS[chord % NCHORD][i] <= 127u) out[n++] = (uint8_t)(root + (uint32_t)CHORDS[chord % NCHORD][i]);
    return n;
}
typedef struct { uint8_t len, div, swing, tempo; step_t st[SEQ_MAX]; uint8_t rsv; } pattern_t;   /* swing
                                                 * 50..75 %, tempo BPM; rsv: (unused) */
static pattern_t pat;                           /* the main loop edits, the audio side plays */

static void step_clear(step_t *s)
{
    uint32_t i;
    for (i = 0; i < SEQ_CHORD; i++) s->note[i] = 0;
    s->vel = 100;
    s->gate = 96;                               /* 76 % */
    s->chord = 0;
    s->ratch = 1;
    s->chance = 100;
}
static void pat_init(pattern_t *p)              /* a bar of sixteenths, empty, 120 BPM */
{
    uint32_t i;
    p->len = 16;
    p->div = D_16;
    p->swing = 50;
    p->tempo = 120;
    p->rsv = 0;
    for (i = 0; i < SEQ_MAX; i++) step_clear(&p->st[i]);
}

/* main loop -> audio side */
static volatile uint32_t ctl_seq_run, ctl_seq_restart, ctl_tempo_ext;   /* ext: a MIDI clock's BPM, 0 none */
static volatile int32_t ctl_transp;             /* semitones the sequence and the held arp are played up or down: the
                                                 * keys set it while they play (dinghy.c) */
static int32_t tr_note(uint32_t n) { return clamp((int32_t)n + ctl_transp, 1, 127); }
/* audio side -> main loop */
static volatile uint32_t ui_seq_step = 0xFFFFFFFFu, ui_beat, ui_nheld;   /* ui_nheld: the notes the arp holds */

/* a quarter in frames, rounded to 48 frames (~1 ms): every step length (q / 1, 2, 3, 4, 6, 8, 12) comes out whole */
static uint32_t quarter_len(void)
{
    uint32_t bpm = ctl_tempo_ext ? ctl_tempo_ext : pat.tempo;
    return (60u * FS / (uint32_t)clamp((int32_t)bpm, 40, 240) + 24u) / 48u * 48u;
}

/* ---- the keys held (audio side): the arp plays them. phys: the key is down (with HOLD a note stays held after its
 * key is up, until a new chord starts with every key up: OberVave's latch) */
static struct { uint8_t note, vel, phys; } held[NHELD];
static volatile uint32_t ctl_hold;               /* HOLD (main loop: a long press on ARP) */
static uint32_t hold_was;
static uint32_t nheld;
static void held_remove(uint32_t note)
{
    uint32_t i, j;
    for (i = j = 0; i < nheld; i++)
        if (held[i].note != note) held[j++] = held[i];
    nheld = j;
}

/* ---- the arpeggiator (OberVave's arp_step / arp_tick) */
static struct { uint32_t cnt, period, pos, sounding, note, was_on, vel, play, rat, rk; } arp;   /* rat: the ratchet's
                                                 * hits in the step, rk: the last one played; play: CHANCE let it */
static struct { uint8_t note[SEQ_CHORD], vel, n; } sa;   /* the sequencer's step, when it plays the arp (on) */
static int32_t arp_rng = 0x6C8E9CF5;
static int seq_phase(uint32_t *ph);
static int seq_starting(void);
static void arp_stop_note(void)
{
    if (arp.sounding) { gp_voice_off(arp.note); arp.sounding = 0; }
}
static void arp_step(void)
{
    uint8_t sorted[NHELD];
    uint32_t i, j, n = sa.n ? sa.n : nheld, oct = (uint32_t)clamp(gp_p[GQ_AOCT], 1, 4), len, k, mode = (uint32_t)gp_p[GQ_AMODE], idx;
    uint32_t chance = (uint32_t)clamp(gp_p[GQ_ACHANCE], 0, 127);
    for (i = 0; i < n; i++) sorted[i] = sa.n ? sa.note[i] : held[i].note;
    if (mode != A_PLAY)
        for (i = 1; i < n; i++) {               /* insertion sort: 16 at most */
            uint8_t t = sorted[i];
            for (j = i; j && sorted[j - 1u] > t; j--) sorted[j] = sorted[j - 1u];
            sorted[j] = t;
        }
    len = n * oct;
    if (mode == A_RND)
        k = noise32(&arp_rng) % len;
    else if (mode == A_UPDN && len > 2u) {
        if (arp.pos >= 2u * len - 2u) arp.pos = 0;
        k = arp.pos < len ? arp.pos : 2u * len - 2u - arp.pos;
        arp.pos++;
    } else {
        if (arp.pos >= len) arp.pos = 0;
        k = mode == A_DOWN ? len - 1u - arp.pos : arp.pos;
        arp.pos++;
    }
    idx = k % n;
    arp.note = (uint32_t)clamp((int32_t)sorted[idx] + 12 * (int32_t)(k / n) + ctl_transp, 0, 127);
    for (i = 0; i < n && !sa.n; i++)            /* the velocity of that key (the step's: its own) */
        if (held[i].note == sorted[idx]) break;
    arp.vel = sa.n ? sa.vel : i < n ? held[i].vel : 100u;
    arp.play = chance >= 127u || noise32(&arp_rng) % 127u < chance;   /* CHANCE: the step may stay silent */
    arp.rat = (uint32_t)clamp(gp_p[GQ_ARATCH], 1, 4);
    arp.rk = 0;
    if (arp.play) { gp_voice_on(arp.note, arp.vel); arp.sounding = 1; }
}
/* within the step: the ratchet's hits (RATCH: the step cut in 1..4), each held for GATE of its part */
static void arp_gate(uint32_t gate)
{
    uint32_t L = arp.period / (arp.rat ? arp.rat : 1u), k;
    if (L < CTL) L = CTL;
    k = arp.cnt / L;
    if (k >= arp.rat) k = arp.rat ? arp.rat - 1u : 0u;
    if (k > arp.rk) {                           /* the next hit: the same note again */
        arp.rk = k;
        arp_stop_note();
        if (arp.play) { gp_voice_on(arp.note, arp.vel); arp.sounding = 1; }
    }
    if (arp.sounding && gate < 127u && arp.cnt - k * L >= (L * (gate + 1u)) >> 7) arp_stop_note();
}
static void arp_tick(void)
{
    uint32_t mode = (uint32_t)gp_p[GQ_AMODE], div = DIV_PER_QUARTER[(uint32_t)gp_p[GQ_ADIV] % NDIV], q, gate = (uint32_t)gp_p[GQ_AGATE];
    if (mode == A_OFF) {
        if (arp.was_on) { arp_stop_note(); arp.was_on = 0; }
        return;
    }
    if (!arp.was_on) {                          /* the arp comes on: the keys' own notes go, it plays them now */
        uint32_t i;
        for (i = 0; i < nheld; i++) gp_voice_off(held[i].note);
        arp.was_on = 1; arp.pos = 0; arp.cnt = 0xFFFFFFFFu;
    }
    if (!nheld && !sa.n) { arp_stop_note(); arp.pos = 0; arp.cnt = 0xFFFFFFFFu; return; }
    q = quarter_len();
    arp.period = q / div;
    if (arp.period < CTL) arp.period = CTL;
    {                                           /* the sequencer running: on its grid (the first key: at once if just
                                                 * after a line, else on the next one), as a DAW's arp */
        uint32_t ph;
        if (seq_starting()) {                   /* the transport starts in this block: the downbeat */
            arp.cnt = 0; arp_stop_note(); arp_step(); return;
        }
        if (seq_phase(&ph)) {
            if (ph / arp.period >= div) return; /* (the last sliver of a beat: its line is the next beat's start) */
            ph %= arp.period;
            if (ph < CTL || (arp.cnt == 0xFFFFFFFFu && ph < arp.period / 4u)) { arp.cnt = ph; arp_stop_note(); arp_step(); return; }
            if (arp.cnt == 0xFFFFFFFFu) return; /* (waiting for the line) */
            arp.cnt = ph;
            arp_gate(gate);
            return;
        }
    }
    if (arp.cnt == 0xFFFFFFFFu || arp.cnt + CTL >= arp.period) {
        arp.cnt = arp.cnt == 0xFFFFFFFFu ? 0 : arp.cnt + CTL - arp.period;
        arp_stop_note();
        arp_step();
    } else {
        arp.cnt += CTL;
        arp_gate(gate);
    }
}

/* ---- the keys' events (OberVave's ev_note_on / ev_note_off, its HOLD): to the held list, then to the arp or the
 * voices */
static void kayak_events(void)
{
    uint32_t arp_on = gp_p[GQ_AMODE] != A_OFF, hold = ctl_hold, i, j;
    if (hold_was && !hold) {                    /* HOLD let go: what no key holds is released */
        for (i = j = 0; i < nheld; i++)
            if (held[i].phys) held[j++] = held[i];
            else if (!arp_on) gp_voice_off(held[i].note);
        nheld = j;
    }
    hold_was = hold;
    while (evq_r != evq_w) {
        uint32_t note = evq[evq_r % EVQ_N][0], vel = evq[evq_r % EVQ_N][1];
        evq_r++;
        if (vel) {
            if (hold) {                         /* a new chord after every key was up: the held one goes */
                for (i = 0; i < nheld && !held[i].phys; i++) ;
                if (i == nheld) {
                    if (!arp_on) for (i = 0; i < nheld; i++) gp_voice_off(held[i].note);
                    nheld = 0;
                    arp.pos = 0;
                    arp.cnt = 0xFFFFFFFFu;
                }
            }
            held_remove(note);
            if (nheld == NHELD) { for (i = 1; i < NHELD; i++) held[i - 1u] = held[i]; nheld--; }
            held[nheld].note = (uint8_t)note;
            held[nheld].vel = (uint8_t)vel;
            held[nheld].phys = 1;
            nheld++;
            if (arp_on) {
                if (nheld == 1u) { arp.pos = 0; arp.cnt = 0xFFFFFFFFu; }   /* the first key: a step right away */
            } else
                gp_voice_on(note, vel);
        } else {
            for (i = 0; i < nheld; i++)
                if (held[i].note == note) held[i].phys = 0;
            if (hold) continue;                 /* HOLD keeps it */
            held_remove(note);
            if (!arp_on) gp_voice_off(note);
        }
    }
    ui_nheld = nheld;
}

/* ---- the sequencer (OberVave's seq_tick, one track) */
static struct { uint32_t cnt, dur, step, frac, gate_at, nsnd, play, tie, rat, rk, rlen, rgate; uint8_t snd[SEQ_CHORD]; } sr;
/* rat: the ratchet's hits in the step, rk: the next one, rlen: a hit's length, rgate: its gate */
static int32_t seq_rng = 0x2545F491;
static uint32_t seq_arp(void) { return gp_p[GQ_AMODE] != A_OFF; }   /* the arp on: the steps' notes play it */
static uint32_t seq_was, seq_beat_cnt;

static void seq_release(void)
{
    uint32_t i;
    for (i = 0; i < sr.nsnd; i++) gp_voice_off(sr.snd[i]);
    sr.nsnd = 0;
    sa.n = 0;
}
static void seq_sound(const step_t *s)
{
    uint32_t i;
    if (seq_arp()) {                            /* the step's notes, as keys held, to the arp */
        uint32_t was = sa.n;
        for (i = 0; i < SEQ_CHORD && s->note[i]; i++) sa.note[i] = s->note[i];
        sa.n = (uint8_t)i;
        sa.vel = s->vel ? s->vel : 100u;
        if (!was && !nheld) { arp.pos = 0; arp.cnt = 0xFFFFFFFFu; }
        return;
    }
    for (i = 0; i < SEQ_CHORD && s->note[i] && sr.nsnd < SEQ_CHORD; i++) {
        uint32_t m = (uint32_t)tr_note(s->note[i]);
        gp_voice_on(m, s->vel ? s->vel : 100u);
        sr.snd[sr.nsnd++] = (uint8_t)m;
    }
}
static uint32_t seq_has(const step_t *s, uint32_t m)   /* the step plays note m */
{
    uint32_t i;
    for (i = 0; i < SEQ_CHORD && s->note[i]; i++)
        if ((uint32_t)tr_note(s->note[i]) == m) return 1;
    return 0;
}
/* a tie, note by note: what the new step plays again keeps sounding, the rest lets go, the new ones start */
static void seq_tie_into(const step_t *s, uint32_t play)
{
    uint32_t i, j, keep = 0;
    if (seq_arp()) {                            /* (the arp: the step's notes in place of the last) */
        if (play) seq_sound(s); else seq_release();
        return;
    }
    for (i = j = 0; i < sr.nsnd; i++) {
        if (play && seq_has(s, sr.snd[i])) { sr.snd[j++] = sr.snd[i]; keep++; }
        else gp_voice_off(sr.snd[i]);
    }
    sr.nsnd = j;
    if (!play) return;
    for (i = 0; i < SEQ_CHORD && s->note[i] && sr.nsnd < SEQ_CHORD; i++) {
        uint32_t m = (uint32_t)tr_note(s->note[i]);
        for (j = 0; j < keep && sr.snd[j] != m; j++) ;
        if (j < keep) continue;
        gp_voice_on(m, s->vel ? s->vel : 100u);
        sr.snd[sr.nsnd++] = (uint8_t)m;
    }
}
static void seq_from_top(void)                  /* step 1 in this block */
{
    seq_release();
    sr.step = 0xFFFFFFFFu;
    sr.cnt = sr.dur = sr.frac = 0;
    sr.play = sr.tie = 0;
}
static int seq_phase(uint32_t *ph) { *ph = seq_beat_cnt; return ctl_seq_run && seq_was; }
static int seq_starting(void) { return ctl_seq_run && (!seq_was || ctl_seq_restart); }

static void seq_tick(void)
{
    uint32_t q, len = (uint32_t)clamp(pat.len, 1, SEQ_MAX);
    const step_t *s;
    if (!ctl_seq_run) {
        if (seq_was) { seq_release(); seq_was = 0; ui_seq_step = 0xFFFFFFFFu; }
        return;
    }
    if (!seq_was || ctl_seq_restart) {          /* start: step 1 in this block */
        ctl_seq_restart = 0;
        seq_from_top();
        seq_beat_cnt = 0;
        seq_was = 1;
    }
    q = quarter_len();
    if (seq_beat_cnt < CTL)                     /* a beat in this block: the PLAY light blinks */
        ui_beat++;
    seq_beat_cnt += CTL;
    if (seq_beat_cnt >= q)
        seq_beat_cnt = seq_beat_cnt - q < CTL ? seq_beat_cnt - q : 0u;
    if (sr.cnt >= sr.dur) {                     /* the next step */
        uint32_t dv = DIV_PER_QUARTER[pat.div % NDIV], base = q / dv, sw;
        sr.frac += q % dv;                      /* (the rest of q spread over the steps: div steps are exactly a quarter) */
        if (sr.frac >= dv) { sr.frac -= dv; base++; }
        sr.cnt -= sr.dur;
        sr.step = sr.step + 1u >= len ? 0u : sr.step + 1u;
        sw = base * (uint32_t)(clamp(pat.swing, 50, 75) - 50) / 50u;
        sr.dur = sr.step & 1u ? base - sw : base + sw;   /* swing: the first of each pair longer */
        if (sr.dur < CTL) sr.dur = CTL;
        s = &pat.st[sr.step];
        sr.play = s->note[0] != 0;
        if (sr.play && s->chance < 100u && noise32(&seq_rng) % 100u >= s->chance)
            sr.play = 0;                        /* CHANCE: this time a rest */
        sr.rat = sr.play ? (uint32_t)clamp(s->ratch, 1, 4) : 1u;
        sr.rlen = sr.dur / sr.rat;
        sr.rk = 1;
        if (sr.tie)                             /* a tie: the notes the step plays again hold */
            seq_tie_into(s, sr.play);
        else {
            seq_release();
            if (sr.play) seq_sound(s);
        }
        sr.tie = sr.play && s->gate >= GATE_TIE && sr.rat == 1u;   /* (a ratchet: no tie) */
        sr.rgate = (uint32_t)clamp((int32_t)((sr.rlen * ((s->gate >= GATE_TIE ? 127u : s->gate) + 1u)) >> 7), CTL,
                                   (int32_t)sr.rlen);
        sr.gate_at = sr.tie ? 0xFFFFFFFFu
                   : sr.rat > 1u ? sr.rgate
                   : (uint32_t)clamp((int32_t)((sr.dur * (s->gate + 1u)) >> 7), CTL, (int32_t)sr.dur);
        ui_seq_step = sr.step;
    }
    if (sr.rk < sr.rat && sr.cnt >= sr.rk * sr.rlen) {   /* the ratchet's next hit: the step again */
        seq_release();
        seq_sound(&pat.st[sr.step % SEQ_MAX]);
        sr.gate_at = sr.rk * sr.rlen + sr.rgate;
        sr.rk++;
    }
    if ((sr.nsnd || sa.n) && sr.gate_at != 0xFFFFFFFFu && sr.cnt >= sr.gate_at)
        seq_release();
    sr.cnt += CTL;
}
