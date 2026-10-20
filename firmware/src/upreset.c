/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* User presets (editor protocol v2, cmds 16-21; the SAVE > USER page): 32
 * slots in two storage.c objects (OBJ_UPRESET0/1, 0xDC000..0xDFFFF), 16
 * records each. sloop-lab: no RAM mirror; the banks are read where they are, in the flash's plain
 * XIP window (storage.c st_xip), as the samples and the FM6 bank already are; a write stages the
 * bank in the project buffer (project.c up_stage) and goes through st_save (6,160 B of RAM back).
 * The host test (UP_HOST) keeps the RAM banks. A record:
 * engine, name, the instrument parameters, a 16-step pattern.
 *
 * A bank whose magic, record size or slot count differ reads as empty; so
 * does a record with another UP_VER. A record keeps np = the P_COUNT it was
 * stored with; when that differs it is mapped by count: its last 8 values
 * are P_E0..P_E7, the first np - 8 are P_LEVEL.. in order, missing ones take
 * their defaults. So common parameters may only be added just before P_E0
 * (else bump UP_VER).
 *
 * With -DUP_HOST (host test) only the part above #ifndef UP_HOST is built;
 * it needs nothing but core.h. */
/* Logical records are 256 bytes. On flash, signed-byte parameters make a
 * 160-byte v2 record, so 16 slots still fit one atomic A/B sector payload.
 * Using 256-byte wire records would OVERFLOW the existing 3840-byte payload;
 * the apparently free flash at E7000 belongs to user sample slot 4. */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
#define UP_PER_BANK 16u
#define UP_PMAX 104u
#define UP_USED 0xA5u
#define UP_VER 2u
#define UP_BANK_MAGIC 0x31425055u
#define UP_V1_SIZE 192u
typedef struct {
    uint8_t used, ver, engine, np;
    char name[12];
    int16_t p[UP_PMAX];
    uint8_t note[16], flags[16];
} up_rec_t;
typedef struct {
    uint8_t used, ver, engine, np;
    char name[12];
    int8_t p[UP_PMAX];
    uint8_t note[16], flags[16], reserved[8];
} up_wire_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_wire_t r[UP_PER_BANK];
} up_bank_t;
_Static_assert(sizeof(up_rec_t) == 256 && sizeof(up_wire_t) == 160, "user preset layouts");
_Static_assert(sizeof(up_bank_t) <= 3840, "atomic preset bank payload");
_Static_assert(P_COUNT <= UP_PMAX && P_COUNT < 128, "user preset parameter count");
static const up_bank_t *up_bank_xip[UP_SLOTS / UP_PER_BANK];
/* Main-loop consumers only: never accessed by the renderer or the audio ISR. */
static up_rec_t up_read_cache;
static const up_rec_t up_rec_none;
#ifndef UP_HOST
static void *up_stage(void);
#endif
UI_SMALL static int up_bank_ok(const up_bank_t *bk, int len)
{
    return len >= 8 && bk->magic == UP_BANK_MAGIC && bk->nslot == UP_PER_BANK &&
        (bk->rsize == sizeof(up_wire_t) || bk->rsize == UP_V1_SIZE) &&
        len == (int)(8u + UP_PER_BANK*bk->rsize);
}
UI_SMALL static void up_decode(const up_bank_t *bk, uint32_t k, up_rec_t *out)
{
    uint32_t i;
    const uint8_t *raw;
    memset(out, 0, sizeof *out);
    if (!bk) return;
    raw = (const uint8_t *)bk + 8u + (k % UP_PER_BANK)*bk->rsize;
    memcpy(out, raw, 16);
    if (bk->rsize == UP_V1_SIZE) {
        if (raw[1] != 1u || raw[3] > 72u) { out->used = 0; return; }
        for (i = 0; i < 72; i++) out->p[i] = (int16_t)((uint16_t)raw[16+2*i] | (uint16_t)raw[17+2*i] << 8);
        memcpy(out->note, raw+160, 32);
    } else {
        const up_wire_t *r = (const up_wire_t *)raw;
        if (r->ver != UP_VER) { out->used = 0; return; }
        for (i = 0; i < UP_PMAX; i++) out->p[i] = r->p[i];
        memcpy(out->note, r->note, 32);
    }
}
UI_SMALL static int up_encode(up_wire_t *out, const up_rec_t *r)
{
    uint32_t i;
    memset(out, 0, sizeof *out);
    if (!r || !r->used) return 0;
    if (r->np > UP_PMAX) return 1;
    memcpy(out, r, 16);
    out->ver = UP_VER;
    for (i = 0; i < r->np; i++)                     /* every parameter fits a byte (project_test.c asserts the ranges);
                                                     * a value outside (a record a client wrote unclamped) is clamped,
                                                     * never refused: a refusal would leave the whole bank unsaveable */
        out->p[i] = (int8_t)(r->p[i] < -128 ? -128 : r->p[i] > 127 ? 127 : r->p[i]);
    memcpy(out->note, r->note, 32);
    return 0;
}
UI_SMALL static const up_rec_t *up_rec(uint32_t k)
{
    up_decode(k < UP_SLOTS ? up_bank_xip[k/UP_PER_BANK] : 0, k, &up_read_cache);
    return &up_read_cache;
}
UI_SMALL static int up_valid(const up_rec_t *r)
{
    return r->used == UP_USED && (r->ver == 1u || r->ver == UP_VER) && r->engine < NENGINES &&
        r->np >= 8u && r->np <= (r->ver == 1u ? 72u : UP_PMAX) && r->name[0];
}
UI_SMALL static int up_used(uint32_t k) { return k < UP_SLOTS && up_valid(up_rec(k)); }
#ifndef UP_HOST
UI_SMALL static void up_bank_find(uint32_t b)
{
    const void *p = 0; uint32_t len = 0;
#if FELUCCA_FLASH
    p = flash_ok ? st_xip(OBJ_UPRESET0+b, &len) : 0;
#endif
    up_bank_xip[b % NELEM(up_bank_xip)] = p && up_bank_ok(p, (int)len) ? p : 0;
}
#endif
/* a decoded record a save must carry over: in use, of its layout's version (up_decode empties another) and with an
 * np its layout holds, whatever its engine or name. up_valid decides what this build reads; a preset of an engine index
 * past this build's (a FELUCCA_SLICE=1 synth build's TERRAIN, 13; a later SLOOP's) or without a name stays in flash,
 * as 2.7.1's whole-bank copy kept it */
