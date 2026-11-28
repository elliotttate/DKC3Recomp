#!/usr/bin/env python3
"""Apply source-owned DKC3 adaptations to private generated C.

The widescreen helpers are DKC2Recomp's; the routine-specific adaptations are
added as DKC3's activation and clipping code is identified. The MSU-1 music
transition observer also lives here because generated code is never edited by
hand."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


INCLUDE = '#include "dkc3_video.h"'
GAME_INCLUDE = '#include "dkc3_game.h"'


def find_unit(generated_dir: Path, symbol: str) -> Path:
    matches = [
        path for path in generated_dir.glob("*.c")
        if f"RecompReturn {symbol}(CpuState *cpu) {{" in
        path.read_text(encoding="utf-8")
    ]
    if len(matches) != 1:
        raise ValueError(
            f"expected exactly one generated unit defining {symbol}; "
            f"found {len(matches)}")
    return matches[0]


def add_include(text: str, include: str = INCLUDE) -> str:
    if include in text:
        return text
    marker = '#include "funcs.h"'
    if text.count(marker) != 1:
        raise ValueError("generated unit has an unexpected funcs.h include")
    return text.replace(marker, marker + "\n" + include, 1)


def wrap_single_read(text: str, address: str, helper: str,
                     extra_argument: str | None = None) -> str:
    suffix = f", {extra_argument}" if extra_argument else ""
    already = re.compile(
        rf"{helper}\(\s*cpu_read16\([^;\n]*{address}[^;\n]*\)"
        rf"{re.escape(suffix)}\s*\)")
    if len(already.findall(text)) == 1:
        return text
    if already.search(text):
        raise ValueError(f"ambiguous existing {helper} adaptation")

    pattern = re.compile(
        rf"(uint16\s+\w+\s*=\s*)(cpu_read16\([^;\n]*{address}[^;\n]*\))"
        rf"(;)")
    text, count = pattern.subn(rf"\1{helper}(\2{suffix})\3", text)
    if count != 1:
        raise ValueError(
            f"expected one read from {address} for {helper}; found {count}")
    return text


def adapt_trace_block(
        text: str, start_label: str, end_label: str) -> str:
    start = text.find(start_label)
    end = text.find(end_label, start + len(start_label))
    if start < 0 or end < 0:
        raise ValueError(
            f"could not isolate generated trace block {start_label}")
    block = text[start:end]

    replacements = (
        (r"(uint16\s+\w+\s*=\s*)0x30;", "Dkc3VideoExpandCullLeft(0x30)"),
        (r"(uint16\s+\w+\s*=\s*)0x160;", "Dkc3VideoExpandCullSpan(0x160)"),
    )
    for pattern, expression in replacements:
        if expression in block:
            if block.count(expression) != 1:
                raise ValueError(
                    f"ambiguous existing adaptation in {start_label}")
            continue
        block, count = re.subn(pattern, rf"\1{expression};", block)
        if count != 1:
            raise ValueError(
                f"expected one native cull constant in {start_label}; "
                f"found {count}")
    return text[:start] + block + text[end:]


def adapt_constant_block(
        text: str, start_label: str, end_label: str,
        literal: str, helper: str) -> str:
    start = text.find(start_label)
    end = text.find(end_label, start + len(start_label))
    if start < 0 or end < 0:
        raise ValueError(
            f"could not isolate generated trace block {start_label}")
    block = text[start:end]
    expression = f"{helper}({literal})"
    if expression in block:
        if block.count(expression) != 1:
            raise ValueError(
                f"ambiguous existing adaptation in {start_label}")
        return text

    pattern = rf"(uint16\s+\w+\s*=\s*){re.escape(literal)};"
    block, count = re.subn(pattern, rf"\1{expression};", block)
    if count != 1:
        raise ValueError(
            f"expected one {literal} constant in {start_label}; "
            f"found {count}")
    return text[:start] + block + text[end:]


def adapt_nth_accumulator_write(
        text: str, start_label: str, end_label: str,
        write_index: int, helper: str) -> str:
    start = text.find(start_label)
    end = text.find(end_label, start + len(start_label))
    if start < 0 or end < 0:
        raise ValueError(
            f"could not isolate generated trace block {start_label}")
    block = text[start:end]
    if helper in block:
        if block.count(helper) != 1:
            raise ValueError(
                f"ambiguous existing {helper} adaptation in {start_label}")
        return text

    pattern = re.compile(
        r"cpu_write_a_m\(cpu, \(uint16\)\((\w+)\)\);")
    matches = list(pattern.finditer(block))
    if write_index < 0 or write_index >= len(matches):
        raise ValueError(
            f"expected accumulator write {write_index} in {start_label}; "
            f"found {len(matches)} writes")
    match = matches[write_index]
    variable = match.group(1)
    replacement = (
        f"cpu_write_a_m(cpu, (uint16)({helper}({variable})));"
    )
    block = block[:match.start()] + replacement + block[match.end():]
    return text[:start] + block + text[end:]


# Camera-relative X windows to widen, by the trace address of the generated
# block that holds them: (native left margin, native span, left helper,
# span helper). Each block subtracts $196D from a world X, adds the margin,
# and compares the sum with the span, keeping [-margin, span - margin).
# Sprite renderer culls ($B7) use the sprite-cull helpers so newly activated
# sprites can queue VRAM tile DMA off-screen before crossing the widened edge.
CULL_SITES = {
    # The sprite renderer's cull, [-$30, $130). The block is inlined into
    # every renderer variant that reaches it, so the same address recurs
    # across units; every copy must widen.
    "0xB780C9": ("0x30", "0x160", "Dkc3VideoExpandSpriteCullLeft", "Dkc3VideoExpandSpriteCullSpan"),
    "0xB78925": ("0x30", "0x160", "Dkc3VideoExpandSpriteCullLeft", "Dkc3VideoExpandSpriteCullSpan"),
    "0xB78A2E": ("0x30", "0x160", "Dkc3VideoExpandSpriteCullLeft", "Dkc3VideoExpandSpriteCullSpan"),
    "0xB78A60": ("0x30", "0x160", "Dkc3VideoExpandSpriteCullLeft", "Dkc3VideoExpandSpriteCullSpan"),
    "0xB78B68": ("0x30", "0x160", "Dkc3VideoExpandSpriteCullLeft", "Dkc3VideoExpandSpriteCullSpan"),
    "0xB78CFB": ("0x30", "0x160", "Dkc3VideoExpandSpriteCullLeft", "Dkc3VideoExpandSpriteCullSpan"),
    "0xB78E73": ("0x30", "0x160", "Dkc3VideoExpandSpriteCullLeft", "Dkc3VideoExpandSpriteCullSpan"),
    "0xB7903C": ("0x30", "0x160", "Dkc3VideoExpandSpriteCullLeft", "Dkc3VideoExpandSpriteCullSpan"),
    # The renderer variant at $B7:8DFC culls narrower, [-$10, $110).
    "0xB78DFC": ("0x10", "0x120", "Dkc3VideoExpandSpriteCullLeft", "Dkc3VideoExpandSpriteCullSpan"),
    # $B6:F186 answers "is this sprite on screen" for sprite behaviors with
    # X in [-$10, $100) (its Y test, in the next block, is untouched). Keep it
    # on the exact presented view without extra off-screen spawn lead.
    "0xB6F186": ("0x10", "0x110", "Dkc3VideoExpandCullLeft", "Dkc3VideoExpandCullSpan"),
}
# The same $30/$160 cull at $B7:A8C8 sits in CODE_B7A88F, which the analysis
# leaves to the interpreter; the runtime widens its immediates in the ROM
# image instead (Dkc3PatchInterpreterCull).


def adapt_cull_blocks(text: str) -> tuple[str, set[str]]:
    """Widen every listed cull block in one generated unit.

    Returns the adapted text and the trace addresses of the blocks changed
    (or already adapted), so the caller can check the whole set was found.
    A function's entry label shares a line with the statement before it, so
    the split does not require the label to start a line.
    """
    label = re.compile(r"(  L_[0-9A-F]{4}_M[01]X[01]:)")
    parts = label.split(text)
    found: set[str] = set()
    for index in range(2, len(parts), 2):
        block = parts[index]
        trace = re.search(r"cpu_trace_block\(cpu, (0x[0-9A-F]+)\)", block)
        if not trace or trace.group(1) not in CULL_SITES:
            continue
        address = trace.group(1)
        left, span, left_helper, span_helper = CULL_SITES[address]
        if "0x196d" not in block:
            raise ValueError(f"cull block {address} does not read camera X")
        left_expr = f"{left_helper}({left})"
        span_expr = f"{span_helper}({span})"
        if left_expr not in block:
            block, count = re.subn(
                rf"(uint16\s+\w+\s*=\s*){left};",
                rf"\1{left_expr};", block)
            if count != 1:
                raise ValueError(
                    f"expected one {left} margin in cull block {address}; "
                    f"found {count}")
        if span_expr not in block:
            block, count = re.subn(
                rf"(uint16\s+\w+\s*=\s*){span};",
                rf"\1{span_expr};", block)
            if count != 1:
                raise ValueError(
                    f"expected one {span} span in cull block {address}; "
                    f"found {count}")
        parts[index] = block
        found.add(address)
    return "".join(parts), found


def adapt_block_constant(text: str, trace_address: str, literal: str,
                         helper: str) -> str:
    """Wrap one literal in the generated block with the given trace address."""
    label = re.compile(r"(  L_[0-9A-F]{4}_M[01]X[01]:)")
    parts = label.split(text)
    expression = f"{helper}({literal})"
    for index in range(2, len(parts), 2):
        block = parts[index]
        trace = re.search(r"cpu_trace_block\(cpu, (0x[0-9A-F]+)\)", block)
        if not trace or trace.group(1) != trace_address:
            continue
        if expression in block:
            if block.count(expression) != 1:
                raise ValueError(
                    f"ambiguous existing adaptation in {trace_address}")
            return text
        block, count = re.subn(
            rf"(uint16\s+\w+\s*=\s*){re.escape(literal)};",
            rf"\1{expression};", block)
        if count != 1:
            raise ValueError(
                f"expected one {literal} constant in block {trace_address}; "
                f"found {count}")
        parts[index] = block
        return "".join(parts)
    raise ValueError(f"generated block {trace_address} not found")


def adapt_block_accumulator_write(text: str, trace_address: str,
                                  write_index: int, helper: str) -> str:
    """Wrap the Nth accumulator write in the block with the trace address."""
    label = re.compile(r"(  L_[0-9A-F]{4}_M[01]X[01]:)")
    parts = label.split(text)
    for index in range(2, len(parts), 2):
        block = parts[index]
        trace = re.search(r"cpu_trace_block\(cpu, (0x[0-9A-F]+)\)", block)
        if not trace or trace.group(1) != trace_address:
            continue
        if helper in block:
            if block.count(helper) != 1:
                raise ValueError(
                    f"ambiguous existing {helper} adaptation in "
                    f"{trace_address}")
            return text
        pattern = re.compile(r"cpu_write_a_m\(cpu, \(uint16\)\((\w+)\)\);")
        matches = list(pattern.finditer(block))
        if write_index < 0 or write_index >= len(matches):
            raise ValueError(
                f"expected accumulator write {write_index} in block "
                f"{trace_address}; found {len(matches)} writes")
        match = matches[write_index]
        variable = match.group(1)
        replacement = (
            f"cpu_write_a_m(cpu, (uint16)({helper}({variable})));")
        parts[index] = (
            block[:match.start()] + replacement + block[match.end():])
        return "".join(parts)
    raise ValueError(f"generated block {trace_address} not found")


def adapt_grid_renderer(sources: dict, generated_dir: Path) -> None:
    """Widen the OAM grid renderer for the level's static tile-row objects.

    $B7:E4D6 builds a list at $7E:6780 of the level's static tile-row
    objects (banana arcs and bonus stars) and $B7:E8E0 walks it every frame:
    it stops at the first object starting at or beyond camera + $100 and
    skips one whose right end lies more than $10F - $100 = 15 pixels left
    of the screen (adding $10F back after subtracting camera + $100, so $10F
    is the full span from the right bound to the left slack and must widen by
    both margins; widening it by only the left margin shrank the left edge
    inward by 2*bias near a left wall and skipped both drawing and collection
    for stars there). $B7:E981 draws one object, clipping its tile columns at
    the screen's left edge (the $0F slack at $B7:E9C8) and right edge (the
    $107 at $B7:E9EF), and takes OAM's ninth X bit from bit 15 of the
    column's screen X, which native play only needs for negative values;
    columns in the widened right margin must mirror bit 8 there first.
    """
    walk_path = find_unit(generated_dir, "CODE_B7E8E0_M0X0")
    draw_path = find_unit(generated_dir, "CODE_B7E981_M0X0")
    for path in (walk_path, draw_path):
        if path in sources:
            raise ValueError(f"{path.name} already adapted for another site")
        sources[path] = add_include(path.read_text(encoding="utf-8"))
    walk = sources[walk_path]
    walk = adapt_block_constant(
        walk, "0xB7E8E0", "0x100", "Dkc3VideoExpandCullRight")
    walk = adapt_block_constant(
        walk, "0xB7E909", "0x10f", "Dkc3VideoExpandCullSpan")
    sources[walk_path] = walk
    draw = sources[draw_path]
    draw = adapt_block_constant(
        draw, "0xB7E9C8", "0xf", "Dkc3VideoExpandCullLeft")
    draw = adapt_block_constant(
        draw, "0xB7E9EF", "0x107", "Dkc3VideoExpandCullRight")
    draw = adapt_block_accumulator_write(
        draw, "0xB7EA41", 1, "Dkc3VideoPromoteOamXHigh")
    draw = adapt_block_accumulator_write(
        draw, "0xB7EAA4", 1, "Dkc3VideoPromoteOamXHigh")
    sources[draw_path] = draw


def adapt_music_transition(sources: dict, generated_dir: Path) -> None:
    """Record the stock transition-song command without changing it.

    $B2:800F is a three-byte trampoline into the stock transition handler.
    The host uses its accumulator value to select DKC3 MSU-1 tracks 49-58;
    cartridge registers, control flow, and sound effects remain untouched.
    """
    path = find_unit(generated_dir, "CODE_B2800F_M0X0")
    if path in sources:
        raise ValueError(f"{path.name} already adapted for another site")
    text = add_include(path.read_text(encoding="utf-8"), GAME_INCLUDE)
    hook = "Dkc3RecordMusicTransition(cpu->A);"
    if hook in text:
        if text.count(hook) != 1:
            raise ValueError("ambiguous existing music transition observer")
        sources[path] = text
        return
    marker = "    cpu_trace_block(cpu, 0xB2800F);"
    if text.count(marker) != 1:
        raise ValueError(
            "expected one $B2:800F music transition block; "
            f"found {text.count(marker)}")
    sources[path] = text.replace(marker, marker + "\n    " + hook, 1)


def adapt_palette_overflow(sources: dict, generated_dir: Path) -> None:
    """Record sprite palette allocator overflows without changing guest state.

    When all eight SNES hardware OBJ palettes ($073C[0..7]) are full in
    widescreen, $BB:95E6 falls back to palette slot 0. Recording the failed
    palette ID ($04EF) and active sprite slot (DP $76) lets the widescreen PPU
    render that sprite with its true ROM/WRAM palette from an extended table.
    """
    path = find_unit(generated_dir, "request_palette_direct_M0X0")
    if path in sources:
        raise ValueError(f"{path.name} already adapted for another site")
    text = add_include(path.read_text(encoding="utf-8"), GAME_INCLUDE)
    hook = "Dkc3RecordSpritePaletteOverflow(cpu->D);"
    if hook in text:
        if text.count(hook) != 1:
            raise ValueError("ambiguous existing palette overflow observer")
        sources[path] = text
        return
    marker = "    cpu_trace_block(cpu, 0xBB95E6);"
    if text.count(marker) != 1:
        raise ValueError(
            "expected one $BB:95E6 palette overflow block; "
            f"found {text.count(marker)}")
    sources[path] = text.replace(marker, marker + "\n    " + hook, 1)


def adapt_trunk_windows(sources: dict, generated_dir: Path) -> None:
    """Record the tree-trunk BG3 windows without changing guest state.

    $B3:C0F4 loads two world spans into DP $1A-$20 and writes W1 and W2 as
    those spans minus the camera, each edge clamped to the 256-pixel
    screen. A trunk inside a wide margin is therefore written as [255, 255]
    or [0, 0]. Right after the last register write ($2129, block $B3:C1B1),
    the host reads the spans back so the widescreen PPU can mask the margin
    part of the trunk the clamp removed.
    """
    path = find_unit(generated_dir, "CODE_B3C0F4_M0X0")
    if path in sources:
        raise ValueError(f"{path.name} already adapted for another site")
    text = add_include(path.read_text(encoding="utf-8"), GAME_INCLUDE)
    hook = "Dkc3RecordTrunkWindows(cpu->D);"
    if hook in text:
        if text.count(hook) != 1:
            raise ValueError("ambiguous existing trunk window observer")
        sources[path] = text
        return
    marker = "    cpu_trace_block(cpu, 0xB3C1B1);"
    if text.count(marker) != 1:
        raise ValueError(
            "expected one $B3:C1B1 trunk window block; "
            f"found {text.count(marker)}")
    start = text.index(marker)
    following = text.find("cpu_trace_block(cpu, 0x", start + len(marker))
    end = following if following >= 0 else len(text)
    block = text[start:end]
    write = re.compile(
        r"\n    cpu_write8\(cpu, cpu->DB, \(uint16\)\(0x2129\), _v\d+\);")
    matches = write.findall(block)
    if len(matches) != 1:
        raise ValueError(
            "expected one W2R ($2129) write in $B3:C1B1; "
            f"found {len(matches)}")
    block = block.replace(matches[0], matches[0] + "\n    " + hook, 1)
    sources[path] = text[:start] + block + text[end:]


HIDDEN_RENDER_BLOCK = "0xB78904"
HIDDEN_RENDER_HOOK = "Dkc3RevealHiddenSpriteFlags"
# The primary sprite renderer, whose loop holds the block's own copy.
HIDDEN_RENDER_PRIMARY = "CODE_B78857"


def adapt_hidden_sprite_reveal(sources: dict, generated_dir: Path) -> None:
    """Let the Show hidden objects assist reveal invisible sprites.

    The sprite renderer reads each slot's render word (+$26) at $B7:8908, in
    the block traced as $B7:8904. Bits 15 and 14 together make $B7:89C4 skip
    the sprite without drawing it (the hidden bear coin spawns that way).
    The block is inlined into every renderer variant that loops back to the
    top, so every copy is wrapped. The host helper returns the word unchanged
    unless the assist is on; the native behavior is the helper disabled.
    Every trace marker must sit in exactly one wrapped block, and the
    primary renderer's copy must be among them, so a partial match fails.
    """
    label = re.compile(r"(  L_[0-9A-F]{4}_M[01]X[01]:)")
    read = re.compile(
        r"(uint16\s+\w+\s*=\s*)(cpu_read16\([^;\n]*\(uint32\)0x0026 \+ "
        r"\(uint32\)cpu->Y[^;\n]*\));")
    function = re.compile(r"RecompReturn (\w+)\(")
    marker = f"cpu_trace_block(cpu, {HIDDEN_RENDER_BLOCK});"
    markers = 0
    found = 0
    wrapped_functions: set[str] = set()
    for path in sorted(generated_dir.glob("*.c")):
        text = sources.get(path)
        if text is None:
            text = path.read_text(encoding="utf-8")
        if marker not in text:
            continue
        markers += text.count(marker)
        parts = label.split(text)
        for index in range(2, len(parts), 2):
            block = parts[index]
            if marker not in block:
                continue
            if block.count(marker) != 1:
                raise ValueError(
                    f"ambiguous $B7:8904 sprite renderer block in {path.name}")
            owners = function.findall("".join(parts[:index]))
            if owners:
                wrapped_functions.add(owners[-1])
            if f"{HIDDEN_RENDER_HOOK}(" in block:
                if block.count(f"{HIDDEN_RENDER_HOOK}(") != 1:
                    raise ValueError(
                        "ambiguous existing hidden sprite adaptation in "
                        f"{path.name}")
                found += 1
                continue
            block, count = read.subn(
                rf"\1{HIDDEN_RENDER_HOOK}(\2, cpu->Y);", block)
            if count != 1:
                raise ValueError(
                    "expected one $B7:8908 render word (+$26) read in "
                    f"{path.name}; found {count}")
            parts[index] = block
            found += 1
        text = add_include("".join(parts), GAME_INCLUDE)
        sources[path] = text
    if found == 0:
        raise ValueError(
            "no $B7:8904 sprite renderer block found for the hidden "
            "sprite reveal")
    if found != markers:
        raise ValueError(
            f"wrapped {found} of {markers} $B7:8904 sprite renderer blocks "
            "for the hidden sprite reveal")
    if not any(name.startswith(HIDDEN_RENDER_PRIMARY + "_")
               for name in wrapped_functions):
        raise ValueError(
            f"the primary sprite renderer {HIDDEN_RENDER_PRIMARY} was not "
            "wrapped for the hidden sprite reveal")


def apply_overrides(generated_dir: Path) -> list[Path]:
    """Widen DKC3's object activation and sprite culls to the presented view.

    Placed objects activate through $BB:AAD4, which keeps an object whose
    world X lies in [camera - Lx, camera - Lx + Wx) for the (Lx, Wx) row of
    DATA_BBA8BE selected by the placement's parameter. Every row satisfies
    Wx = 2 * Lx + $100, and the terrain-gated helpers preserve that. The
    persistent index can be native-width in an older quick save, so $BB:A647's
    broad-phase walk merges the current 256-pixel cell with its horizontal
    neighbors before this final compare.
    """
    compare_path = find_unit(generated_dir, "CODE_BBAAD4_M0X0")
    scan_path = find_unit(generated_dir, "CODE_BBA647_M0X0")
    sources = {compare_path: add_include(
        compare_path.read_text(encoding="utf-8"))}
    compare = sources[compare_path]
    compare = wrap_single_read(
        compare, "0xbba8be", "Dkc3VideoExpandSpawnLeft")
    compare = wrap_single_read(
        compare, "0xbba8c0", "Dkc3VideoExpandSpawnSpan")
    sources[compare_path] = compare

    scan = add_include(scan_path.read_text(encoding="utf-8"))
    scan = add_include(scan, GAME_INCLUDE)
    scan = wrap_single_read(
        scan, "0x15fe", "Dkc3PlacementScanBegin", "cpu->X")
    scan = wrap_single_read(
        scan, "0x7e4180", "Dkc3PlacementScanNext")
    sources[scan_path] = scan
    adapt_music_transition(sources, generated_dir)
    adapt_palette_overflow(sources, generated_dir)
    adapt_trunk_windows(sources, generated_dir)

    found: set[str] = set()
    for path in sorted(generated_dir.glob("*.c")):
        text = path.read_text(encoding="utf-8")
        if "0x196d" not in text:
            continue
        adapted, sites = adapt_cull_blocks(text)
        if not sites:
            continue
        found |= sites
        if path in sources:
            raise ValueError(f"{path.name} holds both a table read and a cull")
        sources[path] = add_include(adapted)
    missing = set(CULL_SITES) - found
    if missing:
        raise ValueError(
            "renderer cull blocks not found: " + ", ".join(sorted(missing)))
    adapt_grid_renderer(sources, generated_dir)
    adapt_hidden_sprite_reveal(sources, generated_dir)

    changed = []
    for path, text in sorted(sources.items()):
        if path.read_text(encoding="utf-8") != text:
            path.write_text(text, encoding="utf-8", newline="\n")
            changed.append(path)
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated-dir", required=True, type=Path)
    args = parser.parse_args()
    generated_dir = args.generated_dir.expanduser().resolve(strict=True)
    changed = apply_overrides(generated_dir)
    for path in changed:
        print(f"Applied DKC3 widescreen overrides: {path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"error: {error}")
        raise SystemExit(1)
