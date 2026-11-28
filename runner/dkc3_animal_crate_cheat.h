#ifndef DKC3_ANIMAL_CRATE_CHEAT_H
#define DKC3_ANIMAL_CRATE_CHEAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cartridge facts for DKC3 USA (En,Fr) in-game pause and animal buddy crate
 * spawning, verified from bank $80 ($80:8A6D..$80:8B0B), bank $B8
 * ($B8:EE88 animal_buddy_box_main), bank $BB ($BB:8588/$BB:8F8B/$BB:8FF1),
 * and bank $FF ($FF:656D/$FF:6586/$FF:6590/$FF:659D/$FF:65A7/$FF:65B1):
 *
 * - $004E..$0050: 24-bit main-loop vector ($B3:8076 during level gameplay).
 * - $04DA: active player newly-pressed SNES joypad buttons (edge-triggered).
 *   - Bit 15 ($8000): B
 *   - Bit 12 ($1000): Start
 *   - Bit  9 ($0200): Left
 *   - Bit  8 ($0100): Right
 * - $04F9: pointer to the active Kong/player sprite record in WRAM.
 * - $05AF: level state flags; bit 6 ($0040) is set while paused in-game.
 *   Bit 15 ($8000) is set while no player is in control: the ending credits
 *   run on the level main loop with $05AF = $80C0 (frozen bit set between
 *   credit scenes), and $BB:A19F returns early on it. A player pause never
 *   has it ($40C0 in a paused level save).
 * - Sprite table: $0878 (Dixie), $08E6 (Kiddy), $0954..$1412 (26 non-Kong
 *   slots, stride $006E, end $1480). A slot is free when type (+$00) == 0.
 * - Bank $FF spawn scripts for animal_buddy_box (type $0250):
 *   - $FF:6586: Ellie crate    (+$5C = $0234, palette $0016)
 *   - $FF:6590: Enguarde crate (+$5C = $0238, palette $0017)
 *   - $FF:659D: Squitter crate (+$5C = $0240, palette $0019)
 *   - $FF:65A7: Squawks crate  (+$5C = $023C, palette $0018 / $001E Quawks)
 *   - $FF:65B1: Parry crate    (+$5C = $0244, palette $001A)
 */
enum {
  kDkc3PauseCrateMainLoopVectorAddr = 0x004Eu,
  kDkc3PauseCrateMainLoopLevel = 0xB38076u,

  kDkc3PauseCrateDpScratchSlotAddr = 0x0076u,
  kDkc3PauseCrateDpScratchTempAddr = 0x0042u,
  kDkc3PauseCrateDpMountedBuddyAddr = 0x007Cu,

  kDkc3PauseCrateActivePressedAddr = 0x04DAu,
  kDkc3PauseCratePaletteReqAddr = 0x04EFu,
  kDkc3PauseCrateActiveKongAddr = 0x04F9u,
  kDkc3PauseCrateLevelFlagsAddr = 0x05AFu,
  kDkc3PauseCratePausedFlagBit = 0x0040u,
  kDkc3PauseCrateNoControlFlagBit = 0x8000u,
  kDkc3PauseCratePaletteSlotIdsAddr = 0x073Cu,
  kDkc3PauseCratePaletteSlotRefsAddr = 0x074Cu,
  kDkc3PauseCrateLevelConfigFlagsAddr = 0x0777u,
  kDkc3PauseCrateQuawksFlagBit = 0x0010u,

  kDkc3PauseCrateInputBBit = 0x8000u,
  kDkc3PauseCrateInputStartBit = 0x1000u,
  kDkc3PauseCrateInputLeftBit = 0x0200u,
  kDkc3PauseCrateInputRightBit = 0x0100u,

  kDkc3PauseCrateSpriteTableStart = 0x0878u,
  kDkc3PauseCrateFirstFreeSlot = 0x0954u,
  kDkc3PauseCrateSpriteTableEnd = 0x1480u,
  kDkc3PauseCrateSpriteStride = 0x006Eu,
  kDkc3PauseCrateSpriteTypeOffset = 0x00u,
  kDkc3PauseCrateSpriteParamOffset = 0x0Au,
  kDkc3PauseCrateSpriteXOffset = 0x12u,
  kDkc3PauseCrateSpriteYOffset = 0x16u,
  kDkc3PauseCrateSpriteOamOffset = 0x1Eu,
  kDkc3PauseCrateDefaultParam = 0x0005u,

  kDkc3PauseCrateTypeEllie = 0x0234u,
  kDkc3PauseCrateTypeEnguarde = 0x0238u,
  kDkc3PauseCrateTypeSquawks = 0x023Cu,
  kDkc3PauseCrateTypeSquitter = 0x0240u,
  kDkc3PauseCrateTypeParry = 0x0244u,

