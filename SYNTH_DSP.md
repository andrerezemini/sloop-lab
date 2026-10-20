# synth: ladder, expressive presets and Wave Terrain

This note covers S5–S7. Hardware timing and listening checks remain separate
acceptance steps; the numbers below are host measurements, not FM-1 results.

## Ladder

`dsp.c` adds four trapezoidal one-pole stages with a solved linear feedback
loop and one interpolated tanh per sample. Audio and coefficients are Q15;
four `int32_t` states retain eight extra fractional bits (Q23) to avoid a
low-frequency deadband. Integer 64-bit multiply intermediates are used; no
floating point, allocation, 64-bit division or oversampling enters the filter.
The existing 257-point tanh table and the SVF cutoff table are reused: no new
lookup table or per-voice storage is allocated. ANALOG uses formerly unused
voice state words 3–6. Track filters have independent channel states.

This follows the economical nonlinear four-pole idea of Surge's legacy
[LPMOOGquad](https://github.com/surge-synthesizer/sst-filters/blob/main/include/sst/filters/QuadFilterUnit_Impl.h).
It is **not** a verbatim port: the legacy implementation has five states and
explicit delayed feedback; this four-state implementation solves the linear
loop to stay stable at high cutoff without oversampling. RES 127 maps to a
feedback of 4.5 (`LADDER_K127`), above the ideal ladder's 4: the fixed-point
losses at a low cutoff eat the loop gain, and 4.04 oscillated only above 130 Hz.
The tanh bounds the oscillation at about −19 dBFS from 49 Hz up. Every product
is rounded to nearest, half away from zero (a loop of floors leans one way:
the states rested a few Q23 units below zero and the output never returned to
0), and when there is no input and all four stages are below 16 LSB they walk
to exactly 0. A retrigger or a stolen voice keeps the four stages, as the SVF's
two (zeroing them thumped on fast mono lines). The ladder returns Q23;
`ladder_q15` rounds it for a voice, `ladder_bus` scales a track bus by 32 in
and 8 out, so the track path keeps its resolution, and holds it within
±600,000: the track LEVEL that follows multiplies in 32 bits and overflows
at LEVEL 127 from 884,376 up (-884,373 down) (19/10: it was ±2^20, which a bus
at full scale reached at RES 0; no engine's bus gets there).

LP/BP/HP keep their original arithmetic. LP24 is appended as ANALOG TYPE 3;
track TYPE 1 selects the ladder only on the low-pass side. A TYPE switched
under a note starts each type where the other's output was: the ladder's four
stages at the SVF's low-pass state, the SVF at rest on the ladder's last stage
(19/10: the SVF came back on its states of before the ladder and the ladder
started at rest on the input, a step as large as the signal at a closed
cutoff). The track resonance is an offset over the original 40, so zero
retains the original filter.

Host acceptance (`tests/synth_dsp_test.c`, also run with UBSan):

- Impulse response at cutoff index 70: **−24.06 dB/octave** from 3,820 to 7,640 Hz.
- Ten seconds at RES 127, cutoff 80, after one impulse: bounded and still
  oscillating (peak 5,258, final-second RMS 3,716 Q15 units).
- Self-oscillation at RES 127 stays between −30 and −12 dBFS at every cutoff
  from index 10 (49 Hz) to 127 (it was dead below index 30 at 4.04).
- After a full-scale bipolar burst the output and all four stages are exactly
  0 within a second, at cutoff 0, 40, 80 and 127, with RES 0 and 100.
- All 128 cutoff positions and the resonance endpoints tolerate full-scale
  alternating input without overflow; the Terrain reference check is also
  relative (error 26 dB under the signal).

## Terrain

`eng_terrain.c` adapts Emilie Gillet's five analytic
[Plaits Wave Terrain surfaces](https://github.com/pichenettes/eurorack/blob/master/plaits/dsp/engine2/wave_terrain_engine.cc)
to Q15. The MIT notice is in `LICENSES/plaits-wave-terrain.txt`. This is the
analytic portion of Plaits, not its wavetable/user-terrain modes. Two phase
accumulators trace the X/Y orbit; the path is sampled twice per output sample,
as in Plaits. Adjacent surfaces crossfade. High notes reduce the orbit radius;
a DC blocker with error feedback and an output SVF keep offset trajectories
usable. All render state belongs to the voice. No arena or shared PRNG is used.

The two EDIT pages are TERRAIN (TERR, RAD, X, Y) and ORBIT (RATIO, ROT, DRV, CUT).
The eight factory presets cover keys, bass, bells, plucks, lead, pad and drone.
Engine numbers 0–11 are unchanged. Optional SLICE remains 12 when enabled;
TERRAIN is appended at 12 without SLICE, 13 with it. The preset trim table
uses `ENGI_TERRAIN` so the optional engine cannot select another bank's gains.

The independent float reference in `tests/synth_dsp_test.c` compares all five
surface equations at 85,050 coordinates: RMS error **0.000377**, maximum
**0.001932** (normalized full scale). Complete one-second renders of the eight
preset trajectories, including drive, DC removal and filter, have RMS error
**0.000291–0.002335**, maximum **0.006672**. Acceptance limits are 0.005 RMS and
0.015 peak. All 256 min/max combinations of the eight parameters, at low/high
notes and extreme modulation, pass UBSan. This reference lives only in the
host test; the firmware contains no float calculation for the engine.

## Presets and measurement

ANALOG's original indices 0–21 are preserved. Indices 22–29 append LADDER ACID,
WHEEL LEAD, AFTER BRASS, SLEW PAD, EXPR KEYS, RAND PLUCK, DUAL ORBIT and HALL SWELL.
They demonstrate velocity, modulation wheel, aftertouch, expression, note
randomness, slew and LFO 2. No preset changes the global reverb type (an earlier
draft had SLEW PAD, HALL SWELL and ISLAND DRONE switch the bus to HALL: a part's
sound must not change what the other parts and the drums hear); try them with
GLO > REVERB > TYPE HALL.

`tools/level_synth_presets.py` uses the existing `level_presets.py` BS.1770
measurement and its phrase roles/target, but updates only the sixteen appended
sounds. It preserves every upstream trim and the optional SLICE table. The
existing −40..23 half-dB trim limits still apply; heavily filtered/plucked
presets may reach the upper limit before reaching the target loudness.

```sh
cc -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/preset_level.c -lm -pthread -o build/host/synth_preset_level
build/host/synth_preset_level build/host/synth-presets.wav build/host/synth-presets.txt build/host/synth-presets.raw
python3 tools/level_synth_presets.py build/host/synth-presets.raw build/host/synth-presets.txt
# Rebuild and re-render until changes=0; do not reuse audio after changing trims.
cc -O2 -Ibuild/gen -Ifirmware/src tests/synth_dsp_test.c -lm -o build/host/synth_dsp_test
build/host/synth_dsp_test
cc -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined -Ibuild/gen -Ifirmware/src tests/synth_dsp_test.c -lm -o build/host/synth_dsp_ubsan
build/host/synth_dsp_ubsan
```

Golden hashes are added only after leveling. Existing hashes must never be
rewritten to accommodate these features. FM-1 maximum interrupt time, audible
aliasing at extreme terrain ratios, ladder sound and the eight-voice CPU cost
still require the device acceptance procedure in `SYNTH.md`.
