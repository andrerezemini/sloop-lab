# Synth validation, 2026-10-13

Firmware revision: `bc9b5b01f022066cb6fbaa65d115b2dd0d858fb3`.
The complete acceptance run used test revision `f525aa6`; subsequent test and
documentation commits do not change the firmware package.
The target manifests record the full source/input fingerprint and every
artifact's SHA-256. Files copied to `build/` were checked against the normal
lite manifest; all three variant directories were checked independently.

## Automated acceptance

| Check | Result |
|---|---|
| Full host regression | 198 renders; 0 changed, missing, health, routing, CPU-budget or crash failures |
| Lite host regression | 195 renders; same zero-failure result; only FLUTE/SCRCH-dependent presets excluded |
| Existing golden baseline | All 182 original rows identical to `ram`; 16 appended |
| Existing host CPU baseline | All 158 original rows identical to `ram`; 16 appended |
| Target instruction budgets | All 23 functions pass against the final target disassembly |
| Dual-core golden modes | CORES=1/2, CHAOS=7, timeout fallback, forced split off/all: identical |
| Unit partition tests | Nine scenes, eleven masks; single/threaded/delayed/timeout modes pass |
| ThreadSanitizer | Three split masks, CORES=2 CHAOS=3: pass, no race |
| UI | New MOD/LFO2/LP24/HALL controls, ninth GLO page and 20,000-frame fuzz pass |
| Stress | 40,000 frames (10.7 simulated minutes); clean stop; ASan/UBSan pass |
| Soak | Ten simulated minutes; released voices drain in 7.11 seconds, gates stay off, voice count never increases, final tail is zero |
| Modulation | MIDI routing/reset, shadow edits/fades, dynamic enum guards, per-part RNG, voice/event destinations, ARP gate source and all LFO2 wave/sync combinations pass |
| Persistence | FUN1–FUN6 migrations, compact FUN7, section inheritance across save/transition/stop, v1/v2 preset banks and interrupted writes pass |
| DSP | Ladder slope/self-oscillation/extremes and independent Terrain float reference pass |
| Reverb | All four models at size/damping extremes, tails, stereo, model changes and FREEZE pass |
| Assets and boot | 87 lossless asset blocks, rejected truncation, stable full/lite sample IDs and identical BSS/non-BSS boot render pass |
| Complete `tests/run_tests.sh` | Exit 0: ALL HOST TESTS PASSED; editor, backup, migration, installer and recovery scenarios included |

The full host suite explicitly generates `build/host-full-gen`, independent of
the target's sample profile. Its generated sample header was verified identical
to the previously validated full header. Lite uses `build/host-lite-gen`.
Native MOD, LP24 and HALL screen captures were visually inspected at 240×240.
The complete run and lite regression logs are retained locally in
`build/synth-validation/acceptance.log` and `build/synth-validation/lite.log`.

The target budget corrections are explicit: five new functions; previously
missing FM6 entries measured against `ram`; and the stale IRQ entry corrected
to the same 268 measured on both branches. No other old budget was increased.
PHASE's unused second-oscillator setup was removed, preserving audio and
bringing RESO PLUCK comfortably below its original host limit.

The old soak test assumed every release ended within five seconds. The new
engine/preset count changes the seeded random selection, exposing the existing
FM6 GLASS BELL's longer operator-envelope release. Instrumentation confirmed
gates were already off and the final voice ended after 7.11 seconds. Soak now
uses the existing stress test's bounded 30-second release deadline, additionally
asserting gates remain off and active voice count never increases. The original
all-voices-free, stopped transport, bounded output and silence checks remain.

## Target packages

| Variant | App bytes | App headroom |
|---|---:|---:|
| Normal lite | 528,376 | 53,188 |
| Lite FPU diagnostic | 533,032 | 48,532 |
| Lite single core | 526,480 | 55,084 |

Normal lite RAM is 59,324 bytes, lowram 23,280, pool 333,636. Full samples exceed
XIP by 1,368 bytes; the linker refuses the image and removes stale output.
The installable normal `.fwsc` SHA-256 is
`84f262293287b9904820ffc446478b800bfb2fd4c5f93102f00eb29434a6e229`.

These are compiler, host and artifact-integrity results. No FM-1 was attached
for this acceptance run. Hardware audio timing, shedding, stack minima, FPU
timings/preservation, MIDI keyboard behavior and listening remain pending as
listed in [SYNTH.md](../SYNTH.md). Public upstream submissions/release are also
pending; [local drafts](synth-upstream.md) are prepared.

