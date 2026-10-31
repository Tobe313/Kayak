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
typedef struct { uint8_t note[SEQ_CHORD], vel, gate; } step_t;   /* note 0 = none */
typedef struct { uint8_t len, div, swing, tempo; step_t st[SEQ_MAX]; } pattern_t;   /* swing 50..75 %, tempo BPM */
static pattern_t pat;                           /* the main loop edits, the audio side plays */

static void step_clear(step_t *s)
{
    uint32_t i;
    for (i = 0; i < SEQ_CHORD; i++) s->note[i] = 0;
    s->vel = 100;
    s->gate = 96;                               /* 76 % */
}
static void pat_init(pattern_t *p)              /* a bar of sixteenths, empty, 120 BPM */
{
    uint32_t i;
    p->len = 16;
    p->div = D_16;
    p->swing = 50;
    p->tempo = 120;
    for (i = 0; i < SEQ_MAX; i++) step_clear(&p->st[i]);
}

/* main loop -> audio side */
static volatile uint32_t ctl_seq_run, ctl_seq_restart, ctl_tempo_ext;   /* ext: a MIDI clock's BPM, 0 none */
/* audio side -> main loop */
static volatile uint32_t ui_seq_step = 0xFFFFFFFFu, ui_beat;

/* a quarter in frames, rounded to 48 frames (~1 ms): every step length (q / 1, 2, 3, 4, 6, 8, 12) comes out whole */
static uint32_t quarter_len(void)
{
    uint32_t bpm = ctl_tempo_ext ? ctl_tempo_ext : pat.tempo;
    return (60u * FS / (uint32_t)clamp((int32_t)bpm, 40, 240) + 24u) / 48u * 48u;
}

/* ---- the keys held (audio side): the arp plays them */
static struct { uint8_t note, vel; } held[NHELD];
static uint32_t nheld;
static void held_remove(uint32_t note)
{
    uint32_t i, j;
    for (i = j = 0; i < nheld; i++)
        if (held[i].note != note) held[j++] = held[i];
    nheld = j;
}

/* ---- the arpeggiator (OberVave's arp_step / arp_tick) */
static struct { uint32_t cnt, period, gate_at, pos, sounding, note, was_on; } arp;
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
    uint32_t i, j, n = nheld, oct = (uint32_t)clamp(gp_p[GQ_AOCT], 1, 4), len, k, mode = (uint32_t)gp_p[GQ_AMODE], idx;
    for (i = 0; i < n; i++) sorted[i] = held[i].note;
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
    arp.note = (uint32_t)clamp((int32_t)sorted[idx] + 12 * (int32_t)(k / n), 0, 127);
    for (i = 0; i < n; i++)                     /* the velocity of that key */
        if (held[i].note == sorted[idx]) break;
    gp_voice_on(arp.note, i < n ? held[i].vel : 100u);
    arp.sounding = 1;
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
    if (!nheld) { arp_stop_note(); arp.pos = 0; arp.cnt = 0xFFFFFFFFu; return; }
    q = quarter_len();
    arp.period = q / div;
    if (arp.period < CTL) arp.period = CTL;
    arp.gate_at = (arp.period * (gate + 1u)) >> 7;
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
            if (arp.sounding && arp.cnt >= arp.gate_at && gate < 127u) arp_stop_note();
            return;
        }
    }
    if (arp.cnt == 0xFFFFFFFFu || arp.cnt + CTL >= arp.period) {
        arp.cnt = arp.cnt == 0xFFFFFFFFu ? 0 : arp.cnt + CTL - arp.period;
        arp_stop_note();
        arp_step();
    } else {
        arp.cnt += CTL;
        if (arp.sounding && arp.cnt >= arp.gate_at && gate < 127u) arp_stop_note();
    }
}

/* ---- the keys' events (OberVave's ev_note_on / ev_note_off): to the held list, then to the arp or the voices */
static void kayak_events(void)
{
    while (evq_r != evq_w) {
        uint32_t note = evq[evq_r % EVQ_N][0], vel = evq[evq_r % EVQ_N][1], arp_on = gp_p[GQ_AMODE] != A_OFF, i;
        evq_r++;
        if (vel) {
            held_remove(note);
            if (nheld == NHELD) { for (i = 1; i < NHELD; i++) held[i - 1u] = held[i]; nheld--; }
            held[nheld].note = (uint8_t)note;
            held[nheld].vel = (uint8_t)vel;
            nheld++;
            if (arp_on) {
                if (nheld == 1u) { arp.pos = 0; arp.cnt = 0xFFFFFFFFu; }   /* the first key: a step right away */
            } else
                gp_voice_on(note, vel);
        } else {
            held_remove(note);
            if (!arp_on) gp_voice_off(note);
        }
    }
}

/* ---- the sequencer (OberVave's seq_tick, one track) */
static struct { uint32_t cnt, dur, step, frac, gate_at, nsnd, play, tie; uint8_t snd[SEQ_CHORD]; } sr;
static uint32_t seq_was, seq_beat_cnt;

static void seq_release(void)
{
    uint32_t i;
    for (i = 0; i < sr.nsnd; i++) gp_voice_off(sr.snd[i]);
    sr.nsnd = 0;
}
static void seq_sound(const step_t *s)
{
    uint32_t i;
    for (i = 0; i < SEQ_CHORD && s->note[i] && sr.nsnd < SEQ_CHORD; i++) {
        gp_voice_on(s->note[i], s->vel ? s->vel : 100u);
        sr.snd[sr.nsnd++] = s->note[i];
    }
}
static uint32_t seq_has(const step_t *s, uint32_t m)   /* the step plays note m */
{
    uint32_t i;
    for (i = 0; i < SEQ_CHORD && s->note[i]; i++)
        if (s->note[i] == m) return 1;
    return 0;
}
/* a tie, note by note: what the new step plays again keeps sounding, the rest lets go, the new ones start */
static void seq_tie_into(const step_t *s, uint32_t play)
{
    uint32_t i, j, keep = 0;
    for (i = j = 0; i < sr.nsnd; i++) {
        if (play && seq_has(s, sr.snd[i])) { sr.snd[j++] = sr.snd[i]; keep++; }
        else gp_voice_off(sr.snd[i]);
    }
    sr.nsnd = j;
    if (!play) return;
    for (i = 0; i < SEQ_CHORD && s->note[i] && sr.nsnd < SEQ_CHORD; i++) {
        for (j = 0; j < keep && sr.snd[j] != s->note[i]; j++) ;
        if (j < keep) continue;
        gp_voice_on(s->note[i], s->vel ? s->vel : 100u);
        sr.snd[sr.nsnd++] = s->note[i];
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
        if (sr.tie)                             /* a tie: the notes the step plays again hold */
            seq_tie_into(s, sr.play);
        else {
            seq_release();
            if (sr.play) seq_sound(s);
        }
        sr.tie = sr.play && s->gate >= GATE_TIE;
        sr.gate_at = s->gate >= GATE_TIE ? 0xFFFFFFFFu
                   : (uint32_t)clamp((int32_t)((sr.dur * (s->gate + 1u)) >> 7), CTL, (int32_t)sr.dur);
        ui_seq_step = sr.step;
    }
    if (sr.nsnd && sr.gate_at != 0xFFFFFFFFu && sr.cnt >= sr.gate_at)
        seq_release();
    sr.cnt += CTL;
}
