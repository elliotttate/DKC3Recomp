# Widescreen field guide

How the Donkey Kong Country recompilations present a wider view than the
cartridge drew, how that presentation goes wrong, and how each kind of
defect was found and fixed across DKC1Recomp, DKC2Recomp, and DKC3Recomp.
Read this before touching the terrain path, the band policies, the edge
policy, or the object windows. `docs/BRINGUP.md` (this repository) and
DKC2Recomp's `docs/ARCHITECTURE.md`, `docs/HARDWARE_NOTES.md`, and
`docs/WIDESCREEN_DIAGNOSTICS.md` hold the dated evidence this guide
condenses.

## 1. The rules that do not bend

- **The native 256x224 frame is the oracle.** Nothing inside the
  cartridge's own window may change. Every fix is judged first by whether
  the 4:3 run is bit-identical before and after (frame, WRAM, VRAM, and OAM
  hashes), and only then by what the margins show.
- **Never repeat, wrap, or invent unverified side art.** A margin cell is
  served from a proven source or it is black. "It looks plausible" is not a
  source. A verified transparent tile is an honest hole; a stale rolling
  VRAM page is a lie.
- **The view stays locked to the cartridge camera.** Any presentation bias
  that diverges from the camera makes sprites and the HUD drift against the
  background and players notice at once. The one sanctioned bias is the
  glide at a level wall, released one pixel per eight of camera travel.
- **Level-name cards and other static pictures get black margins**, never
  mirrored or wrapped. "Not widescreen at all" on a card is expected; the
  report is about the level behind it.
- **The console's behavior is the only authority on behavior.** An enemy
  that idles or never attacks in the recomp is a recompilation defect
  (usually a dispatch table), not a presentation one, and a 4:3 run of the
  recomp showing the same idle proves nothing.
- **Private evidence stays private.** ROMs, saves, captures, and generated
  code never enter a repository. Evidence lives under `.cache/` or the
  scratchpad; the record of it lives in the docs.

## 2. The model on one page

**Geometry.** 16:10 adds 26 source columns per side (308x224), 16:9 adds
43 (342x224). The authentic center is never scaled.

**Ring versus world.** The cartridge scrolls a rolling 64x32 tilemap (the
ring) and streams columns into it as the camera moves. Columns beside the
native window are whatever was last written there: the opposite page, a
previous row, or the last level. The margins therefore never read the ring
directly. The runtime keeps a world-keyed store (`ws_shadow` in the engine
fork): every cell is addressed by its world tile, filled by capturing the
native window each frame, by attributing the game's VRAM uploads to world
chunks, and by prefilling from the decoded level map.

**Where a margin cell comes from, in order of trust.** An exact world or
history hit; an exact periodic fold of the current native row (bounded
32-column maps, whose 256-pixel hardware wrap is real); the verified
transparent tile; raw wrapped VRAM. The last is the only unsafe source and
the audits expect it to be zero.

**Prefill.** The adapter decodes the decompressed level map and metatile
definitions from WRAM (DKC2: bank `$9A`; DKC3: `$7F:0000`) into the store
for every tile the widened view can reach, one guard tile beyond each
margin. The decoder knows the map layouts (column-major with 16 rows, and
row-major with 32, 64, 160, or 192-byte rows) and a per-frame stride
calibrator confirms a row-major stride against the staged native window.

**Phase.** Store keys derive from the PPU scroll actually rendered, not
the WRAM camera, which can lead by a few pixels at the NMI boundary. When
HDMA rewrites the scroll on every line (Slime Climb's water, Toxic Tower's
skew) the frame-start register is meaningless and the terrain phase is
taken from the band covering the most lines at the camera phase.

**HDMA bands.** The band scanner records each scanline band's layer maps,
scrolls, and enables. Each band gets a policy: *World* (served from the
store), *Repeat* (the native line repeated at its proven period, for
bounded and periodic maps), *Plane* (a static 64-column map authored to
wrap, or a map used as an object plane, drawn raw so its own wrap
continues into the margins), or clamped. Layer masks for these policies
use the union of the frame-start enables and every band's enables.

