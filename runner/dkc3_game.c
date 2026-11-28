#include "dkc3_game.h"
#include "dkc3_animal_crate_cheat.h"
#include "dkc3_show_hidden.h"
#include "dkc3_hdma.h"
#include "dkc3_intro_skip.h"
#include "dkc3_kong_tracker.h"
#include "dkc3_video.h"
#include "dkc3_facts.h"
#include "dkc3_spc_music.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  /* DKC3 USA vectors: reset $00:80C4, native NMI $00:CA45 (a non-returning
   * frame dispatcher through direct-page $4A, as in DKC2). */
  kDkc3ResetPc = 0x0080C4,
  kDkc3NmiPc = 0x00CA45,
};

static bool s_cpu_initialized;
static Dkc3KongTracker s_kong_tracker;
static bool s_kong_tracking;
static bool s_kong_badges;
static bool s_pause_crate_spawn_enabled;
static bool s_kong_map_settled;
static Dkc3KongMapSettle s_kong_map_settle;
static char s_kong_path[512];
static uint32_t s_resume_pc = kDkc3ResetPc;
static int s_last_lle_result = 1;
static uint64_t s_next_frame_master;
static bool s_widescreen_shadow_active;
static uint32_t s_widescreen_world_x[2];
static uint32_t s_widescreen_world_y[2];
static bool s_widescreen_source_valid;
static uint64_t s_widescreen_source_signature;
static Dkc3TerrainPrefillStats s_terrain_prefill_stats;

/* Per-frame scanline geometry read from the cartridge's own HDMA tables and
 * the presentation policy chosen for each (wide layer, band). A band is
 * served from the world-keyed terrain store (the layer displays the
 * streamed level map at the terrain phase), presented from its own map's
 * hardware wrap (a static, fully authored 64-column plane the cartridge
 * never streams), or repeats its rendered native scanline (a bounded
 * effect or a backdrop the cartridge keeps streaming). */
/* The cartridge's NMI sub-mode ($96) while a level-name card is shown. */
enum { kDkc3NameCardNmiSubMode = 11 };

enum {
  kDkc3BandPolicyRepeat = 0,
  kDkc3BandPolicyWorld = 1,
  kDkc3BandPolicyPlane = 2,
  /* A world band of a second layer whose rows are the terrain ring's rows
   * at a fixed vertical offset (an underwater reflection of the ceiling). */
  kDkc3BandPolicyAlias = 3,
  kDkc3BandPolicyWaterfall = 4,
};
static Dkc3HdmaBands s_frame_bands;
static uint8_t s_band_policy[2][kDkc3HdmaMaxBands];
static bool s_waterfall_scene;
static bool s_waterfall_verified;
static uint32_t s_waterfall_world_x;

static void Dkc3CheckWaterfallSource(uint8_t wide_mask, int terrain_layer) {
  s_waterfall_scene = terrain_layer == 0 && (wide_mask & 2u) &&
      Dkc3VideoUsesWaterfallColumns(Dkc3FactRead16(0x0775),
          Dkc3FactRead16(0x0056), g_ppu->bgmode, g_ppu->bgXsc[1]) &&
      !PPU_bigTiles(g_ppu, 1);
  s_waterfall_verified = false;
  if (!s_waterfall_scene)
    return;
  s_waterfall_world_x = Dkc3VideoTerrainShadowX(
      g_ppu->hScroll[1], Dkc3FactRead16(kDkc3WramCameraX));
  unsigned matching = 0, total = 0;
  /* F3 is the full HiROM mirror of bank B3; B3:0000 itself maps WRAM. */
  s_waterfall_verified = Dkc3VideoVerifyWaterfallViewport(
      RomPtr(0xf30000), 0x10000u, Dkc3FactRead16(0x15e4),
      g_ppu->vram, 0x8000u, (uint16_t)s_waterfall_world_x,
      &matching, &total);
  if (getenv("DKC3_WATERFALL_TRACE"))
    fprintf(stderr, "waterfall frame=%u camera=%u verified=%d cells=%u/%u\n",
            (unsigned)snes_frame_counter, (unsigned)s_waterfall_world_x,
            s_waterfall_verified, matching, total);
}
/* The terrain layer's rendered scroll phase for the frame
 * (Dkc3VideoSelectTerrainPhase): the key for the world store, the prefill's
 * source rows, and the band classification. */
static uint16_t s_terrain_phase_h;
static uint16_t s_terrain_phase_v;
/* The camera's scroll bounds this frame (the current region's walls, or a
 * west hold): the structural wall rule continues walls only into margin
 * columns outside the span those bounds let the native window reach. */
static uint16_t s_reach_minimum_x;
static uint16_t s_reach_maximum_x;
static bool s_terrain_phase_from_band;
static int s_plane_band_count[2];
/* Per layer: the verified row offset of its alias bands this frame (0 none). */
static int s_alias_offset_rows[2];
/* The west hold the last prefill found in the level map (Dkc3VideoHoldWest):
 * the world x of the authored edge the player is held at, and the
 * presented bias, which moves one pixel per frame toward the glide's
 * target (more only to cancel the camera's own motion, see
 * Dkc3VideoStepPresentationBias) so a hold that appears or vanishes never
 * snaps the picture. Both reset with the world store. */
static bool s_hold_west_valid;
static uint32_t s_hold_west_x;
static bool s_bias_valid;
static int s_bias_presented;
/* The camera X the presented bias was last stepped at, so the step may
 * cancel the camera's own motion (Dkc3VideoStepPresentationBias). */
static uint16_t s_bias_camera_x;
/* Frames since the presented bias was reset: within the first few the
 * bias snaps to its target rather than gliding, so a level that starts at
 * a hold opens already slid, as one that starts at the map's first page
 * does. */
enum { kDkc3BiasSettleFrames = 4 };
static uint32_t s_bias_frames;
/* The margins last presented and the view position (camera plus bias)
 * they were presented at, so a wall that jumps outward opens its margin at
 * a bounded rate (Dkc3VideoLimitMarginGrowth). Valid only while the bias
 * is settled and the terrain stayed ready, so a restore, a level start, or
 * a store reset still opens at once. */
static bool s_margin_valid;
static int s_margin_left;
static int s_margin_right;
static int s_margin_view_x;
/* The edge policy the presented bias was computed under. Changing it from
 * the menu re-snaps the bias: easing from the old policy's bias would slide
 * the background and the screen-fixed HUD for up to a margin's worth of
 * frames. */
static Dkc3VideoEdgePolicy s_bias_policy;
/* The overworld's resident maps (Dkc3VideoIsResidentOverworld). Each drawn
 * frame compares the enabled layers' tilemap words with the previous drawn
 * frame's copy. Once the hovercraft opens the north, the game streams rows
 * ahead of a climbing camera (Dkc3VideoOverworldWritesAreRows). Writes of
 * any other shape while the picture is displayed mean the map is not
 * resident after all: margins stay black until the scene is left. A change
 * under forced blank or zero brightness, or a new layer layout, is a load
 * and only restarts the proof. */
static bool s_overworld_copy_valid;
static uint8_t s_overworld_layers;
static uint8_t s_overworld_sc[3];
static uint16_t s_overworld_maps[3][0x800];
static bool s_overworld_streams;
/* The camera of the last prefill, for the hold's entry test: the player
 * stands within kDkc3HoldPlayerEdge pixels of the frame's west edge while
 * the camera does not move. The camera leads a walking player by about
 * sixty pixels, so a player at twenty is one the camera failed to centre:
 * pinned at the level's edge. The player's world x is at $0A2A. */
enum { kDkc3HoldPlayerEdge = 40 };
static bool s_hold_camera_valid;
static uint32_t s_hold_camera_x;
/* A level opens with its camera at a bound, so for the first prefill
 * frames after a level change the hold enters on the void beside the
 * window alone: a fresh Screech's Sprint spawns Diddy sixty pixels in,
 * where a free camera would also put him, and the pin would wait for the
 * player to walk into the edge. A state restore is not a level start
 * (DKC3_LOAD_AS_LEVEL_START=1 makes the headless runner treat it as one,
 * to test the start path from a mid-level save). */
enum { kDkc3HoldStartFrames = 8 };
static uint32_t s_hold_start_frames;
static bool s_state_loaded_recently;

enum {
  kDkc3PlacementCellTableBytes = 0x0180,
  kDkc3PlacementListBytes = 0x2000,
  kDkc3PlacementListWords = kDkc3PlacementListBytes / 2,
};
static uint16_t s_placement_scan[kDkc3PlacementListWords];
static size_t s_placement_scan_count;
static size_t s_placement_scan_index;
static bool s_placement_scan_combined;
static uint16_t s_music_transition;
static bool s_music_transition_pending;

void Dkc3RecordMusicTransition(uint16_t command) {
  s_music_transition = command;
  s_music_transition_pending = true;
}

int Dkc3TakeMusicTransition(uint16_t *command) {
  if (!s_music_transition_pending)
    return 0;
  if (command)
    *command = s_music_transition;
  s_music_transition_pending = false;
  return 1;
}

int Dkc3GetPlaneBandCount(int layer) {
  return layer >= 0 && layer < 2 ? s_plane_band_count[layer] : 0;
}

/*
 * Static tilemap planes. A 64-column map that is not the terrain stream's
 * destination is a fully authored plane (Red-Hot Ride's foreground rocks
 * and far spikes share one such map, swapped between BG1 and BG2 by HDMA)
 * only if the cartridge never streams it. The engine stamps every VRAM
 * page with the frame of its last write; a page counts as static once the
 * camera has traveled kDkc3PlaneTravel pixels from where it stood at that
 * write (or at the level's first widescreen frame) without another write,
 * because a ring the cartridge streams for the camera is rewritten well
 * within that distance. Until then such a band keeps the repeat policy.
 */
enum { kDkc3VramPages = 32, kDkc3PlaneTravel = 24 };
static uint64_t Dkc3LevelSourceSignature(void);
static bool s_page_signature_valid;
static uint64_t s_page_signature;
static uint32_t s_page_write_seen[kDkc3VramPages];
static int32_t s_page_anchor_x[kDkc3VramPages];
static int32_t s_page_anchor_y[kDkc3VramPages];
static bool s_page_traveled[kDkc3VramPages];

/* Cached wrap-authoring verdict per BGnSC value, refreshed when the map's
 * pages are written, on a level change, and periodically as a guard. */
typedef struct Dkc3PlaneCacheEntry {
  bool valid;
  bool authored;
  uint64_t broken_rows;   /* Dkc3VideoTilemapBrokenRows, with `authored` */
  uint16_t character_base; /* the layer's character base the verdict used */
  uint32_t stamp;
  uint32_t frame;
} Dkc3PlaneCacheEntry;
static Dkc3PlaneCacheEntry s_plane_cache[256];
static uint32_t s_plane_frame;
/* Object planes are rewritten as the object animates, so their verdict is
 * refreshed whenever a page changes (every frame, in practice). */
static Dkc3PlaneCacheEntry s_object_plane_cache[256];

/*
 * Row-level change tracking for the two wide layers' 64x32 tilemaps. The
 * engine's page stamps cover 1 KB pages, so a layer that streams four rows
 * (a river's reflection band) marks its whole map as written and its
 * untouched backdrop rows fall to the repeat policy, which repeats a
 * 512-pixel plane at 256. Each row keeps its own travel gate instead: a row
 * counts as static once the camera has traveled kDkc3PlaneTravel pixels from
 * where it stood when the row last changed. A level (or a restored state)
 * starts with every row counted as traveled, as the pages do.
 */
enum { kDkc3TrackedMaps = 2, kDkc3MapRows = 32, kDkc3MapCols = 64,
       kDkc3AliasMinCells = 8 };
static bool Dkc3DecodeLevelTile(const uint8_t *bank_data, uint16_t map_base,
                                uint16_t metatile_base,
                                Dkc3VideoLevelLayout layout,
                                unsigned row_bytes, uint32_t tile_x,
                                uint32_t tile_y, uint16_t *entry);
static uint16_t s_map_base_word[kDkc3TrackedMaps];
static uint16_t s_map_rows[kDkc3TrackedMaps][kDkc3MapRows][kDkc3MapCols];
static int32_t s_row_anchor_x[kDkc3TrackedMaps][kDkc3MapRows];
static int32_t s_row_anchor_y[kDkc3TrackedMaps][kDkc3MapRows];
static bool s_row_traveled[kDkc3TrackedMaps][kDkc3MapRows];
/* Per layer and ring row: the row offset at which the row equals the terrain
 * ring over the native columns this frame (0: none verified). */
static int8_t s_row_alias_offset[kDkc3TrackedMaps][kDkc3MapRows];
static bool s_row_alias_valid[kDkc3TrackedMaps][kDkc3MapRows];
/* Rows with at least kDkc3AliasMinCells populated native cells: content an
 * alias band must account for. */
static bool s_row_alias_populated[kDkc3TrackedMaps][kDkc3MapRows];
static int s_layer_alias_rows[kDkc3TrackedMaps];

static void Dkc3ReadMapRow(uint16_t base_word, unsigned row,
                           uint16_t *out) {
  for (unsigned col = 0; col < kDkc3MapCols; col++) {
    const uint16_t word = (uint16_t)(
        base_word + ((col & 32u) ? 0x400u : 0u) + row * 32u + (col & 31u));
    out[col] = g_ppu->vram[word & 0x7fffu];
  }
}

static void Dkc3TrackMapRows(int32_t camera_x, int32_t camera_y,
                             bool reset) {
  for (unsigned layer = 0; layer < kDkc3TrackedMaps; layer++) {
    const uint8_t bg_sc = g_ppu->bgXsc[layer];
    const uint16_t base = (uint16_t)((bg_sc & 0xfcu) << 8);
    const bool wide64 = (bg_sc & 1u) != 0;
    const bool rebase = reset || !wide64 || base != s_map_base_word[layer];
    s_map_base_word[layer] = wide64 ? base : 0;
    if (!wide64)
      continue;
    for (unsigned row = 0; row < kDkc3MapRows; row++) {
      uint16_t current[kDkc3MapCols];
      Dkc3ReadMapRow(base, row, current);
      if (rebase) {
        memcpy(s_map_rows[layer][row], current, sizeof current);
        s_row_traveled[layer][row] = true;
        continue;
      }
      if (memcmp(current, s_map_rows[layer][row], sizeof current) != 0) {
        memcpy(s_map_rows[layer][row], current, sizeof current);
        s_row_traveled[layer][row] = false;
        s_row_anchor_x[layer][row] = camera_x;
        s_row_anchor_y[layer][row] = camera_y;
      }
      if (!s_row_traveled[layer][row]) {
        const int32_t dx = camera_x - s_row_anchor_x[layer][row];
        const int32_t dy = camera_y - s_row_anchor_y[layer][row];
        if (dx >= kDkc3PlaneTravel || dx <= -kDkc3PlaneTravel ||
            dy >= kDkc3PlaneTravel || dy <= -kDkc3PlaneTravel)
          s_row_traveled[layer][row] = true;
      }
    }
  }
}

/* The ring rows a band's scanlines show on a 32-row map. */
static void Dkc3BandRingRows(uint16_t v_scroll, uint8_t first_line,
                             uint8_t last_line, unsigned *first_row,
                             unsigned *last_row) {
  *first_row = ((unsigned)v_scroll + first_line - 1u) >> 3;
  *last_row = ((unsigned)v_scroll + last_line - 1u) >> 3;
}

static bool Dkc3BandRowsStatic(int layer, uint16_t v_scroll,
                               uint8_t first_line, uint8_t last_line) {
  if (layer < 0 || layer >= kDkc3TrackedMaps || !s_map_base_word[layer])
    return false;
  unsigned first, last;
  Dkc3BandRingRows(v_scroll, first_line, last_line, &first, &last);
  for (unsigned row = first; row <= last; row++)
    if (!s_row_traveled[layer][row % kDkc3MapRows])
      return false;
  return true;
}

/*
 * A second 64-column layer scrolling with the terrain can carry the level
 * map itself at a vertical offset: the river levels stream a strip of the
 * map from 288 pixels above into BG2 under the water line, on the
 * subscreen, as the reflection the water's half-color math tints. Those
 * rows hold world content the level map decodes, so neither the plane nor
 * the repeat policy is right for them; the terrain store is, read at that
 * offset, once the prefill has decoded the source rows for the margins.
 * Verify the relation on the cartridge's own ring every frame: a row is an
 * alias at offset d when its cells over the native columns equal the map's
 * decode d rows away in at least nine of ten non-empty cells. The offset
 * proven last is tried first; a full search runs only when it fails.
 */
enum { kDkc3AliasRowReach = 40 };
static int s_alias_offset_cached[kDkc3TrackedMaps];

typedef struct Dkc3AliasDecodeContext {
  const uint8_t *bank_data;
  uint16_t map_base;
  uint16_t metatile_base;
  Dkc3VideoLevelLayout layout;
  unsigned row_bytes;
  uint32_t top_world_row;    /* the world tile row at the ring window's top */
  unsigned top_ring_row;     /* that row's ring row */
  unsigned first_native_col; /* ring column of the native window's first tile */
  uint32_t first_world_col;  /* world tile column of that ring column */
} Dkc3AliasDecodeContext;

static unsigned Dkc3AliasRowMatches(const Dkc3AliasDecodeContext *ctx,
                                    const uint16_t *cells, uint32_t world_row,
                                    int offset, unsigned *populated_out) {
  unsigned populated = 0, matches = 0;
  const int64_t source_row =
      (int64_t)world_row + offset - (int64_t)(kDkc3WorldOriginX >> 3);
  for (unsigned i = 0; i <= 32u; i++) {
    const unsigned col = (ctx->first_native_col + i) & 63u;
    const uint16_t cell = cells[col];
    if (cell == 0)
      continue;
    populated++;
    const int64_t source_col =
        (int64_t)(ctx->first_world_col + i) - (int64_t)(kDkc3WorldOriginX >> 3);
    if (source_row < 0 || source_col < 0)
      continue;
    uint16_t entry = 0;
    if (Dkc3DecodeLevelTile(ctx->bank_data, ctx->map_base, ctx->metatile_base,
                            ctx->layout, ctx->row_bytes, (uint32_t)source_col,
                            (uint32_t)source_row, &entry) &&
        entry == cell)
      matches++;
  }
  *populated_out = populated;
  return matches;
}

static void Dkc3ComputeRowAliasOffsets(int layer, int terrain_layer,
                                       uint16_t layer_h, uint16_t terrain_h,
                                       const Dkc3AliasDecodeContext *ctx) {
  memset(s_row_alias_offset[layer], 0, sizeof s_row_alias_offset[layer]);
  memset(s_row_alias_valid[layer], 0, sizeof s_row_alias_valid[layer]);
  memset(s_row_alias_populated[layer], 0, sizeof s_row_alias_populated[layer]);
  s_layer_alias_rows[layer] = 0;
  if (layer < 0 || layer >= kDkc3TrackedMaps || terrain_layer < 0 ||
      terrain_layer >= kDkc3TrackedMaps || layer == terrain_layer ||
      !s_map_base_word[layer] || !ctx || !ctx->bank_data)
    return;
  /* Both rings must share the column phase within a tile. */
  const unsigned phase_distance =
      (unsigned)(((int)layer_h - (int)terrain_h + 512) & 511);
  if (phase_distance > 8u && phase_distance < 504u)
    return;
  for (unsigned row = 0; row < kDkc3MapRows; row++) {
    const uint16_t *cells = s_map_rows[layer][row];
    const uint32_t world_row =
        ctx->top_world_row + ((row - ctx->top_ring_row) & 31u);
    unsigned populated = 0;
    int best_offset = 0;
    unsigned best_matches = 0;
    const int cached = s_alias_offset_cached[layer];
    if (cached != 0) {
      best_matches = Dkc3AliasRowMatches(ctx, cells, world_row, cached,
                                         &populated);
      best_offset = cached;
    }
    if (populated == 0 || best_matches * 10u < populated * 9u) {
      for (int offset = -kDkc3AliasRowReach; offset <= kDkc3AliasRowReach;
           offset++) {
        unsigned count = 0;
        const unsigned matches =
            Dkc3AliasRowMatches(ctx, cells, world_row, offset, &count);
        populated = count;
        if (matches > best_matches) {
          best_matches = matches;
          best_offset = offset;
        }
        if (populated < kDkc3AliasMinCells)
          break;
      }
    }
    if (populated < kDkc3AliasMinCells)
      continue;
    s_row_alias_populated[layer][row] = true;
    if (best_matches * 10u >= populated * 9u) {
      s_row_alias_offset[layer][row] = (int8_t)best_offset;
      s_row_alias_valid[layer][row] = true;
      s_layer_alias_rows[layer]++;
      s_alias_offset_cached[layer] = best_offset;
    }
    if (getenv("DKC3_ALIAS_TRACE"))
      fprintf(stderr,
              "alias layer=%d ring_row=%u world_row=%u populated=%u best=%d "
              "matches=%u %s\n",
              layer, row, world_row, populated, best_offset, best_matches,
              s_row_alias_valid[layer][row] ? "verified" : "-");
  }
}

/* The band verifies against the terrain when every populated row it shows
 * verifies at one common offset (zero: the terrain itself); that offset is
 * returned. A populated row that verifies nowhere vetoes the band: it is
 * content the alias could not serve. A band with no populated row verifies
 * nothing. (The forest's parallax band once passed on the strength of the
 * one reflection row it happened to wrap onto, and its margins went black.) */
