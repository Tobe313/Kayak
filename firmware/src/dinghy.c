/* SPDX-License-Identifier: GPL-3.0-only */
/* Kayak (a fork of Dinghy, the smallest FM-1 firmware to build your own on): a granular synth, ChoralRoot FM-1's
 * GRAIN engine (grain.c) on Dinghy's platform (platform.c: updates, the USB rescue, MIDI, USB audio, keys, knobs,
 * LEDs, the screen, flash storage; the FM-1 and the SMK-37).
 *
 *   keys       play the grains, 8 voices; the notes go out over USB-MIDI too, on the MIDI channel
 *   SELECT     the page: SRC A, SRC B (TYPE TONE MOTION SEED), GRAIN (SIZE DENS POS SPRD),
 *              GRAIN+ (MORPH RAND PITCH SCAN), FILT/ENV (CUT RES ATK REL)
 *   KNOB 1..4  the page's four values (kept in flash)
 *   OCT- OCT+  the octave (kept in flash)
 *   PRESETS    the MIDI channel: OMNI (in: all, out: 1) or 1..16 (kept in flash)
 *   MASTER     the volume (platform.c)
 *   MIDI in    notes from USB or the TRS jack play the grains, with their velocity; a MIDI clock shows its tempo
 *              and blinks PLAY on the beat, START / STOP light it
 *   SMK-37     the keys with velocity, the outer keys too, the pads (white keys F3..G5, lit while they sound), the
 *              pitch wheel bends +-2 semitones
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

/* ---- the pages: five of four knobs */
#define NPAGE 5u
static const char *const PAGE_NAME[NPAGE] = {"SRC A", "SRC B", "GRAIN", "GRAIN+", "FILT/ENV"};

/* ---- the screen: the page, the wave the grains read with the grains on it, the four values, the octave */
#define C_ACC RGB(255, 140, 26)                 /* orange, as the update loader's screen */
#define C_SRC_A RGB(70, 150, 255)
#define C_OFF RGB(90, 90, 90)
#define C_DIMBAR RGB(45, 45, 45)
#define C_KEYW RGB(210, 210, 210)
#define KEY0 53u                                /* key 0 = F3 */
#define WV_Y 70u                                /* the wave band: 240 x 64 */
#define CELL_Y 142u                             /* the four values: 240 x 56 */

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
static void draw_wave(uint32_t pg)
{
    uint32_t x, k;
    uint16_t col = pg == 0u ? C_SRC_A : pg == 1u ? C_ACC : C_KEYW;
    cv_begin(240, 64, C_BLACK);
    if (pg >= 2u) {                             /* where the grains start: POS (moved by SCAN), SPRD round it */
        int32_t c = (int32_t)((((uint32_t)(gp_p[GQ_POS] << 8) + (gp_scan >> 17)) % (127u * 256u + 1u)) * 239u / (127u << 8));
        int32_t s = gp_p[GQ_SPRD] * 240 / 127;
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
    cv_blit(0, WV_Y);
}

static void fmt_int(char *s, int32_t v, int sign)
{
    char t[8];
    uint32_t n = 0, u = (uint32_t)(v < 0 ? -v : v);
    if (v < 0) *s++ = '-';
    else if (sign && v > 0) *s++ = '+';
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) *s++ = t[--n];
    *s = 0;
}
static void draw_cells(uint32_t pg)
{
    uint32_t c;
    cv_begin(240, 56, C_BLACK);
    for (c = 0; c < 4u; c++) {
        uint32_t q = pg * 4u + c;
        int32_t x0 = (int32_t)c * 60, v = gp_p[q], w;
        char s[8];
        const char *val = s;
        if (q == GQ_TYPE_A || q == GQ_TYPE_B) val = GS_TYPE_NAMES[v];
        else fmt_int(s, v, q == GQ_PITCH || q == GQ_SCAN);
        cv_text(x0 + (60 - text_w(&FONT_S, GQ[q].name)) / 2, 2, &FONT_S, GQ[q].name, C_OFF);
        cv_text(x0 + (60 - text_w(&FONT_S, val)) / 2, 22, &FONT_S, val, C_WHITE);
        w = (v - GQ[q].min) * 48 / (GQ[q].max - GQ[q].min);
        cv_rect(x0 + 6, 46, 48, 6, C_DIMBAR);
        cv_rect(x0 + 6, 46, w, 6, c & 1u ? C_ACC : C_SRC_A);
    }
    cv_blit(0, CELL_Y);
}

