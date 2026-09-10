#include "dkc3_haptics.h"

#include <stddef.h>

enum {
  kActiveKongSprite = 0x04F9,
  kDixieSpriteSlot = 0x0878,
  kKiddySpriteSlot = 0x08E6,
  kFirstEnemySpriteSlot = 0x0954,
  kSpriteStride = 0x006E,
  kSpriteType = 0x00,
  kSpritePlacementNumber = 0x08,
  kSpritePlacementParameter = 0x0A,
  kSpriteRenderOrder = 0x0E,
  kSpriteOamProperty = 0x1E,
  kSpriteVerticalVelocity = 0x2E,
  kSpriteInteractionFlags = 0x3A,
  kDixieType = 0x022C,
  kKiddyType = 0x0230,
  kDefeatedPlacementParameter = 0x0005,
  kDefeatedRenderOrder = 0x00F4,
  kDefeatedOamMask = 0x3000,
};

static uint16_t Read16(const uint8_t *wram, size_t address) {
  return (uint16_t)(wram[address] | ((uint16_t)wram[address + 1] << 8));
}

static bool PlayerSlotMatchesType(uint16_t slot, uint16_t type) {
  return (slot == kDixieSpriteSlot && type == kDixieType) ||
         (slot == kKiddySpriteSlot && type == kKiddyType);
}

/* defeat_sprite_using_animation at $B6:8085 gives every standard defeated
 * enemy this exact inactive-collision/fall-away signature. The enemy's type
 * remains present while its defeat animation runs. */
static bool HasDefeatedEnemySignature(const uint8_t *wram, size_t slot) {
  return Read16(wram, slot + kSpriteType) != 0 &&
         Read16(wram, slot + kSpritePlacementNumber) == 0 &&
         Read16(wram, slot + kSpritePlacementParameter) ==
             kDefeatedPlacementParameter &&
         Read16(wram, slot + kSpriteRenderOrder) == kDefeatedRenderOrder &&
         (Read16(wram, slot + kSpriteOamProperty) & kDefeatedOamMask) ==
             kDefeatedOamMask &&
         Read16(wram, slot + kSpriteInteractionFlags) == 0;
}

void Dkc3EnemyDefeatProbeCapture(Dkc3EnemyDefeatProbe *probe,
                                 const uint8_t *wram) {
  if (!probe)
    return;
  *probe = (Dkc3EnemyDefeatProbe){0};
  if (!wram) return;

  const uint16_t slot = Read16(wram, kActiveKongSprite);
  const uint16_t type =
      slot == kDixieSpriteSlot || slot == kKiddySpriteSlot
          ? Read16(wram, slot + kSpriteType)
          : 0;
  if (!PlayerSlotMatchesType(slot, type))
    return;

  probe->player_slot = slot;
  probe->actor_type = type;
  probe->vertical_velocity =
      (int16_t)Read16(wram, slot + kSpriteVerticalVelocity);
  for (size_t index = 0; index < DKC3_HAPTICS_ENEMY_SLOT_COUNT; index++) {
    const size_t enemy_slot =
        kFirstEnemySpriteSlot + index * kSpriteStride;
    probe->enemy_types[index] = Read16(wram, enemy_slot + kSpriteType);
    if (HasDefeatedEnemySignature(wram, enemy_slot))
      probe->defeated_enemy_mask |= UINT32_C(1) << index;
  }
  probe->valid = true;
}

bool Dkc3EnemyDefeatProbeAccepted(const Dkc3EnemyDefeatProbe *probe,
                                  const uint8_t *wram) {
  if (!probe || !probe->valid || !wram || probe->vertical_velocity <= 0)
    return false;

  const uint16_t slot = Read16(wram, kActiveKongSprite);
  if (slot != probe->player_slot ||
      Read16(wram, slot + kSpriteType) != probe->actor_type ||
      (int16_t)Read16(wram, slot + kSpriteVerticalVelocity) >= 0)
    return false;

  for (size_t index = 0; index < DKC3_HAPTICS_ENEMY_SLOT_COUNT; index++) {
    const uint32_t bit = UINT32_C(1) << index;
    if (probe->defeated_enemy_mask & bit)
      continue;
    const size_t enemy_slot =
        kFirstEnemySpriteSlot + index * kSpriteStride;
    if (probe->enemy_types[index] != 0 &&
        Read16(wram, enemy_slot + kSpriteType) ==
            probe->enemy_types[index] &&
        HasDefeatedEnemySignature(wram, enemy_slot))
      return true;
  }
  return false;
}
