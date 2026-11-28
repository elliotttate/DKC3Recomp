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
The Northern Kremisphere overworld now fills the wide view from its fully
resident 512-pixel map. The view stays locked to the camera, and a margin
past the map's east or west end is black; the east end was measured, the
west end is assumed to be the map's first column. Verified at one overworld quick
save in 16:10, 16:9, and 21:9 with the native 256 columns and guest memory
unchanged. The overworld's own sprite window extends only about 45 pixels
past each native edge, so a sprite can vanish in the outermost pixels of a
16:9 margin and well inside a 21:9 margin. Once the hovercraft opens
the north, the game streams map rows as the camera climbs; the margins now
stay filled through that (verified headless at a hovercraft save, and by
the user in the desktop app). The headless replay of that save also shows
garbled tiles in the streamed rows at native 4:3, with or without the
widening; it was not seen in the desktop app and is not investigated.
Other overworld screens are
unverified and keep black margins unless they pass the same checks.
Short rooms such as the bonus games no longer slide the view after the
player stops: the inward shift at each wall shrinks to fit the room and
changes at most one pixel per eight of camera travel, or is zero when the
room is narrower than the wide view, so the HUD stays put and black shows
only beyond a wall. Verified headless in the first level's two bonus rooms
only; the bonus rooms of later levels and the live app are unverified.
The presented shift still eases one pixel a frame, but it may also move as
far as the camera moved that frame when doing so cancels the camera's
motion (ported from DKC2Recomp), so a pinned wall holds the view still
instead of letting it run past the wall and drift back. Unit-tested only;
no live scene has been replayed with it.
Walls now follow the game's own camera regions. A room the camera cannot
leave sideways no longer widens into other rooms' map columns, which showed
as a mismatched, cut-off strip at one edge. The elevator shaft in the
reported save is exactly one screen wide, so it rides between black margins,
and the right margin opens once the camera reaches the room at the top. A
region's linked neighbours extend its walls, so a run of linked rooms is
one span, up to a missing link or a neighbour flagged `$20`, which never
joins.
Verified headless at that save in 16:10, 16:9, and 21:9.
The wall rule that continues a wall into the void beside it now stays out of
any column the camera's region lets the native window reach, where the level
map is what the console draws. In a waterfall level it had filled the open
gap between two tree trunks with rock and grass, which popped into the right
margin whenever the camera stood on an 8-pixel boundary. Verified headless at
the reported save in 16:9 and 21:9.
The placed-object spatial scan and final activation checks, the sprite renderer
culls, and the static banana/star grids are widened to the presented view by
`scripts/apply_dkc3_widescreen_overrides.py`. Where the aspect permits, the
placement scan and sprite culls also extend past the presented edges by an
off-screen spawn/cull lead (48 pixels per side by default, configurable via
`DKC3_SPAWN_LEAD`, clamped to 0–64; sprite-cull lead is further clamped by
aspect to preserve nine-bit OAM X) so sprites activate and queue tile DMA
before entering the frame. This widening is verified on Lakeside Limbo and
in-game in 16:9 on Doorstop Dash Bonus Level 1 ("Collect the Stars!") on
Windows; the `DKC3_CULL_WIDEN=0` native-parity capture, 16:10/21:9 traversal,
other bonus stages, and macOS rebuild remain unverified (see
[docs/BRINGUP.md](docs/BRINGUP.md)). The
scan includes adjacent 256-pixel cells so 21:9 objects, including objects in
quick saves made by older builds, do not wait for a cell boundary to activate.
Murky Mill's HDMA-windowed BG3 light cones are evaluated across the physical
widescreen span instead of being clipped and repeated at the native edges,
verified at the reported quick-save state in both 16:10 and 16:9.
Ripsaw Rage's tree-trunk BG3 masks now cover a trunk inside a wide margin:
the cartridge clamps each mask to the 256-pixel screen, so the forest showed
through the trunk's hollow beside the native edge. The host records the
unclamped edges (headless-verified in 16:9 and 21:9 against the native view;
a paused save shows the fix from the first unpaused frame).
Floodlit Fish's underwater BG3 color-math composition now receives its
subscreen tint across transparent side-margin pixels without clipping the
reconstructed BG1 terrain at the native edges, verified at the reported
quick-save state in 16:10, 16:9, and 21:9 with an unchanged 4:3 frame.
KAOS's body layer, enabled partway down the frame by HDMA, now uses its
complete object tilemap in the wide margins. This fixes the right-edge body
cutoff and the repeated fragment at the left edge in the reported boss save.
The streamed waterfall layer now decodes its authored columns into the wide
margins, checked against the native tilemap every frame. Waterfalls keep their
world positions after scrolling instead of disappearing at one edge and
repeating at the other, verified at the reported save in all wide aspects.
Bleak's snowball arena now preserves Mode 2's correct background/sprite order,
so the snowman appears in front of the distant snowbank and behind foreground
cover. Its bounded background maps fill 16:10, 16:9, and 21:9 using their
hardware wrap, with the native center and gameplay unchanged by widening.
The level `$23` KAOS arena now fills 16:10, 16:9, and 21:9 from its static
512-pixel room plane, stopping at the plane's ends instead of wrapping. KAOS's
body continues into a margin through the cartridge's own body window and is
never duplicated. The K. Roolenstein phase that follows, where the room moves
to BG1, fills the wide view the same way; its floor electricity and haze
(BG2/BG3) are drawn into the margins from their own map, clipped by the
cartridge's room-anchored windows. The native center, 4:3 output, and
gameplay are unchanged.
Pothole Panic's cave now fills the wide view from its authored level map.
Its shape-1 layout uses 32 metatile rows per column, twice the height of
the previously supported horizontal layout.
When the widened view keeps more enemies alive than the SNES's eight sprite
palettes can hold (Squeals on Wheels' Sneek wheels with four Buzzes), the
game's allocator falls back to palette 0 and the sprite turns fire-coloured.
Wide aspects now draw that sprite with its own palette from a host-side
table, leaving guest memory unchanged; the 4:3 path returns early and is
not expected to change, but a byte-identical 4:3 comparison has not been
measured. Quick saves that already captured the fallback are recovered
in the first frames after the load: a sprite whose type's init scripts
all request one palette gets that palette (a bear coin is silver again,
not orange), and otherwise the game's last-overflow word is used when the
fallback sprite can be identified. This was checked on two release saves
only, and the interpreter path
through the allocator does not record overflows. A save made in a wide
aspect still shows the fallback colours if loaded at 4:3 until the sprite
despawns.

A second layer that streams a strip of the level map, Riverside Race's
reflection under the water line, is served in the margins from the
terrain store at the row offset its rows prove every frame, while its
static underwater backdrop wraps as a plane judged by row-level write
tracking; and the first visible row of the margins now decodes like the
rest instead of being blanked on tile boundaries. See
[docs/BRINGUP.md](docs/BRINGUP.md) for the evidence.

In builds from this source (Windows verified; the published v0.0.4 Mac
archive predates it), the game's opening Rare logo, bouncing/developer credit
intro, and copyright screen can be skipped directly to the Title Screen with
the **Start** button on cold boot (`DKC3_SKIP_INTRO_START=0` restores the
unskippable original first-boot behavior).

In builds from this source, the desktop and SDL apps remember, per save
file, which levels have had all four K-O-N-G letters collected, and show
**KONG** beside that level's name on the world map. A world's name on the
Northern Kremisphere shows it too once all five of its levels are done.
The cartridge never saves KONG letters, so this lives in its own file,
`saves/kong_letters.dat`; `saves/save.srm` is only read and stays exactly
what the game writes, so it still works in other emulators. Copying or
erasing a file on the file-select screen copies or clears its letters;
nothing else clears them, so files played only through quick saves keep
theirs. The exception is a `kong_letters.dat` that fails its checks at
launch (wrong magic, version, size, or checksum): it is treated as empty,
which drops every file's letters.
**Settings > Gameplay > KONG letter badges on the map** (on by default,
remembered in `launcher.cfg`) shows or hides the badges at once; the letters
are recorded either way. `DKC3_KONG_BADGES=0` or `=1` sets the same option at
launch. See [docs/BRINGUP.md](docs/BRINGUP.md) for what was verified.

Under **Assist Tools / Cheats**, an optional **Spawn animal buddy crates from
pause menu** toggle (off by default, remembered as `PauseCrateSpawn` in
`launcher.cfg` and gated behind **Enable Assist Tools / Cheats**) lets the
active player press **Left** or **Right** while paused in a level to select any
of the five animal buddy crates (**Ellie**, **Enguarde**, **Squitter**,
**Squawks**, **Parry** — shown at the bottom middle of the screen) and press
**B** to spawn it at the Kong's position (`DKC3_PAUSE_CRATE_SPAWN=1` enables it
in headless runs). While enabled, pressing **X** also dismounts Squawks
(matching Enguarde's dismount behavior), and animal buddy sprites that
overflowed to palette 0 are automatically repaired when a hardware palette slot
is available or via the widescreen extended OBJ palette table.

Also under **Assist Tools / Cheats**, **Show hidden objects** (off by default,
remembered as `ShowHiddenObjects` in `launcher.cfg` and gated behind **Enable
Assist Tools / Cheats**) draws sprites the game keeps invisible, such as hidden
bear coins. Their behavior is unchanged; only the renderer's skip is bypassed
(`DKC3_SHOW_HIDDEN_OBJECTS=1` enables it in headless runs). It applies only
during level gameplay and only to sprites that hold tiles or a palette, so
hidden controllers such as the world map's are never drawn. K. Rool, whom the
game hides after the final fight, also stays hidden.

## Native macOS release

The v0.0.6 release fixes the Windows menu bar staying on screen in
fullscreen, restores the requested window size under the menu bar, keeps the
game DPI-aware when the launcher is skipped, and makes the Windows CMake
configure work again; it carries the unchanged v0.0.4 Mac archive below. The
Mac source, menus, and display-link pacing are retained; the Mac binary has
not been rebuilt for these Windows-focused releases.

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

The Windows x64 SDL build has passed the full project CTest suite (see
[docs/BRINGUP.md](docs/BRINGUP.md)), a 4,801-frame headless run, and a
packaged launcher/visible-game smoke check. Full-game completion and
hardware/controller coverage are not claimed.

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
Config, quick states and SRAM live beside the executable unless
`DKC3_USER_DIR` names another directory; the Windows smoke tests use their own
directories under `build-windows`, so running CTest leaves `Release/saves`
untouched.

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

Release builds enable interprocedural optimization when CMake's compiler
and linker check succeeds, allowing optimization across the generated game
code and runtime. Set `-DDKC3_ENABLE_IPO=OFF` when configuring to disable it;
unsupported toolchains retain ordinary Release optimization. The
project's runtime adaptations live as literal hunks under
`cmake/runtime-patches/`; `scripts/apply_dkc3_runtime_patches.py` applies
them to build-directory copies of the pinned snesrecomp sources at
configure time and fails closed when an anchor moves
(`-DDKC3_RUNTIME_PATCH_DIR` points a scratch build at another hunk
directory, for example with `tools/diagnostics/` added). They keep the
scalar PPU's widescreen merge and composite off per-pixel branch chains,
give the shared bus a direct path for plain cartridge ROM reads, stop the
interpreter prefetching poll bytes it never consults, let the interpreter
hand a compiled routine it reaches by jump to the compiled code whenever it
owns the return frame that routine will pop (`SNESRECOMP_LLE_JUMP_BOUNCE=0`
restores the call-only behavior), re-interpret a compiled jump-table
dispatch whose static table misses the live index instead of skipping the
handler, and compile out audio reference diagnostics that a zero-valued
trace define had left running on every sample. Tier-2 coverage journals are now opt in
(`SNESRECOMP_TIER2_CAPTURE=1`), as is the stack-balance auditor
(`SNESRECOMP_STACKBAL_AUDIT=1`). Measurements and validation limits are
recorded in [docs/BRINGUP.md](docs/BRINGUP.md).

On macOS a visible game window presents through a Metal layer driven by
`CAMetalDisplayLink` on its own thread (`runner/macos_metal_presenter.m`):
the emulation thread hands each frame to a mailbox and never enters the
window system, which removes the WindowServer round trip inside the legacy
OpenGL swap from the frame loop. The display link's callbacks also supply
the pacing ticks, so frames stay locked to the refresh as before. The
OpenGL path remains for the settings overlay, for hidden test windows and
their drawable captures, and when `DKC3_METAL_PRESENTER=0` is set.

The bank configurations carry exit-width contracts (`exit_mx_at`) and
function splits that let the recompiler compile routines whose exits it
cannot derive; they are tables in `tools/ingest_dkc3_disasm.py`
(`EXIT_MX_AT`, `FUNC_SPLITS`) with the disassembly structure that
justifies each, so a re-ingest reproduces them.

The SDL host gives the active player's game controller a short haptic pulse
when a descending jump both defeats an enemy and rebounds upward. The trigger
observes the enemy's actual defeated-sprite transition, so ordinary jumps,
damage, swimming, and enemies defeated by unrelated causes do not activate
it. The rumble request runs off the frame-critical thread. The pause menu's
Settings tab shows the detected controller and rumble capability, provides a
test pulse, enables or disables the feedback, and remembers that choice;
setting `DKC3_HAPTICS=0` when launching the app also disables it.

On macOS, optional MSU-1 replacement music can be enabled from **Music >
Choose MSU-1 Music Pack…**. Choose **PCM Folder** for an extracted pack or
**.msu1 Archive** for an archive, select it, then restart the app. The host
accepts `track-N.pcm`,
`dkc3_msu-N.pcm`, and `dkc3_msu1-N.pcm` naming, mixes the 44.1 kHz music with
the game's original sound effects, and preserves the selected pack for later
launches. **Music > Disable Replacement Music** restores the stock soundtrack
after a restart. For development runs, `DKC3_MSU1_PACK=/path/to/pack` selects
a pack, `DKC3_MSU1_DISABLE=1` suppresses a saved selection, and
`DKC3_MSU1_GAIN=0.0..4.0` adjusts replacement-music gain.
Quick loads and rewind keep the current music choice: older saves cannot
bring the original soundtrack back underneath a replacement pack. Disabling
replacement music also restores native sequencing when loading an MSU-era save.

For macOS audio isolation checks, the headless runner accepts an explicit
`DKC3_MSU1_PACK` too (it never reads the app's saved pack preference). Combine
it with `DKC3_MSU1_GAIN=0`, `DKC3_SAVESTATE_INPUT`, and `DKC3_AUDIO_PCM` to
capture only the remaining stock sound effects; omit the gain override to
capture the full replacement mix.

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
input (`SNESRECOMP_INPUT_PLAY`). `DKC3_KONG_TRACKER=<file>` turns on the
KONG letter tracker and its map badges with that sidecar file; it is off by
default, so headless hashes do not depend on it.
`DKC3_PPU_LEGACY=1` selects the independent scalar renderer for pixel-oracle
comparisons. CMake applies the checked Mode 2 priority adaptation, with the
other runtime hunks, to build copies of the pinned sources; the submodule
stays unchanged.

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
