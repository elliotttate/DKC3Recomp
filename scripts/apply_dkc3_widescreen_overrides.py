#!/usr/bin/env python3
"""Apply source-owned DKC3 widescreen adaptations to private generated C.

The helpers are DKC2Recomp's; the routine-specific adaptations are added
as DKC3's activation and clipping code is identified."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


INCLUDE = '#include "dkc3_video.h"'


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


def add_include(text: str) -> str:
    if INCLUDE in text:
        return text
    marker = '#include "funcs.h"'
    if text.count(marker) != 1:
        raise ValueError("generated unit has an unexpected funcs.h include")
    return text.replace(marker, marker + "\n" + INCLUDE, 1)


def wrap_single_read(text: str, address: str, helper: str) -> str:
    already = re.compile(
        rf"{helper}\(\s*cpu_read16\([^;\n]*{address}[^;\n]*\)\s*\)")
    if len(already.findall(text)) == 1:
        return text
    if already.search(text):
        raise ValueError(f"ambiguous existing {helper} adaptation")

    pattern = re.compile(
        rf"(uint16\s+\w+\s*=\s*)(cpu_read16\([^;\n]*{address}[^;\n]*\))"
        rf"(;)")
    text, count = pattern.subn(rf"\1{helper}(\2)\3", text)
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


def apply_overrides(generated_dir: Path) -> list[Path]:
    """Adapt the generated units that decide object activation and sprite
    clipping so they cover the widened view. DKC3's routines are not yet
    identified (docs/BRINGUP.md); until they are, the pass changes nothing
    and reports so, and placed objects activate at the native edge."""
    return []


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