static bool Dkc3BandAliasOffset(int layer, uint16_t v_scroll,
                                uint8_t first_line, uint8_t last_line,
                                int *offset_rows) {
  if (layer < 0 || layer >= kDkc3TrackedMaps || !s_layer_alias_rows[layer])
    return false;
  unsigned first, last;
  Dkc3BandRingRows(v_scroll, first_line, last_line, &first, &last);
  if (last - first >= kDkc3MapRows)
    return false;
  bool any = false;
  int offset = 0;
  for (unsigned row = first; row <= last; row++) {
    const unsigned ring_row = row % kDkc3MapRows;
    if (!s_row_alias_populated[layer][ring_row])
      continue;
    if (!s_row_alias_valid[layer][ring_row])
      return false;
    const int candidate = s_row_alias_offset[layer][ring_row];
    if (any && candidate != offset)
      return false;
    offset = candidate;
    any = true;
  }
  if (!any)
    return false;
  *offset_rows = offset;
  return true;
}

int Dkc3AliasOffsetRows(int layer) {
  return layer >= 0 && layer < kDkc3TrackedMaps ? s_alias_offset_rows[layer]
                                                : 0;
}

static void Dkc3TrackVramPages(int32_t camera_x, int32_t camera_y) {
  const uint64_t signature = Dkc3LevelSourceSignature();
  const bool reset =
      !s_page_signature_valid || signature != s_page_signature;
  s_page_signature = signature;
  s_page_signature_valid = true;
  s_plane_frame++;
  if (reset) {
    memset(s_plane_cache, 0, sizeof s_plane_cache);
    memset(s_object_plane_cache, 0, sizeof s_object_plane_cache);
  }
  Dkc3TrackMapRows(camera_x, camera_y, reset);
  for (unsigned page = 0; page < kDkc3VramPages; page++) {
    const uint32_t stamp = WsShadowVramPageWriteFrame(page);
    if (reset) {
      /* Every non-terrain map in the audited stages is uploaded once at
       * load and never streamed, so a level starts with its pages counted
       * as traveled; the first write after that restarts the gate. */
      s_page_write_seen[page] = stamp;
      s_page_traveled[page] = true;
    } else if (stamp != s_page_write_seen[page]) {
      s_page_write_seen[page] = stamp;
      s_page_traveled[page] = false;
      s_page_anchor_x[page] = camera_x;
      s_page_anchor_y[page] = camera_y;
    }
    if (!s_page_traveled[page]) {
      const int32_t dx = camera_x - s_page_anchor_x[page];
      const int32_t dy = camera_y - s_page_anchor_y[page];
      if (dx >= kDkc3PlaneTravel || dx <= -kDkc3PlaneTravel ||
          dy >= kDkc3PlaneTravel || dy <= -kDkc3PlaneTravel)
        s_page_traveled[page] = true;
    }
  }
}

static bool Dkc3MapWrapsAuthored(uint8_t bg_sc, uint32_t newest_stamp,
                                 uint16_t character_base) {
  Dkc3PlaneCacheEntry *entry = &s_plane_cache[bg_sc];
  if (!entry->valid || entry->stamp != newest_stamp ||
      entry->character_base != character_base ||
      s_plane_frame - entry->frame >= 64u) {
    entry->valid = true;
    entry->stamp = newest_stamp;
    entry->frame = s_plane_frame;
    entry->character_base = character_base;
    entry->authored = Dkc3VideoTilemapWrapsAuthored(g_ppu->vram, 0x8000u,
                                                    bg_sc, character_base);
    entry->broken_rows = Dkc3VideoTilemapBrokenRows(g_ppu->vram, 0x8000u,
                                                    bg_sc, character_base);
  }
  return entry->authored;
}

/* The rows a band shows of a map, from its own vertical scroll and its
 * scanlines, tested against the map's broken rows (a dense painted strip
 * that stops short of the wrap: see Dkc3VideoTilemapBrokenRows). */
static bool Dkc3BandShowsBrokenRows(uint8_t bg_sc, uint16_t v_scroll,
                                    uint8_t first_line, uint8_t last_line) {
  const Dkc3PlaneCacheEntry *entry = &s_plane_cache[bg_sc];
  if (!entry->valid || entry->broken_rows == 0)
    return false;
  const unsigned rows = (bg_sc & 2u) ? 64u : 32u;
  const unsigned first = ((unsigned)v_scroll + first_line - 1u) >> 3;
  const unsigned last = ((unsigned)v_scroll + last_line - 1u) >> 3;
  for (unsigned row = first; row <= last; row++) {
    if (entry->broken_rows & ((uint64_t)1 << (row % rows)))
      return true;
  }
  return false;
}

static bool Dkc3MapIsObjectPlane(uint8_t bg_sc, uint32_t newest_stamp,
                                 uint16_t character_base) {
  Dkc3PlaneCacheEntry *entry = &s_object_plane_cache[bg_sc];
  if (!entry->valid || entry->stamp != newest_stamp ||
      entry->character_base != character_base ||
      s_plane_frame - entry->frame >= 64u) {
    entry->valid = true;
    entry->stamp = newest_stamp;
    entry->frame = s_plane_frame;
    entry->character_base = character_base;
    entry->authored = Dkc3VideoTilemapIsObjectPlane(g_ppu->vram, 0x8000u,
                                                    bg_sc, character_base);
  }
  return entry->authored;
}

/* A band's map is presented as its own wrap when it is 64 columns wide,
 * not the terrain stream's destination, and either a static plane (every
 * page unwritten since the camera last traveled kDkc3PlaneTravel pixels,
 * content authored to continue across the wrap:
 * Dkc3VideoTilemapWrapsAuthored, and none of the rows this band shows a
 * painted strip that stops short of the wrap: Dkc3VideoTilemapBrokenRows;
 * such a band repeats the ring instead) or an object plane (one page holds a
 * block the layer's scroll positions, the other is blank:
 * Dkc3VideoTilemapIsObjectPlane; Haunted Hall's Kackle on BG2). */
static bool Dkc3BandShowsStaticPlane(uint8_t bg_sc, uint16_t stream_base,
                                     int layer, uint16_t v_scroll,
                                     uint8_t first_line, uint8_t last_line) {
  const uint16_t character_base = (uint16_t)PPU_bgTileAdr(g_ppu, layer);
  const uint16_t base = (uint16_t)((bg_sc & 0xfcu) << 8);
  if (!(bg_sc & 1u) || base == stream_base)
    return false;
  uint8_t pages[4];
  const unsigned count = Dkc3VideoTilemapPages(bg_sc, pages);
  if (count == 0)
    return false;
  uint32_t newest = 0;
  for (unsigned index = 0; index < count; index++) {
    const uint32_t stamp = WsShadowVramPageWriteFrame(pages[index]);
    if (stamp > newest)
      newest = stamp;
  }
  /* Static by row: only the rows this band shows must have gone
   * unwritten for the travel gate, so a map that streams a few rows
   * (the reflection band) keeps its authored backdrop rows as a plane. */
  const bool all_static =
      Dkc3BandRowsStatic(layer, v_scroll, first_line, last_line);
  if (all_static && Dkc3MapWrapsAuthored(bg_sc, newest, character_base) &&
      !Dkc3BandShowsBrokenRows(bg_sc, v_scroll, first_line, last_line))
    return true;
  return Dkc3MapIsObjectPlane(bg_sc, newest, character_base);
}

void Dkc3GetTerrainPrefillStats(Dkc3TerrainPrefillStats *out) {
  if (out)
    *out = s_terrain_prefill_stats;
}

int Dkc3GetHdmaBandCount(void) {
  return s_frame_bands.count;
}

typedef struct Dkc3HostSnapshot {
  CpuState cpu;
  uint32_t resume_pc;
  uint64_t next_frame_master;
  uint64_t main_cpu_cycles_estimate;
  uint64_t apu_pace_cycles_estimate;
  uint64_t apu_last_sync_cycles;
  uint64_t apu_last_sync_master;
  int last_lle_result;
  int frame_counter;
  uint8_t cpu_initialized;
  uint8_t last_hdmaen;
  uint8_t memsel;
} Dkc3HostSnapshot;

enum {
  /* NTSC master clocks per non-short host frame. The shared interpreter
   * already accounts each opcode and bus region in this unit. A deadline at
   * this cadence lets VBlank interrupt productive loading/decompression code
   * instead of atomically running hundreds of console frames to the next WAI. */
  kDkc3NtscFrameMasterClocks = 1364 * 262,
};

static void Dkc3KongTrackFrame(void) {
  if (!s_kong_tracking) return;
  s_kong_map_settled =
      Dkc3KongMapSettleUpdate(&s_kong_map_settle, g_ram, 0x20000u);
  bool changed = Dkc3KongTrackerObserveFrame(&s_kong_tracker, g_ram, 0x20000u);
  if (g_sram && g_sram_size > 0)
    changed = Dkc3KongTrackerSyncSram(
                  &s_kong_tracker, g_sram, (size_t)g_sram_size,
                  Dkc3KongInFileSelect(g_ram, 0x20000u)) ||
              changed;
  if (changed || s_kong_tracker.dirty) {
    /* A failed write stays dirty and is retried, but warns only once. */
    static bool warned;
    if (Dkc3KongTrackerSaveFile(&s_kong_tracker, s_kong_path)) {
      s_kong_tracker.dirty = false;
      warned = false;
    } else if (!warned) {
      fprintf(stderr, "warning: unable to write %s\n", s_kong_path);
      warned = true;
    }
  }
}

bool Dkc3KongTrackingEnable(const char *path) {
  Dkc3KongTrackerInit(&s_kong_tracker);
  s_kong_tracking = false;
  s_kong_map_settled = false;
  memset(&s_kong_map_settle, 0, sizeof s_kong_map_settle);
  if (!path || !*path) return false;
  if (strlen(path) >= sizeof s_kong_path) {
    fprintf(stderr, "warning: KONG letter path is too long: %.80s...\n", path);
    return false;
  }
  snprintf(s_kong_path, sizeof s_kong_path, "%s", path);
  const Dkc3KongLoadResult loaded =
      Dkc3KongTrackerLoadFile(&s_kong_tracker, s_kong_path);
  if (loaded == kDkc3KongLoadInvalid) {
    fprintf(stderr, "warning: unreadable KONG letter file %s\n",
            s_kong_path);
    Dkc3KongTrackerInit(&s_kong_tracker);
  }
  s_kong_badges =
      Dkc3KongBadgesEnabledFromEnv(getenv("DKC3_KONG_BADGES"));
  s_kong_tracking = true;
  /* Adopt the loaded battery save as the baseline for the file select's
   * copy and erase. This never clears flags: a slot without a valid save
   * may be a file played only through quick saves. */
  if (g_sram && g_sram_size > 0)
    (void)Dkc3KongTrackerSyncSram(&s_kong_tracker, g_sram,
                                  (size_t)g_sram_size, false);
  return loaded != kDkc3KongLoadInvalid;
}

void Dkc3KongSetBadgesVisible(bool visible) {
  s_kong_badges = visible;
}

static Dkc3PauseCrateKind s_pause_crate_selected = kDkc3PauseCrateEllie;

void Dkc3SetPauseCrateSpawnEnabled(bool enabled) {
  s_pause_crate_spawn_enabled = enabled;
}

bool Dkc3PauseCrateSpawnEnabled(void) {
  return s_pause_crate_spawn_enabled;
}

const char *Dkc3PauseCrateHudLabel(void) {
  const bool enabled =
      s_pause_crate_spawn_enabled ||
      Dkc3PauseCrateSpawnEnabledFromEnv(getenv("DKC3_PAUSE_CRATE_SPAWN"));
  if (!g_ram || !Dkc3IsPauseCrateMenuActive(g_ram, 0x20000u, enabled))
    return NULL;
  return Dkc3PauseCrateName(s_pause_crate_selected);
}

extern RecompReturn apply_spawn_script_to_slot_direct_M0X0(CpuState *cpu);
extern RecompReturn request_palette_direct_M0X0(CpuState *cpu);

/* In vanilla DKC3, $B8:CF2F[$1D] ($B8:CF69, ROM $38CF69) points to $B8:D228
 * (RTS) so pressing X does not dismount Squawks, whereas Enguarde/Ellie/
 * Squitter point to $B8:D229 (the standard X-button dismount routine). When
 * the pause crate spawn cheat is enabled, redirect that interpreter-read table
 * entry to $B8:D229 so the player can dismount Squawks with X; restore $B8:D228
 * when disabled. Fails closed if the ROM bytes do not match. */
static void Dkc3PatchSquawksDismount(bool enabled) {
  static uint16_t applied_target = 0xD228u;
  const uint16_t desired_target = enabled ? 0xD229u : 0xD228u;
  if (desired_target == applied_target || !g_rom)
    return;
  enum { kSquawksXHandlerOffset = 0x38CF69u };
  const uint32_t rom_size =
      g_snes && g_snes->cart ? (uint32_t)g_snes->cart->romSize : 0u;
  if (rom_size < kSquawksXHandlerOffset + 2u)
    return;
  uint8_t *rom = (uint8_t *)(uintptr_t)g_rom;
  if (rom[kSquawksXHandlerOffset] != (uint8_t)(applied_target & 0xFFu) ||
      rom[kSquawksXHandlerOffset + 1u] != (uint8_t)(applied_target >> 8))
    return;
  rom[kSquawksXHandlerOffset] = (uint8_t)(desired_target & 0xFFu);
  rom[kSquawksXHandlerOffset + 1u] = (uint8_t)(desired_target >> 8);
  applied_target = desired_target;
}

static void Dkc3RunAnimalBuddyPaletteRepair(bool enabled) {
  Dkc3AnimalBuddyPaletteRepair repair;
  if (!Dkc3DetectAnimalBuddyPaletteRepair(g_ram, 0x20000u, enabled, &repair))
    return;

  const uint8_t saved_req_lo = g_ram[kDkc3PauseCratePaletteReqAddr];
  const uint8_t saved_req_hi = g_ram[kDkc3PauseCratePaletteReqAddr + 1u];
  CpuState saved_cpu = g_cpu;
  g_cpu.ram = g_ram;
  g_cpu.P = (uint8_t)(g_cpu.P & ~(0x30u | 0x08u));
  g_cpu.m_flag = 0;
  g_cpu.x_flag = 0;
  g_cpu._flag_D = 0;
  g_cpu.D = 0x0000u;
  g_cpu.DB = 0xBBu;
  g_cpu.PB = 0xBBu;
  g_cpu.A = repair.palette_id;
  cpu_write8(&g_cpu, 0x00u, g_cpu.S, 0x95u);
  g_cpu.S = (uint16_t)(g_cpu.S - 1u);
  cpu_write8(&g_cpu, 0x00u, g_cpu.S, 0xC7u);
  g_cpu.S = (uint16_t)(g_cpu.S - 1u);
  g_cpu.host_return_valid = 2;
  (void)request_palette_direct_M0X0(&g_cpu);

  const uint8_t carry = g_cpu._flag_C;
  const uint16_t pal_bits = (uint16_t)(g_cpu.A & 0x0E00u);
  g_cpu = saved_cpu;
  g_ram[kDkc3PauseCratePaletteReqAddr] = saved_req_lo;
  g_ram[kDkc3PauseCratePaletteReqAddr + 1u] = saved_req_hi;

  if (carry == 0u && pal_bits != 0u) {
    const size_t oam_addr =
        (size_t)repair.target_slot + kDkc3PauseCrateSpriteOamOffset;
    const uint16_t old_oam =
        (uint16_t)(g_ram[oam_addr] | ((uint16_t)g_ram[oam_addr + 1u] << 8));
    const uint16_t new_oam = (uint16_t)((old_oam & ~0x0E00u) | pal_bits);
    g_ram[oam_addr] = (uint8_t)(new_oam & 0xFFu);
    g_ram[oam_addr + 1u] = (uint8_t)(new_oam >> 8);
    /* Release the fallback reference on palette 0 if it is above baseline. */
    const uint16_t ref0 = (uint16_t)(g_ram[kDkc3PauseCratePaletteSlotRefsAddr] |
        ((uint16_t)g_ram[kDkc3PauseCratePaletteSlotRefsAddr + 1u] << 8));
    if (ref0 > 1u) {
      const uint16_t next_ref0 = (uint16_t)(ref0 - 1u);
      g_ram[kDkc3PauseCratePaletteSlotRefsAddr] = (uint8_t)(next_ref0 & 0xFFu);
      g_ram[kDkc3PauseCratePaletteSlotRefsAddr + 1u] = (uint8_t)(next_ref0 >> 8);
    }
  }
}

static void Dkc3RunPauseCrateSpawn(void) {
  const bool enabled =
      s_pause_crate_spawn_enabled ||
      Dkc3PauseCrateSpawnEnabledFromEnv(getenv("DKC3_PAUSE_CRATE_SPAWN"));
  Dkc3PatchSquawksDismount(enabled);
  Dkc3RunAnimalBuddyPaletteRepair(enabled);
  (void)Dkc3UpdatePauseCrateSelection(g_ram, 0x20000u, enabled,
                                      &s_pause_crate_selected);
  Dkc3PauseCrateRequest request;
  if (!Dkc3DetectPauseCrateSpawn(g_ram, 0x20000u, enabled,
                                 s_pause_crate_selected, &request))
    return;

  const uint8_t saved_dp76_lo = g_ram[kDkc3PauseCrateDpScratchSlotAddr];
  const uint8_t saved_dp76_hi = g_ram[kDkc3PauseCrateDpScratchSlotAddr + 1u];
  const uint8_t saved_dp42_lo = g_ram[kDkc3PauseCrateDpScratchTempAddr];
  const uint8_t saved_dp42_hi = g_ram[kDkc3PauseCrateDpScratchTempAddr + 1u];
  if (!Dkc3PreparePauseCrateSlot(g_ram, 0x20000u, &request))
    return;

  CpuState saved_cpu = g_cpu;
  g_cpu.ram = g_ram;
  g_cpu.P = (uint8_t)(g_cpu.P & ~(0x30u | 0x08u));
  g_cpu.m_flag = 0;
  g_cpu.x_flag = 0;
  g_cpu._flag_D = 0;
  g_cpu.D = 0x0000u;
  g_cpu.DB = 0xBBu;
  g_cpu.PB = 0xBBu;
  g_cpu.X = request.slot;
  g_cpu.Y = request.script_ptr;
  cpu_write8(&g_cpu, 0x00u, g_cpu.S, 0xBBu);
  g_cpu.S = (uint16_t)(g_cpu.S - 1u);
  cpu_write8(&g_cpu, 0x00u, g_cpu.S, 0x8Fu);
  g_cpu.S = (uint16_t)(g_cpu.S - 1u);
  cpu_write8(&g_cpu, 0x00u, g_cpu.S, 0xF0u);
  g_cpu.S = (uint16_t)(g_cpu.S - 1u);
  g_cpu.host_return_valid = 3;
  (void)apply_spawn_script_to_slot_direct_M0X0(&g_cpu);

  g_cpu = saved_cpu;
  g_ram[kDkc3PauseCrateDpScratchSlotAddr] = saved_dp76_lo;
  g_ram[kDkc3PauseCrateDpScratchSlotAddr + 1u] = saved_dp76_hi;
  g_ram[kDkc3PauseCrateDpScratchTempAddr] = saved_dp42_lo;
  g_ram[kDkc3PauseCrateDpScratchTempAddr + 1u] = saved_dp42_hi;
}

static bool s_show_hidden_objects;
static int s_show_hidden_env = -1;

void Dkc3SetShowHiddenObjectsEnabled(bool enabled) {
  s_show_hidden_objects = enabled;
}

bool Dkc3ShowHiddenObjectsEnabled(void) {
  if (s_show_hidden_env < 0)
    s_show_hidden_env =
        Dkc3ShowHiddenEnabledFromEnv(getenv("DKC3_SHOW_HIDDEN_OBJECTS")) ? 1
                                                                          : 0;
  return s_show_hidden_objects || s_show_hidden_env != 0;
}

uint16_t Dkc3RevealHiddenSpriteFlags(uint16_t flags, uint16_t slot) {
  if ((flags & kDkc3HiddenRenderMask) != kDkc3HiddenRenderMask ||
      !Dkc3ShowHiddenObjectsEnabled() || !g_ram)
    return flags;
  const uint32_t main_loop_vector =
      (uint32_t)g_ram[kDkc3HiddenMainLoopVectorAddr] |
      ((uint32_t)g_ram[kDkc3HiddenMainLoopVectorAddr + 1u] << 8) |
      ((uint32_t)g_ram[kDkc3HiddenMainLoopVectorAddr + 2u] << 16);
  const size_t oam_addr = (size_t)slot + kDkc3HiddenSpriteOamOffset;
  const uint16_t oam_attr =
      (uint16_t)(g_ram[oam_addr] | ((uint16_t)g_ram[oam_addr + 1u] << 8));
  if (!Dkc3ShowHiddenSlotEligible(main_loop_vector, oam_attr))
    return flags;
  const size_t type_addr = (size_t)slot + kDkc3HiddenSpriteTypeOffset;
  if (!Dkc3ShowHiddenTypeEligible(
          (uint16_t)(g_ram[type_addr] | ((uint16_t)g_ram[type_addr + 1u] << 8))))
    return flags;
  const uint16_t mounted_buddy =
      (uint16_t)(g_ram[kDkc3HiddenMountedBuddyAddr] |
                 ((uint16_t)g_ram[kDkc3HiddenMountedBuddyAddr + 1u] << 8));
  return Dkc3ShowHiddenRenderFlags(flags, slot, mounted_buddy, true);
}

static void Dkc3KongDrawFrameBadge(void) {
  if (!s_kong_tracking || !s_kong_badges || !s_kong_map_settled || !g_ppu ||
      !g_snes || !g_snes->cart)
    return;
  if (Dkc3KongTrackerMapBadge(&s_kong_tracker, g_ram, 0x20000u, g_rom,
                              (size_t)g_snes->cart->romSize) ==
      kDkc3KongBadgeNone)
    return;
  (void)Dkc3KongDrawBadge(g_ppu->renderBuffer, (size_t)g_ppu->renderPitch,
                          Dkc3VideoWidth(), kDkc3VideoHeight);
}