static void draw_status(int32_t oct, uint32_t ch)
{
    char s[24] = "OCT +0  MIDI OMNI";
    s[4] = oct < 0 ? '-' : '+';
    s[5] = (char)('0' + (oct < 0 ? -oct : oct));
    if (ch) {
        s[13] = 'C'; s[14] = 'H'; s[15] = (char)(ch < 10u ? ' ' : '1'); s[16] = (char)('0' + ch % 10u);
    }
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

/* ---- MIDI in: from the main loop, and from app_idle while the screen draws (idle_ok) */
static struct { uint8_t oct, ch, page; int8_t p[GQ_N]; } set;   /* what survives a power-off (up to SECTOR_MAX bytes) */
static uint32_t idle_ok, midi_src, clk_n, clk_t0, clk_bpm, clk_run, clk_beat_ms;

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
        if (st == 0xFAu) { clk_run = 1; clk_n = 0; continue; }   /* START: the next clock is beat 1 */
        if (st == 0xFBu) { clk_run = 1; continue; }              /* CONTINUE */
        if (st == 0xFCu) { clk_run = 0; continue; }              /* STOP */
        if (set.ch && (st & 15u) != set.ch - 1u) continue;      /* not our channel */
        if (ty == 0x90u && d2) { gp_note_on(d1, d2); midi_src = (uint32_t)src; }
        else if (ty == 0x80u || ty == 0x90u) gp_note_off(d1);
    }
}
static void app_idle(void)
{
    if (idle_ok) midi_in();
}

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, status_drawn = ~0u, midi_drawn = ~0u;
    uint32_t page_drawn = ~0u, cells_drawn = ~0u, wave_page = ~0u, wave_renders = ~0u, wave_morph = ~0u;
    uint32_t wave_t = 0, wave_live = 1, wave_pos = ~0u;
    uint8_t held[27] = {0};                     /* the note each key plays */
#if BOARD_SMK37
    static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* white key -> key */
    uint8_t pad_note[16] = {0}, ext_note[FM1_NKEY] = {0};
    smk_pw_t wheel = {0};
    smk_pad_events = 1;                         /* the pads as events (smk_pad_take): they play here, lit while they sound */
#endif
    gp_defaults();
    if (settings_load(&set, sizeof set) || (int8_t)set.oct < -2 || (int8_t)set.oct > 2 || set.ch > 16u || set.page >= NPAGE)
        set.oct = 0, set.ch = 0, set.page = 0;
    else
        for (k = 0; k < GQ_N; k++)              /* (a value out of its range: its default) */
            gp_p[k] = set.p[k] < GQ[k].min || set.p[k] > GQ[k].max ? GQ[k].def : set.p[k];

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 6, 240, &FONT_S, APP_NAME "  GRAIN  v" APP_VER, C_ACC, 1);

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, out_ch = set.ch ? set.ch - 1u : 0u, st, pg = set.page;
        int32_t oct = (int8_t)set.oct, e;
        uint32_t base = (uint32_t)((int32_t)KEY0 + 12 * oct);
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {                 /* (a key stops the note it started: the octave may change meanwhile) */
#if BOARD_SMK37
            uint32_t vel = fm1_in.key_vel[SMK_KEY_F3 + k];   /* the SMK's keys feel the velocity */
#else
            uint32_t vel = 100;
#endif
            if (down >> k & 1u) { held[k] = (uint8_t)(base + k); gp_note_on(held[k], vel); midi_send(0x90u | out_ch, held[k], vel); }
            if (up >> k & 1u) { gp_note_off(held[k]); midi_send(0x80u | out_ch, held[k], 0); }
        }
#if BOARD_SMK37
        while ((e = smk_extkey_take()) >= 0) {  /* the keys left and right of the FM-1's 27: key << 8 | velocity */
            uint32_t key = (uint32_t)e >> 8, vel = (uint32_t)e & 0x7Fu;
            if (key >= FM1_NKEY) continue;
            if (vel) { ext_note[key] = (uint8_t)(base + key - SMK_KEY_F3); gp_note_on(ext_note[key], vel); midi_send(0x90u | out_ch, ext_note[key], vel); }
            else if (ext_note[key]) { gp_note_off(ext_note[key]); midi_send(0x80u | out_ch, ext_note[key], 0); ext_note[key] = 0; }
        }
        while ((e = smk_pad_take()) >= 0) {     /* a pad: hardware pad << 8 | velocity (0 = let go) */
            uint32_t pad = ((uint32_t)e >> 8) & 15u, vel = (uint32_t)e & 0x7Fu;
            uint32_t w = pad < 8u ? pad + 8u : pad - 8u;   /* bottom row = white keys 1-8, top row 9-16 */
            if (vel) { pad_note[pad] = (uint8_t)(base + WHITE[w]); gp_note_on(pad_note[pad], vel); midi_send(0x90u | out_ch, pad_note[pad], vel); }
            else if (pad_note[pad]) { gp_note_off(pad_note[pad]); midi_send(0x80u | out_ch, pad_note[pad], 0); pad_note[pad] = 0; }
        }
        {
            int32_t bd = smk_pw_bend(&wheel, fm1_in.wheel[0], fm1_ms);   /* 1/4096 semitone */
            gp_bend_q16 = (uint32_t)(65536 + (bd * 3785 >> 12) + (((bd * bd) >> 12) * 109 >> 12));   /* ~2^(s/12) */
        }
