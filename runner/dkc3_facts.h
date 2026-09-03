#ifndef DKC3_FACTS_H
#define DKC3_FACTS_H

#include <stdint.h>

#include "common_rtl.h"

/* Cartridge facts the widescreen presentation keys on, for DKC3 USA (En,Fr),
 * established from the H4v0c21 disassembly and live memory of Lakeside
 * Limbo; docs/BRINGUP.md records the evidence for each. */
enum {
  /* The main loop runs through the vector at direct-page $4E/$50; a level
   * installs $B3:8076 there ($80:84BA), so that value identifies gameplay.
   * DKC3 keeps no DKC2-style mode word. */
  kDkc3WramMainLoopVector = 0x004e,
  kDkc3MainLoopLevel = 0xb38076,
  /* The camera copies the renderer and the terrain streamer read ($B7:E20D
   * copies camera_x_position $0493 to $196D, $1973 is camera y). The world
   * begins one screen in: a level's first map column sits at world x $100. */
  kDkc3WramCameraX = 0x196d,
  kDkc3WramCameraY = 0x1973,
  /* The camera clamp, the running maximum of the level's camera regions
   * less one screen ($B7:D71E). Zero outside a level. */
  kDkc3WramScrollLimitX = 0x04bc,
  kDkc3WramScrollLimitY = 0x04be,
  /* The decompressed level map at $7F:0000 (the long pointer at $8C is set
   * to $7F0000 by $B3:D917). This is the raw address of the map's first
   * column, the one at world x $100: the presentation subtracts the
   * one-screen origin itself (Dkc3VideoResolveEdgeTile), so a base that
   * also compensated for it ($FF00) read every margin one 256-pixel page
   * early. */
  kDkc3LevelMapBank = 0x7f,
  kDkc3LevelMapBase = 0x0000,
  /* The metatile definitions follow the map in bank $7F; their offset is
   * the word at $1967 ($B3:DAF7). Each is 32 bytes: four rows of four
   * tile words. */
  kDkc3WramMetatileBase = 0x1967,
  /* VRAM word address of the terrain ring the column and row streamers
   * fill ($B7:B8A0, $B7:BB27). */
  kDkc3WramTerrainVramBase = 0x1969,
  /* The map's shape, the low nibble of $0470 ($B3:DA62): 0 and 8 are
   * column-major with sixteen metatile rows (32 bytes a column), 1 is
   * column-major with thirty-two rows, 4 is row-major 64 bytes a row, 5
   * row-major 32 bytes, 6 and 9 row-major 192 bytes, 7 row-major 160. */
  kDkc3WramMapShape = 0x0470,
  kDkc3WramLevelNumber = 0x00c0,
  /* Pointer to the active Kong's sprite record; x_position is at +$12. */
  kDkc3WramActiveKongSprite = 0x04f9,
  kDkc3SpriteXPositionOffset = 0x12,
  kDkc3WorldOriginX = 0x100,
};

static inline uint16_t Dkc3FactRead16(uint16_t address) {
  return (uint16_t)(g_ram[address] | ((uint16_t)g_ram[(uint16_t)(address + 1u)] << 8));
}

static inline uint32_t Dkc3FactRead24(uint16_t address) {
  return (uint32_t)Dkc3FactRead16(address) |
         ((uint32_t)g_ram[(uint16_t)(address + 2u)] << 16);
}

static inline int Dkc3InLevel(void) {
  return Dkc3FactRead24(kDkc3WramMainLoopVector) == kDkc3MainLoopLevel;
}

/* The camera's maximum x and y; zero outside a level, which the prefill
 * treats as "no terrain". */
static inline uint16_t Dkc3MaximumScrollX(void) {
  return Dkc3InLevel() ? Dkc3FactRead16(kDkc3WramScrollLimitX) : 0;
}

static inline uint16_t Dkc3MaximumScrollY(void) {
  return Dkc3InLevel() ? Dkc3FactRead16(kDkc3WramScrollLimitY) : 0;
}

static inline uint16_t Dkc3MapShape(void) {
  return (uint16_t)(Dkc3FactRead16(kDkc3WramMapShape) & 0x0fu);
}

static inline uint32_t Dkc3PlayerX(void) {
  const uint16_t record = Dkc3FactRead16(kDkc3WramActiveKongSprite);
  if (record == 0) return 0;
  return Dkc3FactRead16((uint16_t)(record + kDkc3SpriteXPositionOffset));
}

#endif
