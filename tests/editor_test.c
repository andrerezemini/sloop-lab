/* SPDX-License-Identifier: GPL-3.0-only */
/* The web editor's SysEx commands (firmware/src/editor.c, web/EDITOR_PROTOCOL.md) on the host: the real ed_handle on the
 * ui_pages_test harness, its replies captured, the user sample slots in a RAM image of their flash (erases and writes
 * counted). Built without editor.c's backup part (FELUCCA_FLASH 0: its objects are project.c's, upreset.c's and
 * fm6_bank.c's); the user preset commands only compile here (upreset.c doubles).
 *   samples  (after 2.7.1) SMP_BEGIN / SMP_ERASE / SMP_WRITE / SMP_END answer rc 3 while the song plays or PLAY is asked,
 *            and touch no flash (a 4 KiB erase silences the audio ~50 ms): a slot in use keeps playing as it was, PLAY
 *            during an upload stops it at the next SMP_WRITE; SMP_INFO answers; a MIDI START between two of
 *            SMP_ERASE's 20 erases stops it there (rc 3, the slot empty); stopped, the same upload goes in and
 *            SMP_ERASE empties the slot. */
#include <stdint.h>
static uint32_t host_slots[4u * 0x14000u / 4u];          /* USR1..4 (a RAM image, as tests/userkit_test.c) */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define UI_PAGES_HARNESS_ONLY 1
#define UI_PAGES_EDITOR 1                                /* (editor.c brings its own ed_b and editor_live.c) */
#include "ui_pages_test.c"

