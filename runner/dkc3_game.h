#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common_cpu_infra.h"

typedef struct Dkc3TerrainPrefillStats {
  size_t expected;
  size_t decoded;
  size_t present;
  size_t matching;
  size_t margin_expected;
  size_t margin_present;
  size_t margin_matching;
  /* Margin tiles decoded from a continued wall (Dkc3VideoFindStructuralWallSource). */
  size_t structural;
  /* Margin tiles mirrored across a player-held wall whose edge metatile is
   * partial (Dkc3VideoMirrorSourceTileAcrossEdge). */
  size_t mirrored;
  /* Structurally continued tiles whose metatile came from the level map's
   * own adjacency (what the map places beside the wall's metatile) rather
   * than from a copy of the wall's edge column. */
  size_t chained;
  /* The terrain layer's rendered scroll phase used for keys and band
   * classification, and whether an HDMA band supplied it instead of the
   * frame-start register. */
  uint16_t phase_h;
  uint16_t phase_v;
  uint32_t top_shadow_row;
  uint32_t top_source_row;
  uint8_t phase_from_band;
  /* Row-major level maps: the bytes per metatile row in use, and the
   * percentage of fully staged native cells the decode reproduced with it
   * this frame (the calibration gate). */
  uint16_t row_bytes;
  uint8_t row_match_percent;
  /* The presentation walls and margins this frame used, and the camera
   * region word ($0478) they were taken from. */
  uint16_t camera_region;
  uint16_t wall_left;
  uint16_t wall_right;
  int16_t left_margin;
  int16_t right_margin;
} Dkc3TerrainPrefillStats;

const RtlGameInfo *Dkc3GameInfo(void);
/* The game's own frame counter, the value DKC3_PREFILL_DUMP keys on. */
unsigned Dkc3FrameCounter(void);
/* Diagnostic: the verified row offset of a layer's alias bands this frame. */
int Dkc3AliasOffsetRows(int layer);
/* Diagnostic: sub-screen lines the underwater tint saw, judged water, and tinted since the last call. */
void Dkc3TintLineCounters(unsigned *seen, unsigned *water, unsigned *matched);

void Dkc3BeginDrawing(uint8_t *pixels, size_t pitch);
/* Starts the KONG letter tracker (dkc3_kong_tracker.h) with its sidecar
 * file. Hosts call it once the battery save is in g_sram; tracking runs after
 * every game frame and the map badge is drawn into each rendered frame
 * unless DKC3_KONG_BADGES=0 or Dkc3KongSetBadgesVisible(false). Returns false when the sidecar existed but was
 * unreadable (it is then left untouched until the next record). */
bool Dkc3KongTrackingEnable(const char *path);
/* Shows or hides the map badges (the desktop Settings checkbox); recording
 * continues either way. */
void Dkc3KongSetBadgesVisible(bool visible);
/* Enables or disables the Assist Tools / Cheats option that spawns animal
 * buddy crates from the in-game pause state (Left/Right selects buddy, B
 * spawns the selected crate). Off by default. */
void Dkc3SetPauseCrateSpawnEnabled(bool enabled);
bool Dkc3PauseCrateSpawnEnabled(void);
/* Returns the currently selected animal buddy name while paused in a level
 * with the crate-spawn cheat enabled (or NULL otherwise), for host HUD
 * display at the bottom middle of the screen. */
const char *Dkc3PauseCrateHudLabel(void);
/* Enables or disables the Assist Tools / Cheats option that draws sprites
 * the game keeps invisible (render word +$26 with bits 15 and 14 set, such as
 * the hidden bear coin). Off by default; DKC3_SHOW_HIDDEN_OBJECTS=1 also
 * enables it for headless runs. */
void Dkc3SetShowHiddenObjectsEnabled(bool enabled);
bool Dkc3ShowHiddenObjectsEnabled(void);
void Dkc3DrawPpuFrame(void);
uint32_t Dkc3ResumePc(void);
int Dkc3LastLleResult(void);
void Dkc3GetTerrainPrefillStats(Dkc3TerrainPrefillStats *out);
/* Ship-deck rigging (BG3) margin decode accounting for the widescreen
 * trace: whether the cartridge's rigging streamer is active, whether the
 * ROM decode reproduced every fully uploaded native cell this frame, and
 * how many margin cells were decoded. */
/* Lava geyser steam (bounded BG3) decode diagnostics for the last rendered
 * frame: whether the stage runs the geyser effect, whether the decode
 * reproduced every geyser column the cartridge had fully drawn, the
 * animation frame the ring shows against the frame-counter prediction,
 * and how many margin/inset cells and listed geysers were decoded. */
/* Scanline bands read from the cartridge's HDMA tables for the last
 * rendered frame (host-only diagnostics). */
int Dkc3GetHdmaBandCount(void);
/* Bands of a wide layer presented as a static plane (the map's own wrap)
 * in the last rendered frame (host-only diagnostics). */
int Dkc3GetPlaneBandCount(int layer);
/* Generated $BB:A647 calls these around its placement-list walk. Widescreen
 * scans the current 256-pixel spatial cell and its horizontal neighbors so
 * restored native-index states cannot delay visible objects. */
uint16_t Dkc3PlacementScanBegin(uint16_t native_head,
                                uint16_t native_cell_offset);
uint16_t Dkc3PlacementScanNext(uint16_t native_placement);

/* The generated $B2:800F transition-song trampoline records the command that
 * reached it. The desktop MSU-1 player consumes the last command after the
 * cartridge frame; other hosts may ignore it without changing game state. */
void Dkc3RecordMusicTransition(uint16_t command);
int Dkc3TakeMusicTransition(uint16_t *command);

/* The generated $BB:95E6 palette-allocator overflow block records the failed
 * palette ID and sprite slot so widescreen can serve the sprite's true palette
 * from host extended OBJ CGRAM without mutating guest WRAM or 4:3 output. */
void Dkc3RecordSpritePaletteOverflow(uint16_t direct_page);

/* The generated $B3:C0F4 block that writes W2R records the tree-trunk
 * windows' world edges (DP $1A-$20) so a wide aspect can mask a trunk that
 * lies in a margin. Besides those edges it reads only the camera X and
 * level number from WRAM and the PPU window registers, and writes no guest
 * state; 4:3 returns at once. */
void Dkc3RecordTrunkWindows(uint16_t direct_page);

/* The generated $B7:8904 renderer block passes each slot's render word
 * (+$26) and slot address through this helper. It returns the word unchanged
 * unless Show hidden objects is on, and never writes guest state. */
uint16_t Dkc3RevealHiddenSpriteFlags(uint16_t flags, uint16_t slot);

/* Host-only unclamped window edges in patched ppu.c (window 0 = W1). */
void Dkc3PpuSetWsWindowUnclamped(int window, bool active, int left,
                                 int right, uint8_t reg_left,
                                 uint8_t reg_right);

/* Host-only widescreen extended OBJ palette setters in patched ppu.c. */
void Dkc3PpuClearWsExtObjPalettes(void);
void Dkc3PpuSetWsExtObjPalette(uint8_t slot, const uint16_t colors[16]);
void Dkc3PpuSetWsOamExtPalettes(const uint8_t oam_ext_pal[128], bool active);
