#include "dkc3_haptics.h"

#include <stdio.h>
#include <stdint.h>

static int s_failures;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition);   \
      s_failures++;                                                            \
    }                                                                          \
  } while (0)

enum {
  kWramSize = 0x20000,
  kActiveKongSprite = 0x04F9,
  kDixieSpriteSlot = 0x0878,
  kKiddySpriteSlot = 0x08E6,
  kFirstEnemySpriteSlot = 0x0954,
  kLastEnemySpriteSlot = 0x1412,
  kSpriteType = 0x00,
  kSpritePlacementNumber = 0x08,
  kSpritePlacementParameter = 0x0A,
  kSpriteRenderOrder = 0x0E,
  kSpriteOamProperty = 0x1E,
  kSpriteVerticalVelocity = 0x2E,
  kSpriteInteractionFlags = 0x3A,
};

static void Write16(uint8_t *wram, unsigned address, uint16_t value) {
  wram[address] = (uint8_t)value;
  wram[address + 1] = (uint8_t)(value >> 8);
}

static void SetPlayer(uint8_t *wram, uint16_t slot, uint16_t type,
                      uint16_t vertical_velocity) {
  Write16(wram, kActiveKongSprite, slot);
  Write16(wram, slot + kSpriteType, type);
  Write16(wram, slot + kSpriteVerticalVelocity, vertical_velocity);
}

static void SetLiveEnemy(uint8_t *wram, uint16_t slot, uint16_t type) {
  Write16(wram, slot + kSpriteType, type);
  Write16(wram, slot + kSpritePlacementNumber, 7);
  Write16(wram, slot + kSpritePlacementParameter, 0);
  Write16(wram, slot + kSpriteRenderOrder, 3);
  Write16(wram, slot + kSpriteOamProperty, 0);
  Write16(wram, slot + kSpriteInteractionFlags, 0x08A8);
}

static void DefeatEnemy(uint8_t *wram, uint16_t slot) {
  Write16(wram, slot + kSpritePlacementNumber, 0);
  Write16(wram, slot + kSpritePlacementParameter, 5);
  Write16(wram, slot + kSpriteRenderOrder, 0x00F4);
  Write16(wram, slot + kSpriteOamProperty, 0x3000);
  Write16(wram, slot + kSpriteInteractionFlags, 0);
}

static void TestJumpKillIsAccepted(void) {
  uint8_t wram[kWramSize] = {0};
  SetPlayer(wram, kDixieSpriteSlot, 0x022C, 0x0300);
  SetLiveEnemy(wram, kFirstEnemySpriteSlot, 0x02F8);
  Dkc3EnemyDefeatProbe probe;
  Dkc3EnemyDefeatProbeCapture(&probe, wram);
  CHECK(probe.valid);

  Write16(wram, kDixieSpriteSlot + kSpriteVerticalVelocity, 0xF820);
  DefeatEnemy(wram, kFirstEnemySpriteSlot);
  CHECK(Dkc3EnemyDefeatProbeAccepted(&probe, wram));
}

static void TestFinalEnemySlotIsCovered(void) {
  uint8_t wram[kWramSize] = {0};
  SetPlayer(wram, kKiddySpriteSlot, 0x0230, 0x0180);
  SetLiveEnemy(wram, kLastEnemySpriteSlot, 0x02FC);
  Dkc3EnemyDefeatProbe probe;
  Dkc3EnemyDefeatProbeCapture(&probe, wram);

  Write16(wram, kKiddySpriteSlot + kSpriteVerticalVelocity, 0xF800);
  DefeatEnemy(wram, kLastEnemySpriteSlot);
  CHECK(Dkc3EnemyDefeatProbeAccepted(&probe, wram));
}

static void TestOrdinaryJumpIsRejected(void) {
  uint8_t wram[kWramSize] = {0};
  SetPlayer(wram, kDixieSpriteSlot, 0x022C, 0x0300);
  SetLiveEnemy(wram, kFirstEnemySpriteSlot, 0x02F8);
  Dkc3EnemyDefeatProbe probe;
  Dkc3EnemyDefeatProbeCapture(&probe, wram);

  Write16(wram, kDixieSpriteSlot + kSpriteVerticalVelocity, 0xF820);
  CHECK(!Dkc3EnemyDefeatProbeAccepted(&probe, wram));
}

