# DKC3Recomp

A native recompilation of *Donkey Kong Country 3: Dixie Kong's Double
Trouble!* (SNES, USA, En/Fr) built the way DKC2Recomp was built: the
game's code is statically recompiled to C by [snesrecomp](snesrecomp/README.md)
from a bank configuration derived from a public disassembly, the shared
snesrecomp runtime executes anything the analysis cannot prove through its
65816 interpreter, and project-owned hosts present the game natively on
macOS (and, unverified here, Windows).

You must supply your own DKC3 ROM. The supported image is the headerless
North American (En,Fr) release, 4 MiB, SHA-256
`2277a2d8dddb01fe5cb0ae9a0fa225d42b3a11adccaeafa18e3c339b3794a32b`. No ROM
data, generated code, or extracted assets are stored in this repository.

## Status

Bring-up. See [docs/BRINGUP.md](docs/BRINGUP.md) for the dated record of
what runs and what has been verified. The presentation is the native
256x224 frame; the wider aspects center that frame between black margins.
Widescreen terrain reconstruction of the kind DKC2Recomp has is later
work with its own evidence gates.

## Building on macOS

Requirements: CMake, Ninja, SDL2 (`brew install cmake ninja sdl2`), Python 3,
and Rust's `cargo` for the native analyzer (Python falls back when it is
absent).

```bash
git clone --recurse-submodules https://github.com/elliotttate/DKC3Recomp.git
cd DKC3Recomp
python3 scripts/generate_snesrecomp.py --rom /private/path/dkc3.sfc
./build_macos.sh /private/path/dkc3.sfc
```

`generate_snesrecomp.py` verifies the ROM's hash, refreshes `recomp/funcs.h`,
and emits the private recompiled units under `generated/` (ignored by Git).
`build_macos.sh` builds `build/macos/DKC3Recomp.app` and the headless
runner, bundles SDL2, and ad-hoc signs the app. Open the app and select the
ROM in its launcher.

## Headless validation

```bash
build-headless/dkc3_snesrecomp_headless /private/path/dkc3.sfc 600
```

runs the game for 600 frames with no window and prints frame, WRAM, VRAM,
CGRAM, and OAM hashes with video and audio activity counts. The switches
in `runner/headless_main.c` write frames as PPM (`DKC3_FRAME_PPM`,
`DKC3_FRAME_PPM_PREFIX` with `START/END/STEP`), raw audio
(`DKC3_AUDIO_PCM`), and memory dumps, restore an SRAM image or a quick
save (`DKC3_SRAM_INPUT`, `DKC3_SAVESTATE_INPUT`), and replay scripted
input (`SNESRECOMP_INPUT_PLAY`).

## Regenerating the bank configuration

The cfg files under `recomp/` were derived from the
[H4v0c21 DKC3 disassembly](https://github.com/H4v0c21/DKC3-Disassembly)
by `tools/ingest_dkc3_disasm.py`, which needs a checkout of that project
with its assembled `dkc3.sym` beside the bank sources:

```bash
python3 tools/ingest_dkc3_disasm.py --disasm /path/to/DKC3-Disassembly --output recomp
```

The output holds only names, addresses, bounded ranges, data regions, and
finite dispatch contracts. The disassembly itself is GPL-3 and is not
redistributed here; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Tests

```bash
cmake -S . -B build-headless -G Ninja -DDKC3_ROM=/private/path/dkc3.sfc
cmake --build build-headless
ctest --test-dir build-headless --output-on-failure
```

The unit tests cover the host modules carried over from DKC2Recomp, the
ingester, and the pacing-log tool. With `DKC3_ROM` set, the suite also
boots the game headlessly and, on macOS, runs the app hidden.

## Lineage

The host code, build scripts, and working rules come from
[DKC2Recomp](https://github.com/elliotttate/DKC2Recomp); the game adapter
and the ingester are new. Widescreen, save tools, and the diagnostics that
DKC2Recomp accumulated are not carried over until DKC3 has its own
evidence for them.
