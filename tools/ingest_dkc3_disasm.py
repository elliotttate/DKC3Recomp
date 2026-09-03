#!/usr/bin/env python3
"""Derive snesrecomp bank configs for DKC3 from the H4v0c21 disassembly.

The disassembly (https://github.com/H4v0c21/DKC3-Disassembly) is byte-exact
asar source in one file per bank, with an address comment on every emitted
instruction line and an asar no$sns symbol file listing every label. This
tool turns three kinds of structural fact into cfg lines and nothing else:

* function entries: the direct JSR/JSL targets found in the sources plus
  the code labels named in `dw`/`dl` tables (the same policy as the DKC2
  importer in snesrecomp, whose entry harvester and cfg writer this tool
  reuses); each entry is bounded by the next entry in its bank;
* indirect dispatch contracts: every `JMP (table,x)` / `JSR (table,x)`
  site whose table is a symbolic `dw` list in the source, with the exact
  target set; sites that read a pointer from RAM are left to the runtime's
  interpreter, which is the failsafe for everything unproven;
* data regions: every labelled data block inside a code bank, bounded by
  the next label in that bank, so the analyzer never decodes a table.

The output holds only names, addresses, and structure. No assembly source,
comments, or bytes are copied. The disassembly is GPL-3; it is consulted as
an address and structure reference and is not redistributed here.

Usage:
    python3 tools/ingest_dkc3_disasm.py --disasm /path/to/DKC3-Disassembly \
        --output recomp

The symbol file `dkc3.sym` must exist beside the bank sources; the
disassembly's build writes it (it is ignored by that repository's Git).
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPOSITORY / "snesrecomp" / "tools"))

import ingest_dkc2_disasm as base  # noqa: E402

NOCASH_SYM_RE = re.compile(r"^([0-9A-Fa-f]{8})\s+:?(\S+)\s*$")
CALL_RE = re.compile(
    r"^\s*(JSR|JSL)(?:\.[bwl])?\s+([A-Za-z_.][A-Za-z0-9_.]*)\s*$", re.I)
SITE_RE = re.compile(
    r"^\s*(JMP|JSR)(?:\.[bwl])?\s+\(\s*([A-Za-z_.][A-Za-z0-9_.]*)\s*,\s*[xX]\s*\)")
ADDRESS_COMMENT_RE = re.compile(r";\s*\$([0-9A-Fa-f]{6})")
CODE_BANKS = frozenset(range(0x80, 0xC0))


def load_nocash_symbols(path: Path) -> list[tuple[int, str]]:
    """(pc24, name) for every label in an asar no$sns symbol file."""
    out = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = NOCASH_SYM_RE.match(line)
        if match:
            out.append((int(match.group(1), 16) & 0xFFFFFF, match.group(2)))
    return out


def write_wla_symbols(symbols, path: Path) -> None:
    path.write_text(
        "".join(f"{pc24 >> 16:02X}:{pc24 & 0xFFFF:04X} {name}\n"
                for pc24, name in symbols),
        encoding="utf-8")


def flatten(parent: str, name: str) -> str:
    """The symbol file's name for a dot-local label under its parent."""
    if name.startswith(".."):
        return f"{parent}_{name[2:]}"
    if name.startswith("."):
        return f"{parent}_{name[1:]}"
    return name


def bank_sources(disasm: Path):
    for path in sorted(disasm.glob("bank_*.asm")):
        yield path, path.read_text(encoding="utf-8", errors="replace").splitlines()


def collect_direct_targets(disasm: Path, by_name) -> tuple[set[str], Counter]:
    targets: set[str] = set()
    unresolved: Counter = Counter()
    for _path, lines in bank_sources(disasm):
        parent = ""
        for raw in lines:
            source = raw.split(";", 1)[0]
            label = base.GLOBAL_LABEL_RE.match(source.strip())
            if label:
                parent = label.group(1)
            match = CALL_RE.match(source)
            if not match:
                continue
            name = flatten(parent, match.group(2))
            if name in by_name:
                targets.add(name)
            else:
                unresolved[name] += 1
    return targets, unresolved


