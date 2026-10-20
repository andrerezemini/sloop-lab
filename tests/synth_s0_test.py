#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build profiles retain sample IDs; compressed UI assets reproduce every byte."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def generate(tool, dest, **flags):
    subprocess.run(["python3", str(ROOT / "tools" / tool), str(dest)],
                   env={**os.environ, **flags}, check=True, capture_output=True)
    return dest.read_text()

def array(text, name):
    body = re.search(r"\b" + name + r"\[[^;=]*\]\s*=\s*\{(.*?)\};", text, re.S)[1]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return bytes(int(v, 0) for v in re.findall(r"0x[\da-fA-F]+|\d+", body))

with tempfile.TemporaryDirectory(prefix="sloop-s0-") as td:
    tmp = Path(td)
    full = generate("gen_samples.py", tmp / "full.h", FELUCCA_SAMPLES="full")
    lite = generate("gen_samples.py", tmp / "lite.h", FELUCCA_SAMPLES="lite")
    def sets(text):
        table = re.search(r"SMP_SETS\[\].*?=\s*\{(.*?)\};", text, re.S)[1]
        return [(n, int(z), int(c)) for n, z, c in re.findall(r'\{"([^"]+)", (\d+), (\d+)\}', table)]
    fs, ls = sets(full), sets(lite)
    assert [s[0] for s in fs] == [s[0] for s in ls]
    assert len(array(full, "SMP_DATA")) - len(array(lite, "SMP_DATA")) == 54284
    for (name, _, count), (_, _, reduced) in zip(fs, ls):
        assert reduced == (0 if name in ("FLUTE", "SCRCH") else count)
    for name in ("LOFI FLUTE", "SCRATCH"):      # the presets stay in both tables: the stores' numbers are stable
        assert f'"{name}"' in full and f'"{name}"' in lite
    assert "#define SMP_SET_EMPTY 96u" in lite and "#define SMP_SET_EMPTY 0u" in full   # FLUTE 5, SCRCH 6 empty
    assert '"GM KIT"' in lite
    invalid = subprocess.run(["python3", str(ROOT / "tools/gen_samples.py"), str(tmp / "bad.h")],
                            env={**os.environ, "FELUCCA_SAMPLES": "invalid"}, capture_output=True)
    assert invalid.returncode != 0
    cases = []
    for tool, name in (("gen_logo.py", "SLOOP_SPLASH_RLE"), ("gen_icons.py", "ICON_DATA"), ("gen_font.py", "FONT_S_DATA")):
        raw = generate(tool, tmp / "raw.h", FELUCCA_ASSET_LZ4="0")
        packed = generate(tool, tmp / "packed.h", FELUCCA_ASSET_LZ4="1")
        if tool == "gen_logo.py":
            cases.append((array(raw, name), array(packed, name)))
        elif tool == "gen_font.py":
            cases.append((array(raw, name), array(packed, "FONT_S_PACKED")))
        else:
            blob = array(packed, "ICON_PACKED")
            offsets = [int(x) for x in re.search(r"ICON_OFF\[\] = \{(.*?)\}", packed)[1].split(",")]
            source = array(raw, name)
            for i, (a, b) in enumerate(zip(offsets, offsets[1:])):
                cases.append((source[i * 36:(i + 1) * 36], blob[a:b]))
    lines = ['#include <stdint.h>', '#include <string.h>', '#include <assert.h>',
             f'#include "{ROOT}/firmware/src/asset_lz4.h"', 'int main(void) {']
    for i, (raw, packed) in enumerate(cases):
        lines += [f'{{ uint8_t dst[{len(raw)}];',
                  'const uint8_t raw[] = {' + ','.join(map(str, raw)) + '};',
                  'const uint8_t packed[] = {' + ','.join(map(str, packed)) + '};',
                  'assert(asset_lz4(packed, sizeof packed, dst, sizeof dst));',
                  'assert(!memcmp(raw, dst, sizeof raw));',
                  'for (uint32_t n = 0; n < sizeof packed; n++) assert(!asset_lz4(packed, n, dst, sizeof dst));',
                  'assert(!asset_lz4(packed, sizeof packed, dst, sizeof dst - 1)); }']
    lines += ['return 0; }']
    (tmp / 'test.c').write_text('\n'.join(lines))
    subprocess.run(['cc', '-std=c11', '-fsanitize=address,undefined', str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
    print(f'S0: full/lite stable IDs, 54284 B saved, {len(cases)} asset blocks exact, truncated blocks rejected (ASan/UBSan)')

# the cold entry points (cold.h FELUCCA_COLD_FN): a file that uses the macro includes cold.h itself, above its first
# use, and does not lean on another file of the unity build (felucca.c) or of a host test having included it first:
# project.c used it on proj_apply 70 lines before its own include, which only arranger builds reached, and compiled
# only because ui_input.c (firmware) and usb.c (tests/project_test.c) include cold.h without using it
users = 0
for src in sorted((ROOT / "firmware").rglob("*.[ch]")):
    if src.name == "cold.h":
        continue
    code = re.sub(r"/\*.*?\*/", lambda m: "\n" * m[0].count("\n"), src.read_text(errors="replace"), flags=re.S)
    lines = [l.split("//")[0] for l in code.splitlines()]          # (comments out, line numbers kept)
    use = next((i for i, l in enumerate(lines) if "FELUCCA_COLD_FN" in l and not l.lstrip().startswith("#")), None)
    if use is None:
        continue
    users += 1
    inc = next((i for i, l in enumerate(lines) if re.match(r'\s*#\s*include\s+"cold\.h"', l)), None)
    assert inc is not None and inc < use, \
        f"{src.relative_to(ROOT)}:{use + 1}: FELUCCA_COLD_FN before #include \"cold.h\" (line {inc + 1 if inc is not None else 'none'})"
print(f"S0: the cold entry points: {users} files use FELUCCA_COLD_FN, each includes cold.h above its first use")
