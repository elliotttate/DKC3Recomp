#include "dkc3_kong_tracker.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Fail(const char *message, int line) {
  fprintf(stderr, "test_dkc3_kong_tracker.c:%d: %s\n", line, message);
  exit(1);
}

#define CHECK(cond)          \
  do {                       \
    if (!(cond))             \
      Fail(#cond, __LINE__); \
  } while (0)

enum { kWramSize = 0x20000, kRomSize = 0x400000, kWidth = 256, kHeight = 224 };

static uint8_t s_wram[kWramSize];
static uint8_t *s_rom;
static uint8_t s_sram[0x800];
static uint8_t s_frame[kHeight][kWidth * 4];

static void Write16(uint8_t *memory, size_t address, uint16_t value) {
  memory[address] = (uint8_t)value;
  memory[address + 1u] = (uint8_t)(value >> 8);
}

/* Recomputes the sidecar's FNV-1a checksum over its first 108 bytes. */
static void Reseal(uint8_t *data) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < 108; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  data[108] = (uint8_t)hash;
  data[109] = (uint8_t)(hash >> 8);
  data[110] = (uint8_t)(hash >> 16);
  data[111] = (uint8_t)(hash >> 24);
}

static void SetMainLoop(uint32_t pc24) {
  Write16(s_wram, kDkc3KongWramMainLoopVector, (uint16_t)pc24);
  s_wram[kDkc3KongWramMainLoopVector + 2u] = (uint8_t)(pc24 >> 16);
}

static size_t HiRomOffset(uint32_t pc24) {
  return ((size_t)((pc24 >> 16) & 0x3fu) << 16) | (uint16_t)pc24;
}

/* A synthetic bank $B4 with the shape the cartridge uses: world descriptors
 * at $CFB9, each pointing at a node table of 11-byte records ending in
 * $FF,$FF. World 0 is the overworld; worlds 1 and 2 hold two levels, a boss,
 * and a cave each. */
static void BuildRom(void) {
  memset(s_rom, 0, kRomSize);
  const uint16_t descriptor_base = 0xd000;
  const uint16_t table_base = 0xd100;
  static const uint8_t nodes[3][6][2] = {
      {{0x00, 0xff}, {0x43, 0x41}, {0x44, 0x42}, {0xcb, 0x03}, {0xff, 0xff}},
      {{0x00, 0xff}, {0x01, 0x25}, {0x83, 0x00}, {0x02, 0x28}, {0x88, 0x1d},
       {0xff, 0xff}},
      {{0x00, 0xff}, {0x0b, 0x30}, {0x91, 0x1e}, {0x0c, 0x32}, {0xff, 0xff}},
  };
  for (unsigned world = 0; world < 3; world++) {
    const uint16_t descriptor = (uint16_t)(descriptor_base + world * 0x10u);
    const uint16_t table = (uint16_t)(table_base + world * 0x80u);
    Write16(s_rom, HiRomOffset(kDkc3KongRomWorldDescriptors) + world * 2u,
            descriptor);
    Write16(s_rom,
            HiRomOffset(0xb40000u | descriptor) + kDkc3KongNodeTableOffset,
            table);
    for (unsigned node = 0; node < 6; node++) {
      const size_t record =
          HiRomOffset(0xb40000u | table) + node * kDkc3KongNodeStride;
      s_rom[record] = nodes[world][node][0];
      s_rom[record + 1u] = nodes[world][node][1];
      if (nodes[world][node][0] == 0xff) break;
    }
  }
}

/* Rewrites a slot's marker and checksums the way the cartridge's save does. */
static void SealSlot(uint8_t *data, uint16_t marker) {
  Write16(data, 4, marker);
  uint16_t sum = 0, parity = 0;
  for (unsigned offset = 6; offset < kDkc3KongSramSlotSize; offset += 2) {
    const uint16_t word = (uint16_t)(data[offset] | (data[offset + 1u] << 8));
    sum = (uint16_t)(sum + word);
    parity ^= word;
  }
  Write16(data, 0, sum ? sum : 1);
  Write16(data, 2, parity);
}