static void Dkc3RunOneFrame(void) {
  bool first_frame = !s_cpu_initialized;
  if (s_next_frame_master == 0) {
    s_next_frame_master =
        g_cpu.master_cycles + kDkc3NtscFrameMasterClocks;
  }
  while (s_next_frame_master <= g_cpu.master_cycles)
    s_next_frame_master += kDkc3NtscFrameMasterClocks;
  interp_bridge_set_master_deadline(s_next_frame_master);

  if (first_frame) {
    cpu_state_init(&g_cpu, g_ram);
    s_cpu_initialized = true;
  }
  if (!first_frame && g_snes) {
    if (Dkc3ApplyIntroStartSkip(
            g_ram, 0x20000u, g_snes->input1_currentState,
            Dkc3IntroSkipEnabledFromEnv(getenv("DKC3_SKIP_INTRO_START")))) {
      /* On real hardware, VBlank auto-joypad read shifts all 16 bits out of
       * JOYSER0 ($4016), leaving subsequent reads returning 1 (controller
       * present). When skipping State 0/1 before State 3 has polled 16 times,
       * advance joypad1Index to 16 so $80:8B49 sees the plugged-in pad on the
       * very first button poll. */
      if (g_snes->autoJoyRead && g_snes->joypad1Index < 16u)
        g_snes->joypad1Index = 16u;
    }
  }
  if (!first_frame && g_snes->nmiEnabled) {
    /* DKC3's boot/intro NMI is a non-returning frame dispatcher. The handler
     * jumps through the continuation pointer at direct-page $20; that frame
     * routine resets S and ends at its own WAI rather than executing RTI.
     * Run the handler and continuation together to the next quiescent wait.
     * Resuming the pre-NMI WAI afterwards would discard all progress made by
     * the continuation and leave the palette source buffer permanently zero. */
    g_snes->inNmi = true;
    cpu_push_interrupt_frame_at(&g_cpu, s_resume_pc);
    s_last_lle_result =
        interp_bridge_run_until_quiescent(&g_cpu, kDkc3NmiPc);
  } else {
    s_last_lle_result =
        interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
  }

  interp_bridge_set_master_deadline(0);
  if (!first_frame)
    Dkc3RunPauseCrateSpawn();
  s_resume_pc = interp_bridge_lle_resume_pc();
  if (g_cpu.master_cycles < s_next_frame_master) {
    g_cpu.master_cycles = s_next_frame_master;
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
  }
  s_next_frame_master += kDkc3NtscFrameMasterClocks;
  Dkc3KongTrackFrame();
}

static void Dkc3SaveExtra(SaveLoadInfo *sli) {
  Dkc3HostSnapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.cpu = g_cpu;
  snapshot.cpu.ram = NULL;
  snapshot.resume_pc = s_resume_pc;
  snapshot.next_frame_master = s_next_frame_master;
  snapshot.main_cpu_cycles_estimate = g_main_cpu_cycles_estimate;
  snapshot.apu_pace_cycles_estimate = g_apu_pace_cycles_estimate;
  snapshot.apu_last_sync_cycles = g_apu_last_sync_cycles;
  snapshot.apu_last_sync_master = g_apu_last_sync_master;
  snapshot.last_lle_result = s_last_lle_result;
  snapshot.frame_counter = snes_frame_counter;
  snapshot.cpu_initialized = s_cpu_initialized ? 1u : 0u;
  snapshot.last_hdmaen = g_snesrecomp_last_hdmaen;
  snapshot.memsel = g_memsel;
  sli->func(sli, &snapshot, sizeof snapshot);
}

static void Dkc3LoadExtra(SaveLoadInfo *sli, uint32_t version) {
  (void)version;
  Dkc3HostSnapshot snapshot;
  sli->func(sli, &snapshot, sizeof snapshot);
  g_cpu = snapshot.cpu;
  g_cpu.ram = g_ram;
  s_resume_pc = snapshot.resume_pc;
  s_next_frame_master = snapshot.next_frame_master;
  g_main_cpu_cycles_estimate = snapshot.main_cpu_cycles_estimate;
  g_apu_pace_cycles_estimate = snapshot.apu_pace_cycles_estimate;
  g_apu_last_sync_cycles = snapshot.apu_last_sync_cycles;
  g_apu_last_sync_master = snapshot.apu_last_sync_master;
  s_last_lle_result = snapshot.last_lle_result;
  snes_frame_counter = snapshot.frame_counter;
  s_cpu_initialized = snapshot.cpu_initialized != 0;
  g_snesrecomp_last_hdmaen = snapshot.last_hdmaen;
  g_memsel = snapshot.memsel;
}

static void Dkc3ResetSpritePaletteOverflow(void);
static void Dkc3ResetTrunkWindows(void);

static void Dkc3OnStateLoaded(uint32_t version) {
  (void)version;
  /* Pack selection belongs to the current host session, not the snapshot.
   * Both file loads and in-memory rewind restores pass through this hook. */
  RtlApuLock();
  if (g_snes && g_snes->cart)
    (void)Dkc3RestoreSpcMusicPolicy(
        g_snes->apu, g_rom, g_snes->cart->romSize);
  RtlApuUnlock();
  g_cpu.ram = g_ram;
  g_apu_last_sync_master = g_cpu.master_cycles;
  g_snes->beamMasterLast = g_cpu.master_cycles;
  interp_bridge_set_master_deadline(0);
  WsShadowReset();
  s_widescreen_shadow_active = false;
  s_widescreen_source_valid = false;
  s_hold_west_valid = false;
  s_hold_camera_valid = false;
  s_bias_valid = false;
  s_state_loaded_recently = !getenv("DKC3_LOAD_AS_LEVEL_START");
  s_kong_map_settled = false;
  memset(&s_kong_map_settle, 0, sizeof s_kong_map_settle);
  /* A quick save carries the cartridge SRAM, already restored when this
   * hook runs; adopt it now as the baseline rather than reading the
   * difference as a copy or erase. Adopting here instead of on the next
   * frame keeps a copy or erase made in that first file-select frame. */
  if (s_kong_tracking && g_sram && g_sram_size > 0)
    (void)Dkc3KongTrackerSyncSram(&s_kong_tracker, g_sram,
                                  (size_t)g_sram_size, false);
  else
    Dkc3KongTrackerForgetSram(&s_kong_tracker);
  Dkc3VideoSetTerrainReady(false);
  Dkc3ResetSpritePaletteOverflow();
  Dkc3ResetTrunkWindows();
}

static const RtlGameInfo kDkc3GameInfo = {
  .title = "dkc3",
  .initialize = NULL,
  .run_frame = &Dkc3RunOneFrame,
  .draw_ppu_frame = &Dkc3DrawPpuFrame,
  .save_name_prefix = "dkc3s",
  .state_save_extra = &Dkc3SaveExtra,
  .state_load_extra = &Dkc3LoadExtra,
  .on_state_loaded = &Dkc3OnStateLoaded,
};

const RtlGameInfo *Dkc3GameInfo(void) {
  return &kDkc3GameInfo;
}

void Dkc3BeginDrawing(uint8_t *pixels, size_t pitch) {
  PpuBeginDrawing(g_ppu, pixels, pitch, kPpuRenderFlags_NewRenderer);
}

static uint16_t Dkc3ReadWram16(uint16_t address) {
  return (uint16_t)g_ram[address] |
         ((uint16_t)g_ram[(uint16_t)(address + 1u)] << 8);
}

enum {
  kDkc3SpriteSlotCount = 24,
  kDkc3SpriteTableBase = 0x0878u,
  kDkc3SpriteSlotStride = 0x006Eu,
  kDkc3WramPaletteReqId = 0x04EFu,
  kDkc3WramPaletteSlotIds = 0x073Cu,
  kDkc3WramPaletteTintedFlags = 0x0775u,
  kDkc3WramPaletteLastOverflow = 0x1D4Bu,
  kDkc3SpriteStaticTileBase = 0x01E0u,
  kDkc3CommonPaletteId = 0x000Fu,
  kDkc3BurstEffectSpriteType = 0x026Cu,
  kDkc3WsExtObjMaxPalettes = 24,
};

enum { kDkc3OverflowRecoveryFrames = 8 };

static uint16_t s_sprite_overflow_pal[kDkc3SpriteSlotCount];
/* Frames left in which a $1D4B fallback captured before this session may be
 * re-attached to a palette-0 sprite. Armed by a state load and by entering a
 * wide aspect; consumed by the first attachment or when it runs out, so a
 * stale $1D4B is never matched to an unrelated sprite later on. */
static unsigned s_overflow_recovery_frames = 0;

static void Dkc3ResetSpritePaletteOverflow(void) {
  memset(s_sprite_overflow_pal, 0, sizeof s_sprite_overflow_pal);
  s_overflow_recovery_frames = kDkc3OverflowRecoveryFrames;
  Dkc3PpuClearWsExtObjPalettes();
}

void Dkc3RecordSpritePaletteOverflow(uint16_t direct_page) {
  if (!Dkc3VideoIsWidescreen())
    return;
  const uint16_t palette_id = Dkc3ReadWram16(kDkc3WramPaletteReqId);
  if (palette_id == 0 || palette_id == kDkc3CommonPaletteId)
    return;
  const uint16_t slot_addr =
      Dkc3ReadWram16((uint16_t)(direct_page + 0x0076u));
  if (slot_addr < kDkc3SpriteTableBase)
    return;
  const uint16_t offset = (uint16_t)(slot_addr - kDkc3SpriteTableBase);
  if ((offset % kDkc3SpriteSlotStride) != 0)
    return;
  const unsigned slot = (unsigned)(offset / kDkc3SpriteSlotStride);
  if (slot >= kDkc3SpriteSlotCount)
    return;
  s_sprite_overflow_pal[slot] = palette_id;
}

/* $B3:C0F4's trunk windows, recorded when the routine writes them and
 * applied to every frame the PPU draws while the level and the window
 * registers are still the ones recorded. The routine does not run while the
 * game is paused; the registers stay put then, so the masks stay too. It
 * runs before the frame's camera update, so its edges trail the camera by
 * one frame exactly as the registers it writes do. A level change, a state
 * load, or any other window write withdraws them. */
static struct {
  int16_t edges[4];
  uint8_t regs[4];
  uint16_t level;
  unsigned mask;
} s_trunk_windows;

static void Dkc3ResetTrunkWindows(void) {
  s_trunk_windows.mask = 0;
  for (int window = 0; window < 2; window++)
    Dkc3PpuSetWsWindowUnclamped(window, false, 0, 0, 0, 0);
}

void Dkc3RecordTrunkWindows(uint16_t direct_page) {
  if (!Dkc3VideoIsWidescreen() || !g_ppu)
    return;
  uint16_t world[4];
  for (int i = 0; i < 4; i++)
    world[i] = Dkc3ReadWram16((uint16_t)(direct_page + 0x001Au + i * 2u));
  const uint8_t regs[4] = {g_ppu->window1left, g_ppu->window1right,
                           g_ppu->window2left, g_ppu->window2right};
  const uint16_t camera = Dkc3ReadWram16(kDkc3WramCameraX);
  s_trunk_windows.mask = Dkc3VideoUnclampedTrunkWindows(
      world, camera, regs, s_trunk_windows.edges);
  memcpy(s_trunk_windows.regs, regs, sizeof regs);
  s_trunk_windows.level = Dkc3ReadWram16(kDkc3WramLevelNumber);
  if (getenv("DKC3_TRUNK_WINDOW_TRACE"))
    fprintf(stderr,
            "trunk_windows world=[%04x,%04x,%04x,%04x] camera=%04x "
            "regs=[%u,%u,%u,%u] edges=[%d,%d,%d,%d] mask=%u\n",
            world[0], world[1], world[2], world[3], camera, regs[0],
            regs[1], regs[2], regs[3], s_trunk_windows.edges[0],
            s_trunk_windows.edges[1], s_trunk_windows.edges[2],
            s_trunk_windows.edges[3], s_trunk_windows.mask);
}

/* Hand the recorded windows to the PPU for this frame, or withdraw them.
 * `active` is false whenever the terrain path is not presenting margins.
 * The PPU also drops a window whose registers change mid-frame. */
static void Dkc3ApplyTrunkWindows(bool active) {
  const bool live =
      active && Dkc3VideoCullWidenEnabled() && s_trunk_windows.mask != 0 &&
      s_trunk_windows.level == Dkc3ReadWram16(kDkc3WramLevelNumber);
  for (int window = 0; window < 2; window++) {
    const bool on = live && (s_trunk_windows.mask & (1u << window)) != 0;
    Dkc3PpuSetWsWindowUnclamped(
        window, on, s_trunk_windows.edges[window * 2],
        s_trunk_windows.edges[window * 2 + 1],
        s_trunk_windows.regs[window * 2],
        s_trunk_windows.regs[window * 2 + 1]);
  }
}

static bool Dkc3LoadSpritePaletteColors(uint16_t palette_id,
                                        uint16_t out[16]) {
  if (!out || !g_rom || !g_snes || !g_snes->cart || palette_id == 0)
    return false;
  const size_t rom_size = (size_t)g_snes->cart->romSize;
  const size_t ptr_off = 0x3D3201u + (size_t)palette_id * 2u;
  if (ptr_off + 2u > rom_size)
    return false;
  const uint16_t ptr = (uint16_t)g_rom[ptr_off] |
                       ((uint16_t)g_rom[ptr_off + 1u] << 8);
  out[0] = 0;
  if ((Dkc3ReadWram16(kDkc3WramPaletteTintedFlags) & 0x8000u) != 0) {
    const uint16_t wram_ptr = (uint16_t)(ptr - 0x341Bu + 0x3180u);
    for (unsigned c = 0; c < 15u; c++) {
      out[c + 1u] = Dkc3ReadWram16((uint16_t)(wram_ptr + c * 2u));
    }
    return true;
  }
  const size_t data_off = 0x3D0000u + (size_t)ptr;
  if (data_off + 30u > rom_size)
    return false;
  for (unsigned c = 0; c < 15u; c++) {
    const size_t o = data_off + (size_t)c * 2u;
    out[c + 1u] = (uint16_t)g_rom[o] | ((uint16_t)g_rom[o + 1u] << 8);
  }
  return true;
}

/* The palette a sprite type's bank-$FF init scripts request, or 0. */
static uint16_t Dkc3InitScriptPaletteForType(uint16_t sprite_type) {
  enum { kInitScriptBankOffset = 0x3F0000u, kInitScriptBankSize = 0x10000u };
  if (!g_rom || !g_snes || !g_snes->cart ||
      (size_t)g_snes->cart->romSize <
          (size_t)kInitScriptBankOffset + kInitScriptBankSize)
    return 0;
  return Dkc3VideoInitScriptPaletteForType(
      g_rom + kInitScriptBankOffset, kInitScriptBankSize, sprite_type);
}

static uint16_t Dkc3SpriteTileRangeEnd(unsigned target_slot,
                                       uint16_t tile_base) {
  uint16_t tile_end = kDkc3SpriteStaticTileBase;
  for (unsigned other = 0; other < kDkc3SpriteSlotCount; other++) {
    if (other == target_slot)
      continue;
    const uint16_t base =
        (uint16_t)(kDkc3SpriteTableBase + other * kDkc3SpriteSlotStride);
    if (Dkc3ReadWram16(base) == 0)
      continue;
    const uint16_t oam_attr = Dkc3ReadWram16((uint16_t)(base + 0x1Eu));
    if (oam_attr == 0)
      continue;
    const uint16_t other_base = (uint16_t)(oam_attr & 0x01FFu);
    if (other_base > tile_base && other_base < tile_end)
      tile_end = other_base;
  }
  return tile_end;
}

static void Dkc3SyncWidescreenObjPalettes(bool in_level) {
  Dkc3PpuClearWsExtObjPalettes();
  if (!Dkc3VideoIsWidescreen() || !in_level) {
    /* Nothing is recorded at 4:3, so a tracked association would be stale by
     * the time a wide aspect returns. Re-arm recovery for that return. */
    memset(s_sprite_overflow_pal, 0, sizeof s_sprite_overflow_pal);
    s_overflow_recovery_frames = in_level ? kDkc3OverflowRecoveryFrames : 0;
    return;
  }

  /* Clear entries whose sprite slot despawned or received a real hardware
   * palette (1..7). */
  for (unsigned slot = 0; slot < kDkc3SpriteSlotCount; slot++) {
    if (s_sprite_overflow_pal[slot] == 0)
      continue;
    const uint16_t base =
        (uint16_t)(kDkc3SpriteTableBase + slot * kDkc3SpriteSlotStride);
    const uint16_t type = Dkc3ReadWram16(base);
    const uint16_t oam_attr = Dkc3ReadWram16((uint16_t)(base + 0x1Eu));
    if (type == 0 || oam_attr == 0 || ((oam_attr >> 9) & 7u) != 0)
      s_sprite_overflow_pal[slot] = 0;
  }

  /* If the active Kong is riding Squawks or Enguarde and inherited an
   * overflowed palette 0 when DKC3 swapped OAM attributes on mount, ensure
   * the extended widescreen palette table still serves the buddy's palette. */
  {
    const uint16_t mounted_buddy =
        Dkc3ReadWram16(kDkc3PauseCrateDpMountedBuddyAddr);
    const uint16_t active_kong =
        Dkc3ReadWram16(kDkc3PauseCrateActiveKongAddr);
    if (mounted_buddy >= kDkc3PauseCrateFirstFreeSlot &&
        mounted_buddy < kDkc3PauseCrateSpriteTableEnd &&
        ((mounted_buddy - kDkc3SpriteTableBase) % kDkc3SpriteSlotStride) == 0 &&
        (active_kong == kDkc3SpriteTableBase ||
         active_kong == kDkc3SpriteTableBase + kDkc3SpriteSlotStride)) {
      const uint16_t buddy_type = Dkc3ReadWram16(mounted_buddy);
      const uint16_t buddy_pal = Dkc3AnimalBuddyPaletteIdForType(
          buddy_type, Dkc3ReadWram16(kDkc3PauseCrateLevelConfigFlagsAddr));
      const unsigned kong_slot =
          (unsigned)((active_kong - kDkc3SpriteTableBase) /
                     kDkc3SpriteSlotStride);
      const uint16_t kong_oam =
          Dkc3ReadWram16((uint16_t)(active_kong + 0x1Eu));
      if (buddy_pal != 0u && kong_slot < kDkc3SpriteSlotCount &&
          kong_oam != 0u && ((kong_oam >> 9) & 7u) == 0u) {
        s_sprite_overflow_pal[kong_slot] = buddy_pal;
      }
    }
  }

  /* Savestate recovery. The hook's associations are host state that a quick
   * save does not carry, so after a load (or an aspect change) a dynamic
   * sprite stuck on palette 0 has lost its palette. Only runs in the short
   * window after a load or an aspect change: live overflows are recorded by
   * the hook instead.
   *
   * First, a sprite whose type's init scripts all request one palette gets
   * that palette. $1D4B names only the most recent overflow, which need not
   * be this sprite's: a bear coin ($0284, palette $00C8) restored beside a
   * later $0071 overflow was drawn orange. */
  const uint16_t last_overflow = Dkc3ReadWram16(kDkc3WramPaletteLastOverflow);
  const bool try_recovery = s_overflow_recovery_frames != 0;
  if (try_recovery)
    s_overflow_recovery_frames--;
  bool type_known[kDkc3SpriteSlotCount];
  memset(type_known, 0, sizeof type_known);
  for (unsigned slot = 0; try_recovery && slot < kDkc3SpriteSlotCount;
       slot++) {
    const uint16_t base =
        (uint16_t)(kDkc3SpriteTableBase + slot * kDkc3SpriteSlotStride);
    const uint16_t type = Dkc3ReadWram16(base);
    if (type == 0)
      continue;
    const uint16_t palette_id = Dkc3InitScriptPaletteForType(type);
    if (palette_id == 0)
      continue;
    type_known[slot] = true;
    if (s_sprite_overflow_pal[slot] != 0 ||
        palette_id == kDkc3CommonPaletteId)
      continue;
    const uint16_t oam_attr = Dkc3ReadWram16((uint16_t)(base + 0x1Eu));
    if (oam_attr == 0 || ((oam_attr >> 9) & 7u) != 0)
      continue;
    const uint16_t tile_base = (uint16_t)(oam_attr & 0x01FFu);
    if (tile_base >= kDkc3SpriteStaticTileBase)
      continue;
    s_sprite_overflow_pal[slot] = palette_id;
  }

  /* Then, for a sprite whose type the scripts do not settle, fall back to
   * $1D4B when it is not already loaded in hardware CGRAM or tracked on
   * another slot. */
  if (try_recovery && last_overflow != 0 &&
      last_overflow != kDkc3CommonPaletteId) {
    bool known = false;
    for (unsigned p = 0; p < 8u; p++) {
      if (Dkc3ReadWram16((uint16_t)(kDkc3WramPaletteSlotIds + p * 2u)) ==
          last_overflow) {
        known = true;
        break;
      }
    }
    for (unsigned slot = 0; !known && slot < kDkc3SpriteSlotCount; slot++) {
      if (s_sprite_overflow_pal[slot] == last_overflow)
        known = true;
    }
    if (!known) {
      int best_slot = -1;
      for (unsigned slot = 0; slot < kDkc3SpriteSlotCount; slot++) {
        if (s_sprite_overflow_pal[slot] != 0 || type_known[slot])
          continue;
        const uint16_t base =
            (uint16_t)(kDkc3SpriteTableBase + slot * kDkc3SpriteSlotStride);
        const uint16_t type = Dkc3ReadWram16(base);
        const uint16_t oam_attr = Dkc3ReadWram16((uint16_t)(base + 0x1Eu));
        if (type == 0 || type == kDkc3BurstEffectSpriteType ||
            oam_attr == 0 || ((oam_attr >> 9) & 7u) != 0)
          continue;
        const uint16_t tile_base = (uint16_t)(oam_attr & 0x01FFu);
        if (tile_base >= kDkc3SpriteStaticTileBase)
          continue;
        const uint16_t tile_end = Dkc3SpriteTileRangeEnd(slot, tile_base);
        bool in_oam = false;
        for (unsigned i = 0; i < 128u; i++) {
          const uint16_t oam1 = g_ppu->oam[i * 2u + 1u];
          if (((oam1 >> 9) & 7u) == 0) {
            const uint16_t tile = (uint16_t)(oam1 & 0x01FFu);
            if (tile >= tile_base && tile < tile_end) {
              in_oam = true;
              break;
            }
          }
        }
        if (in_oam)
          best_slot = (int)slot;
      }
      if (best_slot >= 0) {
        s_sprite_overflow_pal[best_slot] = last_overflow;
        s_overflow_recovery_frames = 0;
      }
    }
  }

  uint16_t ext_ids[kDkc3WsExtObjMaxPalettes];
  unsigned ext_count = 0;
  uint8_t oam_ext_pal[128];
  memset(oam_ext_pal, 0, sizeof oam_ext_pal);
  bool any_active = false;

  for (unsigned slot = 0; slot < kDkc3SpriteSlotCount; slot++) {
    const uint16_t palette_id = s_sprite_overflow_pal[slot];
    if (palette_id == 0)
      continue;
    const uint16_t base =
        (uint16_t)(kDkc3SpriteTableBase + slot * kDkc3SpriteSlotStride);
    const uint16_t oam_attr = Dkc3ReadWram16((uint16_t)(base + 0x1Eu));
    const uint16_t tile_base = (uint16_t)(oam_attr & 0x01FFu);
    if (tile_base >= kDkc3SpriteStaticTileBase)
      continue;

    uint8_t ext_slot = 0;
    for (unsigned e = 0; e < ext_count; e++) {
      if (ext_ids[e] == palette_id) {
        ext_slot = (uint8_t)(e + 1u);
        break;
      }
    }
    if (ext_slot == 0) {
      if (ext_count >= kDkc3WsExtObjMaxPalettes)
        continue;
      uint16_t colors[16];
      if (!Dkc3LoadSpritePaletteColors(palette_id, colors))
        continue;
      ext_ids[ext_count] = palette_id;
      ext_slot = (uint8_t)(ext_count + 1u);
      ext_count++;
      Dkc3PpuSetWsExtObjPalette(ext_slot, colors);
    }

    const uint16_t tile_end = Dkc3SpriteTileRangeEnd(slot, tile_base);
    for (unsigned i = 0; i < 128u; i++) {
      const uint16_t oam1 = g_ppu->oam[i * 2u + 1u];
      if (((oam1 >> 9) & 7u) != 0)
        continue;
      const uint16_t tile = (uint16_t)(oam1 & 0x01FFu);
      if (tile >= tile_base && tile < tile_end) {
        oam_ext_pal[i] = ext_slot;
        any_active = true;
      }
    }
  }

  if (any_active)
    Dkc3PpuSetWsOamExtPalettes(oam_ext_pal, true);
  if (getenv("DKC3_EXT_PAL_TRACE")) {
    fprintf(stderr, "ext_pal frame=%u last=%04x ext=%u active=%d slots:",
            (unsigned)snes_frame_counter, last_overflow, ext_count,
            any_active ? 1 : 0);
    for (unsigned slot = 0; slot < kDkc3SpriteSlotCount; slot++)
      if (s_sprite_overflow_pal[slot])
        fprintf(stderr, " %u=%04x", slot, s_sprite_overflow_pal[slot]);
    fprintf(stderr, "\n");
  }
}

