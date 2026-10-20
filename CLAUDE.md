# sloop-lab: project memory

## Base: SLOOP 2.7.1 (merged 17/10/2026)

`synth` sits on **SLOOP 2.7.1** (`4ec1ac8`, 16/10/2026) since one merge commit of 17/10/2026. Upstream took
the fork's `dualcore` and `ram` series whole into 2.7 (pull request #111: `hal/fm1_cpu1.{h,S}`, `cpu1.{c,h}`,
`.lowram`, the stack painting, `st_xip`, the scratch union, the nibbles, DRAWBAR in the arena, the four cpu1
tests, DUALCORE.md and RAM.md are byte-identical to the fork's `for-upstream`), so those branches are **done**:
frozen at the tags `dualcore-2.6`, `ram-2.6`, `for-upstream-2.6` (and `synth-2.6`, the branch before the merge);
`main` tracks upstream. Upstream's own edits to that code: `cpu1.c` (`cpu1_off` read before boot: 2ND CORE OFF
never starts core 1, ON takes effect at the next power-on; `cpu1_supplies()` for ABOUT), `main.c` (`UI_SMALL`,
`fm1_supplies_stock()` SYSVDD 1.38 V / VDC14 1.60 V at `fm1_cstart`, after X0X 1.0.9), `scratch.h` (no OTA
member: `ota.c` keeps its own `ota_dec`), and 2.7.1's `fm1_sys.h fm1_cpu1_halt()` (after Felucca 1.6.1): the
hold of core 1 with the bus-invalid traps off (DBG_MSG bits 4, 5, 16..21). The old plain hold, under
`FM1_GUARD_BUS`, made a late core 1 (8 ms, `fm1_cpu1_wait`) end in a FELUCCA CRASH and a reboot instead of a
glitch: X0X 1.0.3 has the same two lines; the fork carried them. Every build of the fork before this merge has
that bug.

**How the merge was made** (do it this way again for 2.8 if upstream has meanwhile taken more of the fork):
`git merge-tree --write-tree --merge-base=ram synth v2.7.1` (the merge base forced to `ram`, so the files
upstream copied verbatim do not conflict add/add), the tree checked out with `git read-tree --reset -u`, the 23
conflicting files resolved by hand (`tools/build.py`, `web/editor.html`, `web/test_web.mjs`, 4 docs, and
`core.h drums.c editor.c gfx.c icons.c main.c ota.c project.c seq.c splash.c ui.c ui_draw.c ui_input.c
ui_layers.c ui_song.c upreset.c`), then `git commit-tree` with both parents (v2.7.1 and the old `synth`), so the
19 synth commits stay in the history and the push was a fast-forward. From now on the merge base with upstream is
v2.7.1 and a plain `git merge origin/main` is right again. Decisions taken in the resolution:

- `UI_SMALL` (2.7, `__attribute__((minsize))`) and the fork's `FELUCCA_COLD_FN` (`cold.h`) are the same
  attribute: where both sides annotated a function, upstream's `UI_SMALL` was kept; `cold.h` stays for the
  fork's own files.
- `G_RTYPE` (the reverb TYPE) comes **after** 2.7's `G_FXCH G_DRMAP G_RMCH G_CLKOUT`, so 2.7's ids stay 2.7's:
  `G_RTYPE` = 49, `G_COUNT` = 50 (`web/editor.html`: `pr.g[49]`, `["rtype",49]`, `EXPECT.gcount` has 50; the
  mock pushes TYPE last; `test_web.mjs` expects gcount 50).
