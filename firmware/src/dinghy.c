/* SPDX-License-Identifier: GPL-3.0-only */
/* Kayak (a fork of Dinghy, the smallest FM-1 firmware to build your own on): a granular synth, ChoralRoot FM-1's
 * GRAIN engine (grain.c) on Dinghy's platform (platform.c: updates, the USB rescue, MIDI, USB audio, keys, knobs,
 * LEDs, the screen, flash storage; the FM-1 and the SMK-37), with OberVave's filter, effects, arpeggiator and
 * sequencer.
 *
 *   keys       play the grains, 8 voices; the notes go out over USB-MIDI too, on the MIDI channel
 *   SELECT     the page: SRC A, SRC B (TYPE TONE MOVE SEED), GRAIN (SIZE DENS POS SPRD),
 *              GRAIN+ (MORPH RAND PITCH SCAN), FILTER (TYPE CUT RES DRIVE: OberVave's), FILTER+ (KTRK SEM),
 *              ENV (ATK DEC SUS REL), LFO (RATE WAVE DEPTH DEST); ARP (MODE DIV OCT GATE) by the ARP button only
 *   KNOB 1..4  the page's four values; the faster the turn, the bigger the step
 *   FX         the effects' pages and back (lit there): CHORUS (RATE DEPTH MIX), DELAY (TIME FDBK TONE MIX),
 *              REVERB (SIZE DAMP PRE MIX), DRIVE (TYPE AMNT TONE MIX): OberVave's; BASS (LEVEL DETUN OCT): an
 *              analog bass under the grains, two detuned saws through the filter and the effects
 *   EDIT       SRC A, SRC B, GRAIN, GRAIN+ (where it was left; pressed again: the next of them)
 *   SEL        FILTER, FILTER+ (the same way)
 *   ENV LFO    straight to their page
 *   ARP        the arpeggiator on (lit) or off, and its page
 *   SEQ        the step sequencer's screen (lit): the keys write the step under the cursor (a chord: keys held
 *              together) and move on, REC writes a rest, SELECT moves the cursor, KNOB 1..4 TEMPO LENGTH DIV SWING
 *   PLAY       the sequencer runs (lit, a blink a beat) or stops; MIDI START / STOP too, a MIDI clock sets the tempo
 *   HOME       the screen after the splash; a short press: the wave and an oscilloscope of the output; a knob or SELECT goes back to the pages
 *   PRESETS    the sound: the 8 factory presets, then the user presets saved; MIDI program change too
 *   SAVE       save the sound: PRESETS picks the user slot (1..16), SAVE again writes it, HOME cancels
 *   ALGORITHM  the MIDI channel: OMNI (in: all, out: 1) or 1..16
 *   OCT- OCT+  the octave
 *   MASTER     the volume (platform.c)
 *   SMK-37     the keys with velocity, the outer keys too, the pads (white keys F3..G5, lit while they sound), the
 *              pitch wheel bends +-2 semitones
 * The buttons that do something glow; they light fully while what they do is on. The sound being played, the octave,
 * the channel and the page are kept in flash (2 s after a change, when silent), the sequence too (when stopped).
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

/* ---- the pages: eight of the sound, then four of the effects (FX goes there and back), four knobs each */
#define NPAGE 14u
#define FX_PAGE0 9u
#define PG_SRC_A 0u                             /* EDIT: SRC A, SRC B, GRAIN, GRAIN+ */
#define PG_GRAIN2 3u
#define PG_FILTER 4u                            /* SEL: FILTER, FILTER+ */
#define PG_FILTER2 5u
#define PG_ENV 6u
#define PG_LFO 7u
#define PG_ARP 8u                               /* (the last of the sound's: SELECT stops before it, ARP opens it) */
#define QX 0xFFu                                /* a page's empty place */
static const uint8_t PAGE_Q[NPAGE][4] = {      /* the knobs of each page */
    {GQ_TYPE_A, GQ_TONE_A, GQ_MOTION_A, GQ_SEED_A}, {GQ_TYPE_B, GQ_TONE_B, GQ_MOTION_B, GQ_SEED_B},
    {GQ_SIZE, GQ_DENS, GQ_POS, GQ_SPRD}, {GQ_MORPH, GQ_RAND, GQ_PITCH, GQ_SCAN},
    {GQ_FTYPE, GQ_CUT, GQ_RES, GQ_FDRIVE}, {GQ_KTRK, GQ_FMORPH, QX, QX}, {GQ_ATK, GQ_DEC, GQ_SUS, GQ_REL},
    {GQ_LRATE, GQ_LWAVE, GQ_LDEPTH, GQ_LDEST}, {GQ_AMODE, GQ_ADIV, GQ_AOCT, GQ_AGATE},
    {GQ_CRATE, GQ_CDEPTH, GQ_CMIX, QX}, {GQ_DTIME, GQ_DFB, GQ_DTONE, GQ_DMIX}, {GQ_RSIZE, GQ_RDAMP, GQ_RPRE, GQ_RMIX},
    {GQ_VTYPE, GQ_VAMT, GQ_VTONE, GQ_VMIX}, {GQ_BVOL, GQ_BDET, GQ_BOCT, QX}};