static void SetSlotData(unsigned slot, uint8_t seed) {
  uint8_t *data = s_sram + kDkc3KongSramSlotOffsets[slot];
  for (unsigned i = 0; i < kDkc3KongSramSlotSize; i++)
    data[i] = (uint8_t)(seed + i * 7u);
  SealSlot(data, 0x0152);
}

static void TestSlotValidity(void) {
  uint8_t slot[kDkc3KongSramSlotSize];
  memset(slot, 0, sizeof slot);
  CHECK(!Dkc3KongSramSlotValid(slot));
  CHECK(!Dkc3KongSramSlotValid(NULL));
  SealSlot(slot, 0x0052);
  CHECK(Dkc3KongSramSlotValid(slot)); /* all-zero body: sum 0 counts as 1 */
  CHECK(slot[0] == 1 && slot[1] == 0);
  for (unsigned i = 6; i < sizeof slot; i++) slot[i] = (uint8_t)(i * 13u);
  SealSlot(slot, 0x0253); /* bit 0 of the marker is ignored */
  CHECK(Dkc3KongSramSlotValid(slot));
  slot[100] ^= 1u;
  CHECK(!Dkc3KongSramSlotValid(slot)); /* sum and XOR disagree */
  slot[100] ^= 1u;
  SealSlot(slot, 0x0352);
  CHECK(!Dkc3KongSramSlotValid(slot)); /* unknown marker */
  SealSlot(slot, 0x0152);
  slot[0] = slot[1] = 0; /* the file select's erase */
  CHECK(!Dkc3KongSramSlotValid(slot));
}

static void TestFlagsAndFormat(void) {
  Dkc3KongTracker tracker;
  Dkc3KongTrackerInit(&tracker);
  CHECK(!Dkc3KongTrackerSetLevel(&tracker, 0, 0x1d)); /* boss */
  CHECK(!Dkc3KongTrackerSetLevel(&tracker, 3, 0x25)); /* bad slot */
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 1, 0x25));
  CHECK(!Dkc3KongTrackerSetLevel(&tracker, 1, 0x25));
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 2, 0x4c));
  CHECK(tracker.dirty);
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 1, 0x25));
  CHECK(!Dkc3KongTrackerHasLevel(&tracker, 0, 0x25));

  uint8_t data[kDkc3KongFileSize];
  CHECK(Dkc3KongTrackerSerialize(&tracker, data, sizeof data - 1) == 0);
  CHECK(Dkc3KongTrackerSerialize(&tracker, data, sizeof data) ==
        kDkc3KongFileSize);
  CHECK(memcmp(data, "DKC3KONG", 8) == 0);

  Dkc3KongTracker copy;
  Dkc3KongTrackerInit(&copy);
  CHECK(Dkc3KongTrackerDeserialize(&copy, data, sizeof data));
  CHECK(Dkc3KongTrackerHasLevel(&copy, 1, 0x25));
  CHECK(Dkc3KongTrackerHasLevel(&copy, 2, 0x4c));
  CHECK(!copy.dirty);

  data[20] ^= 1u;
  CHECK(!Dkc3KongTrackerDeserialize(&copy, data, sizeof data));
  data[20] ^= 1u;
  /* A future version with a valid checksum is rejected for its version. */
  data[8] = 2;
  Reseal(data);
  CHECK(!Dkc3KongTrackerDeserialize(&copy, data, sizeof data));
  /* A valid image is rejected only for its size. */
  data[8] = 1;
  Reseal(data);
  CHECK(Dkc3KongTrackerDeserialize(&copy, data, sizeof data));
  CHECK(!Dkc3KongTrackerDeserialize(&copy, data, sizeof data - 1));
}