/* $BB:A647 normally walks one list selected from the level's persistent
 * 256-pixel placement index. A quick save made by an older build preserves
 * that native-width index, and changing aspect is host-only, so rebuilding it
 * on restore would either lose object state or leave stale buckets. Merge the
 * current cell and every horizontal neighbor the widened placement interval
 * can reach (up to two cells per side at 21:9 with spawn lead) for the
 * generated list walk instead. The terrain-gated $BB:AAD4 compare remains
 * authoritative. */
uint16_t Dkc3PlacementScanBegin(uint16_t native_head,
                                uint16_t native_cell_offset) {
  uint16_t cell_offsets[kDkc3VideoPlacementScanMaxCells];
  const uint16_t row_stride = Dkc3ReadWram16(0x15fa);
  const size_t cell_count = Dkc3VideoPlacementScanCells(
      native_cell_offset, row_stride, cell_offsets);
  s_placement_scan_count = 0;
  s_placement_scan_index = 0;
  s_placement_scan_combined = cell_count > 1;
  if (!s_placement_scan_combined)
    return native_head;

  for (size_t cell_index = 0; cell_index < cell_count; cell_index++) {
    const uint16_t cell_offset = cell_offsets[cell_index];
    if (cell_offset >= kDkc3PlacementCellTableBytes)
      continue;
    uint16_t list_offset = Dkc3ReadWram16(
        (uint16_t)(0x15feu + cell_offset));
    if (list_offset == 0 || (list_offset & 1u) != 0 ||
        list_offset >= kDkc3PlacementListBytes)
      continue;
    while (list_offset < kDkc3PlacementListBytes) {
      const uint16_t placement = Dkc3ReadWram16(
          (uint16_t)(0x4180u + list_offset));
      if (placement == 0)
        break;
      bool duplicate = false;
      for (size_t index = 0; index < s_placement_scan_count; index++) {
        if (s_placement_scan[index] == placement) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate && s_placement_scan_count < kDkc3PlacementListWords)
        s_placement_scan[s_placement_scan_count++] = placement;
      list_offset = (uint16_t)(list_offset + 2u);
    }
  }

  if (s_placement_scan_count == 0)
    return 0;
  return native_head != 0 ? native_head : 2;
}

uint16_t Dkc3PlacementScanNext(uint16_t native_placement) {
  if (!s_placement_scan_combined)
    return native_placement;
  if (s_placement_scan_index >= s_placement_scan_count)
    return 0;
  return s_placement_scan[s_placement_scan_index++];
}

/* The terrain ring's VRAM base is the streamer's destination word at
 * $1969. Should it read zero, fall back to the 64-column layer whose
 * horizontal scroll follows the camera. */
static uint16_t Dkc3TerrainVramBase(void) {
  const uint16_t stored = Dkc3ReadWram16(kDkc3WramTerrainVramBase);
  if (stored != 0)
    return stored;
  const uint16_t camera_x = Dkc3ReadWram16(kDkc3WramCameraX);
  for (unsigned layer = 0; layer < 2; layer++) {
    if (!(g_ppu->bgXsc[layer] & 1u))
      continue;
    const uint16_t offset =
        (uint16_t)((g_ppu->hScroll[layer] - camera_x) & 0x03ffu);
    const uint16_t distance =
        offset > 0x0200u ? (uint16_t)(0x0400u - offset) : offset;
    if (distance <= 16u)
      return (uint16_t)((g_ppu->bgXsc[layer] & 0xfcu) << 8);
  }
  return 0;
}

static void Dkc3ResetWidescreenShadow(void) {
  if (s_widescreen_shadow_active)
    WsShadowReset();
  s_widescreen_shadow_active = false;
  s_widescreen_source_valid = false;
  s_hold_west_valid = false;
  s_hold_camera_valid = false;
  s_bias_valid = false;
  Dkc3VideoSetTerrainReady(false);
}

static void Dkc3ResetOverworldState(void) {
  s_overworld_copy_valid = false;
  s_overworld_streams = false;
}

/* The layers whose vertical scroll this frame's HDMA rewrites per line. */
static uint8_t Dkc3OverworldVerticalHdmaLayers(void) {
  uint8_t b_address[8];
  uint8_t mode[8];
  for (int index = 0; index < 8; index++) {
    b_address[index] = g_dma->channel[index].bAdr;
    mode[index] = g_dma->channel[index].mode;
  }
  return Dkc3VideoHdmaVerticalScrollLayers(g_snesrecomp_last_hdmaen,
                                           b_address, mode);
}

/* Compare each enabled BG1-BG3 tilemap (32 rows, one or two pages) with the
 * previous drawn frame's copy and refresh the copy. Returns false when the
 * copy was not comparable (first frame, or a new layer layout) or a layer's
 * writes are not row streaming; sets *foreign_writes in the latter case. */
static bool Dkc3OverworldMapsResident(uint8_t enabled_layers,
                                      bool *foreign_writes) {
  const uint8_t layers = (uint8_t)(enabled_layers & 0x07u);
  bool comparable = s_overworld_copy_valid && s_overworld_layers == layers;
  for (unsigned layer = 0; layer < 3; layer++) {
    if ((layers & (1u << layer)) &&
        s_overworld_sc[layer] != g_ppu->bgXsc[layer])
      comparable = false;
  }
  const uint8_t vertical_hdma = Dkc3OverworldVerticalHdmaLayers();
  *foreign_writes = false;
  for (unsigned layer = 0; layer < 3; layer++) {
    if (!(layers & (1u << layer)))
      continue;
    uint32_t changed_rows = 0;
    uint8_t pages[4];
    const unsigned count = Dkc3VideoTilemapPages(g_ppu->bgXsc[layer], pages);
    for (unsigned page = 0; page < count && page < 2; page++) {
      const uint16_t *words = g_ppu->vram + ((unsigned)pages[page] << 10);
      uint16_t *copy = s_overworld_maps[layer] + page * 0x400u;
      for (unsigned word = 0; word < 0x400; word++) {
        if (words[word] == copy[word])
          continue;
        changed_rows |= 1u << (word >> 5);
        copy[word] = words[word];
      }
    }
    if (comparable &&
        !Dkc3VideoOverworldWritesAreRows(changed_rows, g_ppu->vScroll[layer],
                                         (vertical_hdma >> layer) & 1u))
      *foreign_writes = true;
    s_overworld_sc[layer] = g_ppu->bgXsc[layer];
  }
  s_overworld_layers = layers;
  s_overworld_copy_valid = true;
  return comparable && !*foreign_writes;
}

/* Present the resident overworld widened: raw BG1-BG3 across the margins,
 * each margin clamped to the 512-pixel map. The view stays locked to the
 * camera (no glide): a presentation bias shifts objects in the PPU, but on
 * this raw path nothing shifts the backgrounds with them, so a biased frame
 * would misplace every sprite against the map. Returns false (black
 * margins) until the maps are proven resident. */
static bool Dkc3PresentOverworld(uint8_t enabled_layers) {
  bool foreign_writes = false;
  const bool stable =
      Dkc3OverworldMapsResident(enabled_layers, &foreign_writes);
  const bool displayed =
      !PPU_forcedBlank(g_ppu) && PPU_brightness(g_ppu) != 0;
  if (foreign_writes && displayed)
    s_overworld_streams = true;
  if (!stable || s_overworld_streams)
    return false;

  const int extra = Dkc3VideoExtra();
  int left_margin = extra;
  int right_margin = extra;
  /* Clamp at the scroll the frame renders, not the camera word, which
   * leads it while the boat moves. */
  Dkc3VideoOverworldMargins(Dkc3VideoOverworldRenderedX(g_ppu->hScroll[0]),
                            &left_margin, &right_margin);
  PpuSetExtraSpace(g_ppu, (uint8_t)extra);
  Dkc3VideoSetPresentationBias(0);
  PpuSetExtraSideSpace(g_ppu, left_margin, right_margin, 0);
  Dkc3VideoClearOutsideMargins(g_ppu->renderBuffer, g_ppu->renderPitch,
                               Dkc3VideoWidth(), kDkc3VideoHeight, extra,
                               left_margin, right_margin);
  /* Every enabled layer reads VRAM directly: BG1 and BG3 are the resident
   * 512-pixel pictures and BG2's 32-column water wraps at 256 pixels as
   * the hardware does, with its per-line HDMA scroll evaluated in place. */
  const uint8_t layers = (uint8_t)(enabled_layers & 0x07u);
  PpuSetWidescreenLayerMask(g_ppu, layers);
  PpuSetWidescreenBg3Widen(g_ppu, (layers & 0x04u) != 0 ? 1u : 0u);
  return true;
}

static const uint8_t *Dkc3LevelSourceBank(uint8_t *bank_out) {
  if (bank_out)
    *bank_out = kDkc3LevelMapBank;
  return g_ram + 0x10000;
}

static uint64_t Dkc3LevelSourceSignature(void) {
  const uint64_t bank = kDkc3LevelMapBank;
  const uint64_t map = (uint64_t)Dkc3ReadWram16(kDkc3WramLevelNumber);
  const uint64_t metatiles = Dkc3ReadWram16(kDkc3WramMetatileBase);
  const uint64_t vram = Dkc3TerrainVramBase();
  return bank | (map << 8) | (metatiles << 24) | (vram << 40);
}

static void Dkc3RecordTerrainPrefillTile(int layer,
                                          uint32_t world_tile_x,
                                          uint32_t world_tile_y,
                                          uint16_t expected_entry,
                                          bool margin) {
  uint16_t actual = 0;
  if (!WsShadowLookupWorldTile(
          layer, world_tile_x, world_tile_y, &actual))
    return;
  s_terrain_prefill_stats.present++;
  if (margin)
    s_terrain_prefill_stats.margin_present++;
  {
    /* DKC3_PREFILL_DUMP=<frame counter>: print every cell the world store
     * and the map decode disagree on for that frame, and every margin cell
     * with the store's own view of it (status 1 captured, 2 prefilled). */
    static long dump_frame = -2;
    if (dump_frame == -2) {
      const char *text = getenv("DKC3_PREFILL_DUMP");
      dump_frame = text && *text ? atol(text) : -1;
    }
    if (dump_frame >= 0 && snes_frame_counter == dump_frame &&
        (actual != expected_entry || margin)) {
      uint16_t debug_entry = 0;
      const int status = WsShadowDebugCell(layer, world_tile_x, world_tile_y,
                                           &debug_entry);
      fprintf(stderr,
              "prefill_cell layer=%d x=%u y=%u actual=%04x expected=%04x "
              "margin=%d status=%d debug=%04x\n",
              layer, (unsigned)world_tile_x, (unsigned)world_tile_y, actual,
              expected_entry, margin ? 1 : 0, status, debug_entry);
    }
  }
  if (actual != expected_entry)
    return;
  s_terrain_prefill_stats.matching++;
  if (margin)
    s_terrain_prefill_stats.margin_matching++;
}

/* A margin metatile column that is empty for the whole visible height is a
 * void the console never shows beside a wall (a shaft's far side). An
 * authored window or doorway is empty for a row or two with wall above and
 * below in the same column, and must stay open. */
struct Dkc3MetatileClassifyContext;
static bool Dkc3MetatileColumnIsVoid(struct Dkc3MetatileClassifyContext *ctx,
                                     uint32_t metatile_x,
                                     uint32_t metatile_y, bool east_side,
                                     uint32_t edge_metatile_x);

/* Metatile fill classifier for the structural wall continuation: decodes
 * the sixteen tiles of a 32x32 level-map metatile and tests each character
 * against live VRAM. Results are cached for one prefill pass. */
enum { kDkc3MetatileCacheWidth = 32, kDkc3MetatileCacheHeight = 16 };

typedef struct Dkc3MetatileClassifyContext {
  const uint8_t *bank_data;
  uint16_t map_base;
  uint16_t metatile_base;
  Dkc3VideoLevelLayout layout;
  unsigned row_bytes;             /* row-major maps; 0 = column-major */
  const uint16_t *vram;
  uint16_t character_base;
  uint32_t source_tile_limit_x;
  uint32_t source_tile_limit_y;   /* 0 = no vertical limit */
  uint32_t cache_base_x;
  uint32_t cache_base_y;
  uint8_t cache[kDkc3MetatileCacheHeight][kDkc3MetatileCacheWidth];
  /* Visible metatile rows for the void-column test (inclusive). */
  uint32_t visible_first_y;
  uint32_t visible_last_y;
  uint8_t column_void[kDkc3MetatileCacheWidth]; /* 0 unknown, else 1 + rows empty from the top */
  uint8_t column_wall[kDkc3MetatileCacheWidth]; /* 0 unknown, 1 wall, 2 not */
} Dkc3MetatileClassifyContext;

/* Decode a level tile with the frame's row stride for row-major maps. */
static bool Dkc3DecodeLevelTile(const uint8_t *bank_data, uint16_t map_base,
                                uint16_t metatile_base,
                                Dkc3VideoLevelLayout layout,
                                unsigned row_bytes, uint32_t tile_x,
                                uint32_t tile_y, uint16_t *entry) {
  if (Dkc3VideoLevelLayoutColumnRows(layout) != 0 || row_bytes == 0)
    return Dkc3VideoDecodeLevelTile(bank_data, 0x10000u, map_base,
                                    metatile_base, layout, tile_x, tile_y,
                                    entry);
  return Dkc3VideoDecodeLevelTileRowMajor(bank_data, 0x10000u, map_base,
                                          metatile_base, row_bytes, tile_x,
                                          tile_y, entry);
}

/*
 * Row stride calibration for row-major level maps. The sub-mode's layout
 * gives a default stride, but a stage can run a different column builder
 * than its sub-mode suggests: Bramble $002D (sub-mode $10, the square
 * scroller's 192-byte rows) stores 80 metatiles per row like a ship hold.
 * Decoded with the wrong stride, every margin cell is a tile from another
 * row of the map. Each frame the stride in use is verified against the
 * fully staged native window (32 columns by 28 rows of the ring); when it
 * reproduces fewer than kDkc3RowStrideAcceptPercent of those cells, every
 * candidate stride is tried and the best one above that gate replaces it.
 * With no candidate above the gate the terrain stays unproven for the
 * frame, so the margins are black rather than a wrong row of the map.
 */
enum { kDkc3RowStrideAcceptPercent = 90 };
static const unsigned kDkc3RowStrideCandidates[] = {32u, 64u, 96u, 128u,
                                                    160u, 192u, 224u, 256u};
static uint64_t s_row_stride_signature;
static bool s_row_stride_valid;
static unsigned s_row_bytes;

static unsigned Dkc3RowStrideMatchPercent(
    const uint8_t *bank_data, uint16_t map_base, uint16_t metatile_base,
    int terrain_layer, unsigned row_bytes, uint32_t first_source_tile,
    uint32_t top_source_row, uint32_t ring_top_row) {
  const uint16_t ring_base =
      (uint16_t)PPU_bgTilemapAdr(g_ppu, terrain_layer);
  unsigned matched = 0;
  unsigned total = 0;
  for (uint32_t column = 0; column < 32u; column++) {
    const uint32_t ring_column = (first_source_tile + 32u + column) & 63u;
    for (uint32_t row = 0; row < 28u; row++) {
      uint16_t decoded = 0;
      /* Rows wrap like the prefill's: at the top of a stage the first
       * viewport row is the guard row above the map (tile row 8191) and
       * the rows below it start again at zero. */
      if (!Dkc3VideoDecodeLevelTileRowMajor(
              bank_data, 0x10000u, map_base, metatile_base, row_bytes,
              first_source_tile + column,
              (top_source_row + row) & 0x1fffu, &decoded))
        continue;
      const uint16_t word = (uint16_t)(
          ring_base + ((ring_column & 32u) ? 0x400u : 0u) +
          (((ring_top_row + row) & 31u) << 5) + (ring_column & 31u));
      total++;
      if ((decoded & 0x03ffu) == (g_ppu->vram[word & 0x7fffu] & 0x03ffu))
        matched++;
    }
  }
  return total ? matched * 100u / total : 0u;
}

/* Returns the stride to decode with, or 0 when none reproduces the native
 * window. `percent` receives the match of the stride returned (or of the
 * default when none passes). */
static unsigned Dkc3CalibrateRowStride(
    const uint8_t *bank_data, uint16_t map_base, uint16_t metatile_base,
    Dkc3VideoLevelLayout layout, int terrain_layer,
    uint32_t first_source_tile, uint32_t top_source_row,
    uint32_t ring_top_row, unsigned *percent) {
  const unsigned default_bytes = Dkc3VideoLevelLayoutRowBytes(layout);
  const uint64_t signature = Dkc3LevelSourceSignature();
  if (!s_row_stride_valid || signature != s_row_stride_signature ||
      s_row_bytes == 0u) {
    s_row_stride_signature = signature;
    s_row_stride_valid = true;
    s_row_bytes = default_bytes;
  }
  if (default_bytes == 0u) {
    *percent = 0u;
    return 0u;
  }
  unsigned current = Dkc3RowStrideMatchPercent(
      bank_data, map_base, metatile_base, terrain_layer, s_row_bytes,
      first_source_tile, top_source_row, ring_top_row);
  if (current >= (unsigned)kDkc3RowStrideAcceptPercent) {
    *percent = current;
    return s_row_bytes;
  }
  unsigned best_bytes = 0u;
  unsigned best_percent = 0u;
  for (size_t index = 0;
       index < sizeof kDkc3RowStrideCandidates /
                   sizeof kDkc3RowStrideCandidates[0];
       index++) {
    const unsigned candidate = kDkc3RowStrideCandidates[index];
    const unsigned match = Dkc3RowStrideMatchPercent(
        bank_data, map_base, metatile_base, terrain_layer, candidate,
        first_source_tile, top_source_row, ring_top_row);
    if (match > best_percent) {
      best_percent = match;
      best_bytes = candidate;
    }
  }
  if (best_percent >= (unsigned)kDkc3RowStrideAcceptPercent) {
    s_row_bytes = best_bytes;
    *percent = best_percent;
    return best_bytes;
  }
  *percent = current;
  return 0u;
}