**Edge policy.** `reflect`, `bars`, `shift`, and `glide`; glide is the
default in both games. Near a wall the glide slides the frame so the void
beyond the authored world never shows; the slide exposes ring columns the
store must serve (`WsShadowSetNativeViewportInset`) and the PPU decodes
nine-bit sprite X as positive up to 256 + extra + bias. A wall can be a
camera bound or a map-derived hold (void beside the window and the player
pinned there).

**Objects.** The cartridge activates, releases, and culls objects against
its 256-column screen with a little slack. The overrides script rewrites
the generated C for those windows to use the presented view; the helpers
add `extra - bias` on the left and `extra + bias` on the right, once
terrain is proven. Grid renderers that pack their own OAM take the ninth X
bit from the sign and need it promoted for the right margin.

## 3. Facts a game needs

Everything above keys on cartridge facts. Wrong facts produce plausible
pictures that are subtly wrong, so each was proven against live memory and
the disassembly before use.

| Fact | DKC2 | DKC3 | Trap met |
| --- | --- | --- | --- |
| In a level | mode word | main-loop vector `$4E` == `$B3:8076` | `$04C4` is 1P/2P, not a mode |
| Camera | WRAM camera | `$196D`/`$1973` | the copies, not the working `$0493` |
| Camera clamp | `$0AFC` | `$04BC`/`$04BE` | `$B4`/`$B6` are HUD words |
| Level map | bank `$9A` | `$7F:0000`, base `$0000` | a base of `$FF00` compensated the `$100` world origin twice |
| Metatile table | fixed | word `$1967` | 32-byte definitions, DKC2's form |
| Ring VRAM base | `$17B6` | word `$1969` | `$04F8` is a moving upload pointer |
| Map shape | scroll sub-mode `$0529` | nibble `$0470` | shape 1 (32-row column-major) has no decoder |
| World origin | `$100` | `$100` | the presentation subtracts it itself |
| Sprite table | own | `$0878`, stride `$6E`, x `+$12`, y `+$16`, param `+$0A` | |

The double-origin trap deserves its own line: `Dkc3VideoResolveEdgeTile`,
the metatile classifier, and the stride calibrator all subtract the
32-tile origin on the way from a world tile to a map tile, so the map base
fact must be the raw address of the map's first column. Every margin read
one 256-pixel page early until it was.

## 4. Symptom to cause

| What the player sees | Where to look first | What it was |
| --- | --- | --- |
| Margin shows content from exactly 256 or 512 px away; a roof stops at the native edge and reappears in the far margin | pixel oracle: margin vs native columns 256 px away on one layer | map base off by a page (DKC3); store keyed a page off; ring wrap shown raw |
| Blocks of unrelated tiles in both margins | prefill trace: matching far below present; stride sweep in Python | wrong row stride or layout for this stage (Bramble Blast `$002D`, Arctic Abyss) |
| Margins look like the native line repeated on a scrolling layer | band dump: per-band h/v vs frame-start | HDMA sets the scroll per line; phase must come from the bands (Slime Climb) |
| Parallax or foreground cut at the 4:3 edges | band dump: the band's map, whether it was written recently, its period | static 64-column plane repeated at 256 (Red-Hot Ride rocks); needs the plane policy |
| A "sprite" cut at the edge that is not in OAM | layer-isolated render, then that map's populated pages | object plane on a BG (Kackle) |
| Texture flashes for a frame at the edge | signature search across a scripted run, then the band dump at that frame | band policy or phase for one band |
| Effect present in no isolated layer | VRAM map diffs across frames | sub-screen BG under color math (lava steam) |
| Rope, steam, or another streamed effect ends falsely in a margin | is the ring ever valid beside the view for this layer? | zero-lead streamer; decode the ROM map into its own store layer and verify per frame (rigging, geysers) |
| Backdrop through a hole beside a held wall | classifier fill map ('.', '+', '#') for the source window | player-held wall with void beyond; structural wall rule, mirror on rejected rows |
| Wrong strip of tiles at one native edge while the view is slid | compare glide against reflect per world column | biased ring tail not served by the store (inset) |
| Abrupt jump stepping away from a wall | | endpoint bias; replaced by glide |
| Objects pop in inside the margin, or vanish while still visible | spawn trace, OAM trace, before/after with widening off | activation window, release window, or renderer cull not widened |
| Banana arcs or other tile rows appear only at the native edge | OAM census for first appearance at x in [256, 266) | separate grid renderer with its own clips and sign-derived ninth bit |
| Objects cut exactly at the authentic margin near a wall | | PPU nine-bit X limit must include the bias |
| Floor missing, level "loads" only at 4:3, moves when walking | prefill trace `ready` | a fact read from the wrong word (the moving upload pointer) |
| No widescreen at all, black margins, in play | classifier | unknown layout for this stage; add the case after the stride sweep |
| Enemy idle, never attacks, disappears when walked away from | generated dispatcher `_disp_n`, the state table | `JSR (abs,X)` table cut at a null slot; 14 declared entries |

