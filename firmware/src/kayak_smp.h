/* SPDX-License-Identifier: GPL-3.0-only */
/* Kayak's samples: up to NSLOT (8 on the FM-1, 4 on the SMK-37) sounds of up to GS_N samples (0.74 s at 22.05 kHz,
 * mono, int16), each in a flash slot (store.h, SLOT_SECT sectors); SRC A / SRC B's TYPE picks one (SMP1 ..) instead
 * of a rendered source. They come over USB-MIDI SysEx from a web page (docs/samples/index.html), between F0 and F7:
 *
 *   7D 4B 59 01                          INFO: the reply lists the slots
 *   7D 4B 59 02 slot l0 l1 l2 root n*8   BEGIN: a sample of l samples (7 bits each, low first), its root note, an
 *                                        8-letter name (7-bit ASCII)
 *   7D 4B 59 03 q0 q1 data...            DATA: bytes q * 512 .. (q: 7 + 7 bits) of the samples (int16, little
 *                                        endian), packed 7 to 8 (a byte of the next 7's top bits, then their low 7)
 *   7D 4B 59 04                          COMMIT: into the slot (its sectors erased: the sound holds ~0.5 s)
 *   7D 4B 59 05 slot                     ERASE: the slot empty
 *
 * Each answered by 7D 4B 59 7E cmd q0 q1 status (0: done, 1: refused); INFO by 7D 4B 59 01 nslot n0 n1 n2 (GS_N)
 * r0 r1 r2 (the rate) then a slot each: used, l0 l1 l2, root, the name's 8 letters. One frame at a time: the page
 * waits for each answer (usb.c drops a frame that comes while one is pending). 7D: the non-commercial SysEx ID. */
typedef struct { uint8_t name[8]; uint16_t len; uint8_t root, rsv; int16_t s[GS_N]; } smp_t;
static smp_t smp;                               /* the one coming in, or the one being read */
static uint32_t smp_rx, smp_rx_bytes, smp_rx_t; /* receiving: the slot + 1, the bytes come, when the last frame came */

static int smp_send(const uint8_t *p, uint32_t n)
{
#ifdef DINGHY_HOST
    return host_sysex_out(p, n);
#else
    return ota_wire_send(p, n);
#endif
}
static void smp_answer(uint32_t cmd, uint32_t q, uint32_t status)
{
    uint8_t m[10] = {0xF0, 0x7D, 0x4B, 0x59, 0x7E, 0, 0, 0, 0, 0xF7};
    m[5] = (uint8_t)(cmd & 0x7Fu);
    m[6] = (uint8_t)(q & 0x7Fu);
    m[7] = (uint8_t)(q >> 7 & 0x7Fu);
    m[8] = (uint8_t)(status & 0x7Fu);
    smp_send(m, sizeof m);
}
static void smp_info(void)
{
    static uint8_t m[13u + NSLOT * 13u];
    uint32_t i, j, w = 0;
    m[w++] = 0xF0, m[w++] = 0x7D, m[w++] = 0x4B, m[w++] = 0x59, m[w++] = 0x01, m[w++] = (uint8_t)NSLOT;
    m[w++] = (uint8_t)(GS_N & 0x7Fu), m[w++] = (uint8_t)(GS_N >> 7 & 0x7Fu), m[w++] = (uint8_t)(GS_N >> 14 & 0x7Fu);
    m[w++] = (uint8_t)(GS_RATE & 0x7Fu), m[w++] = (uint8_t)(GS_RATE >> 7 & 0x7Fu), m[w++] = (uint8_t)(GS_RATE >> 14 & 0x7Fu);
    for (i = 0; i < NSLOT; i++) {
        uint32_t used = !slot_load(i, &smp, sizeof smp) && smp.len && smp.len <= GS_N;
        m[w++] = (uint8_t)used;
        m[w++] = (uint8_t)(used ? smp.len & 0x7Fu : 0u);
        m[w++] = (uint8_t)(used ? smp.len >> 7 & 0x7Fu : 0u);
        m[w++] = (uint8_t)(used ? smp.len >> 14 & 0x7Fu : 0u);
        m[w++] = (uint8_t)(used ? smp.root & 0x7Fu : 60u);
        for (j = 0; j < 8u; j++) m[w++] = (uint8_t)(used ? smp.name[j] & 0x7Fu : 0u);
    }
    m[w++] = 0xF7;
    smp_send(m, w);
}

