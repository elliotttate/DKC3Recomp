#include "dkc3_kong_tracker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* Replace `path` with `temporary` in one step, so a failure leaves the old
 * file in place. POSIX rename() already replaces atomically; Windows
 * rename() refuses an existing target. */
static bool KongReplaceFile(const char *temporary, const char *path) {
#ifdef _WIN32
  return MoveFileExA(temporary, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(temporary, path) == 0;
#endif
}

const uint16_t kDkc3KongSramSlotOffsets[kDkc3KongSlotCount] = {
    0x062, 0x2ec, 0x576};

static const uint8_t kMagic[8] = {'D', 'K', 'C', '3', 'K', 'O', 'N', 'G'};
enum { kFileVersion = 1 };

void Dkc3KongTrackerInit(Dkc3KongTracker *tracker) {
  if (tracker) memset(tracker, 0, sizeof *tracker);
}

static bool IsKongLevel(unsigned level) {
  return level >= kDkc3KongFirstLevel && level <= kDkc3KongLastLevel;
}

bool Dkc3KongTrackerHasLevel(const Dkc3KongTracker *tracker, unsigned slot,
                             unsigned level) {
  if (!tracker || slot >= kDkc3KongSlotCount || level > 0xffu) return false;
  return (tracker->levels[slot][level >> 3] >> (level & 7u)) & 1u;
}

bool Dkc3KongTrackerSetLevel(Dkc3KongTracker *tracker, unsigned slot,
                             unsigned level) {
  if (!tracker || slot >= kDkc3KongSlotCount || !IsKongLevel(level))
    return false;
  if (Dkc3KongTrackerHasLevel(tracker, slot, level)) return false;
  tracker->levels[slot][level >> 3] |= (uint8_t)(1u << (level & 7u));
  tracker->dirty = true;
  return true;
}

static bool SlotHasFlags(const Dkc3KongTracker *tracker, unsigned slot) {
  for (unsigned i = 0; i < kDkc3KongLevelBitmapBytes; i++)
    if (tracker->levels[slot][i]) return true;
  return false;
}

void Dkc3KongTrackerClearSlot(Dkc3KongTracker *tracker, unsigned slot) {
  if (!tracker || slot >= kDkc3KongSlotCount) return;
  if (SlotHasFlags(tracker, slot)) tracker->dirty = true;
  memset(tracker->levels[slot], 0, sizeof tracker->levels[slot]);
}

static uint32_t Fnv1a(const uint8_t *data, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

static void Put32(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16);
  out[3] = (uint8_t)(value >> 24);
}

static uint32_t Get32(const uint8_t *in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
         ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

size_t Dkc3KongTrackerSerialize(const Dkc3KongTracker *tracker, uint8_t *out,
                                size_t capacity) {
  if (!tracker || !out || capacity < kDkc3KongFileSize) return 0;
  memcpy(out, kMagic, sizeof kMagic);
  Put32(out + 8, kFileVersion);
  memcpy(out + 12, tracker->levels, sizeof tracker->levels);
  Put32(out + 108, Fnv1a(out, 108));
  return kDkc3KongFileSize;
}

bool Dkc3KongTrackerDeserialize(Dkc3KongTracker *tracker, const uint8_t *data,
                                size_t size) {
  if (!tracker || !data || size != kDkc3KongFileSize) return false;
  if (memcmp(data, kMagic, sizeof kMagic) != 0) return false;
  if (Get32(data + 8) != kFileVersion) return false;
  if (Get32(data + 108) != Fnv1a(data, 108)) return false;
  memcpy(tracker->levels, data + 12, sizeof tracker->levels);
  /* Only KONG levels are ever recorded; drop anything else. */
  for (unsigned slot = 0; slot < kDkc3KongSlotCount; slot++)
    for (unsigned level = 0; level < 256u; level++)
      if (!IsKongLevel(level))
        tracker->levels[slot][level >> 3] &= (uint8_t)~(1u << (level & 7u));
  tracker->dirty = false;
  return true;
}

Dkc3KongLoadResult Dkc3KongTrackerLoadFile(Dkc3KongTracker *tracker,
                                           const char *path) {
  if (!tracker || !path) return kDkc3KongLoadInvalid;
  FILE *stream = fopen(path, "rb");
  if (!stream) return kDkc3KongLoadMissing;
  uint8_t data[kDkc3KongFileSize + 1];
  size_t size = fread(data, 1, sizeof data, stream);
  fclose(stream);
  return Dkc3KongTrackerDeserialize(tracker, data, size)
             ? kDkc3KongLoadOk
             : kDkc3KongLoadInvalid;
}

bool Dkc3KongTrackerSaveFile(const Dkc3KongTracker *tracker,
                             const char *path) {
  if (!tracker || !path) return false;
  uint8_t data[kDkc3KongFileSize];
  if (Dkc3KongTrackerSerialize(tracker, data, sizeof data) != sizeof data)
    return false;
  size_t length = strlen(path);
  char *temporary = (char *)malloc(length + 5);
  if (!temporary) return false;
  memcpy(temporary, path, length);
  memcpy(temporary + length, ".tmp", 5);
  FILE *stream = fopen(temporary, "wb");
  bool ok = stream != NULL;
  if (ok) ok = fwrite(data, 1, sizeof data, stream) == sizeof data;
  if (stream && fclose(stream) != 0) ok = false;
  if (ok) ok = KongReplaceFile(temporary, path);
  if (!ok) remove(temporary);
  free(temporary);
  return ok;
}

static uint16_t Read16(const uint8_t *wram, uint16_t address) {
  return (uint16_t)(wram[address] | ((uint16_t)wram[address + 1u] << 8));
}

static uint32_t MainLoop(const uint8_t *wram) {
  return (uint32_t)Read16(wram, kDkc3KongWramMainLoopVector) |
         ((uint32_t)wram[kDkc3KongWramMainLoopVector + 2u] << 16);
}

static bool WramUsable(const uint8_t *wram, size_t wram_size) {
  return wram && wram_size >= 0x2000u;
}

int Dkc3KongActiveLevel(const uint8_t *wram, size_t wram_size) {
  if (!WramUsable(wram, wram_size)) return -1;
  const unsigned level = Read16(wram, kDkc3KongWramLevel);
  if (IsKongLevel(level)) return (int)level;
  if (Read16(wram, kDkc3KongWramBonusEntry) != 0) {
    const unsigned parent = Read16(wram, kDkc3KongWramBonusParentLevel);
    if (IsKongLevel(parent)) return (int)parent;
  }
  return -1;
}

bool Dkc3KongTrackerObserveFrame(Dkc3KongTracker *tracker,
                                 const uint8_t *wram, size_t wram_size) {
  if (!tracker || !WramUsable(wram, wram_size)) return false;
  if (MainLoop(wram) != kDkc3KongMainLoopLevel) return false;
  if ((wram[kDkc3KongWramLetters] & kDkc3KongAllLetters) !=
      kDkc3KongAllLetters)
    return false;
  const unsigned slot = Read16(wram, kDkc3KongWramFileSlot);
  const int level = Dkc3KongActiveLevel(wram, wram_size);
  if (slot >= kDkc3KongSlotCount || level < 0) return false;
  return Dkc3KongTrackerSetLevel(tracker, slot, (unsigned)level);
}

static uint16_t SlotWord(const uint8_t *slot, unsigned offset) {
  return (uint16_t)(slot[offset] | (slot[offset + 1u] << 8));
}

bool Dkc3KongSramSlotValid(const uint8_t *slot) {
  if (!slot) return false;
  const uint16_t marker = (uint16_t)(SlotWord(slot, 4) & 0xfffeu);
  if (marker != 0x0052u && marker != 0x0152u && marker != 0x0252u)
    return false;
  uint16_t sum = 0, parity = 0;
  for (unsigned offset = 6; offset < kDkc3KongSramSlotSize; offset += 2) {
    sum = (uint16_t)(sum + SlotWord(slot, offset));
    parity ^= SlotWord(slot, offset);
  }
  if (sum == 0) sum = 1;
  return SlotWord(slot, 0) == sum && SlotWord(slot, 2) == parity;
}

static bool SlotBlank(const uint8_t *slot) {
  return !Dkc3KongSramSlotValid(slot);
}

void Dkc3KongTrackerForgetSram(Dkc3KongTracker *tracker) {
  if (tracker) tracker->sram_shadow_valid = false;
}

bool Dkc3KongInFileSelect(const uint8_t *wram, size_t wram_size) {
  return WramUsable(wram, wram_size) &&
         MainLoop(wram) == kDkc3KongMainLoopFileSelect;
}

bool Dkc3KongTrackerSyncSram(Dkc3KongTracker *tracker, const uint8_t *sram,
                             size_t sram_size, bool follow_edits) {
  if (!tracker || !sram ||
      sram_size < (size_t)kDkc3KongSramSlotOffsets[kDkc3KongSlotCount - 1] +
                      kDkc3KongSramSlotSize)
    return false;
  const uint8_t *slots[kDkc3KongSlotCount];
  for (unsigned i = 0; i < kDkc3KongSlotCount; i++)
    slots[i] = sram + kDkc3KongSramSlotOffsets[i];

  const bool was_dirty = tracker->dirty;
  tracker->dirty = false;
  /* Only the file select's own copy and erase are followed. An invalid slot
   * at adoption may be a file played only through quick saves, and SRAM
   * changing elsewhere is an in-game save or a quick load of older SRAM;
   * neither says anything about the letters, so the SRAM is just adopted. */
  if (tracker->sram_shadow_valid && follow_edits) {
    uint8_t before[kDkc3KongSlotCount][kDkc3KongLevelBitmapBytes];
    memcpy(before, tracker->levels, sizeof before);
    for (unsigned j = 0; j < kDkc3KongSlotCount; j++) {
      if (memcmp(slots[j], tracker->sram_shadow[j], kDkc3KongSramSlotSize) ==
          0)
        continue;
      if (SlotBlank(slots[j])) {
        Dkc3KongTrackerClearSlot(tracker, j);
        continue;
      }
      /* A slot that now matches another slot's image was copied from it.
       * Copy from the flags as they were before this sync, so a copy onto a
       * slot that also changed this frame cannot chain. When several slots
       * hold the same image (a slot copied twice, or played without saving
       * after a copy), the source is ambiguous: keep only the levels every
       * candidate agrees on, so no candidate's extra badges are carried
       * onto the wrong file. A candidate that also changed this sync is
       * usually another destination of the same copy, and its pre-sync
       * flags belong to the file it replaced, so unchanged candidates are
       * preferred; changed ones are used only when no unchanged slot holds
       * the image (a slot saved and copied within one sync), and then only
       * if they held a file before this sync. */
      uint8_t copied[kDkc3KongLevelBitmapBytes];
      bool any_source = false;
      for (int pass = 0; pass < 2 && !any_source; pass++) {
        for (unsigned i = 0; i < kDkc3KongSlotCount; i++) {
          if (i == j || SlotBlank(slots[i]) ||
              memcmp(slots[i], slots[j], kDkc3KongSramSlotSize) != 0)
            continue;
          const bool changed = memcmp(slots[i], tracker->sram_shadow[i],
                                      kDkc3KongSramSlotSize) != 0;
          if (changed != (pass == 1)) continue;
          /* A slot that was blank before this sync cannot be the source. */
          if (changed && SlotBlank(tracker->sram_shadow[i])) continue;
          if (!any_source) {
            memcpy(copied, before[i], sizeof copied);
            any_source = true;
          } else {
            for (unsigned b = 0; b < kDkc3KongLevelBitmapBytes; b++)
              copied[b] &= before[i][b];
          }
        }
      }
      if (any_source &&
          memcmp(tracker->levels[j], copied, sizeof copied) != 0) {
        memcpy(tracker->levels[j], copied, sizeof copied);
        tracker->dirty = true;
      }
    }
  }
  for (unsigned i = 0; i < kDkc3KongSlotCount; i++)
    memcpy(tracker->sram_shadow[i], slots[i], kDkc3KongSramSlotSize);
  tracker->sram_shadow_valid = true;
  const bool changed = tracker->dirty;
  tracker->dirty = was_dirty || changed;
  return changed;
}

static bool RomByte(const uint8_t *rom, size_t rom_size, uint32_t pc24,
                    uint8_t *out) {
  const uint8_t bank = (uint8_t)(pc24 >> 16);
  const uint16_t address = (uint16_t)pc24;
  /* HiROM: banks $C0-$FF and the $8000 half of $80-$BF map the same 64 KiB
   * pages. */
  if (bank < 0xc0u && address < 0x8000u) return false;
  const size_t offset = ((size_t)(bank & 0x3fu) << 16) | address;
  if (!rom || offset >= rom_size) return false;
  *out = rom[offset];
  return true;
}

static bool RomWord(const uint8_t *rom, size_t rom_size, uint32_t pc24,
                    uint16_t *out) {
  uint8_t lo, hi;
  if (!RomByte(rom, rom_size, pc24, &lo) ||
      !RomByte(rom, rom_size, (pc24 & 0xff0000u) | (uint16_t)(pc24 + 1u), &hi))
    return false;
  *out = (uint16_t)(lo | (hi << 8));
  return true;
}

static bool NodeTable(const uint8_t *rom, size_t rom_size, unsigned world,
                      uint16_t *table) {
  const uint32_t bank = kDkc3KongRomWorldDescriptors & 0xff0000u;
  uint16_t descriptor;
  if (!RomWord(rom, rom_size,
               (uint32_t)kDkc3KongRomWorldDescriptors + world * 2u,
               &descriptor))
    return false;
  return RomWord(rom, rom_size,
                 bank | (uint16_t)(descriptor + kDkc3KongNodeTableOffset),
                 table);
}

static bool NodeDestination(const uint8_t *rom, size_t rom_size,
                            uint16_t table, unsigned node, uint8_t *kind,
                            uint8_t *destination) {
  const uint32_t bank = kDkc3KongRomWorldDescriptors & 0xff0000u;
  const uint16_t record = (uint16_t)(table + node * kDkc3KongNodeStride);
  return RomByte(rom, rom_size, bank | record, kind) &&
         RomByte(rom, rom_size,
                 bank | (uint16_t)(record + kDkc3KongNodeDestination),
                 destination);
}

size_t Dkc3KongWorldLevels(const uint8_t *rom, size_t rom_size,
                           unsigned world, uint8_t *out, size_t capacity) {
  if (world == 0 || world > kDkc3KongWorldCount) return 0;
  uint16_t table;
  if (!NodeTable(rom, rom_size, world, &table)) return 0;
  /* Uniqueness is tracked here, not in `out`, which may be shorter than the
   * list or absent. At most one entry per node. */
  uint8_t found[kDkc3KongMaxNodes];
  size_t count = 0;
  for (unsigned node = 0; node < kDkc3KongMaxNodes; node++) {
    uint8_t kind, destination;
    if (!NodeDestination(rom, rom_size, table, node, &kind, &destination))
      return 0;
    if (kind == 0xffu && destination == 0xffu) break;
    if (!IsKongLevel(destination)) continue;
    bool seen = false;
    for (size_t i = 0; i < count; i++) seen = seen || found[i] == destination;
    if (seen) continue;
    found[count] = destination;
    if (count < capacity && out) out[count] = destination;
    count++;
  }
  return count;
}

bool Dkc3KongTrackerWorldComplete(const Dkc3KongTracker *tracker,
                                  unsigned slot, const uint8_t *rom,
                                  size_t rom_size, unsigned world) {
  uint8_t levels[kDkc3KongMaxNodes];
  const size_t count =
      Dkc3KongWorldLevels(rom, rom_size, world, levels, sizeof levels);
  if (count == 0 || count > sizeof levels) return false;
  for (size_t i = 0; i < count; i++)
    if (!Dkc3KongTrackerHasLevel(tracker, slot, levels[i])) return false;
  return true;
}

Dkc3KongBadge Dkc3KongTrackerMapBadge(const Dkc3KongTracker *tracker,
                                      const uint8_t *wram, size_t wram_size,
                                      const uint8_t *rom, size_t rom_size) {
  if (!tracker || !WramUsable(wram, wram_size)) return kDkc3KongBadgeNone;
  if (MainLoop(wram) != kDkc3KongMainLoopMap) return kDkc3KongBadgeNone;
  const unsigned slot = Read16(wram, kDkc3KongWramFileSlot);
  const unsigned world = Read16(wram, kDkc3KongWramMapWorld);
  const unsigned node = Read16(wram, kDkc3KongWramMapNode);
  if (slot >= kDkc3KongSlotCount || world > kDkc3KongWorldCount ||
      node >= kDkc3KongMaxNodes)
    return kDkc3KongBadgeNone;
  uint16_t table;
  uint8_t kind, destination;
  if (!NodeTable(rom, rom_size, world, &table) ||
      !NodeDestination(rom, rom_size, table, node, &kind, &destination))
    return kDkc3KongBadgeNone;
  if (world == 0) {
    if ((destination & kDkc3KongOverworldWorldFlag) == 0)
      return kDkc3KongBadgeNone;
    const unsigned target = destination & 0x3fu;
    return Dkc3KongTrackerWorldComplete(tracker, slot, rom, rom_size, target)
               ? kDkc3KongBadgeWorld
               : kDkc3KongBadgeNone;
  }
  return Dkc3KongTrackerHasLevel(tracker, slot, destination)
             ? kDkc3KongBadgeLevel
             : kDkc3KongBadgeNone;
}

bool Dkc3KongMapSettleUpdate(Dkc3KongMapSettle *settle, const uint8_t *wram,
                             size_t wram_size) {
  if (!settle) return false;
  if (!WramUsable(wram, wram_size) ||
      MainLoop(wram) != kDkc3KongMainLoopMap) {
    settle->key = 0;
    settle->frames = 0;
    return false;
  }
  const uint32_t key = 0x80000000u |
                       ((uint32_t)wram[kDkc3KongWramMapWorld] << 16) |
                       ((uint32_t)wram[kDkc3KongWramMapNode] << 8) |
                       wram[kDkc3KongWramFileSlot];
  if (key != settle->key) {
    settle->key = key;
    settle->frames = 0;
  }
  if (settle->frames < kDkc3KongSettleFrames) settle->frames++;
  return settle->frames >= kDkc3KongSettleFrames;
}

/* 7x7 glyphs, doubled vertically to the 14 rows of the map's name font. */
static const char *const kGlyphs[4][7] = {
    {"##...##", "##..##.", "##.##..", "####...", "##.##..", "##..##.",
     "##...##"},
    {".#####.", "##...##", "##...##", "##...##", "##...##", "##...##",
     ".#####."},
    {"##...##", "###..##", "####.##", "##.####", "##..###", "##...##",
     "##...##"},
    {".#####.", "##...##", "##.....", "##..###", "##...##", "##...##",
     ".#####."},
};

enum {
  kNameTop = 201,
  kNameBottom = 214,
  kNameMinimumPixels = 24,
  kGlyphWidth = 7,
  kGlyphAdvance = 8,
  kBadgeWidth = 4 * kGlyphAdvance - 1,
  kBadgeHeight = 14,
  kBadgeGap = 10,
};

static bool IsNameYellow(const uint8_t *pixel) {
  return pixel[0] == 0x00 && pixel[1] == 0xff && pixel[2] == 0xff;
}

static bool BadgeInk(int x, int y) {
  if (x < 0 || y < 0 || x >= kBadgeWidth || y >= kBadgeHeight) return false;
  const int glyph = x / kGlyphAdvance;
  const int column = x % kGlyphAdvance;
  if (column >= kGlyphWidth) return false;
  return kGlyphs[glyph][y / 2][column] == '#';
}

static void PutPixel(uint8_t *pixels, size_t pitch, int width, int height,
                     int x, int y, uint8_t b, uint8_t g, uint8_t r) {
  if (x < 0 || y < 0 || x >= width || y >= height) return;
  uint8_t *pixel = pixels + (size_t)y * pitch + (size_t)x * 4u;
  pixel[0] = b;
  pixel[1] = g;
  pixel[2] = r;
}

bool Dkc3KongDrawBadge(uint8_t *pixels, size_t pitch, int width, int height) {
  if (!pixels || width <= 0 || height <= kNameBottom ||
      pitch < (size_t)width * 4u)
    return false;
  int left = width, right = -1, count = 0;
  for (int y = kNameTop; y <= kNameBottom; y++) {
    const uint8_t *row = pixels + (size_t)y * pitch;
    for (int x = 0; x < width; x++) {
      if (!IsNameYellow(row + (size_t)x * 4u)) continue;
      if (x < left) left = x;
      if (x > right) right = x;
      count++;
    }
  }
  if (count < kNameMinimumPixels) return false;

  int origin_x = right + kBadgeGap;
  int origin_y = kNameTop;
  if (origin_x + kBadgeWidth + 1 >= width) {
    /* No room beside the name: centre the badge above it. */
    origin_x = (left + right) / 2 - kBadgeWidth / 2;
    origin_y = kNameTop - kBadgeHeight - 3;
  }
  for (int y = -1; y <= kBadgeHeight; y++)
    for (int x = -1; x <= kBadgeWidth; x++) {
      if (BadgeInk(x, y)) continue;
      bool edge = false;
      for (int dy = -1; dy <= 1 && !edge; dy++)
        for (int dx = -1; dx <= 1 && !edge; dx++)
          edge = BadgeInk(x + dx, y + dy);
      if (edge)
        PutPixel(pixels, pitch, width, height, origin_x + x, origin_y + y,
                 0x00, 0x00, 0x00);
    }
  for (int y = 0; y < kBadgeHeight; y++)
    for (int x = 0; x < kBadgeWidth; x++)
      if (BadgeInk(x, y))
        PutPixel(pixels, pitch, width, height, origin_x + x, origin_y + y,
                 0x00, 0xff, 0xff);
  return true;
}

bool Dkc3KongBadgesEnabledFromEnv(const char *env_value) {
  return !(env_value && strcmp(env_value, "0") == 0);
}