static void TestFiles(void) {
  const char *path = "test_dkc3_kong_tracker.dat";
  remove(path);
  Dkc3KongTracker tracker;
  Dkc3KongTrackerInit(&tracker);
  CHECK(Dkc3KongTrackerLoadFile(&tracker, path) == kDkc3KongLoadMissing);
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x30));
  CHECK(Dkc3KongTrackerSaveFile(&tracker, path));
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x32));
  CHECK(Dkc3KongTrackerSaveFile(&tracker, path)); /* replaces the file */

  Dkc3KongTracker loaded;
  Dkc3KongTrackerInit(&loaded);
  CHECK(Dkc3KongTrackerLoadFile(&loaded, path) == kDkc3KongLoadOk);
  CHECK(Dkc3KongTrackerHasLevel(&loaded, 0, 0x30));
  CHECK(Dkc3KongTrackerHasLevel(&loaded, 0, 0x32));

  FILE *stream = fopen(path, "wb");
  CHECK(stream != NULL);
  fputs("not a tracker", stream);
  fclose(stream);
  CHECK(Dkc3KongTrackerLoadFile(&loaded, path) == kDkc3KongLoadInvalid);
  remove(path);
}

static void TestObserve(void) {
  Dkc3KongTracker tracker;
  Dkc3KongTrackerInit(&tracker);
  memset(s_wram, 0, sizeof s_wram);
  SetMainLoop(kDkc3KongMainLoopLevel);
  Write16(s_wram, kDkc3KongWramLevel, 0x29);
  Write16(s_wram, kDkc3KongWramFileSlot, 1);

  s_wram[kDkc3KongWramLetters] = 0x07; /* K, O, N */
  CHECK(!Dkc3KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  s_wram[kDkc3KongWramLetters] = 0x1f; /* plus G and an unrelated bit */
  SetMainLoop(kDkc3KongMainLoopMap);
  CHECK(!Dkc3KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  SetMainLoop(kDkc3KongMainLoopLevel);
  CHECK(Dkc3KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 1, 0x29));
  CHECK(!Dkc3KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));

  /* A bonus room credits its parent level. */
  Write16(s_wram, kDkc3KongWramLevel, 0x90);
  Write16(s_wram, kDkc3KongWramBonusEntry, 2);
  Write16(s_wram, kDkc3KongWramBonusParentLevel, 0x2f);
  CHECK(Dkc3KongActiveLevel(s_wram, sizeof s_wram) == 0x2f);
  CHECK(Dkc3KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 1, 0x2f));

  /* A stale bonus entry never overrides a main level. */
  Write16(s_wram, kDkc3KongWramLevel, 0x34);
  CHECK(Dkc3KongActiveLevel(s_wram, sizeof s_wram) == 0x34);

  /* Bosses and unknown slots are ignored. */
  Write16(s_wram, kDkc3KongWramLevel, 0x1e);
  Write16(s_wram, kDkc3KongWramBonusEntry, 0);
  CHECK(Dkc3KongActiveLevel(s_wram, sizeof s_wram) == -1);
  CHECK(!Dkc3KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  Write16(s_wram, kDkc3KongWramLevel, 0x30);
  Write16(s_wram, kDkc3KongWramFileSlot, 3);
  CHECK(!Dkc3KongTrackerObserveFrame(&tracker, s_wram, sizeof s_wram));
  CHECK(!Dkc3KongTrackerObserveFrame(&tracker, s_wram, 0x100));
}

/* SRAM changing outside the file select is an in-game save or a quick load,
 * never a copy or erase. */