static Dkc3VideoMetatileFill Dkc3ClassifyMetatile(void *context,
                                                  uint32_t metatile_x,
                                                  uint32_t metatile_y) {
  Dkc3MetatileClassifyContext *ctx = (Dkc3MetatileClassifyContext *)context;
  const bool cached =
      metatile_x >= ctx->cache_base_x &&
      metatile_x - ctx->cache_base_x < kDkc3MetatileCacheWidth &&
      metatile_y >= ctx->cache_base_y &&
      metatile_y - ctx->cache_base_y < kDkc3MetatileCacheHeight;
  if (cached) {
    const uint8_t hit = ctx->cache[metatile_y - ctx->cache_base_y]
                                  [metatile_x - ctx->cache_base_x];
    if (hit != 0)
      return (Dkc3VideoMetatileFill)hit;
  }
  Dkc3VideoMetatileFill fill = kDkc3VideoMetatileUnknown;
  const uint32_t tile_x0 = metatile_x * 4u;
  const uint32_t tile_y0 = metatile_y * 4u;
  if (tile_x0 + 4u <= ctx->source_tile_limit_x &&
      (ctx->source_tile_limit_y == 0 ||
       tile_y0 + 4u <= ctx->source_tile_limit_y)) {
    int transparent = 0, decoded = 0;
    for (uint32_t j = 0; j < 4u; j++) {
      for (uint32_t i = 0; i < 4u; i++) {
        uint16_t entry = 0;
        if (!Dkc3DecodeLevelTile(ctx->bank_data, ctx->map_base,
                                 ctx->metatile_base, ctx->layout,
                                 ctx->row_bytes, tile_x0 + i, tile_y0 + j,
                                 &entry))
          continue;
        decoded++;
        if (Dkc3VideoCharacterIsTransparent(ctx->vram, 0x8000u,
                                            ctx->character_base, entry))
          transparent++;
      }
    }
    if (decoded == 16) {
      fill = transparent == 16 ? kDkc3VideoMetatileEmpty
             : transparent == 0 ? kDkc3VideoMetatileFull
                                : kDkc3VideoMetatilePartial;
    }
  }
  if (cached)
    ctx->cache[metatile_y - ctx->cache_base_y]
              [metatile_x - ctx->cache_base_x] = (uint8_t)fill;
  return fill;
}

static bool Dkc3MetatileColumnIsVoid(struct Dkc3MetatileClassifyContext *ctx,
                                     uint32_t metatile_x,
                                     uint32_t metatile_y, bool east_side,
                                     uint32_t edge_metatile_x);

/* Continuing a wall by copying its edge column repeats whatever stands in
 * that column once per margin column: the mine's panel of red lamps beside
 * the Kongs appeared three times in a row. The level map knows what
 * belongs beside each of its metatiles, because the same lamp panel sits at
 * the edge of ten other shafts with the level's rock fill to its east. A
 * continued cell therefore takes the metatile the map most often places
 * beside the previous one on the outward side (the first such metatile
 * that is fully populated), column by column away from the wall, which
 * reproduces the level's own fill sequences. Successors and per-id fills
 * are cached per level and recounted every kDkc3MetatileCacheRefresh
 * frames, since a level can decompress a new map section into the same
 * bank addresses. */
enum { kDkc3MetatileCacheRefresh = 256 };
enum { kDkc3SuccessorUnknown = 0xffffu, kDkc3SuccessorNone = 0u };
static uint16_t s_metatile_successor[2][0x4000];
static uint8_t s_metatile_id_fill[0x4000];   /* Dkc3VideoMetatileFill, 0 unknown */
static uint64_t s_metatile_cache_signature;
static uint32_t s_metatile_cache_frame;
static bool s_metatile_cache_valid;

static void Dkc3RefreshMetatileCaches(uint32_t frame) {
  const uint64_t signature = Dkc3LevelSourceSignature();
  if (s_metatile_cache_valid && s_metatile_cache_signature == signature &&
      frame - s_metatile_cache_frame < (uint32_t)kDkc3MetatileCacheRefresh)
    return;
  memset(s_metatile_successor, 0xff, sizeof s_metatile_successor);
  memset(s_metatile_id_fill, 0, sizeof s_metatile_id_fill);
  s_metatile_cache_signature = signature;
  s_metatile_cache_frame = frame;
  s_metatile_cache_valid = true;
}

/* The fill of a metatile definition by id, independent of any map cell. */
static Dkc3VideoMetatileFill Dkc3MetatileIdFill(
    struct Dkc3MetatileClassifyContext *ctx, uint16_t id) {
  id &= 0x3fffu;
  if (s_metatile_id_fill[id] != 0)
    return (Dkc3VideoMetatileFill)s_metatile_id_fill[id];
  int transparent = 0;
  for (unsigned j = 0; j < 4u; j++) {
    for (unsigned i = 0; i < 4u; i++) {
      uint16_t entry = 0;
      if (!Dkc3VideoDecodeMetatileEntry(ctx->bank_data, 0x10000u,
                                        ctx->metatile_base, id, i, j, &entry))
        return kDkc3VideoMetatileUnknown;
      if (Dkc3VideoCharacterIsTransparent(ctx->vram, 0x8000u,
                                          ctx->character_base, entry))
        transparent++;
    }
  }
  const Dkc3VideoMetatileFill fill =
      transparent == 16 ? kDkc3VideoMetatileEmpty
      : transparent == 0 ? kDkc3VideoMetatileFull : kDkc3VideoMetatilePartial;
  s_metatile_id_fill[id] = (uint8_t)fill;
  return fill;
}

/* The fully populated metatile the map most often places beside `id` on
 * the outward side; kDkc3SuccessorNone when the map never continues it. */
static uint16_t Dkc3MetatileSuccessor(struct Dkc3MetatileClassifyContext *ctx,
                                      uint16_t id, bool east_side) {
  uint16_t *slot = &s_metatile_successor[east_side ? 1 : 0][id & 0x3fffu];
  if (*slot != (uint16_t)kDkc3SuccessorUnknown)
    return *slot;
  uint16_t ids[8], counts[8];
  const unsigned found = Dkc3VideoMetatileNeighbours(
      ctx->bank_data, 0x10000u, ctx->map_base, ctx->metatile_base,
      ctx->layout, ctx->row_bytes, (uint16_t)(id & 0x3fffu), east_side, ids,
      counts, 8u);
  uint16_t chosen = (uint16_t)kDkc3SuccessorNone;
  for (unsigned n = 0; n < found; n++) {
    if (Dkc3MetatileIdFill(ctx, ids[n]) == kDkc3VideoMetatileFull) {
      chosen = ids[n];
      break;
    }
  }
  *slot = chosen;
  return chosen;
}

/* The metatile for a continued cell `steps` columns beyond the wall's edge
 * cell at (edge_x, row): the map's successor chain from that cell's
 * metatile. A wall row the map never continues (the lamp panel's unique
 * upper half) starts its chain from the nearest wall row above or below
 * that the map does continue, so the panel is not repeated. Returns false
 * when no row of the wall offers a chain; the caller then copies the edge
 * column as before. */
enum { kDkc3WallChainReach = 4 };
static bool Dkc3WallChainMetatile(struct Dkc3MetatileClassifyContext *ctx,
                                  uint32_t edge_x, uint32_t row,
                                  bool east_side, uint32_t steps,
                                  uint16_t *metatile) {
  uint16_t start = 0;
  if (!Dkc3VideoReadLevelMetatile(ctx->bank_data, 0x10000u, ctx->map_base,
                                  ctx->layout, ctx->row_bytes, edge_x, row,
                                  &start))
    return false;
  uint16_t id = Dkc3MetatileSuccessor(ctx, start, east_side);
  if (id == (uint16_t)kDkc3SuccessorNone) {
    bool above_open = true, below_open = true;
    for (uint32_t d = 1; d <= (uint32_t)kDkc3WallChainReach &&
                         id == (uint16_t)kDkc3SuccessorNone; d++) {
      const uint32_t candidates[2] = {row - d, row + d};
      for (unsigned k = 0; k < 2u && id == (uint16_t)kDkc3SuccessorNone; k++) {
        bool *open = k == 0 ? &above_open : &below_open;
        if (!*open || (k == 0 && row < d))
          continue;
        const uint32_t r = candidates[k];
        uint16_t other = 0;
        if (Dkc3ClassifyMetatile(ctx, edge_x, r) != kDkc3VideoMetatileFull ||
            !Dkc3VideoReadLevelMetatile(ctx->bank_data, 0x10000u,
                                        ctx->map_base, ctx->layout,
                                        ctx->row_bytes, edge_x, r, &other)) {
          *open = false;
          continue;
        }
        id = Dkc3MetatileSuccessor(ctx, other, east_side);
      }
    }
    if (id == (uint16_t)kDkc3SuccessorNone)
      return false;
  }
  for (uint32_t step = 1; step < steps; step++) {
    const uint16_t next = Dkc3MetatileSuccessor(ctx, id, east_side);
    if (next == (uint16_t)kDkc3SuccessorNone)
      break;   /* the chain ends: the last metatile carries on */
    id = next;
  }
  *metatile = id;
  return true;
}

/* A void margin metatile column is a player-held wall when the structural
 * rule continues it on at least one visible row: the wall is proven there,
 * and the rows it fails closed on (a partial edge metatile, the boundary of
 * a cave pocket the console cuts at its screen edge) may mirror the
 * authored terrain across the wall line instead of opening the pocket
 * further than the console ever shows it. Cached per column per pass. */
static bool Dkc3MetatileColumnIsHeldWall(
    struct Dkc3MetatileClassifyContext *ctx, bool east_side,
    uint32_t metatile_x, uint32_t edge_metatile_x) {
  const bool cached = metatile_x >= ctx->cache_base_x &&
                      metatile_x - ctx->cache_base_x < kDkc3MetatileCacheWidth;
  if (cached && ctx->column_wall[metatile_x - ctx->cache_base_x] != 0)
    return ctx->column_wall[metatile_x - ctx->cache_base_x] == 1;
  bool wall = false;
  for (uint32_t my = ctx->visible_first_y; my <= ctx->visible_last_y; my++) {
    uint32_t source = 0;
    if (!Dkc3MetatileColumnIsVoid(ctx, metatile_x, my, east_side,
                                  edge_metatile_x))
      break;
    if (Dkc3VideoFindStructuralWallSource(Dkc3ClassifyMetatile, ctx,
                                          east_side, metatile_x,
                                          edge_metatile_x, my, &source)) {
      wall = true;
      break;
    }
  }
  if (cached)
    ctx->column_wall[metatile_x - ctx->cache_base_x] = wall ? 1 : 2;
  return wall;
}

/* The margin column is open beside a player-held wall at `metatile_y`:
 * empty from the visible top down through the row, whether that spans the
 * whole visible height (the crystal shaft) or ends on a floor that
 * continues past the wall (the mine at camera 256, whose neighbouring room
 * showed its backdrop through the void above that floor), or part of an
 * empty run at least kDkc3WallVoidRunRows metatiles tall that a wall seals
 * from the view on every row of the run (the unauthored gap between two
 * mine shafts, thirteen rows of void under a ceiling of authored rock that
 * the camera scrolls into view first). A porthole or a doorway is one or
 * two rows tall with wall above it and fails both tests, and a flooded
 * hold's water beyond the view fails the second: its run opens into the
 * view on the rows above the crate the rule would otherwise continue into
 * it. The cache keeps the first non-empty row of the column. */
enum { kDkc3WallVoidRunRows = 4, kDkc3WallVoidRunReach = 16 };
static bool Dkc3MetatileRowSealed(struct Dkc3MetatileClassifyContext *ctx,
                                  uint32_t metatile_x, uint32_t metatile_y,
                                  bool east_side, uint32_t edge_metatile_x) {
  /* Walking from the margin column toward the view, a non-empty cell must
   * come no deeper than one column inside the cartridge's edge column: a
   * cave pocket's boundary row has an empty edge cell in front of its
   * partial one, open water has empty cells all the way in. */
  const uint32_t deepest = east_side
                               ? (edge_metatile_x > 0 ? edge_metatile_x - 1u : 0u)
                               : edge_metatile_x + 1u;
  uint32_t c = metatile_x;
  for (;;) {
    if (east_side) {
      if (c == 0 || c <= deepest)
        return false;
      c--;
    } else {
      if (c >= deepest)
        return false;
      c++;
    }
    if (Dkc3ClassifyMetatile(ctx, c, metatile_y) != kDkc3VideoMetatileEmpty)
      return true;
  }
}

static bool Dkc3MetatileColumnVoidRun(struct Dkc3MetatileClassifyContext *ctx,
                                      uint32_t metatile_x,
                                      uint32_t metatile_y, bool east_side,
                                      uint32_t edge_metatile_x) {
  if (Dkc3ClassifyMetatile(ctx, metatile_x, metatile_y) !=
      kDkc3VideoMetatileEmpty)
    return false;
  uint32_t top = metatile_y, bottom = metatile_y;
  for (uint32_t d = 1; d <= (uint32_t)kDkc3WallVoidRunReach && d <= metatile_y;
       d++) {
    if (Dkc3ClassifyMetatile(ctx, metatile_x, metatile_y - d) !=
        kDkc3VideoMetatileEmpty)
      break;
    top = metatile_y - d;
  }
  for (uint32_t d = 1; d <= (uint32_t)kDkc3WallVoidRunReach; d++) {
    if (Dkc3ClassifyMetatile(ctx, metatile_x, metatile_y + d) !=
        kDkc3VideoMetatileEmpty)
      break;
    bottom = metatile_y + d;
  }
  if (bottom - top + 1u < (uint32_t)kDkc3WallVoidRunRows)
    return false;
  for (uint32_t r = top; r <= bottom; r++) {
    if (!Dkc3MetatileRowSealed(ctx, metatile_x, r, east_side,
                               edge_metatile_x))
      return false;
  }
  return true;
}

static bool Dkc3MetatileColumnIsVoid(struct Dkc3MetatileClassifyContext *ctx,
                                     uint32_t metatile_x,
                                     uint32_t metatile_y, bool east_side,
                                     uint32_t edge_metatile_x) {
  const bool cached = metatile_x >= ctx->cache_base_x &&
                      metatile_x - ctx->cache_base_x < kDkc3MetatileCacheWidth;
  uint32_t first_solid = ctx->visible_last_y + 1u;
  if (cached && ctx->column_void[metatile_x - ctx->cache_base_x] != 0) {
    first_solid = ctx->visible_first_y +
                  (uint32_t)(ctx->column_void[metatile_x - ctx->cache_base_x] -
                             1u);
  } else {
    for (uint32_t my = ctx->visible_first_y; my <= ctx->visible_last_y;
         my++) {
      if (Dkc3ClassifyMetatile(ctx, metatile_x, my) !=
          kDkc3VideoMetatileEmpty) {
        first_solid = my;
        break;
      }
    }
    if (cached)
      ctx->column_void[metatile_x - ctx->cache_base_x] =
          (uint8_t)(first_solid - ctx->visible_first_y + 1u);
  }
  return metatile_y < first_solid ||
         Dkc3MetatileColumnVoidRun(ctx, metatile_x, metatile_y, east_side,
                                   edge_metatile_x);
}