static const char *const PAGE_NAME[NPAGE] = {"SRC A", "SRC B", "GRAIN", "GRAIN+", "FILTER", "FILTER+", "ENV", "LFO", "ARP",
                                             "FX CHORUS", "FX DELAY", "FX REVERB", "FX DRIVE", "FX BASS"};
static const uint8_t OLD_N[2] = {GQ_N_V041, GQ_N_V04};   /* the values a sound had in 0.41, in 0.4: read again */
enum { V_PAGES, V_HOME, V_SEQ };                /* what the screen shows */

/* ---- the presets: the factory ones (grain.c), then NUSER the user saves (blob 0) */
#define NUSER 16u
#define NPRESET (GP_NFACT + NUSER)
static struct { uint8_t used[NUSER]; int8_t p[NUSER][GQ_N]; } user;
static uint32_t preset_ok(uint32_t i) { return i < GP_NFACT || (i < NPRESET && user.used[i - GP_NFACT]); }
static void preset_load(uint32_t i)
{
    const int8_t *src = i < GP_NFACT ? GP_FACT[i] : user.p[i - GP_NFACT];
    uint32_t k;
    for (k = 0; k < GQ_N; k++)
        gp_p[k] = (int8_t)clamp(src[k], GQ[k].min, GQ[k].max);
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
static void draw_wave(uint32_t pg)
{
    uint32_t x, k;
    uint16_t col = pg == 0u ? C_SRC_A : pg == 1u ? C_ACC : C_KEYW;
    cv_begin(240, 64, C_BLACK);
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
    cv_blit(0, WV_Y);
}

/* HOME: the output as an oscilloscope, 240 frames (5.4 ms) from a rising zero crossing, scaled to its own peak */
static void draw_scope(void)
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
        int32_t y = 32 - s[t + (uint32_t)x] * 30 / pk;
        if (x) cv_line(x - 1, y0, x, y, C_ACC);
        y0 = y;
    }
    cv_blit(0, SCOPE_Y);
}

