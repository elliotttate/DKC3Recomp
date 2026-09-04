#include "dkc3_hdma.h"
#include "dkc3_video.h"
#include "snes/ws_shadow.h"

#include <stdio.h>
#include <string.h>

static void WriteWord(uint8_t *data, uint16_t address, uint16_t value) {
  data[address] = (uint8_t)value;
  data[(uint16_t)(address + 1u)] = (uint8_t)(value >> 8);
}

/* Synthetic guest memory for the HDMA dry run: one 64 KiB WRAM bank. */
static uint8_t s_fake_wram[0x10000];

static const uint8_t *FakeHdmaPointer(void *context, uint32_t address) {
  (void)context;
  if ((address >> 16) != 0x7eu)
    return NULL;
  return s_fake_wram + (address & 0xffffu);
}

static bool FakeHdmaReadable(void *context, const uint8_t *pointer,
                             size_t length) {
  (void)context;
  if (!pointer || pointer < s_fake_wram)
    return false;
  const size_t offset = (size_t)(pointer - s_fake_wram);
  return offset <= sizeof s_fake_wram && length <= sizeof s_fake_wram - offset;
}

static Dkc3VideoMetatileFill GridClassifier(void *context,
                                            uint32_t metatile_x,
                                            uint32_t metatile_y) {
  const char *const *grid = (const char *const *)context;
  if (metatile_y >= 4 || metatile_x >= 8)
    return kDkc3VideoMetatileUnknown;
  switch (grid[metatile_y][metatile_x]) {
    case '.': return kDkc3VideoMetatileEmpty;
    case '#': return kDkc3VideoMetatileFull;
    case '/': return kDkc3VideoMetatilePartial;
    default: return kDkc3VideoMetatileUnknown;
  }
}

/* Character data for the plane tests: every character but 0 is opaque at
 * character base 0, so a cell is blank when its entry is 0 or names
 * character 0. */
static void OpaqueCharacters(uint16_t *vram) {
  for (unsigned word = 16; word < 0x4000u; word++)
    vram[word] = 0x1111;
}

static bool CheckMargins(uint16_t camera_x, uint16_t maximum_scroll_x,
                         int expected_bias, int expected_left,
                         int expected_right) {
  int bias = 99, left = 99, right = 99;
  Dkc3VideoPresentationMargins(camera_x, maximum_scroll_x,
                               &bias, &left, &right);
  if (bias != expected_bias || left != expected_left ||
      right != expected_right) {
    fprintf(stderr,
            "FAIL: presentation margins for camera %u max %u: "
            "got bias %d left %d right %d, expected %d %d %d\n",
            camera_x, maximum_scroll_x, bias, left, right,
            expected_bias, expected_left, expected_right);
    return false;
  }
  return true;
}

