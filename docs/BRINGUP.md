# DKC3 bring-up log

## 2026-09-03 - repository created, first generation, the game boots

Scaffolded from DKC2Recomp the way DKC1Recomp was: the generic host
modules under `runner/` (SDL2/OpenGL presenter, launcher and overlay,
input, rewind, diagnostics, the macOS menu, display-link pacing and audio
rate control) were copied with the game prefix renamed; the game adapter,
the video module, the headless main, the ingester, the build, and the
documents are new. `snesrecomp` is pinned to upstream `mstan/snesrecomp`
main (`c2f2421`) through the `elliotttate/snesrecomp` fork's
`dkc3-bringup` branch, which adds only two missing includes (`<time.h>`
in `interp816.c`, `sdd1.h` in `interp_bridge.c`) that a strict C11 build
on macOS needed. `recomp-ui` is pinned as in DKC2Recomp (`ad2f3e2`).

### Supported ROM

Headerless DKC3 USA (En,Fr), 4 MiB, HiROM with FastROM (`$31`), SRAM 2 KiB,
SHA-256 `2277a2d8dddb01fe5cb0ae9a0fa225d42b3a11adccaeafa18e3c339b3794a32b`.
Vectors from the header: reset `$00:80C4`, native NMI `$00:CA45`, native IRQ
`$00:CA6E`. The NMI saves the registers and jumps through the frame pointer
at direct-page `$4A` rather than returning, the same non-returning frame
dispatcher DKC2 uses, so the adapter runs the handler and its
continuation to the next quiescent wait exactly as the DKC1 and DKC2
adapters do.

### Bank configuration

The H4v0c21 DKC3 disassembly (`bed9689`, 2025-08-01) is the same format
family as the H4v0c21 DKC2 disassembly that fed DKC2Recomp, so
`tools/ingest_dkc3_disasm.py` reuses the DKC2 importer in the snesrecomp
submodule for entry harvesting and cfg writing, converts the disassembly's
asar no$sns symbol file (27,126 labels) to the importer's WLA form, builds
the direct-call overlay from the sources' JSR/JSL operands (1,994 targets,
3 unresolved), and adds two collectors of its own: `JMP/JSR (table,x)`
sites whose table is a symbolic `dw` list, and labelled data blocks in the
code banks bounded by the next label. The DKC2 importer's own contract
collectors are DKC2-specific (their RAM addresses) and harvest nothing
here.

Result: 4,716 bounded function entries across banks `$80`, `$AC`,
`$B2`-`$B9`, `$BB`, `$BC`, `$BE`, `$BF`; 353 indirect dispatch contracts
with 2,274 targets (two sites skipped for having no symbolic table); 540
data regions. Sites that read a pointer from RAM (`JMP ($0042)`,
`JMP ($004A)`, and the like) carry no contract and run on the interpreter.

### First generation (native analyzer)

```
analysis: 4725 roots -> 4757 exact variants, 14041 edges
analysis: 3034 AOT-eligible, 1723 LLE-only
v2_emit: 15 bank(s) emitted in 5.03s
```

3,930 translation units, 61 MB of C. The 1,723 interpreter-only variants
are the next coverage target; raising them means the same leaf work DKC1
and DKC2 needed (dispatch contracts at the RAM-pointer sites, bounds at
unproven callees).

### The game runs

A 4,801-frame headless run at native size completed with no interpreter
stop and no runtime failure: 4,258 video-active frames, 4,329 audio-active
frames, 362 state transitions, and 1.2 s of wall time per 900 frames. The
captured frames show the Rare rolling-log logo with the "Nintendo presents"
barrel organ, the Dixie Kong's Double Trouble title with Kiddy and Dixie on
the hovercraft, the Northern Kremisphere map, and the attract-mode demo in
Lakeside Limbo with both Kongs, a Kobble, bananas, a bear coin, and the
timer HUD, then the loop back to the logo. Three independent 3,000-frame
runs produced identical frame, WRAM, VRAM, CGRAM, OAM, and audio hashes.

### What is and is not verified

- Verified: headless boot, title, map, and attract demo at native size;
  determinism across runs; the unit tests for the carried-over host
  modules, the ingester, and the pacing-log tool.
- The macOS app (`build/macos/DKC3Recomp.app`, ad-hoc signed) builds from
  the same sources with the DKC2Recomp SDL host. Its four hidden smoke
  tests pass: a 180-frame run exercising the overlay, rewind, and
  fast-forward; a 90-frame 16:10 run; a 90-frame quick-save save-and-load
  run; and the 600-frame headless boot. The unit suite is 14 of 14: the
  eleven host-module tests carried over from DKC2Recomp, the launcher
  defaults, the ingester, and the pacing-log tool.
- Two snesrecomp tests are deliberately not wired in at this pin: the
  submodule's `interp816` test fails its two page-cross cycle checks
  (upstream `cbcd323` expects 6 cycles for an indexed read that crosses a
  page; the runtime counts 5), and its bridge test calls
  `tier2_capture_set_default_enabled`, which the runtime no longer
  declares. Both are upstream test-versus-runtime drift, not DKC3
  behaviour; the DKC2 fork pin's older `interp816` test passes.
- Three things a build on this machine needed that DKC2Recomp's CMake
  supplied elsewhere: the C linkage block in the DKC3 headers the C++
  overlay includes, the recomp-ui include path for the input test, and the
  input tests' fixture arguments.
- Not verified: gameplay under real input, saves, the Windows host (copied
  but not built here), and every wide aspect beyond "the native frame
  between black margins".
- No widescreen terrain reconstruction exists for DKC3 yet. DKC2Recomp's
  presentation, HDMA band classification, and level-map decoding are
  DKC2-specific and were deliberately not copied; DKC3 needs its own
  evidence before any of that is attempted.

### Launcher recognition

The first app build's launcher reported "ROM not recognized": the
carried-over launcher record still held DKC2's SHA-256 and CRC32 and its
"USA v1.0" label. The record now carries DKC3's SHA-256, CRC32
`448EEC19`, and "USA (En,Fr)"; the recomp-ui model marks a ROM recognized
when the CRC matches and the hash is in the known list, so both were
needed. The boxart card is the generated placeholder.

## 2026-09-03 - lockup after Wrinkly: a number printer that rewrites its stack

- **Symptom:** the app locked up or stopped after the first visit to
  Wrinkly's Save Cave. Its report named the stop at frame 2385 with the
  resume PC at `$80:FFC0`, the cartridge header, and its tier-2 manifest
  showed the real event earlier: a call chain through `$80:9507`, `$80:953C`,
  `$80:9F53`, `$80:A1AA`, `$80:A20A`, `$80:A2D1`, an RTS treated as a
  dispatch at `$80:A696` to `$80:A64D`, then 19,002 hits of a jump from
  `$00:0005` to `$00:0000`. Because DKC3's NMI is a non-returning frame
  dispatcher, the game kept drawing frames while its main thread spun at
  address zero, which is why it looked like a hang before the stop.
- **Cause:** `CODE_80A65F`, the routine that prints a number as digits,
  pops its own return address into `$22`, divides the value by ten in a
  loop pushing each remainder, then re-pushes the address and returns to
  `CODE_80A64A`, which pops the pushed digits into the tilemap. A
  recompiled `CODE_80A64A` and an interpreted `CODE_80A65F` disagree
  about that stack, and the caller's return address is gone. It is the
  only `LDA/PHA/RTS` re-push in the game; 119 labels are followed by a
  pull, but those are register restores and loop heads.
- **Fix:** `tools/ingest_dkc3_disasm.py` now emits `force_lle` for both
  routines with the reason beside each, so regeneration keeps them on the
  interpreter and the whole stack dance is interpreted; the surrounding
  recompiled code calls in and returns through the runtime's normal bridge.
  The generation moved by one variant (3,033 proven, 1,722 interpreted).
- **Verified:** a 6,401-frame scripted headless run with the user's SRAM
  starts a file, enters the cave, presses through the dialogue, reaches the
  save summary that prints the times and percentages, chooses from the
  save menu, returns to the map, and re-enters, with 21 interpreted calls
  into the printer and no address-zero or off-header site in the manifest.
  The earlier headless boot never showed the fault because its script
  stopped pressing at the first dialogue page. The same script against the
  pre-fix configuration, built into a scratch directory, reproduces the
  app's failure headlessly: the RTS at `$80:A696` taken as a dispatch at
  host frame 3305, a call into address zero at frame 3313, and the
  off-rails stop with the resume PC at `$80:FFC0` at frame 3439. The fix
  removes all three.
- The SRAM the app wrote holds only the game's fill pattern; no file was
  saved before the failure, and the pattern also selects French in the
  file select, which is the game's reading of that SRAM, not a bug here.


## 2026-09-03 - widescreen: DKC2Recomp's presentation on DKC3's map

The owner asked for the Lakeside Limbo spot to present wide, the way
DKC2Recomp does. DKC2Recomp's presentation modules were copied under DKC3
names (`runner/dkc3_video.c`, `dkc3_hdma.c`, and the widescreen game
adapter in `dkc3_game.c`), their DKC2-only ship-rigging and lava-geyser
paths removed, and every cartridge fact they key on replaced through
`runner/dkc3_facts.h`. The facts came from the H4v0c21 disassembly and
from live memory of the owner's Lakeside Limbo quick save:

- DKC3 keeps no DKC2-style mode word; a level is recognised by the main
  loop vector at direct-page `$4E` holding `$B3:8076` (`$80:84BA`).
- The renderer's camera copies are `$196D`/`$1973` (from `$0493`/`$0497`);
  the camera clamp is `$04BC`/`$04BE`, the running maximum of the level's
  camera regions less one screen (`$B7:D71E`). The world begins one screen
  in: a level's first map column sits at world x `$100`, as in DKC2.
- Every level map is decompressed at `$7F:0000` (the long pointer at `$8C`,
  set by `$B3:D917`); the metatile definitions follow it, at the offset in
  `$1967` (`$B3:DAF7`), 32 bytes each in DKC2's four-rows-of-four-words
  form; the terrain ring's VRAM base is the word at `$1969`
  (`$B7:B8A0`). The map shape nibble at `$0470` (`$B3:DA62`) selects the
  layout: 0 and 8 are column-major with sixteen metatile rows, DKC2's
  horizontal layout; 4, 5, 6/9, and 7 are row-major with 64-, 32-, 192-,
  and 160-byte rows, DKC2's vertical, narrow-vertical, square, and
  ship-hold strides; shape 1, column-major with thirty-two rows, has no
  decoder yet and stays black-margined.
- The word at `$04F8` that read `$7800` at the level's start is a moving
  upload pointer, not the ring base; it read `$E600` a few screens on and
  briefly stopped the prefill before `$1969` replaced it.

DKC2's map decoder was confirmed on DKC3 before any of that was wired:
sixteen-tile blocks of the live ring each matched exactly one 32-byte
definition in bank `$7F`, and the fourteen-block id pattern reproduced at
exactly one map position with a sixteen-row column-major stride, at the
level's start and again eleven screens in.

**Result.** At the owner's spot the prefill decodes 1,260 of 1,271
window cells and all 339 margin cells at 16:10 (461 of 461 at 16:9). A
3,300-frame scripted run holding right through the pier, the barrel
cannons, and the underwater stretch kept the terrain ready on 3,300 of
3,302 frames with the margins matching the decode on every sampled frame,
and the captures show the boardwalk, posts, mountains, water, and the
water's HDMA split continuing through both margins. The Mac launcher now
defaults to 16:10.

