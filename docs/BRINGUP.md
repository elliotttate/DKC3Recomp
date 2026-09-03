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
- The macOS app is built from the same sources with the DKC2Recomp SDL
  host; its hidden smoke tests are recorded below when they have run.
- Not verified: gameplay under real input, saves, the Windows host (copied
  but not built here), and every wide aspect beyond "the native frame
  between black margins".
- No widescreen terrain reconstruction exists for DKC3 yet. DKC2Recomp's
  presentation, HDMA band classification, and level-map decoding are
  DKC2-specific and were deliberately not copied; DKC3 needs its own
  evidence before any of that is attempted.