#endif

        b = fm1_input_edges(&rel);              /* buttons pressed since the last call (bit = button id) */
        if (b & (1u << BTN_OCT_DOWN) && oct > -2) { set.oct = (uint8_t)(oct - 1); dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && oct < 2) { set.oct = (uint8_t)(oct + 1); dirty = 1; t_change = fm1_ms; }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {   /* detents turned since the last call */
            int32_t c = (int32_t)set.ch + e;
            set.ch = (uint8_t)(c < 0 ? 0 : c > 16 ? 16 : c);
            dirty = 1;
            t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) {    /* the page */
            int32_t c = (int32_t)set.page + e;
            set.page = (uint8_t)(c < 0 ? 0 : c >= (int32_t)NPAGE ? NPAGE - 1u : (uint32_t)c);
            pg = set.page;
            dirty = 1;
            t_change = fm1_ms;
        }
        for (k = 0; k < 4u; k++)                /* the page's values: a fast turn moves four a detent */
            if ((e = fm1_enc_take(ENC_KNOB1 + k)) != 0) {
                uint32_t q = pg * 4u + k;
                int32_t v = gp_p[q] + (e > 2 || e < -2 ? e * 4 : e), lo = GQ[q].min, hi = GQ[q].max;
                if (q == GQ_TYPE_A || q == GQ_TYPE_B)
                    v = (int32_t)(((uint32_t)(gp_p[q] + e + 8 * GS_NTYPE)) % GS_NTYPE);   /* the TYPE: round the eight */
                gp_p[q] = (int8_t)(v < lo ? lo : v > hi ? hi : v);
                dirty = 1;
                t_change = fm1_ms;
            }

        midi_in();
        if (clk_bpm && fm1_ms - clk_t0 > 2000u) clk_bpm = 0, clk_run = 0;   /* the clock has stopped coming */
        grain_poll();                           /* a slice of a render, if one is due */

        oct = (int8_t)set.oct;
        base = (uint32_t)((int32_t)KEY0 + 12 * oct);
        for (k = 0; k < GP_NV; k++)             /* what sounds, on the keys of this octave */
            if (gpv[k].gate && gpv[k].note >= base && gpv[k].note < base + 27u)
                lit |= 1u << (gpv[k].note - base);
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, oct < 0);
        fm1_led_key(BTN_OCT_UP, oct > 0);
        fm1_led_key(BTN_PLAY, clk_run || (clk_bpm && fm1_ms - clk_beat_ms < 80u));   /* lit: running; a blink a beat */
#if BOARD_SMK37
        for (k = 0; k < 16u; k++) {             /* a pad glows orange while its key sounds */
            uint32_t w = k < 8u ? k + 8u : k - 8u, on = lit >> WHITE[w] & 1u;
            padled_set(k, on ? 128 : 8, on ? 70 : 4, on ? 10 : 1);
        }
        padled_flush();                         /* (sends only what changed) */
#endif

        idle_ok = 1;                            /* the drawing below may read MIDI meanwhile (LCD_IDLE) */
        if (pg != page_drawn) { page_drawn = pg; draw_text_box(0, 28, 240, &FONT_L, PAGE_NAME[pg], C_WHITE, 1); }
        {
            uint32_t sum = pg, live = 0;        /* the page's values, as one number to compare */
            for (k = 0; k < 4u; k++) sum = sum * 131u + (uint8_t)gp_p[pg * 4u + k];
            if (sum != cells_drawn) { cells_drawn = sum; draw_cells(pg); }
            for (k = 0; k < GP_NG; k++) live |= gpg[k].owner;
            st = (uint32_t)(uint8_t)gp_p[GQ_MORPH];
            if (pg != wave_page || grs.renders != wave_renders || (pg >= 2u && st != wave_morph)) {
                wave_page = pg; wave_renders = grs.renders; wave_morph = st;
                wave_take(pg);
                wave_live = 1;
            }
            st = (uint32_t)(uint8_t)gp_p[GQ_POS] | (uint32_t)(uint8_t)gp_p[GQ_SPRD] << 8;
            if (st != wave_pos) wave_pos = st, wave_live = 1;
            if (live || gp_p[GQ_SCAN]) wave_live |= fm1_ms - wave_t >= 50u;   /* the grains move: 20 frames a second */
            if (wave_live) { wave_live = 0; wave_t = fm1_ms; draw_wave(pg); }
        }
        if ((st = (uint32_t)(oct + 2) | (uint32_t)set.ch << 4) != status_drawn) { status_drawn = st; draw_status(oct, set.ch); }
        if ((st = midi_src | clk_bpm << 2 | clk_run << 12) != midi_drawn) { midi_drawn = st; draw_midi(midi_src, clk_bpm, clk_run); }
        idle_ok = 0;

        if (dirty && gp_quiet() && !grs.busy && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent */
            for (k = 0; k < GQ_N; k++) set.p[k] = gp_p[k];
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
