#pragma once

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
} Dkc3TerrainPrefillStats;

const RtlGameInfo *Dkc3GameInfo(void);
/* The game's own frame counter, the value DKC3_PREFILL_DUMP keys on. */
unsigned Dkc3FrameCounter(void);
/* Diagnostic: the verified row offset of a layer's alias bands this frame. */
int Dkc3AliasOffsetRows(int layer);
/* Diagnostic: sub-screen lines the underwater tint saw, judged water, and tinted since the last call. */
void Dkc3TintLineCounters(unsigned *seen, unsigned *water, unsigned *matched);

void Dkc3BeginDrawing(uint8_t *pixels, size_t pitch);
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