static void smp_changed(uint32_t slot);
/* a SysEx frame (the 7-bit bytes between F0 and F7) from usb.c via ota.c: 1 if it was ours */
static int app_sysex(const uint8_t *p, uint32_t n)
{
    uint32_t cmd, i;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Bu || p[2] != 0x59u)
        return 0;
    cmd = p[3];
    smp_rx_t = fm1_ms;
    if (cmd == 0x01u && !smp_rx) {
        smp_info();
    } else if (cmd == 0x02u && n >= 16u && p[4] < NSLOT) {   /* BEGIN */
        uint32_t len = p[5] | (uint32_t)p[6] << 7 | (uint32_t)p[7] << 14;
        if (!len || len > GS_N) { smp_answer(cmd, 0, 1); return 1; }
        memset(&smp, 0, sizeof smp);
        smp.len = (uint16_t)len;
        smp.root = p[8];
        for (i = 0; i < 8u; i++) smp.name[i] = p[9u + i];
        smp_rx = p[4] + 1u;
        smp_rx_bytes = 0;
        smp_answer(cmd, 0, 0);
    } else if (cmd == 0x03u && smp_rx && n >= 6u) {   /* DATA: 7 to 8 */
        uint32_t q = p[4] | (uint32_t)p[5] << 7, o = q * 512u, k = 6;
        uint8_t *d = (uint8_t *)smp.s;
        while (k < n) {
            uint32_t top = p[k++], j;
            for (j = 0; j < 7u && k < n; j++, k++)
                if (o < sizeof smp.s) d[o++] = (uint8_t)(p[k] | ((top >> j & 1u) << 7));
        }
        if (o > smp_rx_bytes) smp_rx_bytes = o;
        smp_answer(cmd, q, 0);
    } else if (cmd == 0x04u && smp_rx) {        /* COMMIT */
        uint32_t slot = smp_rx - 1u, bad = smp_rx_bytes < smp.len * 2u;
        smp_rx = 0;
        if (!bad) bad = slot_save(slot, &smp, sizeof smp) != 0;
        if (!bad) smp_changed(slot);
        smp_answer(cmd, slot, bad);
    } else if (cmd == 0x05u && !smp_rx && n >= 5u && p[4] < NSLOT) {   /* ERASE */
        memset(&smp, 0, sizeof smp);
        i = slot_save(p[4], &smp, sizeof smp) != 0;
        if (!i) smp_changed(p[4]);
        smp_answer(cmd, p[4], i);
    } else
        smp_answer(cmd, 0, 1);
    return 1;
}
static void smp_poll(void)                      /* main loop: a transfer left half way ends after 10 s */
{
    if (smp_rx && fm1_ms - smp_rx_t > 10000u) smp_rx = 0;
}

/* slot's sample into a source's buffer (grain_poll): looped to fill GS_N from MOVE's start (a share of its length),
 * backwards for SEED 64 and up, darker by TONE under 64; its root (1/16 semitones). Empty, or a transfer under way:
 * silence (and -1: grain_poll tries again) */
static int smp_fill(uint32_t slot, int16_t *dst, int32_t tone, int32_t move, int32_t seed, int32_t *root16)
{
    uint32_t i, len, start;
    int32_t k = tone >= 64 ? 32768 : (tone + 1) * 512, y = 0;   /* Q15 */
    if (smp_rx || slot_load(slot, &smp, sizeof smp) || !smp.len || smp.len > GS_N) {
        memset(dst, 0, GS_N * sizeof dst[0]);
        return smp_rx ? -1 : 0;
    }
    len = smp.len;
    start = (uint32_t)clamp(move, 0, 127) * len / 128u;
    for (i = 0; i < GS_N; i++) {
        uint32_t j = (start + i) % len;
        int32_t x = smp.s[seed >= 64 ? len - 1u - j : j];
        y += ((x - y) * k) >> 15;
        dst[i] = (int16_t)y;
    }
    *root16 = (int32_t)clamp(smp.root, 0, 127) * 16;
    return 0;
}
static void smp_changed(uint32_t slot)          /* a source playing that slot reads it again */
{
    uint32_t k;
    for (k = 0; k < 2u; k++)
        if (grs.cur[k][0] == GS_NTYPE + slot) grs.cur[k][0] = 0xFF;
}
