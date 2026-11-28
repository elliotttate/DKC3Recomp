#include "dkc3_animal_crate_cheat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Fail(const char *message, int line) {
  fprintf(stderr, "test_dkc3_animal_crate_cheat.c:%d: %s\n", line, message);
  exit(1);
}

#define CHECK(cond) \
  do {              \
    if (!(cond))    \
      Fail(#cond, __LINE__); \
  } while (0)

static void Write16(uint8_t *wram, size_t addr, uint16_t value) {
  wram[addr] = (uint8_t)(value & 0xFFu);
  wram[addr + 1u] = (uint8_t)(value >> 8);
}

static uint16_t Read16(const uint8_t *wram, size_t addr) {
  return (uint16_t)wram[addr] | ((uint16_t)wram[addr + 1u] << 8);
}

static void SetMainLoop(uint8_t *wram, uint32_t pc24) {
  wram[kDkc3PauseCrateMainLoopVectorAddr] = (uint8_t)(pc24 & 0xFFu);
  wram[kDkc3PauseCrateMainLoopVectorAddr + 1u] = (uint8_t)((pc24 >> 8) & 0xFFu);
  wram[kDkc3PauseCrateMainLoopVectorAddr + 2u] = (uint8_t)((pc24 >> 16) & 0xFFu);
}

static void SetupPausedLevel(uint8_t *wram, uint16_t player_x, uint16_t player_y) {
  memset(wram, 0, kDkc3PauseCrateMinWramSize);
  SetMainLoop(wram, kDkc3PauseCrateMainLoopLevel);
  Write16(wram, kDkc3PauseCrateLevelFlagsAddr, kDkc3PauseCratePausedFlagBit);
  Write16(wram, kDkc3PauseCrateActiveKongAddr, kDkc3PauseCrateSpriteTableStart);
  Write16(wram, kDkc3PauseCrateSpriteTableStart + kDkc3PauseCrateSpriteTypeOffset,
          0x022Cu); /* Dixie */
  Write16(wram, kDkc3PauseCrateSpriteTableStart + kDkc3PauseCrateSpriteXOffset,
          player_x);
  Write16(wram, kDkc3PauseCrateSpriteTableStart + kDkc3PauseCrateSpriteYOffset,
          player_y);
}

int main(void) {
  /* Default off in environment parsing. */
  CHECK(!Dkc3PauseCrateSpawnEnabledFromEnv(NULL));
  CHECK(!Dkc3PauseCrateSpawnEnabledFromEnv(""));
  CHECK(!Dkc3PauseCrateSpawnEnabledFromEnv("0"));
  CHECK(Dkc3PauseCrateSpawnEnabledFromEnv("1"));

  /* All 5 animal buddy crate names and bank $FF script pointers. */
  CHECK(strcmp(Dkc3PauseCrateName(kDkc3PauseCrateEllie), "Ellie") == 0);
  CHECK(strcmp(Dkc3PauseCrateName(kDkc3PauseCrateEnguarde), "Enguarde") == 0);
  CHECK(strcmp(Dkc3PauseCrateName(kDkc3PauseCrateSquitter), "Squitter") == 0);
  CHECK(strcmp(Dkc3PauseCrateName(kDkc3PauseCrateSquawks), "Squawks") == 0);
  CHECK(strcmp(Dkc3PauseCrateName(kDkc3PauseCrateParry), "Parry") == 0);
  CHECK(Dkc3PauseCrateName(kDkc3PauseCrateCount) == NULL);

  CHECK(Dkc3PauseCrateScriptPtr(kDkc3PauseCrateEllie) == kDkc3PauseCrateScriptEllie);
  CHECK(Dkc3PauseCrateScriptPtr(kDkc3PauseCrateEnguarde) == kDkc3PauseCrateScriptEnguarde);
  CHECK(Dkc3PauseCrateScriptPtr(kDkc3PauseCrateSquitter) == kDkc3PauseCrateScriptSquitter);
  CHECK(Dkc3PauseCrateScriptPtr(kDkc3PauseCrateSquawks) == kDkc3PauseCrateScriptSquawks);
  CHECK(Dkc3PauseCrateScriptPtr(kDkc3PauseCrateParry) == kDkc3PauseCrateScriptParry);
  CHECK(Dkc3PauseCrateScriptPtr(kDkc3PauseCrateCount) == 0u);

  uint8_t wram[kDkc3PauseCrateMinWramSize];
  Dkc3PauseCrateRequest req;
  Dkc3PauseCrateKind selected = kDkc3PauseCrateEllie;

  /* Null / undersized buffers fail safely. */
  CHECK(!Dkc3IsPauseCrateMenuActive(NULL, sizeof wram, true));
  CHECK(!Dkc3IsPauseCrateMenuActive(wram, sizeof wram - 1u, true));
  CHECK(!Dkc3DetectPauseCrateSpawn(NULL, sizeof wram, true, selected, &req));
  CHECK(!Dkc3DetectPauseCrateSpawn(wram, sizeof wram - 1u, true, selected, &req));

  /* Disabled fails closed even when paused + B pressed. */
  SetupPausedLevel(wram, 0x0180u, 0x00C0u);
  Write16(wram, kDkc3PauseCrateActivePressedAddr, kDkc3PauseCrateInputBBit);
  CHECK(!Dkc3IsPauseCrateMenuActive(wram, sizeof wram, false));
  CHECK(!Dkc3DetectPauseCrateSpawn(wram, sizeof wram, false, selected, &req));

  /* Outside a level fails closed. */
  SetMainLoop(wram, 0xB28E05u);
  CHECK(!Dkc3IsPauseCrateMenuActive(wram, sizeof wram, true));
  CHECK(!Dkc3DetectPauseCrateSpawn(wram, sizeof wram, true, selected, &req));

  /* Unpaused in a level fails closed. */
  SetupPausedLevel(wram, 0x0180u, 0x00C0u);
  Write16(wram, kDkc3PauseCrateLevelFlagsAddr, 0x0000u);
  Write16(wram, kDkc3PauseCrateActivePressedAddr, kDkc3PauseCrateInputBBit);
  CHECK(!Dkc3IsPauseCrateMenuActive(wram, sizeof wram, true));
  CHECK(!Dkc3DetectPauseCrateSpawn(wram, sizeof wram, true, selected, &req));

  /* The credits run on the level main loop with $05AF = $80C0: the frozen
   * bit is set while no player is in control (bit 15). Fails closed, and
   * Left/Right does not move the selection. */
  SetupPausedLevel(wram, 0x0180u, 0x00C0u);
  Write16(wram, kDkc3PauseCrateLevelFlagsAddr, 0x80C0u);
  Write16(wram, kDkc3PauseCrateActivePressedAddr, kDkc3PauseCrateInputBBit);
  CHECK(!Dkc3IsPauseCrateMenuActive(wram, sizeof wram, true));
  CHECK(!Dkc3DetectPauseCrateSpawn(wram, sizeof wram, true, selected, &req));
  {
    Dkc3PauseCrateKind credits_sel = kDkc3PauseCrateEllie;
    Write16(wram, kDkc3PauseCrateActivePressedAddr,
            kDkc3PauseCrateInputRightBit);
    CHECK(!Dkc3UpdatePauseCrateSelection(wram, sizeof wram, true,
                                         &credits_sel));
    CHECK(credits_sel == kDkc3PauseCrateEllie);
  }

  /* A player pause in a level ($05AF = $40C0) is still accepted. */
  SetupPausedLevel(wram, 0x0180u, 0x00C0u);
  Write16(wram, kDkc3PauseCrateLevelFlagsAddr, 0x40C0u);
  CHECK(Dkc3IsPauseCrateMenuActive(wram, sizeof wram, true));

  /* Left/Right selection cycles and wraps across all 5 buddies while paused. */
  SetupPausedLevel(wram, 0x0180u, 0x00C0u);
  CHECK(Dkc3IsPauseCrateMenuActive(wram, sizeof wram, true));

  /* Left from Ellie (0) wraps to Parry (4). */
  Write16(wram, kDkc3PauseCrateActivePressedAddr, kDkc3PauseCrateInputLeftBit);
  CHECK(Dkc3UpdatePauseCrateSelection(wram, sizeof wram, true, &selected));
  CHECK(selected == kDkc3PauseCrateParry);

  /* Right from Parry (4) wraps back to Ellie (0). */
  Write16(wram, kDkc3PauseCrateActivePressedAddr, kDkc3PauseCrateInputRightBit);
  CHECK(Dkc3UpdatePauseCrateSelection(wram, sizeof wram, true, &selected));
  CHECK(selected == kDkc3PauseCrateEllie);

  /* Right steps through Enguarde -> Squitter -> Squawks. */
  CHECK(Dkc3UpdatePauseCrateSelection(wram, sizeof wram, true, &selected));
  CHECK(selected == kDkc3PauseCrateEnguarde);
  CHECK(Dkc3UpdatePauseCrateSelection(wram, sizeof wram, true, &selected));
  CHECK(selected == kDkc3PauseCrateSquitter);
  CHECK(Dkc3UpdatePauseCrateSelection(wram, sizeof wram, true, &selected));
  CHECK(selected == kDkc3PauseCrateSquawks);

  /* Simultaneous Left+Right or Start+Right does nothing. */
  Write16(wram, kDkc3PauseCrateActivePressedAddr,
          kDkc3PauseCrateInputLeftBit | kDkc3PauseCrateInputRightBit);
  CHECK(!Dkc3UpdatePauseCrateSelection(wram, sizeof wram, true, &selected));
  CHECK(selected == kDkc3PauseCrateSquawks);
  Write16(wram, kDkc3PauseCrateActivePressedAddr,
          kDkc3PauseCrateInputStartBit | kDkc3PauseCrateInputRightBit);
  CHECK(!Dkc3UpdatePauseCrateSelection(wram, sizeof wram, true, &selected));
  CHECK(selected == kDkc3PauseCrateSquawks);

  /* Start + B on the same frame fails closed. */
  Write16(wram, kDkc3PauseCrateActivePressedAddr,
          kDkc3PauseCrateInputStartBit | kDkc3PauseCrateInputBBit);
  CHECK(!Dkc3DetectPauseCrateSpawn(wram, sizeof wram, true, selected, &req));

  /* Unrelated buttons (Up/Down/Left/Right) do not spawn. */
  Write16(wram, kDkc3PauseCrateActivePressedAddr, 0x0F00u);
  CHECK(!Dkc3DetectPauseCrateSpawn(wram, sizeof wram, true, selected, &req));

  /* Paused + B spawns the selected crate (Squawks, $FF:65A7) in first free slot. */
  Write16(wram, kDkc3PauseCrateActivePressedAddr, kDkc3PauseCrateInputBBit);
  CHECK(Dkc3DetectPauseCrateSpawn(wram, sizeof wram, true, selected, &req));
  CHECK(req.kind == kDkc3PauseCrateSquawks);
  CHECK(req.slot == kDkc3PauseCrateFirstFreeSlot);
  CHECK(req.script_ptr == kDkc3PauseCrateScriptSquawks);
  CHECK(req.spawn_x == 0x0180u);
  CHECK(req.spawn_y == 0x00C0u);

  /* Prepare slot clears stale data and sets param/position/DP $76. */
  memset(wram + req.slot, 0xAA, kDkc3PauseCrateSpriteStride);
  CHECK(Dkc3PreparePauseCrateSlot(wram, sizeof wram, &req));
  CHECK(Read16(wram, req.slot + kDkc3PauseCrateSpriteTypeOffset) == 0u);
  CHECK(Read16(wram, req.slot + kDkc3PauseCrateSpriteParamOffset) ==
        kDkc3PauseCrateDefaultParam);
  CHECK(Read16(wram, req.slot + kDkc3PauseCrateSpriteXOffset) == 0x0180u);
  CHECK(Read16(wram, req.slot + kDkc3PauseCrateSpriteYOffset) == 0x00C0u);
  CHECK(Read16(wram, kDkc3PauseCrateDpScratchSlotAddr) == req.slot);

  /* Occupy first free slot and spawn Parry ($FF:65B1) in next slot. */
  Write16(wram, kDkc3PauseCrateFirstFreeSlot + kDkc3PauseCrateSpriteTypeOffset,
          0x0250u);
  CHECK(Dkc3DetectPauseCrateSpawn(wram, sizeof wram, true,
                                  kDkc3PauseCrateParry, &req));
  CHECK(req.kind == kDkc3PauseCrateParry);
  CHECK(req.slot == kDkc3PauseCrateFirstFreeSlot + kDkc3PauseCrateSpriteStride);
  CHECK(req.script_ptr == kDkc3PauseCrateScriptParry);
  CHECK(req.spawn_x == 0x0180u);
  CHECK(req.spawn_y == 0x00C0u);

  /* Full sprite table fails closed. */
  for (uint16_t slot = kDkc3PauseCrateFirstFreeSlot;
       slot < kDkc3PauseCrateSpriteTableEnd;
       slot = (uint16_t)(slot + kDkc3PauseCrateSpriteStride)) {
    Write16(wram, slot + kDkc3PauseCrateSpriteTypeOffset, 0x0100u);
  }
  CHECK(!Dkc3DetectPauseCrateSpawn(wram, sizeof wram, true,
                                   kDkc3PauseCrateEllie, &req));

  /* Invalid active Kong pointer fails closed. */
  SetupPausedLevel(wram, 0x0180u, 0x00C0u);
  Write16(wram, kDkc3PauseCrateActivePressedAddr, kDkc3PauseCrateInputBBit);
  Write16(wram, kDkc3PauseCrateActiveKongAddr, 0x0879u); /* misaligned */
  CHECK(!Dkc3DetectPauseCrateSpawn(wram, sizeof wram, true,
                                   kDkc3PauseCrateEllie, &req));
  Write16(wram, kDkc3PauseCrateActiveKongAddr, 0x0000u);
  CHECK(!Dkc3DetectPauseCrateSpawn(wram, sizeof wram, true,
                                   kDkc3PauseCrateEllie, &req));

  /* Palette ID lookup for all 5 animal buddies + Quawks. */
  CHECK(Dkc3AnimalBuddyPaletteIdForType(kDkc3PauseCrateTypeEllie, 0x0000u) ==
        kDkc3PauseCratePaletteEllie);
  CHECK(Dkc3AnimalBuddyPaletteIdForType(kDkc3PauseCrateTypeEnguarde, 0x0000u) ==
        kDkc3PauseCratePaletteEnguarde);
  CHECK(Dkc3AnimalBuddyPaletteIdForType(kDkc3PauseCrateTypeSquitter, 0x0000u) ==
        kDkc3PauseCratePaletteSquitter);
  CHECK(Dkc3AnimalBuddyPaletteIdForType(kDkc3PauseCrateTypeSquawks, 0x000Au) ==
        kDkc3PauseCratePaletteSquawks);
  CHECK(Dkc3AnimalBuddyPaletteIdForType(kDkc3PauseCrateTypeSquawks, 0x001Au) ==
        kDkc3PauseCratePaletteQuawks);
  CHECK(Dkc3AnimalBuddyPaletteIdForType(kDkc3PauseCrateTypeParry, 0x0000u) ==
        kDkc3PauseCratePaletteParry);
  CHECK(Dkc3AnimalBuddyPaletteIdForType(0x022Cu, 0x0000u) == 0u);

  /* Mounted Squawks with Kong stuck on pal 0 and a free HW slot -> repair. */
  Dkc3AnimalBuddyPaletteRepair repair;
  SetupPausedLevel(wram, 0x0180u, 0x00C0u);
  Write16(wram, kDkc3PauseCrateActiveKongAddr, 0x08E6u);
  Write16(wram, 0x08E6u + kDkc3PauseCrateSpriteTypeOffset, 0x0230u); /* Kiddy */
  Write16(wram, 0x08E6u + kDkc3PauseCrateSpriteOamOffset, 0x2040u); /* pal 0 */
  Write16(wram, kDkc3PauseCrateDpMountedBuddyAddr, 0x0DA0u);
  Write16(wram, 0x0DA0u + kDkc3PauseCrateSpriteTypeOffset,
          kDkc3PauseCrateTypeSquawks);
  Write16(wram, 0x0DA0u + kDkc3PauseCrateSpriteOamOffset, 0x2580u); /* pal 2 */
  /* Fill slots 0..5 and 7, leaving slot 6 free (0x0000). */
  Write16(wram, kDkc3PauseCratePaletteSlotIdsAddr + 0u, 0x000Fu);
  Write16(wram, kDkc3PauseCratePaletteSlotIdsAddr + 2u, 0x0001u);
  Write16(wram, kDkc3PauseCratePaletteSlotIdsAddr + 4u, 0x0002u);
  Write16(wram, kDkc3PauseCratePaletteSlotIdsAddr + 6u, 0x0027u);
  Write16(wram, kDkc3PauseCratePaletteSlotIdsAddr + 8u, 0x001Cu);
  Write16(wram, kDkc3PauseCratePaletteSlotIdsAddr + 10u, 0x00C8u);
  Write16(wram, kDkc3PauseCratePaletteSlotIdsAddr + 12u, 0x0000u);
  Write16(wram, kDkc3PauseCratePaletteSlotIdsAddr + 14u, 0x0017u);
  CHECK(!Dkc3DetectAnimalBuddyPaletteRepair(wram, sizeof wram, false, &repair));
  CHECK(Dkc3DetectAnimalBuddyPaletteRepair(wram, sizeof wram, true, &repair));
  CHECK(repair.target_slot == 0x08E6u);
  CHECK(repair.palette_id == kDkc3PauseCratePaletteSquawks);

  /* When all 8 HW slots are full with other palettes, repair fails closed. */
  Write16(wram, kDkc3PauseCratePaletteSlotIdsAddr + 12u, 0x0099u);
  CHECK(!Dkc3DetectAnimalBuddyPaletteRepair(wram, sizeof wram, true, &repair));

  /* Unmounted Enguarde on pal 0 reuses existing HW slot 7 (0x0017). */
  Write16(wram, kDkc3PauseCrateDpMountedBuddyAddr, 0x0000u);
  Write16(wram, 0x0CC4u + kDkc3PauseCrateSpriteTypeOffset,
          kDkc3PauseCrateTypeEnguarde);
  Write16(wram, 0x0CC4u + kDkc3PauseCrateSpriteOamOffset, 0x2080u); /* pal 0 */
  CHECK(Dkc3DetectAnimalBuddyPaletteRepair(wram, sizeof wram, true, &repair));
  CHECK(repair.target_slot == 0x0CC4u);
  CHECK(repair.palette_id == kDkc3PauseCratePaletteEnguarde);

  puts("dkc3 animal crate cheat tests passed");
  return 0;
}
