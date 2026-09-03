#include "dkc3_game.h"
#include "dkc3_hdma.h"
#include "dkc3_video.h"
#include "dkc3_facts.h"

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
};
static Dkc3HdmaBands s_frame_bands;
static uint8_t s_band_policy[2][kDkc3HdmaMaxBands];
/* The terrain layer's rendered scroll phase for the frame
 * (Dkc3VideoSelectTerrainPhase): the key for the world store, the prefill's
 * source rows, and the band classification. */
static uint16_t s_terrain_phase_h;
static uint16_t s_terrain_phase_v;
static bool s_terrain_phase_from_band;
static int s_plane_band_count[2];
/* The west hold the last prefill found in the level map (Dkc3VideoHoldWest):
 * the world x of the authored edge the player is held at, and the
 * presented bias, which moves at most one pixel per frame toward the
 * glide's target so a hold that appears or vanishes never snaps the
 * picture. Both reset with the world store. */
static bool s_hold_west_valid;
static uint32_t s_hold_west_x;
static bool s_bias_valid;
static int s_bias_presented;
/* Frames since the presented bias was reset: within the first few the
 * bias snaps to its target rather than gliding, so a level that starts at
 * a hold opens already slid, as one that starts at the map's first page
 * does. */
enum { kDkc3BiasSettleFrames = 4 };
static uint32_t s_bias_frames;
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