static void TestSramOutsideFileSelect(void) {
  Dkc3KongTracker tracker;
  Dkc3KongTrackerInit(&tracker);
  memset(s_sram, 0, sizeof s_sram);
  SetSlotData(0, 1);
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x25));
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 1, 0x26));
  CHECK(!Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, false));

  /* A quick load restores SRAM from before slot 0 was ever saved: slot 0
   * turns invalid while the game is running. */
  uint8_t saved[kDkc3KongSramSlotSize];
  memcpy(saved, s_sram + kDkc3KongSramSlotOffsets[0], sizeof saved);
  memset(s_sram + kDkc3KongSramSlotOffsets[0], 0, kDkc3KongSramSlotSize);
  CHECK(!Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, false));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 0, 0x25));

  /* A quick load that leaves two slots identical is not a copy either. */
  memcpy(s_sram + kDkc3KongSramSlotOffsets[0], saved, sizeof saved);
  memcpy(s_sram + kDkc3KongSramSlotOffsets[1], saved, sizeof saved);
  CHECK(!Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, false));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 1, 0x26));
  CHECK(!Dkc3KongTrackerHasLevel(&tracker, 1, 0x25));

  /* After a quick load the host forgets the SRAM, so even a first sync that
   * lands in the file select only adopts it. */
  Dkc3KongTrackerForgetSram(&tracker);
  memset(s_sram + kDkc3KongSramSlotOffsets[0], 0, kDkc3KongSramSlotSize);
  CHECK(!Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 0, 0x25));
  Dkc3KongTrackerForgetSram(NULL);

  /* The real erase, in the file select, still clears. */
  memcpy(s_sram + kDkc3KongSramSlotOffsets[0], saved, sizeof saved);
  CHECK(!Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, false));
  Write16(s_sram, kDkc3KongSramSlotOffsets[0], 0);
  CHECK(Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));
  CHECK(!Dkc3KongTrackerHasLevel(&tracker, 0, 0x25));

  memset(s_wram, 0, sizeof s_wram);
  CHECK(!Dkc3KongInFileSelect(s_wram, sizeof s_wram));
  SetMainLoop(kDkc3KongMainLoopFileSelect);
  CHECK(Dkc3KongInFileSelect(s_wram, sizeof s_wram));
  CHECK(!Dkc3KongInFileSelect(s_wram, 0x100));
}

