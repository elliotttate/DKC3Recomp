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
enum {
  kDkc3HiddenRenderMask = 0xC000u,
  kDkc3HiddenRenderVariantBit = 0x8000u,
  kDkc3HiddenKongSlotFirst = 0x0878u,
  kDkc3HiddenKongSlotSecond = 0x08E6u,
  kDkc3HiddenMountedBuddyAddr = 0x007Cu,
};

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
