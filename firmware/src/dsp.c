/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Shared DSP building blocks for the Felucca engines (all fixed point).
 * Voice output convention: add sample * amp to out[], where a full-scale
 * oscillator at amp = 1.0 (Q15 32767) contributes VOICE_FS. */
#define VOICE_FS 24000           /* per-voice level: one voice peaks near -6 dBFS before the master */

static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* a * k / 65536 without 64-bit: a up to 2^31, k Q16 */
static inline int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* sine, linearly interpolated between the 1024 table points (plain lookup: THD -55 dB) */
static inline int32_t sine_i(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static inline int32_t osc_sine(uint32_t ph) { return sine_i(ph); }

/* polyBLEP residual (Q15) around a wrap of a phase accumulator */
static inline int32_t blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                       /* 0..32767 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);       /* -32767..0 */
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}

static inline int32_t osc_saw(uint32_t ph, uint32_t inc)
{
    return ((int32_t)(ph >> 16) - 32768) - blep(ph, inc);
}

static inline int32_t osc_pulse(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (osc_saw(ph, inc) - osc_saw(ph + pw, inc)) >> 1;
}

static inline int32_t osc_tri(uint32_t ph)
{
    int32_t s = (int32_t)(ph >> 15);                 /* 0..131071 */
    return s < 65536 ? s - 32768 : 98303 - s;
}

/* tanh(x / 32768) * 32767, linearly interpolated (a plain lookup gives
 * 256-unit steps, audible on quiet signals) */
static inline int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = TANH_Q15[256];                           /* continuous with the table */
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

static inline uint32_t noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}

/* Trapezoidal SVF (A. Simper), unconditionally stable. Coefficients per
 * block in Q13; signals stay within +-150000 so products fit in 32 bits. */
typedef struct { int32_t a1, a2, a3; } tsvf_t;
static inline void tsvf_coef(tsvf_t *c, int32_t cut, int32_t reso)   /* cut: 0..127 << 8 */
{
    int32_t i, g, den, k = 8192 - reso * 7600 / 127;   /* damping 2.0 .. ~0.15 (Q12) */
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)                                       /* between table points: sweeps without 128 steps */
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}

static inline int32_t tsvf_lp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return v2;
}

/* Four-pole resonant ladder, Q15 audio, no oversampling and one tanh per sample.
 * The inexpensive four one-pole / nonlinear feedback topology is inspired by
 * Surge's legacy LPMOOGquad (Surge Synth Team, GPL-3.0-or-later), but this is a
 * scalar trapezoidal implementation, not its SIMD/one-sample-feedback port.
 * Solving the linear feedback loop avoids the high-cutoff instability of an
 * explicit Euler ladder. The existing interpolated TANH_Q15 table is shared.
 * All mutable state belongs to a voice or one track/channel; no shared scratch.
 * States carry eight extra fractional guard bits (Q23); coefficients and I/O
 * remain Q15. This avoids low-cutoff deadbands and impulse-response quantization.
 * Wide products are integer intermediates (no floating point or 64-bit divide).
 */
typedef struct { int32_t s[4]; } ladder_t;
#ifndef LADDER_K127
#define LADDER_K127 147456           /* 4.5: self-oscillation at -19 dBFS from 49 Hz up (tests/synth_dsp_test.c) */
#endif
/* Rounded to nearest, half away from zero. A loop of floors (or of half-up roundings) leans one
 * way: the states rest a few Q23 units off zero, the output never returns to exactly 0, and the
 * bias eats the 1 % of loop gain that self-oscillation lives on at a low cutoff (tests/synth_dsp_test.c:
 * half-up rounding kills it below 63 Hz). The residual that rounding leaves is walked out by
 * ladder_run's deadband. */
static inline int32_t ladder_mul(int32_t a, int32_t b)
{
    int64_t p = (int64_t)a * b;
    return (int32_t)((p + 16384 - (p < 0)) >> 15);
}
typedef struct { int32_t g, k, norm; } ladder_coef_t;
/* The DC solution for an input in with the loop's feedback k: every stage at in / (1 + k) */
static void ladder_seed(ladder_t *l, const ladder_coef_t *c, int32_t in)
{
    uint32_t i;
    int32_t y = (int32_t)(((int64_t)clamp(in, -32768, 32767) * 256 * 32768) / (32768 + c->k));
    for (i = 0; i < 4u; i++) l->s[i] = y;
}
/* Every stage at y (Q23): the output carries on from y (fx.c track_filter_run: the SVF's output when the TYPE turns
 * to LP24 under a note) and moves from there at the cutoff's pace */