static bool Dkc3VramPageStatic(unsigned page) {
  return page < kDkc3VramPages && s_page_traveled[page];
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
  bool all_static = true;
  for (unsigned index = 0; index < count; index++) {
    if (!Dkc3VramPageStatic(pages[index]))
      all_static = false;
    const uint32_t stamp = WsShadowVramPageWriteFrame(pages[index]);
    if (stamp > newest)
      newest = stamp;
  }
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
  s_resume_pc = interp_bridge_lle_resume_pc();
  if (g_cpu.master_cycles < s_next_frame_master) {
    g_cpu.master_cycles = s_next_frame_master;
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
  }
  s_next_frame_master += kDkc3NtscFrameMasterClocks;
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

static void Dkc3OnStateLoaded(uint32_t version) {
  (void)version;
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
  Dkc3VideoSetTerrainReady(false);
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
  if (actual != expected_entry) {
    /* DKC3_PREFILL_DUMP=<frame>: print every cell the world store and the
     * map decode disagree on for that host frame. */
    static long dump_frame = -2;
    if (dump_frame == -2) {
      const char *text = getenv("DKC3_PREFILL_DUMP");
      dump_frame = text && *text ? atol(text) : -1;
    }
    if (dump_frame >= 0 && snes_frame_counter >= dump_frame &&
        snes_frame_counter < dump_frame + 1)
      fprintf(stderr, "prefill_mismatch layer=%d x=%u y=%u actual=%04x expected=%04x margin=%d\n",
              layer, (unsigned)world_tile_x, (unsigned)world_tile_y, actual,
              expected_entry, margin ? 1 : 0);
    return;
  }
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
  if (layout == kDkc3VideoLevelLayoutHorizontal || row_bytes == 0)
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
  size_t decoded = 0;
  size_t expected = 0;
  unsigned row_bytes = 0;
  if (layout != kDkc3VideoLevelLayoutHorizontal) {
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
      if (edge == 0 && outside_cartridge && cartridge_first_tile >= 32u) {
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
      /* At the horizontal $xxff->$xx00 vertical page boundary the first
       * visible tile row is supplied by the live rolling map, not by a full
       * decompressed source row. The native row is one pixel high and the
       * retained map can still contain a previous ship section there. Never
       * seed those unobserved side cells from that stale row. */
      if (layout == kDkc3VideoLevelLayoutHorizontal && row <= 0) {
        const uint32_t tile_pixel_x = tile_x << 3;
        if (tile_pixel_x < cartridge_x ||
            tile_pixel_x >= cartridge_x + kDkc3VideoNativeWidth) {
          WsShadowForceTile(
              terrain_layer, tile_x, shadow_tile_y, transparent_tile);
        }
        decoded++;
        Dkc3RecordTerrainPrefillTile(
            terrain_layer, tile_x, shadow_tile_y, transparent_tile, margin);
        continue;
      }
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
    if (have_owner && alias_layer[layer] &&
        (layer_mask & (uint8_t)(1u << layer))) {
      /* The view shares the owner's keys. The renderer adds this layer's own
       * per-line scroll delta, so a band that leads the frame anchor by a
       * few pixels still resolves the exact world cell. */
      WsShadowSetEntryAlias(layer, terrain_layer,
                            owner_world_x, owner_world_y,
                            owner_scroll_x, owner_scroll_y);
      WsShadowSetNativeViewportInset(
          layer, presentation_bias < 0 ? -presentation_bias : 0,
          presentation_bias > 0 ? presentation_bias : 0);
    } else {
      WsShadowClearEntryAlias(layer);
    }
  }

  WsShadowFrame(g_ppu);
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
    const bool wide = (wide_layer_mask & (uint8_t)(1u << layer)) != 0;
    for (int index = 0; index < bands->count; index++) {
      const Dkc3HdmaBand *band = &bands->band[index];
      const bool world =
          have_owner && wide &&
          Dkc3VideoScrollAtTerrainPhase(
              band->h_scroll[layer], band->v_scroll[layer],
              terrain_h, terrain_v);
      const bool plane =
          !world && wide &&
          Dkc3BandShowsStaticPlane(band->bg_sc[layer], stream_base, layer,
                                   band->v_scroll[layer], band->first_line,
                                   band->last_line);
      policy[layer][index] = world ? kDkc3BandPolicyWorld
                             : plane ? kDkc3BandPolicyPlane
                                     : kDkc3BandPolicyRepeat;
      if (plane)
        s_plane_band_count[layer]++;
      if (world && layer != terrain_layer)
        alias_layer[layer] = true;
    }
  }
  /* DKC3_BAND_DUMP=1: print every scanline band's scrolls, tilemap
   * register, and the policy chosen for each wide layer (W world, P plane,
   * R repeat, - not wide) to stderr each frame. A band whose policy
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

void Dkc3DrawPpuFrame(void) {
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
  uint8_t wide_layer_mask =
      Dkc3VideoIsWidescreen()
          ? Dkc3VideoPpuWideLayerMask(g_ppu->bgmode, g_ppu->bgXsc,
                                      g_ppu->screenEnabled[0],
                                      g_ppu->screenEnabled[1])
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
  const bool extend_world = wide_layer_mask != 0 && !name_card;
  int presentation_bias = 0;
  bool band_policies_active = false;
  bool blank_margins = false;
  /* Reset host presentation latches before deriving the current frame. A
   * prior gameplay scene must not leave a physically wide BG3 enabled on a
   * bounded title, menu, or unsupported layout. */
  PpuSetWidescreenLayerMask(g_ppu, 0);
  PpuSetWidescreenBg3Widen(g_ppu, 0);
  PpuSetWidescreenPresentationXBias(g_ppu, 0);
  s_frame_bands.count = 0;
  if (extend_world) {
    const int extra = Dkc3VideoExtra();
    PpuSetExtraSpace(g_ppu, (uint8_t)extra);
    const uint16_t camera_x = Dkc3ReadWram16(kDkc3WramCameraX);
    const uint16_t maximum_scroll_x = Dkc3MaximumScrollX();
    int bias = 0;
    int left_margin = extra;
    int right_margin = extra;
    /* The level's west bound: the map's first page unless the last prefill
     * found the player held at the authored world's edge with nothing
     * beside it (Dkc3VideoHoldWest). The bias then moves at most one pixel
     * a frame toward the glide's target. */
    const uint16_t minimum_scroll_x =
        s_hold_west_valid && s_hold_west_x > 0x100u &&
                s_hold_west_x <= camera_x
            ? (uint16_t)s_hold_west_x : 0x0100u;
    int wanted_bias = 0;
    Dkc3VideoPresentationMarginsBounded(camera_x, minimum_scroll_x,
                                        maximum_scroll_x, &wanted_bias,
                                        &left_margin, &right_margin);
    if (!s_bias_valid) {
      s_bias_presented = wanted_bias;
      s_bias_frames = 0;
    } else if (s_bias_frames < (uint32_t)kDkc3BiasSettleFrames) {
      s_bias_presented = wanted_bias;
    } else if (wanted_bias > s_bias_presented) {
      s_bias_presented++;
    } else if (wanted_bias < s_bias_presented) {
      s_bias_presented--;
    }
    s_bias_valid = true;
    if (s_bias_frames < 0xffffffffu)
      s_bias_frames++;
    bias = s_bias_presented;
    Dkc3VideoMarginsForBias(camera_x, minimum_scroll_x, maximum_scroll_x,
                            bias, &left_margin, &right_margin);
    const int terrain_layer = Dkc3VideoTerrainLayer(
        wide_layer_mask, g_ppu->bgXsc, Dkc3TerrainVramBase());
    /* The cartridge has already built this frame's HDMA tables. Read the
     * exact scanline geometry from them before drawing. */
    Dkc3ScanFrameBands(&s_frame_bands);
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
    Dkc3ClassifyBands(wide_layer_mask, terrain_layer, &s_frame_bands,
                      s_band_policy, alias_layer);
    /* WsShadow owns only BG1/BG2 terrain. Establish exact terrain readiness
     * before allowing any additional physical layer into the final render
     * mask; this keeps 64-column HUD/staging allocations fail-closed. */
    PpuSetWidescreenLayerMask(g_ppu, wide_layer_mask);
    const bool terrain_ready = Dkc3PrepareWidescreenShadow(
        wide_layer_mask, terrain_layer, layout, bias, alias_layer);
    presentation_bias = terrain_ready ? bias : 0;
    /* Nothing authored reaches the margins while the world is unproven (a
     * level intro's static picture, the first frames of a stream), and the
     * PPU would otherwise fill them with the backdrop color, which Barrel
     * Bayou's intro sets to pure blue. Show black there, as a bounded
     * screen does, never the backdrop. */
    blank_margins = !terrain_ready;
    PpuSetWidescreenPresentationXBias(g_ppu, presentation_bias);
    Dkc3VideoSetPresentationBias(presentation_bias);
    if (terrain_ready)
      PpuSetExtraSideSpace(g_ppu, left_margin, right_margin, 0);
    uint8_t physical_wide_mask =
        terrain_ready
            ? Dkc3VideoPhysicalWideLayerMask(
                  g_ppu->bgmode, g_ppu->bgXsc,
                  g_ppu->screenEnabled[0], g_ppu->screenEnabled[1])
            : 0;
    /* A mirrored margin at a level wall has no authored BG3 ring columns to
     * expose. Within one margin of a wall, a physical 64-column BG3 repeats
     * its rendered line like a bounded layer instead of reading the ring. */
    if (Dkc3VideoMarginLeavesAuthoredExtent(camera_x, maximum_scroll_x))
      physical_wide_mask = (uint8_t)(physical_wide_mask & ~0x04u);
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
     * phase, windows, and color-math participation. Rolling 64-column
     * layers are handled per scanline band below.
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
