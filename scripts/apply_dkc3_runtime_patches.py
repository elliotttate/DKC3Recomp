#!/usr/bin/env python3
"""Apply DKC3's runtime adaptations to build-directory copies of snesrecomp.

The pinned submodule is never edited. Each ``*.hunks`` file under
``cmake/runtime-patches`` names one runtime source and lists literal
replacements. A replacement's ``old`` text must occur exactly once in the
source (or exactly ``count`` times when the hunk says so); anything else means
the pinned runtime moved underneath the adaptation, and the build fails
closed so the change is reviewed instead of silently dropped.

Hunk file grammar::

    file: runner/src/snes/ppu.c

    @@ hunk-name [count=N]
    <<<<
    exact original text
    ====
    replacement text
    >>>>

Text between the markers is taken verbatim, without the newline that ends
the marker line and without the newline before the closing marker. Lines
outside hunks that start with ``#`` are comments. Output copies keep their
basenames and land flat in the output directory; a manifest lists each
source and its patched copy so CMake can swap them into the runtime target.
"""
from __future__ import annotations

import argparse
import pathlib
import sys

HUNK_PREFIX = "@@ "
OLD_MARK = "<<<<"
SEP_MARK = "===="
NEW_MARK = ">>>>"


class PatchError(Exception):
    """A hunk file is malformed or no longer matches the pinned runtime."""


class Hunk:
    def __init__(self, name: str, count: int, old: str, new: str, where: str):
        self.name = name
        self.count = count
        self.old = old
        self.new = new
        self.where = where


def parse_hunks(path: pathlib.Path) -> tuple[str, list[Hunk]]:
    lines = path.read_text(encoding="utf-8").split("\n")
    target = None
    hunks: list[Hunk] = []
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.startswith("file:"):
            if target is not None:
                raise PatchError(f"{path}:{index + 1}: a hunk file names one source")
            target = line[len("file:"):].strip()
            index += 1
            continue
        if not line.startswith(HUNK_PREFIX):
            if line.strip() and not line.startswith("#"):
                raise PatchError(f"{path}:{index + 1}: unexpected text outside a hunk")
            index += 1
            continue
        header = line[len(HUNK_PREFIX):].split()
        if not header:
            raise PatchError(f"{path}:{index + 1}: hunk needs a name")
        name = header[0]
        count = 1
        for option in header[1:]:
            if option.startswith("count="):
                count = int(option[len("count="):])
                if count < 1:
                    raise PatchError(f"{path}:{index + 1}: count must be positive")
            else:
                raise PatchError(f"{path}:{index + 1}: unknown hunk option {option!r}")
        where = f"{path}:{index + 1}"
        index += 1
        if index >= len(lines) or lines[index] != OLD_MARK:
            raise PatchError(f"{where}: expected {OLD_MARK} after the hunk header")
        index += 1
        old: list[str] = []
        while index < len(lines) and lines[index] != SEP_MARK:
            old.append(lines[index])
            index += 1
        if index >= len(lines):
            raise PatchError(f"{where}: hunk has no {SEP_MARK}")
        index += 1
        new: list[str] = []
        while index < len(lines) and lines[index] != NEW_MARK:
            new.append(lines[index])
            index += 1
        if index >= len(lines):
            raise PatchError(f"{where}: hunk has no {NEW_MARK}")
        index += 1
        old_text = "\n".join(old)
        new_text = "\n".join(new)
        if not old_text:
            raise PatchError(f"{where}: hunk has empty original text")
        if old_text == new_text:
            raise PatchError(f"{where}: hunk changes nothing")
        hunks.append(Hunk(name, count, old_text, new_text, where))
    if target is None:
        raise PatchError(f"{path}: missing 'file:' line")
    if not hunks:
        raise PatchError(f"{path}: no hunks")
    return target, hunks


def apply_hunks(source: str, hunks: list[Hunk]) -> str:
    for hunk in hunks:
        found = source.count(hunk.old)
        if found != hunk.count:
            raise PatchError(
                f"{hunk.where}: anchor for hunk '{hunk.name}' found {found} "
                f"time(s), expected {hunk.count}; the pinned runtime moved "
                "or the adaptation is stale")
        source = source.replace(hunk.old, hunk.new)
    return source


def apply_directory(patch_dir: pathlib.Path, snesrecomp_root: pathlib.Path,
                    out_dir: pathlib.Path) -> list[tuple[pathlib.Path, pathlib.Path]]:
    out_dir.mkdir(parents=True, exist_ok=True)
    hunk_files = sorted(patch_dir.glob("*.hunks"))
    if not hunk_files:
        raise PatchError(f"{patch_dir}: no *.hunks files")
    by_target: dict[str, list[Hunk]] = {}
    for hunk_file in hunk_files:
        target, hunks = parse_hunks(hunk_file)
        by_target.setdefault(target, []).extend(hunks)
    results: list[tuple[pathlib.Path, pathlib.Path]] = []
    seen_names: dict[str, str] = {}
    for target, hunks in by_target.items():
        source_path = snesrecomp_root / target
        if not source_path.is_file():
            raise PatchError(f"{source_path}: runtime source not found")
        name = source_path.name
        if name in seen_names and seen_names[name] != target:
            raise PatchError(f"{target} and {seen_names[name]} share the basename {name}")
        seen_names[name] = target
        patched = apply_hunks(source_path.read_text(encoding="utf-8"), hunks)
        output = out_dir / name
        if not output.exists() or output.read_text(encoding="utf-8") != patched:
            output.write_text(patched, encoding="utf-8")
        results.append((source_path, output))
    manifest = out_dir / "manifest.txt"
    manifest_text = "".join(f"{src}\t{dst}\n" for src, dst in results)
    if not manifest.exists() or manifest.read_text(encoding="utf-8") != manifest_text:
        manifest.write_text(manifest_text, encoding="utf-8")
    return results


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--patches", required=True, type=pathlib.Path,
                        help="directory holding *.hunks files")
    parser.add_argument("--snesrecomp", required=True, type=pathlib.Path,
                        help="pinned snesrecomp checkout")
    parser.add_argument("--out", required=True, type=pathlib.Path,
                        help="directory receiving the patched copies")
    args = parser.parse_args(argv)
    try:
        results = apply_directory(args.patches, args.snesrecomp, args.out)
    except PatchError as error:
        print(f"apply_dkc3_runtime_patches: {error}", file=sys.stderr)
        return 1
    for source, output in results:
        print(f"{source.name} -> {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
