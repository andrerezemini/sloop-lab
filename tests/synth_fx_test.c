/* SPDX-License-Identifier: GPL-3.0-only */
/* synth (sloop-lab): a part's effects under the synth's additions, rendered through mix_block, as fx251_test.c does
 *   dist mod   DIST 20 taken through 0 by the matrix (LFO -> DIST AMT -40): the drive stays on, no re-seeded filter
 *              (fx.c track_dist treats 0 as off and re-seeds when it comes back: a thump every half cycle)
 *   insert fade a sound loaded under a held note with an INSERT on: the 8 blocks of the fade play the old sound through
 *              the old INSERT (the render view), whatever INSERT the new sound has
 *   tflt type  the track FILTER's TYPE switched SVF <-> LP24 under a note, under a sweep and over a closed cutoff:
 *              each type starts where the other's output was (not the SVF's states of before the ladder took over,
 *              not the input): no step bigger than the filters' own
 * Exit status: the number of failed checks. */
#include <stdint.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/mman.h>
static int fails;
static void check(int ok, const char *what)
{
    printf("synth fx: %-86s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
#define NB 2000u                                      /* blocks (1.45 s) */
static int32_t out_l[NB * CTL];
/* nb blocks of the mix's left channel into out_l from block at */
static void render_at(uint32_t at, uint32_t nb)
{
    static int32_t o[2u * CTL];
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            out_l[(at + b) * CTL + i] = o[2u * i];
    }
}
/* the largest second difference over samples a..b: a step (a click) where the signal itself is smooth */
static double d2_of(uint32_t a, uint32_t b)
{
    double m = 0, d;
    uint32_t i;
    for (i = a + 2u; i < b; i++)
        if ((d = fabs((double)out_l[i] - 2.0 * out_l[i - 1] + out_l[i - 2])) > m)
            m = d;
    return m;
}
/* the run of fn(arg) in a child process from this very state (the voices, the buses, the random numbers): its nb
 * blocks from block at, in shared memory; this process goes on from the same state */
static int32_t *shared;
static void fork_run(void (*fn)(int), int arg, uint32_t at, uint32_t nb)
{
    pid_t pid;
    if (!shared)
        shared = mmap(0, NB * CTL * 4u, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    fflush(stdout);
    pid = fork();
    if (!pid) {
        fn(arg);
        render_at(at, nb);
        memcpy(shared + at * CTL, out_l + at * CTL, nb * CTL * 4u);
        _exit(0);
    }
    waitpid(pid, 0, 0);
}
static void slot(track_t *t, int k, int src, int dst, int amt)
{
    t->p[P_M1SRC + 3 * k] = (int16_t)src, t->p[P_M1DST + 3 * k] = (int16_t)dst, t->p[P_M1AMT + 3 * k] = (int16_t)amt;
}
/* a clean start: the defaults, the INSERT states at rest, no sends; track 1 an ANALOG sine, one oscillator, open,
 * held at full sustain; the master low (the limiter never acts: the mix comes out as the part made it) */
static void setup(void)
{
    uint32_t i;
    host_tracks_init();
    memset(ins, 0, sizeof ins);
    memset(tflt, 0, sizeof tflt);
    memset(track_ladder_on, 0, sizeof track_ladder_on);
    for (i = 0; i < NTRK; i++) {
        trk[i].ins_run = 0;
        trk[i].tail = 0;
        trk[i].dist_on = 0;
        trk[i].p[P_REV] = trk[i].p[P_CHOR] = trk[i].p[P_DLY] = 0;
        trk[i].p[P_LEVEL] = 100;
    }
    host_preset(&trk[0], 0, 0);
    trk[0].p[P_E0] = 3, trk[0].p[P_E1] = 0, trk[0].p[P_E2] = 0, trk[0].p[P_E3] = 0;   /* SIN, one oscillator */
    trk[0].p[P_E4] = 127, trk[0].p[P_E5] = 0, trk[0].p[P_E6] = 0, trk[0].p[P_E7] = 0;  /* open, no RES, DRV, KTR */
    trk[0].p[P_ATK] = 0, trk[0].p[P_DEC] = 0, trk[0].p[P_SUS] = 127, trk[0].p[P_REL] = 0;
    trk[0].p[P_ED_FLT] = trk[0].p[P_ED_PIT] = trk[0].p[P_ED_SHP] = 0;
    trk[0].p[P_LD_PIT] = trk[0].p[P_LD_FLT] = trk[0].p[P_LD_SHP] = trk[0].p[P_LD_AMP] = 0;
    trk[0].p[P_VOICE] = V_POLY, trk[0].p[P_GLIDE] = 0, trk[0].p[P_DIST] = 0, trk[0].p[P_TRANS] = 0;
    trk[0].p[P_ED_FX] = 0;
    memset(trk[0].p + P_M1SRC, 0, 12u * sizeof(int16_t));
    for (i = 0; i < NVOICE; i++)
        trk[0].v[i].active = 0;
    song.playing = 0;
    song.master_q12 = 2048;
    texture_reset(0);
    memset(&lim_dac, 0, sizeof lim_dac);
    lim_dac.env = LIM_T, lim_dac.g = 32768, lim_dac.hold = 0;
    tx.g = 0;
    song.g[G_TXSRC] = 0;
}

/* B9 (VARREDURA 19/10): DIST 20 with LFO -> DIST AMT -40 swings the modulated DIST over 0..99. The drive must stay on
 * through it: 0 is DIST off (track_dist), and coming back on re-seeds its low cut and zeroes its tone poles, a step
 * (a thump) at every crossing. The reference: the same sine and LFO over DIST 60, AMT -20 (21..99), the block-by-block
 * staircase of a modulated DIST that never reaches 0 */
static double dist_mod_run(int dist, int amt, uint32_t *offs)
{
    uint32_t b;
    setup();
    trk[0].p[P_DIST] = (int16_t)dist;
    trk[0].p[P_LRATE] = 80, trk[0].p[P_LWAVE] = 0;   /* ~3.4 Hz */
    slot(&trk[0], 0, MS_LFO, MD_DIST, amt);
    trk_note_on(&trk[0], 45, 100);                   /* A2: 110 Hz */
    render_at(0, 100);
    *offs = 0;
    for (b = 100; b < 1500u; b++) {
        render_at(b, 1);
        *offs += !trk[0].dist_on;
    }
    return d2_of(100u * CTL, 1500u * CTL);
}
static void t_dist_mod(void)
{
    uint32_t offs_ref, offs;
    double ref = dist_mod_run(60, -20, &offs_ref), sw = dist_mod_run(20, -40, &offs);
    printf("  DIST under LFO: worst sample step %.0f (DIST 60 +- 39: %.0f); blocks with the drive off %u\n", sw, ref, offs);
    check(!offs_ref && !offs && sw < 2.0 * ref + 50.0,
          "DIST modulated through 0 (LFO -> DIST): the drive stays on, no re-seed, no step (B9)");
}

/* B10: a sound loaded as ui.c apply_preset_to loads one (sound_change, the panic, the new values) over a held note
 * that goes through FOLD at full MIX. The fade's 8 blocks render the old sound (mod_begin: p_old), the INSERT too:
 * the new sound's INSERT must not be heard before the fade ends. Expected: the same load with the new sound's INSERT
 * equal to the old one (the child). The new sound here leaves the INSERT OFF, as a factory preset does */
static void load_sound(int same_insert)
{
    track_t *t = &trk[0];
    sound_change(t);
    panic_req |= 1u;
    host_preset_req(t, 0, 3);
    t->p[P_ITYPE] = (int16_t)(same_insert ? IT_FOLD : IT_OFF);
}
static void t_insert_fade(void)
{
    uint32_t a = 300u, nb = 24u, fade = 0, b;
    double d = 0, r = 0;
    setup();
    trk[0].p[P_REL] = 40;                             /* (the panic releases the note: a release to fade) */
    trk[0].p[P_ITYPE] = IT_FOLD, trk[0].p[P_IA] = 100, trk[0].p[P_IB] = 96, trk[0].p[P_IC] = 96, trk[0].p[P_IMIX] = 127;
    trk_note_on(&trk[0], 45, 100);
    render_at(0, a);
    fork_run(load_sound, 1, a, nb);
    load_sound(0);
    for (b = 0; b < nb; b++) {
        render_at(a + b, 1);
        fade += trk[0].xf_on && trk[0].pold;
    }
    for (b = a * CTL; b < (a + XF_BLOCKS) * CTL; b++) {
        d += fabs((double)out_l[b] - shared[b]);
        r += (double)shared[b] * shared[b];
    }
    r = sqrt(r / (XF_BLOCKS * CTL));
    printf("  INSERT under a preset fade: %u blocks of fade, rms %.0f, mean |diff| to the old INSERT's %.1f\n", fade, r,
           d / (XF_BLOCKS * CTL));
    check(fade >= XF_BLOCKS - 1u && r > 200.0 && d == 0.0,
          "a sound loaded under a held note: its fade plays through the old INSERT, bit for bit (B10)");
}

/* B11: the track FILTER's TYPE switched under a held note, four times (four phases of the note at the switches), RES 0
 * on both types (TRES -40). The switch must leave no step bigger than the filters' own (the largest second difference
 * of the steady sound on the SVF, then on LP24): each type starts where the other's output was. Before, the SVF came
 * back on the states it had when the ladder took over, and the ladder started at rest on the input: right only while
 * the filter passes the note. Two sounds: a sine well under the cutoff (-20) with a sweep to -40 and back on LP24 (the
 * SVF's old states from another cutoff), and a saw at C5 at -60 (the filter closed on it, its usual sound) */
static void tflt_switch(int wave, int note, int cut, int sweep, double *to24, double *back, double *own)
{
    static const uint16_t LADDER_BLOCKS[4] = {301, 304, 307, 310};
    uint32_t a = 0, k;
    double d;
    setup();
    trk[0].p[P_E0] = (int16_t)wave;
    trk[0].p[P_TFLT] = (int16_t)cut, trk[0].p[P_TRES] = -40, trk[0].p[P_TFTYPE] = 0;
    trk_note_on(&trk[0], note, 100);
    render_at(a, 300), a += 300;
    *own = d2_of((a - 100u) * CTL, a * CTL), *to24 = *back = 0;
    for (k = 0; k < 4u; k++) {
        uint32_t n = LADDER_BLOCKS[k];
        trk[0].p[P_TFTYPE] = 1;
        render_at(a, 4);
        d = d2_of((a - 1u) * CTL, (a + 4u) * CTL), *to24 = d > *to24 ? d : *to24, a += 4;
        trk[0].p[P_TFLT] = (int16_t)sweep;
        render_at(a, n / 2u), a += n / 2u;
        trk[0].p[P_TFLT] = (int16_t)cut;
        render_at(a, n - 4u - n / 2u), a += n - 4u - n / 2u;
        d = d2_of((a - 100u) * CTL, a * CTL), *own = d > *own ? d : *own;
        trk[0].p[P_TFTYPE] = 0;
        render_at(a, 4);
        d = d2_of((a - 1u) * CTL, (a + 4u) * CTL), *back = d > *back ? d : *back, a += 4;
        render_at(a, 56), a += 56;
    }
}
static void t_tflt_type(void)
{
    double to24, back, own;
    tflt_switch(3, 45, -20, -40, &to24, &back, &own);   /* SIN A2: 110 Hz, the cutoff some kHz above it */
    printf("  track FILTER TYPE, a sine under a sweep: worst sample step SVF -> LP24 %.0f, LP24 -> SVF %.0f "
           "(the filters' own %.0f)\n", to24, back, own);
    check(to24 < 3.0 * own + 100.0 && back < 3.0 * own + 100.0,
          "track FILTER TYPE SVF <-> LP24 under a sweep: each starts where the other was, no step (B11)");
    tflt_switch(0, 72, -60, -60, &to24, &back, &own);   /* SAW C5 at -60: the filter closed on the note */
    printf("  track FILTER TYPE, a saw over a closed cutoff: worst sample step SVF -> LP24 %.0f, LP24 -> SVF %.0f (the "
           "filters' own %.0f)\n", to24, back, own);
    check(to24 < 3.0 * own + 100.0 && back < 3.0 * own + 100.0,
          "track FILTER TYPE SVF <-> LP24 over a closed cutoff: not from the input, no step (B11)");
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    t_dist_mod();
    t_insert_fade();
    t_tflt_type();
    printf("synth fx: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
