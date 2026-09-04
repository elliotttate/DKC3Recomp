#!/usr/bin/env python3
"""Apply source-owned DKC3 widescreen adaptations to private generated C.

The helpers are DKC2Recomp's; the routine-specific adaptations are added
as DKC3's activation and clipping code is identified."""

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
# block that holds them: (native left margin, native span). Each block
# subtracts $196D from a world X, adds the margin, and compares the sum
# with the span, keeping [-margin, span - margin).
CULL_SITES = {
    # The sprite renderer's cull, [-$30, $130). The block is inlined into
    # every renderer variant that reaches it, so the same address recurs
    # across units; every copy must widen.
    "0xB780C9": ("0x30", "0x160"), "0xB78925": ("0x30", "0x160"),
    "0xB78A2E": ("0x30", "0x160"), "0xB78A60": ("0x30", "0x160"),
    "0xB78B68": ("0x30", "0x160"), "0xB78CFB": ("0x30", "0x160"),
    "0xB78E73": ("0x30", "0x160"), "0xB7903C": ("0x30", "0x160"),
    # The renderer variant at $B7:8DFC culls narrower, [-$10, $110).
    "0xB78DFC": ("0x10", "0x120"),
    # $B6:F186 answers "is this sprite on screen" for sprite behaviors with
    # X in [-$10, $100) (its Y test, in the next block, is untouched).
    "0xB6F186": ("0x10", "0x110"),
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
        left, span = CULL_SITES[address]
        if "0x196d" not in block:
            raise ValueError(f"cull block {address} does not read camera X")
        left_expr = f"Dkc3VideoExpandCullLeft({left})"
        span_expr = f"Dkc3VideoExpandCullSpan({span})"
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
    objects (banana arcs and the like) and $B7:E8E0 walks it every frame:
    it stops at the first object starting at or beyond camera + $100 and
    skips one whose right end lies more than $10F - $100 = 15 pixels left
    of the screen. $B7:E981 draws one object, clipping its tile columns at
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
        walk, "0xB7E909", "0x10f", "Dkc3VideoExpandCullLeft")
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
        compare, "0xbba8be", "Dkc3VideoExpandCullLeft")
    compare = wrap_single_read(
        compare, "0xbba8c0", "Dkc3VideoExpandCullSpan")
    sources[compare_path] = compare

    scan = add_include(scan_path.read_text(encoding="utf-8"))
    scan = add_include(scan, GAME_INCLUDE)
    scan = wrap_single_read(
        scan, "0x15fe", "Dkc3PlacementScanBegin", "cpu->X")
    scan = wrap_single_read(
        scan, "0x7e4180", "Dkc3PlacementScanNext")
    sources[scan_path] = scan

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