static void TestSramSync(void) {
  Dkc3KongTracker tracker;
  Dkc3KongTrackerInit(&tracker);
  memset(s_sram, 0, sizeof s_sram);
  SetSlotData(0, 1);
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x25));
  /* Slot 1 has never been saved in game: a file played through quick
   * saves. */
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 1, 0x26));
  tracker.dirty = false;

  /* Adopting the SRAM, even in the file select, keeps every flag. */
  CHECK(!Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));
  CHECK(!tracker.dirty);
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 0, 0x25));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 1, 0x26));
  CHECK(!Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));
  CHECK(!tracker.dirty);

  /* An ordinary save changes a slot without touching its flags. */
  s_sram[kDkc3KongSramSlotOffsets[0] + 40] ^= 0x55;
  SealSlot(s_sram + kDkc3KongSramSlotOffsets[0], 0x0152);
  CHECK(!Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 0, 0x25));

  /* A new game in an empty slot starts with no flags. */
  SetSlotData(2, 9);
  CHECK(!Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));

  /* Copy slot 0 onto slot 1. */
  memcpy(s_sram + kDkc3KongSramSlotOffsets[1],
         s_sram + kDkc3KongSramSlotOffsets[0], kDkc3KongSramSlotSize);
  CHECK(Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 1, 0x25));
  CHECK(!Dkc3KongTrackerHasLevel(&tracker, 1, 0x26)); /* replaced */
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 0, 0x25));

  /* Erase slot 0 the way the file select does (its first word zeroed, the
   * rest left behind) and fill slot 1 with a pattern. */
  Write16(s_sram, kDkc3KongSramSlotOffsets[0], 0);
  memset(s_sram + kDkc3KongSramSlotOffsets[1], 0x55, kDkc3KongSramSlotSize);
  tracker.dirty = false;
  CHECK(Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));
  CHECK(tracker.dirty);
  CHECK(!Dkc3KongTrackerHasLevel(&tracker, 0, 0x25));
  CHECK(!Dkc3KongTrackerHasLevel(&tracker, 1, 0x25));

  CHECK(!Dkc3KongTrackerSyncSram(&tracker, s_sram, 0x700, true));

  /* Two slots hold the same image with different flags (played without
   * saving after a copy); a copy onto the third keeps only the levels both
   * agree on. */
  Dkc3KongTrackerInit(&tracker);
  memset(s_sram, 0, sizeof s_sram);
  SetSlotData(0, 3);
  memcpy(s_sram + kDkc3KongSramSlotOffsets[1],
         s_sram + kDkc3KongSramSlotOffsets[0], kDkc3KongSramSlotSize);
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x25));
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x28));
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 1, 0x25));
  (void)Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true);
  memcpy(s_sram + kDkc3KongSramSlotOffsets[2],
         s_sram + kDkc3KongSramSlotOffsets[0], kDkc3KongSramSlotSize);
  CHECK(Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 2, 0x25));
  CHECK(!Dkc3KongTrackerHasLevel(&tracker, 2, 0x28));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 0, 0x28));

  /* One slot copied onto both others within one sync: each copy takes the
   * unchanged source's flags, not the intersection with the other
   * destination's old flags. */
  Dkc3KongTrackerInit(&tracker);
  memset(s_sram, 0, sizeof s_sram);
  SetSlotData(0, 3);
  SetSlotData(1, 5);
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x25));
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x28));
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 1, 0x30));
  (void)Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true);
  memcpy(s_sram + kDkc3KongSramSlotOffsets[1],
         s_sram + kDkc3KongSramSlotOffsets[0], kDkc3KongSramSlotSize);
  memcpy(s_sram + kDkc3KongSramSlotOffsets[2],
         s_sram + kDkc3KongSramSlotOffsets[0], kDkc3KongSramSlotSize);
  CHECK(Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));
  for (unsigned slot = 1; slot < 3; slot++) {
    CHECK(Dkc3KongTrackerHasLevel(&tracker, slot, 0x25));
    CHECK(Dkc3KongTrackerHasLevel(&tracker, slot, 0x28));
    CHECK(!Dkc3KongTrackerHasLevel(&tracker, slot, 0x30));
  }

  /* A slot saved and copied within one sync is still the source: no
   * unchanged slot holds the image, so the changed one is used. */
  Dkc3KongTrackerInit(&tracker);
  memset(s_sram, 0, sizeof s_sram);
  SetSlotData(0, 3);
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x25));
  (void)Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true);
  s_sram[kDkc3KongSramSlotOffsets[0] + 40] ^= 0x55;
  SealSlot(s_sram + kDkc3KongSramSlotOffsets[0], 0x0152);
  memcpy(s_sram + kDkc3KongSramSlotOffsets[1],
         s_sram + kDkc3KongSramSlotOffsets[0], kDkc3KongSramSlotSize);
  CHECK(Dkc3KongTrackerSyncSram(&tracker, s_sram, sizeof s_sram, true));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 1, 0x25));
  CHECK(Dkc3KongTrackerHasLevel(&tracker, 0, 0x25));
}

