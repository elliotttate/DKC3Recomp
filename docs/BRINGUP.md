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

## 2026-09-10 - fifth pass: exit-width contracts promote the hot interpreted routines

The interpreted-PC histogram of the third pass named four regions. The
recompiler's manifest explained each with `truncated_call_continuation`
and `unproven_callee_exit`: a `JSR`/`JSL` whose callee's exit width the
analysis could not derive stops the decode at the call, and a node that
stops at a call is never compiled (`snesrecomp/recompiler-rs/src/analyze.rs`,
lines 466-472 and 670-676 at the pinned revision). The cfg directive
`exit_mx_at <pc24> <m> <x>` supplies that width as a declared fact; it is
an assertion, so each one below names the disassembly structure behind it.

### Contracts

`tools/ingest_dkc3_disasm.py` now carries two tables, `EXIT_MX_AT` and
`FUNC_SPLITS`, and re-emits them after the automatic contracts, so a
re-ingest reproduces the committed cfg byte for byte (the previous hand
edit for `$BB:A6B2` from `bc2856e` is the first entry). Added, with the
structural facts from `H4v0c21/DKC3-Disassembly` (addresses and mnemonics
only):

- `exit_mx_at B7D078 0 0`: `$B7:D078` is one instruction, `JMP ($009E)`;
  callers stage a handler address from `DATA_B7D07B` into `$9E` first, and
  every handler returns to the caller's 16-bit `CMP.w #$001F` at
  `$B7:CFC3`. Promoted six nodes: `B7C942`, `B7CB2B`, `B7CC25`, `B7CD1F`,
  `B7CE43` and `B7CF68`.
- `exit_mx_at BB8FDD 0 0`: `$BB:8FDD` falls through its declared end into
  the init-script parser at `$BB:8FF1`, whose command dispatch at
  `$BB:9015` recurses through `$BB:8FF9`, so the exit equation never
  closes; the caller at `$BB:A71B` resumes with `AND #$0100` and
  `LDA #$0134`.
- `exit_mx_at BBA786/BBA7E7/BBA853/BBA85F 0 0`: the four placement handlers
  dispatched from `$BB:A778` through `DATA_BBA992` contain no `REP`/`SEP`,
  call `CODE_BB8F8B`/`CODE_BB8FC8`/`CODE_BB8F78` and the parser, and `RTS`
  into the caller's 16-bit loop (`AND #$3FFF`/`AND #$000F`/`ASL`/`TAX` at
  `$BB:A76C`-`$BB:A777`, `PLB`/`PLX`/`INX` at `$BB:A77B`). With them
  `BBA6B2` compiles to its declared end.
- `exit_mx_at B8806C 0 0` and the split `func sprite_handler_direct B8A5
  end:B9B0`: `$B8:806C` is a `JMP` stub into the interaction dispatcher at
  `$B8:8304` (`indirect_dispatch 8304 45 ptrtail`). A scan of all 45
  handlers found no `REP`/`SEP` and `RTL` exits in every one; two
  (`$B8:8A23`, `$B8:8978`) also leave through `JML [$001A]`, which is why
  the equation stays open. Both sprite-handler call sites (`$BB:B8F9`,
  `$BB:B983`) resume 16-bit (`ADC #$0063` at `$BB:B9A9`). The loop at
  `$BB:B8CE` belongs to `sprite_handler_direct` at `$BB:B8A5`, reached by
  `JMP` from `$BB:85C1`; the split gives it its own node.

Manifest: 3,035 `aot_eligible` / 1,721 `lle_only` before; 3,044 / 1,713
after (the sprite handler is a new node), 66,255 to 67,169 compiled
instructions. Regenerated with `scripts/generate_snesrecomp.py` (native
analyzer) after each table change; the ingester test and the cfg diff
were checked at each step.

### What changed at run time

The interpreted-PC histogram over the 843-frame cave replay fell from
1,447,604 instructions (1,717 per frame) to 1,104,759 (1,310 per frame):
`$B7:CF68` and its family, `$BB:A6B2`, and the `$BB:B8CE` loop's callers
now run compiled. Two promoted routines are still entered by the
interpreter: `CODE_B7CE43` (a handler address read from `DATA_B7B01B` at
`$B7:F293` and jumped to through RAM) and `sprite_handler_direct` (entered
by `JMP` from `$BB:85C1`). The bridge bounces only `JSR`/`JSL` arrivals into
compiled bodies, never `JMP` arrivals, because a jump target has no return
frame to host-return through; the frame handler at `$00:80C4` and the main
loop at `$80:89CA` (entered by `JMP` at `$80:8015`) are interpreted for the
same reason. Moving that trunk to compiled execution needs a bridge
contract for jump arrivals, not another cfg fact, and was not attempted.

### Evidence

Each generation was compared against the pre-promotion binary on the
48-case corpus. Game state is identical everywhere: every frame, WRAM,
VRAM, CGRAM and OAM hash, every sampled cave frame (2,248 pairs), every
stderr line. What changed is the `run_stats` audio line in some runs: the
Lakeside Limbo scene after the first contract (37,214 to 36,324 nonzero
samples over 123 frames, peak 6,920 to 6,923), and the boot runs after the
sprite-handler contract. A routine that moves from the interpreter to a
compiled body accounts its cycles by the compiled model, so the SPC catch-up
lands on different instructions and the audio samples shift by a few
positions; the interpreter-versus-compiled split already produces this
difference (`SNESRECOMP_LLE_BOUNCE=0` reaches different states), so it is
the expected tier-timing effect and not a state divergence. The cave
replay's audio hash is unchanged. All 27 CTest checks pass.

Headless throughput, medians of three alternating pairs against the day's
`3877929` baseline (the state hashes still match; the benchmark now ignores
the `run_stats` audio line for the reason above): cave replay 0.956 to
0.532 ms per frame in 16:9 (44.3% less time) and 1.107 to 0.624 ms in 21:9
(43.6%); boot/attract 0.819 to 0.534 ms in 4:3 (34.7%) and 0.935 to 0.604
ms in 21:9 (35.5%). Against the fourth pass alone the contracts are worth
about 4.5% in the cave replay.

Binaries, manifests and verification logs for each step are outside Git
under `/Users/briantate/Documents/Codex/diagnostics/dkc3-optimize2-20260910/pass5`.

## 2026-09-10 - sixth pass: Metal display-link presenter on macOS

The third pass attributed the desktop present call to a synchronous
WindowServer round trip inside the legacy OpenGL swap. DKC1Recomp had
already moved its presentation onto a `CAMetalLayer` driven by a
`CAMetalDisplayLink` thread; `runner/macos_metal_presenter.m` adapts that
design (and its Metal translation of the reconstruction upscaler) to DKC3.

### Design

- The presenter adds a `CAMetalLayer`-backed `NSView` above the SDL
  OpenGL view, with `displaySyncEnabled = NO` and three drawables, and
  runs a `CAMetalDisplayLink` on its own `NSQualityOfServiceUserInteractive`
  thread. Each callback first publishes a display tick with the fields the
  CADisplayLink path reports (sequence, host time, refresh target, and the
  interval between the link's refresh targets), then presents.
- The emulation thread keeps its existing pacing: `WaitForDisplayTick`
  and the pacing log now take ticks from the presenter when it is active
  (`DisplayTickLatest`/`DisplayTickWait` in `sdl_main.c`), so the refresh
  divisor lock and the 60.0988 Hz behavior are unchanged. After emulating
  it copies the frame into a three-slot mailbox (`QueueFrame`) and returns;
  the next callback uploads the newest frame into one of three rotating
  shared input textures (never one still read by an in-flight command
  buffer), draws a fitted quad through `dkc3_flat` (nearest or bilinear)
  or `dkc3_reconstruct`, and presents. A callback without a newer frame
  re-presents the current texture without re-uploading.
- The settings overlay is Dear ImGui on OpenGL. While it is open the host
  hides the Metal view and presents through the OpenGL path exactly as
  before, then unhides it when the overlay closes; `DKC3_DESKTOP_TEST_OVERLAY`
  exercises that transition. Hidden test windows never start the
  presenter, so the hidden-mode drawable captures and all desktop tests
  keep the OpenGL path. `DKC3_METAL_PRESENTER=0` keeps OpenGL for a visible
  window too. `DKC3_DESKTOP_SCREENSHOT` on a visible Metal window reads the
  presented drawable back through a blit into a shared texture.
- The frame loop is pinned to `QOS_CLASS_USER_INTERACTIVE`; this did not
  change the measurement below but is kept as the correct class for it.

### Evidence

Visible 16:9 window, the owner's settings (window scale 4, reconstruction
upscaler), cave save, 1,200 frames, rows after the first 90; the same
build with `DKC3_METAL_PRESENTER=0` beside it:

| | OpenGL swap | Metal presenter |
| --- | ---: | ---: |
| present call, mean / p95 / max | 2.229 / 7.680 / 10.93 ms | 0.018 / 0.031 / 0.10 ms |
| emulation, mean / p95 | 0.394 / 0.559 ms | 0.787 / 1.398 ms |
| tick interval p95 / max | 16.728 / 29.21 ms | 16.667 / 33.33 ms |
| display-locked frames | 1,065 of 1,110 | 1,089 of 1,110 |

The present call is the emulation thread's cost of handing over a frame;
the Metal path's memory copy replaces two milliseconds of window-system
time per frame and the 8 to 11 ms spikes. Emulation time reads higher on
the Metal path in every run; the thread now sleeps about 15.8 ms of each
refresh instead of 13.9 and wakes into a colder core, and the QoS pin did
not change that. It is still under 1.5 ms at the 95th percentile against a
16.7 ms budget. The presenter's counters for that run: 1,214 callbacks,
1,112 uploads, 100 repeats, 86 drops; the drops are the unpaced start
before the audio queue primes (presents closer than 8 ms apart occur only
in the first 14 frames and at one lock loss).

Pixel parity: `DKC3_DESKTOP_SCREENSHOT` at frame 89 of the same run through
both presenters, 3,416 x 1,920 drawables, reconstruction mode 3: 2,677 of
6,558,720 pixels differ (0.04%), 667 by more than 8 levels in any channel
(0.01%), all shader rounding at texel boundaries; the Metal reconstruction
shader is DKC1Recomp's line-for-line translation of DKC3's GLSL. The
overlay test mode completes with the presenter active. All 27 CTest checks
pass (the desktop tests run hidden and therefore through OpenGL).

