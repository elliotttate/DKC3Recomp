#include "dkc3_animal_crate_cheat.h"

#include <string.h>

static uint16_t ReadLe16(const uint8_t *wram, size_t address) {
  return (uint16_t)wram[address] | ((uint16_t)wram[address + 1u] << 8);
}

static uint32_t ReadLe24(const uint8_t *wram, size_t address) {
  return (uint32_t)ReadLe16(wram, address) |
         ((uint32_t)wram[address + 2u] << 16);
}

static void WriteLe16(uint8_t *wram, size_t address, uint16_t value) {
  wram[address] = (uint8_t)(value & 0xFFu);
  wram[address + 1u] = (uint8_t)(value >> 8);
}

static bool IsValidSpriteSlot(uint16_t slot, uint16_t first_slot) {
  if (slot < first_slot || slot >= kDkc3PauseCrateSpriteTableEnd)
    return false;
  return ((slot - kDkc3PauseCrateSpriteTableStart) %
          kDkc3PauseCrateSpriteStride) == 0u;
}

static uint16_t FindFreeSpriteSlot(const uint8_t *wram) {
  for (uint16_t slot = kDkc3PauseCrateFirstFreeSlot;
       slot < kDkc3PauseCrateSpriteTableEnd;
       slot = (uint16_t)(slot + kDkc3PauseCrateSpriteStride)) {
    if (ReadLe16(wram, slot + kDkc3PauseCrateSpriteTypeOffset) == 0u)
      return slot;
  }
  return 0u;
}

bool Dkc3PauseCrateSpawnEnabledFromEnv(const char *env_value) {
  if (!env_value || !env_value[0])
    return false;
  return strcmp(env_value, "0") != 0;
}

const char *Dkc3PauseCrateName(Dkc3PauseCrateKind kind) {
  switch (kind) {
    case kDkc3PauseCrateEllie:
      return "Ellie";
    case kDkc3PauseCrateEnguarde:
      return "Enguarde";
    case kDkc3PauseCrateSquitter:
      return "Squitter";
    case kDkc3PauseCrateSquawks:
      return "Squawks";
    case kDkc3PauseCrateParry:
      return "Parry";
    default:
      return NULL;
  }
}

uint16_t Dkc3PauseCrateScriptPtr(Dkc3PauseCrateKind kind) {
  switch (kind) {
    case kDkc3PauseCrateEllie:
      return kDkc3PauseCrateScriptEllie;
    case kDkc3PauseCrateEnguarde:
      return kDkc3PauseCrateScriptEnguarde;
    case kDkc3PauseCrateSquitter:
      return kDkc3PauseCrateScriptSquitter;
    case kDkc3PauseCrateSquawks:
      return kDkc3PauseCrateScriptSquawks;
    case kDkc3PauseCrateParry:
      return kDkc3PauseCrateScriptParry;
    default:
      return 0u;
  }
}

bool Dkc3IsPauseCrateMenuActive(const uint8_t *wram, size_t wram_size,
                                bool enabled) {
  if (!enabled || !wram || wram_size < kDkc3PauseCrateMinWramSize)
    return false;
  if (ReadLe24(wram, kDkc3PauseCrateMainLoopVectorAddr) !=
      kDkc3PauseCrateMainLoopLevel)
    return false;
  const uint16_t flags = ReadLe16(wram, kDkc3PauseCrateLevelFlagsAddr);
  if ((flags & kDkc3PauseCrateNoControlFlagBit) != 0u)
    return false;
  return (flags & kDkc3PauseCratePausedFlagBit) != 0u;
}