static void TestWorldsAndMapBadge(void) {
  BuildRom();
  uint8_t levels[8];
  CHECK(Dkc3KongWorldLevels(s_rom, kRomSize, 0, levels, 8) == 0);
  CHECK(Dkc3KongWorldLevels(s_rom, kRomSize, 1, levels, 8) == 2);
  CHECK(levels[0] == 0x25 && levels[1] == 0x28);
  CHECK(Dkc3KongWorldLevels(s_rom, kRomSize, 2, levels, 8) == 2);
  CHECK(levels[0] == 0x30 && levels[1] == 0x32);
  CHECK(Dkc3KongWorldLevels(s_rom, 0x1000, 1, levels, 8) == 0);
  /* A short or absent output still counts every level, writing only what
   * fits. */
  memset(levels, 0xee, sizeof levels);
  CHECK(Dkc3KongWorldLevels(s_rom, kRomSize, 1, levels, 1) == 2);
  CHECK(levels[0] == 0x25 && levels[1] == 0xee);
  CHECK(Dkc3KongWorldLevels(s_rom, kRomSize, 1, NULL, 0) == 2);

  Dkc3KongTracker tracker;
  Dkc3KongTrackerInit(&tracker);
  memset(s_wram, 0, sizeof s_wram);
  SetMainLoop(kDkc3KongMainLoopMap);
  Write16(s_wram, kDkc3KongWramFileSlot, 0);

  /* World 2, standing on level $30. */
  Write16(s_wram, kDkc3KongWramMapWorld, 2);
  Write16(s_wram, kDkc3KongWramMapNode, 1);
  CHECK(Dkc3KongTrackerMapBadge(&tracker, s_wram, sizeof s_wram, s_rom,
                                kRomSize) == kDkc3KongBadgeNone);
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x30));
  CHECK(Dkc3KongTrackerMapBadge(&tracker, s_wram, sizeof s_wram, s_rom,
                                kRomSize) == kDkc3KongBadgeLevel);
  /* The boss node never earns a badge. */
  Write16(s_wram, kDkc3KongWramMapNode, 2);
  CHECK(Dkc3KongTrackerMapBadge(&tracker, s_wram, sizeof s_wram, s_rom,
                                kRomSize) == kDkc3KongBadgeNone);
  /* Not while the map is not idle, nor for another slot. */
  Write16(s_wram, kDkc3KongWramMapNode, 1);
  SetMainLoop(kDkc3KongMainLoopLevel);
  CHECK(Dkc3KongTrackerMapBadge(&tracker, s_wram, sizeof s_wram, s_rom,
                                kRomSize) == kDkc3KongBadgeNone);
  SetMainLoop(kDkc3KongMainLoopMap);
  Write16(s_wram, kDkc3KongWramFileSlot, 1);
  CHECK(Dkc3KongTrackerMapBadge(&tracker, s_wram, sizeof s_wram, s_rom,
                                kRomSize) == kDkc3KongBadgeNone);
  Write16(s_wram, kDkc3KongWramFileSlot, 0);

  /* The overworld node for world 2 lights once both its levels are done. */
  Write16(s_wram, kDkc3KongWramMapWorld, 0);
  Write16(s_wram, kDkc3KongWramMapNode, 2);
  CHECK(!Dkc3KongTrackerWorldComplete(&tracker, 0, s_rom, kRomSize, 2));
  CHECK(Dkc3KongTrackerMapBadge(&tracker, s_wram, sizeof s_wram, s_rom,
                                kRomSize) == kDkc3KongBadgeNone);
  CHECK(Dkc3KongTrackerSetLevel(&tracker, 0, 0x32));
  CHECK(Dkc3KongTrackerWorldComplete(&tracker, 0, s_rom, kRomSize, 2));
  CHECK(Dkc3KongTrackerMapBadge(&tracker, s_wram, sizeof s_wram, s_rom,
                                kRomSize) == kDkc3KongBadgeWorld);
  /* World 1 is still open, and a non-world overworld node is ignored. */
  Write16(s_wram, kDkc3KongWramMapNode, 1);
  CHECK(Dkc3KongTrackerMapBadge(&tracker, s_wram, sizeof s_wram, s_rom,
                                kRomSize) == kDkc3KongBadgeNone);
  Write16(s_wram, kDkc3KongWramMapNode, 3);
  CHECK(Dkc3KongTrackerMapBadge(&tracker, s_wram, sizeof s_wram, s_rom,
                                kRomSize) == kDkc3KongBadgeNone);
}

