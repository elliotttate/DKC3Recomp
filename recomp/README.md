# DKC3 recompiler configuration

This directory contains source-only structural input for
[`snesrecomp`](../snesrecomp/README.md). It must not contain ROM bytes,
extracted assets, generated C, assembly source, comments, or game data.

The checked-in cfg files were mechanically derived from the public H4v0c21
DKC3 disassembly at revision `bed9689` (2025-08-01) by
`tools/ingest_dkc3_disasm.py`: 4,716 bounded function entries (direct
JSR/JSL targets plus the code labels named in `dw`/`dl` tables), 353
indirect dispatch contracts from symbolic state tables (2,274 targets), and
540 labelled data regions in the code banks. Dispatch sites that read a
pointer from RAM carry no contract and run on the interpreter tier.

`funcs.h` is the mechanically generated declaration index for these
entries; `scripts/generate_snesrecomp.py` refreshes it before each emit.
`.ingest/` holds the derived symbol files and is ignored by Git.

`bank00.cfg` declares only the architectural vectors (`auto_vectors`); the
game's code lives in the `$80`-`$BF` mirrors of the HiROM banks.