bool Dkc3UpdatePauseCrateSelection(const uint8_t *wram, size_t wram_size,
                                   bool enabled, Dkc3PauseCrateKind *selected) {
  if (!selected || !Dkc3IsPauseCrateMenuActive(wram, wram_size, enabled))
    return false;

  int index = (int)(*selected);
  if (index < 0 || index >= (int)kDkc3PauseCrateCount)
    index = 0;

  const uint16_t pressed = ReadLe16(wram, kDkc3PauseCrateActivePressedAddr);
  if ((pressed & kDkc3PauseCrateInputStartBit) != 0u)
    return false;

  const bool left = (pressed & kDkc3PauseCrateInputLeftBit) != 0u;
  const bool right = (pressed & kDkc3PauseCrateInputRightBit) != 0u;
  if (left == right)
    return false;

  if (right) {
    index = (index + 1) % (int)kDkc3PauseCrateCount;
  } else {
    index = (index + (int)kDkc3PauseCrateCount - 1) % (int)kDkc3PauseCrateCount;
  }
  *selected = (Dkc3PauseCrateKind)index;
  return true;
}

bool Dkc3DetectPauseCrateSpawn(const uint8_t *wram, size_t wram_size,
                               bool enabled, Dkc3PauseCrateKind selected,
                               Dkc3PauseCrateRequest *out) {
  if (out)
    memset(out, 0, sizeof *out);
  if (!Dkc3IsPauseCrateMenuActive(wram, wram_size, enabled))
    return false;

  const uint16_t script_ptr = Dkc3PauseCrateScriptPtr(selected);
  if (script_ptr == 0u)
    return false;

  const uint16_t pressed = ReadLe16(wram, kDkc3PauseCrateActivePressedAddr);
  if ((pressed & kDkc3PauseCrateInputStartBit) != 0u)
    return false;
  if ((pressed & kDkc3PauseCrateInputBBit) == 0u)
    return false;

  const uint16_t player_slot = ReadLe16(wram, kDkc3PauseCrateActiveKongAddr);
  if (!IsValidSpriteSlot(player_slot, kDkc3PauseCrateSpriteTableStart))
    return false;
  if (ReadLe16(wram, player_slot + kDkc3PauseCrateSpriteTypeOffset) == 0u)
    return false;

  const uint16_t free_slot = FindFreeSpriteSlot(wram);
  if (free_slot == 0u)
    return false;

  if (out) {
    out->kind = selected;
    out->slot = free_slot;
    out->script_ptr = script_ptr;
    out->spawn_x = ReadLe16(wram, player_slot + kDkc3PauseCrateSpriteXOffset);
    out->spawn_y = ReadLe16(wram, player_slot + kDkc3PauseCrateSpriteYOffset);
  }
  return true;
}

bool Dkc3PreparePauseCrateSlot(uint8_t *wram, size_t wram_size,
                               const Dkc3PauseCrateRequest *request) {
  if (!wram || !request || wram_size < kDkc3PauseCrateMinWramSize)
    return false;
  if (Dkc3PauseCrateScriptPtr(request->kind) == 0u)
    return false;
  if (!IsValidSpriteSlot(request->slot, kDkc3PauseCrateFirstFreeSlot))
    return false;

  memset(wram + request->slot, 0, kDkc3PauseCrateSpriteStride);
  WriteLe16(wram, request->slot + kDkc3PauseCrateSpriteParamOffset,
            kDkc3PauseCrateDefaultParam);
  WriteLe16(wram, request->slot + kDkc3PauseCrateSpriteXOffset,
            request->spawn_x);
  WriteLe16(wram, request->slot + kDkc3PauseCrateSpriteYOffset,
            request->spawn_y);
  WriteLe16(wram, kDkc3PauseCrateDpScratchSlotAddr, request->slot);
  return true;
}