- Projects: still FUN7 (synth) on top of 2.7's FUN6 tricks, which FUN7 keeps room for: `rsv[0]` bit 0x40
  (saved by 2.7+: KBD HOLD's default), the drum TUNE / DECAY in the drum track's `e[1..7]` with a check in
  `g[G_SAVE]` (`drum_tw_put/get`); `proj_from_v6` copies `g`, `rsv` and `e`, so a 2.7 project keeps them.
  The settings word: 2.7 took bits 26 (2ND CORE OFF), 27 (KNOB ACCEL OFF), 28-29 (USB RATE), 30 (theme),
  31 (REC QUANT OFF); the side record (theme + favourites) rides on `st_side`; `midi_set` has 10 entries.
- Favourites (2.7, `ui.c fav*`) store BANK indices; `fav_list` maps them to the offered list (`bank_vis`, the
  lite build hides the empty sets' presets) and skips a hidden one; `base_kind` reads `bank_vis` too.
- `seq.c arp_tick`: 2.7's `arp_end / arp_step` structure with the matrix's `mod_time` on `arp_off`.
- `drums.c`: 2.7's glide fields plus the fork's `#if !FELUCCA_BOOT_BSS` initializer; `gfx.c cv_text`: 2.7's
  `rgb565_over` ramp plus `font_unpack()`; `icons.c`: the LZ4 block plus the new ramp; `ui_song.c`: "sel:FX
  rec:store  save:chain" (fixed 19/10: 29 characters of FONT_S from x 4 end at 236, inside the 240-pixel canvas; the
  merge's "sel: FX  rec: store  save: chain", 32, was cut to "save: ch"), and the second line "oct-:loop/song
  oct+ x2:load" (28, as on the upstream branch fix-misc, 55389f0).
- `tools/build.py`: 2.7's retry with `FELUCCA_LOGO_TEXT=1` when the XIP overflows, then the fork's
  "rebuild with FELUCCA_SAMPLES=lite" message; `symbol_report` kept.
- The serial console is **out of the default build** since 2.7 (`felucca.c FELUCCA_CDC 0`, 6-8 KB):
  `FELUCCA_CDC=1 FELUCCA_SAMPLES=lite sh build.sh` is the device-measurement build.

**Flash after the merge:** lite 530,180 B (51,384 free), RAM `.data+.bss` 65,548 B of 98,304, `.lowram` 23,280
of 29,488, pool 333,636 of 344,064. The full sample set does not link on 2.7.1: 3,000 B over with the boot logo,
**568 B over with `FELUCCA_LOGO_TEXT=1`** (upstream 2.7.1 alone: 577,920 B, 3,644 free). `lite` is the build.

## Branch `synth` (13/10/2026)

`synth` started at `ram` (`9843161`, SLOOP 2.6) and is on SLOOP 2.7.1 since 17/10/2026 (above). [SYNTH.md](SYNTH.md) describes
the implemented synth series, formats, build profiles and acceptance status.
The historical **Only the second core** restriction below applies to `dualcore`,
not `synth`. The synth plan explicitly permits appended golden entries: retain
all 182 original entries unchanged and append only the sixteen new presets.
Do not blanket-refresh hashes or CPU baselines. Preserve neutral-default audio
under every dual-core mode, and keep per-part rendering state independent.

Build this feature set with `FELUCCA_SAMPLES=lite sh build.sh` (530,180 B, 51,384
free on 2.7.1). The full sample library linked on 2.6 with 232 bytes to spare (the
screen font is an LZ4 block unpacked into RAM, `gfx.c font_unpack`); on 2.7.1 it is
568 bytes over even with the boot logo as text, as predicted. `tests/run_tests.sh` renders the
goldens for both profiles and compares the two boot-initialization hashes. Keep
separately generated sample headers for each profile so a target build does not
change a running host test. Lite keeps every factory preset in the tables and
hides those of its empty sets (`engines.c preset_hidden`): preset numbers in
projects mean the same in both builds.
`sh tests/run_tests.sh` must pass, including the target budgets. The formerly
stale IRQ and missing FM6 target budgets below were measured against `ram` and
corrected; they are no longer accepted failures. The current synth build has
not yet been validated on hardware. Source fingerprints, image sizes and
checksums live in `docs/synth-build-manifest.json`.

During audio rendering `render_p(t)` reads a per-part shadow or the original
parameter array. UI code must read `t->p` directly, never an active render view.
Project writes use FUN7; user-preset version 2 uses a 256-byte logical record
packed to 160 bytes on flash. Keep all legacy readers and the existing A/B
sector map. Attribution and notices travel with each port.

## Historical dualcore / ram notes (both series are in SLOOP 2.7; kept for the record)

This checkout is a **thin fork** of [isod89/sloop-fm1](https://github.com/isod89/sloop-fm1) (SLOOP, a
groovebox firmware for the M-VAVE FM-1). Branch `dualcore` adds the FM-1's second processor core to
SLOOP (2.6 since the merge of 11/10/2026; 2.5 before) as a short series of patches; `main` is upstream, untouched. Public fork:
https://github.com/andrerezemini/sloop-lab. The owner's planning documents (Portuguese) live in a private
repository, not here. Status and the patch table: [DUALCORE.md](DUALCORE.md).

## Rules of this fork

- **Only the second core.** Anything else SLOOP might want goes upstream as an issue or into another
  branch. Patches stay small (at most six files each) and additive, so they can be carried onto each
  SLOOP release: upstream publishes one squashed commit per release and merges no pull requests, so a
  new release means merging upstream's `main` into `dualcore` (done for 2.6 on 11/10/2026: one merge commit,
  four files in conflict, `fx.c`, `panel.c`, `ui_menu.c`, `tests/ui_pages_test.c`), then re-auditing the
  render for state shared across parts (below) and running `tests/cpu1_modes.sh`.
- **Bit-identical audio.** The render split only sums integers in private buffers; the sums commute. Every
  change must keep all 182 hashes of `tests/golden.txt` unchanged, under every mode of the host double
  (below). No `GOLDEN_UPDATE=1` on this branch.
- **Credits travel with the code.** `hal/fm1_cpu1.{h,S}` are Charles Vestal's (X0X, GPL-3.0-only), after
  Melodee's findings (Kerem Kilic); SLOOP is isod89's; Felucca is Leo Kuroshita's. Keep the notices.
- **Upstream strategy.** The HAL part is offered to Felucca (the root of every FM-1 firmware; its
  Discussion 141 asks for CPU1 DSP); the SLOOP-specific split is offered to SLOOP as an issue with
  measurements and a link here. Neither is a precondition for this branch.

## What the branch changed (as of 11/10/2026)

| Patch | Files | What |
|---|---|---|
| 1 hal | `firmware/hal/fm1_cpu1.h`, `fm1_cpu1.S` (new), `fm1_guard.h` (`fm1_guard_pc_open`, `fm1_guard_top_locked`), `fm1_sys.h` (core 1 held before a reboot and before the loader's core reset) | the second core's start, hold, one-job hand-off and wait, from X0X |
| 2 build and boot | `firmware/app.ld` (`.c1_text` in RAMTEXT, `_rt_end = _c1_end`), `tools/build.py` (assembles `fm1_cpu1.S`, `c1text.bin`, flag `FELUCCA_CPU1`), `firmware/src/main.c` (copies `.c1_text`, `cpu1_boot()` before `audio_init()`), `firmware/src/cpu1.c` (new), `ui_menu.c` (ABOUT: "2 CORES" / "1 CORE"), `console.c` (`cpu1`, `cpu1_trace`, `cpu1_timeouts`), `felucca.c` (includes) | core 1 started at power-on, self-tested with a checksum job, reported; fallback to one core |
| 2b host double | `firmware/src/cpu1.h` (new: `cpu1_run` / `cpu1_wait` / `cpu1_on`), `tests/cpu1_host.c` (new), `tests/cpu1_test.c` (new), `tests/hostsim.c` (includes the double), `tests/run_tests.sh` (five runs of `cpu1_test`) | the render's job API, with a host implementation for the tests |
| 3 render split | `firmware/src/fx.c` (`mixacc_t acc[2]`, `part_buf[2]`, `drum_buf`, `mix_part(t, n, mx, b)`, `mix_drums`, `mix_units`, `cpu1_units_job`, `units_split`, `mix_block`), `fm6_core.c` + `eng_fm6.c` + `engines.c` (FM6's operator buses per part: `fm6_part_t` in the engine arena; `phys_render_draws` in `engines.c`), `cpu1.h` (`cpu1_units`), `cpu1.c` + `console.c` (`cpu1_units`, `cpu1_stack_free`); tests: `cpu1_host.c` (`CPU1_UNITS`), `mix_split_test.c` (new), `cpu1_modes.sh` (new), `run_tests.sh` | the parts and the drums as units with private accumulator sets; core 1 renders the last part and the drums, with the timeout fallback; bit-exact in every host mode; **runs on the FM-1** (first boot 10/10/2026: ABOUT says 2 CORES; the measurements are still to come) |
| 4 switch and figures | `ui_menu.c` (`MI_CPU1` "2ND CORE" in SYSTEM, `mi_cpu1_have`), `panel.c` (`cpu1_off`, bit 26 of the settings word (bit 21 until the 2.6 merge: 2.6 took 21..25 for MIDI IN = CH n), applied to `cpu1_units` in `lights_from_word`), `fx.c` (`cpu1_job_ticks`, `cpu1_wait_ticks`, `cpu1_busy_q8`, `cpu1_wait_q8`, `cpu1_load_half`), `audio.c` (calls `cpu1_load_half` after each half), `cpu1.h` + `cpu1.c` + `cpu1_host.c` (`cpu1_ticks`: TIMER4 on the device, 0 on the host), `console.c` (`cpu1_busy_pct`, `cpu1_wait_pct`), `cpu1.c` (`cpu1_label`: "C1 OFF"); tests: `ui_pages_test.c` (the SYSTEM section has four rows since 2.6: 2ND CORE, HARDWARE CALIBRATION, MIDI LEARN CLEAR, ABOUT, KNOB 1..4; the switch and its settings bit checked) | the menu switch, kept in the settings; the load figures on the console; **runs on the FM-1** (first boot 10/10/2026: ABOUT says 2 CORES; the measurements are still to come) |
| 2.6 merge (11/10/2026) | `fx.c` (`mix_part` is 2.6's preset-fade wrapper over `mix_part_run(t, n, mx, b)`, its sound set aside per core, `pnew[2]`; `drum_insert` inside `mix_drums`, with or without the FILTER; the THROW glide before the hand-off; `texture_mix` and `punch_freeze_feed` after `acc[0] += acc[1]`; `units_split` predicts the LFO wrap with 2.6's increment (`lfo_sync_inc`) and reads the values the block renders with, `p_old` / `pe_old`, and skips parts not in the mask), `voice.c` (`esync_k[NTRK][3]`: ENV SYNC's steps per track; `lfo_sync_inc(const int16_t *p)`), `engines.c` (`phys_render_draws(t, model)`), `panel.c` (bit 26), `ui_menu.c` (four rows in SYSTEM; the help text only on a section of three), `tests/mix_split_test.c` (scene `fx26`: INSERT on every unit, LFO 2 SYNC S&H, ENV SYNC, two parts' presets fading at once, TEXTURE, THROWs, FREEZE, drum mutes, a bend), `tests/ui_pages_test.c`, `tests/font_test.c` (2.6's new test includes `gfx.c` without hostsim's `#define __attribute__(x)`: clang on macOS refuses the `.pool` section; one line added, worth an upstream issue) | the series on SLOOP 2.6: builds; the 184 golden renders and `mix_split_test` give the same hashes in every mode, no race under TSan; **not yet run on the FM-1 as a 2.6 build** |
| 5 ram (11/10/2026) | `firmware/app.ld` (RAMTEXT to 0x01C07F00, `.lowram` output section taking `*(.bss.c1stack)` and `*(.lowram*)`, `_lr_start` / `_lr_end`), `main.c` (`lowram_test`: every word written and read back, then zeroed, before `.data` is copied; `lowram_state` for the console), `fx.c` (`section(".lowram")` on `rev_line`, `acc`, `part_buf`, `drum_buf`, `mix_part`'s `pnew`), `console.c` (`lowram`), `tools/build.py` (the `lowram` figure) | the SRAM below the RAM region (29,488 B that Felucca's map left for RAM-run debug) holds the reverb lines, core 1's stacks and the split's scratch: RAM region 96,524 -> **73,756 B**; builds with and without `FELUCCA_CPU1`; placement only, so the goldens cannot change; **not yet run on the FM-1** (the console's `lowram = 1` is the proof that this SRAM behaves) |
| 5b stacks (11/10/2026) | `main.c` (`stack_paint` below whichever of `sp` / `ssp` / `usp` sits in each region, `stack_free`), `console.c` (`stack_free`, `sstack_free`, `sp`) | core 0's main (24 KB) and supervisor (8 KB) stacks measured for the first time; the device's figures decide whether `_guard0` can move up and the pool grow |
| 6 (next) | this file, `DUALCORE.md` | the 2.6 build on the device (ABOUT: 2 CORES, `lowram = 1`) and its measurements; then perf patterns |

Memory of the current build against 2.6 (upstream 2.6 built here: RAM 92,696 B, image 573,052 B): RAM
`.data+.bss` **73,756 B** of 98,304 (24,548 B spare since patch 5; before it 96,524 B, 1,780 spare), `.lowram`
22,772 B of 29,488 (`rev_line` 17,368, core 1's stacks 3,072, `acc` 1,280, `drum_buf` 512, `pnew` 284, `part_buf`
256), pool 333,900 of 344,064 (unchanged: the FM6 buses fit in the arena), image 576,564 B, `.c1_text` 60 B. (On 2.5:
RAM 88,472 B, image 567,296 B.) The rule from patch 5 on: **anything big this branch adds to RAM goes to `.lowram`**
(a section mark, as `.pool`), so a SLOOP release never competes with the split for the RAM region; an upstream object
may be moved there too when a merge overflows the region (the reverb lines were the first). The region ends at
0x01C07F00: `crt0.S` writes the SPL's registers at 0x01C07F20. The host tests see no attribute (`hostsim.c` defines
`__attribute__(x)` away). First run on an FM-1 on 10/10/2026 (patches 1-4 on 2.5): core 1 starts, passes its self-test,
the split render plays; first figure: CPU 31 % with 2ND CORE OFF, 23 % ON on the owner's pattern. **The 2.6-based
build ran on the FM-1 on 11/10/2026 (13:30, the `ram` branch's build): `lowram 1`, 2 CORES, `cpu1_timeouts 0`, at
idle `stack_free 22956`, `sstack_free 7080`, `sp 0x01C79E38` (the main loop on the main stack, the interrupts on
the supervisor stack).** The stack figures after heavy use, the console load figures and a worst-case pattern are
still to come from the device.

## Branch `ram` (11/10/2026, on top of `dualcore`)

Four commits, each a RAM reduction with its measured delta, the goldens unchanged ([RAM.md](RAM.md) has the table):
`midi_sel_on` in nibbles (seq.c); `firmware/src/scratch.h`, one union for `st_buf`, `vis_l` / `vis_r`, the HOME
scope's snapshot and `ota_dec` (main loop only, filled and consumed inside one call: the rules are in the header),
with the OTA staging over `proj_tmp` (`felucca.c OTA_STAGE_RAM`; the host tests keep `ota.c`'s statics); the user
preset banks read in place (`storage.c st_xip`, firmware only through `ST_XIP_PTR`; `upreset.c up_bank_xip[]`,
`up_rec_none`, `up_put` staging in `proj_tmp` through `project.c up_stage`; `tests/upreset_test.c` keeps the RAM
banks under `UP_HOST`; without a flash the message is NO FLASH); the DRAWBAR part state in the engine arena
(`drw_part_t` in `engines.c`'s union; the wheels' phases restart after GRAIN / FM6 / PHYS took the part). RAM region
58,220 B. Not done: `st_buf`'s removal (1.8 KB after the union), `usr_zone` / `dsu` in place (the ISR reads them
while the editor rewrites). Merging a SLOOP release here: merge upstream into `dualcore` first, then `dualcore`
into `ram`; the conflicts will be in `upreset.c`, `storage.c`, `ota.c`, `editor.c` (the bank paths).

## The render split: what may and may not be shared

A unit (a synth part, or the drums with the metronome) may only touch its own state while another unit renders
on the other core. The audit of SLOOP 2.5's render (10/10/2026) found four things shared across parts, handled
in `fx.c` (the comment above `mix_drums` is the list): `part_buf` (now one per core); FM6's `fm6_bus` scratch
(now per part, in the engine arena); `formant_nz`, one xorshift for every FORMANT voice, read and written by
`formant_render` (two FORMANT parts stay together on core 0); and libc's `rng()`, drawn by `track_lfo_tick` when
a part's LFO phase wraps and by `phys_render` when a slot's model is not the part's (such a part stays on core 0
this block, so the sequence of draws is the one of a single core). Everything else is per track, per part
(`eng_arena`, `drw_t`, `fm6_eff`, `sl[]`, `tflt[]`) or per voice, or only written by `events_block` on core 0
before the hand-off (`vage`, `voice_kills`, `strum_block`, `click_on`, `drum_on`, note-ons). A new engine or
effect that keeps global mutable state touched during the render must be added to `units_split` or made per
part, or `tests/cpu1_modes.sh` will show it (a different hash with `CORES=2`, or a ThreadSanitizer report).

The audit of SLOOP 2.6 (11/10/2026) found two more, both fixed in the merge: `esync_k` (voice.c), ENV SYNC's three
envelope steps, written by `esync_coef` at the top of every part's `track_render` and read by every voice's
`env_tick` (upstream's comment says "the parts render one at a time"): now `esync_k[NTRK][3]`; and `mix_part`'s
`pnew`, where 2.6 sets a part's new sound aside while the old one fades out (`sound_change`): now one per core,
as `part_buf`. Per part already: the INSERT state `ins[]` (the drum track's four channels its own), `bend_s`,
`ins_res` / `ins_xt`. On core 0 outside the units: TEXTURE (`tx`), the THROW glides (`thr_rev` / `thr_dly`, written
at the top of `mix_block` before the hand-off and only read by the units), FREEZE (`rev_frz`, `rev_l`, `rev_r`),
the look-ahead limiter (`lim_dac`, `lim_usb`). `rng()`'s call sites are the same as in 2.5. Two things `units_split`
must now take from the values the block renders with, not `t->p`: the LFO increment (LFO 2 SYNC: `lfo_sync_inc`,
and the part's `p_old` while a preset fades) and the PHYS model (`pe_old[0]` while an engine fades).

## Building

```sh
export JIELI_TOOLCHAIN=$HOME/.jieli/toolchain      # tools/get_toolchain.sh installs it (Linux x86-64)
export AC79_SDK=$HOME/fw-AC79_AIoT_SDK              # only cpu/wl82/tools/{uboot.boot,cfg_tool.bin,cfg/eq_cfg_hw.bin}
sh build.sh                                         # build/felucca.fwsc; ~8 s; on macOS Docker must be running
FELUCCA_CPU1=0 sh build.sh                          # one core, as SLOOP 2.5
sh build.sh --release 2.5                           # identity FM-1_925 (upstream publishes the dev identity FM-1_900)
```

`sh build.sh`, not `./build.sh`: the files carry no execute bit. On macOS every toolchain call runs in a
`linux/amd64` Debian container; on Linux x86-64 it runs natively. The JieLi toolchain does ship newlib 2.2.0
(`pi32v2/lib/r3/libm.a` is FPU-built; `firmware/hal/fm1_fpu_probe.c` links it), but the firmware links none of
it and brings its own `libc.c`. SLOOP's generators run on Apple's Python; X0X's need Pillow with raqm.

## Testing

```sh
sh tests/run_tests.sh                               # ~15 min (the 10-minute soak); after a build
CORES=2 CHAOS=7 build/host/cpu1_test                # env vars as separate words (zsh does not split a quoted string)
```

- (2.6 / 2.5 note, kept for the record; the synth branch measured the budgets against `ram`, see above.) One failure was pre-existing in SLOOP 2.6 (and 2.5) and not this branch's: `target_budget`'s
  `fm1_alnk0_irq` 268 vs 174 and its three FM6 functions without a budget (the upstream budget file is stale),
  so `run_tests.sh` ends with "HOST TESTS FAILED" on a clean run. (2.5's other one, three PHASE presets over the
  host CPU instruction baseline, is gone in 2.6: the baseline now only notes presets that got faster.) Everything
  else, including the 182 golden renders, the 10-minute soak and `cpu1_modes.sh`, must pass (last full run:
  11/10/2026, on the 2.6 merge). 2.6's `tests/font_test.c` needed `#define __attribute__(x)` before its
  `gfx.c` include to compile on macOS (clang refuses the `.pool` section for a host build; hostsim.c has the
  same define): one line, carried here, worth an upstream issue.
- **17/10/2026, on 2.7.1: the whole suite passes** (198 / 195 golden renders unchanged, every mode of the second core's
  host double, the web tests; `pwa_test` skips without Playwright, `run_preview_tests` needs the WASM build). Two
  expectations were the fork's to set: `ui_pages_test` counts ten GLO pages (the synth's REVERB page) and
  `target_budget.txt` says `drums_mix 6793` (2.7's drum TUNE / DECAY fade and LEVEL / PAN glide; measured the same
  on upstream 2.7.1 alone, whose own file still says 6153, so upstream's suite stops on it, as it did on the IRQ
  budget before). `test_web.mjs`: the LIVE page's engine values start at the device's `pe0` (`LVX.e0()`).
- The second core's host double (`tests/cpu1_host.c`): `CORES=1` runs a job inline but only at
  `cpu1_wait` (the default; deterministic), `CORES=2` on a POSIX thread, `CHAOS=N` with random delays
  before and after each job, `CPU1_TIMEOUT_AT=K` loses the K-th job and holds the core (the fallback path),
  `CPU1_UNITS=mask` forces the split (0 none; bit k = part k, bit 3 = the drums; 15 everything that may go).
  `tests/cpu1_test.c` runs in all of them and under `-fsanitize=thread`. `tests/cpu1_modes.sh` (~100 s, called
  by `run_tests.sh`) runs the golden renders and `tests/mix_split_test.c` under every mode; a render change is
  done when it passes. The host double's thread is created at the first job: a test that forks must fork before
  its first render (as `regress.c` and `mix_split_test.c` do).
- The host CPU baseline: the split costs core 0 the second set's clearing and adding (`cpu/mix/idle` 365 ->
  378 instructions a sample, +3.6 %; `idle_drums` +2.5 %; the heavy mixes unchanged), inside the +25 %.
- Host tests include single firmware files (e.g. `ui_pages_test.c` includes `ui_menu.c` without `cpu1.c`):
  anything that only exists in the firmware build goes under `#if FELUCCA_CPU1`.
- `tools/build.py` refuses a literal that looks like a register address outside `hal/` (a test seed of
  `0x5100F` tripped it); keep such constants decimal.
- A menu row added to a section shifts the rows below it: `tests/ui_pages_test.c` drives the menu by knob
  number (KNOB 2 = row 2) and, on the host, HARDWARE CALIBRATION (`panel_setup`) waits for buttons forever, so
  a test that lands OCT+ on it hangs the suite at 100 % CPU (seen on 10/10/2026 when 2ND CORE became SYSTEM's
  first row). Attach `lldb -p PID --batch -o bt` to a `-O0 -g` build to see where. Since 2.6 SYSTEM has four
  rows (2.6 added MIDI LEARN CLEAR): KNOB 3 = MIDI LEARN CLEAR, KNOB 4 = ABOUT; upstream's own test turns KNOB 2
  for LEARN CLEAR and KNOB 3 for ABOUT, so every merge moves those two lines. The menu draws three rows and the
  help text in 219 canvas rows; a fourth row takes the help text's place (`draw_menu`, `MI_DY` 50).

## On the device

Back up with the web editor first. Install from a local copy of the site (Web MIDI needs localhost or https):

```sh
python3 web/make_site.py build/felucca.fwsc "2.7.1 synth b9b13df lite" ../sloop-site --note ../sloop-note.html
cd ../sloop-site && python3 -m http.server 8000      # Chrome or Edge: http://localhost:8000/webapp/installer/
# the label may be any text (the page says "SLOOP <label>"; the file name keeps [A-Za-z0-9.-]); --note FILE (sloop-lab) puts
# an HTML fragment at the top of the installer: this build's version, commit, profile, what it adds, how to go back
```

First boot checklist: ABOUT (hold HOME, SYSTEM, ABOUT) says **2 CORES** and the two supplies (2.7: "1.38V 1.60V"); the serial
console (only in a `FELUCCA_CDC=1` build since 2.7; MENU > AUDIO > USB SERIAL ON, restart; then `screen /dev/tty.usbmodem* 115200` on the Mac, type `status`; there is no `fm1t`
tool in SLOOP's tree, the console is a plain USB CDC port) prints `lowram = 1` (2 = a word of the SRAM below the RAM
region failed the boot test: do not use the build), `stack_free` / `sstack_free` (bytes of core 0's two stacks never
reached; `sp` tells which region the main loop runs on: note them, they decide the pool's growth), `cpu1 = 3` and
`cpu1_trace = 0xC1000002`. "1 CORE" with `cpu1_trace 0` means core 1 never reached
its entry; `0xC1000001` means it reached the entry but not its loop. Then play: `cpu1_timeouts` must stay 0
(one means core 1 was held and SLOOP fell back to one core), `cpu1_stack_free` well above 0 on the heaviest
patterns (core 1's stack is 2,816 B), `cpu1_busy_pct` and `cpu1_wait_pct` plausible (busy well under 100, wait
small), and `cpu_pct` lower than with MENU > SYSTEM > 2ND CORE OFF on the same pattern (the switch is live and
the sound does not change; 2.7: OFF takes effect live, ON only at the next power-on, core 1 is not started while OFF). `cpu1 units MASK` in the console changes the split finer (0 = everything on core 0,
`cpu1 units auto` = the firmware's share). Rescue: hold OCT- while switching on, then install again. The package
keeps identity FM-1_900, so the official 2.7.1 installs back over it. **On `synth`: a slot or user-preset bank saved by synth (FUN7, preset version 2) reads as
empty on 2.7.1 and older: back up with the web editor before installing synth and before going back.** A device that ran the 2.5-based build with 2ND CORE **OFF** saved that in bit 21, which 2.6
reads as GLO > MIDI 2 > IN = CH 1: set IN back to NOTES once, and 2ND CORE as wanted (now bit 26).

## What the device alone can tell

Core 1's start (registers, ROM, PC limits), real timing (cycles per sample, I-cache contention between the
cores, XIP latency, late DMA halves), USB and OTA on the hardware. The host answers everything else.