int main(void) {
  uint16_t placement_cells[3] = {0xffff, 0xffff, 0xffff};
  Dkc3VideoSetWidescreen(false);
  if (Dkc3VideoIsWidescreen() ||
      Dkc3VideoGetAspect() != kDkc3VideoAspectNative ||
      Dkc3VideoWidth() != kDkc3VideoNativeWidth ||
      Dkc3VideoExtra() != 0 ||
      Dkc3VideoExpandCullLeft(0x20) != 0x20 ||
      Dkc3VideoExpandCullSpan(0x140) != 0x140 ||
      Dkc3VideoPlacementScanCells(2, 64, placement_cells) != 1 ||
      placement_cells[0] != 2 ||
      Dkc3VideoPromoteOamXHigh(0x0120) != 0x0120 ||
      !CheckMargins(0x0100, 0x0800, 0, 0, 0) ||
      Dkc3VideoPixelCount() !=
          (size_t)kDkc3VideoNativeWidth * kDkc3VideoHeight) {
    fprintf(stderr, "FAIL: native video geometry\n");
    return 1;
  }

  Dkc3VideoAspect parsed_aspect = kDkc3VideoAspectNative;
  if (kDkc3VideoAspectNative != 0 || kDkc3VideoAspect16x10 != 1 ||
      kDkc3VideoAspect16x9 != 2 || kDkc3VideoAspect21x9 != 3 ||
      !Dkc3VideoAspectFromName("16:10", &parsed_aspect) ||
      parsed_aspect != kDkc3VideoAspect16x10 ||
      strcmp(Dkc3VideoAspectName(parsed_aspect), "16:10") != 0 ||
      !Dkc3VideoAspectFromName("21:9", &parsed_aspect) ||
      parsed_aspect != kDkc3VideoAspect21x9 ||
      strcmp(Dkc3VideoAspectName(parsed_aspect), "21:9") != 0 ||
      Dkc3VideoAspectFromName("wide", &parsed_aspect)) {
    fprintf(stderr, "FAIL: aspect vocabulary\n");
    return 1;
  }

  Dkc3VideoSetAspect(kDkc3VideoAspect16x10);
  if (!Dkc3VideoIsWidescreen() ||
      Dkc3VideoGetAspect() != kDkc3VideoAspect16x10 ||
      Dkc3VideoWidth() != 308 ||
      Dkc3VideoExtra() != kDkc3Video16x10Extra ||
      Dkc3VideoPlacementScanCells(2, 64, placement_cells) != 3 ||
      placement_cells[0] != 2 || placement_cells[1] != 0 ||
      placement_cells[2] != 4 ||
      Dkc3VideoPlacementScanCells(0, 64, placement_cells) != 2 ||
      placement_cells[0] != 0 || placement_cells[1] != 2 ||
      Dkc3VideoPlacementScanCells(126, 64, placement_cells) != 2 ||
      placement_cells[0] != 126 || placement_cells[1] != 124) {
    fprintf(stderr, "FAIL: 16:10 video geometry\n");
    return 1;
  }
  {
    Dkc3VideoEdgePolicy policy = kDkc3VideoEdgePolicyCount;
    if (Dkc3VideoGetEdgePolicy() != kDkc3VideoEdgeGlide ||
        !Dkc3VideoEdgePolicyFromName("bars", &policy) ||
        policy != kDkc3VideoEdgeBars ||
        !Dkc3VideoEdgePolicyFromName("shift", &policy) ||
        policy != kDkc3VideoEdgeShift ||
        !Dkc3VideoEdgePolicyFromName("glide", &policy) ||
        policy != kDkc3VideoEdgeGlide ||
        strcmp(Dkc3VideoEdgePolicyName(kDkc3VideoEdgeGlide), "glide") != 0 ||
        !Dkc3VideoEdgePolicyFromName("reflect", &policy) ||
        policy != kDkc3VideoEdgeReflect ||
        Dkc3VideoEdgePolicyFromName("wide", &policy) ||
        strcmp(Dkc3VideoEdgePolicyName(kDkc3VideoEdgeBars), "bars") != 0 ||
        strcmp(Dkc3VideoEdgePolicyName(kDkc3VideoEdgeShift), "shift") != 0) {
      fprintf(stderr, "FAIL: edge policy vocabulary\n");
      return 1;
    }
  }
  /* reflect: the view stays locked to the camera and both margins remain
   * visible everywhere; the terrain decoder mirrors columns at a wall. */
  /* reflect: the view is locked to the camera with full margins. */
  Dkc3VideoSetEdgePolicy(kDkc3VideoEdgeReflect);
  if (!CheckMargins(0x0100, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0800, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0100, 0x0120, 0, 26, 26) ||
      !CheckMargins(0x0100, 0x00ff, 0, 26, 26)) {
    fprintf(stderr, "FAIL: 16:10 reflect margins\n");
    return 1;
  }
  {
    uint32_t source = 99;
    bool mirror = true;
    if (Dkc3VideoResolveEdgeTile(40, 0x0100, &source, &mirror) != 0 ||
        source != 8 || mirror ||
        Dkc3VideoResolveEdgeTile(31, 0x0100, &source, &mirror) != 1 ||
        source != 0 || !mirror ||
        Dkc3VideoResolveEdgeTile(26, 0x0100, &source, &mirror) != 1 ||
        source != 5 || !mirror ||
        Dkc3VideoResolveEdgeTile(0, 0x0100, &source, &mirror) != 1 ||
        source != 31 || !mirror ||
        Dkc3VideoResolveEdgeTile(64, 0x0100, &source, &mirror) != 1 ||
        source != 31 || !mirror ||
        Dkc3VideoResolveEdgeTile(70, 0x0100, &source, &mirror) != 1 ||
        source != 25 || !mirror ||
        Dkc3VideoResolveEdgeTile(100, 0x0100, &source, &mirror) != -1 ||
        Dkc3VideoResolveEdgeTile(63, 0x0100, &source, &mirror) != 0 ||
        source != 31 || mirror ||
        Dkc3VideoResolveEdgeTile(31, 0x0100, NULL, &mirror) != -1 ||
        !Dkc3VideoMarginLeavesAuthoredExtent(0x0100, 0x0800) ||
        !Dkc3VideoMarginLeavesAuthoredExtent(0x0110, 0x0800) ||
        Dkc3VideoMarginLeavesAuthoredExtent(0x011a, 0x0800) ||
        Dkc3VideoMarginLeavesAuthoredExtent(0x0300, 0x0800) ||
        !Dkc3VideoMarginLeavesAuthoredExtent(0x07f0, 0x0800) ||
        Dkc3VideoMarginLeavesAuthoredExtent(0x0300, 0x00ff)) {
      fprintf(stderr, "FAIL: reflected edge tiles\n");
      return 1;
    }
  }
  {
    /* A map-derived west bound: the glide treats it as the west wall and
     * releases the slide with travel away from it; the margins for a bias
     * the host chose follow the same bounds. */
    Dkc3VideoSetEdgePolicy(kDkc3VideoEdgeGlide);
    int bias = 99, left = 99, right = 99;
    Dkc3VideoPresentationMarginsBounded(608, 608, 3072, &bias, &left, &right);
    if (bias != 26 || left != 26 || right != 26) {
      fprintf(stderr, "FAIL: glide at a map-derived bound (%d %d %d)\n", bias,
              left, right);
      return 1;
    }
    Dkc3VideoPresentationMarginsBounded(640, 608, 3072, &bias, &left, &right);
    if (bias != 22) {
      fprintf(stderr, "FAIL: glide release from a map-derived bound (%d)\n",
              bias);
      return 1;
    }
    Dkc3VideoPresentationMarginsBounded(608, 0x0100, 3072, &bias, &left,
                                        &right);
    if (bias != 0) {
      fprintf(stderr, "FAIL: no bound, no slide (%d)\n", bias);
      return 1;
    }
    Dkc3VideoMarginsForBias(608, 608, 3072, 10, &left, &right);
    if (left != 10 || right != 26) {
      fprintf(stderr, "FAIL: margins for a chosen bias (%d %d)\n", left,
              right);
      return 1;
    }
    /* The hold itself over a synthetic map: two empty columns beside the
     * window's first column hold; one row with a cell in them does not. */
    static const char *const held[4] = {
        "..######", "..######", "..######", "..######",
    };
    static const char *const open[4] = {
        "..######", ".+######", "..######", "..######",
    };
    uint32_t hold_column = 0;
    if (!Dkc3VideoHoldWest(GridClassifier, (void *)held, 2, 3, 0, 3,
                           &hold_column) ||
        hold_column != 2 ||
        Dkc3VideoHoldWest(GridClassifier, (void *)open, 2, 3, 0, 3,
                          &hold_column) ||
        Dkc3VideoHoldWest(GridClassifier, (void *)held, 0, 3, 0, 3,
                          &hold_column) ||
        Dkc3VideoHoldWest(NULL, (void *)held, 2, 3, 0, 3, &hold_column)) {
      fprintf(stderr, "FAIL: map-derived west hold\n");
      return 1;
    }
  }
  {
    /* Structural wall continuation over a synthetic metatile map. */
    static const char *const grid[4] = {
        "..##..##",   /* row 0: empty target, thick wall two cells east */
        "..##..##",   /* row 1: corroborates row 0 */
        "../#..##",   /* row 2: a partial cell between target and wall */
        "..#/..##",   /* row 3: full cell but thin, no corroborating row */
    };
    uint32_t source = 99;
    if (!Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                           1, 3, 0, &source) ||
        source != 2 ||
        !Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                           0, 3, 1, &source) ||
        source != 2 ||
        Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                          1, 3, 2, &source) ||
        Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                          1, 3, 3, &source) ||
        Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                          2, 3, 0, &source) ||
        Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                          4, 3, 0, &source) ||
        !Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)grid, true,
                                           5, 3, 0, &source) ||
        source != 3 ||
        /* east: a target beside the wall itself still resolves to it. */
        !Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)grid, true,
                                           4, 3, 0, &source) ||
        source != 3 ||
        Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)grid, true,
                                          5, 6, 0, &source) ||
        Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)grid, false,
                                          1, 1, 0, &source) ||
        Dkc3VideoFindStructuralWallSource(NULL, (void *)grid, false,
                                          1, 3, 0, &source)) {
      fprintf(stderr, "FAIL: structural wall source\n");
      return 1;
    }
  }
  {
    /* A one-cell mast is two thick only on the row a sign hangs beside it;
     * neither adjacent row proves a wall there, so nothing continues. */
    static const char *const mast[4] = {
        "..#.....",   /* row 0: thin mast */
        "..##....",   /* row 1: the sign makes it two thick here only */
        "..#.....",   /* row 2: thin again */
        "..#.....",   /* row 3 */
    };
    uint32_t source = 99;
    if (Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)mast, false,
                                          0, 4, 1, &source) ||
        Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)mast, false,
                                          1, 4, 1, &source) ||
        Dkc3VideoFindStructuralWallSource(GridClassifier, (void *)mast, false,
                                          0, 4, 0, &source)) {
      fprintf(stderr, "FAIL: one-cell mast with a sign continued\n");
      return 1;
    }
    static uint16_t vram[0x8000];
    memset(vram, 0, sizeof vram);
    vram[0x1000 + 16 * 2 + 5] = 0x0100;  /* character 2 has one opaque row */
    if (!Dkc3VideoCharacterIsTransparent(vram, 0x8000u, 0x1000, 0x0001) ||
        Dkc3VideoCharacterIsTransparent(vram, 0x8000u, 0x1000, 0x0002) ||
        Dkc3VideoCharacterIsTransparent(vram, 0x8000u, 0x1000, 0x4002) ||
        !Dkc3VideoCharacterIsTransparent(vram, 0x8000u, 0x1000, 0xc003) ||
        Dkc3VideoCharacterIsTransparent(vram, 0x100u, 0x1000, 0x0001)) {
      fprintf(stderr, "FAIL: character transparency\n");
      return 1;
    }
  }
  /* bars: the view stays locked to the camera and the visible margin
   * shrinks to the authored extent. */
  Dkc3VideoSetEdgePolicy(kDkc3VideoEdgeBars);
  if (!CheckMargins(0x0100, 0x0800, 0, 0, 26) ||
      !CheckMargins(0x0110, 0x0800, 0, 16, 26) ||
      !CheckMargins(0x0300, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x07f8, 0x0800, 0, 26, 8) ||
      !CheckMargins(0x0100, 0x00ff, 0, 26, 26) ||
      Dkc3VideoMarginLeavesAuthoredExtent(0x0100, 0x0800)) {
    fprintf(stderr, "FAIL: 16:10 bars margins\n");
    return 1;
  }
  {
    uint32_t source = 99;
    bool mirror = true;
    if (Dkc3VideoResolveEdgeTile(31, 0x0100, &source, &mirror) != -1 ||
        mirror ||
        Dkc3VideoResolveEdgeTile(64, 0x0100, &source, &mirror) != -1 ||
        Dkc3VideoResolveEdgeTile(40, 0x0100, &source, &mirror) != 0 ||
        source != 8) {
      fprintf(stderr, "FAIL: bars edge tiles\n");
      return 1;
    }
  }
  /* shift: presentation geometry at the fixed level endpoints. A room that
   * can absorb both margins biases the viewport inward and shows full
   * margins; a narrower room is centered and each visible margin is clamped
   * to the authored extent; an unknown bound keeps the symmetric margin; a
   * camera outside the authored range is never shifted by more than one
   * margin. */
  Dkc3VideoSetEdgePolicy(kDkc3VideoEdgeShift);
  if (!CheckMargins(0x0100, 0x0800, 26, 26, 26) ||
      !CheckMargins(0x0110, 0x0800, 10, 26, 26) ||
      !CheckMargins(0x0200, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0800, 0x0800, -26, 26, 26) ||
      !CheckMargins(0x07f0, 0x0800, -10, 26, 26) ||
      !CheckMargins(0x0100, 0x0120, 16, 16, 16) ||
      !CheckMargins(0x0120, 0x0120, -16, 16, 16) ||
      !CheckMargins(0x0100, 0x0100, 0, 0, 0) ||
      !CheckMargins(0x0100, 0x00ff, 0, 26, 26) ||
      !CheckMargins(0x0900, 0x0800, -26, 26, 0) ||
      !CheckMargins(0x0080, 0x0800, 26, 0, 26)) {
    fprintf(stderr, "FAIL: 16:10 presentation margins\n");
    return 1;
  }
  /* glide: the same pins as shift, but the inward shift is released one
   * pixel per eight pixels of camera travel from each wall, so the view is
   * centered 208 pixels in at 16:10 and the frame never leaves the level. */
  Dkc3VideoSetEdgePolicy(kDkc3VideoEdgeGlide);
  if (!CheckMargins(0x0100, 0x0800, 26, 26, 26) ||
      !CheckMargins(0x0107, 0x0800, 26, 26, 26) ||
      !CheckMargins(0x0108, 0x0800, 25, 26, 26) ||
      !CheckMargins(0x0100 + 104, 0x0800, 13, 26, 26) ||
      !CheckMargins(0x0100 + 207, 0x0800, 1, 26, 26) ||
      !CheckMargins(0x0100 + 208, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0400, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0800 - 208, 0x0800, 0, 26, 26) ||
      !CheckMargins(0x0800 - 8, 0x0800, -25, 26, 26) ||
      !CheckMargins(0x0800, 0x0800, -26, 26, 26) ||
      !CheckMargins(0x0100, 0x0120, 16, 16, 16) ||
      !CheckMargins(0x0120, 0x0120, -16, 16, 16) ||
      !CheckMargins(0x0100, 0x0100, 0, 0, 0) ||
      !CheckMargins(0x0100, 0x00ff, 0, 26, 26) ||
      !CheckMargins(0x0900, 0x0800, -26, 26, 0) ||
      !CheckMargins(0x0080, 0x0800, 26, 0, 26)) {
    fprintf(stderr, "FAIL: 16:10 glide margins\n");
    return 1;
  }
  {
    const int lhs = Dkc3VideoWidth() * 7 * 10;
    const int rhs = kDkc3VideoHeight * 6 * 16;
    const int error = lhs > rhs ? lhs - rhs : rhs - lhs;
    if (error > 7 * 10) {
      fprintf(stderr,
              "FAIL: 16:10 geometry is not within one source pixel\n");
      return 1;
    }
  }

  Dkc3VideoSetWidescreen(true);
  Dkc3VideoSetEdgePolicy(kDkc3VideoEdgeShift);
  if (Dkc3VideoTerrainReady() ||
      Dkc3VideoExpandCullLeft(0x20) != 0x20 ||
      Dkc3VideoExpandCullSpan(0x140) != 0x140 ||
      Dkc3VideoPromoteOamXHigh(0x0120) != 0x0120) {
    fprintf(stderr, "FAIL: object bounds widened before terrain was ready\n");
    return 1;
  }
  Dkc3VideoSetTerrainReady(true);
  if (!Dkc3VideoIsWidescreen() ||
      !Dkc3VideoTerrainReady() ||
      Dkc3VideoWidth() != kDkc3VideoWidescreenWidth ||
      Dkc3VideoExtra() != kDkc3VideoWidescreenExtra ||
      Dkc3VideoExpandCullLeft(0x20) != 0x4b ||
      Dkc3VideoExpandCullSpan(0x140) != 0x196 ||
      Dkc3VideoExpandCullLeft(0x30) != 0x5b ||
      Dkc3VideoExpandCullSpan(0x160) != 0x1b6 ||
      /* A presentation bias moves the presented window: the left slack
       * shrinks by a positive bias (none left at the left wall) and grows
       * by a negative one; the span keeps both margins either way. */
      (Dkc3VideoSetPresentationBias(43),
       Dkc3VideoExpandCullLeft(0x20) != 0x20 ||
       Dkc3VideoExpandCullSpan(0x140) != 0x196 ||
       Dkc3VideoPresentationBias() != 43) ||
      (Dkc3VideoSetPresentationBias(-43),
       Dkc3VideoExpandCullLeft(0x20) != 0x76 ||
       Dkc3VideoExpandCullSpan(0x140) != 0x196) ||
      (Dkc3VideoSetPresentationBias(0),
       Dkc3VideoExpandCullLeft(0x20) != 0x4b) ||
      Dkc3VideoPromoteOamXHigh(0x00ff) != 0x00ff ||
      Dkc3VideoPromoteOamXHigh(0x0100) != 0x8100 ||
      Dkc3VideoPromoteOamXHigh(0x012a) != 0x812a ||
      Dkc3VideoPromoteOamXHigh(0xffff) != 0xffff ||
      !Dkc3VideoTileTouchesWidescreenMargin(93, 752) ||
      Dkc3VideoTileTouchesWidescreenMargin(94, 752) ||
      Dkc3VideoTileTouchesWidescreenMargin(125, 752) ||
      !Dkc3VideoTileTouchesWidescreenMargin(126, 752) ||
      !Dkc3VideoTileTouchesWidescreenMargin(93, 755) ||
      !Dkc3VideoTileTouchesWidescreenMargin(126, 755) ||
      !CheckMargins(0x0100, 0x0800, 43, 43, 43) ||
      !CheckMargins(0x0800, 0x0800, -43, 43, 43) ||
      !CheckMargins(0x0100, 0x0140, 32, 32, 32) ||
      Dkc3VideoPixelCount() !=
          (size_t)kDkc3VideoWidescreenWidth * kDkc3VideoHeight) {
    fprintf(stderr, "FAIL: widescreen video geometry\n");
    return 1;
  }
  Dkc3VideoSetEdgePolicy(kDkc3VideoEdgeReflect);

  {
    /* Murky Mill puts a 32-column BG3 light plane on the main screen and
     * changes its window through HDMA. It must render physically wide instead
     * of repeating the clipped native line. Ordinary bounded layers and
     * non-Mode-1 scenes retain the repeat/centered paths. */
    const uint8_t maps[4] = {0x79, 0x74, 0x70, 0x00};
    if (Dkc3VideoPhysicalWindowedLayerMask(
            0x09, maps, 0x04, 0x13, 0x04, 0x00) != 0x04 ||
        Dkc3VideoPhysicalWindowedLayerMask(
            0x09, maps, 0x00, 0x04, 0x00, 0x04) != 0x04 ||
        Dkc3VideoPhysicalWindowedLayerMask(
            0x09, maps, 0x04, 0x13, 0x00, 0x00) != 0x00 ||
        Dkc3VideoPhysicalWindowedLayerMask(
            0x09, maps, 0x04, 0x13, 0x01, 0x00) != 0x00 ||
        Dkc3VideoPhysicalWindowedLayerMask(
            0x08, maps, 0x04, 0x13, 0x04, 0x00) != 0x00 ||
        Dkc3VideoPhysicalWindowedLayerMask(
            0x09, NULL, 0x04, 0x13, 0x04, 0x00) != 0x00) {
      fprintf(stderr, "FAIL: physical windowed layer selection\n");
      return 1;
    }
    const uint8_t wide_bg3_maps[4] = {0x79, 0x74, 0x71, 0x00};
    if (Dkc3VideoPhysicalWindowedLayerMask(
            0x09, wide_bg3_maps, 0x04, 0x13, 0x04, 0x00) != 0x00) {
      fprintf(stderr, "FAIL: wide map selected as bounded window layer\n");
      return 1;
    }
  }

  /* Display aspect = source width * (7/6 PAR) / source height. */
  const int lhs = kDkc3VideoWidescreenWidth * 7 * 9;
  const int rhs = kDkc3VideoHeight * 6 * 16;
  int error = lhs > rhs ? lhs - rhs : rhs - lhs;
  if (error > 7 * 9) {
    fprintf(stderr,
            "FAIL: widescreen geometry is not within one pixel of 16:9\n");
    return 1;
  }

  Dkc3VideoSetAspect(kDkc3VideoAspect21x9);
  Dkc3VideoSetTerrainReady(true);
  Dkc3VideoSetEdgePolicy(kDkc3VideoEdgeReflect);
  if (!Dkc3VideoIsWidescreen() ||
      Dkc3VideoGetAspect() != kDkc3VideoAspect21x9 ||
      Dkc3VideoWidth() != kDkc3Video21x9Width ||
      Dkc3VideoWidth() != kDkc3VideoMaximumWidth ||
      Dkc3VideoExtra() != kDkc3Video21x9Extra ||
      kDkc3Video21x9Extra != 95 ||
      Dkc3VideoExpandCullLeft(0x20) != 0x7f ||
      Dkc3VideoExpandCullSpan(0x140) != 0x1fe ||
      !CheckMargins(0x0100, 0x0800, 0, 95, 95) ||
      !CheckMargins(0x0800, 0x0800, 0, 95, 95) ||
      Dkc3VideoPixelCount() !=
          (size_t)kDkc3Video21x9Width * kDkc3VideoHeight) {
    fprintf(stderr, "FAIL: 21:9 video geometry\n");
    return 1;
  }
  {
    /* Exact 21:9 needs 448 source pixels; the safe symmetric OAM limit is
     * two pixels narrower, one per side. */
    const int ultrawide_lhs = Dkc3VideoWidth() * 7 * 9;
    const int ultrawide_rhs = kDkc3VideoHeight * 6 * 21;
    const int ultrawide_error = ultrawide_rhs - ultrawide_lhs;
    if (ultrawide_error != 2 * 7 * 9) {
      fprintf(stderr, "FAIL: 21:9 safe-width geometry\n");
      return 1;
    }
  }

  {
    /* HDMA dry run against the preserved lava balloon-band geometry:
     * channel 0 moves BG2HOFS to 521 for lines 1-165 then restores 18;
     * channel 1 switches TM/TS to $13/$00 for lines 1-122, then $13/$04;
     * channel 2 is an indirect single-line BG1VOFS write; channel 3 points
     * outside readable memory and must terminate harmlessly. */
    memset(s_fake_wram, 0, sizeof s_fake_wram);
    /* A line count above 127 would set the repeat bit, so the cartridge
     * writes 165 lines as two entries. */
    uint8_t *table0 = s_fake_wram + 0x2000;
    table0[0] = 127; table0[1] = 0x09; table0[2] = 0x02;
    table0[3] = 38;  table0[4] = 0x09; table0[5] = 0x02;
    table0[6] = 59;  table0[7] = 0x12; table0[8] = 0x00;
    table0[9] = 0;
    uint8_t *table1 = s_fake_wram + 0x2100;
    table1[0] = 122; table1[1] = 0x13; table1[2] = 0x00;
    table1[3] = 59;  table1[4] = 0x13; table1[5] = 0x04;
    table1[6] = 0;
    uint8_t *table2 = s_fake_wram + 0x2200;
    table2[0] = 0x81; table2[1] = 0x00; table2[2] = 0x23;
    table2[3] = 0;
    s_fake_wram[0x2300] = 0x1f;
    s_fake_wram[0x2301] = 0x01;

    Dkc3HdmaChannelConfig channels[8];
    memset(channels, 0, sizeof channels);
    channels[0].active = true;
    channels[0].b_address = 0x0f;
    channels[0].mode = 2;
    channels[0].table_address = 0x7e2000;
    channels[1].active = true;
    channels[1].b_address = 0x2c;
    channels[1].mode = 1;
    channels[1].table_address = 0x7e2100;
    channels[2].active = true;
    channels[2].indirect = true;
    channels[2].indirect_bank = 0x7e;
    channels[2].b_address = 0x0e;
    channels[2].mode = 2;
    channels[2].table_address = 0x7e2200;
    channels[3].active = true;
    channels[3].b_address = 0x11;
    channels[3].mode = 2;
    channels[3].table_address = 0x7effff;
    channels[4].active = true;
    channels[4].b_address = 0x13;
    channels[4].mode = 2;
    channels[4].table_address = 0xc02000;

    Dkc3HdmaFrameState start;
    memset(&start, 0, sizeof start);
    start.h_scroll[0] = 643; start.h_scroll[1] = 18; start.h_scroll[2] = 834;
    start.v_scroll[0] = 442; start.v_scroll[1] = 495; start.v_scroll[2] = 175;
    start.main_layers = 0x13;
    start.sub_layers = 0x04;
    const Dkc3HdmaMemory memory = {FakeHdmaPointer, FakeHdmaReadable, NULL};
    static Dkc3HdmaBands bands;
    Dkc3HdmaScanBands(channels, &start, &memory, &bands);
    const Dkc3HdmaBand *first = Dkc3HdmaBandForLine(&bands, 1);
    const Dkc3HdmaBand *middle = Dkc3HdmaBandForLine(&bands, 150);
    const Dkc3HdmaBand *last = Dkc3HdmaBandForLine(&bands, 224);
    if (bands.count != 3 || !first || !middle || !last ||
        first->first_line != 1 || first->last_line != 122 ||
        middle->first_line != 123 || middle->last_line != 165 ||
        last->first_line != 166 || last->last_line != 224 ||
        first->h_scroll[1] != 521 || middle->h_scroll[1] != 521 ||
        last->h_scroll[1] != 18 ||
        first->main_layers != 0x13 || first->sub_layers != 0x00 ||
        middle->sub_layers != 0x04 || last->sub_layers != 0x04 ||
        first->v_scroll[0] != 0x011f || last->v_scroll[0] != 0x011f ||
        first->h_scroll[0] != 643 || first->h_scroll[2] != 834 ||
        Dkc3HdmaBandForLine(&bands, 0) != first ||
        Dkc3HdmaBandForLine(&bands, 122) != first ||
        Dkc3HdmaBandForLine(&bands, 123) != middle ||
        Dkc3HdmaBandForLine(&bands, 166) != last) {
      fprintf(stderr, "FAIL: HDMA band dry run (%d bands)\n", bands.count);
      return 1;
    }
    /* Without any active channel the whole frame is one band. */
    memset(channels, 0, sizeof channels);
    Dkc3HdmaScanBands(channels, &start, &memory, &bands);
    if (bands.count != 1 || bands.band[0].first_line != 1 ||
        bands.band[0].last_line != 224 ||
        bands.band[0].h_scroll[1] != 18 ||
        Dkc3HdmaBandForLine(&bands, 100) != &bands.band[0]) {
      fprintf(stderr, "FAIL: HDMA dry run without channels\n");
      return 1;
    }
    Dkc3HdmaScanBands(channels, NULL, &memory, &bands);
    if (bands.count != 0 || Dkc3HdmaBandForLine(&bands, 1) != NULL) {
      fprintf(stderr, "FAIL: HDMA dry run rejects a missing frame state\n");
      return 1;
    }
    /* Tilemap base and size writes (BGnSC) split bands as well: a lava
     * stage swaps the BG1 and BG2 maps at the lava line. */
    uint8_t *table3 = s_fake_wram + 0x2400;
    table3[0] = 100; table3[1] = 0x79; table3[2] = 0x67;
    table3[3] = 1;   table3[4] = 0x67; table3[5] = 0x79;
    table3[6] = 0;
    memset(channels, 0, sizeof channels);
    channels[0].active = true;
    channels[0].b_address = 0x07;
    channels[0].mode = 1;
    channels[0].table_address = 0x7e2400;
    start.bg_sc[0] = 0x67; start.bg_sc[1] = 0x79; start.bg_sc[2] = 0x74;
    Dkc3HdmaScanBands(channels, &start, &memory, &bands);
    if (bands.count != 2 || bands.band[0].first_line != 1 ||
        bands.band[0].last_line != 100 || bands.band[1].first_line != 101 ||
        bands.band[0].bg_sc[0] != 0x79 || bands.band[0].bg_sc[1] != 0x67 ||
        bands.band[1].bg_sc[0] != 0x67 || bands.band[1].bg_sc[1] != 0x79 ||
        bands.band[0].bg_sc[2] != 0x74 || bands.band[1].bg_sc[2] != 0x74) {
      fprintf(stderr, "FAIL: HDMA dry run tilemap base bands (%d bands)\n",
              bands.count);
      return 1;
    }
  }

  {
    uint8_t pages[4];
    if (Dkc3VideoTilemapPages(0x67, pages) != 4 || pages[0] != 25 ||
        pages[1] != 26 || pages[2] != 27 || pages[3] != 28 ||
        Dkc3VideoTilemapPages(0x79, pages) != 2 || pages[0] != 30 ||
        pages[1] != 31 ||
        Dkc3VideoTilemapPages(0x74, pages) != 1 || pages[0] != 29 ||
        Dkc3VideoTilemapPages(0x0a, pages) != 2 || pages[0] != 2 ||
        pages[1] != 3 || Dkc3VideoTilemapPages(0x67, NULL) != 0) {
      fprintf(stderr, "FAIL: tilemap page enumeration\n");
      return 1;
    }
  }

  {
    /* Terrain phase selection: the frame-start register stands when it is
     * at the camera phase; a band pair at the camera phase replaces one
     * that is not, the widest such pair winning. */
    static Dkc3HdmaBands bands;
    memset(&bands, 0, sizeof bands);
    bands.count = 3;
    bands.band[0].first_line = 1;   bands.band[0].last_line = 20;
    bands.band[0].h_scroll[0] = 700; bands.band[0].v_scroll[0] = 100;
    bands.band[1].first_line = 21;  bands.band[1].last_line = 200;
    bands.band[1].h_scroll[0] = 428; bands.band[1].v_scroll[0] = 593;
    bands.band[2].first_line = 201; bands.band[2].last_line = 224;
    bands.band[2].h_scroll[0] = 429; bands.band[2].v_scroll[0] = 592;
    uint16_t h = 0, v = 0;
    if (!Dkc3VideoSelectTerrainPhase(&bands, 0, 426, 80, 430, 5713, &h, &v) ||
        h != 428 || v != 593) {
      fprintf(stderr, "FAIL: terrain phase from the widest camera-phase band\n");
      return 1;
    }
    if (Dkc3VideoSelectTerrainPhase(&bands, 0, 427, 592, 430, 5713, &h, &v) ||
        h != 427 || v != 592) {
      fprintf(stderr, "FAIL: frame-start terrain phase at the camera kept\n");
      return 1;
    }
    if (Dkc3VideoSelectTerrainPhase(&bands, 1, 426, 80, 430, 5713, &h, &v) ||
        h != 426 || v != 80 ||
        Dkc3VideoSelectTerrainPhase(NULL, 0, 426, 80, 430, 5713, &h, &v) ||
        Dkc3VideoSelectTerrainPhase(&bands, 0, 426, 80, 430, 5713, NULL, &v)) {
      fprintf(stderr, "FAIL: terrain phase without a matching band\n");
      return 1;
    }
    /* Toxic Tower on Rattly: the register a frame behind the camera, every
     * band five pixels behind it through HDMA. Nothing is at the camera
     * phase; the scroll covering the whole frame is adopted. A dominant
     * scroll far from the camera (a parallax layer) is not. */
    bands.count = 2;
    bands.band[0].first_line = 1;   bands.band[0].last_line = 220;
    bands.band[0].h_scroll[0] = 512; bands.band[0].v_scroll[0] = 361;
    bands.band[1].first_line = 221; bands.band[1].last_line = 224;
    bands.band[1].h_scroll[0] = 513; bands.band[1].v_scroll[0] = 361;
    if (!Dkc3VideoSelectTerrainPhase(&bands, 0, 512, 356, 512, 9582, &h, &v) ||
        h != 512 || v != 361) {
      fprintf(stderr, "FAIL: terrain phase from the frame's dominant scroll\n");
      return 1;
    }
    bands.band[0].v_scroll[0] = 100;
    bands.band[1].v_scroll[0] = 100;
    if (Dkc3VideoSelectTerrainPhase(&bands, 0, 512, 356, 512, 9582, &h, &v) ||
        h != 512 || v != 356) {
      fprintf(stderr, "FAIL: a far dominant scroll was taken for the phase\n");
      return 1;
    }
  }

  {
    uint32_t mirrored = 0;
    /* West: the native edge is source tile 24; the tile beside it mirrors
     * the edge tile, the next one the tile behind it. */
    if (!Dkc3VideoMirrorSourceTileAcrossEdge(23, 24, false, &mirrored) ||
        mirrored != 24 ||
        !Dkc3VideoMirrorSourceTileAcrossEdge(20, 24, false, &mirrored) ||
        mirrored != 27 ||
        Dkc3VideoMirrorSourceTileAcrossEdge(24, 24, false, &mirrored) ||
        Dkc3VideoMirrorSourceTileAcrossEdge(30, 24, false, &mirrored)) {
      fprintf(stderr, "FAIL: west held-wall mirror\n");
      return 1;
    }
    /* East: the native edge is source tile 55. */
    if (!Dkc3VideoMirrorSourceTileAcrossEdge(56, 55, true, &mirrored) ||
        mirrored != 55 ||
        !Dkc3VideoMirrorSourceTileAcrossEdge(59, 55, true, &mirrored) ||
        mirrored != 52 ||
        Dkc3VideoMirrorSourceTileAcrossEdge(55, 55, true, &mirrored) ||
        Dkc3VideoMirrorSourceTileAcrossEdge(50, 55, true, &mirrored) ||
        Dkc3VideoMirrorSourceTileAcrossEdge(56, 55, true, NULL)) {
      fprintf(stderr, "FAIL: east held-wall mirror\n");
      return 1;
    }
  }

  {
    /* Broken rows of a wrapping plane: Toxic Tower's BG2 keeps its wall
     * across all 64 columns (rows 0-11) but its cornice strip across 55
     * (rows 24-27); scattered decoration (row 30, six cells) is not a
     * strip and is never broken. */
    static uint16_t vram[0x8000];
    memset(vram, 0, sizeof vram);
    OpaqueCharacters(vram);
    for (unsigned row = 0; row < 12; row++)
      for (unsigned column = 0; column < 64; column++)
        vram[0x7800 + (column >= 32 ? 0x400 : 0) + (row << 5) +
             (column & 31)] = (uint16_t)(0x200 + (row * 7 + column) % 61);
    for (unsigned row = 24; row < 28; row++)
      for (unsigned column = 0; column < 55; column++)
        vram[0x7800 + (column >= 32 ? 0x400 : 0) + (row << 5) +
             (column & 31)] = (uint16_t)(0x300 + (row * 5 + column) % 53);
    for (unsigned column = 0; column < 64; column += 11)
      vram[0x7800 + (column >= 32 ? 0x400 : 0) + (30 << 5) + (column & 31)] =
          0x0777;
    if (!Dkc3VideoTilemapWrapsAuthored(vram, 0x8000, 0x79, 0) ||
        Dkc3VideoTilemapBrokenRows(vram, 0x8000, 0x79, 0) !=
            ((uint64_t)0xf << 24) ||
        Dkc3VideoTilemapBrokenRows(vram, 0x8000, 0x78, 0) != 0 ||
        Dkc3VideoTilemapBrokenRows(NULL, 0x8000, 0x79, 0) != 0) {
      fprintf(stderr, "FAIL: broken rows of a wrapping plane\n");
      return 1;
    }
    /* Toxic Tower's top-of-screen wall map fills the cells beyond its
     * slanted edge with $8000, a flip flag over character 0: blank for
     * the wrap test, not painting. With every row's last nine columns so
     * filled the map no longer wraps authored. */
    for (unsigned row = 0; row < 12; row++)
      for (unsigned column = 55; column < 64; column++)
        vram[0x7800 + 0x400 + (row << 5) + (column & 31)] = 0x8000;
    if (Dkc3VideoTilemapWrapsAuthored(vram, 0x8000, 0x79, 0) ||
        Dkc3VideoTilemapBrokenRows(vram, 0x8000, 0x79, 0) !=
            (((uint64_t)0xf << 24) | 0xfffu)) {
      fprintf(stderr, "FAIL: a flip flag over character 0 was taken for painting\n");
      return 1;
    }
  }
  {
    /* Wrap authoring of a 64-column plane. */
    static uint16_t vram[0x8000];
    memset(vram, 0, sizeof vram);
    OpaqueCharacters(vram);
    /* A 96-pixel cabin wall (period 12 columns) on a 64x32 map at $7800. */
    for (unsigned row = 0; row < 12; row++)
      for (unsigned column = 0; column < 64; column++)
        vram[0x7800 + (column >= 32 ? 0x400 : 0) + (row << 5) +
             (column & 31)] = (uint16_t)(0x100 + column % 12);
    if (Dkc3VideoTilemapWrapsAuthored(vram, 0x8000, 0x79, 0)) {
      fprintf(stderr, "FAIL: a 96-pixel wall was taken for a wrapping plane\n");
      return 1;
    }
    /* A sparse rock plane on a 64x64 map at $6400: scattered entries in
     * rows 16-30 reaching both map edges, then far spikes repeating every
     * 32 columns in rows 40-63. */
    for (unsigned row = 16; row <= 30; row++)
      for (unsigned column = 0; column < 64; column++)
        if ((column * 7 + row * 3) % 5 == 0 || column == row - 16 ||
            column == 63 - (row - 16))
          vram[0x6400 + (column >= 32 ? 0x400 : 0) +
               (row >= 32 ? 0x800 : 0) + ((row & 31) << 5) +
               (column & 31)] = (uint16_t)(0x200 + column + row);
    for (unsigned row = 40; row < 64; row++)
      for (unsigned column = 0; column < 64; column++)
        vram[0x6400 + (column >= 32 ? 0x400 : 0) + 0x800 +
             ((row & 31) << 5) + (column & 31)] =
            (uint16_t)(0x300 + column % 32 + row);
    if (!Dkc3VideoTilemapWrapsAuthored(vram, 0x8000, 0x67, 0)) {
      fprintf(stderr, "FAIL: the rock plane was not taken for a wrapping plane\n");
      return 1;
    }
    /* The same plane with its last nine columns blank in every row is a
     * backdrop narrower than its allocation. */
    for (unsigned row = 0; row < 64; row++)
      for (unsigned column = 55; column < 64; column++)
        vram[0x6400 + 0x400 + (row >= 32 ? 0x800 : 0) + ((row & 31) << 5) +
             (column & 31)] = 0;
    if (Dkc3VideoTilemapWrapsAuthored(vram, 0x8000, 0x67, 0)) {
      fprintf(stderr, "FAIL: a blank edge strip was taken for a wrapping plane\n");
      return 1;
    }
    /* An object plane: a block in one page, the other page blank. */
    memset(vram, 0, sizeof vram);
    OpaqueCharacters(vram);
    for (unsigned row = 0; row < 13; row++)
      for (unsigned column = 0; column < 32; column++)
        vram[0x6800 + (row << 5) + column] = (uint16_t)(0x500 + row);
    if (!Dkc3VideoTilemapIsObjectPlane(vram, 0x8000, 0x69, 0) ||
        Dkc3VideoTilemapWrapsAuthored(vram, 0x8000, 0x69, 0)) {
      fprintf(stderr, "FAIL: a one-page block was not an object plane\n");
      return 1;
    }
    vram[0x6800 + 0x400 + 5] = 0x123;
    if (Dkc3VideoTilemapIsObjectPlane(vram, 0x8000, 0x69, 0)) {
      fprintf(stderr, "FAIL: both pages populated was an object plane\n");
      return 1;
    }
    memset(vram, 0, sizeof vram);
    OpaqueCharacters(vram);
    for (unsigned column = 0; column < 40; column++)
      vram[0x6800 + column] = 0x77;
    if (Dkc3VideoTilemapIsObjectPlane(vram, 0x8000, 0x69, 0) ||
        Dkc3VideoTilemapIsObjectPlane(vram, 0x8000, 0x68, 0) ||
        Dkc3VideoTilemapIsObjectPlane(NULL, 0x8000, 0x69, 0)) {
      fprintf(stderr, "FAIL: a sparse or bounded map was an object plane\n");
      return 1;
    }
    memset(vram, 0, sizeof vram);
    /* Empty, 32-column, and missing maps are never planes. */
    if (Dkc3VideoTilemapWrapsAuthored(vram, 0x8000, 0x45, 0) ||
        Dkc3VideoTilemapWrapsAuthored(vram, 0x8000, 0x74, 0) ||
        Dkc3VideoTilemapWrapsAuthored(vram, 0x4000, 0x67, 0) ||
        Dkc3VideoTilemapWrapsAuthored(NULL, 0x8000, 0x67, 0)) {
      fprintf(stderr, "FAIL: an empty or bounded map was taken for a plane\n");
      return 1;
    }
  }

  if (Dkc3VideoUnwrapPpuScroll(0x0010, 0x03f8) != 0x0410 ||
      Dkc3VideoUnwrapPpuScroll(0x03f0, 0x0408) != 0x03f0 ||
      Dkc3VideoUnwrapPpuScroll(0x0123, 0x0520) != 0x0523 ||
      Dkc3VideoUnwrapPpuScroll(0x0123, 0x0120) != 0x0123) {
    fprintf(stderr, "FAIL: PPU scroll phase unwrapping\n");
    return 1;
  }
  if (Dkc3VideoTerrainShadowY(0x00cb, 0x01cd) != 0x00cb ||
      Dkc3VideoTerrainShadowY(0x002f, 0x0130) != 0x002f ||
      Dkc3VideoTerrainShadowY(0x03f8, 0x04f8) != 0x03f8 ||
      Dkc3VideoTerrainShadowY(0x0004, 0x0204) != 0x0404 ||
      (Dkc3VideoTerrainShadowY(0x0004, 0x0204) >> 3) !=
          Dkc3VideoLevelSourceTileY(0x0004, 0x0204, 0)) {
    fprintf(stderr, "FAIL: terrain shadow Y follows rendered source phase\n");
    return 1;
  }
  if (Dkc3VideoTerrainShadowX(0x02fd, 0x0300) != 0x02fd ||
      Dkc3VideoTerrainShadowX(0x0001, 0x03ff) != 0x0401 ||
      Dkc3VideoTerrainShadowX(0x03ff, 0x0401) != 0x03ff) {
    fprintf(stderr, "FAIL: terrain shadow X follows rendered source phase\n");
    return 1;
  }

  /*
   * At this observed NMI boundary WRAM camera Y is one pixel into the next
   * 8-pixel row while the rendered PPU phase is one pixel behind it. Mixing
   * their integer rows produced (5 - 6) & 31 == 31 and decoded row 37 into
   * source row 5. The rendered phase must remain authoritative.
   */
  if (Dkc3VideoLevelSourceTileY(0x002f, 0x0130, 0) != 5 ||
      Dkc3VideoLevelSourceTileY(0x002f, 0x0130, 1) != 6 ||
      Dkc3VideoLevelSourceTileY(0x0029, 0x012a, 0) != 5 ||
      Dkc3VideoLevelSourceTileY(0x03f8, 0x04f8, 2) != 129 ||
      Dkc3VideoLevelSourceTileY(0x009b, 0x069c, 0) != 275 ||
      Dkc3VideoLevelSourceTileY(0x009b, 0x069c, 1) != 276 ||
      Dkc3VideoLevelSourceTileY(0x003d, 0x0246, 0) != 135 ||
      Dkc3VideoLevelSourceTileY(0x003d, 0x0246, 1) != 136) {
    fprintf(stderr, "FAIL: level source Y follows rendered PPU phase\n");
    return 1;
  }
  {
    /* At Topsail Trouble's lower camera limit the rendered 10-bit phase
     * unwraps to world tile 512. The complete 224-pixel viewport reaches
     * tile 540, so the shared world-keyed shadow must retain that range. */
    const uint32_t topsail_top =
        Dkc3VideoLevelSourceTileY(0x0007, 0x0f08, 0);
    const uint32_t topsail_bottom =
        Dkc3VideoLevelSourceTileY(0x0007, 0x0f08, 28);
    if (topsail_top != 512 || topsail_bottom != 540 ||
        topsail_bottom >= kWsShadowYTiles) {
      fprintf(stderr, "FAIL: terrain shadow cannot retain Topsail bottom\n");
      return 1;
    }
  }
  if (Dkc3VideoLevelMapTileY(0x00a2, 0x01a3, 0) != 20 ||
      Dkc3VideoLevelMapTileY(0x002e, 0x012f, 0) != 5 ||
      Dkc3VideoLevelMapTileY(0x00ff, 0x0100, 0) != 0x1fff ||
      Dkc3VideoLevelMapTileY(0x00ff, 0x0100, 1) != 0 ||
      Dkc3VideoLevelMapTileY(0x009b, 0x069c, 0) != 179 ||
      Dkc3VideoLevelMapTileY(0x003d, 0x0246, 0) != 39) {
    fprintf(stderr, "FAIL: rolling level-map source page selection\n");
    return 1;
  }
  {
    uint8_t bank[0x10000];
    uint16_t tile = 0;
    memset(bank, 0, sizeof bank);

    /* World tile (5,10) -> metatile (1,2), sub-tile (1,2). */
    WriteWord(bank, 0x1024, 0x0003);
    WriteWord(bank, 0x2072, 0x1234);
    if (!Dkc3VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutHorizontal, 5, 10, &tile) ||
        tile != 0x1234) {
      fprintf(stderr, "FAIL: normal level metatile decode\n");
      return 1;
    }
    /* Metatile ids and the map's adjacency: a row-major map four columns
     * wide (row_bytes 8) and three rows tall at 0x3000. Id 3 is followed
     * eastward by 5 twice and by 7 once; 7 is never continued east. */
    {
      uint16_t ids[4], counts[4], id = 0, entry = 0;
      static const uint16_t map[12] = {
          0x0003, 0x0005, 0x0003, 0x0007,
          0x4003, 0x0005, 0x0007, 0x0000,
          0x0009, 0x0003, 0x0000, 0x0000,
      };
      for (unsigned n = 0; n < 12u; n++)
        WriteWord(bank, (uint16_t)(0x3000 + n * 2u), map[n]);
      if (!Dkc3VideoReadLevelMetatile(bank, sizeof bank, 0x3000,
                                      kDkc3VideoLevelLayoutVertical, 8, 0, 1,
                                      &id) ||
          id != 3 ||
          !Dkc3VideoReadLevelMetatile(bank, sizeof bank, 0x1000,
                                      kDkc3VideoLevelLayoutHorizontal, 0, 1,
                                      2, &id) ||
          id != 3 ||
          Dkc3VideoReadLevelMetatile(NULL, sizeof bank, 0x3000,
                                     kDkc3VideoLevelLayoutVertical, 8, 0, 1,
                                     &id) ||
          Dkc3VideoMetatileNeighbours(bank, sizeof bank, 0x3000, 0x3018,
                                      kDkc3VideoLevelLayoutVertical, 8, 3,
                                      true, ids, counts, 4) != 2 ||
          ids[0] != 5 || counts[0] != 2 || ids[1] != 7 || counts[1] != 1 ||
          Dkc3VideoMetatileNeighbours(bank, sizeof bank, 0x3000, 0x3018,
                                      kDkc3VideoLevelLayoutVertical, 8, 7,
                                      true, ids, counts, 4) != 0 ||
          Dkc3VideoMetatileNeighbours(bank, sizeof bank, 0x3000, 0x3018,
                                      kDkc3VideoLevelLayoutVertical, 8, 5,
                                      false, ids, counts, 4) != 1 ||
          ids[0] != 3 || counts[0] != 2 ||
          Dkc3VideoMetatileNeighbours(bank, sizeof bank, 0x3000, 0x3018,
                                      kDkc3VideoLevelLayoutVertical, 8, 3,
                                      true, ids, counts, 0) != 0) {
        fprintf(stderr, "FAIL: level metatile neighbours\n");
        return 1;
      }
      /* Decoding by id matches the map decode of the same metatile, and
       * a horizontal flip mirrors the sub-column. */
      if (!Dkc3VideoDecodeMetatileEntry(bank, sizeof bank, 0x2000, 0x0003, 1,
                                        2, &entry) ||
          entry != 0x1234 ||
          !Dkc3VideoDecodeMetatileEntry(bank, sizeof bank, 0x2000, 0x4003, 2,
                                        2, &entry) ||
          entry != (0x1234 ^ 0x4000) ||
          Dkc3VideoDecodeMetatileEntry(bank, sizeof bank, 0x2000, 0x0003, 4,
                                       2, &entry)) {
        fprintf(stderr, "FAIL: metatile entry decode by id\n");
        return 1;
      }
    }
    /* Row-major maps: metatile (1,2) sits at column 2 + row 2 * stride. */
    WriteWord(bank, (uint16_t)(0x1000 + 2 + 2 * 160), 0x0003);
    WriteWord(bank, (uint16_t)(0x1000 + 2 + 2 * 192), 0x0003);
    if (!Dkc3VideoDecodeLevelTileRowMajor(
            bank, sizeof bank, 0x1000, 0x2000, 160, 5, 10, &tile) ||
        tile != 0x1234 ||
        !Dkc3VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutSquare, 5, 10, &tile) ||
        tile != 0x1234 ||
        Dkc3VideoDecodeLevelTileRowMajor(
            bank, sizeof bank, 0x1000, 0x2000, 0, 5, 10, &tile) ||
        Dkc3VideoDecodeLevelTileRowMajor(
            NULL, sizeof bank, 0x1000, 0x2000, 160, 5, 10, &tile) ||
        Dkc3VideoLevelLayoutRowBytes(kDkc3VideoLevelLayoutSquare) != 192 ||
        Dkc3VideoLevelLayoutRowBytes(kDkc3VideoLevelLayoutShipHold) != 160 ||
        Dkc3VideoLevelLayoutRowBytes(kDkc3VideoLevelLayoutVertical) != 64 ||
        Dkc3VideoLevelLayoutRowBytes(kDkc3VideoLevelLayoutNarrowVertical) != 32 ||
        Dkc3VideoLevelLayoutRowBytes(kDkc3VideoLevelLayoutHorizontal) != 0) {
      fprintf(stderr, "FAIL: row-major level metatile decode\n");
      return 1;
    }

    /* Horizontal flip selects sub-x 2 and applies the tilemap flip bit. */
    WriteWord(bank, 0x1024, 0x4003);
    WriteWord(bank, 0x2074, 0x0234);
    if (!Dkc3VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutHorizontal, 5, 10, &tile) ||
        tile != 0x4234) {
      fprintf(stderr, "FAIL: horizontally flipped metatile decode\n");
      return 1;
    }

    /* Both flips select sub-tile (2,1) and preserve both output flips. */
    WriteWord(bank, 0x1024, 0xc003);
    WriteWord(bank, 0x206c, 0x0567);
    if (!Dkc3VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutHorizontal, 5, 10, &tile) ||
        tile != 0xc567) {
      fprintf(stderr, "FAIL: doubly flipped metatile decode\n");
      return 1;
    }

    if (Dkc3VideoDecodeLevelTile(
            NULL, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutHorizontal, 5, 10, &tile) ||
        Dkc3VideoDecodeLevelTile(
            bank, sizeof bank - 1u, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutHorizontal, 5, 10, &tile) ||
        Dkc3VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutHorizontal, 0x2000, 10, &tile) ||
        Dkc3VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutUnknown, 5, 10, &tile)) {
      fprintf(stderr, "FAIL: invalid level source was accepted\n");
      return 1;
    }

    /* Vertical stages store the same metatiles in row-major order. */
    WriteWord(bank, 0x1082, 0x0003);
    WriteWord(bank, 0x2072, 0x3456);
    if (!Dkc3VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutVertical, 5, 10, &tile) ||
        tile != 0x3456) {
      fprintf(stderr, "FAIL: vertical level metatile decode\n");
      return 1;
    }

    /* Square stages store 48 metatiles per row (0x60 bytes). */
    WriteWord(bank, 0x1182, 0x0003);
    WriteWord(bank, 0x2072, 0x4567);
    if (!Dkc3VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutSquare, 5, 10, &tile) ||
        tile != 0x4567) {
      fprintf(stderr, "FAIL: square level metatile decode\n");
      return 1;
    }

    /* Parrot Chute Panic stores 16 metatiles per $20-byte row. */
    WriteWord(bank, 0x1042, 0x0003);
    WriteWord(bank, 0x2072, 0x5678);
    if (!Dkc3VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutNarrowVertical, 5, 10, &tile) ||
        tile != 0x5678) {
      fprintf(stderr, "FAIL: narrow vertical level metatile decode\n");
      return 1;
    }

    /* Ship-hold maps store 80 metatiles per $a0-byte row. */
    WriteWord(bank, 0x1142, 0x0003);
    WriteWord(bank, 0x2072, 0x6789);
    if (!Dkc3VideoDecodeLevelTile(
            bank, sizeof bank, 0x1000, 0x2000,
            kDkc3VideoLevelLayoutShipHold, 5, 10, &tile) ||
        tile != 0x6789) {
      fprintf(stderr, "FAIL: ship-hold level metatile decode\n");
      return 1;
    }
  }

  {
    uint16_t vram[0x8000];
    uint16_t tile = 0xffff;
    const uint16_t base = 0x2000;
    const uint16_t transparent_tile = 0x0123;
    for (size_t word = 0; word < sizeof vram / sizeof vram[0]; word++)
      vram[word] = 0xffff;
    for (unsigned word = 0; word < 16u; word++)
      vram[(base + transparent_tile * 16u + word) & 0x7fffu] = 0;

    if (!Dkc3VideoFindTransparent4bppTile(
            vram, sizeof vram / sizeof vram[0], base, &tile) ||
        tile != transparent_tile) {
      fprintf(stderr, "FAIL: transparent 4bpp tile lookup\n");
      return 1;
    }
    if (Dkc3VideoFindTransparent4bppTile(
            NULL, sizeof vram / sizeof vram[0], base, &tile) ||
        Dkc3VideoFindTransparent4bppTile(vram, 16, base, &tile)) {
      fprintf(stderr, "FAIL: invalid transparent tile source was accepted\n");
      return 1;
    }
    if (!Dkc3VideoIsTransparentTileEntry(transparent_tile, tile) ||
        !Dkc3VideoIsTransparentTileEntry(0xfc00u | transparent_tile, tile) ||
        Dkc3VideoIsTransparentTileEntry(transparent_tile + 1u, tile)) {
      fprintf(stderr, "FAIL: transparent tile-entry classification\n");
      return 1;
    }
  }

  {
    uint16_t vram[0x8000];
    uint16_t tile = 0xffff;
    const uint16_t base = 0x3000;
    const uint16_t transparent_tile = 0x013e;
    for (size_t word = 0; word < sizeof vram / sizeof vram[0]; word++)
      vram[word] = 0xffff;
    for (unsigned word = 0; word < 8u; word++)
      vram[(base + transparent_tile * 8u + word) & 0x7fffu] = 0;
    if (!Dkc3VideoFindTransparent2bppTile(
            vram, sizeof vram / sizeof vram[0], base, &tile) ||
        tile != transparent_tile) {
      fprintf(stderr, "FAIL: transparent 2bpp tile lookup\n");
      return 1;
    }
    if (Dkc3VideoFindTransparent2bppTile(
            NULL, sizeof vram / sizeof vram[0], base, &tile) ||
        Dkc3VideoFindTransparent2bppTile(vram, 8, base, &tile)) {
      fprintf(stderr, "FAIL: invalid transparent 2bpp source was accepted\n");
      return 1;
    }
  }

  Dkc3VideoSetWidescreen(false);
  if (Dkc3VideoTerrainReady()) {
    fprintf(stderr, "FAIL: native mode retained widescreen terrain state\n");
    return 1;
  }

  puts("DKC3 video geometry tests passed");
  return 0;
}