**Open.** The prefill's native-window agreement dropped to 60-90% under
motion at this point; the next entry traces that to the map base, not to
the store. Object activation at the wider view (placed objects appearing
at the native edge) is the next domain and is not done. Shape-1 levels
and every layout other than shape 0 are unverified. `DKC3_PREFILL_TRACE=1`
prints the prefill per frame and `DKC3_PREFILL_DUMP=<frame counter>`
lists a frame's disagreeing cells.

## 2026-09-03 - the margins showed the level one page early

The owner's screenshot a few seconds past the same spot showed the hut's
roof starting exactly at the left edge of the native window, sky where
the roof should have continued into the left margin, and a second hut
roof with its lattice in the right margin. The headless runner reproduced
it at the same camera (`$1B4`, frame 221 of the corpus script), and on
BG2 alone the right margin was the native content from 256 pixels to its
left (3,768 of 4,160 pixels equal) and the left margin the content from
256 pixels to its right.

The cause was in the facts, not the store. `kDkc3LevelMapBase` had been
set to `$FF00`, one screen before the map with 16-bit wrap, so that a
world x would address the map directly. But DKC2Recomp's presentation
already subtracts the one-screen origin when it turns a world tile into a
map tile (`Dkc3VideoResolveEdgeTile`, and the same `- 32` in the metatile
classifier and the stride calibrator), so the base compensated for the
origin a second time and every decoded margin cell came from the metatile
column eight before the right one. The base is now `$0000`, the map's raw
address. The decode had looked right at the level's start only because
the content one page away there (sky over the boardwalk) resembled the
truth, and the store dumps compared the store with the same wrong decode.

**Result.** At frame 221 the roof continues through the left margin and
the right margin shows the broken beam and the mountains that follow the
hut. Over the 253-frame corpus run every one of the 251 ready frames now
has all native cells matching the decode (before the fix, 1,145 of
1,260 at frame 221) and all margin cells matching. As a pixel oracle,
frame 221's right margin equals the native columns for the same world
span at frame 250 on 4,831 of 5,200 BG2 pixels, and frame 180's right
margin equals frame 221's native columns on 5,080 of 5,200; the
remainder is a one-pixel registration between the oracle's camera-based
mapping and the PPU's latched scroll phase, not a seam (the seams in the
full frame are continuous). The earlier "open" item about zero store
entries under motion was this same page error seen through the dump's
expected values, and is closed.

The "phantom roof" first suspected at the level's start was a misreading
of edge crops: the glide bias there is 18 pixels, and the store serves
the exposed ring columns correctly.

## 2026-09-03 - objects activate and draw for the presented view

At 16:9 the owner saw "a lot of things appear after they should": a
Kremling popping in well inside the native area and a banana arc appearing
only at the native edge, while 16:10 looked right. The cause is the same
as in DKC2: the cartridge activates and culls against the 256-column
screen, and at 16:10 its slack (32 pixels of activation, 48 of renderer
cull) happens to cover the 26-pixel margin, but not 16:9's 43.

DKC3 has three such systems, all now widened by
`scripts/apply_dkc3_widescreen_overrides.py` in the generated units, the
way DKC2Recomp does it, with helpers in `runner/dkc3_video.c`:

- **Placement activation.** `$BB:AAD4` keeps an object whose world X lies
  in [camera - Lx, camera - Lx + Wx) for the (Lx, Wx) row of `DATA_BBA8BE`
  its placement parameter selects (spawn rows Lx = $20, despawn rows $28;
  every row has Wx = 2 Lx + $100), and `$BB:A47A` scans the 256-pixel
  spatial-hash cells that window covers before the compare runs. Both
  read Lx at `$BB:A8BE+row` and Wx at `$BB:A8C0+row`; wrapping those two
  reads (`Dkc3VideoExpandCullLeft`, `Dkc3VideoExpandCullSpan`) widens the
  spawn and despawn windows together and keeps the cell scan consistent
  with the compare. The interpreter never runs either routine here, so
  the generated wrappers are the ones executing (`DKC3_TRACE_PC` at both
  addresses reports no hits over 3,300 frames).
- **Sprite renderer culls.** The renderer keeps a sprite whose
  camera-relative X lies in [-$30, $130) at eight generated blocks (the
  block at `$B7:8925` is inlined into eleven renderer variants, so nineteen
  copies widen), and one variant at `$B7:8DFC` uses [-$10, $110). The
  variant at `$B7:A88F` has no generated unit; `Dkc3PatchInterpreterCull`
  in `dkc3_game.c` widens its two immediates in the in-memory ROM image
  instead, checking the bytes it replaces. `$B6:F186`, the "is this sprite
  on screen" test sprite behaviors call, widens the same way.
- **Static tile-row objects.** `$B7:E4D6` builds a list at `$7E:6780` of
  the level's static tile-row objects (the banana arcs; 21 in Lakeside
  Limbo, from world X $7A0 on) and `$B7:E8E0` walks it every frame,
  stopping at the first object starting at camera + $100 and skipping one
  whose right end lies more than 15 pixels left of the screen. `$B7:E981`
  draws one object, clipping its columns at the screen's left edge (the
  $0F slack) and right edge (the $107), and takes OAM's ninth X bit from
  bit 15 of a column's screen X, which native play only needs for
  negative values. The right edges widen by `Dkc3VideoExpandCullRight`
  (extra + bias), the left slacks by `Dkc3VideoExpandCullLeft`, and
  `Dkc3VideoPromoteOamXHigh` mirrors bit 8 into bit 15 before the packing
  sequence so columns in the right margin land on the right.

**Result.** Over the 3,300-frame right-running script at 16:9
(`DKC3_SPAWN_TRACE=1`, new), every right-side spawn landed 279 to 286
pixels ahead of the camera with the widening off (`DKC3_CULL_WIDEN=0`,
new) and 322 to 328 with it on, with left-side spawns now at -60. At the
first banana arc (world $7A0), frames 748 to 780: with the widening off
the Kremling pops in inside the native area at frame 764 and the arc
appears at the native edge at frame 780; with it on the Kremling is
already walking in the right margin at frame 748 and the arc slides in
through the margin from frame 764, on the correct side. At 4:3 the run's
hashes are identical with the widening off and on. `DKC3_OAM_TRACE=1`
(new) prints every sprite outside the native columns per frame, and
`DKC3_CULL_TRACE=1` the first widened helper calls.

**Open.** `$B9:AA3F` tests "sprite fully on screen" ([0, $100) by X) for
some behavior gated on `$05AF` bit 14 and runs in the interpreter; it is
not widened. The level-specific spawner at `$BE:E27F` ([-$18, $118)) is
gated off on this level and is not widened. Sprites at the widened view
share the cartridge's OAM budget, so a screen crowded to the margins can
drop sprites the native view would have drawn.

## 2026-09-03 - public repository and first macOS alpha release

The repository was made public and the first native Mac build was prepared as
the `v0.0.1` alpha. It is an arm64 app for macOS 26 or newer; the deployment
floor is raised from the project's requested 14.0 because the bundled Homebrew
SDL2 binary itself targets 26.0. The bundle is ad-hoc signed and is not
notarized. It contains the executable, its launcher/runtime resources, and the
bundled SDL2 dynamic library, but no ROM, generated C, saves, screenshots, or
other private game data.

The placeholder icon was replaced with original project artwork generated by
OpenAI's built-in image-generation tool: a brass `3`, dark-walnut barrel,
turquoise river, and tropical foliage on a deep-teal Mac icon tile. The 1024px
RGBA source master and the complete multi-resolution `.icns` are recorded in
`assets/macos`; no source image, game artwork, character, or publisher logo was
used.

The release build completed through `build_macos.sh`, and all 19 CTest checks
passed from `build/macos`: the 15 public unit/tool tests plus the private-ROM
600-frame headless boot, hidden interactive-app smoke, 16:10 smoke, and quick
save/load smoke. Deep strict code-signature verification passed before
archiving. Full-game completion, Intel Macs, notarization, and Windows remain
unverified.

## 2026-09-03 - Murky Mill's light windows cross the wide margins

The reported Murky Mill frame cut the right lamp's light cone at the native
256-pixel edge and showed a clipped copy of that cone in the far-left margin.
The quick-save reproduction was terrain-ready at camera `$058C,$07C8` in
Mode 1. Layer isolation put the light on BG3: map `$70`, enabled and windowed
on the main screen (mask `$04`), with HDMA changing the window across the
scanlines.

The map is 32 columns, so the adapter treated BG3 as a bounded plane. It first
rendered the native line with the PPU window applied, then repeated that
already-clipped line into the margins. That preserved the tilemap's 256-pixel
wrap but incorrectly repeated screen-space clipping. The adapter now selects
enabled, windowed 32-column Mode-1 layers for the physical-wide render path.
The PPU therefore evaluates the live window coordinates over the presented
span, while ordinary non-windowed bounded layers still repeat. The existing
`DKC3_CULL_WIDEN=0` switch disables this selection for a same-binary A/B run.

**Result.** With widening on, the right cone continues to its real HDMA edge
through the right margin, while the repeated left fragment becomes the narrow
far-left tail defined by the live window, at 16:9 and 16:10. The 4:3 A/B frame
and its WRAM, VRAM, CGRAM, and OAM hashes are identical. At 16:9, the 256x224
center crop is also pixel-identical while 11,587 pixels change in the wide
frame. Thirteen samples over a 123-frame headless replay kept the center
identical and the guest-state hashes equal; the supplied input did not move
the restored state, so traversal through the whole stage and other
windowed-effect variants remain unverified.

## 2026-09-03 - v0.0.2 macOS alpha

The Murky Mill light-window fix is packaged as the `v0.0.2` Apple-silicon
alpha. The app remains ad-hoc signed, not notarized, and limited to macOS 26
or newer by the bundled SDL2 binary. The source-only repository and release
bundle contain no ROM, saves, generated C, screenshots, music, or extracted
game assets. Full-game traversal, Intel Macs, notarization, and Windows remain
unverified.

## 2026-09-03 - selectable 21:9 ultrawide mode

After publishing v0.0.2, the aspect selector gained a fourth, appended value
so the existing persisted indices for 4:3, 16:10, and 16:9 remain unchanged.
The launcher, in-game overlay, macOS View menu, `DKC3_ASPECT` environment
override, SDL host, Windows host, and headless host all accept 21:9. Host frame
buffers now allocate the maximum supported width rather than the old 16:9
width.

Exact 21:9 at the SNES 7:6 pixel aspect ratio is 448x224, which needs 96
source pixels per side. The shared runtime's proven-safe nine-bit OAM limit is
95, because sprites in the added right margin still need a representable X
coordinate. The option therefore uses the nearest symmetric safe width,
446x224 (approximately 20.91:9), and remains labeled 21:9 (Ultrawide) in the
UI. The `snesrecomp` submodule was not changed.

**Result.** The geometry unit test covers parsing, the stable enum values,
446x224 allocation, 95-pixel margins, edge reflection, cull expansion, and
the two-pixel difference from exact 21:9. A ROM-backed hidden SDL smoke
reported `aspect=21:9 (446x224)`. At the preserved Lakeside Limbo state the
wide frame was terrain-ready with every present native and margin cell
matching the decoded level map. With cull widening disabled, its 256x224
native window at the glide-adjusted offset was pixel-identical to 4:3, and
WRAM, VRAM, CGRAM, and OAM hashes matched. A 180-frame right-input replay
remained terrain-ready at its final frame with all 1,829 present cells and all
868 margin cells matching; the resulting frame showed continuous boardwalk,
mountains, water, and objects through both margins.

The published v0.0.2 release remains unchanged and does not contain this
follow-on option. Traversal across the whole game, crowded-screen OAM behavior
at the maximum width, and the carried-over Windows host remain unverified.

## 2026-09-03 - placement index covers the ultrawide view