static void ladder_hold(ladder_t *l, int32_t y)
{
    uint32_t i;
    y = clamp(y, -16777216, 16777216);
    for (i = 0; i < 4u; i++) l->s[i] = y;
}
static void ladder_coef(ladder_coef_t *c, int32_t cut, int32_t reso)
{
    int32_t i, g, g2, g4;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    c->g = (g * 32768) / (4096 + g);
    /* RES 127 reaches LADDER_K127 / 32768 of feedback: above the 4 of the ideal ladder, so that
     * self-oscillation survives the fixed-point losses down to the lowest cutoffs (the tanh table
     * bounds it); ordinary settings stay well below it. */
    c->k = clamp(reso, 0, 127) * LADDER_K127 / 127;
    g2 = ladder_mul(c->g, c->g);
    g4 = ladder_mul(g2, g2);
    c->norm = (int32_t)((1u << 30) / (uint32_t)(32768 + ladder_mul(c->k, g4)));
}
/* One sample: in Q15, the result in Q23 (the stages' resolution); ladder_q15 / ladder_bus scale it */
static __attribute__((noinline)) int32_t ladder_run(ladder_t *l, const ladder_coef_t *c, int32_t in)
{
    int32_t h = 32768 - c->g;
    int32_t sum = l->s[0], x;
    uint32_t i;
    /* Output = G^4 input + (1-G)(G^3 s0 + G^2 s1 + G s2 + s3). */
    for (i = 1; i < 4u; i++)
        sum = ladder_mul(sum, c->g) + l->s[i];
    sum = ladder_mul(sum, h);
    x = ladder_mul(clamp(in, -32768, 32767) * 256 - ladder_mul(c->k, sum), c->norm);
    {   /* The existing tanh table, interpolated with Q23 input and output. */
        int32_t a = x < 0 ? -x : x, q = a >> 16, y;
        y = q >= 256 ? TANH_Q15[256] * 256 : TANH_Q15[q] * 256 +
            (((TANH_Q15[q + 1] - TANH_Q15[q]) * (a & 65535)) >> 8);
        x = x < 0 ? -y : y;
    }
    for (i = 0; i < 4u; i++) {
        int32_t v = ladder_mul(x - l->s[i], c->g);
        x = v + l->s[i];
        l->s[i] = clamp(x + v, -16777216, 16777216);
    }
    if (!in && (uint32_t)(l->s[3] + 4096) < 8192u && (uint32_t)(l->s[0] + 4096) < 8192u &&
        (uint32_t)(l->s[1] + 4096) < 8192u && (uint32_t)(l->s[2] + 4096) < 8192u)
        for (i = 0; i < 4u; i++)                        /* no input and every stage below 16 LSB: they walk to
                                                         * exactly 0 (a low cutoff's steps round to nothing: the
                                                         * states would rest a few units off, a DC for ever). All
                                                         * four: an oscillation starting at a low cutoff grows by
                                                         * less than a unit a sample in its last stage */
            l->s[i] -= (l->s[i] > 0) - (l->s[i] < 0);
    return x;
}
/* a voice: Q15 in, Q15 out, rounded (so a tail below half an LSB is exactly 0) */
static inline int32_t ladder_q15(ladder_t *l, const ladder_coef_t *c, int32_t in)
{
    return clamp((ladder_run(l, c, in) + 128) >> 8, -32768, 32767);
}
/* a track bus (fx.c: +-2^20 of headroom, as the SVF path): in / 32 to the ladder, its Q23 / 8 back */
static inline int32_t ladder_bus(ladder_t *l, const ladder_coef_t *c, int32_t b)
{
    int32_t x = ladder_run(l, c, clamp(b >> 5, -32768, 32767));
    return clamp((x + 4) >> 3, -1048576, 1048575);
}

/* amplitude ramp over the block. Blocks are always CTL long, so x / CTL is a
 * shift rounded towards zero (-Os would keep a hardware divide per sample) */
#define CTL_LOG2 5
#if (1 << CTL_LOG2) != CTL
#error "CTL_LOG2 does not match CTL"
#endif
static inline int32_t amp_at(const vmod_t *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (CTL - 1))) >> CTL_LOG2);
}
