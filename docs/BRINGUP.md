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