## 5. The workflow

1. **Reproduce headless at the same camera.** Take the player's save
   (absolute path; the app changes directory) and a scripted input
   (`SNESRECOMP_INPUT_PLAY`, lines of `mask*count`; 0x80 Right, 0x40
   Left, 0x01 B, 0x02 Y, 0x08 Start). If the report is a screenshot, read
   the camera from a landmark: display pixels per world pixel = window
   width / frame width; the landmark's world x comes from the decode or
   from a native frame that contains it. Find the corpus frame with that
   camera in the prefill trace. A save taken on the level-name card needs a
   Start press first.
2. **Isolate.** Render the frame per layer (`SNESRECOMP_LAYER_MASK`, bit 0
   BG1 to bit 3 BG4, bit 4 OBJ) and crop the edges at 3x or 4x. Judge by
   zoomed crops of the changed region, never by a full frame: a 10x24
   pixel patch at a corner is invisible at 1x, and a whole conclusion was
   once drawn from a misread crop.
3. **Measure before theorizing.** The cell dump gives ring, decode, store,
   and status per cell; the band dump gives every band's maps and scrolls;
   the prefill trace gives ready, bias, and matching counts per frame; the
   spawn and OAM traces give object timing. Pixel oracles decide between
   theories: compare a margin against the native columns for the same
   world span on a later or earlier frame, and against the native columns
   256 px away on the same frame.
4. **Classify** with the table above. Facts, layout, phase, band policy,
   wall, special layer, object window, or not presentation at all.
5. **Fix at the cause**, in project source or the overrides script, never
   in generated or engine code by hand.
6. **Prove it.** The checklist in section 8.
7. **Record it** in `docs/BRINGUP.md` (DKC3) or the journal and
   architecture notes (DKC2), with the trigger state, the measurement, and
   what remains open. Update the README when behavior changes. Commit with
   a message that says what was wrong, not only what changed.

## 6. Fix recipes

### 6.1 Facts

Verify a candidate word three ways: the disassembly names its writer, the
live dump at the player's spot holds a value consistent with the picture,
and the value survives a few screens of movement. The DKC3 ring base at
`$04F8` passed the first two and failed the third.

### 6.2 Layout and stride

Run the offline stride matcher over WRAM and VRAM dumps: decode the native
window's tiles from the map with the camera (not the PPU scroll), the
metatile base from the trace, and the one-screen origin, for column-major
16 rows and row-major strides 32 to 256 in steps of 32, and take the one
that reproduces the ring. Add the case to the layout classifier with a
test line; the live calibrator (90% gate) then confirms it every frame.

### 6.3 Phase

If a band dump shows the rendered lines at a different scroll than the
frame-start register, the terrain phase selector must follow the dominant
band within the terrain lead of the camera. Keep every consumer (store
keys, prefill rows, band classification) on that one phase; mixing phases
turns an 8-pixel boundary into a 31-row wrap.

### 6.4 Band policy

A 64-column map that is not the terrain stream's destination, has had no
VRAM write since the camera last traveled 24 px, and whose rows continue
across its own wrap (no row with a period that fails to divide 64, no blank
strip of four or more columns at an edge, no broken rows judged by
character transparency) is a plane band. A map with exactly one populated
page is an object plane and skips the static gate. Everything else with a
proven period repeats; a 32-column map keeps its exact hardware wrap. Do
not lower the travel gate below two streamed-column periods.