/* ---- the doubles editor.c needs here ---- */
static uint8_t flash_ok = 1;
static uint32_t n_erase, n_write, n_busy_touch;          /* sector erases, page writes, any of them while busy */
static uint32_t start_at;                                /* the erase after which a MIDI START comes in (0: none) */
static uint8_t *slot_img(uint32_t off)                   /* flash offset -> the RAM image (0: not a sample slot) */
{
    uint32_t k;
    for (k = 0; k < SMP_USER_SLOTS; k++)
        if (off >= SMP_USER_OFF(k) && off < SMP_USER_OFF(k) + SMP_USER_SIZE)
            return (uint8_t *)host_slots + k * SMP_USER_SIZE + (off - SMP_USER_OFF(k));
    return 0;
}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    uint8_t *p = slot_img(off);
    *took = 0;
    n_erase++;
    n_busy_touch += song.playing || transport_req;
    if (n_erase == start_at)
        transport_req = 3;                               /* seq.c mclk_event, in the audio interrupt as the IRQs come back */
    if (!p || (off & 0xFFFu))
        return -8;
    memset(p, 0xFF, 0x1000u);
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n)
{
    uint8_t *p = slot_img(off);
    uint32_t i;
    n_write++;
    n_busy_touch += song.playing || transport_req;
    if (!p || !slot_img(off + n - 1u))
        return -8;
    for (i = 0; i < n; i++)
        p[i] &= src[i];                                  /* NOR: a write only clears bits */
    return 0;
}
static uint8_t rep[700];                                 /* the last reply, F0 .. F7 */
static uint32_t rep_n;
static void ota_wire_send(const uint8_t *p, uint32_t n)
{
    rep_n = n < sizeof rep ? n : sizeof rep;
    memcpy(rep, p, rep_n);
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
static void ota_frame_done(void) {}
static uint32_t so_w, so_r;                              /* (usb.c's SysEx out ring: FELUCCA_OTA only) */
#define SXQ 64u
/* the user preset commands' upreset.c (only compiled here) */
typedef struct { uint8_t used, ver, engine, np; char name[12]; int16_t p[72]; uint8_t note[16], flags[16]; } up_rec_t;
static up_rec_t up_none;
static const up_rec_t *up_rec(uint32_t k) { (void)k; return &up_none; }
static void up_values(const up_rec_t *r, int16_t *v) { (void)r; memset(v, 0, P_COUNT * sizeof *v); }
static int up_parse(const uint8_t *a, uint32_t na, up_rec_t *r, uint32_t *slot) { (void)a, (void)na, (void)r, (void)slot; return 1; }
static int up_put(uint32_t k, const up_rec_t *r) { (void)k; (void)r; return 2; }
static int up_store(uint32_t k, const char *nm) { (void)k; (void)nm; return 2; }
static int up_name_ok(const uint8_t *s, uint32_t n) { (void)s; (void)n; return 0; }
#include "../firmware/src/editor.c"

static int efails;
static void echeck(int ok, const char *what) { printf("editor: %-90s %s\n", what, ok ? "ok" : "FAIL"); efails += !ok; }

/* one request: cmd and its arguments; -> the reply's arguments (0 if none) */
static const uint8_t *req(uint32_t cmd, const uint8_t *a, uint32_t na, uint32_t *nr)
{
    static uint8_t f[700];
    f[0] = ED_HDR0, f[1] = ED_HDR1, f[2] = ED_HDR2, f[3] = (uint8_t)cmd;
    memcpy(f + 4, a, na);
    rep_n = 0;
    ed_handle(f, 4u + na);
    *nr = rep_n >= 6u && rep[4] == cmd ? rep_n - 6u : 0u;
    return rep_n >= 6u && rep[4] == cmd ? rep + 5 : 0;
}
static uint32_t pack7(uint8_t *o, const uint8_t *p, uint32_t n)   /* the editor's pack7 (web/editor.html) */
{
    uint32_t m = 0;
    while (n) {
        uint32_t k = n > 7u ? 7u : n, b = 0, i;
        for (i = 0; i < k; i++)
            b |= (uint32_t)(p[i] >> 7) << i;
        o[m++] = (uint8_t)b;
        for (i = 0; i < k; i++)
            o[m++] = p[i] & 127u;
        p += k;
        n -= k;
    }
    return m;
}
static uint32_t smp_rc(uint32_t cmd, uint32_t k)        /* SMP_BEGIN / SMP_ERASE -> rc (99: no reply) */
{
    uint8_t a[1] = {(uint8_t)k};
    uint32_t nr;
    const uint8_t *r = req(cmd, a, 1, &nr);
    return r && nr == 2u && r[0] == k ? r[1] : 99u;
}
static uint32_t smp_write(uint32_t k, uint32_t off, const uint8_t *d, uint32_t n)   /* -> rc */
{
    uint8_t a[4 + 300];
    uint32_t nr;
    const uint8_t *r;
    a[0] = (uint8_t)k, a[1] = off & 127u, a[2] = (off >> 7) & 127u, a[3] = (off >> 14) & 127u;
    r = req(ED_SMP_WRITE, a, 4u + pack7(a + 4, d, n), &nr);
    return r && nr == 5u && r[0] == k && (uint32_t)(r[1] | r[2] << 7 | r[3] << 14) == off ? r[4] : 99u;
}
static uint32_t smp_end(uint32_t k, const smp_user_hdr_t *h)
{
    uint8_t a[1 + 560];
    uint32_t nr;
    const uint8_t *r;
    a[0] = (uint8_t)k;
    r = req(ED_SMP_END, a, 1u + pack7(a + 1, (const uint8_t *)h, sizeof *h), &nr);
    return r && nr == 2u && r[0] == k ? r[1] : 99u;
}

/* a slot as the web editor builds it: one zone of n ADPCM samples (n / 2 bytes), the name, the data's CRC */
static uint8_t up_data[3u * 4096u];
static smp_user_hdr_t up_hdr;
static void build(const char *name, uint32_t bytes, uint8_t seed)
{
    uint32_t i;
    for (i = 0; i < bytes; i++)
        up_data[i] = (uint8_t)(i * 7u + seed);
    memset(&up_hdr, 0, sizeof up_hdr);
    up_hdr.magic = SMP_USER_MAGIC;
    up_hdr.version = 1;
    up_hdr.nz = 1;
    memcpy(up_hdr.name, name, strlen(name) < 8u ? strlen(name) : 8u);
    up_hdr.data_len = bytes;
    up_hdr.crc = st_crc32(up_data, bytes);
    up_hdr.zone[0].n = 2u * bytes;
    up_hdr.zone[0].le = 2u * bytes - 1u;
    up_hdr.zone[0].rate = 0x8000u;
    up_hdr.zone[0].root16 = 60 * 16;
    up_hdr.zone[0].lo = 0;
    up_hdr.zone[0].hi = 127;
}
/* SMP_BEGIN, the data in 256-byte writes, SMP_END: as web/editor.html smpUpload; -> the first rc that is not 0 */
static uint32_t upload(uint32_t k, uint32_t bytes)
{
    uint32_t rc = smp_rc(ED_SMP_BEGIN, k), off;
    for (off = 0; !rc && off < bytes; off += 256u)
        rc = smp_write(k, SMP_USER_DATA + off, up_data + off, bytes - off < 256u ? bytes - off : 256u);
    return rc ? rc : smp_end(k, &up_hdr);
}

static void t_samples(void)
{
    const uint32_t K = 1, BYTES = 9000;                  /* USR2; the data crosses two sector starts (4096, 8192) */
    static uint8_t before[0x14000];
    uint32_t nr, i, rc, n0, w0, off;
    const uint8_t *r;
    /* stopped: a slot goes in, and plays */
    song.playing = 0, transport_req = 0;
    build("FIRST", BYTES, 1);
    n0 = n_erase;
    rc = upload(K, BYTES);
    echeck(rc == 0 && usr_nz[K] == 1 && !memcmp(SMP_USER_XIP(K) + SMP_USER_DATA, up_data, BYTES) && n_erase - n0 == 3u &&
           !memcmp(((const smp_user_hdr_t *)SMP_USER_XIP(K))->name, "FIRST", 5),
           "stopped: SMP_BEGIN, 36 x SMP_WRITE, SMP_END: rc 0, the slot plays it (3 sectors erased)");
    build("SECOND", BYTES, 2);
    for (i = 0; i < 2u; i++) {                           /* the song plays; then stopped with PLAY asked */
        const char *how = i ? "PLAY asked" : "playing";
        char what[160];
        memcpy(before, SMP_USER_XIP(K), sizeof before);
        song.playing = (uint8_t)!i;
        transport_req = (uint8_t)i;
        n0 = n_erase, w0 = n_write, n_busy_touch = 0;
        {   /* the slot in use: every flash command refused, nothing touched, the slot plays as before */
            uint32_t b = smp_rc(ED_SMP_BEGIN, K), e = smp_rc(ED_SMP_ERASE, K), u = upload(K, BYTES);
            snprintf(what, sizeof what, "%s: SMP_BEGIN, SMP_ERASE rc 3 (an upload stops there), no flash touched, the slot plays", how);
            echeck(b == 3u && e == 3u && u == 3u && n_erase == n0 && n_write == w0 && !n_busy_touch &&
                   !memcmp(SMP_USER_XIP(K), before, sizeof before) && usr_nz[K] == 1 && usr_zone[K][0].n == 2u * BYTES, what);
        }
        r = req(ED_SMP_INFO, 0, 0, &nr);
        snprintf(what, sizeof what, "%s: SMP_INFO answers (a read): USR2 has its zone", how);
        echeck(r && nr >= 6u && r[0] == SMP_USER_SLOTS && r[1] == SMP_USER_SIZE / 1024u && r[2] == 0 && r[5] == 1u, what);
        /* PLAY during an upload: begun stopped, the writes that come after it are refused */
        song.playing = 0, transport_req = 0;
        rc = smp_rc(ED_SMP_BEGIN, K);
        for (off = 0; !rc && off < 1024u; off += 256u)
            rc = smp_write(K, SMP_USER_DATA + off, up_data + off, 256u);
        memcpy(before, SMP_USER_XIP(K), sizeof before);
        song.playing = (uint8_t)!i;
        transport_req = (uint8_t)i;
        n0 = n_erase, w0 = n_write;
        {
            uint32_t w = smp_write(K, SMP_USER_DATA + 1024u, up_data + 1024u, 256u);         /* a page: a write */
            uint32_t w2 = smp_write(K, 4096u, up_data + 4096u - SMP_USER_DATA, 256u);         /* a sector start: an erase */
            uint32_t en = smp_end(K, &up_hdr);
            snprintf(what, sizeof what, "%s during an upload: SMP_WRITE (a page, a sector start), SMP_END rc 3, no flash touched", how);
            echeck(rc == 0 && w == 3u && w2 == 3u && en == 3u && n_erase == n0 && n_write == w0 && !n_busy_touch &&
                   !memcmp(SMP_USER_XIP(K), before, sizeof before) && usr_nz[K] == 0, what);
        }
        song.playing = 0, transport_req = 0;
        rc = upload(K, BYTES);                           /* stopped again: the upload, from SMP_BEGIN */
        snprintf(what, sizeof what, "%s, then stopped: the upload sent again goes in (rc 0, the new sound)", how);
        echeck(rc == 0 && usr_nz[K] == 1 && !memcmp(((const smp_user_hdr_t *)SMP_USER_XIP(K))->name, "SECOND", 6) &&
               !memcmp(SMP_USER_XIP(K) + SMP_USER_DATA, up_data, BYTES), what);
    }
    /* SMP_ERASE stopped, then a MIDI START (SYNC USB / TRS) drained between its erases: the 19 left wait */
    n0 = n_erase, n_busy_touch = 0;
    start_at = n_erase + 1u;
    rc = smp_rc(ED_SMP_ERASE, K);
    start_at = 0;
    for (i = 0; i < 0x1000u && SMP_USER_XIP(K)[i] == 0xFFu; i++)
        ;
    echeck(rc == 3u && n_erase - n0 == 1u && !n_busy_touch && i == 0x1000u && usr_nz[K] == 0,
           "a MIDI START after SMP_ERASE's first erase: rc 3, no erase while it plays, the header gone (slot empty)");
    transport_req = 0;
    build("THIRD", BYTES, 3);
    rc = upload(K, BYTES);
    echeck(rc == 0 && usr_nz[K] == 1 && !memcmp(((const smp_user_hdr_t *)SMP_USER_XIP(K))->name, "THIRD", 5) &&
           !memcmp(SMP_USER_XIP(K) + SMP_USER_DATA, up_data, BYTES), "then stopped: an upload into that slot goes in (rc 0)");
    n0 = n_erase;
    rc = smp_rc(ED_SMP_ERASE, K);
    echeck(rc == 0 && usr_nz[K] == 0 && n_erase - n0 == SMP_USER_SIZE / 0x1000u, "stopped: SMP_ERASE: rc 0, the slot empty (20 sectors)");
}

int main(void)
{
    uint32_t i;
    bank_resolve();
    panel = PANEL_DEFAULT;
    layers_init();
    host_tracks_init();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    memset(host_slots, 0xFF, sizeof host_slots);
    for (i = 0; i < SMP_USER_SLOTS; i++)
        smp_user_scan(i);
    t_samples();
    printf(efails ? "editor: %d FAILED\n" : "editor: all passed\n", efails);
    return efails != 0;
}