UI_SMALL static int up_kept(const up_rec_t *r)
{
    return r->used == UP_USED && r->np <= (r->ver == 1u ? 72u : UP_PMAX);
}
/* Convert a whole bank before touching flash; a torn save leaves the old v1
 * or v2 sector readable. A v2 record is kept byte for byte, a v1 record is
 * re-encoded (every parameter range fits a byte); free slots and records
 * up_kept refuses remain empty. */
static int up_bank_convert(up_bank_t *dst, const up_bank_t *src)
{
    uint32_t i; up_rec_t r;
    memset(dst, 0, sizeof *dst);
    dst->magic = UP_BANK_MAGIC; dst->rsize = sizeof(up_wire_t); dst->nslot = UP_PER_BANK;
    for (i = 0; i < UP_PER_BANK; i++) {
        up_decode(src, i, &r);
        if (!up_kept(&r))
            continue;
        if (src->rsize == sizeof(up_wire_t))
            dst->r[i] = src->r[i];
        else if (up_encode(&dst->r[i], &r))
            return 1;
    }
    return 0;
}

/* the record's values in today's P_* order (mapped by count, see above); def = the defaults */
UI_SMALL static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    uint32_t i, nc = r->np - 8u;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? r->p[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = r->p[nc + i];
}

UI_SMALL static int up_name_ok(const uint8_t *s, uint32_t n)   /* 1..12 printable ASCII */
{
    uint32_t i;
    if (!n || n > 12u)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] < 32u || s[i] > 126u)
            return 0;
    return 1;
}

UI_SMALL static void up_name(uint32_t k, char *b)       /* upper case, 0-terminated: b holds 13 */
{
    const up_rec_t *r = up_rec(k);
    uint32_t i;
    for (i = 0; i < 12u && r->name[i]; i++)
        b[i] = r->name[i] >= 'a' && r->name[i] <= 'z' ? (char)(r->name[i] - 32) : r->name[i];
    b[i] = 0;
}

UI_SMALL static void up_pat_norm(uint8_t *note, uint8_t *flags)   /* tie: no note; rest: no flags */
{
    *note &= 127u;
    if (*flags & 4u) {
        *note = 0;
        *flags = 4;
    } else {
        *flags = *note ? (uint8_t)(*flags & (SF_ACCENT | SF_SLIDE)) : 0u;
    }
}