The owner's Lakeside Limbo quick save reproduced a Sneek appearing well inside
the 21:9 view after moving right only a few pixels. After restoring the paused
state, a Start pulse followed by Right activated the Sneek on replay frame 16,
exactly as camera X crossed `$01FF` to `$0200`. Its camera-relative X was 335;
with the 95-pixel margin and presentation bias 63, it appeared at presented X
367, 79 pixels inside the 446-pixel frame. No sprite slot existed for it on the
preceding frame, ruling out the renderer and the PPU sprite budget.

`$BB:A47A` builds the level's 256-pixel placement-cell index before the first
terrain frame can become ready, so the prior terrain-gated adaptation built a
cartridge-width table. `$BB:A647` also selected only the camera's current cell.
Placements already associated with that cell benefited from `$BB:AAD4`'s
widened final check; a placement such as this Sneek was not examined until the
camera entered the next cell. Quick saves preserved the old table, and aspect
selection is host-only, so rebuilding the table only at level entry would not
fix the owner's exact save.

The runtime scan now merges the current cell with its immediate horizontal
neighbors, deduplicates their placement IDs, and passes that broad-phase union
to the unchanged, terrain-gated and bias-specific final activation check. The
maximum 21:9 extension is less than one 256-pixel cell on either side, so the
neighbors cover every possible glide bias. This also works with existing quick
saves without rebuilding or disturbing their object state. `DKC3_CULL_WIDEN=0`
keeps both the scan and final check native for same-binary comparisons.

**Verified.** Regeneration left `$BB:A647` generated (3,035 AOT functions,
1,721 interpreter fallbacks). On the supplied 21:9 replay, both the Sneek and
the adjacent K token now activate on frame 5 at camera X `$01FB`, before the
old `$0200` bucket boundary; the Sneek is rendered by frame 7. The Sneek also
activates on frame 5 at 16:9 and frame 31 at the narrower 16:10 width. At 4:3,
enabling the new scan produced the exact same frame, WRAM, VRAM, CGRAM, OAM,
audio, and run-stat hashes as the native scan over 120 replay frames. With
`DKC3_CULL_WIDEN=0`, all four aspect modes reproduced the corresponding
pre-change 120-frame hashes exactly. The ROM-free and private integration
suite passes all 20 tests, including headless boot, hidden macOS app smoke,
16:10, 21:9, and quick-state loading. The supplied state remains private under
`.cache`; whole-game traversal and the carried-over Windows host are still
unverified.

## 2026-09-03 - underwater color math covers the wide margins

The owner's Floodlit Fish quick save showed the expected dark underwater tint
inside the native 256-pixel span but untreated, bright side margins at 21:9.
The frame uses Mode 1 with BG3 on the main screen and BG2 on the subscreen.
Across the initial save's water surface, HDMA moves BG1 and OBJ between them
in three measured splits (`TM/TS=$15/$02`, `$05/$12`, and `$04/$13`). BG3 is
selected by an inverted full-width window, and `CGWSEL=$02`, `CGADSUB=$64`
applies half-color addition against the subscreen. In the cartridge view the
effect has a subscreen color to blend against. Some valid adjacent-world
BG1/BG2 pixels are transparent in the reconstructed margins, however, so the
PPU correctly treated them as the backdrop and skipped half-color. That
exposed the otherwise correct BG3 at its raw brightness.

The host now recognizes that exact register signature before subscreen
composition. On matching widescreen scanlines it fills only transparent
subscreen margin pixels from the corresponding native column, following the
layer's 256-pixel wrap. If that wrapped sample is also transparent, it uses the
nearest nontransparent native subscreen pixel on the same line. Valid
adjacent-world subscreen pixels remain intact, the main screen is never
changed, and the policy is inactive in 4:3. The signature and
transparent-only behavior have ROM-free unit coverage; the headless
effect-register dump records the evidence needed to distinguish this
composition from other BG3 effects.

The first implementation exposed a separate renderer interaction in the
owner's next capture: merely installing a line enhancer activated the shared
PPU's legacy BG1 viewport clip. That behavior exists for older clients whose
enhancer replaces BG1, but here the callback repairs only the subscreen. It
therefore removed DKC3's already reconstructed ceiling and terrain from both
margins at exactly the native boundaries. The runtime now has an explicit,
default-off layer mask for enhancers that must preserve an already-widened
layer. DKC3 opts BG1 into that mask while widescreen is active. A runtime unit
test proves both the legacy default and the opt-in behavior.

A later quick save at timer `0:25:54` adds a fourth measured split,
`TM/TS=$17/$13`, over its first 23 scanlines. The original three-pair gate
therefore left the far-right water surface untreated even though the rest of
the frame was corrected. Extending the gate alone was insufficient because
the corresponding native subscreen sample was transparent on that segment;
the nearest-sample fallback is what supplies its color-math operand. The new
pair is part of the same exact Mode 1, window, and color-math signature and is
covered alongside the gate's negative unit case.

**Verified.** The supplied state reproduces both the original untreated
subscreen margins and the follow-up BG1 clipping regression. After the final
change, captures at 16:10, 16:9, and 21:9 carry the underwater grade and BG1
terrain continuously across both margins. The initial loaded frame and a
98-frame rightward replay both keep every widened frame's 256x224 center
pixel-identical to 4:3. At ultrawide replay frame 88, the BG1 opt-in restores
12,463 left-margin and 12,485 right-margin pixels relative to the broken build,
with zero center pixel changes. At the later `0:25:54` state, the nearest
subscreen fallback corrects only the 902 faulty right-margin pixels and changes
neither the left margin nor the center. Both saved positions have zero center
byte differences against 4:3 in 16:10, 16:9, and 21:9. The initial 4:3 frame
hash remains exactly
`733ba0cbd0c420b2519f697d787ff66aac546be536fd9a10a4def243decb877b`,
and its WRAM, VRAM, CGRAM, and OAM hashes match across aspects. The supplied
state remains private under `.cache`. The standalone PPU regression and all
20 project tests pass, including the native headless, macOS quick-state,
16:10, and 21:9 checks. Traversal through the whole stage and other underwater
effect variants remain unverified.

## 2026-09-04 - the river's water edges: a second layer that is the map

The owner's screenshot in the underwater river level (level `$32`, camera
`$0E8A`x`$0218`) showed the water surface looking different at both
margins: flatter and bluer than the native water, with a hard top edge
where the native has the rock ceiling's reflection under the surface.

**What the scene is.** BG1 (`$7800`) is the terrain ring. BG3 (`$6800`,
32x64) is the water texture, on the main screen only below the water line,
through an inverted window 1 that HDMA channel 2 sets to `[0,255]` there;
channels 7 and 3 rewrite TM and CGADSUB per line so the water is BG3
half-added to the subscreen and the underwater region is the backdrop
half-added to it. BG2 (`$7000`, 64x32) carries the subscreen's content
and is two things at once: its rows under the water line are streamed with
the camera, and they are the level map itself, rows 36 to 39 in world tile
units, 288 pixels above the water, decoded exactly (28 of 28 native cells
at two cameras); its underwater rows are a static 512-pixel backdrop that
never changed over 130 pixels of travel; its air rows are a parallax band.

**What went wrong.** The band classifier treated BG2 as one thing per
frame. At the terrain phase it took the plain world policy, so the margin
read the terrain store at the same world rows, which the map leaves empty
under the water, and the reflection vanished there; at rest the whole map
counted as static and became a plane; moving, the streamed rows stamped
the map's pages and every band fell to repeat, which repeats a 512-pixel
backdrop at 256. The subscreen fill for Floodlit Fish
(`Dkc3EnhanceUnderwaterSubscreen`) then painted the nearest native
column into transparent margin pixels, which in this level are open water.

**What changed.**

- Map rows are tracked for change individually (`Dkc3TrackMapRows`), and a
  band is static when the rows it shows have gone unwritten for the travel
  gate, not when the map's pages have; the backdrop rows are planes while
  the reflection rows stream.
- A second layer's band is world content only when it proves it: its map
  is the terrain ring itself, or its rows equal the level map's decode at
  one row offset in nine of ten non-empty cells over the native columns
  (`Dkc3ComputeRowAliasOffsets`, offsets up to 40 rows, the last proven
  offset tried first). Such bands take a new policy, alias (`A` in the
  band dump), which keys the layer's view of the terrain store that many
  rows away, and the prefill decodes those source rows for every column
  the widened view reaches. Here the offset is 36 rows and every populated
  reflection row verifies at 33 of 33 cells.
- The subscreen fill runs only where the native subscreen row is fully
  opaque, its premise; a native row with transparent pixels leaves the
  margins alone.

**Result.** At the save and 128 pixels on, the reflection continues into
both margins and the underwater backdrop wraps as a plane; the native
window is unchanged (0 pixels) and the 4:3 hashes of the Lakeside run are
identical. `DKC3_ALIAS_TRACE=1` prints each ring row's verdict.

**Lessons recorded in the guide.** A render with `SNESRECOMP_LAYER_MASK`
set, even to all layers, presents the margins differently (10,614 margin
pixels differed at this spot, none native): it is for identifying layers,
never for judging margins. And a layer at the terrain phase is not the
terrain until its rows are verified against the map.

**The cartridge's side, traced afterwards.** Level `$32` is Riverside
Race. Its header sub-record sets `$0775` bit 7, which enables a second
column streamer: `$B7:B75E` decodes, for the column entering the view,
map tile rows 2 to 7 of the level map's first two metatile rows (no
vertical term at all; flips honored, no palette change) into a six-word
strip, and `$B7:B8D1` uploads it to VRAM `$70C0` plus the ring column,
that is BG2 ring rows 6 to 11. The strip is therefore world rows 34 to 39
shown at rows 70 to 75, the 36-row offset the verifier proves. The
underwater rows 14 to 31 are a 32x18 tilemap uploaded at load to both
ring halves (`$71C0` and `$75C0`, payload list 7 at `$FD1C8A`), so the
512-pixel wrap is two copies of one picture and the plane policy is
right. The band above the water is a half-speed parallax (`$B3:9485`:
h and v = camera / 2, seam fixed at world y `$22F`), showing backdrop
rows 27 to 30. Channel 7 sets TM/TS per line (`$17/$13` above the seam,
`$15/$02` for 39 lines, `$05/$12` for 9, then `$04/$13`), channel 3
CGADSUB `$24` then `$64`, channel 6 the backdrop color per band, channel 2
window 1, channels 4 and 5 the BG3 and BG1 wobble. The water level word
`$050F` plays no part in any of it.

**A regression caught the same evening.** With the alias in, the owner's
forest save in the same level showed black blocks at the top of both
margins. The half-speed parallax band wraps its rows so that it displays
ring row 8, the one row the verifier had proven, and the band rule
accepted a band on its verified rows alone: the band became an alias
keyed by the terrain phase it does not scroll at, its lookups reached
rows the store never held, and BG2 went transparent to the black
backdrop. An alias band must scroll at the terrain phase and every
populated row it shows must verify; the parallax band is a plane again
and the river's reflection band is unchanged.

**Open.** Floodlit Fish's fill was not re-checked after its gate. A
one-pixel bright column at the native edge in some water frames was seen
once in the sweep and not chased. The header bit `$0775` bit 7 could
gate the verifier's search instead of running it on every second layer.

## 2026-09-04 - a strip at the top of the margins that flashed

In the forest of the same level the owner saw a small corner at the top
of the right margin flash in and out. Every frame of a walk was rendered
and the corner's dark pixels counted: the count jumped for exactly the
two frames on which the terrain's vertical scroll sat on a tile boundary
(400, while the camera drifted from 400 to 399), and the cell dump for
those frames showed the margin's top row decoding as the transparent
tile with the native top row recorded as mismatching.