Not verified: fullscreen toggling and window resizing under the Metal view
were exercised by hand only through the test harness's fixed window; the
view autoresizes with the window and the drawable size follows the backing
size, as in DKC1Recomp. Windows is unaffected (the presenter is
macOS-only). Artifacts (pacing logs and captures for both presenters, the
overlay test run) are under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-optimize2-20260910/pass6`.

## 2026-09-10 - seventh pass: jump-arrival bounce

The fifth pass left about 1,310 interpreted instructions per frame in the
cave replay, most of them in routines the interpreter reaches by `JMP`,
by a computed jump, or by resuming after an unwind rather than by
`JSR`/`JSL`. The bridge bounced only calls into compiled bodies, because a
body entered at a jump target has no return frame of its own to
host-return through.

### Contract

- The interpreter knows that frame whenever it pushed it. A call it could
  not bounce (no compiled variant for the live width state, or excluded by
  policy) and a bounce that unwound back inside the callee both leave the
  callee's return frame at a known S with a known size and continuation;
  the run records them (32 deep). A nested balanced run likewise sits on
  its compiled caller's frame: its exit watermark plus that caller's
  `hrv`, which `interp_tier_dispatch_balanced` now passes in. While S
  equals a recorded base and the interpreter stands at a compiled entry,
  the entry runs through `cpu_dispatch_pc_paired` with that frame size:
  its `RTS`/`RTL` host-returns through the frame it would have popped, and
  the interpreter resumes at the recorded continuation, or reports its own
  frame's run complete. S must match exactly; anything pushed since makes
  the arrival ineligible. Unwinds and `SKIP` results take the same paths
  as the call bounce. `SNESRECOMP_LLE_JUMP_BOUNCE=0` disables the contract.
- Cost control: a direct-mapped (pc, m, x) table remembers entries with no
  variant or excluded by policy, so the per-step cost is a compare; a
  permanent list retires entries whose bounce unwound back to the same PC,
  entries that tier down at their own entry, and entries that unwound
  twice in a row within 200 master cycles of entering. Step 0 of a run is
  never bounced (the run's creator chose that PC to interpret), and
  arrival bounces stop past a bridge depth of 12.

### What the bounce exposed

Entering bodies that had only ever run interpreted surfaced three
latent problems, each fixed before the pass was accepted:

1. **Table misses.** `dixie_kong_main` ends in `JMP (kong_state_table,x)`
   at `$B8:9F4D`. The table's rows are four bytes (handler, flags) and the
   analyzer resolved a single entry, so the compiled dispatch abandoned
   every other state (`[unresolved-abandon]`, handler skipped, frame
   popped) and the cave replay diverged within a frame. `cpu_unresolved_abandon_balanced`
   now re-interprets a `JMP (abs,X)` miss from the jump itself through
   `interp_tier_dispatch_tail`, the tier-down an unresolved indirect goto
   takes; the site has executed nothing past the operand fetch, so the
   state is the pre-jump state. Gated on the opcode: DKC3's pointer-call
   sites are `JSR (abs,X)`, whose continuation miss carries post-callee
   state and keeps the abandon. `SNESRECOMP_TIER2_VERBOSE=1` reports the
   first hit per site; `$B8:9F4D` is the only site hit in the corpus.
2. **Self-recursion.** `CODE_B7D078` is a lone `JMP ($009E)`; its compiled
   body tiers down to a nested run at its own entry, which the arrival
   check bounced again, without bound (host stack overflow in the first
   frame). Hence the step-0 rule and the retirement of entries that tier
   down at their own entry.
3. **A nested run that cannot yield.** Sprite mains such as `$B6:805A` end
   with `JML [$04F5]` back into the sprite loop. Bounced, the compiled
   body's unresolved `JML` started a nested balanced run that interpreted
   the rest of the frame's main loop, and a nested run does not yield at
   `WAI` or at the frame deadline: it spun 2,000,000 steps to the step cap
   and abandoned, first at frame 2,910 of the attract sequence. The
   600-frame boot cases never reach that phase and the final state after
   4,801 frames still matched; the benchmark showed it as a 2% boot
   regression. An arrival-bounced body's own unresolved jump (or that of
   a pure tail chain from it) is now handed back to the owning
   interpreter through the existing unwind sentinel, which is where the
   body ran before the bounce. Doing the same for call bounces is not
   identical: with the lever off, twelve WRAM bytes (in-flight counters
   at `$7E:00E6`/`$00E8`, `$072C`-`$0733`, `$0ACC`-`$0AF8`) differ at the
   end of the cave replay while every frame matches, because nested runs
   ignore the frame deadline and the owner honors it, so the CPU stops at
   a different phase. Call bounces keep nesting.

### Evidence

Interpreted instructions per frame, arrival bounce off / on, same build:
cave replay 1,310 / 984 (-25%), boot run of 4,801 frames 3,695 / 3,613.
The cave's arrival bounces complete 8.1 times per frame and unwind 3.1
times (the two Kong mains run 15 instructions before their table
dispatch; the sprite mains run to their `JML`). What remains interpreted
in the cave: the sprite loop `$BB:B8CE`-`$BB:B8F7` (about 300 per frame,
entered mid-body at `.sprite_return`), the echo wait at `$B2:8262` (55),
the `$B9:EEA0` family (about 90), and the `kong_state` handlers (about
120, `lle_only`).

Verification against the sixth-pass build: 50 cases (the 48 of the fifth
pass plus 4,801-frame boot runs in 4:3 and 16:9 sampled every 25 frames,
to cover the attract phase), state hashes, stderr and 2,632 sampled
frames identical; the `run_stats` audio line differs in 15 cases (the
tier-timing class accepted since the fifth pass; the largest shift is
scene 3, `audio_nonzero_samples` 35,584 to 33,694). All 27 CTest checks
pass, and the hidden-window desktop app's presented capture and
source-resolution frame at frame 89 of the cave save match the original
baseline app in all four aspects.

Paired, order-alternating throughput, three repeats, medians:

| workload | sixth pass | seventh pass | change |
| --- | ---: | ---: | ---: |
| cave 16:9, 843 frames | 0.517 ms/frame | 0.508 ms/frame | -1.7% |
| cave 21:9 | 0.609 | 0.598 | -1.8% |
| boot 4:3, 4,801 frames | 0.525 | 0.519 | -1.1% |
| boot 21:9 | 0.588 | 0.583 | -0.8% |

Every repeat moved in the same direction. The gain is smaller than the
instruction count suggests because the arrival check, the bounce and the
unwind have their own costs, and the largest remaining interpreted block,
the sprite loop, is entered mid-body and therefore not an entry.

Follow-ups, not done: a `func` split at `.sprite_return` (`$BB:B8EC`)
would let the loop run compiled between sprite mains; an
`indirect_dispatch 9F4D 117 ptrtail` contract for `kong_state_table`
(the ingest tool has to accept the `%offset()` row that precedes it)
plus exit contracts for `$B8:CC29`, `$B9:A595` and `$B9:A009` would
compile the Kong state handlers; and the call-bounce nesting phase
difference above is worth a pass of its own. Artifacts (verification and
benchmark JSON, histograms, arrival outcomes, the step trace of cave run
300, the diagnostic hunks) are under
`/Users/briantate/Documents/Codex/diagnostics/dkc3-optimize2-20260910/pass7`.

## 2026-09-12 - Windows fullscreen menu bar, configure on Windows, DPI

The same report as DKC1Recomp v0.0.11: the native Game/View bar stayed
drawn across the top of the borderless fullscreen window. A Win32 menu bar
is non-client area, so SDL's `FULLSCREEN_DESKTOP` popup kept it and lost
its height from the GL drawable. `HostSetFullscreen` in `sdl_main.c` now
owns every fullscreen change (menu item, Escape, the Mac command): the bar
is detached after the borderless window appears and reattached before SDL
restores the windowed frame, so SDL's remembered windowed size is never
measured without the bar. A saved `Fullscreen=1` creates the window
fullscreen before the menu exists, so the host hides the bar right after
`Dkc3WindowsMenuCreate`; that function also restores the client size the
bar had taken from the freshly created window. `windows_menu` asserts the
retained client size, detach/restore, command routing while detached and
the retained fullscreen label.

Two things stood in the way on this Windows machine. The 2026-09-10 hunk
manifest was written with native separators, so
`dkc3_use_patched_runtime_sources` could not find `apu.c` in the
forward-slash runtime list and configure failed; the manifest now uses
POSIX paths. And with `SkipLauncher=1` the game skipped the launcher's
DPI hints and started DPI-unaware, bitmap-scaled by Windows (a 960x720
request read back as 2160x1620 at 225%, fullscreen 3456x2169); `RunGame`
sets the same two hints before its own `SDL_Init`.

Live check (private ROM, isolated staging directory, 3456x2170 display):
windowed 960x720 with the bar; View > Toggle Full Screen and the saved
fullscreen preference both gave `GetMenu()` NULL and a `WS_POPUP` client
equal to the monitor; the menu item and Escape restored the bar and
960x720. All 28 CTest checks pass. No cartridge, widescreen or Mac change.

## 2026-11-21 - widescreen enemy spawn and sprite-cull lead-in (16:9 Doorstop Dash)

- **Symptom:** in 16:9 widescreen (specifically reported in Doorstop Dash,
  level `$0028`), enemies and doors could still blink onto the screen past
  the widened edge instead of walking or scrolling smoothly across it.
- **Cause:** Doorstop Dash places all 118 objects through the standard
  `$BB:A647` / `$BB:AAD4` / `$B7:8857` pipeline (90 placements on
  `DATA_BBA8BE` row `$00` with `32 px` native slack, and 28 door/gear
  placements on row `$10` with `16 px` native slack). The existing
  widescreen helpers (`Dkc3VideoExpandCullLeft` / `Dkc3VideoExpandCullSpan`)
  widened those windows by only the exact presented margin (`extra ± bias`)
  with no extra lead-in. Because newly activated sprites require 2+ frames
  for VRAM tile DMA upload inside the sprite renderer at `$B7:8857`
  (`CODE_B78857`), which queues uploads at `$B7:90AA` (`CODE_B790AA`), and
  `$B7:8925` culls sprites before reaching `CODE_B790AA`, fast sprint/roll
  scrolling plus sprite half-widths consumed the 16-32 px slack before tiles
  finished uploading. In addition, the interpreter-patched cull at `$B7:A88F`
  (`Dkc3PatchInterpreterCull`) previously widened symmetrically without
  `presentation_bias`.
- **Change:**
  - Added configurable widescreen spawn-ahead helpers in
    `runner/dkc3_video.h` and `runner/dkc3_video.c`:
    `Dkc3VideoSetSpawnLead`, `Dkc3VideoSpawnLead`,
    `Dkc3VideoSpriteCullLead`, `Dkc3VideoExpandSpawnLeft`,
    `Dkc3VideoExpandSpawnSpan`, `Dkc3VideoExpandSpriteCullLeft`, and
    `Dkc3VideoExpandSpriteCullSpan` (default `48 px` per side, overridable
    via `DKC3_SPAWN_LEAD`, clamped to `[0, 64]`).
  - Expanded `Dkc3VideoPlacementScanCells` and `Dkc3PlacementScanBegin`
    (`runner/dkc3_game.c`) to scan up to two 256-pixel neighbor buckets per
    side (`kDkc3VideoPlacementScanMaxCells = 5`) whenever the worst-case
    widened reach (`2 * extra + lead + 32 px` asymmetric row slack) can
    cross a second 256-pixel cell boundary (e.g., 21:9 with spawn lead).
  - Placement lead applies only to `DATA_BBA8BE` rows with `Lx < $60` and
    `Wx < $1C0` (rows `$00`, `$10`, `$30`, `$40`, `$70`), preserving 8-pixel
    spawn/despawn hysteresis while skipping already-wide rows (`$20`, `$50`,
    `$60`) so `camera_x - Lx_widened` cannot underflow 16-bit unsigned
    subtraction near `camera_x = $0100`.
  - Sprite renderer cull lead is clamped to `160 - 2 * extra` (`108 px` cap
    at 16:10, `74 px` cap at 16:9, `0 px` at 21:9) so off-screen sprites
    can queue VRAM tile DMA early without wrapping nine-bit OAM X onto the
    opposite margin.
  - Wired `$BB:AAD4` to `Dkc3VideoExpandSpawnLeft/Span` and the nine `$B7`
    sprite renderer cull sites to `Dkc3VideoExpandSpriteCullLeft/Span` in
    `scripts/apply_dkc3_widescreen_overrides.py`, leaving `$B6:F186` and
    the static banana/star grid renderer (`$B7:E8E0` / `$B7:E981`) on the
    exact `Dkc3VideoExpandCull*` helpers. Updated `Dkc3PatchInterpreterCull`
    in `runner/dkc3_game.c` to use `Dkc3VideoExpandSpriteCullLeft/Span`
    after per-frame bias and terrain-readiness updates.
  - Native 4:3, unproven terrain frames, and `DKC3_CULL_WIDEN=0` keep zero
    lead and native windows.
- **Unit tests (ROM-free):** `python -m unittest discover -s tests` (22/22,
  including updated `test_apply_dkc3_widescreen_overrides.py`).
- **Unverified:** this widened window change remains unverified until a
  paired `DKC3_CULL_WIDEN=0` vs `1` A/B comparison (and 4:3 hash-parity
  check) is captured and recorded; an informal live Windows Release check
  in 16:9 Doorstop Dash observed reduced enemy edge pop-in, without a
  paired `DKC3_CULL_WIDEN=0` baseline capture. Also unverified: C/CTest
  (`test_dkc3_video`), macOS rebuild, and full-game traversal across 16:10,
  16:9, and 21:9. Regenerating `generated/snesrecomp/` from a ROM is
  required to pick up the updated generated-code wrappers.

## 2026-11-21 - Pot Hole Panic / Tyrant Twin Tussle black screen, crash, and save corruption

- **Symptom:** in Pot Hole Panic (level `$0045`) and Tyrant Twin Tussle
  (level `$004B`), the window turned black (`INIDISP=$8F`), the game
  eventually crashed with an off-rails runtime failure, and closing the
  window during the black-screen hang could corrupt `saves/save.srm`.
- **Root cause:** both levels contain the only two `PLA; JMP $F27D`
  non-local child-sprite despawn sites in bank `$B6` (`CODE_B6B22E` at
  `$B6:B234` in Pot Hole Panic and `CODE_B69D33` at `$B6:9D3F` in Tyrant
  Twin Tussle). When a parent sprite despawns before its child sprite,
  the interpreted sprite handler (`$B6:B228` / `$B6:9D19`) executes a
  `JSR` that call-bounces into the compiled helper with `entry_s = $01FA,
  hrv = 2`. The helper executes `PLA` (discarding its own 2-byte `JSR`
  return frame so `cpu->S = $01FC > entry_s`), tail-calls `$B6:F27D`
  (deleting the sprite via `$BB:8591`), and executes `$B6:F281: JML [$04F5]`
  (sprite-loop continuation), which enters `interp_tier_dispatch_balanced`
  with `cpu->S` already strictly shallower than `entry_s`.
  - Previously, `interp_tier_dispatch_balanced` started a nested
    `interp_bridge_run_ex2` anchored at the stale `s_exit = entry_s`
    (`$01FA`). When the child was the highest active sprite slot (as
    reproduced deterministically at host frame 759 / `snes_frame_counter=57800`
    in 16:9 Pot Hole Panic with `SNESRECOMP_LLE_JUMP_BOUNCE=1` or `0`),
    the nested run finished the sprite loop and executed `$BB:B9AF: RTL`,
    popping the outer interpreter's `JSL $BB:85BB` frame (`$01FC -> $01FF`),
    exited with `ok = 1`, got `cpu_resolve_post_return_skip($01FF) == -1`
    (outer caller was the interpreter, not compiled `g_recomp_stack`), and
    returned `RECOMP_RETURN_NORMAL`. The owning interpreter then resumed at
    `$B6:B22A` with empty stack `S = $01FF`, executed `$BB:B9AF: RTL` a
    second time, popped garbage `$A0:78C3` from `$0200..$0202`, ran
    off-rails inside the HiROM SRAM window (`$A0:6000-$7FFF`) and WRAM
    with `INIDISP=$8F` for ~388 frames (`frame 800..1187`), and finally
    tripped `off-rails runtime failure at host frame 1188 resume=$8034b3`
    (`rc=6`). Closing the black window before `g_fail` triggered caused
    shutdown to persist the corrupted `g_sram` buffer to `saves/save.srm`.
  - Even when a higher active sprite slot existed (observed on slot 7 with
    slot 8 active), `$BB:B8EB`'s `PHA; PHA; RTL` trampoline for slot 8
    landed at `in.sp = $01FC > s_exit ($01FA)`, causing the nested run to
    exit prematurely, silently skipping slot 8 and resuming `$B6:B22A` on
    the deleted sprite.
- **Change (`cmake/runtime-patches/interp_bridge.hunks`):**
  - In `interp_tier_dispatch_balanced` (`@@ balanced-goto-unwind-to-owner`),
    when `cpu->S` is already strictly shallower than `entry_s`
    (`pre_s_delta != 0 && pre_s_delta < 0x8000u`), unwind straight back to
    the owning interpreter at `target_pc24 & 0xFFFFFFu` with live `cpu->S`
    via `interp_bridge_lle_yield_unwind` whenever
    `s_interp_bounce_owner_depth > 0`, `!cpu_interrupt_context_active()`,
    and `cpu->S` has not crossed above `s_interp_owner_exit_s`.
  - When falling through to `interp_bridge_run_ex2` (`@@ arrival-bounce-balanced-hrv`)
    with `cpu->S` already shallower than `entry_s` (e.g., no owning
    interpreter or `S` crossed above a nested interpreter's exit watermark),
    anchor the nested run at `run_s_exit = cpu->S` with `s_run_hrv = 0`
    rather than the stale `entry_s`.
  - Added defense in depth in `interp_tier_dispatch_balanced`'s epilogue
    (`@@ balanced-overpop-unwind-to-owner`): capture `post_return_pc` via
    `interp_bridge_run_ex2` and, if a nested run returns `ok` with `cpu->S`
    strictly above `expected_post_s` and no compiled ancestor matches
    (`cpu_resolve_post_return_skip <= 0`), unwind to the owning interpreter
    at `post_return_pc` instead of returning `RECOMP_RETURN_NORMAL`.
  - Added ROM-free unit test `test_balanced_dispatch_unwinds_nonlocal_stack_pops`
    in `tests/test_apply_dkc3_runtime_patches.py`.
- **Verified (Windows x64 Release, private ROM):**
  - ROM-free Python tests on this branch: `python -m unittest discover -s tests` (23/23).
  - Full Windows Release build and 28/28 CTest suite (including
    `supplied_rom_headless_boot` and all four Windows SDL aspect smoke
    tests: 4:3, 16:10, 16:9, 21:9), plus Pot Hole Panic (2,000 frames,
    all four aspects, `DKC3_CULL_WIDEN=0`, `SNESRECOMP_LLE_JUMP_BOUNCE=0`,
    and `[ibr]` trace confirming clean `yield-unwind -> $B6F281 sp=$01FC`
    on both crash and slot-skip cases), Tyrant Twin Tussle (2,000 frames
    across all four aspects), and 600-frame boot runs (`4:3`, `16:9`) all
    exited `rc=0` with `inidisp=$0f`.
- **Unverified:** clean rebuild of `build-windows` directly on this
  branch without the sibling widescreen-lead changes, live triggering of
  the `$B6:9D3F` despawn branch inside Tyrant Twin Tussle (proven
  statically to execute the identical `PLA; JMP $F27D -> $B6:F281`
  sequence as `$B6:B234`), full playthrough of both stages to the
  flagpole, and macOS rebuild.

## 2026-11-21 - Poisonous Pipeline (`$003F`) — PPU window logic, widescreen window alignment, and Koco wall-exit investigation

- **Reported symptoms:**
  1. The pink water-surface pattern rendered far below the actual water
     surface (and inverted over the rock walls at water shafts).
  2. Fish ("odd fish movements") swimming into/through solid walls.
- **Root cause 1 — Water surface patterns (three distinct bugs, all fixed):**
  - **Missing `WBGLOG`/`WOBJLOG` combine logic in `PpuWindows_CalcWithExtra`
    (`snesrecomp/runner/src/snes/ppu.c:605`):** Poisonous Pipeline renders
    its pink water surface on BG3 (`bgXsc[2]=$70`, 32x32 tiles wrapping
    vertically every 256 px) with `W34SEL=$0A` (W1 and W2 enabled,
    non-inverted), `WBGLOG=$30` (`XNOR` for BG3), `TMW=TSW=$04`,
    `W2=[255, 0]` (always empty), and `W1=[shaft_left, shaft_right]` at
    water shafts or `W1=[255, 0]` deeper underwater. `PpuWindows_CalcWithExtra`
    ignored `ppu->wbgobjlog` and hardcoded `win->bits = w1_bits | w2_bits`
    (`OR`). Deep underwater (`W1=W2=[255,0]`), `OR(false, false) = false`
    left BG3 unmasked everywhere, exposing its 256 px / 512 px vertical
    wrap on the pipe floor; at water shafts, `OR(in_w1, false) = in_w1`
    inverted the mask, hiding the surface inside the shaft and drawing it
    over the surrounding walls.
  - **Unshifted hardware windows under `wsPresentationXBias`
    (`PpuWindows_CalcWithExtra`):** Near level walls (e.g. Poisonous
    Pipeline's entrance shaft with `bias=+36`), `Dkc3DrawPpuFrame` shifts
    BG `hScroll` and OBJ presentation left by `bias` pixels, but
    `PpuWindows_CalcWithExtra` left non-pinned hardware window edges at
    native screen X, misaligning the water-surface window by `bias` pixels.
  - **Wall-extent BG3 clear overriding windowed 32-column BG3
    (`runner/dkc3_game.c`):** `Dkc3VideoMarginLeavesAuthoredExtent(...)`
    cleared bit `0x04` of `physical_wide_mask` *after* ORing
    `Dkc3VideoPhysicalWindowedLayerMask(...)` instead of only clearing the
    64-column BG3 ring bit from `Dkc3VideoPhysicalWideLayerMask(...)`.
    Near the west wall, that forced Poisonous Pipeline's windowed 32-column
    BG3 into `wsLayerRepeat`, wrapping the entrance-shaft water surface
    into the opposite margin.
- **Root cause 2 — Fish swimming through walls (original SNES behavior +
  widescreen exposure; no recompiler/CPU bug):**
  - Decoded all 109 placements of `$003F` (`$FE:4CEE` via `$FF:0040 + type`)
    against the decompressed 96x80 metatile map (`$7F:0000`). All 40 fish
    in the stage are Kocos (`$B6:8220`, `sid=$0300`): 12 patrolling Kocos
    (`w_5a=$07 -> $B9:E1DB`, all patrol ranges inside open pipe chambers)
    and 28 straight-swimming green Kocos (`w_5a=$08 -> $B9:E20D`:
    `JSR $EE6E; JSR $E0EC; JMP $E183`, constant horizontal speed with
    vertical bobbing and **no terrain/wall collision in the SNES ROM**).
  - Audited `koco_main_idle` (already executes in authoritative LLE),
    `recomp/bankb9.cfg`, and generated bank `$B9` movement functions, and
    verified identical Koco `(x, y)` coordinates in 4:3 AOT vs 4:3 LLE
    (`SNESRECOMP_LLE_BOUNCE=0`).
  - Even on authentic 4:3 LLE, straight-swimming Koco `#7` (`$5C0, $240`,
    `type=$02B8`) swims straight into the sloped ceiling if the player
    pauses to watch. In 16:9 / 21:9, widened side margins and widened
    `$BB:AAD4` activation/despawn (`+43 px` in 16:9, `+95 px` in 21:9)
    spawn straight-swimming Kocos earlier and keep them visible further
    past the native 256 px edges, visibly exposing their straight-line
    wall exits. Per the cartridge correctness oracle, Koco physics were
    left authentic.
- **Changes:**
  - `cmake/runtime-patches/ppu.hunks`: added fail-closed hunks
    `@@ window-combine-logic` (honors `(ppu->wbgobjlog >> (layer * 2)) & 3u`
    for OR/AND/XOR/XNOR across all 6 window layers and masks unused upper
    bits by `(1u << nr) - 1u`, matching `ppu_legacy.c`) and
    `@@ window-presentation-bias` (shifts non-pinned edges of non-empty
    W1/W2 windows by `-ppu->wsPresentationXBias`, clamped to
    `[win->edges[0], window_right - 1]`; exact no-op when bias is 0).
  - `runner/dkc3_game.c`: moved the `Dkc3VideoMarginLeavesAuthoredExtent`
    BG3-ring bit clear before ORing `Dkc3VideoPhysicalWindowedLayerMask`.
  - `tests/test_dkc3_ppu_windows.c`, `CMakeLists.txt`, and
    `tests/test_apply_dkc3_runtime_patches.py`: added 247 ROM-free PPU
    window cases (216 BG3 + 24 OBJ/Color Window oracle comparisons against
    `ppu_legacy.c` and 7 widescreen presentation-X-bias checks) plus
    runtime-patch hunk assertions.
- **Verified (Windows x64 Release, private ROM):**
  - `python -m unittest discover -s tests`: 23/23 passed on this branch.
  - Full Windows Release build and 29/29 CTest suite (including
    `dkc3_ppu_windows`, `dkc3_ppu_mode2`, `supplied_rom_headless_boot`,
    and all four Windows SDL aspect smoke tests: 4:3, 16:10, 16:9, 21:9).
  - Poisonous Pipeline (`$003F`, 1,310-frame swim replay through entrance
    shaft, deep underwater, and second shaft):
    - **4:3 fast PPU vs `DKC3_PPU_LEGACY=1`:** 107/107 sampled frames
      (frames 240–1300 step 10) are **byte-for-byte identical (0 diffs)**;
      `audio_fnv1a=451a775a3b3b4f8f`.
    - **16:9 and 21:9 visual inspection:** entrance shaft (`bias=+36`),
      deep underwater (frames 700, 850), right-margin shaft entry (frames
      1015–1070), and second shaft (frames 1150, 1300) all render with
      aligned water surfaces, zero wrapped patterns deep underwater, and
      zero opposite-margin repeat leaks.
- **Unverified:** full playthrough of Poisonous Pipeline to the flagpole
  (including the second sub-room), Murky Mill / Floodlit Fish live visual
  regression captures, and macOS rebuild.

## 2026-11-21 - missing bonus stars near the left wall in widescreen (issue #4)

- **Symptom:** in Doorstop Dash's first bonus round ("Collect the Stars!",
  60 stars, level `$52`), one star was missing and uncollectible in 16:9
  widescreen, while 4:3 completed normally.
- **Cause:** DKC3's "Collect the Stars!" bonus games store their stars in
  the same `$7E:6780` static tile-row list as banana formations, initialized
  by `$B7:E4B7` and walked every frame by `$B7:E8E0`. For each active group,
  `$B7:E8E0` calls both the renderer (`$B7:E981`) and the collector
  (`$B7:EB69`). The walk computes `$2C = camera_x + right` (`$B7:E8E4`,
  native `$0100`), subtracts `$2C` from the group's left X (`$B7:E905`),
  adds the group's width (`$B7:E909`), and adds back a span constant
  (`$B7:E90D`, native `$010F`), skipping both drawing and collection on
  `BMI` (`$B7:E910`). Because `$B7:E8E4` widens `$0100` with
  `Dkc3VideoExpandCullRight` (`+ extra + bias`), `$B7:E90D` must widen
  `$010F` by both margins (`Dkc3VideoExpandCullSpan`, `+ 2 * extra`) so the
  left cutoff stays at `camera_x - ($0F + extra - bias)`, matching
  `$B7:E9C8`. `scripts/apply_dkc3_widescreen_overrides.py` instead wrapped
  `0x10f` in `Dkc3VideoExpandCullLeft` (`+ extra - bias`), so `extra`
  canceled out and the left cutoff moved to `camera_x - 15 + 2 * bias`:
  with `bias = 0` the left margin was never widened, and near a left wall
  with positive glide/shift bias (`+43` in 16:9, `+95` in 21:9) the cutoff
  moved inside the native viewport (up to `camera_x + 71` in 16:9 and
  `camera_x + 175` in 21:9). In Doorstop Dash Bonus 1 (map shape 4), entry 0
  is a single-star group spanning world X `$0148..$0150` (`328..336`), so
  near the left wall in 16:9 its right edge falls below the shifted cutoff
  and it is skipped for both drawing and collection.
- **Fix:** `adapt_grid_renderer` in
  `scripts/apply_dkc3_widescreen_overrides.py` now wraps `0x10f` in block
  `0xB7E909` with `Dkc3VideoExpandCullSpan`. Because
  `Dkc3VideoExpandCullSpan(0x10f) - Dkc3VideoExpandCullRight(0x100)` equals
  `Dkc3VideoExpandCullLeft(0x0f)` for every aspect ratio, presentation bias,
  terrain-ready state, and `DKC3_CULL_WIDEN=0`, the walk and draw bounds
  agree everywhere and 4:3 stays native. Updated
  `tests/test_apply_dkc3_widescreen_overrides.py` and added the
  `Span(0x10f) - Right(0x100) == Left(0x0f)` invariant checks across
  positive, zero, and negative presentation bias in `tests/test_dkc3_video.c`.
- **Verified:** `tests/test_apply_dkc3_widescreen_overrides.py` (4/4),
  ROM-backed Windows Release regeneration and build, all 28 CTest checks
  (including `dkc3_video`'s new `Span(0x10f) - Right(0x100) == Left(0x0f)`
  checks), and live in-game verification in Doorstop Dash Bonus Level 1
  ("Collect the Stars!", 60 stars) in widescreen confirming the previously
  missing star now appears and is collectible.
- **Unverified:** macOS rebuild, headless 4:3/wide hash-parity capture of
  Doorstop Dash Bonus 1 with `DKC3_CULL_WIDEN=0` vs `1`, and traversal of
  every other "Collect the Stars!" bonus level across 16:10, 16:9, and 21:9.
  Regenerating generated C from a ROM is required to pick up the override
  change in a compiled binary.

## 2026-11-21 - Windows Alt+Tab CPU spike and desktop hang mitigation