uint16_t Dkc3AnimalBuddyPaletteIdForType(uint16_t sprite_type,
                                         uint16_t level_flags_0777) {
  switch (sprite_type) {
    case kDkc3PauseCrateTypeEllie:
      return kDkc3PauseCratePaletteEllie;
    case kDkc3PauseCrateTypeEnguarde:
      return kDkc3PauseCratePaletteEnguarde;
    case kDkc3PauseCrateTypeSquitter:
      return kDkc3PauseCratePaletteSquitter;
    case kDkc3PauseCrateTypeSquawks:
      return (level_flags_0777 & kDkc3PauseCrateQuawksFlagBit) != 0u
                 ? kDkc3PauseCratePaletteQuawks
                 : kDkc3PauseCratePaletteSquawks;
    case kDkc3PauseCrateTypeParry:
      return kDkc3PauseCratePaletteParry;
    default:
      return 0u;
  }
}

static bool HardwarePaletteSlotAvailable(const uint8_t *wram,
                                         uint16_t palette_id) {
  for (unsigned slot = 1u; slot < 8u; slot++) {
    const uint16_t existing =
        ReadLe16(wram, kDkc3PauseCratePaletteSlotIdsAddr + slot * 2u);
    if (existing == 0u || existing == palette_id)
      return true;
  }
  return false;
}

bool Dkc3DetectAnimalBuddyPaletteRepair(const uint8_t *wram, size_t wram_size,
                                        bool enabled,
                                        Dkc3AnimalBuddyPaletteRepair *out) {
  if (out)
    memset(out, 0, sizeof *out);
  if (!enabled || !wram || wram_size < kDkc3PauseCrateMinWramSize)
    return false;
  if (ReadLe24(wram, kDkc3PauseCrateMainLoopVectorAddr) !=
      kDkc3PauseCrateMainLoopLevel)
    return false;

  const uint16_t level_flags =
      ReadLe16(wram, kDkc3PauseCrateLevelConfigFlagsAddr);

  /* Case 1: active Kong is riding an animal buddy (DP $007C) and the swapped
   * OAM palette on the active Kong slot is stuck on palette 0. */
  const uint16_t mounted_buddy =
      ReadLe16(wram, kDkc3PauseCrateDpMountedBuddyAddr);
  const uint16_t active_kong = ReadLe16(wram, kDkc3PauseCrateActiveKongAddr);
  if (IsValidSpriteSlot(mounted_buddy, kDkc3PauseCrateFirstFreeSlot) &&
      IsValidSpriteSlot(active_kong, kDkc3PauseCrateSpriteTableStart)) {
    const uint16_t buddy_type =
        ReadLe16(wram, mounted_buddy + kDkc3PauseCrateSpriteTypeOffset);
    const uint16_t palette_id =
        Dkc3AnimalBuddyPaletteIdForType(buddy_type, level_flags);
    if (palette_id != 0u) {
      const uint16_t kong_oam =
          ReadLe16(wram, active_kong + kDkc3PauseCrateSpriteOamOffset);
      if (kong_oam != 0u && ((kong_oam >> 9) & 7u) == 0u &&
          HardwarePaletteSlotAvailable(wram, palette_id)) {
        if (out) {
          out->target_slot = active_kong;
          out->palette_id = palette_id;
        }
        return true;
      }
    }
  }

  /* Case 2: unmounted animal buddy stuck on palette 0. */
  for (uint16_t slot = kDkc3PauseCrateFirstFreeSlot;
       slot < kDkc3PauseCrateSpriteTableEnd;
       slot = (uint16_t)(slot + kDkc3PauseCrateSpriteStride)) {
    const uint16_t type =
        ReadLe16(wram, slot + kDkc3PauseCrateSpriteTypeOffset);
    const uint16_t palette_id =
        Dkc3AnimalBuddyPaletteIdForType(type, level_flags);
    if (palette_id == 0u)
      continue;
    const uint16_t oam = ReadLe16(wram, slot + kDkc3PauseCrateSpriteOamOffset);
    if (oam != 0u && ((oam >> 9) & 7u) == 0u &&
        HardwarePaletteSlotAvailable(wram, palette_id)) {
      if (out) {
        out->target_slot = slot;
        out->palette_id = palette_id;
      }
      return true;
    }
  }

  return false;
}
