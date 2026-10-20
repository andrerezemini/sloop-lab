/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Adapted for sloop-lab: per-part shadow parameters and PRNGs, no restore into UI state.
 * Sources and gain/pitch scaling follow Felucca's modulation matrix (mod.c).
 * RATE/DEPTH use the preceding control block, avoiding a cyclic evaluation. */
#define NMSLOT 4u
#define MD_ENGINE(d) ((d) >= MD_E1 && (d) < MD_E1 + 8u)
#define MS_BIPOLAR(s) ((s) == MS_LFO || (s) == MS_KEY || (s) == MS_RAND || (s) >= MS_SH)
_Static_assert(NELEM(N_MSRC) == MS_N && NELEM(N_MDST) == MD_N, "matrix descriptors");
static int16_t mod_pm[NPART][P_COUNT] __attribute__((section(".lowram")));
static struct { uint8_t on; int32_t depth; } mod_state[NPART];
static uint32_t mod_random(uint32_t *seed)
{
    uint32_t x = *seed ? *seed : 625341585u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *seed = x;
}
static int32_t mod_key(uint32_t note) { return clamp(((int32_t)note - 60) * 512, -32767, 32767); }
static int32_t mod_source(const track_t *t, const voice_t *v, uint32_t s, int32_t env, int32_t lfo)
{
    switch (s) {
    case MS_LFO: return lfo;
    case MS_LFO2: return t->lfo2_val;
    case MS_ENV: return v ? env : t->m_env;
    case MS_VEL: return (v ? v->mvel : t->m_vel) * 258;
    case MS_KEY: return mod_key(v ? v->note : t->m_key);
    case MS_RAND: return v ? v->mrnd : t->m_rnd;
    case MS_MODW: return t->mw * 258;
    case MS_AT: return t->at * 258;
    case MS_EXPR: return (127 - t->ex_off) * 258;
    case MS_SH: return t->mod_hold;
    case MS_SLEW: return t->lfo_prev + (((t->mod_hold - t->lfo_prev) * (int32_t)(t->lfo_ph >> 17)) >> 15);
    default: return 0;
    }
}
static int32_t mod_gain(uint32_t s, int32_t x, int32_t a)
{
    int32_t u = MS_BIPOLAR(s) ? (x + 32768) >> 1 : x;
    return clamp(32767 - (a > 0 ? ((32767 - u) * a) >> 6 : (u * -a) >> 6), 0, 32767);
}
static uint32_t mod_param(uint32_t d)
{
    static const uint8_t ids[7] = {P_PAN, P_DIST, P_CHOR, P_DLY, P_REV, P_LRATE, P_LD_PIT};
    return MD_ENGINE(d) ? P_E0 + d - MD_E1 : ids[d - MD_PAN];
}
static const param_desc_t *mod_desc(const track_t *t, const int16_t *p, uint32_t id)
{
    if (id < P_E0) return &TP[id];
    const engine_t *e = ENGINES[t->engine];
    uint32_t k = id-P_E0;
    const param_desc_t *d = e == &ENG_LOFI ? lofi_desc_values(p,k) :
        e == &ENG_PHYS ? phys_desc_values(p,k) : e == &ENG_NOISE ? noise_desc_values(p,k) : 0;
    return d ? d : &e->edit[k];
}
static int mod_rate_active(const int16_t *p)
{
    uint32_t k;
    for (k = 0; k < NMSLOT; k++)
        if (p[P_M1SRC + k * 3] && p[P_M1AMT + k * 3] && p[P_M1DST + k * 3] == MD_RATE)
            return 1;
    return 0;
}
/* The only renderer-visible parameter vector. Each part writes only its own shadow;
 * p[], p_old[] and UI writes are never restored or temporarily replaced. */