- **Symptom:** Alt+Tabbing away from the Windows game (`DKC3RecompSDL.exe`,
  configured with `Fullscreen=1`, `Upscaler=2` Reconstruct, OpenGL VSync on
  Windows 11 26200 / NVIDIA 621.24) spiked CPU and could hang the computer.
- **Root causes found in the Windows desktop hosts:**
  1. **No focus-loss / minimize pause or swap suppression
     (`runner/sdl_main.c`, `runner/desktop_main.c`):** `SDL_WINDOW_FULLSCREEN_DESKTOP`
     does not minimize on focus loss in SDL2, and neither desktop host handled
     `SDL_WINDOWEVENT_FOCUS_LOST`, `SDL_WINDOWEVENT_MINIMIZED`, or unfocused
     states. Even though `ReadControls` already blocked all gameplay input
     when unfocused, the main loop kept running full emulation, full-monitor
     21-tap Reconstruct OpenGL shaders, `glFlush()`, `SDL_GL_SwapWindow()`
     (`wglSwapIntervalEXT(1)`), and per-second `SetWindowTextW` updates right
     through Alt+Tab DWM / NVIDIA Direct-Flip transitions and while
     backgrounded.
  2. **Busy-spin inside `PaceFrame()` (`runner/sdl_main.c`) and `WaitUntil()`
     (`runner/desktop_main.c`):** Whenever `SDL_GL_SwapWindow` returned early
     (for example when VSync detached on focus loss/minimization, on >60 Hz
     displays, or in hidden mode), `PaceFrame()` spun in a tight
     `SDL_Delay(0); PumpEvents(host);` loop for the last 1–2 ms of every frame,
     calling `WIN_PumpEvents` and `SDL_JoystickUpdate` (HID/WGI/RawInput/XInput)
     repeatedly at 100% CPU. In `desktop_main.c`, `WaitUntil()` similarly spun
     when `remaining < 1 ms` and never reanchored `deadline` after stalls,
     causing an unthrottled catch-up burst after returning from Alt+Tab or
     window drags.
  3. **`SDL_SYSWMEVENT` queue flooding (`runner/windows_menu.c`):** Enabling
     `SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE)` solely to observe menu
     `WM_COMMAND` messages caused SDL2 to wrap and enqueue every raw Win32
     message sent during Alt+Tab and DWM focus transitions.
- **Changes:**
  - `runner/desktop_pacer.h` / `runner/desktop_pacer.c` /
    `tests/test_desktop_pacer.c`: added unit-tested background-pause and
    paused-repaint policy helpers (`Dkc3DesktopShouldPauseInBackground`,
    `Dkc3DesktopShouldRepaintWhenPaused`).
  - `runner/sdl_main.c`:
    - Pauses emulation, pauses/resets SDL audio (unified with overlay pause),
      and stops OpenGL presentation while minimized/hidden or (on Windows)
      unfocused (`DKC3_RUN_IN_BACKGROUND=1` opts out of focus-loss pause while
      still pausing when minimized). Windowed paused instances repaint once on
      demand for expose/resize; minimized windows never call
      `SDL_GL_SwapWindow`, and unfocused fullscreen windows do not unless
      `DKC3_RUN_IN_BACKGROUND=1`.
    - Sleeps in `MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, ...)`
      while background-paused on Windows (with an anti-spin guard) instead of
      spinning or polling joysticks (`SDL_WaitEventTimeout` falls back to
      polling when a joystick is attached), and reanchors frame deadline and
      FPS counter on focus return.
    - Replaced `PaceFrame()`'s `SDL_Delay(0)` busy-spin on Windows with a
      high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`)
      waited via `MsgWaitForMultipleObjectsEx` (with an anti-spin fallback to
      `WaitForSingleObject`), exiting `PaceFrame` immediately if focus is lost.
  - `runner/desktop_present_sdl.c`: skips GL work immediately when
    `SDL_WINDOW_MINIMIZED` is set and drops the redundant `glFlush()` before
    `SDL_GL_SwapWindow()`.
  - `runner/windows_menu.c` / `tests/test_windows_menu.c`: routes menu
    `WM_COMMAND` from the existing `MenuProc` subclass via a dedicated
    `SDL_RegisterEvents(1)` user event without enabling `SDL_SYSWMEVENT`
    (keeping `SDL_SYSWMEVENT` only as a fallback if event registration fails).
  - `runner/desktop_main.c` / `CMakeLists.txt`: applied the same background
    pause, high-resolution waitable timer, `deadline` stall reanchoring, and
    minimized/unfocused-fullscreen `WM_PAINT` guard to the Win32 host.
- **Verified (Windows x64 Release, private ROM):**
  - Clean MSVC `/W4` Release rebuild of `DKC3RecompSDL.exe`, `DKC3Recomp.exe`,
    `test_desktop_pacer.exe`, and `test_windows_menu.exe` (0 warnings in
    modified files).
  - Full Windows CTest suite: 29/29 passed (including updated `desktop_pacer`,
    `windows_menu` asserting `SDL_SYSWMEVENT` matches the selected routing mode,
    and all four Windows SDL aspect smoke tests).
  - Hidden-mode CPU measurement (`DKC3_DESKTOP_TEST_HIDDEN=1`, 16:9
    Reconstruct): before fix, 300 frames took `1.359 s` total CPU
    (`0.953 s` user, `0.406 s` kernel in `5.475 s` wall); after fix, 360 frames
    (20% more frames) took `0.844 s` total CPU (`0.688 s` user, `0.156 s`
    kernel in `6.329 s` wall, `6.006 s` active runtime for 360 frames).
- **Unverified:** Interactive live Alt+Tab in fullscreen on the user's desktop
  (automated tests run hidden without driving the live Alt+Tab switcher) and
  macOS rebuild.

## 2026-11-21 - Fix widescreen margin distortion during fast movement in HDMA-scrolled horizontal levels

- **Symptom:** Moving rapidly in widescreen (for example, charging on Enguarde
  in Tidal Trouble, Lake Orangatanga level 3, `$00C0 = $0027`, shape 0
  horizontal map) visibly distorted both side margins for the duration of the
  high-speed dash.
- **Root cause:**
  1. In Tidal Trouble, HDMA channel 7 (`$210D`, mode 3) writes `BG1HOFS` and
     `BG2HOFS` (terrain layer `BG2`, `BG2SC = $79`, 64x32 tilemap) on every
     scanline starting at line 1 (flat above water, 3-value wavy ripple
     underwater). Because HDMA overwrites `BG2HOFS` from line 1, the
     frame-start `g_ppu->hScroll[1]` register holds the previous frame's last
     scanline value. During Enguarde's 7–8 px/frame charge, `|band_h - frame_h|`
     exceeds `kDkc3VideoTerrainPhaseLeadX` (6 px).
  2. `Dkc3VideoSelectTerrainPhase()` (`runner/dkc3_video.c`) was designed to
     adopt the rendered HDMA band scroll in this case, but failed on 32-row
     horizontal levels for two reasons:
     - Its vertical follow check compared `dominant_v` against
       `camera_y & 0x03ff` modulo 1024 only. In 32-row horizontal levels
       staged one 256-pixel page above `camera_y` (e.g., `camera_y = $0218`,
       `v_scroll = 23`), the 10-bit distance is 511 (> `kDkc3VideoTerrainPhaseFollowY = 32`),
       so `Dkc3VideoSelectTerrainPhase()` always returned `false`, leaving
       `s_terrain_phase_h` stuck on the stale frame-start register and causing
       `Dkc3ClassifyBands()` to drop 50–69 of 68–69 terrain scanline bands to
       `kDkc3BandPolicyRepeat` (`R`) instead of `kDkc3BandPolicyWorld` (`W`).
     - Its dominant-scroll fallback counted only exact `(h, v)` line matches
       against the `>= 112` half-screen threshold, which can fail when a
       full-screen underwater wavy HDMA ripple spreads 224 lines across 3+
       adjacent horizontal scroll values.
     - Its early-return guard kept `frame_h, frame_v` whenever it lay within
       lead distance of `(camera_h, camera_v)` even when HDMA bands rendered
       more than 6 px away on the opposite side of the camera.
- **Changes:**
  - `runner/dkc3_video.c`: updated `Dkc3VideoSelectTerrainPhase()` to:
    - accept an 8-bit (256-pixel) vertical phase match against `camera_y & 0xff`
      in the dominant-scroll fallback when `dominant_v` also follows `frame_v`
      within `kDkc3VideoTerrainPhaseFollowY` (including 255 -> 0 wrap when both
      `< 0x0100`), preserving rejection of far parallax scrolls on 64-row
      vertical maps;
    - count phase-compatible lines (`Dkc3VideoScrollAtTerrainPhase`) for the
      half-screen majority gate, breaking ties by exact-line count so wavy
      underwater bands pick their dominant scroll;
    - only take the early return when the frame-start register already covers
      at least half the rendered lines (or there are no HDMA bands).
  - `tests/test_dkc3_video.c`: added ROM-free unit tests covering 32-row
    horizontal fast-charge HDMA phase selection, full-screen 3-value wavy
    underwater ripples, 32-row vertical wrap (`255 -> 0`), and stale
    frame-start register replacement when within camera lead.
- **Verified (Windows x64 Release, private ROM):**
  - Rebuilt Release targets cleanly via `cmake --build build-windows --config Release`.
  - CTest: 29/29 passed (`dkc3_video`, `supplied_rom_headless_boot`, and all
    four Windows SDL aspect smoke tests).
  - Python unit tests (`python -m unittest discover -s tests`): 24/24 passed.
  - Headless Tidal Trouble Enguarde charge replay (`dkc3_snesrecomp_headless.exe`
    from Lake Orangatanga node 4 savestate into Tidal Trouble, 2,320 frames):
    - Before fix (16:9, frames 2270–2278): 9 frames dropped 50/68–69/69 terrain
      bands to `R` (`kDkc3BandPolicyRepeat`), distorting 40.2% of the left
      margin (3,871/9,632 px) and 6.9% of the right margin (666/9,632 px) at
      frame 2275.
    - After fix: `0` terrain `R`-band frames across the entire run in 16:10,
      16:9, and 21:9 (frames 2270–2278: `68/68` and `69/69` `W` bands, `0` `R`
      bands). At frame 2275 in 16:9, `terrain_ready=1`, `margin_matching=465/465`,
      and the native 256x224 center window is bit-identical (`0` pixels changed)
      to the pre-fix 16:9 render.
- **Unverified:** macOS rebuild and live interactive desktop playtest in Tidal
  Trouble.

## 2026-11-21 - Fix general audio distortion (DSP ring overflow and starvation)

- **Symptom:** Audio distorted / buzzed during launch (~5–7 seconds in, right
  as the title screen music starts) and after savestate loads / rewinds.
- **Root causes in the pinned `snesrecomp` APU synchronization path:**
  1. **Double-counted SPC cycles in `interp_bridge.c`:**
     `interp_bridge_use_absolute_apu_timeline()` only suppressed the legacy
     relative `bridge_apu_flush()` catch-up for SA-1 cartridges. Because DKC3
     is not SA-1, `interp_bridge.c` ran `bridge_apu_flush()` (`snes_catchupApu`)
     *on top of* `RtlRunFrame()`'s absolute guest-cycle timeline
     (`rtl_sync_apu_to_cpu_locked()`, `apu_schedulePortWrite()`, and
     `rtl_sync_apu_frame_boundary()`). During interpreter-heavy phases such as
     SPC music uploads (frames 60–120 and 378–397), the SPC ran ahead of real
     time, overflowing the 8,192-sample DSP ring (before fix on a 600-frame SDL
     boot with `SNESRECOMP_AUDIO_STATS=1`: `8,719` dropped samples, `2,807`
     audible dropped across `29` drop runs, `hiwater=8192`). Once the upload
     finished, `apu_runToGuestCycle()` saw `portClock >= target` and stalled
     for ~20 frames until guest time caught up, draining the ring dry and
     starving `rtl_render_native()` for 6 consecutive frames (`underflows=10`,
     400 zero samples per 533-sample frame at frames 412–417, producing a 60 Hz
     chopping buzz).
  2. **Unanchored guest timeline and stale port queue after reset / savestate
     load / rewind (`common_rtl.c`):**
     - `apu_saveload()` intentionally does not serialize the host port queue or
       guest-to-SPC anchor, yet neither `RtlLoadSnapshot()` nor
       `RtlLoadSnapshotFromMemory()` called `apu_clearPortQueue()`. Across a
       `snes_frame_counter` jump (savestate load or rewind), stale anchors
       caused either repeated `[apu] frame-boundary guest-clock sync timed out`
       stalls (forward jump) or silent starvation (backward jump).
     - Whenever `apu->portTimeValid` was `false` (after reset or
       `apu_clearPortQueue()`), `apu_runToGuestCycle()` returned immediately
       without advancing the SPC until the next CPU write to `$2140-$217F`. On
       `saves/dkc3s0.sav`, 596 of 598 frames starved with 445 zero samples per
       533-sample frame.
- **Changes:**
  - `cmake/runtime-patches/interp_bridge.hunks`: added
    `@@ apu-flush-use-frame-timeline` and `@@ apu-loop-use-frame-timeline` so
    `interp_bridge.c` defers to `rtl_apu_frame_timeline_active()` instead of
    also running `bridge_apu_flush()` relative catch-up (DKC3 enforces a
    per-frame master-cycle deadline in `Dkc3RunOneFrame()`, so the absolute
    timeline advances monotonically on every frame).
  - `cmake/runtime-patches/common_rtl.hunks` (new):
    - `@@ apu-anchor-frame-timeline`: anchors `portGuestAnchor` /
      `portTargetAnchor` at frame start in `RtlRunFrame()` whenever
      `!g_snes->apu->portTimeValid`, so `rtl_sync_apu_frame_boundary()` always
      advances the SPC by exactly 17,088 cycles (534 DSP samples) per frame.
    - `@@ apu-clear-port-queue-on-file-load` and
      `@@ apu-clear-port-queue-on-memory-load`: clear the host APU port queue
      and mapping on successful `RtlLoadSnapshot()` and
      `RtlLoadSnapshotFromMemory()` calls under `RtlApuLock()`.
  - `tests/test_apply_dkc3_runtime_patches.py`: added
    `test_apu_timeline_uses_single_clock_and_clears_port_queue_on_load`.
- **Verified (Windows x64 Release, private ROM):**
  - Rebuilt `dkc3_snesrecomp_headless.exe`, `DKC3RecompSDL.exe`, and
    `DKC3Recomp.exe` cleanly.
  - Python unit tests (`python -m unittest discover -s tests`): 25/25 passed.
  - Full Windows Release CTest suite: 29/29 passed (including
    `supplied_rom_headless_boot`, `dkc3_runtime_patches`, and all four Windows
    SDL aspect smoke tests exercising overlay, rewind, fast-forward, and
    save/load).
  - 600-frame SDL hidden boot (`SNESRECOMP_AUDIO_STATS=1`): `dropped=0` (was
    `8,719`), `dropped_audible=0` (was `2,807`), `drop_runs=0` (was `29`),
    `underflows=0` (was `10`), `hiwater=2188` (was `8192`).
  - 1,800-frame headless boot PCM: all six starvation chops at frames 412–417
    eliminated (only the 4 genuine game silences remain across 30 seconds).
  - `saves/dkc3s0.sav` 600-frame headless replay: 0 starvation runs across all
    598 active frames (down from 596), 0 APU sync timeouts, and all five
    guest-state hashes (`frame`, `wram`, `vram`, `cgram`, `oam`) unchanged.
  - Live interactive Windows desktop audio playback verified by the user.
- **Unverified:** macOS rebuild.

## 2026-11-22 - Fix black screen and crash after beating the first boss and collecting the token

- **Symptom:** After beating the first boss (Belcha's Barn) and collecting the
  token, the screen went black for ~63 frames and the runtime stopped with
  `FATAL: native runtime stopped unexpectedly` at frame 4633
  (`resume_pc = $80ffc0`; diagnostic bundle
  `diagnostic_bundle_20261122_143241_11584`).
- **Root cause:**
  1. The diagnostic bundle's `dispatch_log` ended abruptly at frame 4569 with
     the boss-victory sequence: `CODE_B898AB` -> `CODE_B89C4C` ->
     `CODE_B8F39B` (unresolved indirect jump at `$B8:F3C0` via
     `interp_tier_dispatch_balanced`) -> `CODE_B8F5B9` -> `CODE_B2800F`
     (music transition) -> three consecutive calls to
     `upload_spc_engine_write_spc_command_a` (`$B2:825E`), ending at
     `$B2:825D` (`i=237044`) and `$B2:825E` (`i=237045`), followed by 0
     compiled dispatches across frames 4570–4632.
  2. Inside `interp_tier_dispatch_balanced` (a nested `interp_bridge_run_ex2`
     run with `auto_quiescent == 0`, `yield_pc == 0`), `_interp_run_core()`
     only checked `s_lle_master_deadline` when `auto_quiescent` was true.
     When `upload_spc_engine_write_spc_command_a` (`$B2:825E`: `SEP #$10;
     LDX $06; CPX $2140; BNE $8262; ...; REP #$10; RTS`) spun waiting for the
     SPC700 past the end of frame 4569 (`RTL_MASTER_CYCLES_PER_FRAME`), the
     nested interpreter did not yield the frame.
  3. With `bridge_apu_flush()` deferred to `RtlRunFrame()`'s absolute guest
     timeline (`0152975`), `rtl_apu_guest_cycle()` clamped `within` at
     `RTL_MASTER_CYCLES_PER_FRAME - 1` while `snes_frame_counter` stayed
     frozen at 4569. The SPC never advanced further, so `$B2:8262` spun to the
     interpreter step cap and returned `ok = 0`.
  4. `interp_tier_dispatch_balanced()` then fell back to
     `cpu_unresolved_abandon_balanced()`, abandoning the routine mid-handshake
     with `x_flag = 1` (8-bit index registers from `SEP #$10` at `$B2:825E`,
     before `REP #$10` at `$B2:8270`) and `DB = $B2`. With `x_flag = 1`, no
     `M0X0` compiled entries matched on frames 4570–4632, the game corrupted
     into a black screen, and 63 frames later went off-rails to `$80:FFC0`.
- **Changes:**
  - `cmake/runtime-patches/interp_bridge.hunks`: added
    `@@ nested-deadline-owner-static`, `@@ nested-deadline-owner-depth`, and
    `@@ nested-deadline-unwind` so `interp_bridge_run_ex2()` records the
    bridge depth of the enclosing auto-quiescent scheduler frame
    (`s_lle_sched_owner_depth`, set only when `yield_pc == 0xFFFFFFFE`), and
    any nested interpreter run that is not in interrupt context, is not a
    `stop_on_rti` run, and has a scheduler owner frame below it, and that
    reaches `s_lle_master_deadline`, cleanly unwinds to that scheduler
    frame at `pc_before` via the existing `s_lle_unwind_active` mechanism. The
    top-level scheduler frame then yields the frame cleanly, `RtlRunFrame()`
    advances the SPC across the frame boundary, and the next frame resumes at
    `pc_before` with exact guest registers and stack.
  - `tests/test_apply_dkc3_runtime_patches.py`: added
    `test_nested_interp_unwinds_to_scheduler_on_master_deadline`.
