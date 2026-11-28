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
  {
    /* Synthetic templates: no cartridge data is part of this fixture. */
    static uint8_t waterfall_bank[0x10000];
    static uint16_t waterfall_vram[0x8000];
    for (unsigned index = 0; index < 10; index++)
      WriteWord(waterfall_bank, (uint16_t)(0x869b + index * 2),
                index == 1 ? 0x870e : 0x86ae);
    const uint8_t groups[] = {0, 1, 7, 2, 3, 4, 5, 6};
    memcpy(waterfall_bank + 0x86af, groups, sizeof groups);
    for (unsigned column = 0; column < 29; column++)
      for (unsigned row = 0; row < 32; row++)
        WriteWord(waterfall_bank,
                  (uint16_t)(0x87b6 + column * 64 + row * 2),
                  (uint16_t)(0x2000 + column * 32 + row));
    uint16_t column[32];
    const unsigned probes[] = {32, 35, 36, 39, 40, 43};
    const unsigned templates[] = {0, 0, 1, 4, 25, 28};
    for (unsigned probe = 0; probe < sizeof probes / sizeof probes[0]; probe++) {
      if (!Dkc3VideoDecodeWaterfallColumn(waterfall_bank, sizeof waterfall_bank,
                                            0x86ae, probes[probe], column))
        return fprintf(stderr, "FAIL: waterfall column decode\n"), 1;
      for (unsigned row = 0; row < 32; row++)
        if (column[row] != 0x2000 + templates[probe] * 32 + row)
          return fprintf(stderr, "FAIL: waterfall template addressing\n"), 1;
    }
    if (!Dkc3VideoUsesWaterfallColumns(0x206, 0x0d, 9, 0x71) ||
        !Dkc3VideoUsesWaterfallColumns(0x200, 0x17, 1, 0x71) ||
        !Dkc3VideoUsesWaterfallColumns(0x200, 0x1b, 1, 0x71) ||
        Dkc3VideoUsesWaterfallColumns(0x006, 0x0d, 9, 0x71) ||
        Dkc3VideoUsesWaterfallColumns(0x206, 0x0c, 9, 0x71) ||
        Dkc3VideoUsesWaterfallColumns(0x206, 0x0d, 7, 0x71) ||
        Dkc3VideoUsesWaterfallColumns(0x206, 0x0d, 9, 0x70))
      return fprintf(stderr, "FAIL: waterfall scene gate\n"), 1;
    if (Dkc3VideoDecodeWaterfallColumn(NULL, sizeof waterfall_bank,
                                         0x86ae, 32, column) ||
        Dkc3VideoDecodeWaterfallColumn(waterfall_bank, 0xffff, 0x86ae, 32, column) ||
        Dkc3VideoDecodeWaterfallColumn(waterfall_bank, sizeof waterfall_bank,
                                         0xffff, 32, column) ||
        Dkc3VideoDecodeWaterfallColumn(waterfall_bank, sizeof waterfall_bank,
                                         0x86ae, 31, column) ||
        Dkc3VideoDecodeWaterfallColumn(waterfall_bank, sizeof waterfall_bank,
                                         0x86ae, 32 + 96 * 4, column) ||
        Dkc3VideoDecodeWaterfallColumn(waterfall_bank, sizeof waterfall_bank,
                                         0x86ae, 32, NULL))
      return fprintf(stderr, "FAIL: waterfall decoder bounds\n"), 1;
    waterfall_bank[0x86af] = 8;
    if (Dkc3VideoDecodeWaterfallColumn(waterfall_bank, sizeof waterfall_bank,
                                         0x86ae, 32, column))
      return fprintf(stderr, "FAIL: invalid waterfall template accepted\n"), 1;
    waterfall_bank[0x86af] = 0;
    for (unsigned col = 32; col < 64; col++) {
      Dkc3VideoDecodeWaterfallColumn(waterfall_bank, sizeof waterfall_bank,
                                       0x86ae, col, column);
      for (unsigned row = 0; row < 32; row++)
        waterfall_vram[0x7400 + row * 32 + (col & 31)] = column[row];
    }
    unsigned matching = 0, total = 0;
    if (!Dkc3VideoVerifyWaterfallViewport(waterfall_bank, sizeof waterfall_bank,
          0x86ae, waterfall_vram, 0x8000, 0x100, &matching, &total) ||
        matching != 1024 || total != 1024)
      return fprintf(stderr, "FAIL: waterfall native viewport verification\n"), 1;
    waterfall_vram[0x7400] ^= 1;
    if (Dkc3VideoVerifyWaterfallViewport(waterfall_bank, sizeof waterfall_bank,
          0x86ae, waterfall_vram, 0x8000, 0x100, &matching, &total) ||
        matching != 1023 || total != 1024 ||
        !Dkc3VideoVerifyWaterfallViewport(waterfall_bank, sizeof waterfall_bank,
          0x86ae, waterfall_vram, 0x8000, 0x107, &matching, &total) ||
        matching != 992 || total != 992 ||
        Dkc3VideoVerifyWaterfallViewport(waterfall_bank, sizeof waterfall_bank,
          0x86ae, NULL, 0x8000, 0x100, NULL, NULL))
      return fprintf(stderr, "FAIL: waterfall mismatch and fine-scroll gates\n"), 1;
  }
  uint16_t placement_cells[kDkc3VideoPlacementScanMaxCells] = {
      0xffff, 0xffff, 0xffff, 0xffff, 0xffff};
  if (!Dkc3VideoUsesUnderwaterSubscreenTint(
          0x09, 0x04, 0x13, 0x04, 0x00, 0x00000300, 0x00, 0xff,
          0x02, 0x64, 0x0000) ||
      !Dkc3VideoUsesUnderwaterSubscreenTint(
          0x09, 0x05, 0x12, 0x04, 0x00, 0x00000300, 0x00, 0xff,
          0x02, 0x64, 0x0000) ||
      !Dkc3VideoUsesUnderwaterSubscreenTint(
          0x09, 0x15, 0x02, 0x04, 0x00, 0x00000300, 0x00, 0xff,
          0x02, 0x64, 0x0000) ||
      !Dkc3VideoUsesUnderwaterSubscreenTint(
          0x09, 0x17, 0x13, 0x04, 0x00, 0x00000300, 0x00, 0xff,
          0x02, 0x64, 0x0000) ||
      Dkc3VideoUsesUnderwaterSubscreenTint(
          0x09, 0x14, 0x03, 0x04, 0x00, 0x00000300, 0x00, 0xff,
          0x02, 0x64, 0x0000)) {
    fprintf(stderr, "FAIL: underwater subscreen tint signature\n");
    return 1;
  }
  /* The fill's premise is a native subscreen row with no transparent
   * pixel; the margins' transparent pixels then take the folded native
   * column. Margins: 0..3 and 260..263; native: 4..259, all opaque. */
  uint16_t subscreen[264];
  for (size_t index = 0; index < 264; index++)
    subscreen[index] = index >= 4 && index < 260 ? 0x0501 : 0x0500;
  subscreen[4] = 0x2101;
  subscreen[5] = 0x2202;
  subscreen[6] = 0x2303;
  subscreen[7] = 0x2404;
  subscreen[256] = 0x1101;
  subscreen[257] = 0x1202;
  subscreen[258] = 0x1303;
  subscreen[259] = 0x1404;
  subscreen[261] = 0x3305;
  subscreen[263] = 0x3406;
  if (Dkc3VideoRepeatTransparentSubscreenMargins(
          subscreen, 264, 4, 4, 4) != 6 ||
      subscreen[0] != 0x1101 || subscreen[1] != 0x1202 ||
      subscreen[2] != 0x1303 || subscreen[3] != 0x1404 ||
      subscreen[260] != 0x2101 || subscreen[261] != 0x3305 ||
      subscreen[262] != 0x2303 || subscreen[263] != 0x3406 ||
      Dkc3VideoRepeatTransparentSubscreenMargins(
          subscreen, 263, 4, 4, 4) != 0) {
    fprintf(stderr, "FAIL: transparent subscreen margin repeat\n");
    return 1;
  }
  /* A native row with a transparent subscreen pixel (open water with no
   * reflection behind it) is not the fill's case: a transparent margin
   * pixel there is authored and stays. */
  for (size_t index = 0; index < 264; index++)
    subscreen[index] = index >= 4 && index < 260 ? 0x0501 : 0x0500;
  subscreen[100] = 0x0500;
  if (Dkc3VideoRepeatTransparentSubscreenMargins(
          subscreen, 264, 4, 4, 4) != 0 ||
      subscreen[0] != 0x0500 || subscreen[263] != 0x0500) {
    fprintf(stderr, "FAIL: partially covered subscreen row must not fill\n");
    return 1;
  }
  Dkc3VideoSetWidescreen(false);
  if (Dkc3VideoIsWidescreen() ||
      Dkc3VideoGetAspect() != kDkc3VideoAspectNative ||
      Dkc3VideoWidth() != kDkc3VideoNativeWidth ||
      Dkc3VideoExtra() != 0 ||
      Dkc3VideoSpawnLead() != 0 ||
      Dkc3VideoSpriteCullLead() != 0 ||
      Dkc3VideoExpandCullLeft(0x20) != 0x20 ||
      Dkc3VideoExpandCullSpan(0x140) != 0x140 ||
      Dkc3VideoExpandSpawnLeft(0x20) != 0x20 ||
      Dkc3VideoExpandSpawnSpan(0x140) != 0x140 ||
      Dkc3VideoExpandSpriteCullLeft(0x30) != 0x30 ||
      Dkc3VideoExpandSpriteCullSpan(0x160) != 0x160 ||
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
  {
    uint8_t maps[4] = {0x7c, 0x74, 0, 0};
    uint16_t opt[0x400] = {0};
#define SNOW(in_level, level, mode, bases, hdma, words, count) \
    Dkc3VideoUsesSnowArenaPlanes(in_level, level, mode, maps, bases, hdma, words, count)
    if (!SNOW(true, 0x21, 2, 0x22, 0, opt, 0x400) ||
        SNOW(false, 0x21, 2, 0x22, 0, opt, 0x400) ||
        SNOW(true, 0x20, 2, 0x22, 0, opt, 0x400) ||
        SNOW(true, 0x21, 1, 0x22, 0, opt, 0x400) ||
        SNOW(true, 0x21, 0x12, 0x22, 0, opt, 0x400) ||
        SNOW(true, 0x21, 2, 0x23, 0, opt, 0x400) ||
        SNOW(true, 0x21, 2, 0x22, 1, opt, 0x400) ||
        SNOW(true, 0x21, 2, 0x22, 0, opt, 0x3ff) ||
        SNOW(true, 0x21, 2, 0x22, 0, NULL, 0x400) ||
        Dkc3VideoUsesSnowArenaPlanes(true, 0x21, 2, NULL, 0x22, 0, opt, 0x400))
      return fprintf(stderr, "FAIL: snow arena scene gate\n"), 1;
    for (int layer = 0; layer < 3; layer++) {
      maps[layer] ^= 1;
      if (SNOW(true, 0x21, 2, 0x22, 0, opt, 0x400))
        return fprintf(stderr, "FAIL: snow arena map gate\n"), 1;
      maps[layer] ^= 1;
    }
    for (unsigned word = 0; word < 0x400; word++) {
      for (unsigned bit = 0x2000; bit <= 0x4000; bit <<= 1) {
        opt[word] = (uint16_t)bit;
        if (SNOW(true, 0x21, 2, 0x22, 0, opt, 0x400))
          return fprintf(stderr, "FAIL: active snow arena OPT\n"), 1;
      }
      opt[word] = 0;
    }
#undef SNOW
  }
  {
    uint8_t maps[4] = {0x78, 0x71, 0x7c, 0};
    const int on_bg2[2] = {0, 1}, on_bg2x3[2] = {0, 3}, on_bg1[2] = {1, 0};
    const int none[2] = {0, 0}, partial[2] = {0, 2};
    if (Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, maps, on_bg2, 1) != 1 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 0x09, maps, on_bg2x3, 3) !=
            1 ||
        Dkc3VideoKaosArenaPlaneLayer(false, 0x23, 1, maps, on_bg2, 1) != -1 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x20, 1, maps, on_bg2, 1) != -1 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 2, maps, on_bg2, 1) != -1 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, maps, none, 1) != -1 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, maps, on_bg1, 1) != -1 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, maps, partial, 3) != -1 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, maps, none, 0) != -1 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, maps, NULL, 1) != -1 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, NULL, on_bg2, 1) != -1)
      return fprintf(stderr, "FAIL: KAOS arena scene gate\n"), 1;
    for (int layer = 0; layer < 3; layer++) {
      maps[layer] ^= 1;
      if (Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, maps, on_bg2, 1) != -1)
        return fprintf(stderr, "FAIL: KAOS arena map gate\n"), 1;
      maps[layer] ^= 1;
    }
    /* K. Roolenstein: the room moves to BG1; BG2 and BG3 share $7C. */
    uint8_t rool[4] = {0x71, 0x7c, 0x7c, 0};
    if (Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, rool, on_bg1, 1) != 0 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, rool, on_bg2, 1) != -1 ||
        Dkc3VideoKaosArenaPlaneLayer(true, 0x22, 1, rool, on_bg1, 1) != -1)
      return fprintf(stderr, "FAIL: K. Roolenstein arena gate\n"), 1;
    for (int layer = 0; layer < 3; layer++) {
      rool[layer] ^= 1;
      if (Dkc3VideoKaosArenaPlaneLayer(true, 0x23, 1, rool, on_bg1, 1) != -1)
        return fprintf(stderr, "FAIL: K. Roolenstein map gate\n"), 1;
      rool[layer] ^= 1;
    }
    int left = -1, right = -1;
    Dkc3VideoKaosArenaMargins(0x0070, 43, &left, &right);
    if (left != 43 || right != 43)
      return fprintf(stderr, "FAIL: KAOS arena interior margins\n"), 1;
    Dkc3VideoKaosArenaMargins(0x0008, 95, &left, &right);
    if (left != 8 || right != 95)
      return fprintf(stderr, "FAIL: KAOS arena west margin\n"), 1;
    Dkc3VideoKaosArenaMargins(0x00f8, 95, &left, &right);
    if (left != 95 || right != 8)
      return fprintf(stderr, "FAIL: KAOS arena east margin\n"), 1;
    Dkc3VideoKaosArenaMargins(0x0120, 43, &left, &right);
    if (left != 43 || right != 0)
      return fprintf(stderr, "FAIL: KAOS arena wrapped native window\n"), 1;
    /* Body map starting left of the screen (scroll 103): a direct entry
     * of other lines, then a repeat entry whose second pair is the body's. */
    const uint8_t left_table[] = {0x20, 5, 255, 0x82, 5, 255, 0, 153, 0};
    uint8_t rl = 0, rr = 0;
    if (!Dkc3VideoKaosBodyWindow(left_table, sizeof left_table, 103, &rl,
                                 &rr, &left, &right) ||
        rl != 0 || rr != 153 || left != -102 || right != 153)
      return fprintf(stderr, "FAIL: KAOS body window left extent\n"), 1;
    /* Body map starting inside the screen (scroll 250). */
    const uint8_t right_table[] = {0x40, 6, 255, 0};
    if (!Dkc3VideoKaosBodyWindow(right_table, sizeof right_table, 0x3fa, &rl,
                                 &rr, &left, &right) ||
        rl != 6 || rr != 255 || left != 6 || right != 261)
      return fprintf(stderr, "FAIL: KAOS body window right extent\n"), 1;
    /* No consistent pair, a zero scroll, a truncated table, and a null
     * table are all rejected. */
    const uint8_t other_table[] = {0x40, 5, 255, 0x10, 0, 255, 0};
    if (Dkc3VideoKaosBodyWindow(other_table, sizeof other_table, 103, &rl,
                                &rr, &left, &right) ||
        Dkc3VideoKaosBodyWindow(right_table, sizeof right_table, 0x400, &rl,
                                &rr, &left, &right) ||
        Dkc3VideoKaosBodyWindow(left_table, 4, 103, &rl, &rr, &left,
                                &right) ||
        Dkc3VideoKaosBodyWindow(NULL, 8, 103, &rl, &rr, &left, &right))
      return fprintf(stderr, "FAIL: KAOS body window rejection\n"), 1;
    /* K. Roolenstein room windows. Scroll 248: both left edges pinned at 0
     * (the west wall is off screen), right edges room-anchored. */
    if (!Dkc3VideoKaosRoomWindow(0, 188, 248, kDkc3KaosRoomWindowWest,
                                 kDkc3KaosRoomWindow1East, &left, &right) ||
        left != 75 - 248 || right != 188)
      return fprintf(stderr, "FAIL: KAOS room window pinned left\n"), 1;
    /* Scroll 41: the wall is on screen, the east end past it. */
    if (!Dkc3VideoKaosRoomWindow(34, 255, 41, kDkc3KaosRoomWindowWest,
                                 kDkc3KaosRoomWindow2East, &left, &right) ||
        left != 34 || right != 348 - 41)
      return fprintf(stderr, "FAIL: KAOS room window pinned right\n"), 1;
    /* Within two pixels of a trailing scroll is accepted as written. */
    if (!Dkc3VideoKaosRoomWindow(0, 190, 248, kDkc3KaosRoomWindowWest,
                                 kDkc3KaosRoomWindow1East, &left, &right) ||
        right != 190)
      return fprintf(stderr, "FAIL: KAOS room window tolerance\n"), 1;
    /* A pinned edge the room places on screen, an unpinned edge off the
     * room's anchor, and an empty window are rejected. */
    if (Dkc3VideoKaosRoomWindow(0, 188, 60, kDkc3KaosRoomWindowWest,
                                kDkc3KaosRoomWindow1East, &left, &right) ||
        Dkc3VideoKaosRoomWindow(0, 255, 248, kDkc3KaosRoomWindowWest,
                                kDkc3KaosRoomWindow1East, &left, &right) ||
        Dkc3VideoKaosRoomWindow(0, 150, 248, kDkc3KaosRoomWindowWest,
                                kDkc3KaosRoomWindow1East, &left, &right) ||
        Dkc3VideoKaosRoomWindow(20, 10, 248, kDkc3KaosRoomWindowWest,
                                kDkc3KaosRoomWindow1East, &left, &right))
      return fprintf(stderr, "FAIL: KAOS room window rejection\n"), 1;
  }
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
    /* A camera inside its region takes the region's walls; one still
     * converging on a region it just entered (the shaft at $02C0..$03C0
     * with the camera at $0295) gets the wall reflected about it, so the
     * margin is the 43 pixels left to travel, capped at 16:10's 26, instead
     * of zero. */
    {
      uint16_t lower = 0, upper = 0;
      Dkc3VideoRegionScrollBounds(0x02c0, 0x02c0, 0x03c0, 0x0a00, &lower,
                                  &upper);
      if (lower != 0x02c0 || upper != 0x02c0) {
        fprintf(stderr, "FAIL: region bounds inside (%04x %04x)\n", lower,
                upper);
        return 1;
      }
      Dkc3VideoRegionScrollBounds(0x0295, 0x02c0, 0x03c0, 0x0a00, &lower,
                                  &upper);
      Dkc3VideoMarginsForBias(0x0295, lower, upper, 0, &left, &right);
      if (lower != 0x026a || upper != 0x02c0 || left != 26 || right != 26) {
        fprintf(stderr, "FAIL: region bounds converging west (%04x %04x %d %d)\n",
                lower, upper, left, right);
        return 1;
      }
      Dkc3VideoRegionScrollBounds(0x02b8, 0x02c0, 0x03c0, 0x0a00, &lower,
                                  &upper);
      Dkc3VideoMarginsForBias(0x02b8, lower, upper, 0, &left, &right);
      if (left != 8 || right != 8) {
        fprintf(stderr, "FAIL: region margin closes with travel (%d %d)\n",
                left, right);
        return 1;
      }
      Dkc3VideoRegionScrollBounds(0x0300, 0x0100, 0x02e0, 0x0a00, &lower,
                                  &upper);
      if (lower != 0x0100 || upper != 0x0420) {
        fprintf(stderr, "FAIL: region bounds converging east (%04x %04x)\n",
                lower, upper);
        return 1;
      }
      Dkc3VideoRegionScrollBounds(0x0110, 0x0200, 0x0400, 0x0300, &lower,
                                  &upper);
      if (lower != 0x0100 || upper != 0x0300) {
        fprintf(stderr, "FAIL: region bounds clamp to the level (%04x %04x)\n",
                lower, upper);
        return 1;
      }
    }
    /* A wall that jumps outward opens its margin at the bounded rate; a
     * wall fixed in the world keeps pace with the view moving away from
     * it; narrowing is immediate. */
    if (Dkc3VideoLimitMarginGrowth(0, 43, 0) != kDkc3VideoMarginOpenRate ||
        Dkc3VideoLimitMarginGrowth(10, 43, 4) !=
            10 + 4 + kDkc3VideoMarginOpenRate ||
        Dkc3VideoLimitMarginGrowth(10, 43, -4) !=
            10 + kDkc3VideoMarginOpenRate ||
        Dkc3VideoLimitMarginGrowth(10, 13, 3) != 13 ||
        Dkc3VideoLimitMarginGrowth(43, 0, 0) != 0) {
      fprintf(stderr, "FAIL: margin growth limit\n");
      return 1;
    }
    /* The bias steps one pixel toward its target, and may also cancel
     * the camera's own motion this frame, never add to it: a camera
     * scrolling 4 pixels right into a pinned east wall leaves the view
     * still, and a still camera or one moving with the step keeps the
     * one-pixel glide. */
    if (Dkc3VideoStepPresentationBias(-20, -43, 4) != -25 ||
        Dkc3VideoStepPresentationBias(-20, -22, 4) != -22 ||
        Dkc3VideoStepPresentationBias(-20, -43, 0) != -21 ||
        Dkc3VideoStepPresentationBias(-20, -43, -4) != -21 ||
        Dkc3VideoStepPresentationBias(20, 43, -3) != 24 ||
        Dkc3VideoStepPresentationBias(20, 43, 3) != 21 ||
        Dkc3VideoStepPresentationBias(5, 5, 9) != 5) {
      fprintf(stderr, "FAIL: presentation bias step\n");
      return 1;
    }
    /* Camera regions shaped like the reported elevator level: a floor room
     * (1), a shaft exactly one screen wide above it (3), the room at the
     * shaft's top (2), and the corridor east of that room (4, then 5). */
    static uint8_t wram[0x10000];
    memset(wram, 0, sizeof wram);
    const struct {
      unsigned index;
      uint16_t left, right, top, bottom;
      uint8_t flags, right_link, left_link0, left_link1;
    } regions[] = {
        {1, 0x0110, 0x0290, 0x0828, 0x0908, 0x00, 0, 0, 0},
        {2, 0x0190, 0x0290, 0x0128, 0x0208, 0x05, 4, 0, 0},
        {3, 0x0190, 0x0290, 0x0208, 0x0828, 0x01, 0, 0, 0},
        {4, 0x0290, 0x0418, 0x0128, 0x0208, 0x04, 5, 2, 0},
        {5, 0x0418, 0x0598, 0x0128, 0x0220, 0x04, 0, 4, 0},
    };
    for (size_t n = 0; n < sizeof regions / sizeof regions[0]; n++) {
      const unsigned at = regions[n].index * 2u;
      const uint16_t words[4] = {regions[n].left, regions[n].right,
                                 regions[n].top, regions[n].bottom};
      for (unsigned k = 0; k < 4; k++) {
        wram[0x995a + k * 0x100 + at] = (uint8_t)words[k];
        wram[0x995a + k * 0x100 + at + 1] = (uint8_t)(words[k] >> 8);
      }
      wram[0x9d5a + at] = regions[n].flags;
      wram[0x9d5b + at] = regions[n].right_link;
      wram[0x9e5a + at] = regions[n].left_link0;
      wram[0x9e5b + at] = regions[n].left_link1;
    }
    uint16_t span_left = 0, span_right = 0;
    /* The shaft has no side links: its span is its own screen. */
    if (!Dkc3VideoCameraRegionSpan(wram, 6, &span_left, &span_right) ||
        span_left != 0x0190 || span_right != 0x0290) {
      fprintf(stderr, "FAIL: shaft region span (%04x %04x)\n", span_left,
              span_right);
      return 1;
    }
    /* The top room chains east through 4 and 5; from the corridor, the
     * west link reaches back to the top room. */
    if (!Dkc3VideoCameraRegionSpan(wram, 4, &span_left, &span_right) ||
        span_left != 0x0190 || span_right != 0x0598) {
      fprintf(stderr, "FAIL: top room region span (%04x %04x)\n", span_left,
              span_right);
      return 1;
    }
    if (!Dkc3VideoCameraRegionSpan(wram, 8, &span_left, &span_right) ||
        span_left != 0x0190 || span_right != 0x0598) {
      fprintf(stderr, "FAIL: corridor region span (%04x %04x)\n", span_left,
              span_right);
      return 1;
    }
    /* A closed ($20) neighbour ends the span; a player-gated ($80) one
     * does not, so the bound never moves with the player. */
    wram[0x9d5a + 8] = 0x24;
    Dkc3VideoCameraRegionSpan(wram, 4, &span_left, &span_right);
    const uint16_t closed_right = span_right;
    wram[0x9d5a + 8] = 0x84;
    Dkc3VideoCameraRegionSpan(wram, 4, &span_left, &span_right);
    if (closed_right != 0x0290 || span_right != 0x0598) {
      fprintf(stderr, "FAIL: region flags (%04x %04x)\n", closed_right,
              span_right);
      return 1;
    }
    wram[0x9d5a + 8] = 0x04;
    /* The second east candidate: the region after the link, when it names
     * this region as a left neighbour ($B7:E0ED). The shaft linked here
     * does not widen the top room. */
    wram[0x9d5b + 4] = 3;
    Dkc3VideoCameraRegionSpan(wram, 4, &span_left, &span_right);
    if (span_right != 0x0598) {
      fprintf(stderr, "FAIL: second east candidate (%04x)\n", span_right);
      return 1;
    }
    wram[0x9e5a + 8] = 0;
    Dkc3VideoCameraRegionSpan(wram, 4, &span_left, &span_right);
    if (span_right != 0x0290) {
      fprintf(stderr, "FAIL: unnamed second candidate (%04x)\n", span_right);
      return 1;
    }
    /* No current region (entry 0), an odd or out-of-range offset, or a
     * region narrower than a screen keeps the level-wide bounds. */
    wram[0x9a5a + 2] = 0x00;
    wram[0x9a5b + 2] = 0x02;
    if (Dkc3VideoCameraRegionSpan(wram, 0, NULL, NULL) ||
        Dkc3VideoCameraRegionSpan(wram, 5, NULL, NULL) ||
        Dkc3VideoCameraRegionSpan(wram, 0x100, NULL, NULL) ||
        Dkc3VideoCameraRegionSpan(wram, 2, NULL, NULL) ||
        Dkc3VideoCameraRegionSpan(NULL, 6, NULL, NULL)) {
      fprintf(stderr, "FAIL: region span without a usable region\n");
      return 1;
    }
    /* The shaft's one-screen span is a room narrower than two margins:
     * no glide, both margins black. */
    Dkc3VideoPresentationMarginsBounded(0x0190, 0x0190, 0x0190, &bias, &left,
                                        &right);
    if (bias != 0 || left != 0 || right != 0) {
      fprintf(stderr, "FAIL: one-screen region margins (%d %d %d)\n", bias,
              left, right);
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
      !CheckMargins(0x0100, 0x0100, 0, 0, 0) ||
      !CheckMargins(0x0100, 0x00ff, 0, 26, 26) ||
      !CheckMargins(0x0900, 0x0800, -26, 26, 0) ||
      !CheckMargins(0x0080, 0x0800, 26, 0, 26)) {
    fprintf(stderr, "FAIL: 16:10 glide margins\n");
    return 1;
  }
  /* glide in a room narrower than two margins (the bonus room of the first
   * level travels 24 pixels, $0100-$0118): the view stays locked
   * to the cartridge camera and each margin is clamped to the room, so the
   * background scrolls exactly as the native frame does. */
  if (!CheckMargins(0x0100, 0x0120, 0, 0, 26) ||
      !CheckMargins(0x0110, 0x0120, 0, 16, 16) ||
      !CheckMargins(0x0120, 0x0120, 0, 26, 0) ||
      !CheckMargins(0x0100, 0x0118, 0, 0, 24) ||
      !CheckMargins(0x010c, 0x0118, 0, 12, 12) ||
      !CheckMargins(0x0118, 0x0118, 0, 24, 0)) {
    fprintf(stderr, "FAIL: 16:10 glide in a narrow room\n");
    return 1;
  }
  /* glide in a room shorter than its two releases (2 * 26 * 8 = 416 pixels
   * at 16:10; the first level's second bonus room travels 128): the wall
   * shift shrinks to range / 16 so both releases fit at one pixel per
   * eight, no pin forces the bias to track the camera, and each margin is
   * clamped to the room. A room of exactly 416 pixels glides in full. */
  if (!CheckMargins(0x0100, 0x0134, 3, 3, 26) ||
      !CheckMargins(0x0134, 0x0134, -3, 26, 3) ||
      !CheckMargins(0x0100, 0x0180, 8, 8, 26) ||
      !CheckMargins(0x0108, 0x0180, 7, 15, 26) ||
      !CheckMargins(0x0140, 0x0180, 0, 26, 26) ||
      !CheckMargins(0x0178, 0x0180, -7, 26, 15) ||
      !CheckMargins(0x0180, 0x0180, -8, 26, 8) ||
      !CheckMargins(0x0100, 0x02a0, 26, 26, 26) ||
      !CheckMargins(0x02a0, 0x02a0, -26, 26, 26)) {
    fprintf(stderr, "FAIL: 16:10 glide in a short room\n");
    return 1;
  }
  /* Columns outside the live span are cleared; the span and other rows'
   * padding are untouched. 16:10 rows are 308 pixels: a left margin of 10
   * leaves 16 dead columns, a right margin of 26 leaves none. */
  {
    enum { kRows = 3, kPitch = 308 * 4 + 8 };
    static uint8_t buffer[kRows * kPitch];
    memset(buffer, 0xab, sizeof buffer);
    Dkc3VideoClearOutsideMargins(buffer, kPitch, 308, kRows, 26, 10, 26);
    bool ok = true;
    for (int y = 0; y < kRows && ok; y++) {
      const uint8_t *row = buffer + (size_t)y * kPitch;
      for (int x = 0; x < 308 && ok; x++) {
        const uint8_t expected = x < 16 ? 0 : 0xab;
        for (int b = 0; b < 4; b++)
          ok = ok && row[x * 4 + b] == expected;
      }
      for (int b = 308 * 4; b < kPitch; b++)
        ok = ok && row[b] == 0xab;
    }
    memset(buffer, 0xab, sizeof buffer);
    Dkc3VideoClearOutsideMargins(buffer, kPitch, 308, kRows, 26, 26, 3);
    for (int y = 0; y < kRows && ok; y++) {
      const uint8_t *row = buffer + (size_t)y * kPitch;
      for (int x = 0; x < 308; x++) {
        const uint8_t expected = x >= 308 - 23 ? 0 : 0xab;
        for (int b = 0; b < 4; b++)
          ok = ok && row[x * 4 + b] == expected;
      }
    }
    memset(buffer, 0xab, sizeof buffer);
    Dkc3VideoClearOutsideMargins(buffer, kPitch, 308, kRows, 26, 26, 26);
    Dkc3VideoClearOutsideMargins(buffer, kPitch, 256, kRows, 26, 0, 0);
    for (size_t i = 0; i < sizeof buffer; i++)
      ok = ok && buffer[i] == 0xab;
    if (!ok) {
      fprintf(stderr, "FAIL: clear outside the live margins\n");
      return 1;
    }
  }
  /* $B3:C0F4's trunk windows. The Ripsaw Rage quick save: W2's trunk
   * starts 273 pixels right of the camera, so the cartridge wrote it as
   * [255, 255]; both windows are the clamp of their unclamped edges. */
  {
    const uint16_t world[4] = {0x0158, 0x0227, 0x02d8, 0x03a7};
    int16_t edges[4];
    const uint8_t save_regs[4] = {0, 96, 255, 255};
    bool ok = Dkc3VideoUnclampedTrunkWindows(world, 0x01c7, save_regs,
                                             edges) == 3u &&
              edges[0] == -111 && edges[1] == 96 && edges[2] == 273 &&
              edges[3] == 480;
    /* The second trunk on screen (camera $0211). */
    const uint8_t later_regs[4] = {0, 22, 199, 255};
    ok = ok && Dkc3VideoUnclampedTrunkWindows(world, 0x0211, later_regs,
                                              edges) == 3u &&
         edges[2] == 199 && edges[3] == 406;
    /* A register some other writer changed fails its window only. */
    const uint8_t other_regs[4] = {0, 96, 255, 0};
    ok = ok && Dkc3VideoUnclampedTrunkWindows(world, 0x01c7, other_regs,
                                              edges) == 1u;
    /* W1L is written as a low byte when past 255; that is not a clamp. */
    const uint8_t wrapped_regs[4] = {0x18, 255, 255, 255};
    ok = ok && Dkc3VideoUnclampedTrunkWindows(world, 0x0040, wrapped_regs,
                                              edges) == 2u &&
         edges[0] == 280;
    ok = ok && Dkc3VideoUnclampedTrunkWindows(NULL, 0, save_regs, edges) ==
                   0u;
    if (!ok) {
      fprintf(stderr, "FAIL: unclamped trunk windows\n");
      return 1;
    }
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
      Dkc3VideoSpawnLead() != 0 ||
      Dkc3VideoSpriteCullLead() != 0 ||
      Dkc3VideoExpandCullLeft(0x20) != 0x20 ||
      Dkc3VideoExpandCullSpan(0x140) != 0x140 ||
      Dkc3VideoExpandSpawnLeft(0x20) != 0x20 ||
      Dkc3VideoExpandSpawnSpan(0x140) != 0x140 ||
      Dkc3VideoExpandSpriteCullLeft(0x30) != 0x30 ||
      Dkc3VideoExpandSpriteCullSpan(0x160) != 0x160 ||
      Dkc3VideoPromoteOamXHigh(0x0120) != 0x0120) {
    fprintf(stderr, "FAIL: object bounds widened before terrain was ready\n");
    return 1;
  }
  Dkc3VideoSetTerrainReady(true);
  {
    /* Widescreen spawn and sprite-renderer lead: 16:9 gets the full configured
     * lead (default 48 px, clamped to [0, 64]) on standard DATA_BBA8BE rows
     * (< $60 / < $1C0) and on sprite renderer culls, while already-wide rows
     * ($60/$1C0 and $F8/$2F0) skip extra lead to prevent unsigned underflow. */
    Dkc3VideoSetSpawnLead(kDkc3VideoDefaultSpawnLead);
    Dkc3VideoSetPresentationBias(0);
    if (Dkc3VideoSpawnLead() != 48 ||
        Dkc3VideoSpriteCullLead() != 48 ||
        Dkc3VideoExpandSpawnLeft(0x10) != 0x10 + 43 + 48 ||
        Dkc3VideoExpandSpawnSpan(0x120) != 0x120 + 86 + 96 ||
        Dkc3VideoExpandSpawnLeft(0x18) != 0x18 + 43 + 48 ||
        Dkc3VideoExpandSpawnSpan(0x130) != 0x130 + 86 + 96 ||
        Dkc3VideoExpandSpawnLeft(0x20) != 0x20 + 43 + 48 ||
        Dkc3VideoExpandSpawnSpan(0x140) != 0x140 + 86 + 96 ||
        Dkc3VideoExpandSpawnLeft(0x28) != 0x28 + 43 + 48 ||
        Dkc3VideoExpandSpawnSpan(0x150) != 0x150 + 86 + 96 ||
        Dkc3VideoExpandSpawnLeft(0x40) != 0x40 + 43 + 48 ||
        Dkc3VideoExpandSpawnSpan(0x1a0) != 0x1a0 + 86 + 96 ||
        /* Already-wide rows skip extra lead, keeping exact extra/bias. */
        Dkc3VideoExpandSpawnLeft(0x60) != 0x60 + 43 ||
        Dkc3VideoExpandSpawnSpan(0x1c0) != 0x1c0 + 86 ||
        Dkc3VideoExpandSpawnLeft(0xf8) != 0xf8 + 43 ||
        Dkc3VideoExpandSpawnSpan(0x2f0) != 0x2f0 + 86 ||
        /* Sprite renderer culls match the lead at 16:9 (74 px safe OAM cap). */
        Dkc3VideoExpandSpriteCullLeft(0x30) != 0x30 + 43 + 48 ||
        Dkc3VideoExpandSpriteCullSpan(0x160) != 0x160 + 86 + 96 ||
        Dkc3VideoExpandSpriteCullLeft(0x10) != 0x10 + 43 + 48 ||
        Dkc3VideoExpandSpriteCullSpan(0x120) != 0x120 + 86 + 96) {
      fprintf(stderr, "FAIL: 16:9 spawn and sprite-cull lead\n");
      return 1;
    }
    /* Bias shifts the spawn and sprite-cull windows asymmetrically while
     * preserving the 8-pixel spawn/despawn hysteresis. */
    Dkc3VideoSetPresentationBias(43);
    if (Dkc3VideoExpandSpawnLeft(0x20) != 0x20 + 48 ||
        Dkc3VideoExpandSpawnSpan(0x140) != 0x140 + 86 + 96 ||
        Dkc3VideoExpandSpawnLeft(0x28) - Dkc3VideoExpandSpawnLeft(0x20) != 8 ||
        Dkc3VideoExpandSpriteCullLeft(0x30) != 0x30 + 48 ||
        Dkc3VideoExpandSpriteCullSpan(0x160) != 0x160 + 86 + 96) {
      fprintf(stderr, "FAIL: biased 16:9 spawn and sprite-cull lead\n");
      return 1;
    }
    Dkc3VideoSetPresentationBias(-43);
    if (Dkc3VideoExpandSpawnLeft(0x20) != 0x20 + 86 + 48 ||
        Dkc3VideoExpandSpriteCullLeft(0x30) != 0x30 + 86 + 48) {
      fprintf(stderr, "FAIL: negative-bias 16:9 spawn and sprite-cull lead\n");
      return 1;
    }
    /* Setter clamping. */
    Dkc3VideoSetPresentationBias(0);
    Dkc3VideoSetSpawnLead(-10);
    if (Dkc3VideoSpawnLead() != 0 || Dkc3VideoSpriteCullLead() != 0 ||
        Dkc3VideoExpandSpawnLeft(0x20) != 0x4b ||
        Dkc3VideoExpandSpriteCullLeft(0x30) != 0x5b) {
      fprintf(stderr, "FAIL: zero/negative spawn lead clamp\n");
      return 1;
    }
    Dkc3VideoSetSpawnLead(200);
    if (Dkc3VideoSpawnLead() != kDkc3VideoMaxSpawnLead ||
        Dkc3VideoSpriteCullLead() != kDkc3VideoMaxSpawnLead) {
      fprintf(stderr, "FAIL: upper spawn lead clamp\n");
      return 1;
    }
    Dkc3VideoSetSpawnLead(kDkc3VideoDefaultSpawnLead);
  }
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
       * by a negative one; the span keeps both margins either way. The
       * static banana/star walk subtracts camera + right and adds back the
       * full $10F span, so Span($10F) - Right($100) must equal Left($0F)
       * at every bias. */
      (Dkc3VideoSetPresentationBias(43),
       Dkc3VideoExpandCullLeft(0x20) != 0x20 ||
       Dkc3VideoExpandCullSpan(0x140) != 0x196 ||
       Dkc3VideoExpandCullRight(0x100) != 0x156 ||
       Dkc3VideoExpandCullSpan(0x10f) - Dkc3VideoExpandCullRight(0x100) !=
           Dkc3VideoExpandCullLeft(0x0f) ||
       Dkc3VideoPresentationBias() != 43) ||
      (Dkc3VideoSetPresentationBias(-43),
       Dkc3VideoExpandCullLeft(0x20) != 0x76 ||
       Dkc3VideoExpandCullSpan(0x140) != 0x196 ||
       Dkc3VideoExpandCullRight(0x100) != 0x100 ||
       Dkc3VideoExpandCullSpan(0x10f) - Dkc3VideoExpandCullRight(0x100) !=
           Dkc3VideoExpandCullLeft(0x0f)) ||
      (Dkc3VideoSetPresentationBias(0),
       Dkc3VideoExpandCullLeft(0x20) != 0x4b ||
       Dkc3VideoExpandCullRight(0x100) != 0x12b ||
       Dkc3VideoExpandCullSpan(0x10f) - Dkc3VideoExpandCullRight(0x100) !=
           Dkc3VideoExpandCullLeft(0x0f)) ||
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

  /* Camera reach for the structural wall rule. The waterfall save's region
   * spans camera $0100..$0380, so the native window can reach world x
   * $0100..$047F: the trunk gap at tiles 100-106 is inside it and keeps
   * its decode. A west hold at $0200 puts tile 63 outside, tile 64 inside;
   * the east end is tile 143 inside, 144 outside. */
  if (!Dkc3VideoTileInCameraReach(100, 0x0100, 0x0380) ||
      !Dkc3VideoTileInCameraReach(106, 0x0100, 0x0380) ||
      !Dkc3VideoTileInCameraReach(32, 0x0100, 0x0380) ||
      Dkc3VideoTileInCameraReach(31, 0x0100, 0x0380) ||
      !Dkc3VideoTileInCameraReach(143, 0x0100, 0x0380) ||
      Dkc3VideoTileInCameraReach(144, 0x0100, 0x0380) ||
      Dkc3VideoTileInCameraReach(63, 0x0200, 0x0380) ||
      !Dkc3VideoTileInCameraReach(64, 0x0200, 0x0380) ||
      /* A tile straddling a wall that is not on a tile boundary is
       * partly beyond it, so the wall rule may still treat it. */
      Dkc3VideoTileInCameraReach(64, 0x0204, 0x0380) ||
      Dkc3VideoTileInCameraReach(64, 0x0300, 0x0200)) {
    fprintf(stderr, "FAIL: camera reach for the wall rule\n");
    return 1;
  }

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
      /* At 21:9, placement spawn still receives the configured lead (fitting
       * within one 256-pixel neighbor bucket even at +95 wall bias), while
       * the sprite renderer cull lead clamps to 0 so nine-bit OAM never wraps. */
      Dkc3VideoSpawnLead() != kDkc3VideoDefaultSpawnLead ||
      Dkc3VideoSpriteCullLead() != 0 ||
      Dkc3VideoExpandSpawnLeft(0x20) != 0x7f + kDkc3VideoDefaultSpawnLead ||
      Dkc3VideoExpandSpawnSpan(0x140) != 0x1fe + 2 * kDkc3VideoDefaultSpawnLead ||
      Dkc3VideoExpandSpriteCullLeft(0x30) != 0x30 + 95 ||
      Dkc3VideoExpandSpriteCullSpan(0x160) != 0x160 + 190 ||
      /* At 21:9 with the 48 px spawn lead, wall bias + lead + 32 px
       * asymmetric row slack can reach a second 256-pixel neighbor bucket,
       * so the broad-phase scan expands to two cells per side (clamped to
       * row bounds); with lead=0 it drops back to one neighbor per side. */
      Dkc3VideoPlacementScanCells(6, 64, placement_cells) != 5 ||
      placement_cells[0] != 6 || placement_cells[1] != 4 ||
      placement_cells[2] != 8 || placement_cells[3] != 2 ||
      placement_cells[4] != 10 ||
      Dkc3VideoPlacementScanCells(2, 64, placement_cells) != 4 ||
      placement_cells[0] != 2 || placement_cells[1] != 0 ||
      placement_cells[2] != 4 || placement_cells[3] != 6 ||
      Dkc3VideoPlacementScanCells(0, 64, placement_cells) != 3 ||
      placement_cells[0] != 0 || placement_cells[1] != 2 ||
      placement_cells[2] != 4 ||
      Dkc3VideoPlacementScanCells(126, 64, placement_cells) != 3 ||
      placement_cells[0] != 126 || placement_cells[1] != 124 ||
      placement_cells[2] != 122 ||
      (Dkc3VideoSetSpawnLead(0),
       Dkc3VideoPlacementScanCells(6, 64, placement_cells) != 3 ||
       placement_cells[0] != 6 || placement_cells[1] != 4 ||
       placement_cells[2] != 8) ||
      (Dkc3VideoSetSpawnLead(kDkc3VideoDefaultSpawnLead), false) ||
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
    /* KAOS's body layer is enabled only for a middle HDMA band. Its
     * 64-column map must qualify before the per-band object-plane policy
     * runs; bounded BG3 must still repeat and colliding maps stay rejected. */
    uint8_t *body_table = s_fake_wram + 0x2500;
    const uint8_t body_enables[] = {19, 0x16, 0, 128, 0x17, 0,
                                     77, 0x16, 0, 0};
    memcpy(body_table, body_enables, sizeof body_enables);
    memset(channels, 0, sizeof channels);
    channels[0].active = true;
    channels[0].b_address = 0x2c;
    channels[0].mode = 1;
    channels[0].table_address = 0x7e2500;
    Dkc3HdmaFrameState body_start = start;
    const uint8_t body_maps[4] = {0x71, 0x79, 0x6c, 0};
    memcpy(body_start.bg_sc, body_maps, sizeof body_maps);
    body_start.main_layers = 0x16;
    body_start.sub_layers = 0;
    Dkc3HdmaScanBands(channels, &body_start, &memory, &bands);
    if (bands.count != 3 || bands.band[1].first_line != 20 ||
        bands.band[1].last_line != 147 ||
        Dkc3VideoPpuFrameWideLayerMask(9, body_maps, 0x16, 0, NULL) != 2 ||
        Dkc3VideoPpuFrameWideLayerMask(9, body_maps, 0x16, 0, &bands) != 3 ||
        Dkc3VideoPpuFrameWideLayerMask(7, body_maps, 0x16, 0, &bands) != 0 ||
        Dkc3VideoPpuFrameWideLayerMask(9, NULL, 0x16, 0, &bands) != 0) {
      fprintf(stderr, "FAIL: HDMA-only boss layer widescreen selection\n");
      return 1;
    }
    /* The same late enable may be on the subscreen for color math. */
    body_table[4] = 0x16;
    body_table[5] = 1;
    Dkc3HdmaScanBands(channels, &body_start, &memory, &bands);
    const uint8_t colliding_maps[4] = {0x71, 0x74, 0x6c, 0};
    if (Dkc3VideoPpuFrameWideLayerMask(9, body_maps, 0x16, 0, &bands) != 3 ||
        Dkc3VideoPpuFrameWideLayerMask(9, colliding_maps, 0x16, 0, &bands) != 0) {
      fprintf(stderr, "FAIL: HDMA subscreen layer and tilemap collision gate\n");
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
    /* Tidal Trouble on Enguarde: 32-row horizontal map stages v_scroll one
     * 256-pixel page above camera_y (camera_y=$0218 -> v_scroll=23), while
     * HDMA writes BG2HOFS from line 1. During a 7-8 px/frame charge, the
     * frame-start register trails the rendered bands by >6 px. */
    memset(&bands, 0, sizeof bands);
    bands.count = 3;
    bands.band[0].first_line = 1;   bands.band[0].last_line = 155;
    bands.band[0].h_scroll[1] = 534; bands.band[0].v_scroll[1] = 23;
    bands.band[1].first_line = 156; bands.band[1].last_line = 190;
    bands.band[1].h_scroll[1] = 535; bands.band[1].v_scroll[1] = 23;
    bands.band[2].first_line = 191; bands.band[2].last_line = 224;
    bands.band[2].h_scroll[1] = 536; bands.band[2].v_scroll[1] = 23;
    if (!Dkc3VideoSelectTerrainPhase(&bands, 1, 542, 23, 0x0a0fu, 0x0218u,
                                     &h, &v) ||
        h != 534 || v != 23) {
      fprintf(stderr,
              "FAIL: horizontal 32-row HDMA terrain phase during fast charge\n");
      return 1;
    }
    /* Full-screen underwater ripple: three horizontal scroll values each
     * cover under half the screen, but together share one terrain phase. */
    bands.count = 3;
    bands.band[0].first_line = 1;   bands.band[0].last_line = 60;
    bands.band[0].h_scroll[1] = 533; bands.band[0].v_scroll[1] = 23;
    bands.band[1].first_line = 61;  bands.band[1].last_line = 144;
    bands.band[1].h_scroll[1] = 534; bands.band[1].v_scroll[1] = 23;
    bands.band[2].first_line = 145; bands.band[2].last_line = 224;
    bands.band[2].h_scroll[1] = 535; bands.band[2].v_scroll[1] = 23;
    if (!Dkc3VideoSelectTerrainPhase(&bands, 1, 542, 23, 0x0a0fu, 0x0218u,
                                     &h, &v) ||
        h != 534 || v != 23) {
      fprintf(stderr,
              "FAIL: full-screen wavy ripple terrain phase selection\n");
      return 1;
    }
    /* 32-row vertical wrap across 255 -> 0 during fast horizontal motion. */
    bands.count = 1;
    bands.band[0].first_line = 1;   bands.band[0].last_line = 224;
    bands.band[0].h_scroll[1] = 534; bands.band[0].v_scroll[1] = 0;
    if (!Dkc3VideoSelectTerrainPhase(&bands, 1, 542, 255, 0x0a0fu, 0x0201u,
                                     &h, &v) ||
        h != 534 || v != 0) {
      fprintf(stderr, "FAIL: 32-row vertical wrap terrain phase selection\n");
      return 1;
    }
    /* Frame-start register within lead of camera_h, but HDMA bands render
     * on the opposite side of camera_h (>6 px from frame_h). */
    bands.count = 1;
    bands.band[0].first_line = 1;   bands.band[0].last_line = 224;
    bands.band[0].h_scroll[1] = 525; bands.band[0].v_scroll[1] = 23;
    if (!Dkc3VideoSelectTerrainPhase(&bands, 1, 533, 23, 0x0a0fu, 0x0018u,
                                     &h, &v) ||
        h != 525 || v != 23) {
      fprintf(stderr,
              "FAIL: stale frame-start register within camera lead replaced\n");
      return 1;
    }
    /* A minority one-line HDMA band at the camera phase must not suppress
     * a qualifying dominant-scroll fallback covering the rest of the frame. */
    bands.count = 2;
    bands.band[0].first_line = 1;   bands.band[0].last_line = 1;
    bands.band[0].h_scroll[1] = 533; bands.band[0].v_scroll[1] = 23;
    bands.band[1].first_line = 2;   bands.band[1].last_line = 224;
    bands.band[1].h_scroll[1] = 520; bands.band[1].v_scroll[1] = 23;
    if (!Dkc3VideoSelectTerrainPhase(&bands, 1, 533, 23, 0x0a0fu, 0x0018u,
                                     &h, &v) ||
        h != 520 || v != 23) {
      fprintf(stderr,
              "FAIL: one-line camera-phase band suppressed dominant fallback\n");
      return 1;
    }
    /* The 8-bit vertical match is restricted to the first 256-pixel page
     * (< 0x0100); a candidate far vertically whose low byte happens to match
     * camera_y & 0xff must be rejected. */
    bands.count = 1;
    bands.band[0].first_line = 1;   bands.band[0].last_line = 224;
    bands.band[0].h_scroll[1] = 520; bands.band[0].v_scroll[1] = 535;
    if (Dkc3VideoSelectTerrainPhase(&bands, 1, 533, 535, 0x0a0fu, 0x0018u,
                                    &h, &v) ||
        h != 533 || v != 535) {
      fprintf(stderr,
              "FAIL: dominant_v >= 0x0100 matched via 8-bit low byte\n");
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
    /* Shape 1 (Pothole Panic): two columns, each with 32 metatile rows.
     * Distinct upper/lower halves catch the old 16-row addressing. */
    uint8_t bank[0x10000] = {0};
    uint16_t tile = 0, id = 0, ids[2], counts[2];
    const Dkc3VideoLevelLayout layout =
        Dkc3VideoLevelLayoutForScene(1, 0x45);
    if (layout != kDkc3VideoLevelLayoutTallHorizontal ||
        Dkc3VideoLevelLayoutColumnRows(layout) != 32 ||
        Dkc3VideoLevelLayoutRowBytes(layout) != 0 ||
        Dkc3VideoLevelLayoutColumnRows(kDkc3VideoLevelLayoutHorizontal) != 16 ||
        Dkc3VideoLevelLayoutColumnRows(kDkc3VideoLevelLayoutVertical) != 0 ||
        Dkc3VideoLevelLayoutColumnRows(kDkc3VideoLevelLayoutUnknown) != 0 ||
        Dkc3VideoLevelLayoutForScene(2, 0x45) != kDkc3VideoLevelLayoutUnknown) {
      fprintf(stderr, "FAIL: column-major layout selection\n");
      return 1;
    }
    for (unsigned y = 0; y < 32; y++) {
      WriteWord(bank, (uint16_t)(0x1000 + y * 2), 3);
      WriteWord(bank, (uint16_t)(0x1040 + y * 2), y < 16 ? 5 : 7);
    }
    WriteWord(bank, 0x20b2, 0x1234); /* id 5, sub-tile (1,2) */
    WriteWord(bank, 0x20f2, 0x2345); /* id 7, sub-tile (1,2) */
    for (unsigned y = 0; y < 64; y++) {
      const uint16_t expected = (y & 31) < 16 ? 0x1234 : 0x2345;
      if (!Dkc3VideoDecodeLevelTile(bank, sizeof bank, 0x1000, 0x2000,
                                     layout, 5, y * 4 + 2, &tile) ||
          tile != expected ||
          !Dkc3VideoReadLevelMetatile(bank, sizeof bank, 0x1000, layout, 0,
                                       1, y, &id) ||
          id != ((y & 31) < 16 ? 5 : 7)) {
        fprintf(stderr, "FAIL: tall horizontal column addressing at row %u\n", y);
        return 1;
      }
    }
    if (Dkc3VideoMetatileNeighbours(bank, sizeof bank, 0x1000, 0x1080,
                                     layout, 0, 3, true, ids, counts, 2) != 2 ||
        counts[0] != 16 || counts[1] != 16 ||
        !((ids[0] == 5 && ids[1] == 7) || (ids[0] == 7 && ids[1] == 5))) {
      fprintf(stderr, "FAIL: tall horizontal map extent and neighbours\n");
      return 1;
    }
    /* Double flip and 16-bit column address wrapping use the same source
     * definition as the native cartridge, including its bit-11 carry. */
    WriteWord(bank, 0x1064, 0xc803);
    WriteWord(bank, 0x206d, 0x0567);
    if (!Dkc3VideoDecodeLevelTile(bank, sizeof bank, 0x1000, 0x2000,
                                   layout, 5, 74, &tile) || tile != 0xc567 ||
        !Dkc3VideoDecodeLevelTile(bank, sizeof bank, 0x1000, 0x2000,
                                   layout, 4101, 74, &tile) || tile != 0xc567) {
      fprintf(stderr, "FAIL: tall horizontal flips and address wrapping\n");
      return 1;
    }
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

  {
    /* The overworld gate: Mode 1, BG1 64x32 at camera - $100, other wide
     * layers at BG1's phase, every enabled layer 32 rows tall. */
    Dkc3VideoSetAspect(kDkc3VideoAspect16x9);
    Dkc3VideoSetWidescreen(true);
    uint8_t maps[4] = {0x79, 0x6c, 0x71, 0x00};
    uint16_t scroll[3] = {0x00a0, 0x01a2, 0x00a0};
#define OVERWORLD(vector, mode, layers, camera) \
    Dkc3VideoIsResidentOverworld(vector, mode, maps, layers, scroll, camera)
    if (!OVERWORLD(0xb4b179, 0x09, 0x17, 0x01a0) ||
        OVERWORLD(0xb38076, 0x09, 0x17, 0x01a0) ||
        OVERWORLD(0xb4b179, 0x02, 0x17, 0x01a0) ||
        OVERWORLD(0xb4b179, 0x09, 0x16, 0x01a0) ||
        !OVERWORLD(0xb4b179, 0x09, 0x17, 0x01a2) ||
        !OVERWORLD(0xb4b179, 0x09, 0x17, 0x0198) ||
        !OVERWORLD(0xb4b179, 0x09, 0x17, 0x01a8) ||
        OVERWORLD(0xb4b179, 0x09, 0x17, 0x0197) ||
        OVERWORLD(0xb4b179, 0x09, 0x17, 0x01a9) ||
        Dkc3VideoIsResidentOverworld(0xb4b179, 0x09, NULL, 0x17, scroll,
                                     0x01a0))
      return fprintf(stderr, "FAIL: overworld scene gate\n"), 1;
    scroll[2] = 0x00a1;
    if (OVERWORLD(0xb4b179, 0x09, 0x17, 0x01a0) ||
        !OVERWORLD(0xb4b179, 0x09, 0x13, 0x01a0))
      return fprintf(stderr, "FAIL: overworld BG3 phase gate\n"), 1;
    scroll[2] = 0x00a0;
    maps[0] = 0x78;
    if (OVERWORLD(0xb4b179, 0x09, 0x17, 0x01a0))
      return fprintf(stderr, "FAIL: overworld 32-column BG1 accepted\n"), 1;
    maps[0] = 0x7b;
    if (OVERWORLD(0xb4b179, 0x09, 0x17, 0x01a0))
      return fprintf(stderr, "FAIL: overworld 64-row BG1 accepted\n"), 1;
    maps[0] = 0x79;
    maps[1] = 0x7c;
    if (OVERWORLD(0xb4b179, 0x09, 0x17, 0x01a0))
      return fprintf(stderr, "FAIL: overworld shared tilemap page\n"), 1;
    maps[1] = 0x6c;
    scroll[0] = 0x02a0;
    scroll[2] = 0x02a0;
    if (!OVERWORLD(0xb4b179, 0x09, 0x17, 0x01a0))
      return fprintf(stderr, "FAIL: overworld scroll above nine bits\n"), 1;
    /* Rendered past the last camera position, where the right margin would
     * wrap to the map's west edge; the east bound itself still qualifies. */
    scroll[0] = scroll[2] = 0x0101;
    if (OVERWORLD(0xb4b179, 0x09, 0x17, 0x0201))
      return fprintf(stderr, "FAIL: overworld past the east bound\n"), 1;
    scroll[0] = scroll[2] = 0x0100;
    if (!OVERWORLD(0xb4b179, 0x09, 0x17, 0x0202))
      return fprintf(stderr, "FAIL: overworld at the east bound\n"), 1;
    scroll[0] = scroll[2] = 0x00a0;
    if (Dkc3VideoOverworldRenderedX(0x02a0) != 0x01a0 ||
        Dkc3VideoOverworldRenderedX(0x0000) != 0x0100)
      return fprintf(stderr, "FAIL: overworld rendered x\n"), 1;
#undef OVERWORLD

    /* Margins clamp to the resident map [$100, $300) under any policy;
     * the view never takes a bias. */
    static const struct {
      uint16_t camera;
      int left, right;
    } cases[] = {
        {0x01a0, 43, 43}, {0x0100, 0, 43}, {0x0110, 16, 43},
        {0x0200, 43, 0},  {0x01f0, 43, 16}, {0x012b, 43, 43},
    };
    const Dkc3VideoEdgePolicy policies[] = {
        kDkc3VideoEdgeGlide, kDkc3VideoEdgeShift, kDkc3VideoEdgeReflect,
        kDkc3VideoEdgeBars};
    for (size_t p = 0; p < sizeof policies / sizeof policies[0]; p++) {
      Dkc3VideoSetEdgePolicy(policies[p]);
      for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int left = -1, right = -1;
        Dkc3VideoOverworldMargins(cases[i].camera, &left, &right);
        if (left != cases[i].left || right != cases[i].right) {
          fprintf(stderr, "FAIL: overworld margins camera %04x: %d %d\n",
                  cases[i].camera, left, right);
          return 1;
        }
      }
    }
    Dkc3VideoSetEdgePolicy(kDkc3VideoEdgeGlide);

    /* Rows the overworld streams into: at vscroll $11F the 224 shown lines
     * start at row 3's last line (its first sits 249 lines down and only
     * line 7 wraps to the top) and end in row 31; rows 0-2 hide. */
    for (unsigned row = 0; row < 32; row++) {
      const bool hidden = row <= 2;
      if (Dkc3VideoTilemapRowVisible(0x011f, row) == hidden) {
        fprintf(stderr, "FAIL: overworld row %u visibility at $11F\n", row);
        return 1;
      }
    }
    for (unsigned row = 0; row < 32; row++) {
      const bool hidden = row >= 28;
      if (Dkc3VideoTilemapRowVisible(0x0000, row) == hidden ||
          Dkc3VideoTilemapRowVisible(0x0100, row) == hidden) {
        fprintf(stderr, "FAIL: overworld row %u visibility at 0\n", row);
        return 1;
      }
    }
    /* Scrolled one line: row 0's last seven lines remain at the top, and
     * row 28 enters at the bottom. */
    if (!Dkc3VideoTilemapRowVisible(0x0001, 0) ||
        !Dkc3VideoTilemapRowVisible(0x0001, 28) ||
        Dkc3VideoTilemapRowVisible(0x0001, 29) ||
        Dkc3VideoTilemapRowVisible(0x0008, 0))
      return fprintf(stderr, "FAIL: overworld row visibility edges\n"), 1;

    /* Row streaming as measured in the hovercraft run: at $11E rows 2
     * (hidden) and 3 (its one-word tail, shown); at $11F rows 3 and 4,
     * both shown. A column stripe touches every shown row. */
    if (!Dkc3VideoOverworldWritesAreRows(0, 0x011f, false) ||
        !Dkc3VideoOverworldWritesAreRows(0x0000000cu, 0x011e, false) ||
        !Dkc3VideoOverworldWritesAreRows(0x00000018u, 0x011f, false) ||
        !Dkc3VideoOverworldWritesAreRows(0x00000007u, 0x011f, false))
      return fprintf(stderr, "FAIL: overworld row streaming refused\n"), 1;
    if (Dkc3VideoOverworldWritesAreRows(0x00000038u, 0x011f, false) ||
        Dkc3VideoOverworldWritesAreRows(0xfffffff8u, 0x011f, false) ||
        Dkc3VideoOverworldWritesAreRows(0x00000007u, 0x011f, true))
      return fprintf(stderr, "FAIL: overworld column writes accepted\n"), 1;

    /* HDMA that writes a vertical scroll register ($0E BG1VOFS, $10
     * BG2VOFS, $12 BG3VOFS) makes that layer's rows vary per line. The
     * overworld's BG2HOFS channel (mode 2, $0F twice) does not; mode 1 at
     * $0F reaches BG2VOFS, and mode 4 at $0D spans $0D-$10. */
    {
      uint8_t b_address[8] = {0x0f, 0x21, 0x21, 0x0f, 0x0d, 0x11, 0x12, 0x0e};
      uint8_t mode[8] = {2, 3, 3, 1, 4, 0, 2, 0};
      if (Dkc3VideoHdmaVerticalScrollLayers(0x07, b_address, mode) != 0)
        return fprintf(stderr, "FAIL: overworld HDMA has no VOFS\n"), 1;
      if (Dkc3VideoHdmaVerticalScrollLayers(0x08, b_address, mode) != 0x02)
        return fprintf(stderr, "FAIL: HDMA mode 1 at $0F reaches $10\n"), 1;
      if (Dkc3VideoHdmaVerticalScrollLayers(0x10, b_address, mode) != 0x03)
        return fprintf(stderr, "FAIL: HDMA mode 4 at $0D spans $0D-$10\n"),
               1;
      if (Dkc3VideoHdmaVerticalScrollLayers(0x20, b_address, mode) != 0 ||
          Dkc3VideoHdmaVerticalScrollLayers(0x40, b_address, mode) != 0x04 ||
          Dkc3VideoHdmaVerticalScrollLayers(0x80, b_address, mode) != 0x01)
        return fprintf(stderr, "FAIL: HDMA single-register VOFS\n"), 1;
      if (Dkc3VideoHdmaVerticalScrollLayers(0xff, NULL, mode) != 0)
        return fprintf(stderr, "FAIL: HDMA without addresses\n"), 1;
    }
  }

  /* Init-script palette lookup on a synthetic bank (no cartridge bytes). */
  {
    static uint8_t bank[0x100];
    const uint8_t two_scripts[] = {
        0x12, 0x34, 0xef,                         /* tail of other data */
        0xff, 0x84, 0x02, 0xfa, 0x48, 0x01, 0xfc, 0xc8, 0x00,
        0xfe, 0x00, 0x20, 0x0e, 0xcc, 0x00, 0xef, /* ends with $EF */
        0xff, 0x84, 0x02, 0xfc, 0xc8, 0x00, 0x3a, 0x00, 0x00,
        0xf0,                                     /* ends with $F0 */
        0xff, 0x34, 0x02, 0xfc, 0x16, 0x00, 0xef};
    memset(bank, 0, sizeof bank);
    memcpy(bank, two_scripts, sizeof two_scripts);
    if (Dkc3VideoInitScriptPaletteForType(bank, sizeof bank, 0x0284) !=
            0x00c8 ||
        Dkc3VideoInitScriptPaletteForType(bank, sizeof bank, 0x0234) !=
            0x0016)
      return fprintf(stderr, "FAIL: init-script palette lookup\n"), 1;
    if (Dkc3VideoInitScriptPaletteForType(bank, sizeof bank, 0x0238) != 0 ||
        Dkc3VideoInitScriptPaletteForType(NULL, sizeof bank, 0x0284) != 0 ||
        Dkc3VideoInitScriptPaletteForType(bank, sizeof bank, 0) != 0)
      return fprintf(stderr, "FAIL: init-script palette absent type\n"), 1;

    /* A disagreeing script, one without a palette, one with two, one that
     * calls a sub-script ($F9), and one that runs off the bank each make
     * the answer unknown. */
    const struct {
      uint8_t bytes[12];
      size_t length;
    } refuting[] = {
        {{0xff, 0x84, 0x02, 0xfc, 0x71, 0x00, 0xef}, 7},
        {{0xff, 0x84, 0x02, 0x3a, 0x00, 0x00, 0xef}, 7},
        {{0xff, 0x84, 0x02, 0xfc, 0xc8, 0x00, 0xfc, 0xc8, 0x00, 0xef}, 10},
        {{0xff, 0x84, 0x02, 0xf9, 0x6d, 0x65, 0xfc, 0xc8, 0x00, 0xef}, 10},
    };
    for (size_t i = 0; i < sizeof refuting / sizeof refuting[0]; i++) {
      memset(bank, 0, sizeof bank);
      memcpy(bank, two_scripts, sizeof two_scripts);
      memcpy(bank + 0x80, refuting[i].bytes, refuting[i].length);
      if (Dkc3VideoInitScriptPaletteForType(bank, sizeof bank, 0x0284) != 0)
        return fprintf(stderr, "FAIL: init-script refutation %zu\n", i), 1;
    }
    memset(bank, 0, sizeof bank);
    memcpy(bank, two_scripts, sizeof two_scripts);
    const uint8_t unterminated[] = {0xff, 0x84, 0x02, 0xfc, 0xc8, 0x00};
    memcpy(bank + sizeof bank - sizeof unterminated, unterminated,
           sizeof unterminated);
    if (Dkc3VideoInitScriptPaletteForType(bank, sizeof bank, 0x0284) != 0)
      return fprintf(stderr, "FAIL: unterminated init script accepted\n"), 1;
  }

  Dkc3VideoSetWidescreen(false);
  if (Dkc3VideoTerrainReady()) {
    fprintf(stderr, "FAIL: native mode retained widescreen terrain state\n");
    return 1;
  }

  puts("DKC3 video geometry tests passed");
  return 0;
}