### 6.5 Walls, voids, and the hold

A margin column is a wall candidate only when it is void from the visible
top down through the row, or an empty run at least four metatiles tall
that is sealed from the view on every row. The first non-empty metatile
toward the native edge must be full and backed by another full one, on a
corroborating adjacent row. A continued wall cell takes the metatile the
map most often places beside the previous one outward, not a copy of the
edge column. Rows the rule rejects mirror the authored terrain across the
wall line. Judge every corpus state a wall-rule edit changes by a zoomed
crop of its diff bounding box.

A west hold needs both the void and a pinned player (within forty pixels
of the frame's west edge with the camera unmoved, or the first eight
frames after a level change; a state restore is not a level start). The
presented bias moves at most one pixel per frame toward its target.

### 6.6 Special streamed layers

When the ring is never valid beside the view (a zero-lead column streamer,
a row rewrite from a partly rebuilt buffer, a 256-pixel map that is always
fully visible), decode the ROM's own map or object list into a dedicated
store layer, verify the decode every frame against the fully uploaded
native cells, and force every margin cell plus a native inset from it.
Accept the cartridge's own DMA quirks in the verifier (the rigging row DMA
lands high bytes one word late) or the whole margin drops out after each
vertical move.

### 6.7 Object windows

Find every camera-relative window by cataloguing each subtraction of the
camera word in the disassembly and reading the few instructions after it.
DKC3 had activation (table rows read by the compare and the cell scan),
the renderer cull at nine sites (one interpreter-only), a narrower cull,
an on-screen test behaviors call, and a grid renderer with two edges, two
clips, and two OAM packers; DKC2 had the renderer cull, the placement
radius, the release window, and a banana renderer.

Widen through the overrides script, which anchors on generated blocks by
trace address and fails closed when an anchor moves:

- table reads: wrap the single `cpu_read16` of the address;
- immediates: wrap the literal in the block that reads the camera;
- OAM ninth bit derived from the sign: wrap the accumulator write of the
  screen X with the promotion helper.

