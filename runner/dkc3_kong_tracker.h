#ifndef DKC3_KONG_TRACKER_H
#define DKC3_KONG_TRACKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-side record of which levels have had all four K-O-N-G letters
 * collected, per save file, for DKC3 USA (En,Fr).
 *
 * The cartridge never saves KONG letters: they live only in WRAM while a
 * level runs. This tracker keeps its own sidecar file next to save.srm and
 * never writes the battery save, so save.srm stays byte-identical to what
 * the cartridge writes and remains portable to other emulators.
 *
 * Cartridge facts (docs/BRINGUP.md, "KONG letter tracker", records the
 * evidence for each):
 *
 * - $05D7 bits 0-3: K, O, N, G collected in the running level. The letter
 *   pickup ($BB:CD3A) sets its bit with TSB and awards the life
 *   ($B8:8084) when the low nibble becomes $F. A star barrel saves the byte
 *   to $05C3 and a respawn restores it ($BB:9AF6); leaving the level clears
 *   it ($BB:A270, $BB:A32A).
 * - $00C0: the current level number. A bonus room replaces it and parks the
 *   parent level in $04E6 while $04EA holds the bonus entry ($BB:A2EE,
 *   restored at $BB:A38B).
 * - $04C8: the active file slot, 0-2, which indexes the SRAM table at
 *   $80:9171 ($80:91CB). Slots start at SRAM $062, $2EC, and $576 and are
 *   $28A bytes long.
 * - $05E5: the current map's world, 0 for the Northern Kremisphere; $05E7
 *   the map node under the Kongs. The world's descriptor is the word at
 *   $B4:CFB9 + 2 * world, its node table the word at descriptor + 6, and a
 *   node record is 11 bytes whose byte 1 is the destination ($B4:B0C5,
 *   $B4:B804). On a world map a destination of $1D or more enters that level
 *   ($B4:B37B); on the Northern Kremisphere, destination $40|w opens world w
 *   ($B4:B31F). Levels with KONG letters are $25-$4C; $1D-$24 are bosses.
 * - The level-name text on the map is pure yellow (#FFFF00) with a black
 *   outline on native rows 201-214.
 */
enum {
  kDkc3KongSlotCount = 3,
  kDkc3KongLevelBitmapBytes = 32,
  kDkc3KongFirstLevel = 0x25,
  kDkc3KongLastLevel = 0x4c,
  kDkc3KongWorldCount = 8,

  kDkc3KongWramMainLoopVector = 0x004e,
  kDkc3KongWramLevel = 0x00c0,
  kDkc3KongWramFileSlot = 0x04c8,
  kDkc3KongWramBonusParentLevel = 0x04e6,
  kDkc3KongWramBonusEntry = 0x04ea,
  kDkc3KongWramLetters = 0x05d7,
  kDkc3KongWramMapWorld = 0x05e5,
  kDkc3KongWramMapNode = 0x05e7,
  kDkc3KongAllLetters = 0x0f,

  kDkc3KongMainLoopLevel = 0xb38076,
  kDkc3KongMainLoopMap = 0xb4b179,
  /* The file select, where its copy and erase run. */
  kDkc3KongMainLoopFileSelect = 0x809586,

  kDkc3KongRomWorldDescriptors = 0xb4cfb9,
  kDkc3KongNodeTableOffset = 6,
  kDkc3KongNodeStride = 11,
  kDkc3KongNodeDestination = 1,
  kDkc3KongMaxNodes = 32,
  kDkc3KongOverworldWorldFlag = 0x40,

  kDkc3KongSramSlotSize = 0x28a,

  kDkc3KongFileSize = 112,
};

extern const uint16_t kDkc3KongSramSlotOffsets[kDkc3KongSlotCount];

typedef struct Dkc3KongTracker {
  uint8_t levels[kDkc3KongSlotCount][kDkc3KongLevelBitmapBytes];
  uint8_t sram_shadow[kDkc3KongSlotCount][kDkc3KongSramSlotSize];
  bool sram_shadow_valid;
  /* Set whenever levels[] changes; the owner clears it after persisting. */
  bool dirty;
} Dkc3KongTracker;

typedef enum Dkc3KongBadge {
  kDkc3KongBadgeNone = 0,
  kDkc3KongBadgeLevel = 1,
  kDkc3KongBadgeWorld = 2,
} Dkc3KongBadge;

void Dkc3KongTrackerInit(Dkc3KongTracker *tracker);
bool Dkc3KongTrackerHasLevel(const Dkc3KongTracker *tracker, unsigned slot,
                             unsigned level);
/* Returns true when the flag was newly set. */
bool Dkc3KongTrackerSetLevel(Dkc3KongTracker *tracker, unsigned slot,
                             unsigned level);
void Dkc3KongTrackerClearSlot(Dkc3KongTracker *tracker, unsigned slot);

/* The sidecar format: "DKC3KONG", a little-endian version (1), three
 * 32-byte level bitmaps, and an FNV-1a checksum of everything before it. */
size_t Dkc3KongTrackerSerialize(const Dkc3KongTracker *tracker, uint8_t *out,
                                size_t capacity);
bool Dkc3KongTrackerDeserialize(Dkc3KongTracker *tracker, const uint8_t *data,
                                size_t size);
typedef enum Dkc3KongLoadResult {
  kDkc3KongLoadOk = 0,
  kDkc3KongLoadMissing = 1,
  kDkc3KongLoadInvalid = 2,
} Dkc3KongLoadResult;
Dkc3KongLoadResult Dkc3KongTrackerLoadFile(Dkc3KongTracker *tracker,
                                           const char *path);
/* Writes path.tmp and replaces path with it. */
bool Dkc3KongTrackerSaveFile(const Dkc3KongTracker *tracker, const char *path);

/* The level whose letters $05D7 holds: $00C0 inside a main level, the parked
 * parent inside a bonus room, or -1 when neither is a KONG level. */
int Dkc3KongActiveLevel(const uint8_t *wram, size_t wram_size);
/* Records the active level for the active slot once all four letters are
 * held in a running level. Returns true when the flag was newly set. */
bool Dkc3KongTrackerObserveFrame(Dkc3KongTracker *tracker,
                                 const uint8_t *wram, size_t wram_size);
/* The cartridge's own slot check ($80:9141): the word at +4, bit 0 ignored,
 * is $52, $152, or $252, and the words at +0 and +2 are the sum (0 counts
 * as 1) and the XOR of the words from +6 to the end ($80:9177). Erase
 * zeroes only the word at +0. */
bool Dkc3KongSramSlotValid(const uint8_t *slot);
/* Follows the file select's copy and erase. With follow_edits set (the
 * file select is running), a slot whose SRAM changed and no longer passes
 * the slot check loses its flags, and a slot that changed to an exact copy
 * of another valid slot takes that slot's flags. Without it, or on the first
 * call after Dkc3KongTrackerForgetSram, the SRAM is only adopted: a slot that
 * has never been saved in game (a file played through quick saves) keeps its
 * flags, and a quick load that brings back older SRAM is not an erase.
 * Returns true when any flag changed. */
bool Dkc3KongTrackerSyncSram(Dkc3KongTracker *tracker, const uint8_t *sram,
                             size_t sram_size, bool follow_edits);
/* Makes the next sync adopt the SRAM as it is (after a quick load). */
void Dkc3KongTrackerForgetSram(Dkc3KongTracker *tracker);
/* Whether the main loop is the file select, where copy and erase run. */
bool Dkc3KongInFileSelect(const uint8_t *wram, size_t wram_size);

/* Writes the KONG level numbers of world 1-8 into out and returns how many
 * there were (0 when the ROM tables are out of range). */
size_t Dkc3KongWorldLevels(const uint8_t *rom, size_t rom_size,
                           unsigned world, uint8_t *out, size_t capacity);
bool Dkc3KongTrackerWorldComplete(const Dkc3KongTracker *tracker,
                                  unsigned slot, const uint8_t *rom,
                                  size_t rom_size, unsigned world);
/* What the map node under the Kongs earns, if the map is idle. */
Dkc3KongBadge Dkc3KongTrackerMapBadge(const Dkc3KongTracker *tracker,
                                      const uint8_t *wram, size_t wram_size,
                                      const uint8_t *rom, size_t rom_size);

/* The node index changes the moment the Kongs set off, while the departing
 * name is still being erased, and the next name appears only after they
 * arrive. A badge is therefore shown only once the map has stayed idle on
 * one node for kDkc3KongSettleFrames game frames. */
enum { kDkc3KongSettleFrames = 16 };
typedef struct Dkc3KongMapSettle {
  uint32_t key;
  uint32_t frames;
} Dkc3KongMapSettle;
/* Call once per game frame; returns true while the map node is settled. */
bool Dkc3KongMapSettleUpdate(Dkc3KongMapSettle *settle, const uint8_t *wram,
                             size_t wram_size);

/* Draws "KONG" beside the map's level-name text in a 32-bit BGRX frame of
 * any width. Returns false, drawing nothing, when no name text is on
 * screen. */
bool Dkc3KongDrawBadge(uint8_t *pixels, size_t pitch, int width, int height);

bool Dkc3KongBadgesEnabledFromEnv(const char *env_value);

#ifdef __cplusplus
}
#endif

#endif