UI_SMALL static void up_pat_from(up_rec_t *r, const step_t *st)   /* the first 16 steps -> the pattern */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        r->note[i] = st[i].time == ST_NOTE && st[i].n ? st[i].note[0] : 0u;
        r->flags[i] = st[i].time == ST_TIE ? 4u : st[i].flags;
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
}

UI_SMALL static int up_pat_empty(const up_rec_t *r)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (r->note[i])
            return 0;
    return 1;
}

/* UP_PUT arguments: slot, engine, name, P_COUNT x v14, 16 x (note, flags) -> *r (values not yet
 * clamped); 0 ok, 1 bad arguments. *slot gets the slot byte when there is one. */
UI_SMALL static int up_parse(const uint8_t *a, uint32_t na, up_rec_t *r, uint32_t *slot)
{
    uint32_t i, n, k;
    if (na < 3u)
        return 1;
    *slot = a[0];
    for (n = 0; 2u + n < na && a[2 + n]; n++)
        ;
    k = 3u + n;                                  /* after the name's 0 */
    if (a[0] >= UP_SLOTS || a[1] >= NENGINES || 2u + n >= na || !up_name_ok(a + 2, n) ||
        na < k + 2u * P_COUNT + 32u)
        return 1;
    memset(r, 0, sizeof *r);
    r->used = UP_USED;
    r->ver = UP_VER;
    r->engine = a[1];
    r->np = P_COUNT;
    for (i = 0; i < n; i++)
        r->name[i] = (char)a[2 + i];
    for (i = 0; i < P_COUNT; i++, k += 2u)
        r->p[i] = (int16_t)((int32_t)((a[k] & 127u) | (a[k + 1] & 127u) << 7) - 8192);
    for (i = 0; i < 16u; i++, k += 2u) {
        r->note[i] = a[k];
        r->flags[i] = a[k + 1];
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
    return 0;
}

#ifndef UP_HOST
UI_SMALL static const param_desc_t *up_desc(uint32_t e, uint32_t i)
{
    return i >= P_E0 ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
}

UI_SMALL static void up_values(const up_rec_t *r, int16_t *v)   /* mapped and clamped for its engine */
{
    int16_t def[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        def[i] = up_desc(r->engine, i)->def;
    up_params(r, v, def);
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int16_t)clamp(v[i], up_desc(r->engine, i)->min, up_desc(r->engine, i)->max);
}

UI_SMALL static void up_boot(void)                      /* persist_boot: the banks, where they are in the flash */
{
    uint32_t b;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        up_bank_find(b);
}

/* record k = *r (0: erase), then the bank to flash: 0 ok, 2 flash error, 3 no flash (nothing kept: there is no
 * RAM mirror). The bank is staged in the project buffer: its current copy, or an empty one, with the record put */
UI_SMALL static int up_put(uint32_t k, const up_rec_t *r)
{
    uint32_t b = (k / UP_PER_BANK) % NELEM(up_bank_xip), i;
    up_bank_t *bk = (up_bank_t *)up_stage();
    int rc = 3;
    if (up_bank_convert(bk, up_bank_xip[b])) return 1;
    if (up_encode(&bk->r[k % UP_PER_BANK], r)) return 1;
    if (!r)
        for (i = 0; i < NTRK; i++)
            if (trk[i].user == k + 1u)
                trk[i].user = 0;
    up_gen++;
#if FELUCCA_FLASH
    if (flash_ok)
        rc = st_save(OBJ_UPRESET0 + b, bk, sizeof *bk) ? 2 : 0;
#endif
    up_bank_find(b);
    return rc;
}

UI_SMALL static void up_slot_label(char *b, uint32_t k)  /* "U07" */
{
    b[0] = 'U';
    b[1] = (char)('0' + (k + 1u) / 10u);
    b[2] = (char)('0' + (k + 1u) % 10u);
    b[3] = 0;
}

/* the selected part's sound -> slot k; name 0 or "": engine name + slot number ("ANALOG 07").
 * 1 = the drum track is selected (it has no sound to store) */
UI_SMALL static int up_store(uint32_t k, const char *name)
{
    up_rec_t r;
    uint32_t i;
    if (is_drum(TSEL))
        return 1;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.ver = UP_VER;
    r.engine = TSEL->eng_req;
    r.np = P_COUNT;
    if (name && name[0]) {
        for (i = 0; i < 12u && name[i]; i++)
            r.name[i] = name[i];
    } else {
        char b[16], l[4];
        str_cpy(b, ENGINES[TSEL->eng_req]->name, 9);
        up_slot_label(l, k);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), l + 1, 3);
        for (i = 0; i < 12u && b[i]; i++)
            r.name[i] = b[i];
    }
    for (i = 0; i < P_COUNT; i++)
        r.p[i] = TSEL->p[i];
    up_pat_from(&r, TSEL->step);
    return up_put(k, &r);
}