- **Verified (Windows x64 Release, private ROM):**
  - Reconfigured and rebuilt `dkc3_snesrecomp_headless.exe`,
    `DKC3RecompSDL.exe`, and `DKC3Recomp.exe` cleanly.
  - Python unit tests (`python -m unittest discover -s tests`): 26/26 passed.
  - Full Windows Release CTest suite: 29/29 passed (including
    `supplied_rom_headless_boot`, `dkc3_runtime_patches`, and all four Windows
    SDL aspect smoke tests).
  - 600-frame SDL hidden boot (`SNESRECOMP_AUDIO_STATS=1`): `dropped=0`,
    `dropped_audible=0`, `drop_runs=0`, `underflows=0`, `hiwater=2201`.
  - 600-frame headless replay on `saves/dkc3s0.sav` and 300-frame headless
    replay on `build-windows/Release/saves/dkc3s0.sav`: both completed cleanly
    with 0 APU timeouts.
  - Live interactive Windows desktop verification by the user beating the first
    boss (Belcha's Barn) and collecting the token with no black screen or crash.
- **Unverified:** macOS rebuild.

## 2026-11-22 - Skip opening logo and bouncing/developer credit scenes with Start

- **Feature:** Allow pressing the **Start** button on cold boot to skip DKC3's
  opening Rare bouncing/rolling log (`$B2:8751`, State 0), bouncing/developer
  credit intro (`$B2:8B2C`, State 1), and copyright screen (`$B2:8EE5`,
  State 3) directly to the Title Screen (`$B2:8E05`, State 2).
- **Cartridge mechanism (verified from bank `$80` and `$B2` disassembly):**
  - On cold boot `$80:82B4` initializes `$06AB = $0000`, causing both
    `$B2:8788` (State 0) and `$B2:8B76` (State 1) to skip controller polling.
    After the first attract-mode loop completes State 3 (`$B2:8F28`), the
    cartridge sets bits 0 and 2 of `$06AB` (`$0005`), enabling its own button
    check (`$04CE & $9080`), fade-out (`$B2:8603`), and direct transition to
    State 2 (Title Screen via `$B2:850E`).
  - State 3 (`$B2:8EE5`) polls buttons but waits until scene frame timer
    `$00F4 >= $005A` (90 frames) before calling `$B2:8603`.
  - When skipping State 0/1 before State 3 has polled `$4016` (`JOYSER0`) 16
    times, `snesrecomp`'s `joypad1Index` is still 0 (whereas real hardware's
    VBlank auto-joypad read shifts all 16 bits, leaving `$4016` bit 0 = 1),
    causing `$80:8B49` to discard `$04CA`/`$04CE`. Advancing `joypad1Index` to
    16 under `autoJoyRead` when the skip triggers lets the cartridge's own
    first poll succeed cleanly.
- **Changes:**
  - `runner/dkc3_intro_skip.h`, `runner/dkc3_intro_skip.c`: added
    `Dkc3IntroSkipEnabledFromEnv()` (enabled by default; `DKC3_SKIP_INTRO_START=0`
    disables) and `Dkc3ApplyIntroStartSkip()`, which sets `$06AB |= $0005` in
    States 0/1 and advances `$00F4` to `$005A` in State 3 only when Start is
    pressed and no fade is active (`$04ED == 0` and `($00BC & 1) == 0`).
  - `runner/dkc3_game.c`: called `Dkc3ApplyIntroStartSkip()` in
    `Dkc3RunOneFrame()` before NMI dispatch and synced `joypad1Index = 16` when
    triggered under `autoJoyRead`.
  - `tests/test_dkc3_intro_skip.c` and `CMakeLists.txt`: wired into all three
    game targets (`dkc3_snesrecomp_headless`, `dkc3_snesrecomp_sdl`,
    `dkc3_snesrecomp_desktop`) and added ROM-free unit test `dkc3_intro_skip`.
  - `README.md`: documented the Start-button intro skip and env override.
- **Verified (Windows x64 Release, private ROM):**
  - Rebuilt `dkc3_snesrecomp_headless.exe`, `DKC3RecompSDL.exe`,
    `DKC3Recomp.exe`, and `test_dkc3_intro_skip.exe` cleanly.
  - Full Windows Release CTest suite: 30/30 passed (including `dkc3_intro_skip`,
    `supplied_rom_headless_boot`, and all four Windows SDL aspect smoke tests).
  - Headless ROM verification (7 scripted scenarios):
    - 900-frame no-input boot: frame and WRAM hashes bit-identical to pre-change
      baseline (`2dbea76e20ae...` / `f05d976d802c...`).
    - Start at frame 200 (State 0) and Start held from frame 1 through fade-in:
      both fade out cleanly and reach Title Screen (`$B2:8E05`) by frame 350.
    - Start at frame 500 (State 1): reaches Title Screen (`$B2:8E05`) by frame 650.
    - Start at frame 1140 (State 3): reaches Title Screen (`$B2:8E05`) by frame 1260.
    - Start during State 0 followed by Start on Title Screen: enters File Select
      (`$80:9586`) cleanly.
    - `DKC3_SKIP_INTRO_START=0` with Start at frame 200: stays in State 0
      (`$B2:8751`, `$06AB=$0000`).
  - Live interactive Windows desktop playtest verified by the user.
- **Unverified:** macOS rebuild.

## 2026-11-22 - Barrel Shield Bust-Up (`$0030`) — black side background from edge-parked PPU windows

- **Symptom:** In Barrel Shield Bust-Up (World 2 Level 1, `$0030`, map shape 4),
  the forest background (`BG2`/`BG3`) was blacked out on the right side in
  widescreen (16:10, 16:9, 21:9) at the level start, and on the left side once
  scrolling into the second hollow tree trunk.
- **Root cause:** Barrel Shield Bust-Up uses both SNES hardware windows (`W1`
  and `W2`, `windowsel=$00a00aa0`, `wbgobjlog=$0000` OR, `main_window=$06`
  masking 32-column `BG2` and `BG3` plus Color Window) to hide the forest
  background behind the transparent arch corners of up to two hollow tree
  trunks. When a trunk is off-screen, the cartridge clamps both of its 8-bit
  window edges to the screen boundary, parking the unused window at the
  1-pixel degenerate sentinel `[255, 255]` (off the right) or `[0, 0]` (off
  the left). In widescreen:
  1. `PpuWidescreenAdjustPinnedWindowEdges()` (`snesrecomp/runner/src/snes/ppu.h`)
     checked `*wNl == 0` and `*wNr == 255` independently, expanding `[255, 255]`
     to `[255, window_right - 1]` and `[0, 0]` to `[screen_left, 0]`, turning
     the 1-pixel parked sentinel into a full-margin black bar.
  2. `@@ window-presentation-bias` (`cmake/runtime-patches/ppu.hunks`) then
     shifted the unpinned endpoint by `-wsPresentationXBias` (e.g. `bias=+33`
     at the start shifted `w2l` to `222`, masking `[222, 298]` in 16:9 and
     `[222, 350]` in 21:9; `bias=-31` at the second trunk shifted `w1r` to
     `+31`, masking `[-43, 31]`), extending the black bar into the native
     viewport.
- **Changes:**
  - `cmake/runtime-patches/ppu.hunks` (`@@ window-presentation-bias`): when
    widescreen margins are active (`win->edges[0] < 0 || window_right > 256`),
    treat 1-pixel edge-parked sentinels (`[0, 0]` and `[255, 255]`) as empty
    (`wl = 1, wr = 0`) and skip presentation-bias shifting for them. Exact
    no-op at native 4:3 (`win->edges[0] == 0`, `window_right == 256`,
    `wsPresentationXBias == 0`).
  - `tests/test_dkc3_ppu_windows.c`: added `{4, 211, 255, 255}` and
    `{0, 0, 105, 255}` to the 4:3 `ppu_legacy.c` oracle (now 288 BG3 cases)
    and added 4 widescreen masked-window cases covering `[255, 255]` and
    `[0, 0]` at zero, positive, and negative presentation bias (11 widescreen
    window cases total).
  - `tests/test_apply_dkc3_runtime_patches.py`: asserted `w1_parked` /
    `w2_parked` in `test_ppu_windows_honors_combine_logic_and_presentation_bias`.
- **Verified (Windows x64 Release, private ROM):**
  - Reconfigured and rebuilt `dkc3_snesrecomp_headless.exe`,
    `DKC3RecompSDL.exe`, `DKC3Recomp.exe`, and `test_dkc3_ppu_windows.exe`
    cleanly.
  - Python unit tests (`python -m unittest discover -s tests`): 26/26 passed.
  - Full Windows Release CTest suite: 30/30 passed (including `dkc3_ppu_windows`
    with 288 BG3 oracle + 24 OBJ/Color Window oracle + 11 widescreen
    presentation-X-bias/parked-window cases, `dkc3_runtime_patches`,
    `supplied_rom_headless_boot`, and all four Windows SDL aspect smoke tests).
  - Barrel Shield Bust-Up (`$0030`) headless replay from the start savestate
    through the first branch into the second tree trunk:
    - 4:3 fast PPU vs `DKC3_PPU_LEGACY=1` at frame 5 (`w1=[4,211], w2=[255,255]`)
      and frame 230 (`w1=[0,0], w2=[20,227]`): byte-identical (`0` diffs).
    - 16:10, 16:9, and 21:9 visual inspection at frames 5, 100, 145, and 230:
      continuous forest canopy on both sides with zero right- or left-side
      black bars and zero 1-pixel vertical seams, while active hollow trunk
      masks stay aligned.
  - Live interactive Windows desktop playtest verified by the user.
- **Unverified:** full playthrough of Barrel Shield Bust-Up and other
  hollow-trunk stages to the flagpole, and macOS rebuild.
- **Scope decision (review follow-up, 2026-11-22):** the parked-sentinel rule
  applies to every stage, not only Barrel Shield Bust-Up. A 1-pixel window
  at `[0, 0]` or `[255, 255]` is what the cartridge's edge clamp produces for
  an off-screen mask, and in a wide aspect neither alternative is faithful:
  pin-expanding it paints a full-margin bar, and keeping its single native
  column leaves a 1-pixel seam inside the picture, because native column 0
  or 255 is no longer the screen edge. Gating by stage ID would miss other
  levels that use the same clamp. The cost is that one native edge column
  loses its window in wide aspects; 4:3 is unaffected. No other stage has
  been surveyed for a legitimate 1-pixel edge window.

## 2026-11-22 - Squeals on Wheels (world 2, level 3) - Sneek drawn with the fire palette in widescreen

- **Symptom:** in the release quick save (`saves/dkc3s0.sav`, SHA-256
  `7542f1de...c0a`, made in a wide aspect), the Sneek in its wheel on the left is drawn
  in red/yellow fire colours instead of its own purple/grey.
- **Measurement (16:9, after load):** the OBJ palette allocator's eight
  hardware slots at `$073C` hold IDs `$0F,$01,$02,$14,$26,$00,$1D,$47`; sprite
  slot 20 (type `$0398`, the Sneek) has OAM attribute `$2060`, palette 0, and
  `$1D4B` (last failed request) is `$0058`. Four Buzzes, the barrel, both
  Kongs, and the Sneek were all alive in the widened window when `$0058` was
  requested, so `$BB:95DF` raised exception `$20` and `$BB:95E6` fell back to
  palette 0, which holds the common effect palette `$0F`. The allocator never
  retries, so the sprite keeps palette 0 after slot 5 later frees.
- **Root cause:** a consequence of widened object activation, not a runtime
  defect: more concurrently live objects than the hardware's eight OBJ palettes.
- **Changes (commit `68ad82c` plus this entry):**
  - `scripts/apply_dkc3_widescreen_overrides.py` (`adapt_palette_overflow`):
    after `cpu_trace_block(cpu, 0xBB95E6)` in `request_palette_direct_M0X0`,
    call `Dkc3RecordSpritePaletteOverflow(cpu->D)`, which records the failed
    ID (`$04EF`) against the sprite slot at DP `$76`. Fails closed if the
    anchor moves. No guest state is written; it returns early at 4:3.
  - `runner/dkc3_game.c` (`Dkc3SyncWidescreenObjPalettes`, every frame before
    the PPU draws): drops entries whose slot despawned or received a real
    palette, recovers a save that already captured the fallback (`$1D4B` not
    in `$073C` and not tracked, attached to the active palette-0 dynamic sprite
    whose tiles are in OAM), loads the 15 colours from the ROM table at
    `$3D3201` (or the WRAM tinted copy when `$0775` bit 15 is set), and tags
    palette-0 OAM entries in the slot's tile range.
  - `cmake/runtime-patches/ppu.hunks`: a host-only extended OBJ CGRAM table (24
    palettes) and per-line OAM tag buffer; `PpuLookupPixelColor` serves tagged
    OBJ pixels (layers 4/6) from it in the main, color-math, and HUD-margin
    composite paths. The SIMD sprite fast path is skipped only while a tag is
    active. Inactive (plain `cgram` lookup) at 4:3 and whenever every sprite
    fits the hardware palettes.
  - `@@ composite-ext-obj-fallback-sub` now anchors on the
    `math_enabled_cur & 0x100` line: its two-line replacement also appeared in
    the main composite path, so reversing hunks found it twice and
    `test_pinned_sources_apply_and_change_only_where_stated` failed.
  - `tests/test_apply_dkc3_widescreen_overrides.py`: a palette allocator
    fixture (the suite failed without one), hook placement assertions, and a
    fail-closed test for a moved `$BB:95E6` anchor.
  - `DKC3_EXT_PAL_TRACE=1` prints the tracked slot/palette pairs every frame.
- **Verified (Windows x64 Release, private ROM):**
  - Python unit tests 27/27; full Release CTest 30/30.
  - Release quick save, headless, 400 frames (unpause, walk left into the
    Buzzes, then right until the Sneek despawns): Sneek drawn in its own
    palette in 16:10, 16:9, and 21:9; a second live overflow (`$0023`, slot 22)
    was recorded and released; tags cleared when the sprites despawned.
  - The 4:3 render of the same save still shows palette 0: the guest state
    in the save carries the fallback, and the 4:3 image is the oracle.
- **Accepted divergence:** for this save, wide aspects now draw the Sneek
  differently from the 4:3 oracle, which shows palette 0. That is deliberate:
  the overflow is produced by widened activation, so at 4:3 the cartridge
  draws exactly what it computed, and the host table only repairs a state the
  wide aspect caused. The 4:3 path is left untouched rather than "corrected",
  because correcting it would mean the 4:3 image no longer follows guest
  state. No follow-up is planned for 4:3.
- **Review follow-up (2026-11-22):**
  - The save-recovery heuristic no longer runs every frame. A state load, and
    every in-level frame at 4:3, arm an 8-frame window; recovery runs only in
    that window and disarms on the first attachment. Before, once the
    original sprite despawned, a stale `$1D4B` could be attached to any later
    palette-0 dynamic sprite.
  - Leaving a wide aspect now clears the tracked slots, so returning to a
    wide aspect cannot apply a stale overflow palette to a sprite that
    legitimately uses palette 0.
  - Verified: headless 16:9 replay of the release quick save still attaches
    `$0058` to slot 20 on the first frame after load; the 4:3 replay
    records nothing. The despawn-then-respawn case that motivated the change
    was not reproduced.
- **Unverified:** a byte-identical 4:3 A/B against the previous build on the
  saved-scene corpus (argued from the early returns, not measured); a live
  overflow reached from a level start rather than a save; the interpreter
  (LLE) path through `$BB:95E6`, which does not call the hook; the live
  desktop app; macOS rebuild.

## 2026-11-22 - bonus-room background slides in widescreen (glide in narrow rooms)

- **Symptom:** in the first level's bonus room (level word `$00C0` = `$0051`,
  the reported release quick save), widescreen showed black bars and the
  background moved noticeably when walking left or right, kept sliding after
  the player stopped, and the HUD banana counter drifted sideways.
- **Measurement:** the room's camera range is `$0100..$0118` (24 pixels of
  travel), narrower than two margins at every wide aspect (52 at 16:10, 86 at
  16:9, 190 at 21:9). `Dkc3VideoPresentationMarginsBounded` under `glide`
  pinned the presented center to `$010C`, so the target bias was
  `$010C - camera`, moving one pixel per pixel of camera travel. The adapter
  limits the presented bias to one pixel per frame (`runner/dkc3_game.c`), so
  on a 16:9 replay (60 frames Right, 60 Left) the camera fell from `$0118` to
  `$0104` in 8 frames while the bias went `-12` to `-4`, then the bias kept
  climbing to `+12` over the next 24 frames with the camera nearly still:
  the picture lagged, then slid back. Every sprite, HUD included, moved by
  the bias.
- **Fix:** `glide` now keeps a zero bias in a room narrower than two margins
  (`range < 2 * extra`) and clamps each margin to the authored extent, which
  is the `bars` geometry. Wider rooms, `shift`, `bars`, and `reflect` are
  unchanged. Tests: the 16:10 glide cases for a `$0120` room now expect the
  camera-locked geometry, plus the `$0118` bonus geometry and a `$0134` room
  (exactly two margins) that still glides.
- **Verified (headless, release quick save, 60 Right then 60 Left):** bias 0
  on every frame at 16:10, 16:9, and 21:9; at 16:9 the native 256 columns of
  every sampled frame (every 4th of 161) are pixel-identical to the 4:3 run;
  every frame and the WRAM/VRAM/CGRAM/OAM hashes are identical to
  `DKC3_WIDESCREEN_EDGE=bars`; with `DKC3_CULL_WIDEN=0` the 16:9 WRAM hash
  equals the 4:3 hash. The right margin blanks 43 to 19 columns as the camera
  moves west, as the clamp predicts. All 30 CTest checks pass. The 4:3 path
  is unchanged by construction (the branch requires `extra > 0`).
- **Open:** at the west wall the left margin still shows 24 columns of
  continuous dock and sky where the clamp asks for none (19 black columns
  remain); `bars` shows the same, so it predates this change and its source
  layer is not yet identified. The black bars themselves are the honest
  answer for a room the map does not extend.
- **Unverified:** the live desktop app, other bonus rooms, ordinary levels at
  a wall in a live run (covered only by the unchanged unit cases), macOS.

## 2026-11-22 - second bonus room still slides in widescreen (glide in short rooms)

- **Symptom:** in the same level's other bonus room (the reported release
  quick save, a "Collect the Stars!" room), the background kept moving after
  the player stopped in widescreen, as in the first bonus room.
- **Measurement:** camera range `$0100..$0180` (128 pixels). That is wider
  than two 16:9 margins (86), so the previous narrow-room lock did not
  apply, but shorter than the glide's two releases (2 x 43 x 8 = 688). The
  pins held the presented center inside `[$012B, $0155]`, 42 pixels for 128
  of camera travel, so the target bias moved about two pixels for every
  three of camera. On a 16:9 replay (90 frames Right, 90 Left) the bias went
  from `-43` and was still climbing one pixel a frame at `+15` with the
  camera parked at `$0102`, 15 frames after it stopped.
- **Fix:** under `glide`, a room shorter than `2 * extra * 8` pixels uses a
  wall shift of `range / 16` (none when narrower than two margins), released
  at the usual one pixel per eight, without the pins; margins clamp to the
  authored extent. Both releases then fit in the room, so the bias changes
  at most one pixel per eight of camera travel and never outruns the
  one-pixel-per-frame limit. Rooms of `2 * extra * 8` pixels or more,
  `shift`, `bars`, and `reflect` are unchanged. Tests: 16:10 short rooms of
  52 and 128 pixels, and a 416-pixel room that still glides in full.
- **Verified (headless, release quick save):** at 16:9 the bias moves from
  `-8` to `+8` in steps of at most one, stops when the camera stops, and
  the native 256 columns of all 56 sampled frames match the 4:3 run pixel
  for pixel at the biased offset. 16:10 gives the same `-8..+8`, and 21:9
  (two margins = 190 > 128) stays at bias 0. With `DKC3_CULL_WIDEN=0` the
  16:9 WRAM hash equals the 4:3 hash. The first bonus room's frames and
  hashes are unchanged from the previous fix. All 30 CTest checks pass.
- **Unverified:** the live desktop app, the bonus rooms of later
  levels, macOS.

## 2026-11-22 - stale pixels beyond a wall in widescreen margins (bonus rooms)

- **Symptom:** in the first level's 128-pixel bonus room (reported release
  quick save, unpaused), walking right to the east wall drew the room's last
  columns correctly, but past them the right margin showed a slanted,
  repeated strip of sky, dock, and lake that changed as the camera moved
  vertically.
- **Measurement:** walking right (input mask `80`, hex) took the camera to
  `$0180` while camera Y fell from `$01B4` to `$01CD`. The clamp
  (`Dkc3VideoMarginsForBias`) gave a right margin of 8 columns, yet 35
  columns past the native window held content. Layer-isolated renders all,
  including empty BG4 and OBJ, showed the same diagonal boundary, so the
  pixels did not come from a layer. The runtime PPU writes only
  `[-extraLeftCur, 256 + extraRightCur)` on ordinary lines and nothing clears
  the rest of the host row, so each column the margin gave up while the
  camera reached the wall kept the pixels of the last frame that drew it,
  frozen at a different camera height: the shear. A run started at the wall
  is black there, which is why a fresh restore did not show it.
- **Fix:** `Dkc3VideoClearOutsideMargins` blacks the columns outside the
  clamped span, and the adapter calls it right after
  `PpuSetExtraSideSpace`, before the frame's lines render. Lines that
  composite the full budget (HUD split, anchor bands, repeat bands, forced
  blank) still write the whole row and overwrite it. No runtime patch. Unit
  test for left, right, none, and a too-narrow row.
