#include "dkc3_show_hidden.h"

#include <stddef.h>

bool Dkc3ShowHiddenEnabledFromEnv(const char *env_value) {
  return env_value != NULL && env_value[0] != '\0' &&
         !(env_value[0] == '0' && env_value[1] == '\0');
}

bool Dkc3ShowHiddenSlotEligible(uint32_t main_loop_vector, uint16_t oam_attr) {
  return main_loop_vector == kDkc3HiddenMainLoopLevel &&
         (oam_attr & kDkc3HiddenOamTilePaletteMask) != 0u;
}

bool Dkc3ShowHiddenTypeEligible(uint16_t type) {
  return type != kDkc3HiddenTypeKRool;
}

uint16_t Dkc3ShowHiddenRenderFlags(uint16_t flags, uint16_t slot,
                                   uint16_t mounted_buddy_slot, bool enabled) {
  if (!enabled || (flags & kDkc3HiddenRenderMask) != kDkc3HiddenRenderMask)
    return flags;
  if (slot == kDkc3HiddenKongSlotFirst || slot == kDkc3HiddenKongSlotSecond)
    return flags;
  if (mounted_buddy_slot != 0u && slot == mounted_buddy_slot)
    return flags;
  return (uint16_t)(flags & ~kDkc3HiddenRenderVariantBit);
}