static __attribute__((noinline)) void mod_begin(track_t *t)
{
    uint32_t part = (uint32_t)(t - trk) % NPART, k, j, active = 0, fade = t->xf_on && t->xf;
    if (t - trk >= NPART) { t->rp = t->p; return; }   /* (the drums: no shadow of their own) */
    const int16_t *base = fade && t->pold ? t->p_old : t->p;
    int16_t *pm = mod_pm[part];
    int32_t lfo = mulq15(t->lfo_val, t->lfo_fade), depth = 32767;
    int32_t offsets[NMSLOT] = {0};
    uint8_t ids[NMSLOT]; uint32_t ni = 0;
    for (k = 0; k < NMSLOT; k++)
        active |= base[P_M1SRC + 3*k] && base[P_M1DST + 3*k] && base[P_M1AMT + 3*k];
    mod_state[part].on = (uint8_t)active;
    mod_state[part].depth = 32767;
    t->rp = base;
    if (!active && !fade) return;
    memcpy(pm, base, sizeof mod_pm[0]);
    if (fade) for (k = 0; k < 8; k++) pm[P_E0+k] = t->pe_old[k];
    t->rp = pm;
    if (!active) return;
    for (k = 0; k < NMSLOT; k++) {
        uint32_t s = (uint32_t)base[P_M1SRC+3*k];
        int32_t a = base[P_M1AMT+3*k];
        if (s && s < MS_N && a && base[P_M1DST+3*k] == MD_DEPTH)
            depth = mulq15(depth, mod_gain(s == MS_LFO && base[P_LPOL] ? MS_ENV : s,
                mod_source(t, 0, s, 0, lfo), a));
    }
    mod_state[part].depth = depth;
    if (depth < 32767) lfo = mulq15(lfo, depth);
    for (k = 0; k < NMSLOT; k++) {
        uint32_t s = (uint32_t)base[P_M1SRC+3*k], d = (uint32_t)base[P_M1DST+3*k], id;
        int32_t a = base[P_M1AMT+3*k];
        const param_desc_t *pd;
        if (!s || s >= MS_N || !a || d < MD_PAN || d >= MD_DEPTH) continue;
        id = mod_param(d);
        pd = mod_desc(t, pm, id);
        /* Enumerations and stepped name lists select algorithms/tables. Never modulate them. */
        if (pd->fmt == F_ENUM || pd->names || pd->max <= pd->min) continue;
        for (j = 0; j < ni && ids[j] != id; j++);
        if (j == ni) ids[ni++] = (uint8_t)id;
        offsets[j] += (((mod_source(t, 0, s, 0, lfo) * a) >> 6) * (pd->max-pd->min)) >> 15;
    }
    for (j = 0; j < ni; j++) {
        uint32_t id = ids[j];
        const param_desc_t *pd = mod_desc(t, pm, id);
        /* DIST 0 is the drive off (fx.c track_dist), and coming back on re-seeds its low cut and tone: a DIST that is
         * on stays on under modulation (1 at the least), or a source swinging it through 0 thumps every half cycle */
        int32_t lo = id == P_DIST && pm[id] > 0 ? 1 : pd->min;
        pm[id] = (int16_t)clamp(pm[id]+offsets[j], lo, pd->max);
    }
}
static __attribute__((noinline)) void mod_voice(track_t *t, voice_t *v, vmod_t *m, int32_t lfo)
{
    const int16_t *p = render_p(t);
    int32_t pit = 0, cut = 0, shp = 0, gain = 32767;
    uint32_t k, amp = 0;
    if ((uint32_t)(v-t->v) == t->m_vi) t->m_env = m->envq15;
    for (k = 0; k < NMSLOT; k++) {
        uint32_t s = (uint32_t)p[P_M1SRC+3*k], d = (uint32_t)p[P_M1DST+3*k];
        int32_t a = p[P_M1AMT+3*k], x;
        if (!s || s >= MS_N || !a || !d || d > MD_AMP) continue;
        x = mod_source(t, v, s, m->envq15, lfo);
        if (d == MD_PITCH) pit += (x*a*3) >> 15;
        else if (d == MD_CUT) cut += (x*a) >> 7;
        else if (d == MD_SHP) shp += (x*a) >> 7;
        else { amp = 1; gain = mulq15(gain, mod_gain(s == MS_LFO && p[P_LPOL] ? MS_ENV : s, x, a)); }
    }
    if (pit) {
        m->pitch16 = clamp(m->pitch16+pit, 0, 2047);
        m->inc = pitch_inc(m->pitch16);
        if (m->fine) m->inc += (uint32_t)((int32_t)(m->inc >> 12)*m->fine);
    }
    m->cutoff = clamp(m->cutoff+cut, -150*256, 150*256);
    m->shape = clamp(m->shape+shp, -62*256, 190*256);
    if (amp) v->env_out = m->amp1 = mulq15(m->amp1, gain);
}
/* Note and gate destinations use the unmodified sound and current controllers.
 * TIME changes gate duration, never the transport grid or note ordering. */
static int32_t mod_event_offset(const track_t *t, uint32_t dst)
{
    uint32_t k; int32_t off = 0, lfo = mulq15(t->lfo_val, t->lfo_fade);
    for (k = 0; k < NMSLOT; k++) {
        uint32_t s = (uint32_t)t->p[P_M1SRC+3*k];
        if (s && s < MS_N && t->p[P_M1DST+3*k] == (int32_t)dst)
            off += (mod_source(t, 0, s, 0, lfo)*t->p[P_M1AMT+3*k]) >> 15;
    }
    return off;
}
static uint32_t mod_note(track_t *t, uint32_t note, uint32_t vel)
{
    t->m_key = (uint8_t)note; t->m_vel = (uint8_t)vel;
    t->m_rnd = (int16_t)(mod_random(&t->mod_seed) >> 16);
    return (uint32_t)clamp((int32_t)vel + mod_event_offset(t, MD_VEL)*2, 1, 127);
}
static uint32_t mod_time(const track_t *t, uint32_t duration)
{
    int32_t off = mod_event_offset(t, MD_TIME);
    if (!off) return duration;
    /* bounded 1/8..2x; split multiplication avoids overflow in clock units */
    uint32_t gain = (uint32_t)clamp(128+off*2, 16, 256);
    return duration/128u*gain + (duration%128u)*gain/128u;
}
static __attribute__((noinline)) void mod_midi(track_t *t, uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st == 0xD0u) t->at = (uint8_t)d1;
    else if (st == 0xB0u) {
        if (d1 == 1u) t->mw = (uint8_t)d2;
        else if (d1 == 11u) t->ex_off = (uint8_t)(127u-d2);
        else if (d1 == 121u) t->mw = t->at = t->ex_off = 0;
    }
}

/* Engine destinations use the current editor labels on the panel. */
static const char *mod_dst_name(const track_t *t, int32_t d)
{
    if (d >= MD_E1 && d < MD_DEPTH) {
        const param_desc_t *p = track_desc(t, P_E0 + (uint32_t)(d-MD_E1));
        if (p->fmt == F_ENUM || p->names || p->max <= p->min) return "--";
        if (p->label && p->label[0]) return p->label;
    }
    return N_MDST[clamp(d,0,MD_N-1)];
}
