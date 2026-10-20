/* SPDX-License-Identifier: GPL-3.0-only */
/* (synth) The PRESETS list of each sample profile, with the real UI (the ui_pages_test harness). The lite build
 * leaves the FLUTE and SCRCH sets empty and does not offer their three sounds (engines.c preset_hidden: LOFI FLUTE,
 * SCRATCH, FLUTE DUST), which stay in BANK and in the engines' tables so that a stored number means the same sound
 * in every build. tests/run_tests.sh builds this twice, with the full and with the lite samples header:
 *   favourites   a hidden sound (a project or another build loaded it) is not taken as a favourite: fav_list would
 *                never show the entry, and it would hold one of the 16 places; one another build stored still goes
 *                with KNOB 4 left
 *   Program Change   PC n (MIDI's 0..127) is BANK n on every build, the numbers of SLOOP 2.7.1 and of the full
 *                profile (LOFI FLUTE 76 and FLUTE DUST 77 keep theirs and load their silent preset, as a project naming
 *                them does); past 127, which MIDI cannot send, preset_go_to still counts the rest of BANK, then the
 *                user presets, then nothing */
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"

static uint32_t bank_find(const char *name)
{
    uint32_t n;
    for (n = 0; n < NBANK && !str_eq(BANK[n].name, name); n++)
        ;
    assert(n < NBANK);
    return n;
}
static int bank_hidden(uint32_t n) { return bank_pi[n] != 0xFF && preset_hidden(BANK[n].e % NENGINES, bank_pi[n]); }