A block inlined into many variants must widen in every copy (nineteen for
DKC3's shared cull). A function's entry label shares a line with the
statement before it, so split on the label pattern without requiring a
line start. A site with no generated unit runs in the interpreter; widen
its immediates in the in-memory ROM image at run time, checking the bytes
replaced, and re-apply when the margin changes.

The helpers must honor the bias (left `extra - bias`, right
`extra + bias`), the readiness gate, and a run-time off switch for
before/after runs. Over-inclusion in a broad-phase scan is harmless; the
narrow compare decides.

### 6.8 Not presentation

A `JSR (abs,X)` dispatcher whose table holds a null slot in the middle is
cut at that slot by the recompiler; declare the full count in the cfg.
Symptoms: an object frozen at one behavior-script command in every aspect.

## 7. What each tool proves

DKC3 headless switches (`runner/headless_main.c`): `DKC3_ASPECT`,
`DKC3_SAVESTATE_INPUT`, `DKC3_SRAM_INPUT`, `SNESRECOMP_INPUT_PLAY`,
`DKC3_FRAME_PPM` and `DKC3_FRAME_PPM_PREFIX/START/END/STEP` (END minus
START must be a multiple of STEP), `DKC3_WRAM_OUTPUT`, `DKC3_VRAM_OUTPUT`,
`DKC3_OAM_OUTPUT`, `DKC3_PREFILL_TRACE=1` (camera, ready, bias, present,
matching per frame), `DKC3_PREFILL_DUMP=<frame counter>` (ring, decode,
store, status per disagreeing or margin cell; the counter is the host
frame plus the restored state's own counter), `DKC3_BAND_DUMP=1`,
`DKC3_TERRAIN_FILL_MAP=1|2`, `DKC3_TRACE_PC=<pc24>` (fires only for
interpreted opcodes: zero hits means the generated unit runs, not that the
routine never runs), `DKC3_OAM_TRACE=1`, `DKC3_SPAWN_TRACE=1`,
`DKC3_CULL_TRACE=1`, `DKC3_CULL_WIDEN=0`, `DKC3_WIDESCREEN_EDGE`,
`SNESRECOMP_LAYER_MASK`.

DKC2 equivalents: `DKC2_BAND_DUMP`, `DKC2_TERRAIN_FILL_MAP`,
`DKC2_REWIND_REPLAY=3`, `DKC2_SAVESTATE_RELOAD_FRAMES`, `DKC2_DRAW_EVERY`,
`DKC2_WIDESCREEN_EDGE`, plus `scripts/capture_widescreen_diagnostics.py`,
the route auditor, and `scripts/check_widescreen_state_corpus.py`, which
judges a rule change against every preserved state and the attract demos
at once with a reference run of the previous binary.

Scratch scripts worth keeping the shape of: a PPM reader with a PNG
writer for crops and montages; a pixel oracle that maps a world column to
a screen column through camera plus bias plus margin; a parser for the
cell dump keyed by (x, y); an OAM census that decodes nine-bit X with the
PPU's positive limit and tracks first appearances per tile; a stride
sweep over bank `$7F`.

What a trace cannot do: a fresh headless replay hides history bugs in the
store (the ring's other page rewritten with the same stale entries every
row upload); probe `WsShadowDebugCell` against the decode in the presented
cells outside the cartridge window for those. Layer-isolated renders hide
sub-screen color-math layers entirely.

## 8. Verification checklist

- 4:3 hashes identical before and after (frame, WRAM, VRAM, OAM) over a
  long scripted run.
- Every native cell matches the decode on every ready frame of the
  corpus; every margin cell matches.
- A margin's pixels equal the native columns for the same world span on
  another frame, on the layer alone. Expect a one-pixel registration
  residue from the PPU phase; seams in the full frame must be continuous.
- Before/after with the run-time switch on the same script: spawn
  positions, OAM first appearances, or a montage of the exact frames.
- The glide compared against reflect per world column on a biased state.
- Zoomed crops of every corpus state that changed.
- The unit suite, the ROM-backed headless boot, and the hidden app smoke
  runs at 4:3, 16:10, and with a quick-state load. The smoke runs use the
  bundle inside the headless build tree; rebuild every target there.
- The app rebuilt, the old process killed by pid, the new one confirmed
  newer than the bundle.

## 9. Gotchas that cost time

- A sampled comparison every twelve frames saw no difference from a change
  that moved every spawn by 40 pixels. Trace per frame; sample only to
  look.
- The first corpus had no object entering during its 253 frames. Choose a
  corpus with the event in it (the long right-running script).
- Counting `RecompReturn NAME` matches counts declarations too; the
  definition has `(CpuState *cpu) {`.
- The interactive shell is zsh: unquoted `$vars` do not word-split (use
  `${=vars}`), and `\$` inside double quotes becomes a bare `$` and anchors
  a regex. macOS `awk` has no array `match`; `sed -i` needs `''`.
- The Bash tool's working directory drifts; prefix builds with an
  absolute `cd`, and check the bundle's mtime afterwards rather than
  trusting a wrapper's exit code.
- `pkill` once left the old app running and `open` refocused it; kill by
  pid and confirm the new pid's start time.
- The 30-second SDL smoke timeouts fire if anything else is compiling.
- The PPU's layer mask did not isolate sprites in one attempt (a black
  frame); the OAM trace answered the question instead.
- A `DKC3_PREFILL_DUMP` keyed on the host frame prints nothing; it keys on
  the game's own frame counter.
- The generated emitter duplicates shared blocks into every unit that
  reaches them and puts a function's entry label inline.
- A memory note or a summary is not evidence; re-derive a claim from a
  dump before building on it (the "phantom roof" at the level start was a
  misreading that survived one context boundary).

## 10. When to stop

Black margins are the honest answer for a picture the evidence does not
cover: a name card, a static intro, a layout with no decoder, a layer
whose ring is never valid beside the view until its ROM source is decoded.
A fix that widens what is shown must arrive with the proof that what is
shown is what a wider console would have drawn. If the proof is not there
yet, record what is known and leave the margin black.
