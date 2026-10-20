/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the deferred flash writes (firmware/src/project.c, the part only the firmware builds: autosave_tick,
 * sections_flush) with the real UI (as tests/ui_pages_test.c drives it), the user presets (upreset.c) and storage.c on
 * a simulated NOR, the audio running between the frames and the main loop's ticks after each, as main.c runs them:
 *   held    (2.7.1) a chord held by KBD HOLD or by the arp's HOLD sounds for good: the working project and a live
 *           section (SAVE + key, in RAM) still reach the flash once a write waited 2 min and nobody played for 10 s,
 *           all of it in the same frame (one gap in the sound, ~50 ms per object), not before; never while a key is
 *           held, a knob turns, the transport plays (nor within 10 s of a MIDI STOP), a recording is armed or a
 *           computer records the USB audio
 *   midi    (2.7.1) MIDI counts as playing: a legato pad over MIDI, or a note held by a MIDI sustain pedal, is
 *           never cut by a write (as 2.7.1: written once quiet)
 *   favs    (2.7.1) an empty user slot is no favourite: a user preset erased (SAVE > USER > ERASE, the editor's
 *           UP_ERASE) leaves the favourites, the others in their order, and a sound saved into a slot whose favourite
 *           an older SLOOP or a backup restore left behind is no favourite; a slot saved over stays one
 * Run by tests/run_tests.sh (needs build/gen). */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_UAC 1                                /* (usb.c: the USB audio input's state, uac.feed) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint16_t screen[240*240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ uint32_t i,j; assert(x+w<=240 && y+h<=240); for(j=0;j<h;j++) for(i=0;i<w;i++) screen[(y+j)*240+x+i]=p[j*w+i]; }
#include "../firmware/src/gfx.c"
static void lcd_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c)
{ uint32_t i,j; assert(x+w<=240&&y+h<=240); for(j=0;j<h;j++)for(i=0;i<w;i++)screen[(y+j)*240+x+i]=swap16(c); }
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
static uint8_t fm1_led[16], fm1_led_dim[16], fm1_led_bg[16];
static volatile uint16_t fm1_led_bg_ns;
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
static void fm1_wdt_feed(void) {}
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#define FELUCCA_ICONS 1
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N], scope_bufr[SCOPE_N];
static uint32_t scope_w;
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/ui_song.c"
#include "../firmware/src/ui_studio.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_vis.c"
#include "../firmware/src/ui_layers.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
/* the flash: felucca.c's st_read / st_erase / st_prog on a RAM NOR (erase -> 0xFF, program clears bits); every
 * sector erase counted, and those while something sounded (the device: ~50 ms of silence, felucca.c
 * fl_erase4k_quiet) */
static uint8_t nor[0x100000];
static uint8_t flash_ok;
static uint32_t erases, loud, loud_ms, loud_last;
static int audio_quiet(void);
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    memset(nor + off, 0xFF, 4096);
    erases++;
    if (!audio_quiet()) {                            /* (over the voices: the first and the last) */
        if (!loud++)
            loud_ms = fm1_ms;
        loud_last = fm1_ms;
    }
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{ const uint8_t *s = src; uint32_t i; for (i = 0; i < n; i++) nor[off + i] &= s[i]; return 0; }
#define ST_XIP_PTR(off) ((const void *)(nor + (off)))
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
#define FL_FAR(fn) fn
static uint32_t fl_jedec_ram(void) { return 0x856014u; }
static void fl_plain_window_init(void) {}
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

