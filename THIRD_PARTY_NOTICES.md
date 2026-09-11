# Third-party notices

## snesrecomp

- Project: `mstan/snesrecomp`, pinned as the `snesrecomp` submodule through
  the `elliotttate/snesrecomp` fork at revision
  `3b24b9daae623cd66a14e1ecf8483f0b0d2be91b`, which adds an explicit,
  default-off line-enhancer layer mask so a callback that adjusts the
  subscreen can preserve an already-widened BG1; the legacy default
  remains unchanged.
- License: PolyForm Noncommercial License 1.0.0; full pinned text below,
  also retained in `snesrecomp/LICENSE`.
- Local adaptations: the hunk files under `cmake/runtime-patches/` are
  applied by `scripts/apply_dkc3_runtime_patches.py` to build-directory
  copies of nine runtime sources (`ppu.c`, `interp_bridge.c`, `cpu_state.c`,
  `common_cpu_infra.c`, `dsp.c`, `dsp_shadow.c`, `audio_trace.c`, `apu.c`,
  `tier2_capture.c`). Each copy retains the original source and license,
  every anchor must match exactly the stated number of times, and the
  pinned submodule is left untouched. They comprise the Mode 2 BG priority
  change (the runtime's independent scalar PPU at the same revision supplies
  the priority and pixel oracle), the active-span priority-buffer clear
  ported from upstream snesrecomp `4020e47`, the opt-in tier-2 capture
  ported from upstream `8d12911`, the write-log prefix latch ported from
  the DKC1Recomp snesrecomp fork (`b46b940`), and DKC3's own measured
  changes to the padded widescreen merge, the line composite, the 4bpp
  viewport test and vector tile stores, the plain-ROM bus path, the
  interpreter's poll prefetch, entry options and jump-arrival bounce, the
  jump-table miss tier-down, the DSP diagnostic gating, the audio statistics
  clock and the stack-balance auditor. Diagnostic presence semantics of
  `SNESRECOMP_APU_PORT_DIAG` are preserved.

<details>
<summary>snesrecomp license text at the pinned revision</summary>

```text
PolyForm Noncommercial License 1.0.0

<https://polyformproject.org/licenses/noncommercial/1.0.0>

Copyright (c) 2026 Matthew Stan

## Acceptance

In order to get any license under these terms, you must agree to them as
both strict obligations and conditions to all your licenses.

## Copyright License

The licensor grants you a copyright license for the software to do
everything you might otherwise need the licensor's permission to do. Your
license covers distributing the software, making changes and new works
based on it, and all other uses of the software, subject to the
limitations and conditions in these terms.

## Noncommercial Purposes

Any noncommercial purpose is a permitted purpose.

## Personal Uses

Personal use for research, experiment, and testing for the benefit of
public knowledge, personal study, private entertainment, hobby projects,
amateur pursuits, or religious observance, without any anticipated
commercial application, counts as use for a permitted purpose.

## Noncommercial Organizations

Use by any charitable organization, educational institution, public
research organization, public safety or health organization, environmental
protection organization, or government institution counts as use for a
permitted purpose regardless of the source of funding or obligations
resulting from the funding.

## Fair Use

You may have "fair use" rights for the software under the law. These
terms do not limit them.

## No Other Rights

These terms do not allow you to sublicense or transfer any of your
licenses to anyone else, or prevent the licensor from granting licenses to
anyone else. These terms do not imply any other licenses.

## Patent Defense

If you make any written claim that the software infringes or contributes
to infringement of any patent, your patent license for the software granted
under these terms ends immediately. If your company makes such a claim,
your patent license ends immediately for work on behalf of your company.

## Violations

The first time you are notified in writing that you have violated any of
these terms, or done anything with the software not covered by your
licenses, your licenses can nonetheless continue if you come into full
compliance with these terms, and take practical steps to correct past
violations, within 32 days of receiving notice. Otherwise, all your
licenses end immediately.

## No Liability

As far as the law allows, the software comes as is, without any warranty
or condition, and the licensor will not be liable to you for any damages
arising out of these terms or the use or nature of the software, under any
kind of legal claim.

## Definitions

The **licensor** is the individual or entity offering these terms, and the
**software** is the software the licensor makes available under these
terms.

**You** refers to the individual or entity agreeing to these terms.

**Your company** is any legal entity, sole proprietorship, or other kind
of organization that you work for, plus all organizations that have control
over, are under the control of, or are under common control with that
organization. **Control** means ownership of substantially all the assets
of an entity, or the power to direct its management and policies by vote,
contract, or otherwise. Control can be direct or indirect.

**Your licenses** are all the licenses granted to you for the software
under these terms.

**Use** means anything you do with the software requiring one of your
licenses.

---

For the avoidance of doubt, the licensor's intent is to restrict uses where
profit is derived from this software. Non-profit personal, educational,
or community use is welcome regardless of organizational context.

For commercial licensing inquiries, contact: https://1379.tech
```

</details>

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
  The SPC music-policy adapter also uses its upload addresses, scheduler range,
  and music-versus-SFX voice-ownership structure as reference facts. Its
  scheduler signature is compared directly to the user's verified ROM at
  runtime; no disassembled engine code is included.
  The KAOS widescreen check uses the boss-scroll and HDMA screen-enable
  addresses at the same revision as reference facts; no assembly or game
  data was copied for that fix.
  The waterfall reconstruction uses the column streamer's addresses and
  layout-pointer/template structure at the same revision. Its column entries
  are read from the user's verified ROM at runtime and checked against native
  VRAM; no source, templates, or layout data are included here.
  Bleak's fixed-map policy uses the arena's level ID, register signature,
  and payload addresses at `$FD:290D` and `$E9:45BC/$4782` as reference facts.
  No disassembly source or map/tile data is copied.

## DKC2Recomp

- Project: `elliotttate/DKC2Recomp` (a fork of `mstan/DKC2Recomp`). The
  host modules under `runner/`, the widescreen presentation (video, HDMA,
  and the game adapter's terrain reconstruction), the build script, the
  desktop and video unit tests, and the pacing-log tool were copied from
  it with the game prefix renamed; DKC3's cartridge facts replace DKC2's
  in `runner/dkc3_facts.h`.
- License: see `LICENSE`, which is carried over unchanged.

## DKC1Recomp

- Project: `elliotttate/DKC1Recomp`. `runner/macos_metal_presenter.m` is
  adapted from its `runner/macos_metal_presenter.m` (the CAMetalLayer view,
  the CAMetalDisplayLink thread and the input-texture rotation) and its
  `runner/macos_graphics.metal` (the vertex and flat shaders and the Metal
  translation of the reconstruction upscaler, which that project had
  already carried over from DKC2Recomp's GLSL). DKC3 replaces the frame
  queue with a three-slot mailbox and publishes display ticks so the
  existing pacer drives the emulation.
- License: MIT, "Copyright (c) 2026 DKC1Recomp contributors"; the adapted
  file keeps that attribution in its header comment.

## SDL2 (Windows)

- Project: `libsdl-org/SDL`, release 2.30.9 at
  `c98c4fbff6d8f3016a3ce6685bf8f43433c3efcc`.
- Built without source adaptations as the shared SDL2.dll for the Windows
  SDL/OpenGL host. The SDL zlib license is included as `licenses/SDL2.txt`
  in the portable Windows package and in the dependency's `LICENSE.txt`.

## Launcher artwork

`recomp/launcher/boxart.tga` and `assets/macos/DKC3Recomp.icns` are
generated placeholders that name the game; they are not retail artwork.