  kDkc3PauseCratePaletteEllie = 0x0016u,
  kDkc3PauseCratePaletteEnguarde = 0x0017u,
  kDkc3PauseCratePaletteSquawks = 0x0018u,
  kDkc3PauseCratePaletteSquitter = 0x0019u,
  kDkc3PauseCratePaletteParry = 0x001Au,
  kDkc3PauseCratePaletteQuawks = 0x001Eu,

  kDkc3PauseCrateScriptEllie = 0x6586u,
  kDkc3PauseCrateScriptEnguarde = 0x6590u,
  kDkc3PauseCrateScriptSquitter = 0x659Du,
  kDkc3PauseCrateScriptSquawks = 0x65A7u,
  kDkc3PauseCrateScriptParry = 0x65B1u,

  kDkc3PauseCrateMinWramSize = 0x1480u,
};

typedef struct Dkc3AnimalBuddyPaletteRepair {
  uint16_t target_slot;
  uint16_t palette_id;
} Dkc3AnimalBuddyPaletteRepair;

typedef enum Dkc3PauseCrateKind {
  kDkc3PauseCrateEllie = 0,
  kDkc3PauseCrateEnguarde = 1,
  kDkc3PauseCrateSquitter = 2,
  kDkc3PauseCrateSquawks = 3,
  kDkc3PauseCrateParry = 4,
  kDkc3PauseCrateCount = 5,
} Dkc3PauseCrateKind;

typedef struct Dkc3PauseCrateRequest {
  Dkc3PauseCrateKind kind;
  uint16_t slot;
  uint16_t script_ptr;
  uint16_t spawn_x;
  uint16_t spawn_y;
} Dkc3PauseCrateRequest;

/* Returns true only when env_value is non-NULL, non-empty, and not "0".
 * Default is off (false). */
bool Dkc3PauseCrateSpawnEnabledFromEnv(const char *env_value);

/* Returns the display name ("Ellie", "Enguarde", "Squitter", "Squawks",
 * "Parry") for a valid crate kind, or NULL for out-of-range values. */
const char *Dkc3PauseCrateName(Dkc3PauseCrateKind kind);

/* Returns the bank $FF spawn script pointer for a valid crate kind, or 0. */
uint16_t Dkc3PauseCrateScriptPtr(Dkc3PauseCrateKind kind);

/* Returns true when the cheat is enabled and the player has paused inside a
 * level ($004E == $B3:8076, $05AF & $0040, and not $05AF & $8000). */
bool Dkc3IsPauseCrateMenuActive(const uint8_t *wram, size_t wram_size,
                                bool enabled);

/* Updates *selected when Left ($0200) or Right ($0100) is newly pressed while
 * paused in a level (ignoring frames where Start is also newly pressed).
 * Wraps across all kDkc3PauseCrateCount buddies. Returns true if changed. */
bool Dkc3UpdatePauseCrateSelection(const uint8_t *wram, size_t wram_size,
                                   bool enabled, Dkc3PauseCrateKind *selected);

/* Inspects WRAM for an eligible pause-menu crate spawn request:
 * - enabled is true, in level gameplay ($004E == $B3:8076), paused ($05AF & $40)
 * - Start is not newly pressed on the same frame
 * - B ($8000) is newly pressed -> spawns the currently selected buddy crate
 * - Active player sprite ($04F9) is valid and a free non-Kong slot exists.
 * Returns true and populates *out when all conditions hold. */
bool Dkc3DetectPauseCrateSpawn(const uint8_t *wram, size_t wram_size,
                               bool enabled, Dkc3PauseCrateKind selected,
                               Dkc3PauseCrateRequest *out);

/* Clears the target sprite slot, writes the spawn coordinates and default
 * placement parameter (+$0A = 5), and publishes the slot address at DP $0076
 * so apply_spawn_script_to_slot_direct ($BB:8FF1) can populate it. */
bool Dkc3PreparePauseCrateSlot(uint8_t *wram, size_t wram_size,
                               const Dkc3PauseCrateRequest *request);

/* Returns the cartridge palette ID for any of the five animal buddies
 * (Ellie $0016, Enguarde $0017, Squawks $0018 / Quawks $001E when $0777 bit 4
 * is set, Squitter $0019, Parry $001A), or 0 for other sprite types. */
uint16_t Dkc3AnimalBuddyPaletteIdForType(uint16_t sprite_type,
                                         uint16_t level_flags_0777);

/* Detects an animal buddy sprite (either unmounted, or currently drawn on the
 * active Kong slot while mounted via DP $007C) that fell back to hardware
 * palette 0 due to a temporary 8-palette overflow, when a hardware palette
 * slot (1..7) is now available or already holds its palette ID. */
bool Dkc3DetectAnimalBuddyPaletteRepair(const uint8_t *wram, size_t wram_size,
                                        bool enabled,
                                        Dkc3AnimalBuddyPaletteRepair *out);

#ifdef __cplusplus
}
#endif

#endif