static int fails;
static void check(int ok, const char *what) { printf("persist: %-90s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

/* one main loop pass (~16 ms): the audio between (as the ISR does), input, LEDs, draw, then the deferred writes */
static void frame(void)
{
    uint32_t q;
    static int32_t o[CTL * 2];
    for (q = 0; q < 22u; q++)
        mix_block(o, CTL);
    ui_input(); ui_leds(); ui_draw(); fm1_ms += 16;
    autosave_tick();
    sections_flush();
}
static void run(uint32_t ms) { uint32_t t = fm1_ms; while (fm1_ms - t < ms) frame(); }
static int stored(uint32_t obj) { return st_load(obj, &proj_tmp, sizeof proj_tmp) == (int)sizeof(project_t); }
static int autosaved_now(void)                       /* the working project as it is now is the one in flash */
{
    static project_t q;
    proj_capture(&q);
    return stored(OBJ_AUTOSAVE) && proj_tmp.v6.sum == q.sum;
}
static void midi_in(uint32_t st, uint32_t d1, uint32_t d2)   /* a USB-MIDI event in (cable 0), as usb.c queues it */
{
    midi_in_q[mi_w % MQ] = (st >= 0xF0u ? 0xFu : st >> 4) | st << 8 | d1 << 16 | d2 << 24;
    mi_w++;
}
static void hold_chord(void)                         /* three keys down a moment and let go */
{
    fm1_in.notes |= 1u << 0 | 1u << 4 | 1u << 7;
    frame(); frame(); frame();
    fm1_in.notes = 0;
    run(1000);
}

int main(void)
{
    uint32_t i, t0, e0, l0;
    memset(nor, 0xFF, sizeof nor);
    panel = PANEL_DEFAULT;
    flash_ok = 1;                                    /* persist_boot's flash part (no sample scan: no XIP window here) */
    arr_defaults(&arrangement);
    dsu_boot();
    up_boot();
    host_tracks_init();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    layers_init();
    go_home();
    song.sel = 1;
    fm1_ms = 1000;
    run(30000);
    check(autosaved_now() && !loud, "quiet: the working project written as before (stopped, idle, nothing sounds)");

    /* ---- held: KBD HOLD (2.7: ARP 2 > HOLD ON with the arp OFF) keeps a chord sounding after the keys are let go */
    TSEL->p[P_AMODE] = 0;
    TSEL->p[P_AHOLD] = 1;
    TSEL->p[P_SUS] = TP[P_SUS].max;
    hold_chord();
    check(!audio_quiet() && kh_any(TSEL), "KBD HOLD: the chord sounds after the keys are let go");
    TSEL->p[P_LEVEL] -= 5;                           /* an edit */
    section_store(1);                                /* SAVE + key B: live section B, in RAM */
    t0 = fm1_ms;
    e0 = erases;
    run(60000);
    check(erases == e0 && !autosaved_now() && (sec_dirty & 2u), "held 1 min: nothing written yet (a write waits 2 min for silence)");
    run(75000);
    check(autosaved_now(), "held 2 min 15 s: the working project in flash all the same (2.7.1)");
    check(stored(OBJ_PROJECT0 + 1u) && !(sec_dirty & 2u), "held 2 min 15 s: live section B in flash all the same (2.7.1)");
    check(loud && loud_ms - t0 >= 120000u, "written over the sounding chord, after 2 min of waiting, not before");
    check(loud >= 2u && loud_last == loud_ms, "the project and section B written in the same frame: one gap, not two (2.7.1)");
    check(!audio_quiet() && kh_any(TSEL), "the chord still sounds after the write (nothing let go)");

    /* a key held: somebody plays, nothing is written over the chord; let go: 10 s later */
    TSEL->p[P_LEVEL] += 3;
    e0 = erases;
    fm1_in.notes |= 1u << 9;
    run(240000);
    check(erases == e0 && !autosaved_now(), "a key held 4 min over the held chord: nothing written");
    fm1_in.notes = 0;
    run(8000);
    check(erases == e0, "the key let go 8 s ago: nothing written yet");
    run(5000);
    check(autosaved_now() && !audio_quiet(), "the key let go 13 s ago: the working project in flash, the chord sounding");

    /* a knob turned every 5 s: somebody is at the panel */
    TSEL->p[P_LEVEL] -= 3;
    e0 = erases;
    for (i = 0; i < 48u; i++) {
        encs[panel.enc[EN_K1]] = (i & 1u) ? -1 : 1;
        run(5000);
    }
    check(erases == e0 && !autosaved_now() && fm1_ms - ui_input_ms < 5100u, "a knob turned every 5 s for 4 min over the held chord: nothing written");
    run(13000);
    check(autosaved_now() && !audio_quiet(), "the knob left alone 13 s: the working project in flash");

    /* the transport: never while it plays. MIDI START over writes already waiting, 3 min, MIDI STOP: the stop counts
     * as playing too (10 s), no write in the frame the transport stops (a STOP and CONTINUE would start late) */
    TSEL->p[P_LEVEL] += 3;
    section_store(2);
    e0 = erases;
    run(30000);                                      /* (the writes wait, over the chord) */
    song.g[G_SYNC] = 1;                              /* SYNC = USB: MIDI START and STOP drive the transport */
    midi_in(0xFAu, 0, 0);
    run(180000);
    check(song.playing && erases == e0 && !autosaved_now() && (sec_dirty & 4u), "MIDI START, the transport playing 3 min over the held chord: nothing written");
    midi_in(0xFCu, 0, 0);
    run(8000);
    check(!song.playing && erases == e0 && !audio_quiet(), "MIDI STOP 8 s ago, the chord ringing: nothing written yet (2.7.1)");
    run(5000);
    check(autosaved_now() && stored(OBJ_PROJECT0 + 2u) && !sec_dirty && !audio_quiet(), "MIDI STOP 13 s ago: written over the chord");
    song.g[G_SYNC] = 0;

    /* a recording armed (REC, stopped: it waits for a note) over the chord: the live section waits too */
    section_store(3);
    e0 = erases;
    rec_wait = 1;
    run(240000);
    check(rec_wait && erases == e0 && (sec_dirty & 8u), "REC armed 4 min over the held chord: nothing written (2.7.1)");
    rec_wait = 0;
    run(13000);
    check(stored(OBJ_PROJECT0 + 3u) && !sec_dirty && !audio_quiet(), "REC let go 13 s ago: live section D written over the chord");

    /* a computer recording the USB audio: never a gap in its recording */
    TSEL->p[P_LEVEL] -= 3;
    e0 = erases;
    uac.feed = 1;
    run(240000);
    check(erases == e0 && !autosaved_now(), "the USB audio recorded 4 min over the held chord: nothing written (2.7.1)");
    uac.feed = 0;
    run(13000);
    check(autosaved_now() && !audio_quiet(), "the recording over 13 s ago: written over the chord");

    TSEL->p[P_AHOLD] = 0;                            /* HOLD off: the chord ends */
    run(5000);
    check(audio_quiet() && !kh_any(TSEL), "KBD HOLD off: the chord ends");

    /* ---- the arp's HOLD (ARP MODE on, HOLD ON, GATE at its most): stopped, the arp plays the latched chord on */
    TSEL->p[P_AMODE] = 1;
    TSEL->p[P_AGATE] = TP[P_AGATE].max;
    TSEL->p[P_AHOLD] = 1;
    hold_chord();
    check(!audio_quiet() && TSEL->nheld, "the arp's HOLD: the arp plays on after the keys are let go");
    TSEL->p[P_LEVEL] += 5;
    section_store(0);
    t0 = fm1_ms;
    e0 = erases;
    l0 = loud;
    run(60000);
    check(erases == e0 && !autosaved_now() && (sec_dirty & 1u), "the arp held 1 min: nothing written yet");
    run(75000);
    check(autosaved_now() && stored(OBJ_PROJECT0) && !sec_dirty && loud > l0 && loud_last - t0 >= 120000u && !audio_quiet(),
          "the arp held 2 min 15 s: the working project and live section A in flash all the same (2.7.1)");
    TSEL->p[P_AHOLD] = 0;
    TSEL->p[P_AMODE] = 0;
    run(5000);
    check(audio_quiet(), "the arp off: quiet");

    /* ---- midi: the FM-1 as a sound module. A legato pad, a note every 4 s and the one before let go 0.5 s later,
     * 4 min, nothing touched on the FM-1: always a note sounding, never a write over it (as 2.7.1) */
    TSEL->p[P_LEVEL] -= 5;
    e0 = erases;
    l0 = loud;
    for (i = 0; i < 60u; i++) {
        midi_in(0x91u, 60u + (i & 7u), 100);
        run(500);
        if (i)
            midi_in(0x81u, 60u + ((i - 1u) & 7u), 0);
        run(3500);
        if (audio_quiet())
            break;
    }
    check(i == 60u && loud == l0 && erases == e0 && !autosaved_now(), "a legato pad over MIDI 4 min: nothing written over it (2.7.1)");
    midi_in(0x81u, 60u + (59u & 7u), 0);
    run(10000);
    check(audio_quiet() && autosaved_now() && loud == l0, "the pad let go: written once quiet, as before");

    /* a note held by a MIDI sustain pedal (CC 64) 4 min, nothing else: the pedal down is somebody playing */
    TSEL->p[P_LEVEL] += 5;
    midi_in(0xB1u, 64, 127);
    midi_in(0x91u, 64, 100);
    run(1000);
    midi_in(0x81u, 64, 0);
    e0 = erases;
    l0 = loud;
    run(240000);
    check(loud == l0 && erases == e0 && !autosaved_now() && !audio_quiet(), "a note held 4 min by a MIDI sustain pedal: nothing written over it (2.7.1)");
    midi_in(0xB1u, 64, 0);
    run(10000);
    check(audio_quiet() && autosaved_now() && loud == l0, "the pedal up: the note ends, written once quiet");

    /* ---- favs: an empty user slot leaves the favourites (ui.c fav, upreset.c up_put, up_fav_prune) */
    {
        uint32_t b[FAV_N];
        up_ui(2, 6);                                 /* SAVE > USER > SAVE: U07, U03 */
        up_ui(2, 2);
        check(up_used(6) && up_used(2), "SAVE > USER: U03 and U07 stored");
        for (i = 0; i < FAV_N; i++)
            fav[i] = FAV_NONE;
        fav[0] = 3;
        up_load(6);                                  /* U07 on the track, KNOB 4 right on PRESETS: a favourite */
        fav_toggle(1);
        fav[2] = 5;
        fav[3] = (uint16_t)(FAV_USER | 2u);
        fav[4] = 9;
        check(fav[1] == (FAV_USER | 6u) && fav_list(b) == 5u, "favourites: 3, U07, 5, U03, 9");
        fav_dirty = 0;
        up_ui(1, 6);                                 /* SAVE > USER > ERASE U07 */
        check(!up_used(6) && fav[0] == 3 && fav[1] == 5 && fav[2] == (FAV_USER | 2u) && fav[3] == 9 && fav[4] == FAV_NONE &&
              fav_dirty, "SAVE > USER > ERASE U07: its favourite gone, the others in their order (2.7.1)");
        TSEL->p[P_LEVEL] -= 7;                       /* another sound into U07 */
        up_ui(2, 6);
        check(up_used(6) && !fav_has(FAV_USER | 6u) && fav_list(b) == 4u, "a new sound saved into U07: not a favourite (2.7.1)");
        fav_dirty = 0;
        check(up_put(2, 0) == 0 && !up_used(2) && !fav_has(FAV_USER | 2u) && fav[2] == 9 && fav[3] == FAV_NONE && fav_dirty,
              "the editor's UP_ERASE of U03 (up_put): its favourite gone too (2.7.1)");
        up_load(6);                                  /* U07 a favourite again, then saved over (not erased): it stays */
        fav_toggle(1);
        TSEL->p[P_LEVEL] += 4;
        up_ui(2, 6);
        check(up_used(6) && fav_has(FAV_USER | 6u) && fav[0] == 3 && fav[1] == 5 && fav[2] == 9 && fav[3] == (FAV_USER | 6u),
              "U07 a favourite, saved over: still one (only an erase takes it out)");
        /* a favourite an older SLOOP left behind (2.7 erased the slot, kept the entry: hidden, taking a place) */
        fav[4] = (uint16_t)(FAV_USER | 9u);
        check(!up_used(9) && fav_list(b) == 4u, "U10 empty, its favourite left behind by an older SLOOP: hidden");
        TSEL->p[P_LEVEL] -= 2;                       /* a sound saved into U10 */
        up_ui(2, 9);
        check(up_used(9) && !fav_has(FAV_USER | 9u) && fav[4] == FAV_NONE && fav_list(b) == 4u,
              "a new sound saved into U10: not a favourite (2.7.1)");
        fav[4] = (uint16_t)(FAV_USER | 11u);         /* U12 empty, left behind too: power-on takes it out */
        fav_dirty = 0;
        up_boot();
        check(fav[3] == (FAV_USER | 6u) && fav[4] == FAV_NONE && fav_dirty, "power-on: U12's favourite left behind gone, U07's kept (2.7.1)");
        /* a user bank restored from a backup by the editor (ed_bk_commit id 6, n 0: the bank empty), its lines as it runs
         * them (editor.c is not in this harness): the bank written, read where it is now, its empty slots pruned */
        fav_dirty = 0;
        check(st_save(OBJ_UPRESET0, b, 0) == 0, "the user bank 1 restored empty");
        up_bank_find(0);
        up_fav_prune(0, UP_PER_BANK);
        check(!up_used(6) && !fav_has(FAV_USER | 6u) && fav[2] == 9 && fav[3] == FAV_NONE && fav_dirty,
              "the bank restored empty: U07 out of the favourites, the others kept (2.7.1)");
    }

    printf(fails ? "persist: %d FAILED\n" : "persist: all ok\n", fails);
    return fails != 0;
}
