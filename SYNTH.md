# SLOOP synth branch

`synth` started at `ram` commit `9843161` (SLOOP 2.6 plus the dual-core and RAM
work) and sits on **SLOOP 2.7.1** since the merge of 17/10/2026 (SLOOP 2.7 took the
dual-core and RAM work itself; CLAUDE.md has the merge's decisions). S0–S8 of the October 12 synth plan are implemented. The new features are
opt-in: old sounds retain FDN, SVF, a disabled matrix and a disabled second LFO.
The 182 existing golden renders remain unchanged; sixteen new renders cover
the appended presets. Hardware acceptance and public upstream submissions are
still pending. No synth image has been flashed as part of this implementation.

## Features and controls

- **MIDI expression:** modulation wheel (CC1), channel aftertouch (0xD0),
  expression (CC11), and reset controllers (CC121) follow the existing MIDI
  routing. Wheel and aftertouch start at zero; expression starts at full scale.
  The serial `midi` command reports all three values per synth part.
- **Four modulation slots per part:** SELECT reaches MOD 1 through MOD 4;
  choose SRC, DST and AMT. Amounts can be parameter-locked. Sources include the
  existing LFO, envelope, velocity, key, per-note random, MIDI controllers,
  sample-and-hold, slew and LFO 2. Destinations include pitch, cutoff, shape,
  amplitude, pan, distortion, sends, LFO rate/depth, vibrato, continuous engine
  parameters, velocity and gate time. Enumerated engine choices are excluded.
  TIME changes note length, not the transport grid.
- **LFO 2:** independent RATE, WAVE, SYNC and PHS controls, selected as a matrix
  source. RATE 0 disables it. The former sync page is named LFO SYNC.
- **LP24:** ANALOG TYPE adds a four-pole resonant ladder that self-oscillates
  at RES 127 from 49 Hz up, returns to exact silence and keeps its stages on a
  retrigger. The track FILTER has CUTOFF, RES and TYPE; LP24 operates on its
  low-pass side. RES is an offset over the old value 40; zero with SVF preserves
  the original result. A sound load keeps the track's RES and TYPE, as it keeps
  its CUTOFF (the web editor agrees).
- **Reverb:** GLO > REVERB > TYPE selects FDN, ROOM, SPRNG or stereo HALL.
  Model changes fade and clear shared state. The original FDN is still type 0.
- **Sixteen sounds:** eight additional ANALOG presets and eight TERRAIN presets
  appear once each in BANK. Wheel, aftertouch, expression, both LFOs, the ladder
  and HALL are demonstrated. No preset changes the global reverb type: select
  HALL under GLO > REVERB to hear SLEW PAD, HALL SWELL and ISLAND DRONE as meant.
- **TERRAIN:** a Q15 adaptation of Plaits' five analytic surfaces. EDIT exposes
  TERR/RAD/X/Y and RATIO/ROT/DRV/CUT. The original twelve engine indices stay
  stable; TERRAIN is appended (after optional SLICE when that engine is enabled).
- **Section effects:** SONG's SELECT pages expose section REVERB and DELAY.
  Each group independently inherits the current globals or uses the section's
  values. SAVE persists settings. Stopping song playback restores the working
  project's globals. The web editor exposes all new controls and section FX.

Every audio block reads a per-part parameter view. Active modulation and preset
fades use a shadow in `.lowram`; rendering never restores a temporary value over
an edit to `track_t.p`. Random modulation and LFO 2 state belong to each part;
voice random/velocity values belong to the note. Dual-core split tests exercise
active matrix slots, HALL, eight ladder voices and TERRAIN under forced splits.

## Build and local packages

```sh
FELUCCA_SAMPLES=lite sh build.sh
FELUCCA_SAMPLES=lite FELUCCA_FPU_PROBE=1 sh build.sh
FELUCCA_SAMPLES=lite FELUCCA_CPU1=0 sh build.sh
```

The sample profile remains `full` by default in `build.py`, but on SLOOP 2.7.1
the full profile no longer links: 3,000 bytes over the app slot with the boot
logo, 568 bytes over with `FELUCCA_LOGO_TEXT=1` (build.py retries that by itself);
on 2.6 it linked with 232 bytes free. `lite` is the build to carry. Lite keeps all eight set numbers
but leaves FLUTE and SCRCH empty; their three factory presets (LOFI FLUTE,
SCRATCH, FLUTE DUST) stay in the engines' tables, so every preset number means
the same sound in both builds (projects and the web editor store numbers), and
are hidden from BANK (`engines.c preset_hidden`). Older projects using those
sets load with silent sample voices; all six retained sets are byte-identical.
Lite does not take a hidden sound as a favourite (*FAV: NOT IN THIS BUILD*).
No user-sample sectors are repurposed.

Validated local packages are in `build/synth-lite/`, `build/synth-lite-fpu/` and
`build/synth-lite-singlecore/`; each directory includes a manifest, `.fwsc`,
`.bin`, ELF, disassembly and flash reports. `build/felucca.fwsc` is the normal
lite package. The tracked [build manifest](docs/synth-build-manifest.json)
records sizes, options, source fingerprint and SHA-256 values. These are local
development packages with the existing FM-1_900 identity, not public releases.

| Measurement | Result / source |
|---|---|
| Base `ram` image | 576,944 bytes |
| S0 cold code optimization | 2,096 bytes saved in isolation |
| S0 boot initialization | 3,008 bytes saved in isolation |
| S0 lossless UI asset compression | 2,032 bytes saved in isolation |
| Combined S0 optimizations | 6,992 bytes saved against S0 control |
| Lite sample data reduction | 54,284 bytes; 54,528 including presets/metadata |
| Screen font as an LZ4 block (13/10) | 3,228 bytes saved, 5,376 of RAM used |
| Normal lite image / free app space | 530,180 / 51,384 bytes on SLOOP 2.7.1 (17/10); 526,964 / 54,600 on 2.6 (13/10) |
| Full sample image / free app space | does not link on 2.7.1: 568 bytes over with the text logo (17/10); 581,332 / 232 on 2.6 (13/10) |
| RAM / lowram / pool | 65,548 / 23,280 / 333,636 bytes on 2.7.1 (64,908 / 23,280 / 333,636 on 2.6) |
| Original render hashes | 182 preserved; 16 appended (198 full / 195 lite) |
| Ladder slope | −24.06 dB/octave, host impulse measurement |
| Terrain equation reference | RMS 0.000377; max 0.001932, normalized full scale |
| FM-1 audio timing, load shedding, FPU timing/preservation | Pending hardware |

Detailed measurements and attribution are in [SYNTH_S0.md](SYNTH_S0.md),
[SYNTH_DSP.md](SYNTH_DSP.md) and [SYNTH_FX.md](SYNTH_FX.md).

## Persistence and differences from the plan

Upstream already used FUN6, so projects now write **FUN7**, not the anticipated
version 4. All six earlier formats are decoded with their original layouts;
engine locks move from the old parameter boundary 63 to 81. New parameters
default to neutral values. FUN7 is 3,684 bytes within the existing 3,840-byte
flash object. It stores 81 common parameters as signed bytes and eight engine
parameters as int16 values. Independent track filter TYPE makes P_COUNT 89.

The logical user-preset record is 256 bytes with version 2 and room for 104
parameters. Sixteen literal 256-byte records plus the bank header would require
4,104 bytes and exceed the flash object's capacity. Version 2 therefore packs
records to **160 bytes on flash**, including all 104 signed parameters, notes,
flags and reserved bytes. A bank uses 2,568 bytes. Legacy 192-byte version-1
records are decoded on read and converted atomically on the next save. The
existing A/B sector map is preserved; the proposed extra sector is already a
user-sample sector. Web backups preserve either bank representation.

The ladder uses four trapezoidal stages with a solved feedback loop and Q23
internal states, rather than copying Surge's five-state delayed-feedback form.
Audio and coefficients stay Q15, no float enters the render, and host tests
verify slope, bounded self-oscillation and extreme input stability. Wave Terrain
implements the five analytic surfaces; user/wavetable terrain, Swarm, String
Machine, automation lanes and production float DSP remain outside this series.

Projects and presets saved by synth require this branch to retain new data.
The web editor rejects a synth backup on firmware that only understands FUN6.
**Back up the device with the web editor before installing synth and before
returning to the official firmware:** the first SAVE of a slot or a user-preset
bank under synth writes FUN7 / preset version 2, which SLOOP 2.6 reads as empty
(the bytes stay in flash, a second save there overwrites the A/B twin). At boot
synth leaves a FUN6 slot in flash as it is when its RAM copy has the same
content (13/10: the earlier build rewrote every slot as FUN7 at the first idle
moment), so a device that only played, never saved, still goes back to 2.6 with
its projects.

## Verification

The [validation record](docs/synth-validation.md) ties the measured results to
the packaged firmware revision.

Run `sh tests/run_tests.sh` after a successful target build. Since 13/10 it also
renders the lite profile's goldens (the same file, less the hidden presets) and
compares the two boot-initialization hashes. It includes storage
and interrupted-write tests, FUN1–FUN6 migration, raw preset bank migration,
MIDI routing, matrix shadows and dynamic enum guards, all LFO 2 wave/sync
combinations, ladder and Terrain reference tests, reverb extremes and switches,
section effects, native UI fuzz, web editor/backup/installer tests, golden and
CPU regression, soak, every dual-core mode and ThreadSanitizer. The S0 tests
also compare every decompressed asset and both boot-initialization paths.

The full-sample host suite uses full generated sample headers even though the
target package is lite; both scripts generate `build/host-full-gen` explicitly.
Lite regression uses separately generated headers and excludes only the three
intentionally omitted presets:

```sh
mkdir -p build/host-lite-gen
FELUCCA_SAMPLES=lite python3 tools/gen_samples.py build/host-lite-gen/felucca_samples.h
cc -O2 -w -Ibuild/host-lite-gen -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/regress.c -lm -pthread -o build/host/regress_lite
build/host/regress_lite
```

Existing CPU budgets and audio hashes must not be silently refreshed. New presets/functions get
explicit baselines; the stale IRQ and missing FM6 target budgets were measured
against `ram` before correction.

Hardware acceptance still requires the FM-1: boot/self-test and stack minima;
20 minutes of play with no timeout, overrun or shedding; a worst-case
PHYS SYMP/FM6/GRAIN pattern with twelve active slots and HALL, CPU1 on/off;
eight ANALOG LP24 voices; each reverb's `audio_max_us`; MIDI aftertouch from a
keyboard; section transitions and listening checks. The optional `fpu` build
must record both cores' multiplication/division/sqrt/sine timings and the real
interrupt-preservation result. No unavailable device measurement is inferred
from a host test.

## Upstream handoff

The implementation, local firmware packages and [submission drafts](docs/synth-upstream.md)
are ready for review. S9's GitHub issues, pull requests, Felucca discussion,
public release and community listing have not been sent or published. Hardware
results should accompany those submissions. The patch history keeps attribution
with the code and separates budget work, persistence, DSP, web and integration.