static void draw_cells(uint32_t pg)
{
    uint32_t c;
    cv_begin(240, 56, C_BLACK);
    for (c = 0; c < 4u; c++) {
        uint32_t q = PAGE_Q[pg][c];
        int32_t x0 = (int32_t)c * 60, v = gp_p[q], w;
        char s[12];
        if (q == QX) continue;                  /* (a page with fewer knobs) */
        gq_text(q, v, s);
        cv_text(x0 + (60 - text_w(&FONT_S, GQ[q].name)) / 2, 2, &FONT_S, GQ[q].name, C_OFF);
        cv_text(x0 + (60 - text_w(&FONT_S, s)) / 2, 22, &FONT_S, s, C_WHITE);
        w = (v - GQ[q].min) * 48 / (GQ[q].max - GQ[q].min);
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

static void note_name(uint32_t n, char **p)     /* "C4", "F#2" */
{
    static const char *const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    gq_put_str(p, NAME[n % 12u]);
    if (n < 12u) *(*p)++ = '-';
    else gq_put_dec(p, n / 12u - 1u);
}

/* HOME's line: the arp, the sequencer, the tempo */
static void draw_home_line(uint32_t run, uint32_t bpm)
{
    char s[40], *p = s;
    gq_put_str(&p, "ARP ");
    gq_put_str(&p, ARP_NAMES[clamp(gp_p[GQ_AMODE], 0, NARPMODE - 1)]);
    gq_put_str(&p, run ? "  PLAY " : "  STOP ");     /* (at most 24 characters: 240 px of FONT_S) */
    gq_put_dec(&p, bpm);
    gq_put_str(&p, " BPM");
    *p = 0;
    draw_text_box(0, 204, 240, &FONT_S, s, C_OFF, 1);
}

/* SEQ: the steps of the cursor's bar of 16, 4 x 4 cells of 60 x 32: the note (a chord: its first and a +), the cursor
 * framed, the step playing filled, the steps past the length dark */
static void draw_seq(uint32_t cur, uint32_t play)
{
    uint32_t half, r, c, b0 = cur / 16u * 16u, len = (uint32_t)clamp(pat.len, 1, SEQ_MAX);
    for (half = 0; half < 2u; half++) {
        cv_begin(240, 64, C_BLACK);
        for (r = 0; r < 2u; r++)
            for (c = 0; c < 4u; c++) {
                uint32_t i = b0 + (half * 2u + r) * 4u + c;
                int32_t x0 = (int32_t)c * 60, y0 = (int32_t)r * 32;
                char s[8], *p = s;
                const step_t *st = &pat.st[i % SEQ_MAX];
                if (i >= len) { cv_rect(x0 + 2, y0 + 2, 56, 28, RGB(20, 20, 20)); continue; }
                cv_rect(x0 + 2, y0 + 2, 56, 28, i == play ? RGB(90, 50, 10) : C_DIMBAR);
                if (i == cur) {
                    cv_rect(x0 + 1, y0 + 1, 58, 2, C_ACC); cv_rect(x0 + 1, y0 + 29, 58, 2, C_ACC);
                    cv_rect(x0 + 1, y0 + 1, 2, 30, C_ACC); cv_rect(x0 + 57, y0 + 1, 2, 30, C_ACC);
                }
                if (st->note[0]) { note_name(st->note[0], &p); if (st->note[1]) *p++ = '+'; }
                else *p++ = '-';
                *p = 0;
                cv_text(x0 + (60 - text_w(&FONT_S, s)) / 2, y0 + 8, &FONT_S, s, st->note[0] ? C_WHITE : C_OFF);
            }
        cv_blit(0, WV_Y + half * 64u);
    }
}
static void draw_seq_lines(uint32_t bpm)        /* the knobs: TEMPO LENGTH, DIV SWING */
{
    char s[40], *p = s;
    gq_put_dec(&p, bpm);
    gq_put_str(&p, " BPM   LENGTH ");
    gq_put_dec(&p, pat.len);
    *p = 0;
    draw_text_box(0, 204, 240, &FONT_S, s, C_OFF, 1);
    p = s;
    gq_put_str(&p, DIV_NAMES[pat.div % NDIV]);
    gq_put_str(&p, "   SWING ");
    gq_put_dec(&p, pat.swing);
    *p++ = '%';
    *p = 0;
    draw_text_box(0, 222, 240, &FONT_S, s, C_OFF, 1);
}

/* ---- the splash: a kayaker in pixel art, 48 x 28, each pixel a 4 x 4 square, one colour on black */
#define ART_W 48u
#define ART_H 28u
#define ART_PX 4u
static const char *const KAYAK_ART[ART_H] = {
    "..........................................####..",
    "..........................###............#####..",
    ".........................#####..........#####...",
    ".........................#####.........#####....",
    ".........................#####........#####.....",
    ".........................#####.......#####......",
    "..........................###.....###...........",
    "..................................###...........",
    ".........................####...###.............",
    ".........................#########..............",
    "........................#########...............",
    "........................#######.................",
    "........................######..................",
    "........................#####...................",
    ".......................#####..................#.",
    ".......................#####...........#######..",
    "......................######.....############...",
    "....................########################....",
    "...................#######################......",
    "..............###########################.......",
    ".......#############################............",
    ".#############################..................",
    "...#####################........................",
    "........#######.................................",
    "..........####..................................",
    ".........#####..................................",
    "........#####...................................",
    "........####....................................",
};
static void splash(void)
{
    uint32_t half, r, x, x0 = (240u - ART_W * ART_PX) / 2u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    for (half = 0; half < 2u; half++) {         /* two strips of 14 rows (240 x 56: the canvas) */
        cv_begin(240, ART_H / 2u * ART_PX, C_BLACK);
        for (r = 0; r < ART_H / 2u; r++)
            for (x = 0; x < ART_W; x++)
                if (KAYAK_ART[half * ART_H / 2u + r][x] == '#')
                    cv_rect((int32_t)(x0 + x * ART_PX), (int32_t)(r * ART_PX), ART_PX, ART_PX, C_ACC);
        cv_blit(0, 24u + half * ART_H / 2u * ART_PX);
    }
    cv_begin(240, 12, C_BLACK);                 /* the water: two rows of dashes, offset */
    for (x = 0; x < 240u / 16u; x++) {
        cv_rect((int32_t)(x * 16u + 4u), 0, 8, ART_PX, C_ACC);
        cv_rect((int32_t)(x * 16u + 12u), 8, 8, ART_PX, C_ACC);
    }
    cv_blit(0, 24u + ART_H * ART_PX + 6u);
    draw_text_box(0, 172, 240, &FONT_L, "KAYAK", C_ACC, 1);
    draw_text_box(0, 214, 240, &FONT_S, "GRAIN SYNTH  v" APP_VER, C_ACC_DIM, 1);
}

/* ---- the LEDs: a button that does something glows, it lights while what it does is on */
static void led_fn(uint32_t id, uint32_t on)
{
    fm1_led_key(id, on);
    fm1_led_key_glow(id, !on);
}

/* ---- MIDI in: from the main loop, and from app_idle while the screen draws (idle_ok) */
static struct { uint8_t oct, ch, page, cur, mod; int8_t p[GQ_N]; } set;   /* what survives a power-off */
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

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, status_drawn = ~0u, midi_drawn = ~0u, preset_drawn = ~0u;
    uint32_t page_drawn = ~0u, cells_drawn = ~0u, wave_page = ~0u, wave_renders = ~0u, wave_morph = ~0u;
    uint32_t wave_t = 0, wave_live = 1, wave_pos = ~0u, saving = 0, save_to = 0, save_drawn = ~0u;
    uint32_t sound_pg = 0, fx_pg = FX_PAGE0;    /* the page left on either side of FX */
    uint32_t edit_pg = PG_SRC_A, filt_pg = PG_FILTER;   /* the page left in either group */
    uint32_t view = V_HOME, view_drawn = ~0u, t_home = 0, scope_t = 0, line_drawn = ~0u, seq_drawn = ~0u, seq_t = 0;
    uint32_t cursor = 0, step_in = 0, pat_dirty = 0, arp_last = A_UP, beat_seen = 0, beat_t = 0;
    uint8_t held[27] = {0};                     /* the note each key plays */
#if BOARD_SMK37
    static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* white key -> key */
    uint8_t pad_note[16] = {0}, ext_note[FM1_NKEY] = {0};
    smk_pw_t wheel = {0};
    smk_pad_events = 1;                         /* the pads as events (smk_pad_take): they play here, lit while they sound */
#endif
    if (blob_load(0, &user, sizeof user)) {     /* the user presets; 0.41's and 0.4's (fewer values a sound) read again,
                                                 * the values they did not have at their defaults */
        static uint8_t raw[NUSER + NUSER * GQ_N];
        uint32_t o, j;
        for (k = 0; k < NUSER; k++) user.used[k] = 0;
        for (o = 0; o < 2u; o++)
            if (!blob_load(0, raw, NUSER + NUSER * (uint32_t)OLD_N[o])) {
                for (k = 0; k < NUSER; k++) {
                    user.used[k] = raw[k];
                    for (j = 0; j < GQ_N; j++) user.p[k][j] = j < OLD_N[o] ? (int8_t)raw[NUSER + k * OLD_N[o] + j] : GQ[j].def;
                }
                break;
            }
    }
    if (blob_load(1, &pat, sizeof pat) || pat.len < 1u || pat.len > SEQ_MAX || pat.div >= NDIV)   /* the sequence */
        pat_init(&pat);
    k = !settings_load(&set, sizeof set);
    if (!k) {                                   /* 0.41's or 0.4's settings: their sound again (0.4: the pages from the first) */
        static uint8_t raw[5u + GQ_N];
        uint32_t o, j;
        for (o = 0; o < 2u && !k; o++)
            if (!settings_load(raw, 5u + OLD_N[o])) {
                set.oct = raw[0], set.ch = raw[1], set.page = o ? 0u : raw[2], set.cur = raw[3], set.mod = raw[4];
                for (j = 0; j < GQ_N; j++) set.p[j] = j < OLD_N[o] ? (int8_t)raw[5u + j] : GQ[j].def;
                k = 1;
            }
    }
    if (!k || (int8_t)set.oct < -2 || (int8_t)set.oct > 2 || set.ch > 16u
        || set.page >= NPAGE || !preset_ok(set.cur)) {
        set.oct = 0, set.ch = 0, set.page = 0, set.cur = 0, set.mod = 0;
        preset_load(0);
    } else
        for (k = 0; k < GQ_N; k++)              /* (a value out of its range: its default) */
            gp_p[k] = set.p[k] < GQ[k].min || set.p[k] > GQ[k].max ? GQ[k].def : set.p[k];

    splash();                                   /* 1.5 s, while the two sources render */
    for (k = fm1_ms; fm1_ms - k < 1500u;) {
        platform_poll();
        grain_poll();
    }
    lcd_fill(0, 0, 240, 240, C_BLACK);

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, out_ch = set.ch ? set.ch - 1u : 0u, st, pg = set.page, bpm;
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
            if (down >> k & 1u) {
                held[k] = (uint8_t)(base + k);
                gp_note_on(held[k], vel);
                midi_send(0x90u | out_ch, held[k], vel);
                if (view == V_SEQ && !saving) {  /* SEQ: the step under the cursor; keys held together make a chord */
                    step_t *s = &pat.st[cursor % SEQ_MAX];
                    uint32_t i;
                    if (!step_in) { step_clear(s); step_in = 1; }
                    for (i = 0; i < SEQ_CHORD && s->note[i] && s->note[i] != held[k]; i++) ;
                    if (i < SEQ_CHORD && !s->note[i]) s->note[i] = held[k];
                    s->vel = (uint8_t)vel;
                    pat_dirty = 1;
                    t_change = fm1_ms;
                }
            }
            if (up >> k & 1u) { gp_note_off(held[k]); midi_send(0x80u | out_ch, held[k], 0); }
        }
        if (step_in && !keys) {                 /* every key of the chord up: the next step */
            step_in = 0;
            cursor = (cursor + 1u) % (uint32_t)clamp(pat.len, 1, SEQ_MAX);
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
        if (b & (1u << BTN_HOME)) t_home = fm1_ms;
        if (b & (1u << BTN_PLAY)) {             /* the sequencer: from the top, or stop */
            if (ctl_seq_run) ctl_seq_run = 0;
            else { ctl_seq_restart = 1; ctl_seq_run = 1; }
        }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {   /* the MIDI channel */
            int32_t c = (int32_t)set.ch + e;
            set.ch = (uint8_t)(c < 0 ? 0 : c > 16 ? 16 : c);
            dirty = 1;
            t_change = fm1_ms;
        }
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
            if (b & (1u << BTN_SEQ))
                view = view == V_SEQ ? V_PAGES : V_SEQ;
            if (b & (1u << BTN_ARP)) {          /* the arp on (as it was last) or off, and its page */
                if (gp_p[GQ_AMODE] != A_OFF) { arp_last = (uint32_t)gp_p[GQ_AMODE]; gp_p[GQ_AMODE] = A_OFF; }
                else gp_p[GQ_AMODE] = (int8_t)arp_last;
                view = V_PAGES;
                set.page = (uint8_t)PG_ARP;
                set.mod = 1;
                dirty = 1;
                t_change = fm1_ms;
            }
            if (b & (1u << BTN_EDIT)) {         /* EDIT: the sources and the grains, where it was left; again: the next */
                set.page = (uint8_t)(view == V_PAGES && pg <= PG_GRAIN2 ? (pg >= PG_GRAIN2 ? PG_SRC_A : pg + 1u) : edit_pg);
                view = V_PAGES; dirty = 1; t_change = fm1_ms;
            }
            if (b & (1u << BTN_SEL)) {          /* SEL: the filter's two pages, the same way */
                set.page = (uint8_t)(view == V_PAGES && (pg == PG_FILTER || pg == PG_FILTER2) ? (pg == PG_FILTER ? PG_FILTER2 : PG_FILTER) : filt_pg);
                view = V_PAGES; dirty = 1; t_change = fm1_ms;
            }
            if (b & (1u << BTN_ENV)) { view = V_PAGES; set.page = (uint8_t)PG_ENV; dirty = 1; t_change = fm1_ms; }
            if (b & (1u << BTN_LFO)) { view = V_PAGES; set.page = (uint8_t)PG_LFO; dirty = 1; t_change = fm1_ms; }
            if (b & (1u << BTN_FX)) {           /* FX: to the effects' pages and back, each where it was left */
                if (view != V_PAGES) { if (set.page < FX_PAGE0) { sound_pg = set.page; set.page = (uint8_t)fx_pg; } }
                else if (set.page < FX_PAGE0) { sound_pg = set.page; set.page = (uint8_t)fx_pg; }
                else { fx_pg = set.page; set.page = (uint8_t)sound_pg; }
                view = V_PAGES;
                dirty = 1;
                t_change = fm1_ms;
            }
            pg = set.page;
            if (view == V_SEQ) {                /* SEQ: the cursor, a rest, TEMPO LENGTH DIV SWING */
                uint32_t len = (uint32_t)clamp(pat.len, 1, SEQ_MAX);
                if ((e = fm1_enc_take(ENC_SELECT)) != 0)
                    cursor = (uint32_t)(((int32_t)cursor + e % (int32_t)len + (int32_t)len)) % len;
                if (b & (1u << BTN_REC)) {
                    step_clear(&pat.st[cursor % SEQ_MAX]);
                    cursor = (cursor + 1u) % len;
                    pat_dirty = 1;
                    t_change = fm1_ms;
                }
                if ((e = fm1_enc_take(ENC_KNOB1)) != 0) { pat.tempo = (uint8_t)clamp(pat.tempo + enc_fast(0, e), 40, 240); pat_dirty = 1; t_change = fm1_ms; }
                if ((e = fm1_enc_take(ENC_KNOB2)) != 0) { pat.len = (uint8_t)clamp(pat.len + e, 1, SEQ_MAX); cursor %= pat.len; pat_dirty = 1; t_change = fm1_ms; }
                if ((e = fm1_enc_take(ENC_KNOB3)) != 0) { pat.div = (uint8_t)clamp(pat.div + e, 0, NDIV - 1); pat_dirty = 1; t_change = fm1_ms; }
                if ((e = fm1_enc_take(ENC_KNOB4)) != 0) { pat.swing = (uint8_t)clamp(pat.swing + e, 50, 75); pat_dirty = 1; t_change = fm1_ms; }
            } else {
                if ((e = fm1_enc_take(ENC_SELECT)) != 0) {    /* the page, among the sound's or the effects' */
                    int32_t c = (int32_t)set.page + e, lo = pg < FX_PAGE0 ? 0 : (int32_t)FX_PAGE0;
                    int32_t hi = pg < FX_PAGE0 ? (int32_t)PG_ARP - 1 : (int32_t)NPAGE - 1;   /* (not ARP: its button) */
                    if (view != V_PAGES) c = (int32_t)set.page;   /* (from HOME: back to the page first) */
                    set.page = (uint8_t)(c < lo ? lo : c > hi ? hi : c);
                    pg = set.page;
                    view = V_PAGES;
                    dirty = 1;
                    t_change = fm1_ms;
                }
                for (k = 0; k < 4u; k++)        /* the page's values */
                    if ((e = fm1_enc_take(ENC_KNOB1 + k)) != 0) {
                        uint32_t q = PAGE_Q[pg][k];
                        int32_t v, lo, hi, fmt;
                        view = V_PAGES;
                        if (q == QX) continue;
                        lo = GQ[q].min, hi = GQ[q].max, fmt = GQ[q].fmt;
                        if (fmt == GF_TYPE || fmt == GF_LWAVE || fmt == GF_LDEST || fmt == GF_FTYPE || fmt == GF_VTYPE
                            || fmt == GF_ARP || fmt == GF_DIV)   /* a list: round it, a step a detent */
                            v = (int32_t)(((uint32_t)(gp_p[q] + e % (hi + 1) + (hi + 1))) % (uint32_t)(hi + 1));
                        else
                            v = gp_p[q] + enc_fast(k, e);
                        gp_p[q] = (int8_t)(v < lo ? lo : v > hi ? hi : v);
                        set.mod = 1;
                        dirty = 1;
                        t_change = fm1_ms;
                    }
            }
        }

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
        led_fn(BTN_OCT_DOWN, oct < 0);
        led_fn(BTN_OCT_UP, oct > 0);
        led_fn(BTN_SAVE, saving);
        led_fn(BTN_HOME, view == V_HOME && !saving);
        led_fn(BTN_FX, view == V_PAGES && pg >= FX_PAGE0);
        if (pg <= PG_GRAIN2) edit_pg = pg;      /* (where EDIT and SEL take back to) */
        if (pg == PG_FILTER || pg == PG_FILTER2) filt_pg = pg;
        led_fn(BTN_EDIT, view == V_PAGES && pg <= PG_GRAIN2);
        led_fn(BTN_SEL, view == V_PAGES && (pg == PG_FILTER || pg == PG_FILTER2));
        led_fn(BTN_ENV, view == V_PAGES && pg == PG_ENV);
        led_fn(BTN_LFO, view == V_PAGES && pg == PG_LFO);
        led_fn(BTN_ARP, gp_p[GQ_AMODE] != A_OFF);
        led_fn(BTN_SEQ, view == V_SEQ);
        led_fn(BTN_PLAY, ctl_seq_run ? fm1_ms - beat_t >= 80u : clk_bpm && fm1_ms - clk_beat_ms < 80u);   /* a blink a beat */
        if (view == V_SEQ) led_fn(BTN_REC, 0);  /* (REC: a rest, on the SEQ screen only) */
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
            uint32_t play = ctl_seq_run ? ui_seq_step : ~0u, sum = cursor | play << 8, i;
            if (page_drawn != ~2u) { page_drawn = ~2u; draw_text_box(0, 28, 240, &FONT_L, "SEQ", C_WHITE, 1); }
            for (i = 0; i < SEQ_MAX; i++) sum = sum * 31u + pat.st[i].note[0] + pat.st[i].note[1] * 7u;
            sum += (uint32_t)pat.len * 977u;
            if (sum != seq_drawn && fm1_ms - seq_t >= 30u) { seq_drawn = sum; seq_t = fm1_ms; draw_seq(cursor, play); }
            if ((st = bpm | (uint32_t)pat.len << 8 | (uint32_t)pat.div << 16 | (uint32_t)pat.swing << 20) != line_drawn) {
                line_drawn = st;
                draw_seq_lines(bpm);
            }
        } else {
            uint32_t sum = pg, live = 0, wpg = view == V_HOME ? 2u : pg;
            if (view == V_HOME) {
                if (page_drawn != ~3u) { page_drawn = ~3u; draw_text_box(0, 28, 240, &FONT_L, "KAYAK", C_ACC, 1); }
                if (fm1_ms - scope_t >= 40u) { scope_t = fm1_ms; draw_scope(); }   /* 25 frames a second */
                if ((st = (uint32_t)gp_p[GQ_AMODE] | ctl_seq_run << 4 | bpm << 8) != line_drawn) { line_drawn = st; draw_home_line(ctl_seq_run, bpm); }
            } else {
                if (pg != page_drawn) { page_drawn = pg; draw_text_box(0, 28, 240, &FONT_L, PAGE_NAME[pg], C_WHITE, 1); }
                for (k = 0; k < 4u; k++) sum = sum * 131u + (uint8_t)(PAGE_Q[pg][k] == QX ? 0 : gp_p[PAGE_Q[pg][k]]);
                if (sum != cells_drawn) { cells_drawn = sum; draw_cells(pg); }
                if ((st = (uint32_t)(oct + 2) | (uint32_t)set.ch << 4) != status_drawn) { status_drawn = st; draw_status(oct, set.ch); }
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
            if (wave_live) { wave_live = 0; wave_t = fm1_ms; draw_wave(wpg); }
        }
        idle_ok = 0;

        if (dirty && gp_quiet() && !grs.busy && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent */
            for (k = 0; k < GQ_N; k++) set.p[k] = gp_p[k];
            settings_save(&set, sizeof set);
            dirty = 0;
        }
        if (pat_dirty && !ctl_seq_run && !step_in && gp_quiet() && fm1_ms - t_change > 2000u) {   /* the sequence */
            blob_save(1, &pat, sizeof pat);
            pat_dirty = 0;
        }
    }
}
