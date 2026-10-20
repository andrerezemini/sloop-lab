/* SPDX-License-Identifier: GPL-3.0-only */
/* synth (sloop-lab): a part's effects under the synth's additions, rendered through mix_block, as fx251_test.c does
 *   dist mod   DIST 20 taken through 0 by the matrix (LFO -> DIST AMT -40): the drive stays on, no re-seeded filter
 *              (fx.c track_dist treats 0 as off and re-seeds when it comes back: a thump every half cycle)
 * Exit status: the number of failed checks. */
#include <stdint.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
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

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    t_dist_mod();
    printf("synth fx: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