The cause was a rule inherited from DKC2Recomp's prefill: for horizontal
layouts, the first visible tile row's margin cells were forced to the
transparent tile, because DKC2's column builder supplies that row from
the live ring at its vertical page boundary. DKC3's builder rotates 32
map rows from the camera's page into the ring, so that row decodes like
the rest, and the rule blanked eight pixels of real foliage whenever the
scroll landed on a boundary. The rule is removed. The walk's corner
count is now smooth across those frames, the native top row matches,
the Lakeside corpus still matches on every ready frame, the river render
is byte-identical, and the 4:3 hashes are unchanged.

**Lesson.** A special case ported from another game must be re-proven
against this game's streamer before it stays; the diagnostic that finds
it is a per-frame signature of the flashing region, not a screenshot.

## 2026-09-06 - first Windows x64 build and packaged launch verification

Built source revision `5afdacbd284144a92521d5219250d39ddca167c3` with
Visual Studio 2022 / MSVC 19.44.35214, Release optimization, static MSVC
runtime, and SDL 2.30.9 (`c98c4fbff6d8f3016a3ce6685bf8f43433c3efcc`).
The supplied external 4 MiB ROM matches the supported SHA-256. Generation
used the Python analyzer: 4,724 roots, 3,046 AOT variants, 1,710 interpreter
variants, and all generated widescreen override anchors passed. This is
not the native-analyzer output used for the earlier Mac measurements.
Neither the private ROM nor generated C is tracked.

All three Windows executable targets compile. The portable package selects
the SDL/OpenGL host (`DKC3RecompSDL.exe`, renamed to `DKC3Recomp.exe`), which
shares the Mac host's reconstruction shaders and portable audio rate control.
CMake now stages SDL2.dll beside that executable and removes one duplicate
HDMA source-list entry from the legacy Win32 target.

The 16 ROM-free baseline tests passed before the build. Four new sequential
Windows integration checks exercise 4:3, 16:10, 16:9, and 21:9 with reconstruction,
overlay, rewind, fast-forward, and quick save/load. Their first run exposed
that save/load and preloaded-state automation were incorrectly inside the
Mac menu-command guard. Only the Mac command call is now guarded; the shared
test hooks execute on Windows as well. The full 21-test suite then passed.
Windows tests use the build executable's directory for temporary saves and
run serially because that host anchors paths beside the executable.

A separate 4,801-frame headless run completed with 4,250 video-active and
4,322 audio-active frames. The standalone package recognized the ROM in its
launcher and visibly rendered the intro, title, and world map. Its SDL2/OpenGL
4.6 NVIDIA runtime reported 16:10 (308x224), reconstruction sampling, audio
available, and a clean exit after 8,998 host frames. No developer PATH or
external SDL DLL was needed. UI automation did not reliably open the overlay
for a visual settings inspection; overlay rendering and the save tools were
exercised by the automated smoke tests instead.

The runtime still logs an `unresolved-abandon` diagnostic at `$808CAD` during
boot, even though these runs complete. This was not suppressed or converted
into a fabricated success. Full-game traversal, physical controller testing,
visual validation of every wide level effect, and frame-pacing benchmarking
remain unverified. The package contains no ROM, saves, generated C, or captures.

## 2026-09-06 - Windows menu parity and dark presentation (r2)

The SDL Windows host now exposes native Game and View menus. Game provides
Pause / Settings, quick save/load, About and Quit. View provides fullscreen,
four aspect ratios, three scalers, five reconstruction modes, four level-edge
policies and four screen models. The platform-independent menu model shares
the existing settings indices, applies selections through the existing host
setters, and synchronizes native checkmarks with the shared overlay. Choosing
a reconstruction mode also enables reconstruction scaling. Detailed sliders,
audio, bindings and Assist Tools remain in the same shared overlay used on Mac.
The stale DKC2 overlay heading and Mac-only 16:10 label were corrected.
Mac application-management items use normal Windows window controls instead;
the Mac display-link implementation is platform-specific, not a missing setting.

The product title is now DKC3Recomp. Windows uses documented DWM title colors
and Win32 owner-drawn menus, retaining accessible labels, command routing,
keyboard mnemonics, selection highlights and checkmarks. No undocumented
uxtheme entry points or system-wide theme changes are used. DWM attributes
unsupported by older Windows versions fail harmlessly. The launcher is themed
through an SDL window-event watch; the game themes its HWND directly.
OS-close events are handled before overlay event consumption so the close
button also exits while Settings is open.

Baseline 21 tests passed before this milestone. Two synthetic tests cover
selection bounds/nulls/enum parity and real native menu hierarchy, command
delivery, checkmarks, background color, owner-draw measurement/rendering and
mnemonics. The updated 23-test suite passes, including the four external-ROM
aspect-ratio smoke checks; the separate ROM-free build passes all 17 tests.
Live packaged checks confirmed dark title/menu bars and nested aspect menus,
16:10 persistence, changing to 16:9, the Level-3 reconstruction controls in
Settings, and exiting through the window close button while Settings was open.
Earlier live menu checks exercised quick save/load and Game > Quit. This is
launch/menu/settings verification, not a claim of complete game traversal.
Private logs are under ignored build-tests-windows; no screenshots are tracked.
The final launcher also visibly uses the dark title bar. A fresh extraction of
the r2 ZIP completed the 180-frame 21:9/reconstruction smoke check with rewind,
fast-forward and save/load all passing and exit code 0. The archive's SHA-256 is
`fb1a16faf398fb0b731788c0b0737085b43579d141809efcda65494358230b92`;
the packaged executable is
`c81a16da2d0c8b57be67820059df80eee3235af5cbe6a9a67c6050a9fb4d7321`.

Win32 API references: Microsoft Learn's [Using Menus](https://learn.microsoft.com/en-us/windows/win32/menurc/using-menus)
and [DWM window attributes](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute).

## 2026-09-06 - v0.0.5 cross-platform release

Version 0.0.5 packages the Windows menu/theme work as a normal release. The
source keeps the existing Mac host, menu and display-link files and both
submodule pins unchanged. The previous v0.0.4 Apple-silicon ZIP is included
byte-for-byte, with its original filename, embedded version and signature;
it was not rebuilt or runtime-tested on Windows. Its SHA-256 matches the
previous GitHub release: `5b8966c44d6973f8244939c49c23a2d25fc0085bc7513ae87e3ab6259f986601`.

All Windows targets were rebuilt after the version bump. Baseline and final
23-test suites passed, as did all 17 tests in the separate ROM-free build.
A fresh extraction of the Windows release ZIP reported version 0.0.5 and
completed 180 frames at 21:9 with reconstruction, overlay, rewind,
fast-forward and save/load checks passing, followed by a clean exit (code 0).
Windows ZIP SHA-256:
`c7576ef6044727206064852fe996938fdd8d89f4d3ff1bbe4de40f7776b4c720`.
Release assets stay outside Git under the ignored build-release-v0.0.5 folder;
only source, tests, notices and documentation are committed. Full-game and
new Mac runtime verification are not claimed.

## 2026-09-04 - DKC1-style enemy-stomp haptics

DKC1's native host uses a narrow game-state transition to recognize a
successful enemy stomp and sends a 55 ms SDL controller-rumble request from a
worker thread. DKC3 now uses the same presentation behavior, with the trigger
re-derived from DKC3 rather than carrying over DKC1's WRAM addresses.

The first DKC3 trigger tried to identify the result of
`player_interaction_10` at `$B8:9001` from only the active player's state and
Y speed. Live checks showed why that is insufficient: different Kongs and
animal paths use different state/velocity pairs, and one Ellie-specific flag
also changes during an ordinary jump. That approach both missed enemy hits
and produced a false pulse on a jump, so none of those player-only heuristics
remain in the trigger.

The corrected evidence follows the actual enemy defeat. DKC3's 26 non-Kong
sprite slots run from `$0954` through `$1412` in `$006E`-byte records.
`defeat_sprite_using_animation` at `$B6:8085` moves a live enemy into a
distinct fall-away state: placement number `$0000`, placement parameter
`$0005`, render order `$00F4`, OAM property bits `$3000`, and cleared
interaction flags, while retaining its sprite type for the defeat animation.
The host snapshots every live enemy before a cartridge frame and accepts a
haptic event only if the same sprite type newly enters that defeated state
while the same active player changes from downward to upward Y movement.
This directly implements the rule that the jump must kill the enemy: a plain
jump has no enemy transition, and a projectile or other unrelated defeat has
no simultaneous player rebound.

The SDL host sends DKC1's pulse (`$2800` low frequency, `$5000` high
frequency, 55 ms) to the gamepad assigned to the cartridge's active player.
The SDL call stays on a dedicated worker so a controller or Bluetooth driver
cannot stall emulation; controller refresh and shutdown wait for any in-flight
call before closing the handle. An `Enemy-stomp haptics` checkbox in the
pause menu applies immediately and persists as `HapticsEnabled` in
`launcher.cfg`; the same section reports the detected controller and its SDL
rumble capability and offers an unmistakable 500 ms test pulse.
`DKC3_HAPTICS=0` remains an override for scripted launches.

Public unit coverage accepts a jump-kill transition in both the first and last
enemy slots. It rejects an ordinary jump, a non-jump enemy defeat, an enemy
that was already defeated before the frame, a reused slot with a different
sprite type, and an invalid or changed player. The macOS SDL host and the full
public test suite were rebuilt successfully. A connected Sony DualSense was
enumerated by the native host as `DualSense Wireless Controller` with SDL
rumble support. Both a direct SDL rumble request and the pause-menu test pulse
were physically confirmed. The corrected automatic gameplay trigger still
requires the final check at the reported save. The separate Win32 host remains
unchanged and unbuilt here.

The same follow-up corrected the pause window itself. SDL's renderer supplies
physical drawable pixels, but ImGui positions windows in logical points; using
the drawable dimensions displaced the menu on Retina displays. The initial
position and size now use `ImGuiIO::DisplaySize`, the window is centered only
on its first appearance, and the `NoMove` flag is removed so its title bar can
be dragged without the next frame snapping it back. The heading now also says
DKC3 rather than DKC2. The hidden overlay smoke and persisted-haptics tests
cover startup and configuration. Its centered initial placement and title-bar
dragging were also confirmed visibly on the Retina display.

## 2026-09-04 - optional DKC1-style MSU-1 music

The macOS SDL host now supports optional MSU-1 PCM packs without applying an
IPS patch to the user's ROM or adding music to the repository. As in the DKC1
host, all present tracks are memory-mapped during startup, resampled from
44.1 kHz into the cartridge audio stream, and mixed after the stock DSP so
sound effects remain intact. Enabling a valid pack applies only the patch's
two-byte SPC-music mute in memory, after the supported ROM has passed its
SHA-256 check. The source bytes at headerless offset `$2D02D3` must be exactly
`D0 05`; the host fails closed if they move.

The DKC3 mapping was derived from the supplied patch and checked against the
public disassembly's addresses and structure. Direct-page `$08` is the current
song. Normal song values 1 through 46 select the same-numbered PCM tracks.
The transition entry at `$B2:800F` maps commands 1 through 10 to tracks 49
through 58, with command 11 intentionally aliasing track 53. Tracks 20, 22,
24, 46, and 50 through 58 are one-shots; track 49 and the other normal songs
use their PCM loop points. A fail-closed generated-source adaptation records
the transition command without modifying cartridge state. Save loads and the
end of a rewind reset the external player from the restored `$08` state.