static void TestMapSettle(void) {
  Dkc3KongMapSettle settle = {0, 0};
  memset(s_wram, 0, sizeof s_wram);
  SetMainLoop(kDkc3KongMainLoopMap);
  Write16(s_wram, kDkc3KongWramMapWorld, 2);
  Write16(s_wram, kDkc3KongWramMapNode, 1);
  for (int i = 1; i < kDkc3KongSettleFrames; i++)
    CHECK(!Dkc3KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  CHECK(Dkc3KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  CHECK(Dkc3KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));

  /* Setting off for another node restarts the wait. */
  Write16(s_wram, kDkc3KongWramMapNode, 3);
  CHECK(!Dkc3KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  for (int i = 2; i < kDkc3KongSettleFrames; i++)
    CHECK(!Dkc3KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  CHECK(Dkc3KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));

  /* Leaving the map resets it. */
  SetMainLoop(kDkc3KongMainLoopLevel);
  CHECK(!Dkc3KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  SetMainLoop(kDkc3KongMainLoopMap);
  CHECK(!Dkc3KongMapSettleUpdate(&settle, s_wram, sizeof s_wram));
  CHECK(!Dkc3KongMapSettleUpdate(NULL, s_wram, sizeof s_wram));
}

static void PaintName(int left, int right) {
  memset(s_frame, 0x40, sizeof s_frame);
  for (int y = 201; y <= 214; y++)
    for (int x = left; x <= right; x++) {
      uint8_t *pixel = &s_frame[y][x * 4];
      const bool ink = ((x + y) % 3) != 0;
      pixel[0] = 0x00;
      pixel[1] = ink ? 0xff : 0x00;
      pixel[2] = ink ? 0xff : 0x00;
    }
}

static bool Yellow(int x, int y) {
  const uint8_t *pixel = &s_frame[y][x * 4];
  return pixel[0] == 0x00 && pixel[1] == 0xff && pixel[2] == 0xff;
}

static int YellowIn(int x0, int x1, int y0, int y1) {
  int count = 0;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) count += Yellow(x, y);
  return count;
}

static void TestDrawBadge(void) {
  memset(s_frame, 0x40, sizeof s_frame);
  CHECK(!Dkc3KongDrawBadge(&s_frame[0][0], kWidth * 4, kWidth, kHeight));
  CHECK(YellowIn(0, kWidth - 1, 0, kHeight - 1) == 0);

  /* Beside a short name. */
  PaintName(90, 160);
  const int name = YellowIn(0, kWidth - 1, 0, kHeight - 1);
  CHECK(Dkc3KongDrawBadge(&s_frame[0][0], kWidth * 4, kWidth, kHeight));
  CHECK(YellowIn(170, 200, 201, 214) > 60);
  CHECK(YellowIn(0, kWidth - 1, 0, 199) == 0);
  CHECK(!Yellow(169, 207) && !Yellow(201, 207)); /* outline */
  CHECK(YellowIn(0, kWidth - 1, 0, kHeight - 1) > name);

  /* Above a name too wide to leave room. */
  PaintName(20, 236);
  CHECK(Dkc3KongDrawBadge(&s_frame[0][0], kWidth * 4, kWidth, kHeight));
  CHECK(YellowIn(0, kWidth - 1, 184, 197) > 60);
  CHECK(YellowIn(237, kWidth - 1, 201, 214) == 0);

  /* A wide frame keeps the badge on the name's right. */
  static uint8_t wide[kHeight][400 * 4];
  memset(wide, 0x40, sizeof wide);
  for (int y = 201; y <= 214; y++)
    for (int x = 150; x <= 240; x++) {
      wide[y][x * 4] = 0x00;
      wide[y][x * 4 + 1] = 0xff;
      wide[y][x * 4 + 2] = 0xff;
    }
  CHECK(Dkc3KongDrawBadge(&wide[0][0], 400 * 4, 400, kHeight));
  CHECK(wide[207][250 * 4 + 1] == 0xff && wide[207][250 * 4 + 2] == 0xff);

  CHECK(!Dkc3KongDrawBadge(NULL, kWidth * 4, kWidth, kHeight));
  CHECK(!Dkc3KongDrawBadge(&s_frame[0][0], kWidth * 4, kWidth, 200));
}

int main(void) {
  s_rom = (uint8_t *)calloc(1, kRomSize);
  CHECK(s_rom != NULL);
  CHECK(Dkc3KongBadgesEnabledFromEnv(NULL));
  CHECK(Dkc3KongBadgesEnabledFromEnv("1"));
  CHECK(!Dkc3KongBadgesEnabledFromEnv("0"));
  TestSlotValidity();
  TestFlagsAndFormat();
  TestFiles();
  TestObserve();
  TestSramSync();
  TestSramOutsideFileSelect();
  TestWorldsAndMapBadge();
  TestMapSettle();
  TestDrawBadge();
  free(s_rom);
  puts("test_dkc3_kong_tracker: ok");
  return 0;
}