## Re-validation after review, 2026-10-13 (Claude)

Four independent code reviews of `ram..synth` (matrix and storage; ladder and
Terrain; reverb, section FX and S0; a cross-cutting sweep) found no memory-safety
or crash-class defect and confirmed the design rules (zero = today, formats by
appending, per-part render shadow, per-track random sources, Q15 only). What
they found, and what changed in the commits that follow `66d24cc`:

| Finding | Fix |
|---|---|
| Loading SLEW PAD, HALL SWELL or ISLAND DRONE switched the global reverb to HALL for every part and the drums (`params.c preset_extras`, the web editor too) | the preset field and the write are gone; the docs say to pick HALL under GLO |
| TOOLS > NEW kept the previous song's section FX flags | `project_new` zeroes `section_fx_mask` (now in `core.h`) |
| Lite shifted the factory preset numbers that projects store (SAMPLE from 5, GRAIN from 2): wrong names and BANK highlight across profiles, and `preset_trim` translated indices | lite keeps every preset in the tables, `engines.c preset_hidden` hides those of empty sets from BANK and the regression; `gen_samples.py` emits `SMP_SET_EMPTY`, empty sets use zone 0; the index translations are gone |
| At boot every FUN6 slot was rewritten as FUN7, so a device that only played could not go back to 2.6 | `persist_boot` leaves a flash object alone when its imported content equals the RAM slot; rewritten at the next SAVE (the four slots; not the working project's autosave, see SYNTH.md) |
| LP24: the four stages were zeroed on every retrigger / steal (a thump on fast mono lines) | `voice.c` keeps `s[3..6]` for ANALOG as it keeps the SVF's |
| LP24: products floored, the output rested at −1 for ever; self-oscillation dead below 130 Hz | symmetric rounding, a deadband that walks all four stages to exactly 0, feedback 4.5 at RES 127 (`LADDER_K127`); the DSP test sweeps self-oscillation (−19 dBFS from 49 Hz) and asserts exact silence after a burst |
| LP24 on a track: 4 LSB of resolution lost and clipping at ±2^19 | `ladder_bus` scales by 32 in / 8 out of the Q23 core: ±2^20 headroom, full resolution |
| `ladder_seed` was the DC solution only at k = 0 | seeded at in / (1 + k) |
| `terrain_surface` (two calls a sub-sample, 172 instructions) was outside the target budget | its own line in `tests/target_budget.txt`; `ladder_run` re-measured (41) |
| ROOM, SPRING and HALL under FREEZE: input kept flowing into lines at gain 1 (clipping), SPRING kept damping | the three models add no input while held, SPRING's loop low-pass is bypassed |
| `up_put` refused a record with a value outside a byte, so one such record made a whole bank unsaveable | `up_encode` clamps; the legacy map-by-count cases (np 53, 58, 61, 71) are back in `upreset_test.c` |
| Firmware and web editor disagreed on what a sound load keeps (`P_TRES`, `P_TFTYPE`) | `ui.c param_kept` keeps them, as it keeps `P_TFLT` |
| `tests/run_tests.sh` ran the two boot-initialization paths without comparing them; the lite image was never regression-rendered | the hashes are compared; a lite regression (`build/host/regress_lite`) runs in the suite |
| `usb_poll` (2 kHz TIMER5 interrupt) was in the cold `minsize` set | removed from it; seven main-loop / boot entry points added instead |
| The full sample profile overflowed the slot by 1,368 bytes | the screen font is an LZ4 block unpacked into RAM at boot (3,228 bytes of flash for 5,376 of RAM): full links at 581,332 bytes (232 free), lite at 526,964 (54,600 free); RAM .data+.bss 64,908 |
| A Terrain float-reference check was absolute only | also relative (error 26 dB under the signal) |
| `mod_begin` would alias the drum track onto part 0's shadow | guarded |

Goldens: the 182 original rows are byte-identical to `ram`; of the 16 appended
rows the 7 LP24 ANALOG presets and ISLAND DRONE were re-rendered (the ladder
and the reverb-type change), the other 8 are unchanged. Both profiles render
`0 changed, 0 gone`; no CPU entry over budget; all 24 target budgets pass.

Still pending: everything the FM-1 alone can tell (audio timing with HALL and
eight LP24 voices, shedding, stack minima, the FPU probe, MIDI controllers,
listening), and the S9 submissions.