static void TestNonJumpEnemyDefeatIsRejected(void) {
  uint8_t wram[kWramSize] = {0};
  SetPlayer(wram, kDixieSpriteSlot, 0x022C, 0xF800);
  SetLiveEnemy(wram, kFirstEnemySpriteSlot, 0x02F8);
  Dkc3EnemyDefeatProbe probe;
  Dkc3EnemyDefeatProbeCapture(&probe, wram);
  DefeatEnemy(wram, kFirstEnemySpriteSlot);
  CHECK(!Dkc3EnemyDefeatProbeAccepted(&probe, wram));

  SetPlayer(wram, kDixieSpriteSlot, 0x022C, 0x0300);
  SetLiveEnemy(wram, kFirstEnemySpriteSlot, 0x02F8);
  Dkc3EnemyDefeatProbeCapture(&probe, wram);
  DefeatEnemy(wram, kFirstEnemySpriteSlot);
  CHECK(!Dkc3EnemyDefeatProbeAccepted(&probe, wram));
}

static void TestAlreadyDefeatedAndChangedSlotsAreRejected(void) {
  uint8_t wram[kWramSize] = {0};
  SetPlayer(wram, kDixieSpriteSlot, 0x022C, 0x0300);
  SetLiveEnemy(wram, kFirstEnemySpriteSlot, 0x02F8);
  DefeatEnemy(wram, kFirstEnemySpriteSlot);
  Dkc3EnemyDefeatProbe probe;
  Dkc3EnemyDefeatProbeCapture(&probe, wram);
  Write16(wram, kDixieSpriteSlot + kSpriteVerticalVelocity, 0xF800);
  CHECK(!Dkc3EnemyDefeatProbeAccepted(&probe, wram));

  SetPlayer(wram, kDixieSpriteSlot, 0x022C, 0x0300);
  SetLiveEnemy(wram, kFirstEnemySpriteSlot, 0x02F8);
  Dkc3EnemyDefeatProbeCapture(&probe, wram);
  Write16(wram, kFirstEnemySpriteSlot + kSpriteType, 0x02FC);
  DefeatEnemy(wram, kFirstEnemySpriteSlot);
  Write16(wram, kDixieSpriteSlot + kSpriteVerticalVelocity, 0xF800);
  CHECK(!Dkc3EnemyDefeatProbeAccepted(&probe, wram));
}

static void TestInvalidPlayersAreRejected(void) {
  uint8_t wram[kWramSize] = {0};
  Dkc3EnemyDefeatProbe probe;

  SetPlayer(wram, kDixieSpriteSlot, 0x0230, 0x0300);
  Dkc3EnemyDefeatProbeCapture(&probe, wram);
  CHECK(!probe.valid);

  SetPlayer(wram, kDixieSpriteSlot, 0x022C, 0x0300);
  SetLiveEnemy(wram, kFirstEnemySpriteSlot, 0x02F8);
  Dkc3EnemyDefeatProbeCapture(&probe, wram);
  SetPlayer(wram, kKiddySpriteSlot, 0x0230, 0xF800);
  DefeatEnemy(wram, kFirstEnemySpriteSlot);
  CHECK(!Dkc3EnemyDefeatProbeAccepted(&probe, wram));

  Dkc3EnemyDefeatProbeCapture(NULL, wram);
  CHECK(!Dkc3EnemyDefeatProbeAccepted(NULL, wram));
  Dkc3EnemyDefeatProbeCapture(&probe, NULL);
  CHECK(!probe.valid);
}

int main(void) {
  TestJumpKillIsAccepted();
  TestFinalEnemySlotIsCovered();
  TestOrdinaryJumpIsRejected();
  TestNonJumpEnemyDefeatIsRejected();
  TestAlreadyDefeatedAndChangedSlotsAreRejected();
  TestInvalidPlayersAreRejected();
  return s_failures ? 1 : 0;
}