The macOS Music menu accepts an extracted folder or a `.msu1` archive and
persists the chosen directory. Folder discovery supports `track-N.pcm`,
`dkc3_msu-N.pcm`, and `dkc3_msu1-N.pcm`. The two supplied external packs,
"DKC3 pcm by JUD6MENT 2025 update" and "Donkey Kong Country 3 (HD Audio
Restoration)", each exposed 56 validly named PCM files. Both completed a
300-frame hidden native run with audio enabled and selected track 22 at the
title sequence. The dedicated PCM/mute/mapping unit test and the full suite
pass (23 of 23), including private headless, macOS, save/load, rewind,
widescreen, and overlay smoke tests.

The live runs prove pack discovery, exact ROM gating, native integration, and
track selection; an audible full-game pass through every normal and transition
track remains unverified. MSU-1 remains macOS-only for now. The separate
Win32 host is unchanged and unbuilt here.

The first mixed file-and-directory `NSOpenPanel` made a selected folder act as
a navigation target, leaving its PCM files disabled and preventing the user
from confirming the pack. The menu now asks for **PCM Folder** or **.msu1
Archive** first and configures a directory-only or file-only panel accordingly.
In folder mode, **Use Music Pack** selects the folder instead of opening it.

## 2026-09-04 - original music overlapping MSU after a quick load

The reported doubled soundtrack was reproducible with an older Lakeside Limbo
save. The boot-time mute only changed the verified ROM image; a snapshot
restored its own unmuted SPC RAM at `$07AB` (`D0 05`) and the original song
continued underneath the host PCM. Applying the branch mute after loading was
not enough either: already-sounding music voices retained their envelopes.

`Dkc3RestoreSpcMusicPolicy` now runs from the adapter's post-load hook, covering
file quick loads and in-memory rewind. The policy comes from the current
verified ROM image, not the save. `$B2:82E5` uploads ROM `$ED:0088` to SPC
`$0560`, placing the two mute bytes at `$07AB`. The helper compares the whole
`$07A9..$07DA` scheduler against that image except for the two known policy
bytes, so an unrecognized/early-boot engine is left untouched. When changing
an old save to replacement music, it also releases only music-owned DSP voices
(`$01E0+voice == 0` in the SPC scheduler), clears pending music key-ons, and
discards the pre-rendered audio and shared echo containing the original song.
An active SFX voice keeps its complete envelope, position and volume. A shared
SFX echo tail is discarded once along with the music echo. Ordinary same-policy
loads do not clear voices or buffers. Loading an MSU-era save with replacement
disabled restores native sequencing instead of leaving the game muted. No
snapshot-format change or on-disk ROM/save rewrite is involved.

The macOS headless host now accepts an explicit `DKC3_MSU1_PACK` for repeatable
audio isolation with its existing snapshot, input-playback and PCM-output
controls. It does not read or modify the app's saved pack preference. Example
(all inputs/outputs remain private and outside Git):

```sh
DKC3_MSU1_PACK=/private/pack DKC3_MSU1_GAIN=0 \
DKC3_SAVESTATE_INPUT=/private/old-state.sav \
DKC3_AUDIO_PCM=/private/sfx-only.pcm \
build/macos/dkc3_snesrecomp_headless /private/dkc3.sfc 602
```

**Verified:** on the existing Lakeside Limbo save, 602-frame native runs with
both supplied packs reduced the stock-only output after the first five seconds from RMS
498.462 to exactly zero (0 nonzero samples out of 321,478). With replacement
gain restored to 1, JUD6MENT and HD Audio Restoration produced RMS 2550.856 and
1451.920 respectively over that same window. A scripted unpause/jump run with
replacement gain 0 produced SFX (peak 7151), then returned to exact silence;
three saved underwater positions also stayed silent when idle. The separate
mouse-pop save preserved its four active SFX-owned voices while all four
music-owned voices had zero output. ROM-free tests cover every one of the 256
SFX-ownership masks, pending notes, stale echo/output, same-policy loads,
restoration of native music, and fail-closed signature/metadata checks. All 24
CTest checks pass. The signed macOS app also completed isolated 300-frame
quick-load runs with each pack, selecting track 15; those smoke runs used a
dummy audio device to avoid interrupting the user's game. Deep strict signature
verification passed. An audible full-game pass through every event/transition
remains unverified, as does the Windows host. The user's ongoing gameplay was
left running; the rebuilt app must be restarted to pick up this fix.

## 2026-09-04 - KAOS's body clipped and repeated at the wide edges

The owner's quick save in level `$0020`, camera `$0130,$01A0`, reproduces
KAOS's body ending at the native right edge while a fragment repeats at the
left. The head and flame are sprites, but layer isolation identifies the
body as BG1: map `$71` (VRAM `$7000`, 64x32), character base `$6000`,
scroll `$0374,$00EC`. Its two horizontal pages contain 156 and zero
nontransparent cells, satisfying the existing object-plane verifier. BG2
at `$79` is the terrain ring.

The initial main-screen enable is `$16`, excluding BG1. HDMA changes it to
`$17` on lines 20 through 147 and back to `$16` afterwards. The frame's
wide-layer selection looked only at the initial registers, so BG1 never
reached the object-plane classifier. The later repeat-mask calculation did
include HDMA enables, causing the already-clipped native body to repeat
every 256 pixels. H4v0c21 revision
`bed96892f5e85eabd5c920306f00b361c2e1f34c` confirms the structure:
`$B5:C2C9` derives the boss scroll, `$B3:A277..A2DD` constructs the
`$16/$17` enable table, and `$B3:A371..A383` applies the BG1 scroll.
These are reference addresses only; no disassembly source or data is copied.

The adapter now scans the already-built HDMA tables before choosing wide
layers. `Dkc3VideoPpuFrameWideLayerMask` includes both main- and subscreen
enables from every band and retains the existing Mode-1, map-width and
tilemap-collision gates. The body consequently reaches the existing verified
object-plane path (`P71` in the band trace), while the original HDMA still
controls its vertical visibility. The guest, generated code, and culling
contracts are untouched. `DKC3_CULL_WIDEN=0` restores the old selection for
same-binary comparisons.

**Verified.** The private save SHA-256 is
`c156d6601659c612012e84c79acf9eb4d6ff20c6beba42e8f43fcd630e2761cc`.
At the initial save, the switch-off frame exactly reproduces the pre-change
binary in all four aspects. The fix changes 4,353 pixels at 16:10, 5,451 at
16:9 and 5,880 at 21:9, restoring the right body and removing its left copy;
zero pixels change in the visible native window. The 4:3 frame remains
`7cffce5ff6e97665ef29eebce5c4f16b0438bb675dd07a4d4fe08736b7d2dd43`.

A 1,803-frame replay (three neutral frames, one Start pulse, then neutral)
moves the boss through the arena. The 151 samples at frames 2 through 1802,
every 12 frames, plus the final frame, preserve every visible native-window
pixel against both the previous binary and the matching 4:3 frame in all
aspects. Final WRAM, VRAM, CGRAM and OAM hashes also match the prior binary
in each aspect. Five existing private saves (Lakeside Limbo, mouse-pop and
three underwater positions), replayed for 123 frames in all four aspects,
retain all frame and guest-memory hashes. All 24 CTest checks pass, including
19 ROM-free tests. The new synthetic HDMA test covers the boss's enable span,
late subscreen enables, bounded layers, unsupported mode, null input and
overlapping tilemap rejection.

The rebuilt, ad-hoc-signed canonical macOS app completed a hidden 90-frame
load of the exact save with the user's reconstruction settings; its
3080x1920 drawable capture shows the corrected body. Deep strict signature
verification passes. The old app process was closed and the canonical bundle
relaunched at the preserved save (PID 60788, 22:29:24 local, after the
22:26:55 executable build); a live window capture confirms the corrected
body and absent left copy. The user's on-disk quick save retains its original
SHA-256. Private inputs, comparison scripts, logs and captures
are retained outside Git under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-kaos-20260904`.

**Limits.** This validates the reported state and the 30-second replay, not
completion of the boss fight or every boss phase. The save's existing terrain
prefill discrepancy (1,293/1,302 total cells and 309/310 margin cells matching
at 16:10) is identical before and after and was not changed here. Windows
remains unbuilt and unverified.

## 2026-09-04 - Waterfall columns disappear and wrap across wide edges

The new quick save is level `$0038`, shape 4, camera `$02BF,$1F99`;
its SHA-256 is
`a038c56a8671a9b45792fd2cd6e8910fd6a1e11bb3be1d8695112ae48f6b7338`.
Layer isolation identifies BG2 as the waterfall: Mode 1, map `$71`
(VRAM `$7000`, 64x32), character base `$5000`, horizontal phase matching
the camera and a vertically animated HDMA phase. BG1 is the terrain.

A fresh restore initially classifies BG2 as a static plane because it has
no write history. Camera travel rewrites its rows and changes the policy to
native-line repetition. The latter produces the reported right cutoff and
left copy; the former can still sample recycled columns outside the view.
This is a zero-lead column streamer, so neither presentation policy is valid.

H4v0c21 revision `bed96892f5e85eabd5c920306f00b361c2e1f34c` provides
the reference structure. `$B3:8609..869A` uploads an entering column when
the camera crosses an eight-pixel boundary. `$B7:F421..F433` selects the
layout pointer at WRAM `$15E4` using the level flag `$0775 & $0200` and
index `$0793`. The ten pointers at `$B3:869B` address per-level lists
minus one; lists occupy `$86AF..87B5`, followed by column templates at
`$87B6..8EF5`. The renderer indices `$0D`, `$17`, and `$1B` call this
streamer. Only these addresses, bounds, and addressing relationships are
used; no disassembly source, comments, or game data are copied.

`Dkc3VideoDecodeWaterfallColumn` reads the user's verified ROM at runtime.
Each list byte selects the four tile columns of a 32-pixel world group;
kind zero selects the blank column and kinds 1 through 7 select the
appropriate 32-word template. Pointer membership, list extent, and template
bounds are checked. The adapter uses the full-bank HiROM mirror `$F3:0000`
to access bank B3, whose low half otherwise maps WRAM.

Before using the decode, `Dkc3VideoVerifyWaterfallViewport` requires exact
agreement with all 992 or 1,024 tile words in the fully uploaded native
columns. Partial edge columns are excluded because the next DMA may not yet
have run. The dedicated BG2 host store holds the decoded world columns and
repeats their 32 rows across all four epochs of the 10-bit vertical HDMA
phase. Its `F` band policy preserves native rendering and honors presentation
bias. Unsupported columns or failed source verification produce transparent
margin columns. Guest VRAM, simulation, generated code, and culling contracts
are unchanged. `DKC3_WATERFALL_TRACE=1` exposes the per-frame source check;
`DKC3_CULL_WIDEN=0` disables the new policy.

**Verified.** A 603-frame replay loads the save, pulses Start, travels left,
right, then left, and idles. All 601 restored frames pass waterfall source
verification at 16:10, 16:9, and 21:9. Every visible native-window pixel
matches both the previous binary and the corresponding 4:3 frame, including
the camera's slide at the west bound. All 601 4:3 frames are unchanged.
The fix changes margins on 124, 133, and 340 frames respectively; the largest
frame differences are 9,796, 17,412, and 29,456 pixels. Final WRAM, VRAM,
CGRAM, and OAM hashes match the previous binary at every aspect.

With `DKC3_CULL_WIDEN=0`, every sampled frame matches the previous binary
with the same switch disabled in all four aspects. Comparing switch-off to
the previous switch-on binary also exposes existing object-culling differences
at wider aspects during travel, so it is not a whole-frame baseline for this
isolated change. Five earlier private saves plus the preserved KAOS save,
replayed for 123 frames each at all four aspects, retain every final frame and
guest-memory hash. All 24 CTest checks pass, including 19 ROM-free tests.
Synthetic tests cover column/template addressing, scene selection, pointer
and list bounds, invalid kinds, native mismatch rejection, and fine-scroll
edge exclusion.

The rebuilt, ad-hoc-signed canonical macOS app completed a hidden 90-frame
load of this save using the user's 16:10 reconstruction settings. Its
3080x1920 drawable capture shows the waterfall continuing on the right and
the false left strip removed. Deep strict signature verification passes.
The main app was reopened from the canonical bundle (PID 63643, 22:51:07
local, after the 22:50:05 executable build), and Quick Load restored the
reported save. Live captures show the correction and subsequent gameplay
at 60 FPS. The user's quick-save file retains its original SHA-256.
Private snapshots, before/after headless binaries, replay scripts, measurements,
selected frames, and the app capture are retained outside Git under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-waterfall-20260904`.