/* slot k -> the selected part's sound: engine and every parameter except its mix (LEVEL,
 * PAN, MUTE: the TRACKS faders) and its pattern parameters (param_kept). LIVE: the pattern
 * stored in the record is not loaded: changing the sound never changes the sequence.
 * 0 ok, 1 empty (or the drum track is selected) */
UI_SMALL static int up_load_to(track_t *t, uint32_t k)       /* user preset k into track t (2.6: any track, MIDI PC) */
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    if (!up_used(k) || is_drum(t))
        return 1;
    r = up_rec(k);
    up_values(r, v);
    for (i = 0; i < P_COUNT; i++)                       /* (LEN etc. of a kept pattern changed too, and */
        if (param_kept(i) || (t->p[P_AMODE] && i >= P_AMODE && i <= P_AORDER) ||   /* a preset pattern then counted as */
            (i == P_AHOLD && !t->p[P_AMODE] && t->p[P_AHOLD]))   /* (2.7: KBD HOLD stays on) */
            v[i] = t->p[i];                             /* edited; 2.7: the arp that runs goes on, ui.c apply_preset_to) */
    if (!v[P_AMODE] && !t->p[P_AMODE])
        v[P_AHOLD] = t->p[P_AHOLD];                     /* (2.7) KBD HOLD (HOLD with the arp OFF) is the track's, not the
                                                         * preset's: as it was (a 2.6 preset's HOLD meant nothing there) */
    sound_change(t);                                    /* (2.6: what sounds fades out with the old sound, no click) */
    if (t->p[P_AMODE])
        arp_keep_req |= (uint8_t)(1u << trk_index(t));  /* (2.7: its notes too) */
    panic_req |= (uint8_t)(1u << trk_index(t));
    fm1_irq_off();                                      /* the audio ISR must not see half a sound */
    t->eng_req = r->engine;
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = v[i];
    t->preset = 0;
    fm1_irq_on();
    fm6_track_loaded(t);                                /* FM6: a user preset holds the PTCH and the macros */
    t->user = (uint8_t)(k + 1u);
    if (t == TSEL)
        sync_reload = 1;
    ui.force = 1;
    return 0;
}
UI_SMALL static int up_load(uint32_t k) { return up_load_to(TSEL, k); }   /* into the selected track */

UI_SMALL static uint32_t up_count(void)                 /* used slots */
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

UI_SMALL static uint32_t up_nth(uint32_t n)             /* slot of the n-th used one (n < up_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && !n--)
            return k;
    return 0;
}

UI_SMALL static uint32_t up_rank(uint32_t slot)         /* used slots before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

/* SAVE > USER page actions, with the message in the top bar */
UI_SMALL static void up_ui(uint32_t op, uint32_t k)     /* 0 load, 1 erase, 2 save */
{
    char l[4];
    int rc;
    up_slot_label(l, k);
    if (op != 1u && is_drum(TSEL)) {
        ui_message("DRUM TRACK: NO SOUND");
        return;
    }
    if (op < 2u && !up_used(k)) {
        ui_message("EMPTY SLOT");
        return;
    }
    if (op == 0u) {
        up_load(k);
        ui_say("LOADED ", l);
        return;
    }
    if (song.playing || transport_req) {               /* a flash erase stops the audio ~50 ms */
        ui_message("STOP BEFORE SAVE");
        return;
    }
    rc = op == 1u ? up_put(k, 0) : up_store(k, 0);
    if (rc == 3)
        ui_message("NO FLASH");                        /* (no RAM mirror to keep it in) */
    else if (rc)
        ui_message(op == 1u ? "ERASE ERROR" : "SAVE ERROR");
    else
        ui_say(op == 1u ? "ERASED " : "SAVED ", l);
    ui.force = 1;
}
#endif
