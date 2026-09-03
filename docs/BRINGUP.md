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