**Limits.** The ten-second scripted traversal and live spot check do not
establish full-level completion or coverage of every waterfall layout and
renderer variant. Existing terrain-prefill mismatches during movement are
unchanged. Windows remains unbuilt and unverified. Changes remain uncommitted
pending the owner's verification.

## 2026-09-09 - Bleak invisible behind the arena; Mode 2 stayed 4:3

The reported and running-app images reproduce from the private quick save
SHA-256 `0a6e0eba0d07d38d28d2268bc1c82a3085b8c987b783905fa78a7374411d570a`.
The checkout is `5afdacb` plus the pre-existing uncommitted haptics, MSU,
KAOS, and waterfall work, all preserved. Level `$0021` uses Mode 2, BG1/BG2
maps `$7C/$74`, character bases `$0022`, and no HDMA. The configured aspect
was already 16:10; this mode had no supported wide presentation path.

**Boss cause and correction.** OBJ-only rendering contains Bleak in OAM
slots 21 through 36 at priority 2. The composite hides him because the fast
PPU uses Mode 1's BG priorities in Mode 2. The independent scalar renderer
in the pinned runtime already implements the correct order: OBJ3, BG1 high,
OBJ2, BG2 high, OBJ1, BG1 low, OBJ0, BG2 low. This also agrees with the
[bsnes Mode 2 implementation](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/ppu/io.cpp).
`cmake/Dkc3Ppu.cmake` adapts only the Mode 2 calls in a build copy of the
pinned PPU source, with a unique, fail-closed anchor. The submodule,
generated guest code, cfg contracts, ROM, and gameplay are unchanged.
`DKC3_PPU_LEGACY=1` exposes the scalar renderer in the headless host for
reproducible comparisons. Fixing native priority intentionally changes 1,210
pixels at the saved frame; preserving the previously broken native render
would preserve the defect.

**Wide policy and evidence.** H4v0c21 revision
`bed96892f5e85eabd5c920306f00b361c2e1f34c` identifies payload `$FD:290D`,
which loads the trench map from `$E9:45BC`, the mountain/sky map from
`$E9:4782`, and their graphics. These are fixed, bounded 32-column maps,
not a rolling terrain stream. The verified scene renders their actual
256-pixel hardware wrap over the wider physical span, keeping the camera
centered and leaving terrain readiness false so no activation/culling
windows widen. The gate requires gameplay, level `$21`, Mode 2 with small
tiles, maps `$7C/$74/$00`, character bases `$0022`, no HDMA, and all 1,024
BG3 offset-map entries having both layer-enable bits clear. Other signatures
remain bounded. Only structural facts are recorded in source; no maps,
graphics, assembly, or game data were copied.

**Verification.** A 1,803-frame replay restores the save, pulses Start,
then runs the fight for 30 seconds. At frames 2 through 1802, sampled every
three frames, all 601 complete images match the independent scalar PPU
pixel-for-pixel in 4:3, 16:10, 16:9, and 21:9. The samples contain 577
distinct native frames. Every wider frame's native center matches the
corrected 4:3 frame, with and without `DKC3_CULL_WIDEN=0`. The switch
disables only this margin policy, retaining the priority correction. Final
WRAM, VRAM, CGRAM, and OAM hashes match the previous binary in every aspect.
The margins therefore have both a source-map contract and an independent
whole-frame renderer comparison, rather than a visual-plausibility check.

Seven previous states (Lakeside Limbo, mouse-pop, three underwater states,
KAOS, and waterfall) retain every final frame and guest-memory hash across
123-frame replays in all four aspects: 28 comparisons, zero differences.
All 25 CTest checks pass, including 20 ROM-free tests. The new actual-PPU
test compares 180 Mode 1/2 BG/OBJ priority combinations on main/sub screens
against the scalar renderer; it fails on the original PPU. Synthetic scene
tests reject incorrect modes/maps, missing inputs, HDMA, and either OPT
enable bit at every address in the 1,024-word table.

The signed canonical bundle completed a 90-frame hidden quick-load run
with the user's reconstruction settings; its 3080x1920 drawable shows
Bleak and the extended arena. Deep strict signature verification passed.
The old process was closed and the rebuilt bundle launched as PID 56993
at 22:32:33 local, after the 22:31:58 executable build. Quick Load restored
the preserved state, and the live window shows Bleak in widescreen at
60 FPS. The quick-save hash is unchanged. Private inputs, before/after
binaries, replay scripts, reports, and captures are outside Git under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-bleak-20260909`.

**Limits.** This verifies the supplied position and the 30-second fight
segment, not boss completion or every phase. Other Mode 2 scenes have no
new widescreen policy. Windows remains unbuilt and unverified. These changes
remain uncommitted for the owner's verification.

## 2026-09-09 - Pothole Panic quick save remained pillarboxed

The new reported position is level `$0045`, Pothole Panic. The private
quick save has SHA-256
`466b9d5c1ca3058f5c5529e302febddf663ef3cc01899b1ef94b6b3215a7309a`.
The live app reproduced the black margins with AspectIndex 1 (16:10),
glide edges, and the user's reconstruction settings. Shape `$0470=1`
was deliberately unknown to the level-map decoder, leaving terrain
readiness false. Mode `$09`, BG1 map `$61`, stream destination `$6000`,
metatile base `$4000`, camera `$0145,$0288`, and rendered terrain phase
`$0145,$0287` identify the saved frame. The existing uncommitted work,
including the Bleak correction above, was preserved.

**Address contract.** H4v0c21 reference revision
`bed96892f5e85eabd5c920306f00b361c2e1f34c`, dispatch `$B7:F3C5/$B7:F401`,
selects builder `$B7:BE37-$B7:C067` for shape 1. The column builder at
`$B7:BE5D-$B7:BE76` doubles the masked horizontal offset and masks vertical
position to `$03E0`; the row builder advances `$0040` per metatile column
at `$B7:BFD5`. In metatile coordinates the map offset is therefore
`x * 64 + (y & 31) * 2`, with 16-bit address wrapping. Shapes 0/8 retain
their existing `x * 32 + (y & 15) * 2` formula. Only addresses and structure
are recorded; no disassembly source or game data was copied.

The new tall-horizontal layout shares the proven metatile definition,
flip, and bit-11 carry handling. Tile reads, metatile classification,
neighbour scans, and map extents use the same explicit column row count.
The prefill adapter skips row-major stride calibration for both
column-major layouts. No generated code, cfg contracts, guest simulation,
or runtime submodule source was changed for this correction.

**Verification.** An offline decode of the saved native BG1 ring matches
all 837 fully visible interior tile words (31 columns by 27 rows). Including
the partially visible top row gives 867/868: the one differing native cell
at world tile `(39,48)` is `$1010` where the source map decodes `$0000`.
Native captured cells remain authoritative. At the saved frame, the 4:3
image and WRAM/VRAM/CGRAM/OAM hashes are unchanged; all three wide aspects
retain its exact 256x224 native view after accounting for glide bias.

A reproducible 843-frame input replay loads the save, pulses Start, then
flaps Squawks while travelling right and left before releasing input.
Camera travel spans X 325..799 and Y 256..648. All 281 sampled 4:3 images
match the previous binary, with 280 distinct native frames. With
`DKC3_CULL_WIDEN=0`, all 281 aligned native views and final guest-memory
hashes match in 16:10, 16:9, and 21:9 as well. Terrain readiness stays true
throughout the restored wide replay, including vertical page transitions.

Default widescreen now also enables the already-audited object windows
once terrain is ready. Comparing those runs against the native windows
produces 31, 28, and 18 differing native samples at 16:10, 16:9, and 21:9,
respectively, and differing final guest-memory hashes. The inspected
differences at frames 311/362 are bomb/explosion animation changes with
those object windows enabled; the native-window comparison above isolates
the terrain fix. Earlier activation is the likely cause of that timing
difference, but a per-object activation trace was not collected.
These default runs are not claimed to be guest-state identical.

Eight previous saved scenes (five archived user states, KAOS, waterfall,
and Bleak) retain every final frame and guest-memory hash across 123-frame
replays in all four aspects: 32 comparisons, zero differences. All 25
CTest checks pass, including 20 ROM-free tests. Synthetic tests cover the
new shape selection, both halves of every 32-row column, vertical and
16-bit horizontal wrapping, flipped/carry-bearing definitions, and
neighbour counts that include the lower half of the map.

The packaged, signed canonical bundle passed a 90-frame hidden quick-load
check at the user's settings with a 3080x1920 drawable. Deep strict
signature verification passed. The old app was closed; the rebuilt
`build/macos/DKC3Recomp.app` launched as PID 62556 at 23:00:13 local,
after the 22:59:04 executable build. Quick Load restored the supplied
position, and the live 60 FPS window fills the 16:10 view with the cave.
The user's quick-save hash is unchanged. Private before/after binaries,
replays, dumps, reports, and captures are outside Git under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-cave-20260909`.

**Limits.** This verifies the reported save and a 14-second traversal,
not fresh level entry, full-level completion, or every shape-1 stage.
Windows remains unbuilt and unverified. Changes remain uncommitted for
the owner's verification.

## 2026-09-10 - combine the validated local changes with v0.0.5

The owner authorized committing and pushing all pending source changes.
Fetching `origin/main` found the newer Windows release commit `f2de72a`.
The local haptics, MSU-1 music, KAOS, waterfall, Bleak, and shape-1 cave
changes were rebased onto that release, preserving its Windows menus,
settings, version, tests, and documentation. The shared shutdown path
retains both native-menu teardown and haptic-worker shutdown. The shared
automated quick-load path guards the Mac-only replacement-music reset
with `__APPLE__`, because the Windows release moved that path outside
the Mac menu-command guard.