- **Verified (headless):** on the reported save at 16:10, 16:9, and 21:9,
  every pixel that changed against the previous build is now black and lies
  outside the clamped span (16:9: columns 307..341). The native 256 columns
  of all 98 sampled 16:9 frames still match the 4:3 run at the biased offset.
  On the first bonus room's save, the only change is the 24 leftover west-wall
  columns (19..42) becoming black, which settles that entry's open item; its
  memory hashes are unchanged. An ordinary level save (walk right, left,
  jump) renders identically to the previous build at 16:9 and 21:9. All 30
  CTest checks pass.
- **Unverified:** the live desktop app, other bonus rooms, macOS.

## 2026-11-22 - the overworld fills the wide view

- **Request:** the reported release quick save is on the Northern
  Kremisphere overworld (boat by Funky's Rentals), where every wide aspect
  showed the native frame between black margins.
- **Why it was black:** every widened path keys on the level main-loop
  vector (`$4E` = `$B3:8076`). The overworld runs `$B4:B179`, so it took the
  bounded-screen branch.
- **Measurements (headless, the reported save):** Mode 1; BG1 a 64x32 map at
  `$7800`, BG2 a 32x32 water map at `$6C00` whose scroll HDMA varies per
  line, BG3 a 64x32 map at `$7000` at BG1's scroll; vscroll `$11F` throughout.
  The overworld camera is the level camera word `$196D`, `$100` ahead of
  BG1HOFS. Holding each direction for 600 frames took the camera across
  `$150..$200`, and not one word of the three tilemaps or of the BG1/BG3
  character data changed: the whole 512-pixel map is resident, spanning
  world `$100..$300`. `$200` is the east bound (the native right edge
  reaches the map end); `$150` was the boat meeting land, so the west bound
  is assumed to be the map's first column, `$100`. While the boat moves,
  `$196D` leads the latched BG1HOFS by up to two pixels.
- **Change:** `Dkc3VideoIsResidentOverworld` admits the scene (the vector,
  Mode 1, BG1 64 columns by 32 rows rendered within eight pixels of the
  camera less `$100` and not past `$200`, every other enabled 64-column
  layer at BG1's phase, every enabled layer 32 rows, no shared tilemap
  pages, and `DKC3_CULL_WIDEN` not `0`). The adapter
  (`Dkc3PresentOverworld`) then hashes every page of the enabled tilemaps
  and widens only on a frame whose hash equals the previous frame's. A
  change while the picture is displayed latches black margins until the
  scene is left (a streamed map would show stale ring columns), and a change
  under forced blank or zero brightness only restarts the proof. Margins
  render BG1-BG3 straight from VRAM: BG1 and BG3 are the resident picture,
  and BG2's 32-column wrap at 256 pixels is the hardware's. Each margin is
  clamped to the map at the rendered scroll (`Dkc3VideoOverworldMargins`),
  under every edge policy, and the columns outside it are cleared.
- **No glide here:** a first version reused the level glide. The PPU's
  presentation bias moves sprites, but on this raw path nothing moves the
  backgrounds with them (levels get that from the world store), so a
  4-pixel bias put the boat and rocks 4 pixels off the map in the native
  center. The overworld is camera-locked at bias 0; at the east bound the
  right margin is black. A runtime hunk that excluded margin-only sprites
  from the 32/34 line limits was tried while chasing that symptom and
  removed: this frame shows 15 sprites, nowhere near either limit.
- **Verified (headless):** at 4:3 the frame, WRAM, VRAM, CGRAM, and OAM
  hashes of a 30-frame run and of 600-frame Left and Right holds are
  identical to the build before the change. At 16:10, 16:9, and 21:9 the
  native 256 columns equal the 4:3 frame on every frame 2..29, and the WRAM
  hash equals 4:3. On a 300-frame Right, 500-frame Left run (camera `$1A0`
  to `$200` to `$150`, every eighth frame), the native center matched 4:3
  in all 100 sampled frames, no frame fell back to black margins, and in
  BG1-only renders all 5,943 margin columns inside the map equal the
  native BG1 column at the same world x (offsets derived from the frames,
  not the leading camera word); all 1,388 margin columns outside the map
  are black. Only the first frame after the restore (the hash's first
  sample) shows black margins. With `DKC3_CULL_WIDEN=0` the margins are
  black and WRAM equals 4:3. Unit tests cover the gate, the camera lead,
  the east bound, the nine-bit scroll, and the clamp under all four edge
  policies. All 30 CTest checks pass; the desktop and SDL apps build.
- **Open - overworld sprite window:** the cartridge keeps overworld objects
  in OAM only about 45-48 pixels past each native edge. A signpost left OAM
  when its left edge passed x 302-304, and a 32-pixel rock when its left
  edge passed about x -56, with 18 of its pixels still inside a 16:9 left
  margin. At 16:9 a sprite can therefore vanish in a margin's outer pixels;
  at 21:9 (margins to 351) the signpost vanishes about 47 pixels inside
  the right margin. Widening that window needs the routine located in the
  disassembly; nothing was widened.
- **Unverified:** the live desktop app, other overworld screens and the
  per-world maps (they keep black margins unless they pass the gate and
  the static-map proof), the real west bound, transitions into and out of
  the overworld in a live session, and macOS.

## 2026-11-22 - right margin cut off around a mill elevator (camera regions)

- **Symptom:** in the reported release quick save (SHA-256
  `30a0ee89...bd00`, level word `$00C0` = `$002A`, 16:9 with `glide`), the
  Kongs stand by an elevator at the foot of a shaft. Riding it up to the
  platform that leads right, the right margin showed misaligned blocks of
  other rooms, with a hard seam at world `$02A0`/`$02C0` and a dark brick
  patch on the plaster wall. The left margin showed only 17 columns.
- **Measurement:** the camera stays at x `$0190` through the shaft while
  the glide held bias `+25`. Its walls were `$0100` and `$04BC` = `$0A00`,
  so it slid toward a west wall 144 pixels away and gave the right margin
  all 43 columns plus the bias. Those columns decode correctly from the
  level map (every margin cell matched), but no camera position shows them.
  The cartridge confines the camera with a table of camera regions that
  `$B7:D4B1` builds in WRAM from `$FD:C25B`. Edges are at `$995A`
  (left), `$9A5A` (right edge of the last screen), `$9B5A` (top), and
  `$9C5A` (bottom), indexed by region x 2. Entry 0 is the whole level.
  `$9D5A` holds flags (low byte) and the right neighbour (high byte);
  `$9E5A` holds two left neighbours. `$0478` is the current region x 2: 2 in
  the floor room, 6 in the shaft, and 4 at the top in this replay. Here the
  floor room is x `$0110..$0290`, the shaft `$0190..$0290` (exactly one
  screen, no side links), the top room `$0190..$0290` linked right to
  `$0290..$0418` and on east. The clamp at `$B7:DB90..DCAD` widens the span
  into a linked neighbour only while it overlaps the target rows (`$049F`)
  by `$38`, or while the player (`$047B`) is near a neighbour flagged `$80`;
  `$20` never joins, and `$B7:E0ED` also tries the region after the link
  when that region names the current one as a left neighbour. The joined
  bounds live in `$0466..$046C` only inside the clamp; the same words hold
  camera velocities at the end of a frame.
- **Fix:** `Dkc3VideoCameraRegionSpan` returns the current region's span
  continued through its left and right links (stopping at a missing link or
  a `$20` neighbour), and the adapter uses it as the glide's walls and the
  margin clamp, the native window always inside. Entry 0, a malformed
  offset, or a region narrower than one screen keeps the old bounds. The map
  hold still overrides the west wall. `reflect` keeps the level-wide wall.
  A first version also applied the cartridge's `$38` row-overlap and `$80`
  player gates. On the Squeals on Wheels save (`7542f1de...`) a left
  neighbour dropped out during a descent and the 16:9 bias swung from -38
  to -20 and back over 30 frames. Those gates change with vertical travel,
  so the span ignores them and moves only when `$0478` does.
- **Verified (headless, private ROM, previous build as reference):**
  - 4:3 frame, WRAM, VRAM, CGRAM, and OAM hashes are identical to the
    previous build on a 480-frame elevator replay (unpause, jump onto the
    lift, ride to the top), a 500-frame Squeals on Wheels walk, and an
    1,800-frame boot. The 16:9 boot is identical too.
  - Elevator replay: the bias is 0 through the shaft at 16:10, 16:9, and
    21:9, and both margins are black. At the foot of the shaft it is -8 at
    16:9, and the right margin shows the floor room up to `$0290`. Once
    `$0478` becomes the top room, the right margin opens onto the platform
    and the bias ramps one pixel a frame to 36 at 16:9 (21 at 16:10, 84 at
    21:9). There are no direction reversals. One step of more than one pixel
    at 16:10 and 16:9 is the settle on the first frames after the restore.
  - The native 256 columns equal the 4:3 frame at the biased offset on 45 of
    59 sampled frames in each aspect. The other 14 differ only in the top
    room's DK barrel, which swings with a different phase because widened
    activation starts it earlier. The previous build has the same 14 frames.
  - With `DKC3_CULL_WIDEN=0`, the WRAM and OAM hashes at 16:9 and 21:9
    equal 4:3.
  - Squeals on Wheels: the bias differs by a steady 3 pixels or less near
    the east end, where the camera parks at the region wall `$09C0`
    (`$04BC` is `$09D8`). The swing is gone. WRAM differs at 16:9 because
    the widened cull windows follow the bias.
  - Unit tests cover the shaft, an eastward chain, a westward link, the
    `$20` and `$80` flags, the second east candidate, and invalid input.
    All 30 CTest checks pass; the desktop and SDL apps build.
- **Unverified:** the live desktop app, other levels' region tables
  (bonus rooms, vertical levels, boss arenas), any level where `$0478`
  points at a region whose links the cartridge never uses, and macOS.

## 2026-11-22 - the overworld keeps its margins after the hovercraft

- **Symptom:** a later release quick save, taken after the hovercraft is
  bought at Funky's, showed black margins on the overworld again.
- **Cause:** the gate still passed (same Mode 1 maps at `$7800`, `$6C00`,
  `$7000`), but the static-map proof latched. The hovercraft opens the map
  north of the original screen, and the camera now climbs (vscroll `$11F`
  down to `$AF` in the replay). The game streams tilemap rows ahead of it.
  Driving each of the eight directions for 900 frames, up-right latched at
  frame 101 (first row write) and up-left at frame 884.
- **Measurement:** per-frame VRAM diffs over a 2,550-frame path through
  every direction. All 44 layer-frames with writes changed words in both
  256-pixel pages. Each write is a 32-word run per page that starts one word
  into a row, so its last word lands in column 0 (and 32) of the next row.
  At most one changed row per frame was on screen: the run's tail. A game
  streaming columns would touch every shown row instead.
- **Change:** the whole-map hash is replaced by a per-row comparison with
  the previous drawn frame's copy (`Dkc3OverworldMapsResident`). A frame
  passes when each enabled layer's changed rows include at most
  `kDkc3OverworldShownRowWrites` (2) rows that the frame shows
  (`Dkc3VideoOverworldWritesAreRows`, `Dkc3VideoTilemapRowVisible`). A
  layer whose vertical scroll register an active HDMA channel writes
  (`Dkc3VideoHdmaVerticalScrollLayers`: `$210E`, `$2110`, `$2112` by
  transfer mode) counts every changed row as shown. Other writes while
  displayed still latch black margins. The code landed in `c9b28d6`
  together with the camera-region change.
- **Verification (headless, the hovercraft save):**
  - No direction latched. Along the 2,550-frame path, 2,547 of 2,548
    overworld frames widened at 16:9 and at 21:9; the remaining frame is
    the first after the restore. In 50 sampled frames per aspect, the west
    margin was never black. East columns were black only where the camera
    stood at or near the map's east bound (`$200`), as the clamp requires.
  - A tilemap oracle keyed each cell by world column and row from the
    tiles the native 256 columns showed in any frame. 542,317 of 596,541
    BG1 margin cells at 16:9 matched it, 50,483 were never shown in the
    center, and 3,741 (18 distinct cells) disagreed. The 18 are cells the
    initial screen drew one way and a streamed row later drew another way.
    The margin shows the streamed VRAM, which is what the native view shows
    at that spot after streaming.
  - Unit tests cover row visibility at `$11F`, `$0`, `$100`, and one-line
    edges; the measured row-streaming shapes; column-shaped writes; and
    HDMA vertical-scroll detection for modes 0, 1, 2, and 4. All 30 CTest
    checks pass.
- **Found, not fixed:** in the headless replay, native 4:3 shows garbled
  tiles in the rows streamed north of the original screen (from about frame
  700), identically with `DKC3_CULL_WIDEN=0`. The user verified the fix in
  the desktop app at this save and saw no garbling there, so this may be
  specific to the headless host or the scripted replay. Not investigated.
- **Unverified:** 4:3 byte-identity was not re-measured against the
  previous build (the new path runs only when widescreen is on). The user
  verified the margins in the desktop app at this save. Unverified: other
  overworld maps and macOS.

## 2026-11-23 - PR 17 review follow-ups (bias policy switch, overworld notes)

- **Changes:**
  - `runner/dkc3_game.c`: the presented bias re-snaps when the widescreen
    edge policy changes (for example `shift` to `glide` from the menu).
    Before, it eased one pixel per frame from the old policy's bias, so the
    background and HUD slid for up to a margin's worth of frames.
  - `runner/dkc3_video.h`: the overworld comment now separates the
    measured two-pixel forward camera lead from the accepted
    `kDkc3OverworldCameraLead` of eight pixels either way, which is
    tolerance, not measurement; and it states the write contract as
    `Dkc3VideoOverworldWritesAreRows` implements it (hidden rows, or at most
    `kDkc3OverworldShownRowWrites` shown rows per frame).
  - `tests/test_dkc3_video.c`: the right-margin clear case checks all four
    bytes of each pixel.
  - `docs/WIDESCREEN_GUIDE.md` 6.5 and `README.md`: the one-pixel limit is
    stated as a per-frame bound; README claims are narrowed to what this log
    records (bonus rooms verified only in the first level, the overworld
    west bound assumed, the headless-only garbling at the hovercraft save,
    linked regions stopping at a missing link or a `$20` neighbour).
- **Not changed:** the overworld gate counts changed rows rather than
  validating each row's write shape. The host sees only the word-level diff
  against the previous frame, so a streamed row whose new words partly
  equal the old ones shows as a partial row; a shape check on the diff
  would reject real streaming. A sound check needs VRAM write spans from
  the runtime, which this host does not record. The map is fully resident,
  so a shown-row write outside the native columns is still map content the
  camera would show on arrival; the risk is a game using margin cells as
  scratch, which has not been observed. Open.
- **Verified:** Python unit tests 27/27 and the full Windows Release CTest
  suite 30/30, with every target (including `DKC3RecompSDL.exe`) rebuilt.
  The policy-switch re-snap was not exercised in the app.

## 2026-11-22 - KONG letter tracker and map badges

- **Feature:** the host records, per save file, every level whose four
  K-O-N-G letters have been collected, and draws "KONG" beside that
  level's name on its world map. A world's name on the Northern
  Kremisphere gets the same badge once all five of its levels are
  recorded.
- **Save compatibility:** the cartridge never saves KONG letters; they
  exist only in WRAM while a level runs. The tracker keeps its own
  sidecar, `saves/kong_letters.dat` (112 bytes: `DKC3KONG`, version 1,
  three 32-byte level bitmaps, FNV-1a), and only reads SRAM. `save.srm`
  stays exactly what the cartridge writes and remains portable to other
  emulators. Losing or deleting the sidecar loses only the badges.
- **Cartridge facts (from the private ROM; addresses only):**
  - `$05D7` bits 0-3 are K, O, N, G. The pickup at `$BB:CD3A` sets its
    bit with TSB and calls the life award (`$B8:8084`) when `$05D7 + 1`
    has a zero low nibble. A star barrel copies the byte to `$05C3`
    (`$BB:9B2F`) and a respawn restores it (`$BB:9AF6`); leaving a level
    clears it (`$BB:A270`, `$BB:A32A`). Bit 4 is a different collectible
    (`$BB:D216`) and is ignored.
  - `$00C0` is the level number. Entering a bonus room parks it in
    `$04E6` and sets `$04EA` (`$BB:A2EE`..`$A2F9`); returning restores it
    and clears `$04EA` (`$BB:A38B`..`$A396`). The tracker credits `$00C0`
    when it is a KONG level, else the parked parent while `$04EA` is set.
  - `$04C8` is the file slot: `$80:91CB` indexes the SRAM table at
    `$80:9171` (`$6062`, `$62EC`, `$6576`, so SRAM offsets `$062`, `$2EC`,
    `$576`, `$28A` bytes each) with it.
  - A slot is valid ($80:9141) when its word at +4, bit 0 ignored, is
    `$52`, `$152`, or `$252`, and the words at +0 and +2 equal the 16-bit
    sum (0 becoming 1) and XOR of the words from +6 to `$288` (`$80:9177`).
    The file select's erase zeroes only the word at +0 and leaves the
    rest of the slot.
  - Maps: `$05E5` is the world (0 is the Northern Kremisphere) and `$05E7`
    the node under the Kongs. The world descriptor is the word at
    `$B4:CFB9 + 2 * world` (`$B4:B0C5`), its node table the word at
    descriptor + 6, a node 11 bytes with the destination at +1
    (`$B4:B804`). On a world map a destination `>= $1D` is a level
    (`$B4:B37B`); on the Kremisphere `$40|w` opens world `w`
    (`$B4:B31F`). Reading all eight tables gives five levels per world,
    `$25`-`$4C` in all, and the bosses `$1D`-`$24`. Byte 0 bit 7 is not a
    level/non-level flag: Razor Ridge's boss node has it clear.
  - The idle map's main loop is `$B4:B179` on world maps and the
    Kremisphere alike; a level runs `$B3:8076`.
  - The map's name text is pure `#FFFF00` with a black outline on native
    rows 201-214.
- **Changes:**
  - `runner/dkc3_kong_tracker.{h,c}`: the tracker (flags, sidecar format
    and atomic replace, frame observation, SRAM copy/erase following, ROM
    node tables, world completion, map badge choice, node-settle wait,
    badge drawing). It takes WRAM, ROM, SRAM, and frame pointers, so it is
    unit-tested without a ROM.
  - `runner/dkc3_game.{h,c}`: `Dkc3KongTrackingEnable(path)`; observation
    and SRAM sync after every game frame, saving the sidecar on any
    change; the badge drawn into the rendered frame at the end of
    `Dkc3DrawPpuFrame`. Inert until a host enables it.
  - `runner/desktop_main.c`, `runner/sdl_main.c`: enable it with
    `saves/kong_letters.dat` after loading `save.srm` (not when SRAM
    persistence is disabled). `DKC3_KONG_BADGES=0` hides the badges but
    keeps recording.
  - `runner/headless_main.c`: `DKC3_KONG_TRACKER=<file>` opts in; off by
    default, so headless hashes are unchanged.
  - `tests/test_dkc3_kong_tracker.c`, `CMakeLists.txt`: unit test
    `dkc3_kong_tracker`; the module is linked into all three game
    targets.
- **Badge placement:** the badge is a 7x7 font doubled to the name's 14
  rows, yellow with a black outline, one game space (10 px) right of the
  rightmost name pixel found in rows 201-214 of the whole frame, so it
  follows the name in every aspect; with no room it is centred above the
  name. Nothing is drawn when no name text is on screen.
- **Node-settle wait:** `$05E7` switches to the destination the frame the
  Kongs set off, while the departing name is still erasing. The first
  version drew the badge on "WRINKLY'S SAVE CAVE" for frames 1302-1304 of
  the walk below. The badge now waits for the map to stay idle on one
  node for 16 game frames; the departing name is gone within 6, and the
  next name appeared 64 frames after the node changed.
- **Verified (Windows x64 Release, private ROM, user's `save.srm`, file
  1 "MATTT" in Kremwood Forest):**
  - `test_dkc3_kong_tracker` and the Release unit label: 26/26 passed.
    `DKC3Recomp.exe`, `DKC3RecompSDL.exe`, and the headless host build;
    no new warnings.
  - Sidecar flagging `$32`: after walking left from the cave, "RIVERSIDE
    RACE" shows the badge at frame 1700; the cave and "BARREL SHIELD
    BUST-UP!" (`$30`, unflagged) do not.
  - Walk cave to Barrel Shield Bust-Up and back with `$30` flagged,
    rendered with and without the tracker every 2 frames from 1280 to
    1918: the frames differ only for 1366-1604, the frames that show
    "BARREL SHIELD BUST-UP!".
  - Leaving Kremwood Forest (Right from Barrel Shield Bust-Up) reaches the
    Kremisphere at node 5 (destination `$42`). With `$30 $32 $29 $2F $34`
    flagged, "KREMWOOD FOREST" shows the badge at 4:3 and 16:9; with
    `$34` missing, the frame is byte-identical to a run without the
    tracker.
  - Recording: the user's quick save in level `$29` (file slot 0) holds
    K and O (`$05D7 = 3`) and writes no sidecar; forcing `$05D7 = $0F`
    for one frame records exactly `$29` for slot 0.
  - File select, driven by scripted input: Erase on file 1 zeroes SRAM
    `$062`-`$063` only and the tracker drops slot 0's flags; Copy of file
    1 to file 2 gives slot 1 the same flags.
- **Unverified:**
  - Collecting real letters in play; recording was shown with a poked
    `$05D7`, not by touching the four letter sprites, and not in a bonus
    room (the parent-level path is unit-tested only).
  - The desktop and SDL apps were built but not run; macOS not built.
  - Worlds other than Kremwood Forest were not visited; their level
    lists come from the ROM tables above. Krematoa (the Lost World) and
    Kremisphere destinations other than `$41`-`$48` were not looked at
    in play.
  - Two-player modes and the Banana Bird and Brothers Bear screens.

### Settings checkbox for the KONG badges

- **Feature:** the overlay's Settings tab has a "Gameplay" section with
  **KONG letter badges on the map**, on by default. It shows or hides the
  badges while the menu is open; recording continues either way.
- **Changes:**
  - `runner/desktop_launcher.{h,c}`: `Dkc3LauncherKongBadges` and
    `Dkc3LauncherSetKongBadges`, default 1, persisted in `launcher.cfg` as
    `KongBadges`, beside `HapticsEnabled`.
  - `runner/desktop_overlay.cpp`: the checkbox.
  - `runner/dkc3_game.{h,c}`: `Dkc3KongSetBadgesVisible`.
  - `runner/desktop_main.c`, `runner/sdl_main.c`: after enabling the
    tracker, a set `DKC3_KONG_BADGES` overrides the saved choice and is
    remembered, as `DKC3_HAPTICS` is; the value is pushed to the game at
    start and on every `ApplyOverlaySettings` call.
  - `tests/test_desktop_launcher_settings.c`: default on, saved `0`
    restored, `KongBadges=0` written.
- **Verified:** the Windows Release suite passed 31/31, including
  `desktop_launcher_settings`, `dkc3_kong_tracker`, and the SDL aspect
  smoke tests; `DKC3Recomp.exe` and the headless host rebuilt.
  `DKC3RecompSDL.exe` was relinked once the running app was closed. The live app had shown the badges working before the
  checkbox was added; the checkbox itself has not been clicked in the app.

## 2026-11-23 - KONG letter tracker review follow-ups (PR 17)

- **Changes:**
  - `runner/dkc3_kong_tracker.c`:
    - `Dkc3KongTrackerSaveFile` replaces the sidecar in one step
      (`MoveFileExA` with `MOVEFILE_REPLACE_EXISTING` on Windows, `rename`
      elsewhere) instead of deleting it first, so a failed replace keeps
      the recorded flags.
    - `Dkc3KongWorldLevels` tracks uniqueness in a local array, so a
      `capacity` smaller than the level count, or `NULL, 0`, no longer reads
      past the caller's buffer.
    - SRAM copy detection: when several slots hold the image a slot was
      just copied to, the source is ambiguous (for example, a slot copied
      earlier and then played without saving). The copy now takes only the
      levels every candidate has, instead of the first candidate's flags.
      Slots whose image did not change in the same sync are preferred as
      sources, so copying one slot onto both others at once gives each the
      source's flags rather than the intersection with the other
      destination's old flags; changed slots are used only when no
      unchanged one holds the image (a slot saved and copied in one sync),
      and then only if they held a file before the sync: a slot that was
      blank cannot be the source, so the saved source keeps its flags
      instead of taking the new copy's empty ones. Two slots that both held
      files, both changed, and now match remain ambiguous and are
      intersected.
      The file select's chosen source is not read, so this stays a
      conservative guess, not exact.
  - `runner/desktop_overlay.cpp`: "Restore All Settings to Defaults" turns
    the KONG badges back on.
  - `runner/dkc3_game.c`: an over-long sidecar path is reported as such,
    and the unreadable-file warning no longer says "ignoring", since the
    headless host stops on it.
  - `runner/headless_main.c`: the headless host keeps failing closed (exit
    21) on an unusable `DKC3_KONG_TRACKER` file, now stated in a comment
    and message; the desktop and SDL hosts carry on with empty flags.
  - `tests/test_dkc3_kong_tracker.c`: version and size rejection are
    tested independently with a recomputed checksum; ambiguous copies and
    a short or absent output buffer are covered.

## 2026-11-23 - KONG letters kept for files played through quick saves

- **Problem (reported):** the tracker cleared the flags of every slot
  without a valid SRAM file when it adopted the battery save at launch.
  A file played only through quick saves never gets a valid slot, so its
  letters were lost on the next launch. Quick saves also carry the
  cartridge SRAM (`cart_saveload` serialises `cart->ram`, which is
  `g_sram`), so loading one taken before the file was saved in game made
  the slot invalid again, and the tracker read that as an erase. It
  could likewise read two slots left identical by a quick load as a copy.
- **Fix:**
  - Adopting SRAM never clears flags, at launch or anywhere else.
  - Copy and erase are followed only while the file select runs (main
    loop `$80:9586`); the earlier erase and copy runs never left it.
    Outside it, an SRAM change is an in-game save or a quick load and is
    only adopted.
  - `Dkc3OnStateLoaded` calls `Dkc3KongTrackerForgetSram`, so the first
    sync after any quick load (or rewind) adopts the restored SRAM even if
    it lands in the file select.
  - `Dkc3KongTrackerSyncSram` takes `follow_edits`; the adapter passes
    `Dkc3KongInFileSelect`.
- **Trade-off:** a file played only through quick saves keeps its letters
  until its slot is erased in the file select. If its slot is never saved
  and a new game is started there, the new game inherits those letters;
  erasing the slot first clears them.
- **Verified (Windows x64 Release, private ROM, user's `save.srm`):**
  - `test_dkc3_kong_tracker` passes, with new cases: adoption keeps
    invalid slots' flags; an older quick load invalidating a slot, or
    leaving two slots identical, changes nothing; a forget-then-sync in
    the file select only adopts; a real erase still clears.
  - Headless: flags on slots 1 and 2 (no valid save in `save.srm`)
    survive boot into the file select (the previous build cleared them at
    launch). Erasing file 1 in the file select still clears slot 0 and
    leaves slots 1 and 2. Loading the user's in-level quick save leaves
    all flags as they were.
- **Unverified:** a quick load in the desktop app, and rewind, both of
  which reach the same `Dkc3OnStateLoaded` hook.

## 2026-11-23 - Ripsaw Rage: forest through a trunk in the right margin (clamped BG3 windows)

- **Symptom:** in the reported release quick save (SHA-256
  `7ef87487...0de5`, level word `$00C0` = `$002E`, Ripsaw Rage, paused,
  16:9 with `reflect` in the user's `launcher.cfg`), the right side of the
  wide view looked wrong: the far forest (BG3) showed through the next
  tree trunk's hollow, and the trunk was split by a column of forest.
- **Measurement:** the margin terrain was correct. BG1 margin cells
  matched the decode (434 of 434), and a pixel oracle against the native
  view of the same world columns (4:3, camera `$0211`) agreed on BG1.
  BG2 and BG3 are static 32-column maps; neither tilemap changed over a
  200-frame walk. The difference was the hardware windows. `windowsel`
  `$00a00aa0` with `main_window=$04` masks BG3 inside W1 or W2. Along a walk
  right, W2's left edge tracked world `$02D8` exactly (`[249,255]` at
  camera `$01DF`, `[226,255]` at `$01F6`, `[199,255]` at `$0211`), and W1's
  right edge tracked `$0227`. The writer is `$B3:C0F4` (the only caller of
  `$B3:9741` is `$B3:9637`, Barrel Shield Bust-Up's fixed trunks). It loads
  W1 = [`$0158`, `$0227`] and W2 = [`$02D8`, `$03A7`] into DP `$1A`-`$20`
  (another pair of spans for other levels), subtracts `$196D`, and clamps
  each edge to 0..255 (W1L only for negatives). At the save's camera
  `$01C7`, W2 is unclamped `[273, 480]` and written `[255, 255]`. The
  parked-window rule (Barrel Shield Bust-Up) correctly treats that as
  empty, so nothing masked BG3 in the right margin from world `$02D8`.
- **Fix:**
  - `scripts/apply_dkc3_widescreen_overrides.py` (`adapt_trunk_windows`):
    calls `Dkc3RecordTrunkWindows(cpu->D)` right after the `$2129` write
    in block `$B3:C1B1`. Fails closed if the block or the write moves.
  - `Dkc3VideoUnclampedTrunkWindows` (`runner/dkc3_video.c`) returns each
    window's unclamped screen edges and accepts a window only when the
    registers equal the clamp of those edges.
  - `runner/dkc3_game.c` keeps the record while the level word is
    unchanged and hands it to the PPU on terrain-ready wide frames
    (`Dkc3ApplyTrunkWindows`); a state load clears it, and
    `DKC3_CULL_WIDEN=0` disables it. `DKC3_TRUNK_WINDOW_TRACE=1` prints
    every record.
  - `cmake/runtime-patches/ppu.hunks`: `Dkc3PpuSetWsWindowUnclamped` plus a
    step at the end of `@@ window-presentation-bias` that replaces a
    window's edges with the unclamped ones (minus the presentation bias,
    clamped to the layer's span) while margins are active and that
    window's registers still hold the recorded values.
  - The routine runs before the camera update, so a record trails the
    camera by one frame, exactly as the registers it writes do. A first
    version also required the camera to equal the recorded one. That
    dropped the masks when the game was paused one frame after the camera
    settled, so the check was removed.
- **Verified (Windows x64 Release, private ROM, previous build as reference):**
  - 4:3: every sampled frame of a 200-frame replay (unpause, walk right)
    and the final frame, WRAM, VRAM, CGRAM, and OAM hashes are identical
    to the previous build. An 1,800-frame boot is identical too. At 16:9
    and 21:9 the memory hashes are identical (no guest change).
  - Only margin pixels change: from the trunk's edge (x 304-309 at 16:9,
    356-361 at 21:9) to the right edge while the trunk is off the native
    screen (frames 8-40), and the left margin at frame 180, after the first
    trunk leaves to the west.
  - Pixel oracle: each changed pixel mapped through camera plus
    `extra - bias` to the 4:3 frame that shows the same world spot. With
    the known 3-pixel camera-word registration (the same best shift on
    unchanged columns), 470 of 470 covered pixels at frame 8 match the
    native view. The previous build matched 0 of them. Without the shift,
    16:9 frames 8-180 match on 1,828 of 4,647 covered pixels and 21:9 on
    4,077 of 7,884, against 0 for the previous build.
  - Unpause, then pause again: the mask stays on every paused frame.
  - With `DKC3_CULL_WIDEN=0`, every 16:9 frame equals the previous build.
  - Python unit tests 29/29; full Release CTest 31/31. New cases: the
    decoder (save, on-screen trunk, foreign register, W1L low byte,
    null), the override placement and two fail-closed anchors, and the
    runtime hunk.
- **Open:** a freshly restored save taken while paused (the reported one)
  still shows the old margin until the first unpaused frame. The routine
  does not run during a pause and its spans are not in WRAM, so there is
  no proven source until it runs. The `$B3:9741` trunks (Barrel Shield
  Bust-Up) get no window at all when off screen; they are untouched.
- **Unverified:** the live desktop app (the SDL host's hidden screenshot
  can only show the paused save, which is unchanged), the other span set
  and the levels that use it, the rest of Ripsaw Rage, 16:10, and macOS.

## 2026-11-23 - Ripsaw Rage: bark blocks under a hollow's platform (kept, faithful)

- **Report:** a later release quick save (SHA-256 `51d1be9a...395a3d`, level
  `$002E`, camera `$026F`,`$107E`, 16:9) showed stair-stepped 8-pixel blocks
  of bark under the tree platform in the right margin. The previous build
  draws the same picture, so the trunk-window change did not cause it.
- **Measurement:**
  - Removing BG2 (`SNESRECOMP_LAYER_MASK=29`) removes the blocks. The bark
    is BG2's main band, the static 32x32 map at `$7800` (HDMA band lines
    1-206, separate from the saw band's `$7400`). Its VRAM did not change
    over a 190-frame walk.
  - BG1 there holds tile entry `$0000` at world tiles (109..111, 575) and
    (111, 576). Character 0 at `$2000` is all zero, so the entry is
    transparent. The cartridge's own ring holds the same `$0000` when those
    cells are native (camera `$02C0`), and the decode matches it cell for
    cell.
  - Natively, BG2 never shows through those cells. Over a walk from camera
    `$026F` to its stop at `$02C0` (the region wall), a render without BG2
    differs from the full render by 0 pixels there, and the area is black
    (BG3 masked by the trunk window, BG2 empty).
  - In the margin, BG2 wraps at 256 pixels as hardware does. The bark in
    the holes equals, pixel for pixel, the BG2 trunk at the native left
    edge (columns 0-25) on the same lines.
- **Decision (owner):** keep it. A wider console would draw exactly this:
  the holes are authored, and only the native camera range keeps them away
  from BG2's trunk. Blacking out transparent BG1 cells inside a trunk
  window was offered and declined, because it would invent content the
  hardware does not draw.

## 2026-11-23 - review follow-ups: KONG baseline at quick load, trunk-window notes

- **Changes:**
  - `runner/dkc3_game.c` (`Dkc3OnStateLoaded`): the KONG tracker adopts the
    restored SRAM as its baseline inside the load hook (the runtime's
    `snes_saveload` has already restored it), instead of invalidating the
    baseline for the next frame to adopt. Before, a copy or erase made in
    the first file-select frame after a quick load was adopted unseen and
    its letters never followed. With tracking off it still only forgets.
  - `runner/dkc3_game.h`: `Dkc3RecordTrunkWindows`'s comment lists what it
    actually reads (the DP edges, camera X and level number from WRAM, and
    the PPU window registers).
  - `README.md`: a `kong_letters.dat` that fails its checks at launch is
    treated as empty, which drops every file's letters.
- **Not changed:** cubic reported that `@@ window-presentation-bias` calls
  `PpuWsWindowUnclamped` before it is declared. In the patched `ppu.c`
  (regenerated from the current hunks) the helper is defined at line 543,
  before both calls at 703-704; the hunk file's order is not the order in
  `ppu.c`.
- **Verified:** Python unit tests 29/29 and the Windows Release CTest
  suite 31/31 with every target rebuilt.
- **Unverified:** a quick load followed by a copy or erase on its first
  file-select frame in the app.

## 2026-11-25 - presented bias may cancel the camera's motion (DKC2Recomp port)

- **Source:** DKC2Recomp commit `d59c6127` ("Stop the widescreen view
  overshooting the walls of small rooms"). There, in a room whose camera
  scrolls 128 pixels, the pins let the 16:9 view move 42 pixels while the
  presented bias stepped one pixel a frame and the camera's look-ahead moved
  2-4, so the view ran up to 18 pixels past the east pin (a clamped black
  strip) and slid back after every turn.
- **DKC3 exposure:** DKC3 already avoids the short-room case under `glide`
  (2026-11-22: rooms shorter than `2 * extra * 8` take a reduced, unpinned
  shift). The pins still bind, and the target bias then tracks the camera,
  under `shift` at every wall, and under `glide` when a camera region or a
  west hold changes the bounds mid-level. In those frames the one-pixel step
  lags the camera exactly as in DKC2.
- **Change:** `Dkc3VideoStepPresentationBias` (`runner/dkc3_video.c`) keeps
  the one-pixel step toward the target and also allows a step as large as
  this frame's camera motion, only against it. `Dkc3DrawPpuFrame` records the
  camera X it last stepped at (`s_bias_camera_x`). The settle frames, the
  re-snap on a policy change, and the margins for a chosen bias are
  unchanged. The cartridge camera and WRAM are unchanged; 4:3 has no bias.
- **Tests:** `test_dkc3_video` checks the DKC2 cases: a camera moving 4 right
  into a pinned east wall leaves the view still; a still camera, or one moving
  with the step, keeps the one-pixel glide; no overshoot of the target.
- **Verified:** Windows Release build; CTest 31/31.
- **Unverified:** no replay measurement. The only local quick save
  (`saves/dkc3s0.sav`) boots to the Rare logo when restored by the headless
  runner, so no scene at a pinned wall was replayed before/after, and the
  saved-scene corpus and cave replay are not on this host. The live desktop
  app, `shift` at a wall, and region-change frames under `glide` are
  unverified.

## 2026-11-25 - left margin popping at a camera-region change (shaft entry)

- **Symptom:** in the owner's release quick save (`saves/dkc3s0.sav`,
  SHA-256 `b959c9b4...bb82`, level word `$00C0` = `$003E`, 16:9, `glide`),
  the Kongs stand where a pipe corridor meets a one-screen shaft. Without
  input the whole 43-column left margin flips between black and terrain:
  black on replay frames 2-17, full 18-45, black 46-127, full from 128.
- **Measurement:** the presentation walls (new `region=`, `walls=`, and
  `margins=` fields on `DKC3_PREFILL_TRACE`) follow `$0478`. Region 14 (the
  corridor, `$02C0..$03A8`, narrower than a screen) is rejected by
  `Dkc3VideoCameraRegionSpan`, so the level bounds apply and both margins
  are 43. Region 15 (the shaft, `$02C0..$03C0`, no side links) bounds both
  walls at `$02C0`. When `$0478` became 15 on frame 46 the camera was still
  at `$0295`, converging on the shaft, and the old code clamped the west
  wall to the camera: the margin went from 43 to 0 in one frame although
  the native window itself still showed 43 columns west of the wall. On
  frame 128 region 16 (linked west through 9 and 2 to `$0100`) opened it
  from 0 to 43 in one frame.
- **Change:** `Dkc3VideoRegionScrollBounds` (`runner/dkc3_video.c`)
  replaces the inline clamp. A camera outside its region's span gets the
  wall reflected about it, so the margin equals the distance the camera has
  left to travel and closes as it arrives. `Dkc3VideoLimitMarginGrowth`
  then caps how fast a margin may open: the view's own motion away from
  that side plus `kDkc3VideoMarginOpenRate` (2) pixels a frame, so a wall
  fixed in the world keeps pace and a wall that jumps outward wipes the
  margin open. Narrowing is never limited. The limiter is off until the
  bias has settled and while terrain is not ready (a restore or level start
  still opens at once), and `DKC3_CULL_WIDEN=0` disables it. Bias, culls,
  and the cartridge state are unchanged.
- **Result:** on the same 400-frame replay the margins now open 2 pixels a
  frame on frame 18 (both sides open by frame 39), close one pixel per pixel
  of camera travel from frame 46 to 107, and the left margin opens again 2
  pixels a frame from frame 128. A montage of every fourth frame shows no
  margin-wide jumps.
- **Verified (headless, private ROM, previous build as reference):** final
  frame, WRAM, VRAM, CGRAM, and OAM hashes identical on the 400-frame replay
  at 4:3, 16:9, and 21:9 with `DKC3_CULL_WIDEN` on and off, and on an
  1,800-frame boot at 4:3 and 16:9. `test_dkc3_video` covers the reflected
  walls (west, east, level clamp) and the growth limit. Windows Release
  CTest 31/31. The test source also lost seven stray carriage returns inside
  `fprintf` literals that MSVC read as line breaks.
- **Unverified:** the saved-scene corpus and cave replay are not on this
  host, so the mill elevator (2026-11-22) and Squeals on Wheels states were
  not replayed against this change. The live desktop app was rebuilt but
  not played at this spot.

## 2026-11-25 - rocks popping into the right margin (wall rule inside camera reach)

- **Symptom:** in the owner's release quick save (`saves/dkc3s0.sav`,
  SHA-256 `89ec2329...524421f`, level word `$00C0` = `$0038`, a waterfall
  level, 16:9, `glide`), blocks of rock and grass appeared in the right
  margin over the waterfall and vanished again. With no input the camera
  drifts from `$022A` to `$0220`; the blocks appear on replay frame 40, the
  first frame at `$0220`, and stay while the camera sits there.
- **Measurement:**
  - Layer isolation: the blocks are BG1 (terrain), not the waterfall's BG2.
    BG2's decoded source verifies on every frame (992 or 1024 cells).
  - The world store equals the decode in every margin cell
    (`margin_matching=420/420`), and the row stride is 64 bytes at 100% on
    every frame (new `stride=` field on `DKC3_PREFILL_TRACE`). Yet the
    expected entry for world tile (105, 280) is `$3A79` at phase 544 and
    `$0000` at 548. The WRAM map at `$7F:0000` is unchanged.
  - `DKC3_TERRAIN_FILL_MAP=2`: at phase 544 the cartridge's last tile is
    99, so its edge metatile is column 16 (the right edge of a tree trunk,
    full and backed by full), and column 17, the open gap between two
    trunks, is void on the visible rows. `Dkc3VideoFindStructuralWallSource`
    treats the trunk as a wall and `Dkc3WallChainMetatile` fills the gap
    with the trunk's successors. At phase 548 the edge metatile is 17 itself
    and the rule finds no wall. The rule ports DKC2's mine and shaft cases,
    where void beside a wall is never shown natively.
  - Native oracle: the 4:3 run walking right shows that gap empty when it
    reaches the window (camera `$0259`). The current camera region (2)
    bounds the camera at `$0100..$0380`, so the native window can reach world
    x `$0100..$047F`, and the gap (`$0320..$0357`) lies inside it.
- **Change:** `Dkc3VideoTileInCameraReach` (`runner/dkc3_video.c`) is true
  for a tile wholly inside `[minimum, maximum + 256)` of the frame's camera
  scroll bounds (the region walls, or a west hold). `Dkc3DrawPpuFrame`
  records those bounds, and the prefill applies the structural
  continuation and the held-wall mirror only to margin tiles outside that
  reach. Inside it the decoded map stands, as it does on the console once
  the camera arrives. Bias, margins, culls, and the cartridge state are
  unchanged.
- **Verified (headless, private ROM, previous build as reference):**
  - A 360-frame replay (idle, right, idle, left) and an 1,800-frame boot at
    4:3, 16:9, and 21:9: the WRAM, VRAM, CGRAM, and OAM hashes are identical
    in every run. 4:3 frames are identical, and the boot's final frame is
    identical at every aspect.
  - 16:9 replay, every frame: pixels change only in the right margin
    (x 295-341) on frames 40-66 and 328-359 and the left margin (x 0-46) on
    frames 85-96 and 268-280. Those are the frames where the rule had
    invented content beside the same two trunks.
  - Pixel oracle on BG1 alone against the native 4:3 view of the same
    world columns: the left margin at replay frame 86 matches on 9,000 of
    9,000 pixels (previous build 4,852). The right margin at frame 59
    matches on 8,619 of 10,152 at the best registration (previous build
    1,047). The remainder sits along the trunk's edge, a registration
    residue between a static and a walking run. A zoomed crop shows the
    same trunk edge, gap, and ledge.
  - `test_dkc3_video` covers the reach (inside, both ends, a west hold, an
    unaligned wall, inverted bounds). Python unit tests 29/29, and Windows
    Release CTest 31/31 with every target rebuilt (one smoke run timed out
    while a second CTest ran beside it, then passed alone).
- **Risk:** inside the region's span the rule no longer continues a wall
  that holds the player while the camera could still scroll on. DKC3's
  regions bound its rooms, and the west hold keeps its own bound, so that
  case should be rare. But the region span ignores the cartridge's vertical
  overlap gates, and a rejected region (narrower than a screen) falls back
  to the level clamp, where nearly every column is in reach.
- **Unverified:** the saved-scene corpus and cave replay are not on this
  host, so the mill elevator, Squeals on Wheels, and bonus-room states
  were not replayed against this change, and no DKC3 scene where the wall
  rule is needed was available to confirm it still fires. 16:10, the live
  desktop app (rebuilt, not played here), and macOS are unverified.

## 2026-11-26 - Assist Tools / Cheats option: spawn Squawks or Enguarde crates while paused

- **Feature:** Added an opt-in, default-off **Spawn animal buddy crates from
  pause menu** checkbox under the overlay's **Assist Tools / Cheats** tab
  (gated behind **Enable Assist Tools / Cheats**, persisted as
  `PauseCrateSpawn` in `launcher.cfg`, reset to off by **Restore All Settings
  to Defaults**, and also selectable in headless runs via
  `DKC3_PAUSE_CRATE_SPAWN=1`).
- **Cartridge facts verified from generated code and ROM:**
  - In-level gameplay runs `$B3:8076` at `$004E..$0050`; in-game pause sets
    `$05AF` bit 6 (`$0040`, `$80:8A6D..$80:8B0B`).
  - `$04DA` holds the active player's newly-pressed SNES buttons each frame
    even while paused (Up = `$0800`, Down = `$0400`, Start = `$1000`).
  - `$04F9` points to the active Kong/player sprite record (`+$12` world X,
    `+$16` world Y). Non-Kong sprite slots span `$0954..$1412` (stride `$6E`,
    free when `+$00 == 0`).
  - `animal_buddy_box_main` (`$B8:EE88`, sprite type `$0250`) uses bank `$FF`
    init-scripts `$FF:65A7` for a Squawks crate (`+$5C = $023C`) and
    `$FF:6590` for an Enguarde crate (`+$5C = $0238`), executed through
    `apply_spawn_script_to_slot_direct` (`$BB:8FF1`).
- **Implementation:** `runner/dkc3_animal_crate_cheat.{h,c}` provides ROM-free
  detection and slot preparation; `Dkc3RunOneFrame` in `runner/dkc3_game.c`
  invokes `apply_spawn_script_to_slot_direct_M0X0` with saved/restored CPU
  and DP scratch state when enabled, paused in a level, Start is not newly
  pressed, and Up or Down is newly pressed. Both `runner/sdl_main.c` and
  `runner/desktop_main.c` sync the setting with `assist_tools`.
- **Verified:**
  - ROM-free unit tests: `test_dkc3_animal_crate_cheat` (env default-off,
    gating, Up -> Squawks `$65A7`, Down -> Enguarde `$6590`, slot allocation,
    full-table and invalid-pointer fail-closed) and
    `test_desktop_launcher_settings` (`PauseCrateSpawn` default-off and
    persistence).
  - Windows x64 Release build (`dkc3_snesrecomp_headless`,
    `DKC3RecompSDL.exe`, `DKC3Recomp.exe`) and full CTest suite: 32/32 passed.
  - Headless ROM verification against a paused level quick save (`$0029`,
    `flags=$40C0`, `kong=$0878`): with `DKC3_PAUSE_CRATE_SPAWN=0`, Up/Down
    spawned nothing; with `DKC3_PAUSE_CRATE_SPAWN=1`, Up spawned a Squawks
    crate (`type=$0250`, `+$5C=$023C`) and Down spawned an Enguarde crate
    (`type=$0250`, `+$5C=$0238`) at the active Kong's position, and unpausing
    for 35 frames completed cleanly (`rc=0`) with the Squawks crate active
    (`state=1`) and Enguarde (`$0238`) hatched at the player's position.
- **Follow-up fix (Squawks miscoloring on palette overflow & Squawks dismount):**
  - **Root causes:**
    1. When all 8 SNES OBJ hardware palette slots (`$073C..$074B`) are occupied
       at the moment a spawned Squawks or Enguarde hatches (for example in
       widescreen where more objects are active), `request_palette_direct`
       (`$BB:95E6`) overflows to palette 0 (`$0F` effect palette) and records
       `$0018` in `$1D4B`. Mounting Squawks then swaps `+$1E` OAM attributes
       between the active Kong slot (`$04F9`, which becomes Squawks's sprite
       with `pal=0`) and the buddy slot (`DP $007C`, which becomes the hanging
       Kong with `pal != 0`). That broke widescreen overflow tracking (which
       had been attached to the buddy slot) and left Squawks miscolored even
       after a hardware palette slot later freed up.
    2. In vanilla DKC3, `$B8:CC29` dispatches the `X` button (`$0040`) through
       the jump table `$B8:CF2F,X` (`X = $04DE`). Enguarde/Ellie/Squitter map
       to `$B8:D229` (the standard animal-buddy dismount routine), whereas
       Squawks (`mode $1D`, `$04DE = $3A`, table entry `$B8:CF69`, ROM
       `0x38CF69`) maps to `$B8:D228` (`RTS`), intentionally preventing
       dismounting from Squawks.
  - **Changes:**
    - `runner/dkc3_animal_crate_cheat.{h,c}` and `runner/dkc3_game.c`: added
      `Dkc3AnimalBuddyPaletteIdForType()` (`$0238` Enguarde -> `$0017`;
      `$023C` Squawks -> `$001E` if `$0777 & $0010` else `$0018`),
      `Dkc3DetectAnimalBuddyPaletteRepair()`, and
      `Dkc3RunAnimalBuddyPaletteRepair()`. While `pause_crate_spawn` is
      enabled, if a mounted or unmounted Squawks/Enguarde is stuck on `pal=0`
      and a hardware slot in `$073C..$074B` is free (or already holds its
      palette ID), the host invokes `request_palette_direct_M0X0` (`$BB:95C8`),
      updates the sprite's `+$1E` palette bits, and decrements `$074C`
      (`ref[0]`) if `> 1`.
    - `runner/dkc3_game.c` (`Dkc3SyncWidescreenObjPalettes`): when all 8
      hardware slots remain full in widescreen and `DP $007C` points to a
      mounted Squawks/Enguarde whose active Kong slot (`0` or `1`) is on
      `pal=0`, attach the buddy's palette ID to `s_sprite_overflow_pal[kong]`
      so the extended OBJ palette table draws Squawks accurately immediately.
    - `runner/dkc3_game.c` (`Dkc3PatchSquawksDismount`): toggles ROM offset
      `0x38CF69` (`$B8:CF69`) between `$D228` (vanilla `RTS`) when disabled and
      `$D229` (standard `X`-button dismount) when `pause_crate_spawn` is
      enabled, failing closed if the expected bytes are not present.
  - **Verified (Windows x64 Release, private ROM):**
    - Extended `tests/test_dkc3_animal_crate_cheat.c` with 5 ROM-free unit
      tests covering mounted Squawks repair, Quawks flag (`$0777 & $0010`),
      full-table fail-closed, unmounted Enguarde slot reuse, and disabled
      fail-closed.
    - Rebuilt `test_dkc3_animal_crate_cheat`, `dkc3_snesrecomp_headless`,
      `DKC3RecompSDL.exe`, and `DKC3Recomp.exe`; full CTest suite: 32/32 passed.
    - Headless ROM E2E on `saves/dkc3s0.sav` (`DKC3_PAUSE_CRATE_SPAWN=1`):
      spawning Squawks crate while paused, unpausing, jumping to hatch and
      mount Squawks (`$7C=$0D32`, `mode=$3A`, `kong=$0878 st=63 pal=4`,
      `pal0018=[(4, 1)]`), then tapping `X` (`0x200` for 2 frames) cleanly
      dismounts (`$7C=$0000`, `mode=$04`, `kong=$0878 st=01 pal=1`, Squawks
      hovering at `$0D32 st=00 pal=4`, `rc=0`).
- **All-buddy selector and pause HUD update (2026-11-26):**
  - Replaced Up/Down direct spawning with **Left/Right** selection across all
    five `animal_buddy_box` bank `$FF` spawn scripts (`$FF:6586` Ellie
    `$0234` / palette `$0016`, `$FF:6590` Enguarde `$0238` / `$0017`,
    `$FF:659D` Squitter `$0240` / `$0019`, `$FF:65A7` Squawks `$023C` /
    `$0018` or Quawks `$001E`, `$FF:65B1` Parry `$0244` / `$001A`) and **B**
    (`$8000` in `$04DA`) to spawn the selected crate. Extended
    `Dkc3AnimalBuddyPaletteIdForType()` to cover all five animal buddies.
  - Added `Dkc3PauseCrateHudLabel()` (`runner/dkc3_game.{h,c}`) and
    `Dkc3DesktopOverlaySetPauseBanner()` (`runner/desktop_overlay.{h,cpp}`,
    wired in `runner/sdl_main.c` and `runner/desktop_main.c`) to draw the
    selected animal buddy name (`< Name >`) in a centered pill at the bottom
    middle of the screen while paused in a level with the cheat enabled.
  - **Verified (Windows x64 Release, private ROM):**
    - Updated `tests/test_dkc3_animal_crate_cheat.c` (all 5 crate names/scripts,
      Left/Right wrap, `B`-button spawn, palette IDs for all 5 buddies + Quawks).
    - Rebuilt `test_dkc3_animal_crate_cheat`, `dkc3_snesrecomp_headless`,
      `DKC3RecompSDL.exe`, and `DKC3Recomp.exe` cleanly; full CTest: 32/32
      passed.
    - Headless ROM verification on `saves/dkc3s0.sav`: cycling Right + B
      spawned all five crates (`Ellie`, `Enguarde`, `Squitter`, `Squawks`,
      `Parry`) in order when enabled and none when disabled; wrapping Left from
      `Ellie` to `Parry`, pressing B, and unpausing hatched Parry cleanly
      (`rc=0`).
- **Unverified:** interactive visual check of the bottom-middle HUD pill in
  the live desktop app, macOS rebuild, and spawning/dismounting across all
  levels/water states.

## 2026-11-27 - black margins in a one-screen rope shaft (kept by decision)

- **Report:** in the release quick save (SHA-256 `d33c5659...064d`, level
  word `$00C0` = `$0041`, 16:9 with `glide`), the Kongs hang on the ropes of a
  cliff shaft. Both margins are black although the cliff looks continuous.
- **Measurement:** the camera is at `[$01E8,$1A09]` in region `$0478` = `$2C`
  (index 22, x `$01E8..$02E8`, exactly one screen, no side links). The trace
  reads `walls=[01e8,01e8] margins=[0,0] bias=0` with every margin cell
  present and matching the decode, so the camera-region clamp
  (2026-11-22) closes the margins on purpose. On the same rows, region 12
  (x `$0100..$01E0`) ends 8 pixels to the west and region 35
  (x `$0300..$0400`) starts 24 pixels to the east.
- **Counterfactual (temporary, uncommitted headless build that skipped
  `Dkc3VideoCameraRegionSpan`):** the walls became `[0100,0300]`, the margins
  `[43,43]` at 16:9 and `[95,95]` at 21:9, and the bias 3. The native 256
  columns were pixel-identical to the 4:3 frame at that offset. Both margins
  show continuous cliff art with the ropes carried through. At 21:9 the right
  margin also shows region 35's rope rows, a barrel, and enemies, which the
  cartridge shows only once the camera reaches that room.
- **Decision (owner):** keep the margins black. The region table does not
  distinguish this shaft from the mill elevator shaft, whose neighbouring
  rooms do not line up. Any rule that opens one-screen regions needs its own
  structural evidence (for example flanking regions on the same rows within
  a few pixels, with terrain continuous at the seam). It must also be judged
  against the elevator, Squeals on Wheels, and bonus-room states.
- The temporary switch was reverted and the headless runner rebuilt. The
  rebuilt runner again reports `margins=[0,0]` for this save.

## 2026-11-27 - bear coin drawn orange after a quick load (overflow recovery)

- **Report:** in the release quick save (SHA-256 `acee01f7...e411b`, level
  word `$00C0` = `$0045`, 16:9), Kiddy rides an Ellie in a cave and the
  spinning coin to the right of the Kongs loads in corrupted.
- **Measurement:**
  - The coin is slot 2 (`$0954`), type `$0284`, handler `$BB:D24E`
    (`bear_coin_main`). Its OAM attribute `$20E0` selects palette 0. The
    hardware palette table `$073C` is `[$0F,$01,$02,$74,$1C,$6D,$00,$16]`,
    and Ellie's `$16` is one of them, so the coin's request overflowed to the
    common `$0F` palette. At 4:3 the coin's tiles show through `$0F` as
    red/white/yellow noise, which is the cartridge's own fallback.
  - At 16:9 `DKC3_EXT_PAL_TRACE` shows the coin tracked as `2=0071`. A quick
    save does not carry the overflow hook's associations, so the 8-frame
    load recovery attached `$1D4B` (the most recent overflow, `$0071`, an
    orange ramp) to the only palette-0 dynamic sprite in OAM. The orange coin
    is that wrong palette.
  - All four bank-`$FF` init scripts that set type `$0284` (`$FF:6235`,
    `$624E`, `$6264`, `$627D`) request palette `$00C8` with command `$FC`.
    Drawing the coin's VRAM tiles through `$00C8` gives a clean silver bear
    coin. Through `$0071` they are orange and through `$000F` they are noise.
  - Init-script format, from the ROM: `$BB:8FF1` runs scripts with DB =
    `$FF`. `$BB:9005` writes commands below `$EF` to field `D+op`, and the
    table at `$BB:9018` sends `$FF` (`$9062`), `$FE` (`$9071`), `$FC`
    (`$9119`, `JSR $95C8` request_palette_direct), `$FB` (`$9137`), and `$FA`
    (`$917E`) through `$9002`, which advances three bytes. `$EF` (`$9041`)
    and `$F0` (`$9048`, which also sets priority 2 but keeps the palette
    bits) end a script.
- **Change:** `Dkc3VideoInitScriptPaletteForType` (`runner/dkc3_video.c`)
  walks every bank-`$FF` run that opens with `$FF type` using only those
  commands. It returns the palette only if every run reaches a terminator
  and requests the same palette exactly once. An unknown command (such as
  the `$F9` sub-script call), a missing terminator, no palette, or a
  disagreement returns 0. On the release ROM 66 types have an answer,
  including Kongs `$022C`/`$0230` -> `$01`/`$02` and Ellie `$0234` -> `$16`,
  which match the hardware slots. In `Dkc3SyncWidescreenObjPalettes`
  (`runner/dkc3_game.c`) the load recovery first attaches that palette to
  each palette-0 dynamic sprite whose type has one (unless it is `$0F`).
  The `$1D4B` fallback now considers only sprites whose type the scripts do
  not settle. The live overflow hook, the 4:3 path, and guest memory are
  unchanged.
- **Verified (headless, private ROM, previous build as reference):**
  - The save, 300 frames at 4:3, 16:9, and 21:9: WRAM, VRAM, CGRAM, and OAM
    hashes are identical in every aspect, and every 4:3 frame is identical.
    At 16:9 and 21:9, 299 of 301 frames differ, only inside the coin's box
    (x 279-302 / 331-354, y 55-85). The trace now reads `2=00c8`, and a
    crop strip shows the silver bear coin spinning.
  - 1,800-frame boot at 4:3 and 16:9: frame, WRAM, VRAM, CGRAM, and OAM
    hashes identical.
  - `test_dkc3_video` covers the lookup: two scripts with `$EF` and `$F0`
    ends, an absent type, and refutation by a disagreeing palette, no
    palette, two palettes, an `$F9` call, and an unterminated run. Windows
    Release CTest 32/32.
- **Not changed (faithful):** at 4:3 the coin keeps the cartridge's `$0F`
  fallback until it respawns. The overflow comes from the eighth palette
  slot being taken, here most likely by an Ellie the pause crate spawner put
  in a level that does not budget for her (not confirmed: the save does not
  record how Ellie arrived).
- **Unverified:** the saved-scene corpus and cave replay are not on this
  host. A sprite type that switches palette after its init script runs
  could be recovered with its initial palette for the rest of its life. The
  live desktop app was rebuilt but not played at this spot, and macOS is
  unverified.

## 2026-11-27 - Assist Tools / Cheats option: show hidden objects

- **Report:** in the release quick save `saves/dkc3s0.sav` (Kongs on a
  cliff ledge, 4:3), an invisible coin sits above the Kongs. Request: an
  assist option, off by default, that draws invisible objects.
- **Measurement:**
  - The coin is slot 3 (`$09C2`), type `$0284`, handler `$BB:D24E`
    (`bear_coin_main`), OAM attribute `$2760` (palette 3, holding `$00C8`
    with one reference), graphic `+$24` = `$274C`, and render word
    `+$26` = `$C000`. Its behavior still runs: it animates and is collected
    on contact.
  - The sprite renderer loop at `$B7:8857` reads `+$26` at `$B7:8908`.
    With bit 15 clear it draws at `$B7:8910` (camera-relative). With bit 15
    set, `$B7:89C4` shifts the word. If bit 14 is also set it jumps to
    `$B7:89E5`, which copies `+$20` to `+$22` and draws nothing. Otherwise
    bits 1-4 index the variant table at `$B7:89CF`; variant 0 (`$B7:89EE`)
    is a screen-space draw. In bank `$B7`, `$B7:8908` is the only
    `LDA $0026,Y` that gates drawing; the other eight read the palette bits.
  - The block holding that read (trace `$B7:8904`) is inlined into twelve
    generated units: `CODE_B78857` and every renderer variant that loops
    back to the top.
- **Change:**
  - `adapt_hidden_sprite_reveal` in
    `scripts/apply_dkc3_widescreen_overrides.py` wraps that one `+$26` read
    in every `$B7:8904` block as
    `Dkc3RevealHiddenSpriteFlags(word, cpu->Y)`. It fails closed if no
    block is found or a block has a different number of `+$26,Y` reads
    than one. On the release ROM it wraps all twelve.
  - `Dkc3ShowHiddenRenderFlags` (`runner/dkc3_show_hidden.c`) returns the
    word unchanged unless the option is on and `(word & $C000) == $C000`.
    It then clears bit 15, so the sprite takes the normal camera-relative
    draw. The Kong slots `$0878`/`$08E6` and the ridden buddy's slot
    (DP `$7C`) stay hidden, because the game draws those sprites on the
    Kong slot. Guest memory is never written by the helper.
  - The option is **Assist Tools / Cheats > Show hidden objects**, off by
    default, saved as `ShowHiddenObjects` in `launcher.cfg`, and only on
    while **Enable Assist Tools / Cheats** is on.
    `DKC3_SHOW_HIDDEN_OBJECTS=1` turns it on for headless runs.
- **Verified (headless, private ROM, Windows Release):**
  - The quick save with the option off, 4 frames: frame, WRAM, VRAM,
    CGRAM, and OAM hashes are identical to the previous build.
  - With the option on, 30 frames: only the box x 140-155, y 22-48
    differs. It is the coin, drawn as OAM 23-26 (tiles `$160`-`$164`,
    palette 3). Frames 30/34/38/42 show the silver bear coin spinning.
    That matches the `$00C8` palette noted in the overflow entry above.
    WRAM differs in 47 bytes: the slot's `+$20`/`+$22`, the OAM buffer,
    and renderer scratch, which is what any drawn sprite writes.
  - `test_dkc3_show_hidden` covers the env parse, the reveal, untouched
    normal/variant/bit-14-only words, and the Kong and ridden-buddy
    exclusions. `test_desktop_launcher_settings` covers the default and the
    round trip. `test_apply_dkc3_widescreen_overrides` covers the wrap in
    each renderer fixture, idempotence, and both fail-closed cases.
    Windows Release CTest 33/33.
- **Unverified:**
  - Other hidden sprite types were not surveyed. The option shows any
    non-Kong slot the game hides this way, and one that never asked for
    VRAM or a palette could draw with whatever those slots hold.
  - The live desktop app was rebuilt but not played. The saved-scene
    corpus, cave replay, and macOS are not checked.
  - The overlay checkbox was not exercised in a window.

## 2026-11-27 - Fix world-map crash with Show hidden objects on

- **Symptom:** after finishing a level, once the world-map icons start
  flashing, the runtime stopped with `FATAL: native runtime stopped
  unexpectedly` (`resume_pc = $8089a0` / `$8089a3`; diagnostic bundles
  `diagnostic_bundle_20261128_004643_26796`, `..._010916_27536`, and
  `..._011308_13872`). The owner reproduced it from a fresh release quick save.
- **Measurement (headless, private ROM, Windows Release):**
  - The quick save fails at host frame 390 with `DKC3_SHOW_HIDDEN_OBJECTS=1`
    in 4:3 and 16:9, with or without the crate spawner. With the option off
    it completes 1,800 frames.
  - Every frame ends in `$80:8992`, which reads the OAM write pointer from
    DP `$82`, stores `$F0FF` and steps X by 4 until X equals `$0400`. Both
    resume PCs lie in that loop, so `$82` was left past `$0400` or
    misaligned and the loop never ends.
  - On the map (main loop vector DP `$4E` = `$B4:B179`) the reveal unhid
    three slots: two type `$00A4` icons (`+$1E` = `$0920`, graphics `+$24` =
    `$0596`/`$059B`) and the controller in slot `$1034`, type `$0198`, with
    `+$1E` = `$3000` (priority only: tile base 0, palette 0) and `+$24`
    counting `$0001`, `$0101`, `$0201`, ... . `+$24` is the graphic id the
    renderer reads at `$B7:8885`, so the controller was drawn from frame
    data that was never a graphic.
  - The bear coin this option was made for has `+$1E` = `$2760` and real
    graphic ids `$2733`-`$2747`, in a level (`$4E` = `$B3:8076`).
- **Change:**
  - `Dkc3ShowHiddenSlotEligible` (`runner/dkc3_show_hidden.c`) allows a
    reveal only when the main loop vector is `$B3:8076` (the level check the
    crate spawner also uses) and the slot's OAM attribute `+$1E` has a
    nonzero tile base or palette (`& $0FFF`). `Dkc3RevealHiddenSpriteFlags`
    returns the word unchanged otherwise.
  - `test_dkc3_show_hidden` covers the level, world-map, priority-only,
    empty, palette-only, and tile-only cases.
- **Verified (headless, private ROM, Windows Release):**
  - The owner's crash quick save, 1,800 frames, 16:9 with the crate spawner
    and 4:3 without: completes, and frame, WRAM, VRAM, CGRAM, and OAM hashes
    are identical with the option on and off.
  - The cliff quick save, 30 frames, 4:3: option on still draws the silver
    bear coin (the only frame difference is the box x 157-180, y 22-50).
  - Python unit tests 31/31; Windows Release CTest 33/33.
- **Unverified:**
  - Other hidden types inside levels are still not surveyed. A level slot
    that holds a tile base or palette but no real graphic would still be
    drawn. The saved-scene corpus and cave replay are not on this host.
  - The live desktop app was rebuilt but not played through this map
    transition. macOS is not checked.

## 2026-11-27 - KAOS arena (level `$23`) stayed pillarboxed

- **Symptom:** the owner's release quick save (SHA-256
  `dcf29db3504b0c86b0ba610132a1a633fb96617b84082013770e879d29c46fd0`)
  shows the KAOS arena with black margins at every wide aspect (app at
  16:9, glide edges).
