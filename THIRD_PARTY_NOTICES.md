# Third-party notices

## snesrecomp

- Project: `mstan/snesrecomp`, pinned as the `snesrecomp` submodule through
  the `elliotttate/snesrecomp` fork's `dkc3-bringup` branch, which is
  upstream `c2f2421` plus one commit adding two missing includes
  (`<time.h>` in `interp816.c`, `sdd1.h` in `interp_bridge.c`).
- License: see `snesrecomp/LICENSE`.

## recomp-ui

- Project: `mstan/recomp-ui`, pinned as the `recomp-ui` submodule at
  `ad2f3e293c6641c93ee69963dd669661f3e40290`. It provides the pre-boot
  launcher and the in-game overlay (Dear ImGui).
- License: see the submodule.

## H4v0c21 DKC3 disassembly

- Project: `H4v0c21/DKC3-Disassembly`,
  <https://github.com/H4v0c21/DKC3-Disassembly>, consulted at its
  2025-08-01 revision (`bed9689`).
- License: GPL-3.
- Use: an address and structure reference. `tools/ingest_dkc3_disasm.py`
  reads its bank sources and assembled symbol file and writes only names,
  addresses, bounded ranges, data regions, and finite dispatch contracts
  into `recomp/*.cfg`. No assembly source, comments, ROM-derived assets, or
  game data are copied into this repository.

## DKC2Recomp

- Project: `elliotttate/DKC2Recomp` (a fork of `mstan/DKC2Recomp`). The
  host modules under `runner/`, the widescreen presentation (video, HDMA,
  and the game adapter's terrain reconstruction), the build script, the
  desktop and video unit tests, and the pacing-log tool were copied from
  it with the game prefix renamed; DKC3's cartridge facts replace DKC2's
  in `runner/dkc3_facts.h`.
- License: see `LICENSE`, which is carried over unchanged.

## Launcher artwork

`recomp/launcher/boxart.tga` and `assets/macos/DKC3Recomp.icns` are
generated placeholders that name the game; they are not retail artwork.