The combined macOS build passes all 26 CTest checks, including 21 ROM-free
tests and five private integration checks. The packaged canonical app
passes deep strict signature verification and a separate 90-frame hidden
quick-load run at the preserved Pothole Panic save and display settings.
Its complete 3080x1920 rendered image is pixel-identical to the validated
pre-integration app capture. Build, test, packaging, and app-smoke logs
are outside Git under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-push-20260910`.
No private inputs, generated sources, captures, or build products were
added to the commit. Earlier uncommitted-status notes record their state
at the time; this authorization supersedes those holds. The combined
changes have not been built or exercised on Windows in this local run.

## 2026-09-10 - investigate a report that LLE functions are too slow

The report supplied no CPU, OS, release, scene, settings, or measured frame
rate, so its particular slowdown remains unverified. The current generated
manifest lists 4,756 variants: 3,035 `aot_eligible` and 1,721 `lle_only`.
These are static coverage counts, not percentages of execution time.
`Dkc3RunOneFrame` enters the interpreter bridge, which calls compiled bodies
when a matching variant exists. The bridge preserves registers and guest
return frames across those transitions; hardware, graphics, and audio still
have runtime costs after a function is compiled.

A preliminary benchmark used the existing September 10 00:06 Release
headless binary on an Apple M3 Max, macOS 27.0 (26A428). At checkout
`750c081`, with snesrecomp `3b24b9d`, one 4,801-frame boot/attract run per
case completed at 4.496 seconds in 4:3 (0.936 ms/frame) and 5.035 seconds
in 21:9 (1.049 ms/frame). Those runs explicitly set
`SNESRECOMP_LLE_BOUNCE=1`, the runtime default. They include software PPU
rendering and synthesized audio, but exclude desktop presentation, GPU
effects, live audio-device contention, and player-driven/full-game coverage.
They do not reproduce a failure to sustain the approximately 16.64 ms game
frame budget on this machine. This was not a fresh rebuild or a Windows test.

The diagnostic `SNESRECOMP_LLE_BOUNCE=0` runs took 7.625 seconds in 4:3 and
8.173 seconds in 21:9, but reached different game states and final hashes.
They are not equivalent workloads, cannot establish a clean recompilation
speedup, and are not a validated alternative setting. The default runs also
logged `interp_cap` / `unresolved-abandon` at `$808CAD`, frame 58; their
successful host completion is not a new correctness qualification.

An eight-second macOS `sample` capture of a separate 30,000-frame default
21:9 run collected 6,523 main-thread samples; that run completed. It shows
compiled bodies executing inside the interpreter bridge, substantial
interpreter/bus work, and PPU/audio work. Environment lookup alone occupied
373 leaf samples (`__findenv_locked` plus `getenv`, about 5.7%). Sampled
stacks include `bridge_bus_write`, which checks `SNESRECOMP_APU_PORT_DIAG`
on each write before checking whether the address is an APU port. Caching
that diagnostic setting is an optimization candidate, not a tested fix or
an established cause of the user's reported slowdown. Inclusive bridge
samples must not be labeled interpreter-only time: they include compiled
bodies and hardware calls beneath the bridge.

Reproduce the throughput check by running
`DKC3_ASPECT=4:3 SNESRECOMP_LLE_BOUNCE=1 build/macos/dkc3_snesrecomp_headless /private/path/dkc3.sfc 4801`
with unrelated `DKC3_*` / `SNESRECOMP_*` overrides cleared; repeat at
`DKC3_ASPECT=21:9`. Time the process externally. Private stdout/stderr,
binary SHA-256, measurements, and CPU sample are outside Git under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-performance-20260910-1434`.
No runtime code was changed. The next useful evidence is the reporter's
hardware, OS, exact build, slow scene, display settings, actual FPS, and a
profile from that configuration.

## 2026-09-10 - measured runtime performance pass

The owner requested a performance optimization pass. A fresh Release
baseline was built from `750c081` before the runtime/build changes, and its
headless executable and native app were retained outside Git. Two changes
were measured separately:

- `cmake/Dkc3RuntimePerf.cmake` moves the APU-port/address predicate ahead
  of `getenv("SNESRECOMP_APU_PORT_DIAG")` in three bridge diagnostic checks.
  Ordinary memory accesses avoid the environment lookup. APU diagnostics
  retain their presence-based semantics, including empty and `0` values;
  there is no cached environment value. The build-directory adaptation
  requires each source anchor exactly once and preserves the pinned
  submodule, guest instruction execution, timing, and bus operations.
- Release game executables enable CMake interprocedural optimization after
  `check_ipo_supported` succeeds for C and C++. AppleClang selected ThinLTO,
  allowing optimization across the generated C/runtime boundary. The
  `DKC3_ENABLE_IPO` option defaults on; unsupported toolchains retain their
  ordinary Release flags, and an explicit `OFF` disables IPO. Debug builds
  do not receive the Release property. No fast-math flags were added.

On the same Apple M3 Max, a diagnostic-only build reduced median elapsed
time by 7.5% in 4:3 and 6.9% in 21:9. With both changes, a new paired test
gave the following medians across three 4,801-frame runs per configuration.
The before/after order alternates between repetitions; the processes run
serially with unrelated diagnostic overrides removed and no build running.

| Boot/attract aspect | Baseline | Optimized | Time reduction |
| --- | ---: | ---: | ---: |
| 4:3 | 4.306 s / 0.897 ms per frame | 3.760 s / 0.783 ms per frame | 12.7% |
| 21:9 | 4.776 s / 0.995 ms per frame | 4.263 s / 0.888 ms per frame | 10.7% |

All stdout fields, including frame/WRAM/VRAM/CGRAM/OAM hashes, audio hash,
audio activity, and state-event counts, are identical before/after each
aspect. A separate 2,403-frame Pothole Panic run using the bidirectional
Squawks replay, again three alternating pairs per aspect, improved from
1.798 to 1.573 seconds in 4:3 (12.5%) and 2.754 to 2.495 seconds in 21:9
(9.4%), with identical reported state and audio. These are headless
throughput measurements, not new desktop FPS limits.

Correctness comparisons cover nine private saved scenes in all four
aspects, plus the cave replay with `DKC3_CULL_WIDEN=0` in all four aspects:
40 before/after cases. The eight cave comparisons each sample 281 full
frames over an 843-frame replay; all 2,248 frame pairs are byte-identical.
All final state/audio fields and stderr diagnostics also match. Four
600-frame boot comparisons with `SNESRECOMP_APU_PORT_DIAG` absent, empty,
`0`, and `1` produce identical stdout and stderr, including the expected
trace output whenever the variable is present. Existing boot dispatch
diagnostics remain unchanged; this pass does not claim to fix those.

All 27 project CTest checks pass. A separate build configured with
`DKC3_BUILD_SNESRECOMP=OFF` and an empty ROM path passes all 21 available
tests. The new ROM-free adaptation test checks the exact transformation
and rejects both missing and duplicated anchors. The final bundle was
rebuilt, packaged with SDL2, and passed deep strict signature verification.
Its headless executable matches the one used in the comparisons.

The baseline and optimized native SDL apps each completed 240 frames from
the cave save in 4:3, 16:10, 16:9, and 21:9 with CoreAudio enabled and the
user's reconstruction settings. All four pairs of full presented captures
and final source-resolution frames are byte-identical. Those short paced
checks did not show a consistent timing improvement. A follow-up using
three alternating 600-frame pairs in 4:3, discarding the first 60 pacing
rows, reduced the median of each run's mean emulation/render time from
1.083 to 1.042 ms (3.8%); individual runs varied. This is a smaller and
noisier desktop result than the sustained headless throughput improvement.