- **Measurement (headless, private ROM, Windows Release):**
  - Level `$23`, Mode 1, maps BG1 `$78` (32x32), BG2 `$71` (64x32), BG3
    `$7C` (32x32), camera `$0170,$0100`, region 1, terrain stream base
    `$1969` = `$7800`.
  - Layer isolation: BG2 is the whole room, one static 512-pixel plane the
    band classifier already accepts (`P71` on every frame). BG1 is KAOS's
    body, scrolled independently. BG3 is the bounded sky.
  - The stream base points at BG1's 32-column map, which is not a wide
    layer, so `Dkc3VideoTerrainLayer` finds no owner, terrain readiness stays
    false, and the not-ready path blacks out both margins.
  - KAOS's body reaches the native left edge (BG1 scroll `$67`). HDMA
    channel 2 writes window 1 (`$2126/$2127`, mode 1, direct) per scanline,
    and BG1 uses window 1 inverted on the main screen (`W12SEL` low nibble
    `$3`, `TMW` bit 0). On the body's lines the window is the body map's
    256-pixel extent clamped to the screen: `[6,255]` at scroll 250,
    `[0,153]` at scroll 103. The cartridge clips the body's wrap with it.
- **Change:**
  - `Dkc3VideoUsesKaosArenaPlane` gates on level `$23`, Mode 1, maps
    `$78/$71/$7C`, every band of BG2 classified as a plane, and
    `DKC3_CULL_WIDEN`. On the not-ready path the arena draws BG2 raw over the
    physical span (plane band policy), repeats BG3 at its hardware period,
    and does not blank the margins. Readiness stays false, so culling,
    camera, and bias are native.
  - `Dkc3VideoKaosArenaMargins` limits each margin to BG2's 512-pixel map,
    so the view never shows the plane's wrap. Beyond it is black: up to
    25 px at 21:9 at the west end of the fight.
  - `Dkc3VideoKaosBodyWindow` scans the window-1 HDMA table for the pair
    that matches BG1's scroll (within 2 px) and returns the unclamped
    extent. The PPU then gets it through `Dkc3PpuSetWsWindowUnclamped`
    (applied after the trunk-window reset) and BG1 joins the wide mask, so
    the body continues into a margin and its wrap stays hidden. With no
    match, BG1 stays inside the native window.
  - `test_dkc3_video` covers the scene gate, the margins at both plane ends,
    and the window scan, including rejection.
