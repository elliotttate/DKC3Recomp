# Repository working agreement

## Purpose

Build a source-only native port foundation for the headerless North
American (En,Fr) SNES release of *Donkey Kong Country 3: Dixie Kong's Double
Trouble!*, the way DKC2Recomp was built. Preserve correctness evidence and
make every milestone reproducible.

## Content boundary

- Never add ROMs, save files, extracted graphics, music, BRR samples, level
  data, screenshots, or generated game binaries to the repository.
- The private ROM must remain outside Git. Its expected SHA-256 is
  `2277a2d8dddb01fe5cb0ae9a0fa225d42b3a11adccaeafa18e3c339b3794a32b`.
- The H4v0c21 DKC3 disassembly is GPL-3 and is consulted as an address and
  structure reference. The cfg files derived from it hold names, addresses,
  ranges, and dispatch contracts only; never copy its source, comments, or
  data.
- Record third-party source provenance, exact revision, local adaptations,
  and license text under `THIRD_PARTY_NOTICES.md`.

## Required workflow

1. Read `docs/BRINGUP.md` before changing the adapter, the ingester, or the
   build.
2. Add unit tests for public behavior. Use hashes and external ROM paths for
   private integration checks; the suite must pass without a ROM.
3. The 65816 interpreter in the snesrecomp runtime is the correctness oracle
   for everything the analysis does not prove. Never widen a cfg contract
   without disassembly structure behind it.
4. The native 256x224 image is the presentation oracle. Wider aspects show
   black margins until a DKC3 reconstruction passes its own evidence gates.
5. Record every material decision, measurement, and unverified step in
   `docs/BRINGUP.md`, and keep `README.md` current.
6. Report unverified work as unverified. The Windows host is carried over
   from DKC2Recomp and has not been built here.