static bool Dkc3PrefillWidescreenLevelTerrain(uint8_t layer_mask,
                                              int terrain_layer,
                                              Dkc3VideoLevelLayout layout,
                                              uint32_t rendered_x,
                                              uint32_t cartridge_x,
                                              uint32_t camera_y) {
  memset(&s_terrain_prefill_stats, 0, sizeof s_terrain_prefill_stats);
  if (terrain_layer < 0 || terrain_layer >= 2 ||
      layout == kDkc3VideoLevelLayoutUnknown ||
      !(layer_mask & (uint8_t)(1u << terrain_layer)) ||
      PPU_bigTiles(g_ppu, terrain_layer))
    return false;

  uint8_t bank = 0;
  const uint8_t *bank_data = Dkc3LevelSourceBank(&bank);
  if (!bank_data)
    return false;

  const uint16_t map_base = kDkc3LevelMapBase;
  const uint16_t metatile_base = Dkc3ReadWram16(kDkc3WramMetatileBase);
  const uint16_t maximum_scroll_x = Dkc3MaximumScrollX();
  const uint16_t maximum_scroll_y = Dkc3MaximumScrollY();
  uint16_t transparent_tile = 0;
  if (maximum_scroll_x == 0 ||
      !Dkc3VideoFindTransparent4bppTile(
          g_ppu->vram, 0x8000u,
          (uint16_t)PPU_bgTileAdr(g_ppu, terrain_layer),
          &transparent_tile))
    return false;
  const uint32_t extra = (uint32_t)Dkc3VideoExtra();
  /*
   * The rolling column builder stages one complete 32x32 metatile beyond
   * the native camera limit so fine scrolling never exposes an incomplete
   * edge. The following metatile belongs to unrelated WRAM in every case.
   * The presentation clamp keeps the visible margins inside the authored
   * camera extent, so this limit only protects the fine-scroll guard tiles.
   */
  const uint32_t source_tile_limit =
      ((uint32_t)maximum_scroll_x + 0x20u + 7u) >> 3;
  const uint32_t source_tile_limit_y =
      ((uint32_t)maximum_scroll_y + 7u) >> 3;
  /* Keep one decoded tile beyond both host margins. A fine-scroll phase can
   * make the final one or two pixels address the adjacent tile even though
   * the nominal 342-pixel span still ends inside the previous source cell.
   * Without this guard Pirate Panic briefly fell through to the verified
   * blank tile during Rambi's fast down-right camera move (frame 6404). */
  const uint32_t guard = 8u;
  const uint32_t west_extent = extra + guard;
  const uint32_t first_x =
      rendered_x > west_extent ? rendered_x - west_extent : 0;
  const uint32_t last_x =
      rendered_x + (uint32_t)kDkc3VideoNativeWidth - 1u + extra + guard;
  const uint32_t first_tile_x = first_x >> 3;
  const uint32_t last_tile_x = last_x >> 3;
  const uint32_t ppu_scroll_y = s_terrain_phase_v & 0x03ffu;
  s_terrain_prefill_stats.phase_h = s_terrain_phase_h;
  s_terrain_prefill_stats.phase_v = s_terrain_phase_v;
  s_terrain_prefill_stats.phase_from_band = s_terrain_phase_from_band ? 1u : 0u;
  const uint32_t fine_y = ppu_scroll_y & 7u;
  const int visible_tile_rows =
      (int)(((uint32_t)kDkc3VideoHeight + fine_y + 7u) >> 3);
  /*
   * Reproduce the cartridge column builder's vertical rotation rather
   * than assuming WRAM camera Y is the already-latched PPU row.
   * $B5:ACC0-$B5:ACCF starts $0100 pixels above the camera;
   * $B5:ADA9-$B5:ADD0 then rotates those 36 source entries into the
   * 32-row rolling tilemap. The rendered PPU phase can trail the next
   * WRAM camera value by one pixel at an NMI boundary, so both the source
   * row and shadow key must derive from that same PPU phase. Mixing the
   * two phases turns an 8-pixel boundary into a transient +31-row wrap,
   * and PrefillTile then preserves the bad margin entry indefinitely.
   *
   * Decode one guard row above and below the viewport as well: an HDMA
   * band that shares the terrain phase may lead the frame anchor by up to
   * four pixels vertically, and its margin lookups must not fall through.
   */
  const uint32_t top_shadow_row =
      Dkc3VideoLevelSourceTileY((uint16_t)ppu_scroll_y, camera_y, 0);
  const uint32_t top_source_row =
      Dkc3VideoLevelMapTileY((uint16_t)ppu_scroll_y, camera_y, 0);
  s_terrain_prefill_stats.top_shadow_row = top_shadow_row;
  s_terrain_prefill_stats.top_source_row = top_source_row;
  size_t decoded = 0;
  size_t expected = 0;
  unsigned row_bytes = 0;
  if (Dkc3VideoLevelLayoutColumnRows(layout) == 0) {
    unsigned percent = 0;
    row_bytes = Dkc3CalibrateRowStride(
        bank_data, map_base, metatile_base, layout, terrain_layer,
        cartridge_x >= 0x0100u ? (cartridge_x - 0x0100u) >> 3 : 0u,
        top_source_row, (uint32_t)(ppu_scroll_y >> 3), &percent);
    s_terrain_prefill_stats.row_bytes = (uint16_t)row_bytes;
    s_terrain_prefill_stats.row_match_percent = (uint8_t)percent;
    if (row_bytes == 0u)
      return false;
  }
  Dkc3MetatileClassifyContext classify;
  memset(&classify, 0, sizeof classify);
  classify.bank_data = bank_data;
  classify.map_base = map_base;
  classify.metatile_base = metatile_base;
  classify.layout = layout;
  classify.row_bytes = row_bytes;
  classify.vram = g_ppu->vram;
  classify.character_base = (uint16_t)PPU_bgTileAdr(g_ppu, terrain_layer);
  classify.source_tile_limit_x = source_tile_limit;
  classify.source_tile_limit_y =
      (layout == kDkc3VideoLevelLayoutVertical ||
       layout == kDkc3VideoLevelLayoutSquare ||
       layout == kDkc3VideoLevelLayoutNarrowVertical)
          ? source_tile_limit_y : 0u;
  classify.cache_base_x =
      first_tile_x >= 32u + 8u ? (first_tile_x - 32u - 8u) >> 2 : 0u;
  classify.cache_base_y = top_source_row >= 8u ? (top_source_row - 8u) >> 2
                                               : 0u;
  classify.visible_first_y = top_source_row >> 2;
  classify.visible_last_y =
      (top_source_row + (uint32_t)visible_tile_rows - 1u) >> 2;
  /* The cartridge's own window in decoded-map tile space: structural
   * continuation reaches from a margin cell toward this edge. */
  const uint32_t cartridge_first_tile = cartridge_x >> 3;
  const uint32_t cartridge_last_tile =
      (cartridge_x + (uint32_t)kDkc3VideoNativeWidth - 1u) >> 3;
  /* DKC3_TERRAIN_FILL_MAP=1: print the classifier's metatile fill map for
   * the prefill window ('.' empty, '+' partial, '#' full, '?' undecoded),
   * eight columns past it on each side and two rows above and below the
   * visible rows. The column the cartridge's window starts in is marked
   * with '|' before it. Reading this map is far quicker than reasoning
   * about a wall rule from screenshots. */
  Dkc3RefreshMetatileCaches(s_plane_frame);
  /* DKC3_TERRAIN_FILL_MAP=2 adds each cell's metatile id, followed by '>'
   * when the map never places a fully populated metatile east of it and
   * '<' for west ('*' for neither). */
  if (getenv("DKC3_TERRAIN_FILL_MAP")) {
    const bool with_ids = getenv("DKC3_TERRAIN_FILL_MAP")[0] == '2';
    const uint32_t first_mx = first_tile_x >= 32u + 32u
                                  ? (first_tile_x - 32u - 32u) >> 2 : 0u;
    const uint32_t last_mx = (last_tile_x - 32u + 32u) >> 2;
    const uint32_t first_my =
        classify.visible_first_y >= 2u ? classify.visible_first_y - 2u : 0u;
    fprintf(stderr, "terrain fill map: metatile cols %u..%u, rows %u..%u, "
            "cartridge cols %u..%u\n", first_mx, last_mx, first_my,
            classify.visible_last_y + 2u, cartridge_first_tile >= 32u
                ? (cartridge_first_tile - 32u) >> 2 : 0u,
            cartridge_last_tile >= 32u ? (cartridge_last_tile - 32u) >> 2 : 0u);
    for (uint32_t my = first_my; my <= classify.visible_last_y + 2u; my++) {
      fprintf(stderr, "  row %4u: ", my);
      for (uint32_t mx = first_mx; mx <= last_mx; mx++) {
        const Dkc3VideoMetatileFill fill =
            Dkc3ClassifyMetatile(&classify, mx, my);
        if (cartridge_first_tile >= 32u &&
            mx == ((cartridge_first_tile - 32u) >> 2))
          fputc('|', stderr);
        if (cartridge_last_tile >= 32u &&
            mx == ((cartridge_last_tile - 32u) >> 2) + 1u)
          fputc('|', stderr);
        fputc(fill == kDkc3VideoMetatileEmpty ? '.'
              : fill == kDkc3VideoMetatilePartial ? '+'
              : fill == kDkc3VideoMetatileFull ? '#' : '?', stderr);
      }
      if (with_ids) {
        fputs("   ", stderr);
        for (uint32_t mx = first_mx; mx <= last_mx; mx++) {
          uint16_t id = 0;
          if (Dkc3VideoReadLevelMetatile(classify.bank_data, 0x10000u,
                                         classify.map_base, classify.layout,
                                         classify.row_bytes, mx, my, &id))
          {
            const bool east_ok =
                id == 0 || Dkc3MetatileSuccessor(&classify, id, true) !=
                               (uint16_t)kDkc3SuccessorNone;
            const bool west_ok =
                id == 0 || Dkc3MetatileSuccessor(&classify, id, false) !=
                               (uint16_t)kDkc3SuccessorNone;
            fprintf(stderr, " %03x%c", id,
                    !east_ok && !west_ok ? '*' : !east_ok ? '>'
                    : !west_ok ? '<' : ' ');
          }
          else
            fputs(" ??? ", stderr);
        }
      }
      fputc('\n', stderr);
    }
  }
  for (uint32_t tile_x = first_tile_x; tile_x <= last_tile_x; tile_x++) {
    for (int row = -1; row <= visible_tile_rows; row++) {
      uint16_t entry = 0;
      const bool margin =
          Dkc3VideoTileTouchesWidescreenMargin(tile_x, rendered_x);
      /* Outside the cartridge's authentic window, whatever the presentation
       * bias placed on screen: only these cells may be continued from a
       * wall, and every one of them takes the decoded map over live history.
       * The columns a bias moves into a margin are still inside that window
       * and keep their captured ring content (so the 4:3 oracle stays exact
       * even on an unstaged guard row). The columns a bias slides into view
       * used to keep whatever history they had, but that history can be a
       * misattributed capture: in a vertical stage the cartridge rewrites
       * the ring's other page with the same stale 32 entries on every row
       * upload, and a one-pixel leftward camera jitter is enough for the
       * store to file those writes under the chunk the strip shows. The
       * crow's-nest art then sat on the mast at the right wall until the
       * player left the stage. The decode is exact for static terrain and
       * ForceTile still yields to a game write from the last frame. */
      const bool outside_cartridge =
          Dkc3VideoTileTouchesWidescreenMargin(tile_x, cartridge_x);
      const bool force_decoded = outside_cartridge;
      const uint32_t shadow_tile_y =
          (uint32_t)((int64_t)top_shadow_row + row);
      const uint32_t source_tile_y =
          (uint32_t)((int64_t)top_source_row + row) & 0x1fffu;
      /* Set when a continued cell's entry came from the map's own
       * adjacency rather than a map cell. */
      bool entry_ready = false;
      expected++;
      if (margin)
        s_terrain_prefill_stats.margin_expected++;
      /*
       * DKC3's camera/object coordinate system starts one 256-pixel page
       * after the decompressed level map. This is the same relationship made
       * explicit by $B5:ACA8-$B5:ACB7 (source column) and
       * $B5:ADF0-$B5:AE01 (rolling-VRAM destination): while moving right, a
       * source column at X is uploaded to the VRAM column for X+$0100.
       *
       * A matching frame-5499 WRAM/VRAM calibration confirms the mapping:
       * source tile (shadow key - 32) agrees with 1,754/2,048 live BG1 cells
       * (85.6%); the next-best tested offset agrees with only 746/2,048.
       * Remaining differences are expected dynamic/partially staged cells.
       *
       * No authored terrain exists west of that origin or beyond the last
       * camera position plus the viewport. The edge policy decides whether
       * such a column mirrors the nearest authored columns or stays
       * verified transparent.
       */
      uint32_t source_tile_x = 0;
      bool mirror_horizontally = false;
      const int edge = Dkc3VideoResolveEdgeTile(
          tile_x, maximum_scroll_x, &source_tile_x, &mirror_horizontally);
      /*
       * $0AFC is the camera's maximum horizontal scroll after the cartridge
       * subtracts the 256-pixel native viewport ($B5:E36C-$B5:E373).
       * Adding the streamer's one 32-pixel guard metatile gives the exclusive
       * safe source width. Reading the following metatile crosses into
       * unrelated WRAM; this was the colorful far-right stripe in the
       * frame-9000 capture. Outside that guard, use a verified transparent
       * character so lower layers remain visible without inventing or
       * repeating terrain.
       */
      if (edge < 0 || source_tile_x >= source_tile_limit) {
        WsShadowForceTile(
            terrain_layer, tile_x, shadow_tile_y, transparent_tile);
        decoded++;
        Dkc3RecordTerrainPrefillTile(
            terrain_layer, tile_x, shadow_tile_y, transparent_tile, margin);
        continue;
      }
      if ((layout == kDkc3VideoLevelLayoutVertical ||
           layout == kDkc3VideoLevelLayoutSquare ||
           layout == kDkc3VideoLevelLayoutNarrowVertical) &&
          (maximum_scroll_y == 0 ||
           source_tile_y >= source_tile_limit_y)) {
        WsShadowForceTile(
            terrain_layer, tile_x, shadow_tile_y, transparent_tile);
        decoded++;
        Dkc3RecordTerrainPrefillTile(
            terrain_layer, tile_x, shadow_tile_y, transparent_tile, margin);
        continue;
      }
      /* The wall rules below invent what the console never shows beside a
       * wall. A column the camera can still scroll the native window over
       * is not that: the console draws the decoded map there once the
       * camera arrives. In the waterfall level the empty gap between two
       * tree trunks sat one metatile past the window whenever the camera
       * stood on a tile boundary, and the rule filled it with the trunk's
       * successors (rock and grass that popped in at the right margin
       * and vanished as soon as the camera moved a pixel). */
      const bool in_reach = Dkc3VideoTileInCameraReach(
          tile_x, s_reach_minimum_x, s_reach_maximum_x);
      if (edge == 0 && outside_cartridge && !in_reach &&
          cartridge_first_tile >= 32u) {
        const bool east =
            ((uint64_t)tile_x << 3) >= (uint64_t)cartridge_x +
                                           kDkc3VideoNativeWidth;
        const uint32_t edge_source_tile =
            (east ? cartridge_last_tile : cartridge_first_tile) - 32u;
        uint32_t source_metatile_x = 0;
        uint32_t mirrored_tile_x = 0;
        if (Dkc3MetatileColumnIsVoid(&classify, source_tile_x >> 2,
                                     source_tile_y >> 2, east,
                                     edge_source_tile >> 2) &&
            Dkc3VideoFindStructuralWallSource(
                Dkc3ClassifyMetatile, &classify, east, source_tile_x >> 2,
                edge_source_tile >> 2, source_tile_y >> 2,
                &source_metatile_x)) {
          const uint32_t target_metatile_x = source_tile_x >> 2;
          const uint32_t steps = east ? target_metatile_x - source_metatile_x
                                      : source_metatile_x - target_metatile_x;
          uint16_t chained = 0;
          if (Dkc3WallChainMetatile(&classify, source_metatile_x,
                                    source_tile_y >> 2, east, steps,
                                    &chained) &&
              Dkc3VideoDecodeMetatileEntry(bank_data, 0x10000u, metatile_base,
                                           chained, source_tile_x & 3u,
                                           source_tile_y & 3u, &entry)) {
            entry_ready = true;
            s_terrain_prefill_stats.chained++;
          } else {
            source_tile_x = source_metatile_x * 4u + (source_tile_x & 3u);
          }
          s_terrain_prefill_stats.structural++;
        } else if (Dkc3MetatileColumnIsVoid(&classify, source_tile_x >> 2,
                                            source_tile_y >> 2, east,
                                            edge_source_tile >> 2) &&
                   Dkc3MetatileColumnIsHeldWall(
                       &classify, east, source_tile_x >> 2,
                       edge_source_tile >> 2) &&
                   Dkc3VideoMirrorSourceTileAcrossEdge(
                       source_tile_x, edge_source_tile, east,
                       &mirrored_tile_x) &&
                   mirrored_tile_x < source_tile_limit) {
          /* The rows the structural rule fails closed on: mirror the
           * authored terrain across the held wall, as the reflect policy
           * does at a level's own wall. */
          source_tile_x = mirrored_tile_x;
          mirror_horizontally = !mirror_horizontally;
          s_terrain_prefill_stats.mirrored++;
        }
      }
      if (!entry_ready &&
          !Dkc3DecodeLevelTile(bank_data, map_base, metatile_base, layout,
                               row_bytes, source_tile_x, source_tile_y,
                               &entry))
        continue;
      if (mirror_horizontally)
        entry ^= 0x4000u;
      /* DKC2Recomp blanked the first visible row's margin cells here: at
       * its vertical page boundary that row came from the live ring, not
       * from a decoded source row. DKC3's column builder rotates 32 map
       * rows from the camera's page into the ring, so every visible row
       * decodes like the rest. Blanking it here made the top eight pixels
       * of both margins vanish whenever the vertical scroll sat on a tile
       * boundary, a strip that flashed as the camera drifted vertically
       * (Riverside Race's forest, top right). */
      /*
       * An older captured VRAM/DMA-pad tile can survive in a world cell that
       * the verified level map says is transparent. That produced the stray
       * deck fragments in Pirate Panic's upper-left margin. Clear only those
       * verified void cells every frame; non-transparent tiles retain real
       * history so dynamic ship tilemap details are not erased by a static
       * source reconstruction.
       */
      if (Dkc3VideoIsTransparentTileEntry(entry, transparent_tile) ||
          force_decoded)
        WsShadowForceTile(terrain_layer, tile_x, shadow_tile_y, entry);
      else
        WsShadowPrefillTile(terrain_layer, tile_x, shadow_tile_y, entry);
      decoded++;
      Dkc3RecordTerrainPrefillTile(
          terrain_layer, tile_x, shadow_tile_y, entry, margin);
    }
  }
  /* A second layer's alias bands read the terrain store at their verified
   * row offset, so the source rows for every column the widened view can
   * reach must be decoded too; the main loop covers only the viewport's own
   * rows. Margin cells take the decode; native-window cells keep any real
   * capture (PrefillTile never overwrites one). */
  for (int layer = 0; layer < 2; layer++) {
    const int alias_rows = s_alias_offset_rows[layer];
    if (layer == terrain_layer || alias_rows == 0)
      continue;
    for (uint32_t tile_x = first_tile_x; tile_x <= last_tile_x; tile_x++) {
      uint32_t source_tile_x = 0;
      bool mirror_horizontally = false;
      const int edge = Dkc3VideoResolveEdgeTile(
          tile_x, maximum_scroll_x, &source_tile_x, &mirror_horizontally);
      if (edge < 0 || source_tile_x >= source_tile_limit)
        continue;
      const bool outside_cartridge =
          Dkc3VideoTileTouchesWidescreenMargin(tile_x, cartridge_x);
      for (int row = -1; row <= visible_tile_rows; row++) {
        const int64_t shadow_row =
            (int64_t)top_shadow_row + row + alias_rows;
        const int64_t source_row =
            (int64_t)top_source_row + row + alias_rows;
        if (shadow_row < 0 || source_row < 0)
          continue;
        uint16_t entry = 0;
        if (!Dkc3DecodeLevelTile(bank_data, map_base, metatile_base, layout,
                                 row_bytes, source_tile_x,
                                 (uint32_t)source_row & 0x1fffu, &entry))
          continue;
        if (mirror_horizontally)
          entry ^= 0x4000u;
        if (outside_cartridge)
          WsShadowForceTile(terrain_layer, tile_x, (uint32_t)shadow_row,
                            entry);
        else
          WsShadowPrefillTile(terrain_layer, tile_x, (uint32_t)shadow_row,
                              entry);
      }
    }
  }
  /* The west hold for the glide, read for the next frame. It is entered
   * when the columns the unbiased margin would reach beside the window are
   * empty for the whole visible height, the camera has not moved since the
   * last frame, and the player stands within kDkc3HoldPlayerEdge pixels of
   * the frame's west edge: pinned at the authored world's edge. It persists
   * while the void stays beside the window, whatever the camera does, so
   * the glide releases the slide with travel as at any wall; the bound is
   * the window's first column at entry. */
  {
    uint32_t hold_column = 0;
    const unsigned reach = ((unsigned)Dkc3VideoExtra() + 31u) / 32u + 1u;
    const bool void_beside =
        cartridge_first_tile >= 32u &&
        Dkc3VideoHoldWest(Dkc3ClassifyMetatile, &classify,
                          (cartridge_first_tile - 32u) >> 2, reach,
                          classify.visible_first_y, classify.visible_last_y,
                          &hold_column);
    if (s_hold_west_valid) {
      s_hold_west_valid = void_beside;
    } else if (void_beside) {
      const uint32_t player_x = Dkc3PlayerX();
      const bool pinned =
          s_hold_camera_valid && s_hold_camera_x == cartridge_x &&
          player_x >= cartridge_x &&
          player_x - cartridge_x < (uint32_t)kDkc3HoldPlayerEdge;
      if (pinned || s_hold_start_frames > 0) {
        s_hold_west_valid = true;
        s_hold_west_x = (hold_column << 5) + 0x100u;
      }
    }
    if (s_hold_start_frames > 0)
      s_hold_start_frames--;
    s_hold_camera_x = cartridge_x;
    s_hold_camera_valid = true;
  }
  s_terrain_prefill_stats.expected = expected;
  s_terrain_prefill_stats.decoded = decoded;
  (void)bank;
  return expected != 0 && decoded == expected;
}

/* Decode the waterfall's streamed columns at their world positions. The
 * native viewport remains authoritative; only the host store is filled. */
static void Dkc3PrefillWaterfall(int presentation_bias) {
  if (!s_waterfall_scene)
    return;
  const uint8_t *bank = RomPtr(0xf30000);
  const uint16_t pointer = Dkc3ReadWram16(0x15e4);
  uint16_t blank = 0;
  Dkc3VideoFindTransparent4bppTile(g_ppu->vram, 0x8000u,
                                   (uint16_t)PPU_bgTileAdr(g_ppu, 1), &blank);
  const int origin = (int)s_waterfall_world_x + presentation_bias;
  int first = (origin - Dkc3VideoExtra()) / 8 - 1;
  if (first < 0) first = 0;
  const int end = (origin + 256 + Dkc3VideoExtra() + 7) / 8 + 1;
  for (int col = first; col < end; col++) {
    uint16_t entries[32];
    const bool decoded = s_waterfall_verified &&
        Dkc3VideoDecodeWaterfallColumn(bank, 0x10000u, pointer,
                                         (uint32_t)col, entries);
    /* HDMA animates the 10-bit vertical phase. The column DMA contains a
     * full 32-row hardware wrap, so seed all four epochs the PPU can use. */
    for (unsigned row = 0; row < 128; row++)
      WsShadowForceTile(1, (uint32_t)col, row,
                        decoded ? entries[row & 31u] : blank);
  }
}

/* Register the terrain owner's world-keyed store (and, when another physical
 * 64-column layer displays the same world map in some HDMA band, that layer
 * as a read-only view of the owner's store), capture the owner's native
 * viewport, and decode the level map into every cell a host margin can
 * sample. Returns whether exact terrain is available for this frame. */
static bool Dkc3PrepareWidescreenShadow(uint8_t layer_mask,
                                        int terrain_layer,
                                        Dkc3VideoLevelLayout layout,
                                        int presentation_bias,
                                        const bool alias_layer[2]) {
  const uint32_t camera_x = Dkc3ReadWram16(kDkc3WramCameraX);
  const uint32_t camera_y = Dkc3ReadWram16(kDkc3WramCameraY);
  const uint64_t source_signature = Dkc3LevelSourceSignature();

  if (!s_widescreen_shadow_active) {
    WsShadowReset();
    s_widescreen_shadow_active = true;
  }
  if (!s_widescreen_source_valid ||
      source_signature != s_widescreen_source_signature) {
    WsShadowReset();
    s_widescreen_source_signature = source_signature;
    s_widescreen_source_valid = true;
    s_hold_west_valid = false;
    s_hold_camera_valid = false;
    s_bias_valid = false;
    s_hold_start_frames =
        s_state_loaded_recently ? 0u : (uint32_t)kDkc3HoldStartFrames;
    s_state_loaded_recently = false;
  }

  uint32_t owner_world_x = camera_x;
  uint32_t owner_world_y = camera_y;
  uint32_t owner_scroll_x = 0;
  uint32_t owner_scroll_y = 0;
  const bool have_owner =
      terrain_layer >= 0 && terrain_layer < 2 &&
      (layer_mask & (uint8_t)(1u << terrain_layer)) != 0;
  if (have_owner) {
    /*
     * DKC3's rolling VRAM address is not its world coordinate. The layer
     * selected by live stream destination $17B6 uses the full WRAM camera
     * for X, but its vertical column buffer is staged one 256-pixel page
     * above camera Y. Key Y by the rendered PPU source phase so native
     * viewport captures, later VRAM writes, and exact prefills all address
     * the same terrain rows. Use the PPU-latched horizontal phase for both
     * the native viewport and widened margins: the WRAM camera can lead
     * hScroll by 1-3 pixels while DKC3 changes direction, and keying margins
     * from that newer value made the old 4:3 edge visibly split.
     */
    owner_scroll_x = s_terrain_phase_h;
    owner_scroll_y = s_terrain_phase_v;
    owner_world_x = Dkc3VideoTerrainShadowX(
        (uint16_t)owner_scroll_x, camera_x);
    owner_world_y = Dkc3VideoTerrainShadowY(
        (uint16_t)owner_scroll_y, camera_y);
    s_widescreen_world_x[terrain_layer] = owner_world_x;
    s_widescreen_world_y[terrain_layer] = owner_world_y;
    WsShadowSetWorld(terrain_layer, owner_world_x, owner_world_y);
    WsShadowSetScroll(terrain_layer, owner_scroll_x, owner_scroll_y);
    /* A presentation bias moves the PPU's 256-column window past the
     * cartridge's authentic VRAM window by |bias| columns on one side. The
     * rolling ring holds nothing authored for those columns (a stale or
     * prefetched page), so the world-keyed store must serve them. */
    WsShadowSetNativeViewportInset(
        terrain_layer, presentation_bias < 0 ? -presentation_bias : 0,
        presentation_bias > 0 ? presentation_bias : 0);
    WsShadowSetWestKeep(terrain_layer, 8);
    WsShadowSetEastKeep(terrain_layer, 8);
    /* Preserve a live dynamic BG write from this or the immediately prior
     * game frame, but do not allow stale history to defeat the verified
     * decompressed level-map value in the widened terrain margins. */
    WsShadowSetRespectGameWrites(terrain_layer, 1);
    /*
     * An unknown world cell must never fall through to a stale rolling VRAM
     * page. Exact viewport/history captures replace this bounded fallback as
     * soon as DKC3 displays or uploads the corresponding tile.
     */
    uint16_t blank_entry = 0;
    if (!PPU_bigTiles(g_ppu, terrain_layer))
      Dkc3VideoFindTransparent4bppTile(
          g_ppu->vram, 0x8000u,
          (uint16_t)PPU_bgTileAdr(g_ppu, terrain_layer), &blank_entry);
    WsShadowSetBlankTile(terrain_layer, blank_entry);
  }
  for (int layer = 0; layer < 2; layer++) {
    if (layer == terrain_layer)
      continue;
    if (have_owner && layer == 1 && s_waterfall_scene) {
      WsShadowSetWorld(1, s_waterfall_world_x, 0);
      WsShadowSetScroll(1, g_ppu->hScroll[1], 0);
      WsShadowSetRespectGameWrites(1, 0);
      WsShadowSetNativeViewportInset(1,
          presentation_bias < 0 ? -presentation_bias : 0,
          presentation_bias > 0 ? presentation_bias : 0);
    } else if (have_owner && alias_layer[layer] &&
        (layer_mask & (uint8_t)(1u << layer))) {
      /* The view shares the owner's keys. The renderer adds this layer's own
       * per-line scroll delta, so a band that leads the frame anchor by a
       * few pixels still resolves the exact world cell. */
      /* An alias band's rows are the terrain's rows s_alias_offset_rows
       * away (negative: the rows above), so the view's world Y is keyed
       * that far from the owner's. */
      WsShadowSetEntryAlias(
          layer, terrain_layer, owner_world_x,
          (uint32_t)((int32_t)owner_world_y +
                     s_alias_offset_rows[layer] * 8),
          owner_scroll_x, owner_scroll_y);
      WsShadowSetNativeViewportInset(
          layer, presentation_bias < 0 ? -presentation_bias : 0,
          presentation_bias > 0 ? presentation_bias : 0);
    } else {
      WsShadowClearEntryAlias(layer);
    }
  }

  WsShadowFrame(g_ppu);
  Dkc3PrefillWaterfall(presentation_bias);
  if (!have_owner)
    return false;
  return Dkc3PrefillWidescreenLevelTerrain(
      layer_mask, terrain_layer, layout,
      owner_world_x + presentation_bias, owner_world_x, camera_y);
}

