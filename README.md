# DKC3Recomp

A native recompilation of *Donkey Kong Country 3: Dixie Kong's Double
Trouble!* (SNES, USA, En/Fr) built the way DKC2Recomp was built: the
game's code is statically recompiled to C by [snesrecomp](snesrecomp/README.md)
from a bank configuration derived from a public disassembly, the shared
snesrecomp runtime executes anything the analysis cannot prove through its
65816 interpreter, and project-owned hosts present the game natively on
macOS and Windows.

You must supply your own DKC3 ROM. The supported image is the headerless
North American (En,Fr) release, 4 MiB, SHA-256
`2277a2d8dddb01fe5cb0ae9a0fa225d42b3a11adccaeafa18e3c339b3794a32b`. No ROM
data, generated code, or extracted assets are stored in this repository.

## Status

Bring-up. See [docs/BRINGUP.md](docs/BRINGUP.md) for the dated record of
what runs and what has been verified, and
[docs/WIDESCREEN_GUIDE.md](docs/WIDESCREEN_GUIDE.md) for how the widened
presentation works and how its defects are diagnosed and fixed. Levels present
wide at 16:10, 16:9, and the selectable 21:9 ultrawide mode with DKC2Recomp's
terrain reconstruction driven by DKC3's own level map, verified on Lakeside
Limbo.
The 21:9 option uses the nearest symmetric width that preserves the shared
runtime's safe sprite coordinate range: 446x224, or approximately 20.91:9.
Screens the map does not cover center the native frame between black margins.
The placed-object spatial scan and final activation checks, the sprite renderer
culls, and the static banana arcs are widened to the presented view by
`scripts/apply_dkc3_widescreen_overrides.py`, verified on Lakeside Limbo. The
scan includes adjacent 256-pixel cells so 21:9 objects, including objects in
quick saves made by older builds, do not wait for a cell boundary to activate.
Murky Mill's HDMA-windowed BG3 light cones are evaluated across the physical
widescreen span instead of being clipped and repeated at the native edges,
verified at the reported quick-save state in both 16:10 and 16:9.
Floodlit Fish's underwater BG3 color-math composition now receives its
subscreen tint across transparent side-margin pixels without clipping the
reconstructed BG1 terrain at the native edges, verified at the reported
quick-save state in 16:10, 16:9, and 21:9 with an unchanged 4:3 frame.

A second layer that streams a strip of the level map, Riverside Race's
reflection under the water line, is served in the margins from the
terrain store at the row offset its rows prove every frame, while its
static underwater backdrop wraps as a plane judged by row-level write
tracking; and the first visible row of the margins now decodes like the
rest instead of being blanked on tile boundaries. See
[docs/BRINGUP.md](docs/BRINGUP.md) for the evidence.

## Native macOS release

The v0.0.5 release includes the new Windows x64 build and the unchanged
v0.0.4 Mac archive below. The Mac source, menus, and display-link pacing are
retained; the Mac binary has not been rebuilt for this Windows-focused release.

Download `DKC3Recomp-v0.0.4-macOS-arm64.zip` from
[Releases](../../releases), extract it, and open `DKC3Recomp.app`. Select your
own legally obtained North American (En,Fr) ROM in the launcher; the ROM stays
at its original path and is never copied into the application bundle.

The published v0.0.4 bundle includes the Floodlit Fish tint fix, the
river's second-layer reflection and backdrop in the margins, and the fix
for the strip that flashed at the top of the margins; v0.0.3 carried 21:9
and the adjacent-cell placement activation fix.

This release is for Apple silicon running macOS 26 or newer. The app is
ad-hoc signed rather than notarized, so if Gatekeeper blocks the first launch,
Control-click the app in Finder, choose **Open**, and confirm once. The project
is still in bring-up: the tested boot, launcher, save-state, and Lakeside Limbo
paths work, but full-game compatibility is not yet claimed.

## Building on Windows

The Windows x64 SDL build has passed all 23 project tests, a 4,801-frame
headless run, and a packaged launcher/visible-game smoke check. Full-game
completion and hardware/controller coverage are not claimed.

Requirements: Visual Studio 2022 with Desktop development with C++, CMake,
and Python 3. Initialize the Git submodules before building. In PowerShell:

```powershell
python scripts/generate_snesrecomp.py --analysis-backend python --rom 'C:\private\dkc3.sfc'
cmake -S . -B build-windows -G 'Visual Studio 17 2022' -A x64 `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DSNESRECOMP_SDL_BACKEND=SDL2 `
  -DDKC3_ROM='C:\private\dkc3.sfc'
cmake --build build-windows --config Release --parallel 4
ctest --test-dir build-windows -C Release --output-on-failure
```

Run `build-windows/Release/DKC3RecompSDL.exe`. This is the shared SDL2/OpenGL
host with 4:3, 16:10, 16:9, and 21:9 presentation, reconstruction upscaling,
overlay, and audio rate control. Keep `SDL2.dll` and the `assets` directory
beside the executable when moving it. Select your own ROM in the launcher.
The separate `DKC3Recomp.exe` in the build directory is the legacy Win32 host;
portable packages use the SDL executable renamed to `DKC3Recomp.exe`.

The game window now has native **Game** and **View** dropdown menus matching
the Mac's game commands: Pause / Settings, Quick Save, Quick Load, fullscreen,
nearest/bilinear scaling, and all four aspect ratios. Windows also exposes
reconstruction scaling, all five dither/edge-reconstruction levels, level-edge
policies, and screen models in View submenus. Selections apply live and are
remembered beside the executable. **Game > Pause / Settings > Settings** opens
the same detailed sliders and dropdowns as the Mac overlay, including
reconstruction strength, softness, shading, audio, and volume; Controls and
Assist Tools are adjacent tabs. These menus appear after launching the game,
not on the pre-boot ROM picker. Ordinary Windows minimize/close commands replace
the macOS-specific Hide and application-management commands.
The Windows title bar, menu bar, and nested dropdowns use a dark theme.
The product title is simply `DKC3Recomp`, without a pre-release label.

CMake fetches the pinned SDL 2.30.9 source when no SDL2 package is installed.
The Python analysis backend avoids a Rust toolchain requirement. If `cmake`
is not on PATH, use its full path from Visual Studio's bundled CMake tools.
Private Windows smoke tests cover all four aspect modes with reconstruction,
overlay, rewind, fast-forward, and quick-state save/load. ROM-free tests remain
available with `-DDKC3_BUILD_SNESRECOMP=OFF`.

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
runner, bundles SDL2, embeds the project icon, and ad-hoc signs the app. Open
the app and select the ROM in its launcher.

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