int main(int argc, char **argv)
{
    static const char *const HIDDEN[3] = {"LOFI FLUTE", "SCRATCH", "FLUTE DUST"};
    uint32_t i, k, pg, hid = 0;
    int lite = SMP_SET_EMPTY != 0u;
    outdir = argc > 1 ? argv[1] : "build/host";
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    bank_resolve();
    for (i = 0; i < NBANK; i++)
        hid += (uint32_t)bank_hidden(i);
    for (i = 0; i < 3u; i++)
        hid += lite ? 0u : (uint32_t)bank_hidden(bank_find(HIDDEN[i]));
    check(hid == (lite ? 3u : 0u) && bank_nvis + hid == NBANK,
          lite ? "lite: LOFI FLUTE, SCRATCH, FLUTE DUST in BANK, not in the list" : "full: every BANK sound in the list");
    for (pg = 0; pg < NPAGES && strcmp(PAGES[pg].title, "PRESETS"); pg++)
        ;
    assert(pg < NPAGES);
    for (i = 0; i < 3u; i++) {   /* each of the three on track 2, as a project loads it; the PRESETS page, KNOB 4 */
        uint32_t n = bank_find(HIDDEN[i]);
        int ok;
        char what[100];
        song.sel = 1; ui.menu = 0; go_home(); frames(2);
        for (k = 0; k < FAV_N; k++) fav[k] = FAV_NONE;
        fav_at = -1; fav_dirty = 0;
        set_engine_of(TSEL, BANK[n].e); apply_preset_to(TSEL, bank_pi[n]); TSEL->engine = TSEL->eng_req;
        open_family(PAGES[pg].fam); ui.page = (uint8_t)pg; ui.fam_last[PAGES[pg].fam] = (uint8_t)pg; page_entered();
        ui.force = 1; frames(2);
        ui.msg[0] = 0;
        encs[panel.enc[EN_K4]] = 1; frames(2);       /* right: add */
        if (!i) { ui.force = 1; frame(); ppm(lite ? "page-presets-fav-lite" : "page-presets-fav-full"); }
        if (lite)
            ok = fav[0] == FAV_NONE && !fav_dirty && strcmp(ui.msg, "FAV ADDED") && ui.msg[0];
        else
            ok = fav[0] == n && fav_dirty && !strcmp(ui.msg, "FAV ADDED");
        snprintf(what, sizeof what, "%s: KNOB 4 right on %s: %s (%s)", lite ? "lite" : "full", HIDDEN[i],
                 lite ? "refused, said, nothing stored" : "a favourite", ui.msg);
        check(ok, what);
        for (k = 0; k < FAV_N; k++) fav[k] = FAV_NONE;
        fav[0] = (uint16_t)n; fav_dirty = 0;         /* one stored by another build (full, 2.7.1): left takes it out */
        ui.msg[0] = 0;
        encs[panel.enc[EN_K4]] = -1; frames(2);
        ok = fav[0] == FAV_NONE && fav_dirty && !strcmp(ui.msg, "FAV REMOVED");
        snprintf(what, sizeof what, "%s: KNOB 4 left on %s stored as a favourite: removed", lite ? "lite" : "full", HIDDEN[i]);
        check(ok, what);
    }
    for (k = 0; k < FAV_N; k++) fav[k] = FAV_NONE;
    go_home(); frames(2);
    {   /* Program Change on track 3's channel as a MIDI controller sends it: the USB-MIDI packet (seq.c takes its data
         * byte, 0..127, and queues it), loaded in the main loop (pc_poll): PC n is BANK n */
        uint32_t n, ch = (uint32_t)song.g[G_CH1 + 2] - 1u, bad = NBANK, be = 0, bp = 0;
        char what[100], msg[16];
        assert(!song.g[G_ROUTE] && song.g[G_CH1 + 2] && midi_track(ch) == &trk[2]);
        for (n = 0; n < 128u; n++) {
            set_engine_of(&trk[2], BANK[(n + 1u) % NBANK].e);   /* (another sound first: the change is seen) */
            apply_preset_to(&trk[2], bank_pi[(n + 1u) % NBANK]);
            ui.msg[0] = 0;
            midi_in_q[mi_w++ % MQ] = 0x0Cu | (0xC0u | ch) << 8 | n << 16;
            frame();
            snprintf(msg, sizeof msg, "T3 PROG %u", (unsigned)n);
            if (!(trk[2].eng_req == BANK[n].e && trk[2].preset == bank_pi[n] && !strcmp(ui.msg, msg)) && bad == NBANK)
                bad = n, be = trk[2].eng_req, bp = trk[2].preset;
        }
        if (bad < NBANK)
            printf("ui: PC %u loaded engine %u preset %u, BANK %u is %s (engine %u preset %u)\n", (unsigned)bad,
                   (unsigned)be, (unsigned)bp, (unsigned)bad, BANK[bad].name, BANK[bad].e, bank_pi[bad]);
        snprintf(what, sizeof what, "%s: MIDI Program Change 0 .. 127 on CH %u: track 3 takes BANK n", lite ? "lite" : "full",
                 (unsigned)ch + 1u);
        check(bad == NBANK, what);
    }
    {   /* past 127, which no Program Change carries: preset_go_to's own count, the rest of BANK, the user presets after
         * it (the slot up_nth gives), then nothing */
        uint32_t n, bad = NBANK;
        char what[100];
        int ok = 1;
        for (n = 128u; n < NBANK; n++)
            if ((preset_go_to(&trk[2], n) || trk[2].eng_req != BANK[n].e || trk[2].preset != bank_pi[n]) && bad == NBANK)
                bad = n;
        if (bad < NBANK)
            printf("ui: preset_go_to %u did not load BANK %u (%s)\n", (unsigned)bad, (unsigned)bad, BANK[bad].name);
        ok &= bad == NBANK;
        for (n = 0; n < up_count(); n++) {
            up_load_k = ~0u;
            ok &= preset_go_to(&trk[2], NBANK + n) == 0 && up_load_k == up_nth(n);
        }
        up_load_k = ~0u;
        ok &= preset_go_to(&trk[2], NBANK + up_count()) == 1 && up_load_k == ~0u;   /* past */
        snprintf(what, sizeof what, "%s: preset_go_to 128 .. %u = BANK n, then user preset n - %u, then nothing",
                 lite ? "lite" : "full", (unsigned)NBANK - 1u, (unsigned)NBANK);
        check(ok, what);
    }
    printf("ui: %s\n", fails ? "FAILED" : (lite ? "lite PRESETS list: hidden sounds not favourites, Program Change = BANK PASS"
                                                : "full PRESETS list: every sound a favourite, Program Change = BANK PASS"));
    return fails;
}