/* Guest-address resolution for the HDMA dry run, matching the runner's
 * SimpleHdma table walk: WRAM banks, the low-RAM mirror, and ROM. */
static const uint8_t *Dkc3HdmaPointer(void *context, uint32_t address) {
  (void)context;
  const uint8_t bank = (uint8_t)(address >> 16);
  const uint16_t offset = (uint16_t)address;
  if (bank == 0x7e)
    return g_ram + offset;
  if (bank == 0x7f)
    return g_ram + 0x10000 + offset;
  if ((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && offset < 0x2000)
    return g_ram + offset;
  return RomPtr(address);
}

static bool Dkc3HdmaReadable(void *context, const uint8_t *pointer,
                             size_t length) {
  (void)context;
  if (!pointer)
    return false;
  const uintptr_t address = (uintptr_t)pointer;
  const uintptr_t ram_base = (uintptr_t)g_ram;
  if (address >= ram_base) {
    const size_t offset = (size_t)(address - ram_base);
    if (offset <= sizeof g_ram && length <= sizeof g_ram - offset)
      return true;
  }
  const uint32_t rom_size =
      g_snes && g_snes->cart ? (uint32_t)g_snes->cart->romSize : 0;
  const uintptr_t rom_base = (uintptr_t)g_rom;
  if (g_rom && rom_size != 0 && address >= rom_base) {
    const size_t offset = (size_t)(address - rom_base);
    if (offset <= rom_size && length <= (size_t)rom_size - offset)
      return true;
  }
  return false;
}

/* The KAOS arena's body window: BG1 must use window 1 alone, inverted, on
 * the main screen, and an active direct mode-1 HDMA channel must write
 * $2126/$2127. Dkc3VideoKaosBodyWindow picks the body lines' pair. */
static bool Dkc3FindKaosBodyWindow(uint8_t *reg_left, uint8_t *reg_right,
                                   int *left, int *right) {
  if ((g_ppu->windowsel & 0x0fu) != 0x03u ||
      !(g_ppu->screenWindowed[0] & 0x01u) ||
      (g_ppu->screenWindowed[1] & 0x01u))
    return false;
  for (int index = 0; index < 8; index++) {
    const DmaChannel *channel = &g_dma->channel[index];
    if (!(g_snesrecomp_last_hdmaen & (uint8_t)(1u << index)) ||
        channel->indirect || channel->bAdr != 0x26 || channel->mode != 1)
      continue;
    const uint32_t address =
        (uint32_t)channel->aAdr | ((uint32_t)channel->aBank << 16);
    enum { kTableLimit = 1024 };
    const uint8_t *table = Dkc3HdmaPointer(NULL, address);
    size_t length = kTableLimit;
    while (length > 0 && !Dkc3HdmaReadable(NULL, table, length))
      length /= 2;
    if (!table || length == 0)
      return false;
    return Dkc3VideoKaosBodyWindow(table, length, g_ppu->hScroll[0],
                                   reg_left, reg_right, left, right);
  }
  return false;
}

static void Dkc3ScanFrameBands(Dkc3HdmaBands *bands) {
  Dkc3HdmaChannelConfig channels[8];
  for (int index = 0; index < 8; index++) {
    const DmaChannel *channel = &g_dma->channel[index];
    channels[index].active =
        (g_snesrecomp_last_hdmaen & (uint8_t)(1u << index)) != 0;
    channels[index].indirect = channel->indirect;
    channels[index].b_address = channel->bAdr;
    channels[index].mode = channel->mode;
    channels[index].indirect_bank = channel->indBank;
    channels[index].table_address =
        (uint32_t)channel->aAdr | ((uint32_t)channel->aBank << 16);
  }
  Dkc3HdmaFrameState start;
  memcpy(start.h_scroll, g_ppu->hScroll, sizeof start.h_scroll);
  memcpy(start.v_scroll, g_ppu->vScroll, sizeof start.v_scroll);
  start.main_layers = g_ppu->screenEnabled[0];
  start.sub_layers = g_ppu->screenEnabled[1];
  memcpy(start.bg_sc, g_ppu->bgXsc, sizeof start.bg_sc);
  start.scroll_prev = g_ppu->scrollPrev;
  start.scroll_prev2 = g_ppu->scrollPrev2;
  const Dkc3HdmaMemory memory = {
      Dkc3HdmaPointer, Dkc3HdmaReadable, NULL};
  Dkc3HdmaScanBands(channels, &start, &memory, bands);
}

/* Decide, for every wide BG1/BG2 layer and every scanline band, whether the
 * layer displays the streamed world map (terrain phase, relative to the
 * scroll the owner rendered at the frame anchor) or a bounded effect plane.
 * Either physical layer may hold either role in any band. */
static void Dkc3ClassifyBands(uint8_t wide_layer_mask,
                              int terrain_layer,
                              Dkc3VideoLevelLayout layout,
                              const Dkc3HdmaBands *bands,
                              uint8_t policy[2][kDkc3HdmaMaxBands],
                              bool alias_layer[2]) {
  const bool have_owner = terrain_layer >= 0 && terrain_layer < 2;
  const uint16_t terrain_h = have_owner ? s_terrain_phase_h : 0;
  const uint16_t terrain_v = have_owner ? s_terrain_phase_v : 0;
  const uint16_t stream_base =
      (uint16_t)(Dkc3TerrainVramBase() & 0xfc00u);
  const int32_t camera_x = (int32_t)Dkc3ReadWram16(kDkc3WramCameraX);
  const int32_t camera_y = (int32_t)Dkc3ReadWram16(kDkc3WramCameraY);
  Dkc3TrackVramPages(camera_x, camera_y);
  for (int layer = 0; layer < 2; layer++) {
    alias_layer[layer] = false;
    s_plane_band_count[layer] = 0;
    s_alias_offset_rows[layer] = 0;
    const bool wide = (wide_layer_mask & (uint8_t)(1u << layer)) != 0;
    if (wide && have_owner && layer != terrain_layer &&
        !(layer == 1 && s_waterfall_scene)) {
      /* Verify against the ring phase of the layer's first band that
       * scrolls with the terrain; a parallax band above the water (the
       * foliage, 130 pixels off) must not stand in for the water bands. */
      uint16_t layer_h = (uint16_t)g_ppu->hScroll[layer];
      for (int index = 0; index < bands->count; index++) {
        const unsigned distance = (unsigned)(
            ((int)bands->band[index].h_scroll[layer] - (int)terrain_h + 512) &
            511);
        if (distance <= 8u || distance >= 504u) {
          layer_h = bands->band[index].h_scroll[layer];
          break;
        }
      }
      Dkc3AliasDecodeContext ctx;
      memset(&ctx, 0, sizeof ctx);
      uint8_t bank = 0;
      ctx.bank_data = layout != kDkc3VideoLevelLayoutUnknown
                          ? Dkc3LevelSourceBank(&bank) : NULL;
      ctx.map_base = kDkc3LevelMapBase;
      ctx.metatile_base = Dkc3ReadWram16(kDkc3WramMetatileBase);
      ctx.layout = layout;
      ctx.row_bytes = s_terrain_prefill_stats.row_bytes;
      const uint32_t top_world_y =
          Dkc3VideoTerrainShadowY(terrain_v, (uint32_t)camera_y);
      ctx.top_world_row = top_world_y >> 3;
      ctx.top_ring_row = (unsigned)((terrain_v >> 3) & 31u);
      const uint32_t world_x =
          Dkc3VideoTerrainShadowX(terrain_h, (uint32_t)camera_x);
      ctx.first_world_col = world_x >> 3;
      ctx.first_native_col = (unsigned)((terrain_h >> 3) & 63u);
      Dkc3ComputeRowAliasOffsets(layer, terrain_layer, layer_h, terrain_h,
                                 &ctx);
    }
    for (int index = 0; index < bands->count; index++) {
      const Dkc3HdmaBand *band = &bands->band[index];
      if (layer == 1 && s_waterfall_scene) {
        policy[layer][index] = kDkc3BandPolicyWaterfall;
        continue;
      }
      const bool at_phase =
          have_owner && wide &&
          Dkc3VideoScrollAtTerrainPhase(
              band->h_scroll[layer], band->v_scroll[layer],
              terrain_h, terrain_v);
      const uint16_t band_base =
          (uint16_t)((band->bg_sc[layer] & 0xfcu) << 8);
      /* A second layer at the terrain phase is world content only when
       * its map is the terrain ring itself (HDMA showing the ring on the
       * other index) or its rows verify against the terrain ring, at the
       * offset those rows prove: the reflection band verifies four rows
       * away, a duplicated ceiling at zero, and a backdrop that merely
       * scrolls at camera speed verifies nowhere and stays a plane. */
      /* The verification keys rows by the terrain's phase, so only a band
       * scrolling at that phase can be read through the terrain store. */
      int alias_rows = 0;
      const bool verified =
          at_phase && layer != terrain_layer &&
          Dkc3BandAliasOffset(layer, band->v_scroll[layer],
                              band->first_line, band->last_line,
                              &alias_rows);
      const bool world =
          at_phase && (layer == terrain_layer || band_base == stream_base ||
                       (verified && alias_rows == 0));
      const bool alias =
          !world && verified && alias_rows != 0 &&
          (s_alias_offset_rows[layer] == 0 ||
           s_alias_offset_rows[layer] == alias_rows);
      const bool plane =
          !world && !alias && wide &&
          Dkc3BandShowsStaticPlane(band->bg_sc[layer], stream_base, layer,
                                   band->v_scroll[layer], band->first_line,
                                   band->last_line);
      policy[layer][index] = world ? kDkc3BandPolicyWorld
                             : alias ? kDkc3BandPolicyAlias
                             : plane ? kDkc3BandPolicyPlane
                                     : kDkc3BandPolicyRepeat;
      if (plane)
        s_plane_band_count[layer]++;
      if (alias)
        s_alias_offset_rows[layer] = alias_rows;
      if ((world || alias) && layer != terrain_layer)
        alias_layer[layer] = true;
    }
  }
  /* DKC3_BAND_DUMP=1: print every scanline band's scrolls, tilemap
   * register, and the policy chosen for each wide layer (W world, A alias,
   * F waterfall, P plane, R repeat, - not wide) to stderr each frame. A band whose policy
   * alternates between frames shows as a strip that changes texture. */
  if (getenv("DKC3_BAND_DUMP")) {
    fprintf(stderr, "bands %d frame %u:", bands->count, s_plane_frame);
    for (int index = 0; index < bands->count; index++) {
      const Dkc3HdmaBand *band = &bands->band[index];
      fprintf(stderr, " [%u-%u", band->first_line, band->last_line);
      for (int layer = 0; layer < 2; layer++) {
        const bool wide = (wide_layer_mask & (uint8_t)(1u << layer)) != 0;
        fprintf(stderr, " %c%02x/%u,%u",
                !wide ? '-'
                : policy[layer][index] == kDkc3BandPolicyWorld ? 'W'
                : policy[layer][index] == kDkc3BandPolicyAlias ? 'A'
                : policy[layer][index] == kDkc3BandPolicyWaterfall ? 'F'
                : policy[layer][index] == kDkc3BandPolicyPlane ? 'P' : 'R',
                band->bg_sc[layer], band->h_scroll[layer],
                band->v_scroll[layer]);
      }
      fputc(']', stderr);
    }
    fputc('\n', stderr);
  }
}

static void Dkc3ApplyBandPolicies(const Dkc3HdmaBand *band,
                                  int band_index,
                                  uint8_t wide_layer_mask) {
  for (unsigned layer = 0; layer < 2; layer++) {
    if (!(wide_layer_mask & (uint8_t)(1u << layer)))
      continue;
    const uint8_t policy =
        band && band_index >= 0 ? s_band_policy[layer][band_index]
                                : (uint8_t)kDkc3BandPolicyWorld;
    if (policy == kDkc3BandPolicyRepeat) {
      PpuSetWidescreenLayerRepeatBand(
          g_ppu, (uint8_t)layer, band->first_line,
          (uint8_t)(band->last_line + 1u));
      PpuSetWidescreenLayerRawBand(g_ppu, (uint8_t)layer, 0, 0);
    } else if (policy == kDkc3BandPolicyPlane) {
      PpuSetWidescreenLayerRepeatBand(g_ppu, (uint8_t)layer, 0, 0);
      PpuSetWidescreenLayerRawBand(
          g_ppu, (uint8_t)layer, band->first_line,
          (uint8_t)(band->last_line + 1u));
    } else {
      PpuSetWidescreenLayerRepeatBand(g_ppu, (uint8_t)layer, 0, 0);
      PpuSetWidescreenLayerRawBand(g_ppu, (uint8_t)layer, 0, 0);
    }
  }
}

/*
 * The sprite renderer variant at $B7:A88F culls at SBC $196D / ADC #$0030 /
 * CMP #$0160 ($B7:A8C8) exactly like the eight variants the override
 * script widens in generated code, but the analysis leaves it to the
 * interpreter, which reads those immediates from the ROM image. Widen them
 * there using the same Dkc3VideoExpandSpriteCullLeft/Span policy (including
 * terrain readiness, presentation bias, DKC3_CULL_WIDEN, and DKC3_SPAWN_LEAD).
 * Each write checks the bytes it replaces, so an image that does not hold the
 * native or the previously patched operands is left alone.
 */
static void Dkc3PatchInterpreterCull(void) {
  static uint16_t applied_left = 0x30u;
  static uint16_t applied_span = 0x160u;
  const uint16_t left = Dkc3VideoExpandSpriteCullLeft(0x30u);
  const uint16_t span = Dkc3VideoExpandSpriteCullSpan(0x160u);
  if ((left == applied_left && span == applied_span) || !g_rom)
    return;
  enum { kLeftOperand = 0x37a8c9u, kSpanOperand = 0x37a8ccu };
  const uint32_t rom_size = g_snes && g_snes->cart ? (uint32_t)g_snes->cart->romSize : 0;
  if (rom_size < kSpanOperand + 2u)
    return;
  uint8_t *rom = (uint8_t *)(uintptr_t)g_rom;
  if (rom[kLeftOperand] != (applied_left & 0xffu) ||
      rom[kLeftOperand + 1] != (applied_left >> 8) ||
      rom[kSpanOperand] != (applied_span & 0xffu) ||
      rom[kSpanOperand + 1] != (applied_span >> 8))
    return;
  rom[kLeftOperand] = (uint8_t)(left & 0xffu);
  rom[kLeftOperand + 1] = (uint8_t)(left >> 8);
  rom[kSpanOperand] = (uint8_t)(span & 0xffu);
  rom[kSpanOperand + 1] = (uint8_t)(span >> 8);
  applied_left = left;
  applied_span = span;
}

/* Floodlit Fish uses a full-screen inverted BG3 window for the water surface
 * and half-color math against BG1/BG2 on the subscreen. The native view has a
 * nontransparent subscreen pixel behind every surface pixel, but valid
 * adjacent-world terrain can be transparent on that exact row. In a margin,
 * that makes the otherwise correct repeated surface skip its tint. Supply
 * only those missing subscreen pixels from the corresponding native column;
 * authored adjacent-world pixels remain untouched. The exact register
 * signature keeps this out of other color-math scenes. */
static unsigned s_tint_lines_seen, s_tint_lines_water, s_tint_lines_matched;

void Dkc3TintLineCounters(unsigned *seen, unsigned *water, unsigned *matched) {
  *seen = s_tint_lines_seen;
  *water = s_tint_lines_water;
  *matched = s_tint_lines_matched;
  s_tint_lines_seen = s_tint_lines_water = s_tint_lines_matched = 0;
}

static void Dkc3EnhanceUnderwaterSubscreen(Ppu *ppu, uint y, bool sub,
                                           void *context) {
  (void)y;
  (void)context;
  if (sub) {
    s_tint_lines_seen++;
    /* A water line: BG3 windowed on the main screen through an inverted
     * window 1 spanning the whole width. */
    if ((ppu->screenWindowed[0] & 0x04u) &&
        (ppu->windowsel & 0x0f00u) == 0x0300u && ppu->window1left == 0 &&
        ppu->window1right == 0xffu)
      s_tint_lines_water++;
  }
  if (!sub || !Dkc3VideoIsWidescreen() ||
      !Dkc3VideoUsesUnderwaterSubscreenTint(
          ppu->bgmode, ppu->screenEnabled[0], ppu->screenEnabled[1],
          ppu->screenWindowed[0], ppu->screenWindowed[1], ppu->windowsel,
          ppu->window1left, ppu->window1right, ppu->cgwsel, ppu->cgadsub,
          ppu->fixedColor))
    return;
  s_tint_lines_matched++;
  Dkc3VideoRepeatTransparentSubscreenMargins(
      ppu->bgBuffers[1].data, kPpuBufWidth, kPpuExtraLeftRight,
      ppu->extraLeftCur, ppu->extraRightCur);
}

void Dkc3DrawPpuFrame(void) {
  PpuSetWidescreenLineEnhancer(
      g_ppu, Dkc3VideoIsWidescreen() ? Dkc3EnhanceUnderwaterSubscreen : NULL,
      NULL);
  /* The enhancer repairs only the subscreen's color-math operand. BG1 is
   * already reconstructed by the world shadow and must remain eligible in
   * both margins; the runtime's legacy enhancer default clips BG1 because
   * older clients used their callback to replace that layer outright. */
  PpuSetWidescreenLineEnhancerWideLayers(
      g_ppu, Dkc3VideoIsWidescreen() ? 0x01u : 0u);
  SimpleHdma channels[8];
  bool active[8] = {false};
  const Dkc3VideoLevelLayout layout =
      Dkc3VideoLevelLayoutForScene(
          Dkc3InLevel() ? Dkc3MapShape() : 0xffffu,
          Dkc3ReadWram16(kDkc3WramLevelNumber));

  /*
   * Widescreen is a host-only PPU policy. Reapply it for every frame because
   * reset/state restore deliberately does not serialize presentation
   * geometry.
   */
  s_frame_bands.count = 0;
  if (Dkc3VideoIsWidescreen() && layout != kDkc3VideoLevelLayoutUnknown)
    Dkc3ScanFrameBands(&s_frame_bands);
  uint8_t wide_layer_mask =
      Dkc3VideoIsWidescreen()
          ? Dkc3VideoPpuFrameWideLayerMask(g_ppu->bgmode, g_ppu->bgXsc,
                                           g_ppu->screenEnabled[0],
                                           g_ppu->screenEnabled[1],
                                           &s_frame_bands)
          : 0;
  if (layout == kDkc3VideoLevelLayoutUnknown)
    wide_layer_mask = 0;
  /*
   * A level-name card (NMI sub-mode 11 inside the gameplay mode) is a static
   * picture on bounded maps with no camera and no terrain stream, whatever
   * its map size. It is presented like every bounded screen, centered
   * between black margins, never through the terrain path: nothing authored
   * exists beyond its 256 columns (the 64-column cards hold a wider painting
   * on the right but only the map's wrap on the left), and the owner
   * prefers black to mirrored or wrapped art there.
   */
  /* DKC3's level-name cards are not yet identified; every card is a
   * bounded screen the layout classifier already centers. */
  const bool name_card = false;
  const bool snow_arena = Dkc3VideoIsWidescreen() &&
      Dkc3VideoUsesSnowArenaPlanes(
          Dkc3InLevel(), Dkc3ReadWram16(kDkc3WramLevelNumber),
          g_ppu->bgmode, g_ppu->bgXsc, g_ppu->bgTileAdr,
          g_snesrecomp_last_hdmaen, g_ppu->vram, 0x8000);
  const bool extend_world = wide_layer_mask != 0 && !name_card;
  const uint8_t enabled_layers =
      (uint8_t)(g_ppu->screenEnabled[0] | g_ppu->screenEnabled[1]);
  const bool overworld = Dkc3VideoIsWidescreen() && !extend_world &&
      !snow_arena &&
      Dkc3VideoIsResidentOverworld(
          Dkc3FactRead24(kDkc3WramMainLoopVector), g_ppu->bgmode,
          g_ppu->bgXsc, enabled_layers, g_ppu->hScroll,
          Dkc3ReadWram16(kDkc3WramCameraX));
  if (!overworld)
    Dkc3ResetOverworldState();
  int presentation_bias = 0;
  bool trunk_windows_active = false;
  bool band_policies_active = false;
  bool blank_margins = false;
  bool kaos_body_window = false;
  uint8_t kaos_body_reg_left = 0, kaos_body_reg_right = 0;
  int kaos_body_left = 0, kaos_body_right = 0;
  bool room_windows = false;
  int room_w1_left = 0, room_w1_right = 0, room_w2_left = 0, room_w2_right = 0;
  /* Reset host presentation latches before deriving the current frame. A
   * prior gameplay scene must not leave a physically wide BG3 enabled on a
   * bounded title, menu, or unsupported layout. */
  PpuSetWidescreenLayerMask(g_ppu, 0);
  PpuSetWidescreenBg3Widen(g_ppu, 0);
  PpuSetWidescreenPresentationXBias(g_ppu, 0);
  bool overworld_presented = false;
  if (overworld) {
    Dkc3ResetWidescreenShadow();
    overworld_presented = Dkc3PresentOverworld(enabled_layers);
  }
  if (extend_world) {
    const int extra = Dkc3VideoExtra();
    PpuSetExtraSpace(g_ppu, (uint8_t)extra);
    const uint16_t camera_x = Dkc3ReadWram16(kDkc3WramCameraX);
    uint16_t region_minimum_x = 0x0100u;
    uint16_t maximum_scroll_x = Dkc3MaximumScrollX();
    /* The walls of the camera's current region and the neighbours it
     * links: $04BC is the maximum over every region, so a region the
     * camera cannot leave sideways (an elevator shaft exactly one screen
     * wide) would otherwise widen into other rooms' map columns that the
     * cartridge never shows beside it. The native window always stays
     * inside the bounds. */
    {
      uint16_t region_left = 0, region_right = 0;
      if (Dkc3VideoCameraRegionSpan(g_ram,
                                    Dkc3ReadWram16(kDkc3WramCameraRegion),
                                    &region_left, &region_right))
        Dkc3VideoRegionScrollBounds(camera_x, region_left, region_right,
                                    maximum_scroll_x, &region_minimum_x,
                                    &maximum_scroll_x);
    }
    int bias = 0;
    int left_margin = extra;
    int right_margin = extra;
    /* The level's west bound: the region's unless the last prefill found
     * the player held at the authored world's edge with nothing beside it
     * (Dkc3VideoHoldWest). The bias then steps toward the glide's target
     * (Dkc3VideoStepPresentationBias). */
    const uint16_t minimum_scroll_x =
        s_hold_west_valid && s_hold_west_x > region_minimum_x &&
                s_hold_west_x <= camera_x
            ? (uint16_t)s_hold_west_x : region_minimum_x;
    int wanted_bias = 0;
    Dkc3VideoPresentationMarginsBounded(camera_x, minimum_scroll_x,
                                        maximum_scroll_x, &wanted_bias,
                                        &left_margin, &right_margin);
    if (s_bias_policy != Dkc3VideoGetEdgePolicy()) {
      s_bias_policy = Dkc3VideoGetEdgePolicy();
      s_bias_valid = false;
    }
    const bool limit_margins = s_margin_valid && s_bias_valid &&
        s_bias_frames >= (uint32_t)kDkc3BiasSettleFrames;
    if (!s_bias_valid) {
      s_bias_presented = wanted_bias;
      s_bias_frames = 0;
    } else if (s_bias_frames < (uint32_t)kDkc3BiasSettleFrames) {
      s_bias_presented = wanted_bias;
    } else {
      s_bias_presented = Dkc3VideoStepPresentationBias(
          s_bias_presented, wanted_bias,
          (int)camera_x - (int)s_bias_camera_x);
    }
    s_bias_camera_x = camera_x;
    s_bias_valid = true;
    if (s_bias_frames < 0xffffffffu)
      s_bias_frames++;
    bias = s_bias_presented;
    Dkc3VideoMarginsForBias(camera_x, minimum_scroll_x, maximum_scroll_x,
                            bias, &left_margin, &right_margin);
    /* A region change that opens onto a wider room moves its wall away
     * in one frame; wipe the margin open rather than popping a full
     * margin of terrain and objects in. DKC3_CULL_WIDEN=0 keeps the
     * immediate walls for same-binary comparisons. */
    const int view_x = (int)camera_x + bias;
    if (limit_margins && Dkc3VideoCullWidenEnabled()) {
      left_margin = Dkc3VideoLimitMarginGrowth(s_margin_left, left_margin,
                                               view_x - s_margin_view_x);
      right_margin = Dkc3VideoLimitMarginGrowth(s_margin_right, right_margin,
                                                s_margin_view_x - view_x);
    }
    s_margin_left = left_margin;
    s_margin_right = right_margin;
    s_margin_view_x = view_x;
    s_reach_minimum_x = minimum_scroll_x;
    s_reach_maximum_x = maximum_scroll_x;
    const int terrain_layer = Dkc3VideoTerrainLayer(
        wide_layer_mask, g_ppu->bgXsc, Dkc3TerrainVramBase());
    /* The terrain phase the frame renders: the frame-start register unless
     * the cartridge left it off the camera and its HDMA sets the camera
     * phase on the rendered lines. */
    s_terrain_phase_h = terrain_layer >= 0 ? g_ppu->hScroll[terrain_layer] : 0;
    s_terrain_phase_v = terrain_layer >= 0 ? g_ppu->vScroll[terrain_layer] : 0;
    s_terrain_phase_from_band =
        terrain_layer >= 0 &&
        Dkc3VideoSelectTerrainPhase(&s_frame_bands, terrain_layer,
                                    s_terrain_phase_h, s_terrain_phase_v,
                                    camera_x, Dkc3ReadWram16(kDkc3WramCameraY),
                                    &s_terrain_phase_h, &s_terrain_phase_v);
    /* Screen enables as the union of the frame start and every HDMA band:
     * the repeat policy needs a layer the
     * cartridge switches on only inside a band (the lava surface). */
    uint8_t band_main_layers = g_ppu->screenEnabled[0];
    uint8_t band_sub_layers = g_ppu->screenEnabled[1];
    for (int index = 0; index < s_frame_bands.count; index++) {
      band_main_layers =
          (uint8_t)(band_main_layers | s_frame_bands.band[index].main_layers);
      band_sub_layers =
          (uint8_t)(band_sub_layers | s_frame_bands.band[index].sub_layers);
    }
    bool alias_layer[2] = {false, false};
    Dkc3CheckWaterfallSource(wide_layer_mask, terrain_layer);
    Dkc3ClassifyBands(wide_layer_mask, terrain_layer, layout, &s_frame_bands,
                      s_band_policy, alias_layer);
    /* WsShadow owns only BG1/BG2 terrain. Establish exact terrain readiness
     * before allowing any additional physical layer into the final render
     * mask; this keeps 64-column HUD/staging allocations fail-closed. */
    PpuSetWidescreenLayerMask(g_ppu, wide_layer_mask);
    const bool terrain_ready = Dkc3PrepareWidescreenShadow(
        wide_layer_mask, terrain_layer, layout, bias, alias_layer);
    s_terrain_prefill_stats.camera_region =
        Dkc3ReadWram16(kDkc3WramCameraRegion);
    s_terrain_prefill_stats.wall_left = minimum_scroll_x;
    s_terrain_prefill_stats.wall_right = maximum_scroll_x;
    s_terrain_prefill_stats.left_margin = (int16_t)left_margin;
    s_terrain_prefill_stats.right_margin = (int16_t)right_margin;
    s_margin_valid = terrain_ready;
    presentation_bias = terrain_ready ? bias : 0;
    trunk_windows_active = terrain_ready;
    /* Nothing authored reaches the margins while the world is unproven (a
     * level intro's static picture, the first frames of a stream), and the
     * PPU would otherwise fill them with the backdrop color, which Barrel
     * Bayou's intro sets to pure blue. Show black there, as a bounded
     * screen does, never the backdrop. */
    blank_margins = !terrain_ready;
    PpuSetWidescreenPresentationXBias(g_ppu, presentation_bias);
    Dkc3VideoSetPresentationBias(presentation_bias);
    if (terrain_ready) {
      PpuSetExtraSideSpace(g_ppu, left_margin, right_margin, 0);
      /* The PPU leaves columns outside the clamped span untouched, so a
       * margin that narrows at a wall would keep last frame's pixels
       * there (a sheared strip once the camera moves vertically). Black
       * them before the lines render; full-budget lines overwrite them. */
      Dkc3VideoClearOutsideMargins(g_ppu->renderBuffer, g_ppu->renderPitch,
                                   Dkc3VideoWidth(), kDkc3VideoHeight, extra,
                                   left_margin, right_margin);
    }
    uint8_t physical_wide_mask =
        terrain_ready
            ? Dkc3VideoPhysicalWideLayerMask(
                  g_ppu->bgmode, g_ppu->bgXsc,
                  g_ppu->screenEnabled[0], g_ppu->screenEnabled[1])
            : 0;
    /* A mirrored margin at a level wall has no authored BG3 ring columns to
     * expose. Within one margin of a wall, a physical 64-column BG3 repeats
     * its rendered line like a bounded layer instead of reading the ring.
     * `reflect` mirrors at the level's walls, not the camera region's. */
    if (Dkc3VideoMarginLeavesAuthoredExtent(camera_x, Dkc3MaximumScrollX()))
      physical_wide_mask = (uint8_t)(physical_wide_mask & ~0x04u);
    /* A bounded layer with a hardware window cannot repeat an already
     * composited 256-pixel line: Murky Mill's per-scanline BG3 light window
     * and Poisonous Pipeline's BG3 water-surface window would otherwise wrap
     * their clipped spans into the opposite margin, including near a level
     * wall. Draw the 32-column map physically across the widened span so the
     * same HDMA window edges are evaluated in screen space. */
    physical_wide_mask = (uint8_t)(
        physical_wide_mask |
        (terrain_ready
             ? Dkc3VideoPhysicalWindowedLayerMask(
                   g_ppu->bgmode, g_ppu->bgXsc,
                   g_ppu->screenEnabled[0], g_ppu->screenEnabled[1],
                   g_ppu->screenWindowed[0], g_ppu->screenWindowed[1])
             : 0));
    const uint8_t repeat_exempt_mask = 0u;
    const uint8_t render_layer_mask =
        (uint8_t)(wide_layer_mask | physical_wide_mask);
    PpuSetWidescreenLayerMask(g_ppu, render_layer_mask);
    /* The shared PPU has a separate clamp for BG3. Any enabled physical
     * 64-column BG3 may use authentic adjacent columns after terrain is
     * proven ready. */
    PpuSetWidescreenBg3Widen(
        g_ppu, (physical_wide_mask & 0x04u) != 0 ? 1u : 0u);
    /*
     * Every enabled bounded (32-column) background repeats its rendered
     * native scanline into the margins. That is exactly what a wider PPU
     * would draw from a map that wraps at 256 pixels, including its HDMA
     * phase and color-math participation. Windowed bounded layers are in the
     * physical-wide mask above so their screen-space clips are evaluated at
     * the presented positions. Rolling 64-column layers are handled per
     * scanline band below.
     */
    /*
     * A bounded layer the cartridge enables only inside an HDMA band (the
     * ship hold's BG3 water surface: TM is zero at frame start and the
     * band switches BG3 on for its scanlines) must repeat like one enabled
     * for the whole frame, or the band draws only the native 256 columns
     * and the surface line stops at the 4:3 edges. The repeat policy is
     * derived from the union of every band's screen enables.
     */
    PpuSetWidescreenLayerRepeat(
        g_ppu, Dkc3VideoRepeatLayerMask(
                   g_ppu->bgmode, band_main_layers, band_sub_layers,
                   (uint8_t)(render_layer_mask | repeat_exempt_mask)));
    /*
     * A 32-column map wraps at 256 pixels on hardware, so its rendered line
     * repeats at exactly that period and shows whatever seam the authored
     * plane has at its wrap, as the console does once the layer scrolls.
     * Only a bounded backdrop kept in a 64-column allocation (the ship-hold
     * cabin wall) has no hardware wrap to fall back on; those lines
     * continue at the period their own rendered interior proves, and their
     * seven stale fine-scroll endpoints are rebuilt from that period.
     */
    PpuSetWidescreenLayerRepeatAutoPeriod(g_ppu, wide_layer_mask,
                                          wide_layer_mask);
    if (terrain_ready) {
      band_policies_active = true;
    } else {
      /* An unproven rolling layer shows no margin content at all rather
       * than raw recycled VRAM. Bounded layers still repeat. */
      PpuSetWidescreenLayerClamp(g_ppu, wide_layer_mask);
    }
    Dkc3VideoSetTerrainReady(terrain_ready);
    /* The KAOS arena (level $23) streams no terrain, so readiness stays
     * false, but its room is a verified static 64-column plane. Draw that
     * plane from its own map, never across its 512-pixel wrap. While KAOS
     * fights (room on BG2), keep his 32-column BG1 body inside its window
     * (a wrap would duplicate it) and repeat the bounded BG3 sky at its
     * hardware period. Once K. Roolenstein appears (room on BG1), repeat
     * BG2 and BG3, which share one 32-column map; the cartridge's own
     * windows keep them off the room's west wall. Culling, bias, and the
     * camera stay native. */
    const int arena_layer =
        (!terrain_ready && terrain_layer < 0)
            ? Dkc3VideoKaosArenaPlaneLayer(
                  Dkc3InLevel(), Dkc3ReadWram16(kDkc3WramLevelNumber),
                  g_ppu->bgmode, g_ppu->bgXsc, s_plane_band_count,
                  s_frame_bands.count)
            : -1;
    if (arena_layer == 0) {
      int arena_left = 0, arena_right = 0;
      Dkc3VideoKaosArenaMargins(g_ppu->hScroll[0], extra, &arena_left,
                                &arena_right);
      blank_margins = false;
      PpuSetExtraSideSpace(g_ppu, arena_left, arena_right, 0);
      Dkc3VideoClearOutsideMargins(g_ppu->renderBuffer, g_ppu->renderPitch,
                                   Dkc3VideoWidth(), kDkc3VideoHeight, extra,
                                   arena_left, arena_right);
      /* BG2 and BG3 are drawn into the margins from their own 32-column
       * map (the hardware wrap), clipped by the cartridge's room-anchored
       * windows. A 256-pixel repeat would copy columns the window hides at
       * the opposite edge. Without both unclamped windows they stay native. */
      room_windows =
          Dkc3VideoKaosRoomWindow(g_ppu->window1left, g_ppu->window1right,
                                  g_ppu->hScroll[0],
                                  kDkc3KaosRoomWindowWest,
                                  kDkc3KaosRoomWindow1East, &room_w1_left,
                                  &room_w1_right) &&
          Dkc3VideoKaosRoomWindow(g_ppu->window2left, g_ppu->window2right,
                                  g_ppu->hScroll[0],
                                  kDkc3KaosRoomWindowWest,
                                  kDkc3KaosRoomWindow2East, &room_w2_left,
                                  &room_w2_right);
      PpuSetWidescreenLayerMask(g_ppu, room_windows ? 0x07 : 0x01);
      PpuSetWidescreenBg3Widen(g_ppu, room_windows ? 1 : 0);
      PpuSetWidescreenLayerClamp(g_ppu, 0);
      PpuSetWidescreenLayerRepeat(g_ppu, 0);
      PpuSetWidescreenLayerRepeatAutoPeriod(g_ppu, 0, 0);
      band_policies_active = true;
    } else if (arena_layer == 1) {
      int arena_left = 0, arena_right = 0;
      Dkc3VideoKaosArenaMargins(g_ppu->hScroll[1], extra, &arena_left,
                                &arena_right);
      blank_margins = false;
      PpuSetExtraSideSpace(g_ppu, arena_left, arena_right, 0);
      Dkc3VideoClearOutsideMargins(g_ppu->renderBuffer, g_ppu->renderPitch,
                                   Dkc3VideoWidth(), kDkc3VideoHeight, extra,
                                   arena_left, arena_right);
      /* BG1 continues into a margin only through the unclamped body
       * window; without it the body stays inside the native window. */
      kaos_body_window = Dkc3FindKaosBodyWindow(&kaos_body_reg_left,
                                                &kaos_body_reg_right,
                                                &kaos_body_left,
                                                &kaos_body_right);
      PpuSetWidescreenLayerMask(g_ppu, kaos_body_window ? 0x03 : 0x02);
      PpuSetWidescreenBg3Widen(g_ppu, 0);
      PpuSetWidescreenLayerClamp(g_ppu, 0);
      PpuSetWidescreenLayerRepeat(g_ppu, 0x04);
      PpuSetWidescreenLayerRepeatAutoPeriod(g_ppu, 0, 0);
      band_policies_active = true;
    }
  } else if (snow_arena) {
    /* Keep the cartridge camera and object activation unchanged. These
     * bounded planes have no map stream or world shadow to prefill. */
    Dkc3ResetWidescreenShadow();
    PpuSetExtraSpace(g_ppu, (uint8_t)Dkc3VideoExtra());
    PpuSetWidescreenLayerMask(g_ppu, 0x03);
  } else if (overworld_presented) {
    /* Dkc3PresentOverworld set this frame's geometry and layer policy. */
  } else if (Dkc3VideoIsWidescreen()) {
    Dkc3ResetWidescreenShadow();
    /*
     * Clear the whole host row before centering a bounded 256-column screen.
     * PpuSetExtraSpaceCentered intentionally draws no margin pixels, so this
     * prevents the preceding wide gameplay frame from surviving there.
     */
    size_t row_bytes = (size_t)Dkc3VideoWidth() * kDkc3VideoBytesPerPixel;
    for (int y = 0; y < kDkc3VideoHeight; y++)
      memset(g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch,
             0, row_bytes);
    PpuSetExtraSpaceCentered(g_ppu, (uint8_t)Dkc3VideoExtra());
  } else {
    Dkc3ResetWidescreenShadow();
    PpuSetExtraSpace(g_ppu, 0);
  }
  Dkc3PatchInterpreterCull();
  Dkc3ApplyTrunkWindows(trunk_windows_active);
  if (kaos_body_window)
    Dkc3PpuSetWsWindowUnclamped(0, true, kaos_body_left, kaos_body_right,
                                kaos_body_reg_left, kaos_body_reg_right);
  if (room_windows) {
    Dkc3PpuSetWsWindowUnclamped(0, true, room_w1_left, room_w1_right,
                                g_ppu->window1left, g_ppu->window1right);
    Dkc3PpuSetWsWindowUnclamped(1, true, room_w2_left, room_w2_right,
                                g_ppu->window2left, g_ppu->window2right);
  }
  Dkc3SyncWidescreenObjPalettes(Dkc3InLevel());

  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  for (int channel = 0; channel < 8; channel++) {
    active[channel] = g_dma->channel[channel].hdmaActive;
    if (active[channel])
      SimpleHdma_Init(&channels[channel], &g_dma->channel[channel]);
  }

  const Dkc3HdmaBand *current_band = NULL;
  for (int line = 0; line <= 224; line++) {
    if (band_policies_active) {
      const Dkc3HdmaBand *band = Dkc3HdmaBandForLine(&s_frame_bands, line);
      if (band != current_band) {
        current_band = band;
        Dkc3ApplyBandPolicies(
            band, band ? (int)(band - s_frame_bands.band) : -1,
            wide_layer_mask);
      }
    }
    if (presentation_bias != 0) {
      for (unsigned layer = 0; layer < 4; layer++)
        g_ppu->hScroll[layer] =
            (uint16_t)(g_ppu->hScroll[layer] + presentation_bias);
    }
    ppu_runLine(g_ppu, line);
    if (presentation_bias != 0) {
      for (unsigned layer = 0; layer < 4; layer++)
        g_ppu->hScroll[layer] =
            (uint16_t)(g_ppu->hScroll[layer] - presentation_bias);
    }
    for (int channel = 0; channel < 8; channel++) {
      if (active[channel]) SimpleHdma_DoLine(&channels[channel]);
    }
  }
  if (band_policies_active) {
    for (unsigned layer = 0; layer < 3; layer++) {
      PpuSetWidescreenLayerRepeatBand(g_ppu, (uint8_t)layer, 0, 0);
      PpuSetWidescreenLayerRawBand(g_ppu, (uint8_t)layer, 0, 0);
    }
  }
  if (blank_margins) {
    const size_t extra = (size_t)Dkc3VideoExtra();
    const size_t width = (size_t)Dkc3VideoWidth();
    if (extra > 0 && width >= (size_t)kDkc3VideoNativeWidth + 2 * extra) {
      const size_t side_bytes = extra * kDkc3VideoBytesPerPixel;
      const size_t right_offset =
          (width - extra) * kDkc3VideoBytesPerPixel;
      for (int y = 0; y < kDkc3VideoHeight; y++) {
        uint8_t *row = g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch;
        memset(row, 0, side_bytes);
        memset(row + right_offset, 0, side_bytes);
      }
    }
  }
  /* Host-only annotation over the finished picture (dkc3_kong_tracker.h). */
  Dkc3KongDrawFrameBadge();

  /* The static-recomp host advances one complete game frame and one complete
   * render pass as separate operations. Model the VBlank boundary after the
   * visible lines so the PPU reloads its internal OAM port from OAMADD before
   * the next frame's NMI performs DKC3's 544-byte OAM DMA. Without this call,
   * the DMA source is correct but the destination begins at the stale address
   * left by the preceding transfer and the sprite table rotates every frame. */
  (void)ppu_checkOverscan(g_ppu);
  ppu_handleVblank(g_ppu);
}

uint32_t Dkc3ResumePc(void) {
  return s_resume_pc;
}

int Dkc3LastLleResult(void) {
  return s_last_lle_result;
}

/* Required neutral hooks declared by generated funcs.h. */
void RunOneFrameOfGame_Internal(void) {
  Dkc3RunOneFrame();
}

void ResetSpritesFunc(int first) {
  (void)first;
}

unsigned Dkc3FrameCounter(void) {
  return (unsigned)snes_frame_counter;
}