- **Verified (headless, private ROM, Windows Release):**
  - 1,803-frame replay (Start, walk right and left, jump, idle), sampled
    every 6 frames against the previous binary: 4:3 frames and final frame,
    WRAM, VRAM, CGRAM, and OAM hashes are identical. At 16:10, 16:9, and
    21:9 only the frame hash changes. Every sampled wide frame's native
    256 columns equal the 4:3 frame. With `DKC3_CULL_WIDEN=0`, every sampled
    frame and every hash equals the previous binary.
  - Margin oracle, 600-frame replay at 21:9: for 10,120 BG2 margin columns,
    each one equals a native column showing the same plane x on another
    frame. The 214 residual columns all come from frames with the camera
    moving, where the band trace's scroll trails the rendered one by 1 to 2
    px. With that shift every one matches.
  - BG1-only render over the 1,803 frames: the body forms a single run of at
    most 256 columns in every frame (no copy). In 413 of 451 samples it
    extends into a margin.
  - The Squeals on Wheels quick save and an older release state are
    bit-identical, old vs new, at 16:9 and 21:9 over 601 frames.
  - Windows Release CTest 33/33.
- **Unverified:**
  - Only this save and a 30-second segment of the fight were checked, not
    later phases or the fight's end. A phase with other maps or no matching
    window pair falls back to black margins or a native-only body.
  - The live desktop app was rebuilt but not played. The saved-scene corpus,
    cave replay, and macOS are not on this host.
  - The Windows smoke tests (`supplied_rom_windows_*_smoke`, save/load on)
    wrote over `build-windows/Release/saves/dkc3s0.sav`, because the SDL host
    anchors saves to the executable's folder. The owner's save was restored
    from a copy taken first. Those tests still need an isolated save
    directory.