Private baseline/candidate binaries, scripts (`benchmark.py`, `verify.py`,
`native_check.py`), timings, captures, build/test logs, and comparison
reports are outside Git under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-optimize-20260910`.
Windows/other compilers and full-game traversal are unverified. The source
changes were held uncommitted after validation; the owner subsequently
authorized committing and pushing this performance pass.

The final canonical app was launched as PID 94390 with the user's current
quick-save loaded. Its live window was inspected at 60 FPS with the Kongs,
ropes, enemies, terrain, and widened view visible; the normal MSU-1 pack
and CoreAudio path are active. The user's original quick-save SHA-256 is
unchanged. The inspected native window is retained outside Git as
`dkc3-optimize-20260910/live-window.png` in the diagnostics directory above.

## 2026-09-10 - second runtime performance pass: profile-led, byte-identical

The owner asked for a further optimization pass and for a comparison with
the other recompilation projects. Three inputs set the direction:

- A 10-second `sample` of the optimized 21:9 boot run from the previous
  pass had been attract-mode work. A new profile of the 30,000-frame
  Pothole Panic replay (`cave.sav` + `flaps.txt`, 16:9, the workload a
  player actually sees) split differently: 54% of headless time in
  `Dkc3DrawPpuFrame` (the scalar PPU: `PpuDrawBackground_4bpp`, the
  line composite inlined into `ppu_runLine`, and
  `PpuMergePaddedBackground` at 11% by itself), 31% in
  `Dkc3RunOneFrame` (interpreter, bus, compiled bodies), and 13% in the
  SPC/DSP catch-up.
- Reading the sibling projects. DKC1Recomp's measured pass (its
  snesrecomp fork `b46b940`) had removed a stack-balance hash probe from
  every generated-function return and a `strncmp` from every entry.
  Upstream snesrecomp `main` (not an ancestor of DKC3's pin, which branches
  at `fe6045c`) had added the active-span priority-buffer clear
  (`4020e47`) and made tier-2 coverage journals opt in (`8d12911`).
  zelda3's `ppu.c` is the origin of this renderer; its composite is the
  plain loop DKC3's world-span and HUD tests were added to.
- Two defects found in DKC3's own build: the hosts define
  `SNESRECOMP_TRACE=0`, but `dsp.c` and `dsp_shadow.c` test the macro
  with `defined()`, so the shadow mixer's reference renderer, the
  reference Gaussian per voice, the reference echo FIR and the reference
  BRR decode all ran on every DSP sample in release builds (the
  substitution stays disabled, so audio was unaffected, only time was
  lost); and `audio_trace_on_sample` read the monotonic clock for every
  sample (about 1.5% of the profile in `mach_absolute_time`).

### Mechanism

The two ad-hoc CMake string patches (`Dkc3Ppu.cmake`,
`Dkc3RuntimePerf.cmake`) are replaced by `cmake/runtime-patches/*.hunks`,
literal old/new replacements per pinned source, applied at configure
time by `scripts/apply_dkc3_runtime_patches.py` to copies under
`build/.../dkc3-runtime/`. Each anchor must match exactly the stated
number of times, so a moved pin fails the configure step instead of
dropping an adaptation. `cmake/Dkc3RuntimePatches.cmake` swaps the copies
into the runtime source list and exports `DKC3_PATCHED_PPU` for the Mode 2
unit test. `tests/test_apply_dkc3_runtime_patches.py` applies the set to
the pinned submodule, reverses every hunk to recover the original byte for
byte, and rejects missing, duplicated and malformed anchors. The submodule
pin is unchanged.

### Changes (nine runtime sources)

- `ppu.c`: the existing Mode 2 priority change; the upstream active-span
  clear and overlay merge; `PpuMergePaddedBackground` rewritten as three
  straight passes (authentic window as a restrict-qualified max-merge with
  the edge-repair columns as shifted runs, each margin as a running index
  that wraps at the period or reflects) with the original per-pixel
  formula retained as the fallback whenever a pass cannot prove its source
  columns stay inside the clamp range; the line composite skips the
  per-pixel world-span and HUD tests when no full-width exception is
  active on the line (columns outside the world span are zero-filled,
  inside are composited by the plain loop; the original loops remain for
  lines with an active exception and for an empty span); the 4bpp
  renderer computes `PpuViewportAllows` once per window span as a column
  interval and decodes each tile row through two 256-entry spread tables
  into eight nibbles, so a pixel is a shift, a mask and a nonzero test.
- `interp_bridge.c`: the previous APU diagnostic predicate reorders; the
  write-log state option resolved once instead of per bridge entry; the
  quiescent ring cleared only for auto-quiescent runs; the two poll
  prefetch bus reads per interpreted instruction skipped unless a yield-PC
  run can consult them (neither DKC3 entry point can: the frame scheduler
  is auto-quiescent and tier dispatch has no yield PC; the reads were
  untimed and never touched a device window); the tier-2 exit manifest
  gated on `SNESRECOMP_TIER2_CAPTURE`.
- `cpu_state.c`: `cpu_read8`/`cpu_read16` resolve an address that can only
  be cartridge ROM on a plain LoROM or HiROM cartridge directly (DKC3 is
  HiROM, map mode `$31`, with the save-RAM window left on the full route)
  instead of walking the register, coprocessor, save-RAM and
  `RomPtr`/`cart_getRomPtr` chain. Every other address and cartridge type
  is unchanged.
- `common_cpu_infra.c`: the write-log prefix compare is skipped unless
  `SNESRECOMP_WLOG` names an output (DKC1's change); the stack-balance
  auditor runs only with `SNESRECOMP_STACKBAL_AUDIT` set and its watchdog
  report says when it was off.
- `dsp.c`, `dsp_shadow.c`: `#if defined(SNESRECOMP_TRACE)` becomes
  `#if SNESRECOMP_TRACE`.
- `audio_trace.c`: the snapshot clock is polled every 64 samples.
- `apu.c`: SPC RAM below `$00F0` and the IPL window is answered before the
  register switch; the per-opcode PC histogram and per-write port counters,
  read only by the trace-build debug server, are compiled out of
  production.
- `tier2_capture.c`: the discovery journal requires
  `SNESRECOMP_TIER2_CAPTURE=1` (or `SNESRECOMP_TIER2=1`). Release runs no
  longer write `tier2_*.json`/`.jsonl` beside the working directory.

Not changed, after measurement or on review: the interpreter's per-step
quiescent ring compare and bus timing (correctness-load-bearing), the
world-shadow tile lookup per margin tile, the sprite evaluator, the SPC
timer loop, and a converted-palette cache for the composite (DKC1Recomp
measured that slower because of mid-frame palette writes).

### Evidence

Baseline is the committed `3877929` build (headless SHA-256
`ff1cf592…`), optimized is this tree (`920917b5…`). Every comparison
below is byte-for-byte against that baseline:

- 48 headless cases, all identical stdout (frame, WRAM, VRAM, CGRAM and
  OAM hashes, audio hash and activity, state-event counts) and stderr:
  eight private saved scenes in 4:3, 16:10, 16:9 and 21:9 for 123 frames;
  the cave replay in the four aspects with default and native
  (`DKC3_CULL_WIDEN=0`) culling, 843 frames with every third frame dumped
  and compared (2,248 frame pairs identical); boot runs of 600 frames in
  the four aspects; and `SNESRECOMP_APU_PORT_DIAG` absent, empty, `0` and
  `1`.
- The native SDL app, 240 paced frames from the cave save in the four
  aspects with CoreAudio active: the presented capture at frame 89 and
  the source-resolution frame are pixel-identical in all four.
- All 27 project CTest checks pass; the ROM-free configuration
  (`DKC3_BUILD_SNESRECOMP=OFF`) passes its 21.

Throughput, Apple M3 Max, macOS 27.0, three order-alternating pairs per
workload, medians, no other load:

| Workload | Baseline | Optimized | Time reduction | Speedup |
| --- | ---: | ---: | ---: | ---: |
| Cave replay, 843 frames, 16:9 | 0.805 s / 0.955 ms per frame | 0.508 s / 0.602 ms per frame | 36.9% | 58.5% |
| Cave replay, 843 frames, 21:9 | 0.938 s / 1.113 ms per frame | 0.592 s / 0.703 ms per frame | 36.8% | 58.3% |
| Boot/attract, 4,801 frames, 4:3 | 3.904 s / 0.813 ms per frame | 2.712 s / 0.565 ms per frame | 30.5% | 44.0% |
| Boot/attract, 4,801 frames, 21:9 | 4.491 s / 0.935 ms per frame | 3.060 s / 0.637 ms per frame | 31.9% | 46.8% |

The first round (everything except the spread-table decode, the
restrict-qualified merge and the SPC changes) measured 28.0% to 32.3%;
the second round added the rest. The native app's per-run mean emulation
time over 240 frames fell in every wide aspect (16:9: 1.23 to 0.69 ms;
21:9: 1.60 to 1.05 ms) but those are single short paced runs and are
noisy; the headless medians are the measurement.

The profile of the optimized cave replay now splits about evenly between
`Dkc3DrawPpuFrame` (50%) and `Dkc3RunOneFrame` plus audio (49%). The
largest remaining items are the 4bpp tile renderer (about 16%), the line
composite (about 10%, three table lookups per pixel), the interpreter
core loop (about 10%), the SPC/DSP (about 15%) and the world-shadow tile
lookup (about 3%). A no-inline attribution build used for this pass is
under `build/profile` (not shipped).

Scripts (`verify.py`, `bench.py`, `native_check.py`), build/test logs,
the four `sample` profiles, both headless binaries and the baseline app
are outside Git under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-optimize2-20260910`.
Windows and other compilers are unverified for this pass; the hunks are
plain C11 and the patch step needs Python 3 at configure time, which the
Windows instructions already require. The existing `tier2_*` files in the
repository root are ignored artifacts of earlier runs and can be deleted.

## 2026-09-10 - third pass: execution-tier accounting and the desktop present path

The owner asked what else could improve and whether the port is fully
static. Two measurements answer both.

### How much of the game runs compiled

The static manifest (`generated/snesrecomp/program_manifest.json`) holds
4,756 variants: 3,035 `aot_eligible` (66,255 instructions) and 1,721
`lle_only` (21,656 instructions), so 75% of the statically reachable
instructions have compiled bodies. At run time the picture is narrower.
The optimized cave-replay profile splits its leaf samples as: scalar PPU
and world shadow 48%, interpreter plus shared bus and dispatch 27%,
SPC/DSP 16%, DKC3 widescreen adapter 4%, and generated compiled bodies
0.3%. A diagnostic build (`tools/diagnostics/interp_pc_histogram.hunks`,
applied through the new `DKC3_RUNTIME_PATCH_DIR` override) counted the
interpreted instructions over the 843-frame replay: 1,447,604, or about
1,717 per frame, against a frame that executes several thousand. The
compiled bodies therefore carry most instructions cheaply, while the
interpreter's share of time comes from a small set of routines executed
at roughly 90 nanoseconds per instruction. The top of the histogram:

| Interpreted PC range | Share of interpreted instructions | Manifest disposition and reason |
| --- | ---: | --- |
| `$B7:CF68` (`CODE_B7CF68`) | 12.5% | `lle_only`: truncated call continuation, unproven callee exit at `$B7:CFC0` to `$B7:D078` |
| `$BB:A6B2` (`CODE_BBA6B2`) | 9.4% | `lle_only`: truncated call continuation, unproven callee exit at `$BB:A71B` to `$BB:8FDD` |
| `$B7:CE43` (`CODE_B7CE43`) | 6.4% | `lle_only`: truncated call continuation, unproven callee exit at `$B7:CE7D` to `$B7:CF68` |
| `$BB:B8CE`-`$BB:B8F7` loop | 13.0% | inside `CODE_BBB884`, but its compiled node stops at `$BB:B8A4`; no node covers the loop |
| `$B2:825E`, `$80:89CA`, `$00:80C4` | 8.8% | `aot_eligible` but reached by fall-through or as the frame handler itself, which the scheduler always interprets |

Promoting those four regions (a cfg contract change with disassembly
structure behind it, per the working agreement) is the largest remaining
CPU-side lever; it was not attempted here because it changes which tier
executes them and so cannot be held byte-identical by construction. The
earlier observation that `SNESRECOMP_LLE_BOUNCE=0` reaches different game
states makes that a correctness question first.

### Interpreter per-step cost

An auto-quiescent run compared every interpreted instruction against all
64 entries of the quiescent ring, loading each 56-byte entry. A parallel
array of the entries' program counters now rejects non-matching slots
from four cache lines; matching slots are checked with the original full
condition in the original order (`interp_bridge.hunks`,
`quiescent-ring-pc-*`). Same evidence as the previous pass, all identical:
48 headless cases (2,248 sampled frame pairs), the native app's presented
pixels in four aspects, 27 CTest checks. Medians of three alternating
pairs against the `3877929` baseline, cumulative for the day:

| Workload | Baseline | Now | Time reduction |
| --- | ---: | ---: | ---: |
| Cave replay, 16:9 | 0.957 ms per frame | 0.590 ms per frame | 38.3% |
| Cave replay, 21:9 | 1.092 ms per frame | 0.693 ms per frame | 36.5% |
| Boot/attract, 4:3 | 0.805 ms per frame | 0.551 ms per frame | 31.5% |
| Boot/attract, 21:9 | 0.919 ms per frame | 0.618 ms per frame | 32.7% |

The ring index itself is worth one to two percent; it is kept because
it is exact and the interpreter core loop remains about a tenth of the
profile.

### Desktop present path

The SDL host's pacing log (`DKC3_PACING_LOG`) over a visible 1,500-frame
16:9 run with the owner's settings (window scale 4, reconstruction
upscaler) shows emulation at a median 0.43 ms per frame and the present
call at a median 0.39 ms but a mean of 1.19 ms with a 95th percentile of
5.2 ms. Hidden-window runs measure the present call at 1.5 to 1.8 ms
mean whether the upscaler is nearest or reconstruction, so the shader is
not the cost. A `sample` of the visible run attributes it: of the main
thread's present samples, most sit in `SDL_GL_SwapWindow` ->
`CGLFlushDrawable` -> `SLSFlushSurfaceWithOptionsAndIndex` ->
`_CGSWindowIsOrderedIn` -> `mach_msg`, a synchronous WindowServer round
trip inside the legacy OpenGL swap on macOS 27. Frame intervals still
hold 16.667 ms at the 95th percentile on this machine, so nothing was
changed. The remedy that removes it is the one DKC1Recomp shipped
(`runner/macos_metal_presenter.m`): a `CAMetalLayer` presented from a
`CAMetalDisplayLink` thread through a three-slot frame queue, with the
emulation thread never calling into the window system. That port,
including a Metal version of the reconstruction shader, is the largest
remaining desktop-side lever and was not attempted in this pass.

Artifacts for this pass (verification and benchmark JSON, CTest and build
logs, the interpreted-PC histogram, the desktop `sample` and pacing logs
with both upscalers, the diagnostic hunk, and the round-3 headless binary,
SHA-256 `2626ddc8…`) are outside Git under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-optimize2-20260910/pass3`.

## 2026-09-10 - fourth pass: vector tile and sprite stores

The 4bpp background renderer and the sprite evaluator now store a full
eight-pixel row with one vector operation on NEON (Apple silicon) and
SSE2 (x86-64); other targets keep the scalar sequence
(`ppu.hunks`, `simd-*`). For a background tile the eight nibbles of the
spread-table decode become lanes, the opacity test, the priority compare
and the per-span viewport interval become lane masks, and a blend writes
the row; partial tiles at span edges stay scalar. For a sprite sliver the
same decode feeds an overwrite-where-opaque blend, exactly the scalar
"later, lower OAM indices overwrite" rule; partial slivers at the buffer
edges and overlay-captured slots stay scalar.

Both code paths were verified on the same 48-case corpus (2,248 sampled
frame pairs) against the `3877929` baseline: the arm64 build natively and
an x86-64 headless build (`-DCMAKE_OSX_ARCHITECTURES=x86_64`, built only
the headless target) under Rosetta, so the SSE2 path is exercised on real
x86 instructions rather than assumed. Zero mismatches in both. Medians of
three alternating pairs, cumulative for the day:

| Workload | Baseline | Now | Time reduction | Frames per second |
| --- | ---: | ---: | ---: | ---: |
| Cave replay, 16:9 | 0.950 ms per frame | 0.557 ms per frame | 41.4% | +70.7% |
| Cave replay, 21:9 | 1.098 ms per frame | 0.640 ms per frame | 41.7% | +71.7% |
| Boot/attract, 4:3 | 0.819 ms per frame | 0.540 ms per frame | 34.1% | +51.8% |
| Boot/attract, 21:9 | 0.933 ms per frame | 0.606 ms per frame | 35.0% | +54.0% |

Artifacts (both verification logs, the benchmark JSON, the x86-64
build log) are under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-optimize2-20260910/pass4`.
