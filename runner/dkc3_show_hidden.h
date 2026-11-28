#ifndef DKC3_SHOW_HIDDEN_H
#define DKC3_SHOW_HIDDEN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cartridge facts for DKC3 USA (En,Fr) hidden sprites, verified from the
 * sprite renderer loop at $B7:8857:
 *
 * - $B7:8908 reads the slot's render word (+$26). With bit 15 clear the
 *   sprite takes the normal camera-relative draw at $B7:8910.
 * - With bit 15 set, $B7:89C4 shifts the word left: when bit 14 is also set
 *   the renderer jumps to $B7:89E5, which copies +$20 into +$22 and skips the
 *   sprite (no OAM). Otherwise bits 1..4 index the variant table at
 *   $B7:89CF (screen-space HUD draws, palette-flash variants, and so on).
 * - So a slot whose render word has both bits 15 and 14 set is alive and
 *   animated but invisible; the hidden bear coin (type $0284, behavior
 *   $BB:D24E) spawns with +$26 = $C000.
 * - Kong slots are $0878 (Dixie) and $08E6 (Kiddy); DP $7C holds the slot of
 *   the animal buddy the active Kong is riding, whose own slot is hidden
 *   while the Kong slot draws it.
 */
/*
 * Not every hidden slot is a sprite with graphics. On the world map, the
 * controller in slot $1034 (type $0198) is hidden with +$26 = $C000, keeps a
 * counter in the graphic word +$24 ($0001, $0101, $0201, ...), and has OAM
 * attribute +$1E = $3000 (priority only: tile base 0, palette 0). Drawing it
 * as a sprite reads frame data the game never meant to draw and leaves the
 * OAM write pointer (DP $82) where the end-of-frame clear at $80:8992 never
 * terminates. So a slot is revealed only during level gameplay (main loop
 * vector DP $4E = $B3:8076) and only when its OAM attribute names a tile
 * base or palette, as the bear coin's $2760 does.
 */
enum {
  kDkc3HiddenRenderMask = 0xC000u,
  kDkc3HiddenRenderVariantBit = 0x8000u,
  kDkc3HiddenKongSlotFirst = 0x0878u,
  kDkc3HiddenKongSlotSecond = 0x08E6u,
  kDkc3HiddenMountedBuddyAddr = 0x007Cu,
  kDkc3HiddenMainLoopVectorAddr = 0x004Eu,
  kDkc3HiddenSpriteOamOffset = 0x001Eu,
  kDkc3HiddenOamTilePaletteMask = 0x0FFFu,
  kDkc3HiddenSpriteTypeOffset = 0x0000u,
  kDkc3HiddenTypeKRool = 0x0118u,
};

#define kDkc3HiddenMainLoopLevel 0xB38076u

/* Returns true only during level gameplay (main_loop_vector == $B3:8076)
 * when the slot's OAM attribute holds a tile base or palette. */
bool Dkc3ShowHiddenSlotEligible(uint32_t main_loop_vector, uint16_t oam_attr);

/* Types the game hides on purpose once they leave play, so revealing them
 * draws a stale body. K. Rool (type $0118, slot +$00) is hidden with
 * +$26 = $C000 after the level $23 fight and keeps his last frame and
 * position, half inside the floor (release quick save, slot $09C2,
 * +$1E = $26C0, graphic $8019). Returns false for those types. */
bool Dkc3ShowHiddenTypeEligible(uint16_t type);

/* Returns true only when env_value is non-NULL, non-empty, and not "0". */
bool Dkc3ShowHiddenEnabledFromEnv(const char *env_value);

/* Returns the render word the renderer should see for the sprite in `slot`.
 * When enabled, a hidden word ((flags & $C000) == $C000) loses bit 15 so the
 * renderer draws the sprite through its normal camera-relative path; Kong
 * slots and the mounted animal buddy's slot stay hidden because the game
 * draws those sprites elsewhere. Every other word is returned unchanged. */
uint16_t Dkc3ShowHiddenRenderFlags(uint16_t flags, uint16_t slot,
                                   uint16_t mounted_buddy_slot, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