## 2026-11-27 - Windows smoke tests isolated from the release saves

- **Problem:** the `supplied_rom_windows_*_smoke` tests run the release
  `dkc3_snesrecomp_sdl.exe` with save/load on. The SDL host anchors its
  working directory to the executable's folder, so each run rewrote
  `build-windows/Release/saves/dkc3s0.sav`, and also `launcher.cfg` and
  `rom.cfg` there.
- **Change:** on Windows and Linux the SDL host now honours `DKC3_USER_DIR`,
  as the macOS host already did. When it is set, the host creates that one
  directory (its parent must exist), enters it instead of the executable
  folder, and keeps reading launcher assets from the executable folder.
  When it is unset, nothing changes. Each Windows smoke test sets its own
  `build-windows/windows-test-user-<aspect>` directory.
- **Verified:** Release rebuilt; CTest 33/33 passed. SHA-256 of every file
  in `Release/saves` plus `Release/launcher.cfg` and `Release/rom.cfg` is
  identical before and after the run, and the quick state, config and
  diagnostics land in the per-aspect test directories.
- **Unverified:** the Linux build of this path; nothing tests the
  `DKC3_USER_DIR` failure messages.

## 2026-11-27 - KAOS arena, K. Roolenstein phase, back to 4:3

- **Symptom:** a later release quick save of the level `$23` fight (K.
  Roolenstein on foot) was pillarboxed again.
- **Measured:** Mode 1, `bgXsc=[71,7c,7c]`, main `$15`; the band classifier
  proves BG1 a static `$71` plane on every band; `$1969` is still `$7800`,
  so no layer owns a terrain stream. The room plane moved from BG2 to BG1 and
  BG2/BG3 now share the 32-column map `$7C`. The first gate required
  `[78,71,7c]`, so it rejected this phase. Over a 1,100-frame replay, BG1
  scroll stayed in 41..137 (the plane never wraps), and whenever the window
  left edge was non-zero, BG1 scroll + W1/W2 left = 75 exactly. The
  cartridge's own windows keep BG2/BG3 off the room's west wall, and the
  runtime's pinned-edge expansion carries them into the margins.
- **Change:** `Dkc3VideoKaosArenaPlaneLayer` replaces the boolean gate and
  returns the room's layer: 1 for `[78,71,7c]`, 0 for `[71,7c,7c]`, and only
  when that layer's plane count equals the band count. For layer 0 the host
  draws BG1 raw, bounded to the plane's `[0,512)` from BG1's scroll, and
  repeats BG2 and BG3 at their hardware period. The KAOS branch is
  unchanged.
- **Verified** (headless, 183 frames sampled every 6 over 1,100, against the
  previous binary):
  - 4:3 frames are identical.
  - The native centre of 16:10, 16:9, and 21:9 equals 4:3 in all 732
    checks.
  - WRAM/VRAM/CGRAM/OAM are identical across aspects.
  - `DKC3_CULL_WIDEN=0` at 21:9 is identical.
  - The KAOS-phase save replay is identical at 16:9 and 21:9.
  - Unit tests and CTest 33/33 pass; release saves are unchanged.
- **Limits:** at the west end (BG1 scroll 41), 21:9 shows 54 black columns
  left of plane column 0 (16:9: 2), by the same plane-end rule as the KAOS
  phase.
- **Unverified:** the live app; any later phase or defeat sequence with
  other maps.

## 2026-11-27 - K. Roolenstein phase: floor effects cut off in the left margin

- **Symptom:** the owner's release quick save (SHA-256
  `fde3da52cd78dc2eb20ed6132a76d537751df88f221fac48313634aa46f1b8da`, level
  `$23`, K. Roolenstein phase, camera `$01F8`) shows the left side cut off
  at 16:9: the floor electricity and haze stop at native x 0.
- **Measured (headless, private ROM, Windows Release):**
  - Layer isolation (`SNESRECOMP_LAYER_MASK`): BG2 (subscreen, added) is the
    electricity, BG3 (main) the haze and sky. At 16:9 the left margin has
    neither, and BG2 leaves a stray 8-column sliver in the right margin.
  - `W12SEL` = `$0C30`: BG2 uses window 1 inverted (sub only), BG3 window 2
    inverted (main only). No HDMA writes the window registers in this phase.
  - The previous change repeated BG2/BG3 at 256 pixels. The left margin was
    copied from native columns 213..255, which window 1 (`[0,188]`) and
    window 2 (`[0,100]`) hide, so it came out empty; the right margin copied
    columns 0..7, which the windows show, past the window's own east edge.
  - Over a 900-frame replay (walk left, right, left), camera + right edge is
    692 (W1) and 604 (W2) on 741 of 764 windowed frames, 1-3 px less on the
    rest while the camera moves. With BG1 scroll = camera - 256, the windows
    are room x `[75, 436]` (W1) and `[75, 348]` (W2), matching the earlier
    measurement BG1 scroll + left = 75.
- **Change:**
  - `Dkc3VideoKaosRoomWindow` (`runner/dkc3_video.c`) returns a window's
    unclamped span from its registers and BG1 scroll. A register pinned at 0
    or 255 takes the room edge (`75 - s`, `east - s`), only if that lies at
    or past the same screen edge; an unpinned register must be within 2 px
    of the room edge. Otherwise it returns false.
  - In the layer-0 arena branch, when both windows pass, BG2 and BG3 join the
    wide mask (BG3 widened from line 1) and are drawn into the margins from
    their 32-column map, the hardware wrap; the repeat is off. Both
    unclamped spans go to `Dkc3PpuSetWsWindowUnclamped` after the trunk
    window reset. If either fails, BG2/BG3 stay inside the native window.
  - `test_dkc3_video` covers pinned left, pinned right, the 2 px tolerance,
    and rejection.
- **Verified** (900-frame replay from the save, sampled every 6 frames,
  against the previous binary):
  - 4:3 frames and all hashes are identical.
  - At 16:10, 16:9, and 21:9, 127 of 150 sampled frames change (the
    others are the closing transition). WRAM/VRAM/CGRAM/OAM hashes are
    identical, and the native centre equals 4:3 in 450/450 checks.
  - `DKC3_CULL_WIDEN=0` at 21:9 is identical.
  - At 16:9 and 21:9 the electricity and haze now run unbroken across the
    left margin.
  - Windows Release CTest 33/33. Release saves unchanged.
- **Unverified:** the replay never brings the west wall into a margin
  (camera stayed in `$01D1..$01F8`), so the pinned-left case with the wall
  on screen in a margin is covered only by the unit test. The KAOS-phase
  save is not on this host, but that branch is unchanged. The live app was
  rebuilt but not played.

## 2026-11-27 - Show hidden objects drew the defeated K. Rool in the floor

- **Symptom:** in a release quick save at the end of the level `$23` fight,
  K. Rool stood half inside the floor.
- **Measured (headless):** `launcher.cfg` had `ShowHiddenObjects=1`. K.
  Rool is slot `$09C2`, type `$0118`, `+$1E` = `$26C0`, graphic `$8019`.
  The game hides him after the fight with `+$26` = `$C000` and leaves his
  last frame and position in place. With the option off, his 16 OAM
  entries (palette 3, priority 2, y 176-227) are gone from the second
  frame after the load. With it on, they are still drawn at frame 59. The
  priority-2 sprite sits over the low-priority floor tiles below row 191,
  as Mode 1 orders them. This is not a widescreen effect: 4:3 and the
  native centre of 16:9 match.
- **Change:** `Dkc3ShowHiddenTypeEligible` (`runner/dkc3_show_hidden.c`)
  refuses type `$0118`, and `Dkc3RevealHiddenSpriteFlags` checks the slot's
  `+$00` type after the existing level and attribute checks.
- **Verified:**
  - With the option on, frame 60 of the save is byte-identical to the
    option-off frame and has no K. Rool sprites; the previous build drew 16.
  - With the option off, 600 frames match the previous build at 16:9.
  - `test_dkc3_show_hidden` covers both types; CTest 33/33 passes; release
    saves are unchanged.
- **Unverified:** the bear-coin reveal was not replayed (no coin save at
  hand; only the unit test covers type `$0284`). Other hidden types are
  still not surveyed, so another object hidden after leaving play could be
  drawn the same way.

## 2026-11-27 - Pause crate selector no longer appears during the credits

- **Report:** with **Spawn animal buddy crates from pause menu** on, the
  ending credits showed the `< Ellie >` selector pill between scenes, as if
  paused.
- **Measurement (release quick save in the credits):** the credits run on
  the level main loop (`$004E` = `$B3:8076`, level word `$00C0` = `$000D` /
  `$000B`) with `$05AF` = `$80C0`, so the frozen bit (`$0040`) the cheat
  read as "paused" is set while the credit text is shown. Bit 15 (`$8000`)
  stays set through the whole credits and is clear in a player pause
  (`$40C0` in the earlier paused level save); `$BB:A19F` returns early on
  it.
- **Change:** `Dkc3IsPauseCrateMenuActive` also requires `$05AF & $8000`
  clear. That gate covers the HUD pill, Left/Right selection and B
  spawning.
- **Verified:** new unit cases (`$80C0` fails closed and does not move the
  selection; `$40C0` is still accepted); full CTest 33/33. Headless run
  of the credits save for 60 frames with B, Right, B pressed: WRAM is
  byte-identical with `DKC3_PAUSE_CRATE_SPAWN=0` and `=1`, and no
  `$0250` crate slot exists.
- **Unverified:** a live pause inside a level was not replayed after this
  change (the paused level save was overwritten by the credits save; the
  unit test covers it). Whether the title attract demo also sets bit 15
  was not checked. `DKC3RecompSDL.exe` did not relink because it was
  running; `DKC3Recomp.exe` and the headless runner were rebuilt.