def collect_table_dispatches(disasm: Path, by_name) -> tuple[list, Counter]:
    """`JMP/JSR (table,x)` sites whose table is a symbolic dw list."""
    tables: dict[str, list[int]] = {}
    sites: list[tuple[int, str, str]] = []  # (site pc24, mode, table name)
    for _path, lines in bank_sources(disasm):
        parent = ""
        open_table: str | None = None
        for raw in lines:
            source = raw.split(";", 1)[0]
            stripped = source.strip()
            label = base.GLOBAL_LABEL_RE.match(stripped)
            local = base.LOCAL_LABEL_RE.match(stripped)
            if label:
                parent = label.group(1)
                open_table = label.group(1)
                tables.setdefault(open_table, [])
                continue
            if local:
                open_table = flatten(parent, local.group(1) + local.group(2))
                tables.setdefault(open_table, [])
                continue
            if not stripped:
                continue
            table = base.TABLE_RE.match(source)
            if table and open_table is not None:
                for token in base.SYMBOL_TOKEN_RE.findall(table.group(1)):
                    name = flatten(parent, token)
                    if name in by_name:
                        tables[open_table].append(by_name[name])
                continue
            open_table = None
            site = SITE_RE.match(source)
            if site:
                address = ADDRESS_COMMENT_RE.search(raw)
                if address:
                    mode = "ptrtail" if site.group(1).upper() == "JMP" else "ptrcall"
                    sites.append((int(address.group(1), 16),
                                  mode, flatten(parent, site.group(2))))
    contracts = []
    skipped: Counter = Counter()
    for pc24, mode, table_name in sites:
        targets = tables.get(table_name)
        if not targets:
            skipped["no symbolic table"] += 1
            continue
        contracts.append(base.DispatchContract(
            bank=(pc24 >> 16) & 0xFF, site_pc16=pc24 & 0xFFFF,
            targets=tuple(dict.fromkeys(targets)), mode=mode))
    return contracts, skipped


def collect_label_data_regions(disasm: Path, symbols) -> list[tuple[int, int, int]]:
    """Labelled data blocks in code banks, bounded by the next label."""
    label_kind, _table_refs = base.scan_disassembly(disasm)
    by_bank: dict[int, list[tuple[int, str]]] = defaultdict(list)
    for pc24, name in symbols:
        by_bank[(pc24 >> 16) & 0xFF].append((pc24 & 0xFFFF, name))
    regions = []
    for bank, items in by_bank.items():
        if bank not in CODE_BANKS:
            continue
        items.sort()
        for index, (start, name) in enumerate(items):
            if label_kind.get(name) != "data":
                continue
            end = items[index + 1][0] if index + 1 < len(items) else 0x10000
            if end > start:
                regions.append((bank, start, end))
    merged: list[tuple[int, int, int]] = []
    for bank, start, end in sorted(set(regions)):
        if merged and merged[-1][0] == bank and start <= merged[-1][2]:
            merged[-1] = (bank, merged[-1][1], max(merged[-1][2], end))
        else:
            merged.append((bank, start, end))
    return merged


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--disasm", type=Path, required=True,
                        help="H4v0c21 DKC3-Disassembly checkout with dkc3.sym")
    parser.add_argument("--output", type=Path, required=True,
                        help="cfg directory to write (recomp/)")
    parser.add_argument("--work", type=Path,
                        help="directory for the derived symbol files "
                             "(default: <output>/.ingest, ignored by Git)")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    symbol_path = args.disasm / "dkc3.sym"
    if not symbol_path.is_file():
        print(f"missing symbol file: {symbol_path}", file=sys.stderr)
        return 1
    symbols = load_nocash_symbols(symbol_path)
    by_name: dict[str, int] = {}
    for pc24, name in symbols:
        by_name.setdefault(name, pc24)

    work = args.work or args.output / ".ingest"
    work.mkdir(parents=True, exist_ok=True)
    full_sym = work / "dkc3_full.sym"
    entries_sym = work / "dkc3_entries.sym"
    write_wla_symbols(symbols, full_sym)
    direct, unresolved = collect_direct_targets(args.disasm, by_name)
    write_wla_symbols(
        sorted(((by_name[name], name) for name in direct)), entries_sym)

    entries = base.collect_entries(entries_sym, full_sym, args.disasm)
    dispatches, skipped = collect_table_dispatches(args.disasm, by_name)
    regions = collect_label_data_regions(args.disasm, symbols)

    histogram = Counter(entry.pc24 >> 16 for entry in entries)
    print(f"symbols: {len(symbols)}; direct-call targets: {len(direct)}"
          + (f"; unresolved call operands: {sum(unresolved.values())}"
             if unresolved else ""))
    print("entries: " + ", ".join(
        f"${bank:02X}:{count}" for bank, count in sorted(histogram.items())))
    print(f"dispatch contracts: {len(dispatches)} "
          f"({sum(len(d.targets) for d in dispatches)} targets)"
          + (f"; skipped {dict(skipped)}" if skipped else ""))
    print(f"data regions: {len(regions)}")
    if args.dry_run:
        return 0
    args.output.mkdir(parents=True, exist_ok=True)
    base.emit_cfg(entries, args.output, dispatches, regions)
    for path in sorted(args.output.glob("bank*.cfg")):
        text = path.read_text(encoding="utf-8")
        text = text.replace(
            "# Auto-generated by tools/ingest_dkc2_disasm.py.\n"
            "# Source: H4v0c21 byte-exact assembly + WLA symbols.",
            "# Auto-generated by tools/ingest_dkc3_disasm.py.\n"
            "# Source: H4v0c21 DKC3 byte-exact assembly + asar symbols.")
        path.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote cfg to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
