#include "dkc3_video.h"
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

bool g_ws_active;
int g_ws_extra;
static bool s_terrain_ready;
static Dkc3VideoAspect s_aspect;
static Dkc3VideoEdgePolicy s_edge_policy = kDkc3VideoEdgeGlide;

void Dkc3VideoSetEdgePolicy(Dkc3VideoEdgePolicy policy) {
  if (policy < kDkc3VideoEdgeReflect || policy >= kDkc3VideoEdgePolicyCount)
    policy = kDkc3VideoEdgeGlide;
  s_edge_policy = policy;
}

Dkc3VideoEdgePolicy Dkc3VideoGetEdgePolicy(void) {
  return s_edge_policy;
}

bool Dkc3VideoEdgePolicyFromName(const char *name,
                                 Dkc3VideoEdgePolicy *policy) {
  if (!name || !policy)
    return false;
  if (strcmp(name, "reflect") == 0 || strcmp(name, "mirror") == 0 ||
      strcmp(name, "0") == 0) {
    *policy = kDkc3VideoEdgeReflect;
    return true;
  }
  if (strcmp(name, "bars") == 0 || strcmp(name, "clamp") == 0 ||
      strcmp(name, "1") == 0) {
    *policy = kDkc3VideoEdgeBars;
    return true;
  }
  if (strcmp(name, "shift") == 0 || strcmp(name, "bias") == 0 ||
      strcmp(name, "2") == 0) {
    *policy = kDkc3VideoEdgeShift;
    return true;
  }
  if (strcmp(name, "glide") == 0 || strcmp(name, "3") == 0) {
    *policy = kDkc3VideoEdgeGlide;
    return true;
  }
  return false;
}

const char *Dkc3VideoEdgePolicyName(Dkc3VideoEdgePolicy policy) {
  switch (policy) {
    case kDkc3VideoEdgeBars:
      return "bars";
    case kDkc3VideoEdgeShift:
      return "shift";
    case kDkc3VideoEdgeGlide:
      return "glide";
    case kDkc3VideoEdgeReflect:
    default:
      return "reflect";
  }
}

void Dkc3VideoSetAspect(Dkc3VideoAspect aspect) {
  if (aspect < kDkc3VideoAspectNative || aspect >= kDkc3VideoAspectCount)
    aspect = kDkc3VideoAspectNative;
  if (s_aspect != aspect)
    s_terrain_ready = false;
  s_aspect = aspect;
  g_ws_active = aspect != kDkc3VideoAspectNative;
  switch (aspect) {
    case kDkc3VideoAspect16x10:
      g_ws_extra = kDkc3Video16x10Extra;
      break;
    case kDkc3VideoAspect16x9:
      g_ws_extra = kDkc3VideoWidescreenExtra;
      break;
    case kDkc3VideoAspect21x9:
      g_ws_extra = kDkc3Video21x9Extra;
      break;
    case kDkc3VideoAspectNative:
    default:
      g_ws_extra = 0;
      break;
  }
}

Dkc3VideoAspect Dkc3VideoGetAspect(void) {
  return s_aspect;
}

bool Dkc3VideoAspectFromName(const char *name, Dkc3VideoAspect *aspect) {
  if (!name || !aspect)
    return false;
  if (strcmp(name, "4:3") == 0 || strcmp(name, "native") == 0 ||
      strcmp(name, "0") == 0) {
    *aspect = kDkc3VideoAspectNative;
    return true;
  }
  if (strcmp(name, "16:10") == 0 || strcmp(name, "1") == 0) {
    *aspect = kDkc3VideoAspect16x10;
    return true;
  }
  if (strcmp(name, "16:9") == 0 || strcmp(name, "2") == 0) {
    *aspect = kDkc3VideoAspect16x9;
    return true;
  }
  if (strcmp(name, "21:9") == 0 || strcmp(name, "3") == 0) {
    *aspect = kDkc3VideoAspect21x9;
    return true;
  }
  return false;
}

const char *Dkc3VideoAspectName(Dkc3VideoAspect aspect) {
  switch (aspect) {
    case kDkc3VideoAspect16x10:
      return "16:10";
    case kDkc3VideoAspect16x9:
      return "16:9";
    case kDkc3VideoAspect21x9:
      return "21:9";
    case kDkc3VideoAspectNative:
    default:
      return "4:3";
  }
}

void Dkc3VideoSetWidescreen(bool enabled) {
  Dkc3VideoSetAspect(enabled ? kDkc3VideoAspect16x9
                             : kDkc3VideoAspectNative);
}

bool Dkc3VideoIsWidescreen(void) {
  return g_ws_active;
}

void Dkc3VideoSetTerrainReady(bool ready) {
  s_terrain_ready = g_ws_active && ready;
}

bool Dkc3VideoTerrainReady(void) {
  return g_ws_active && s_terrain_ready;
}

int Dkc3VideoWidth(void) {
  return kDkc3VideoNativeWidth + 2 * g_ws_extra;
}

int Dkc3VideoExtra(void) {
  return g_ws_extra;
}

size_t Dkc3VideoPixelCount(void) {
  return (size_t)Dkc3VideoWidth() * kDkc3VideoHeight;
}

static int Dkc3VideoClampInt(int value, int lower, int upper) {
  if (value < lower)
    return lower;
  if (value > upper)
    return upper;
  return value;
}

void Dkc3VideoPresentationMargins(uint16_t camera_x,
                                  uint16_t maximum_scroll_x,
                                  int *bias,
                                  int *left_margin,
                                  int *right_margin) {
  Dkc3VideoPresentationMarginsBounded(camera_x, 0x0100u, maximum_scroll_x,
                                      bias, left_margin, right_margin);
}

void Dkc3VideoMarginsForBias(uint16_t camera_x, uint16_t minimum_scroll_x,
                             uint16_t maximum_scroll_x, int bias,
                             int *left_margin, int *right_margin) {
  const int extra = Dkc3VideoExtra();
  int result_left = extra;
  int result_right = extra;
  const int lower = minimum_scroll_x < 0x0100u ? 0x0100 : minimum_scroll_x;
  const int upper = maximum_scroll_x;
  if (extra > 0 && upper >= lower) {
    if (s_edge_policy == kDkc3VideoEdgeShift ||
        s_edge_policy == kDkc3VideoEdgeGlide) {
      const int presented = (int)camera_x + bias;
      result_left = Dkc3VideoClampInt(presented - lower, 0, extra);
      result_right = Dkc3VideoClampInt(upper - presented, 0, extra);
    } else if (s_edge_policy == kDkc3VideoEdgeBars) {
      result_left = Dkc3VideoClampInt((int)camera_x - lower, 0, extra);
      result_right = Dkc3VideoClampInt(upper - (int)camera_x, 0, extra);
    }
  }
  if (left_margin)
    *left_margin = result_left;
  if (right_margin)
    *right_margin = result_right;
}

bool Dkc3VideoHoldWest(Dkc3VideoMetatileClassifier classify, void *context,
                       uint32_t window_column, unsigned reach,
                       uint32_t first_row, uint32_t last_row,
                       uint32_t *hold_column) {
  if (!classify || reach == 0 || window_column == 0 || last_row < first_row)
    return false;
  for (unsigned n = 1; n <= reach && n <= window_column; n++) {
    const uint32_t column = window_column - n;
    for (uint32_t row = first_row; row <= last_row; row++) {
      if (classify(context, column, row) != kDkc3VideoMetatileEmpty)
        return false;
    }
  }
  if (hold_column)
    *hold_column = window_column;
  return true;
}

void Dkc3VideoPresentationMarginsBounded(uint16_t camera_x,
                                         uint16_t minimum_scroll_x,
                                         uint16_t maximum_scroll_x,
                                         int *bias,
                                         int *left_margin,
                                         int *right_margin) {
  const int extra = Dkc3VideoExtra();
  int result_bias = 0;
  int result_left = extra;
  int result_right = extra;
  const int lower = minimum_scroll_x < 0x0100u ? 0x0100 : minimum_scroll_x;
  const int upper = maximum_scroll_x;
  if (extra > 0 && upper >= lower) {
    if (s_edge_policy == kDkc3VideoEdgeShift ||
        s_edge_policy == kDkc3VideoEdgeGlide) {
      const int range = upper - lower;
      int reach = range / 2;
      if (reach > extra)
        reach = extra;
      /* The pins: the presented center never leaves [lower+reach,
       * upper-reach], so the wide frame stays inside the level. */
      const int pin_low = lower + reach - (int)camera_x;
      const int pin_high = upper - reach - (int)camera_x;
      int wanted = 0;
      if (s_edge_policy == kDkc3VideoEdgeGlide) {
        /* One margin of inward shift at each wall, released one pixel per
         * kDkc3VideoEdgeGlideSpan pixels of camera travel away from it. */
        const int span = extra * kDkc3VideoEdgeGlideSpan;
        const int west_travel = (int)camera_x - lower;
        const int east_travel = upper - (int)camera_x;
        if (west_travel >= 0 && west_travel < span)
          wanted += extra - west_travel / kDkc3VideoEdgeGlideSpan;
        if (east_travel >= 0 && east_travel < span)
          wanted -= extra - east_travel / kDkc3VideoEdgeGlideSpan;
      }
      result_bias = Dkc3VideoClampInt(
          Dkc3VideoClampInt(wanted, pin_low, pin_high), -extra, extra);
      const int presented = (int)camera_x + result_bias;
      result_left = Dkc3VideoClampInt(presented - lower, 0, extra);
      result_right = Dkc3VideoClampInt(upper - presented, 0, extra);
    } else if (s_edge_policy == kDkc3VideoEdgeBars) {
      result_left = Dkc3VideoClampInt((int)camera_x - lower, 0, extra);
      result_right = Dkc3VideoClampInt(upper - (int)camera_x, 0, extra);
    }
  }
  if (bias)
    *bias = result_bias;
  if (left_margin)
    *left_margin = result_left;
  if (right_margin)
    *right_margin = result_right;
}

bool Dkc3VideoMarginLeavesAuthoredExtent(uint16_t camera_x,
                                         uint16_t maximum_scroll_x) {
  const int extra = Dkc3VideoExtra();
  const int lower = 0x0100;
  const int upper = maximum_scroll_x;
  if (extra <= 0 || upper < lower || s_edge_policy != kDkc3VideoEdgeReflect)
    return false;
  return (int)camera_x - lower < extra || upper - (int)camera_x < extra;
}

int Dkc3VideoResolveEdgeTile(uint32_t world_tile_x,
                             uint16_t maximum_scroll_x,
                             uint32_t *source_tile_x,
                             bool *mirror_horizontally) {
  const uint32_t origin_tile = 0x0100u >> 3;
  /* First world tile column at or beyond the authored extent. */
  const uint32_t extent_tile =
      ((uint32_t)maximum_scroll_x + kDkc3VideoNativeWidth) >> 3;
  if (mirror_horizontally)
    *mirror_horizontally = false;
  if (!source_tile_x)
    return -1;
  if (world_tile_x >= origin_tile && world_tile_x < extent_tile) {
    *source_tile_x = world_tile_x - origin_tile;
    return 0;
  }
  if (s_edge_policy != kDkc3VideoEdgeReflect || extent_tile <= origin_tile)
    return -1;
  uint32_t mirrored;
  if (world_tile_x < origin_tile) {
    /* Reflect about the boundary between world tiles 31 and 32:
     * 31 -> 32, 30 -> 33, and so on. */
    mirrored = 2u * origin_tile - 1u - world_tile_x;
  } else {
    /* Reflect about the boundary just before the first tile beyond the
     * extent: extent -> extent-1, extent+1 -> extent-2, and so on. */
    if (world_tile_x >= 2u * extent_tile)
      return -1;
    mirrored = 2u * extent_tile - 1u - world_tile_x;
  }
  if (mirrored < origin_tile || mirrored >= extent_tile)
    return -1;
  *source_tile_x = mirrored - origin_tile;
  if (mirror_horizontally)
    *mirror_horizontally = true;
  return 1;
}

bool Dkc3VideoTileTouchesWidescreenMargin(uint32_t world_tile_x,
                                          uint32_t camera_x) {
  const uint64_t left = (uint64_t)world_tile_x << 3;
  const uint64_t native_left = camera_x;
  const uint64_t native_right = native_left + kDkc3VideoNativeWidth;
  return left < native_left || left + 8u > native_right;
}

static int s_presentation_bias;

void Dkc3VideoSetPresentationBias(int bias) {
  s_presentation_bias = bias;
}

int Dkc3VideoPresentationBias(void) {
  return s_presentation_bias;
}

/*
 * The presented window is the cartridge camera shifted by the presentation
 * bias, with one margin on each side: world [camera + bias - extra,
 * camera + bias + 256 + extra). A camera-relative cull that natively
 * covers [-left, span - left) therefore needs extra - bias more slack on
 * the left and extra + bias more on the right; the span grows by two
 * margins either way. Without the bias term an object standing in the
 * right margin near a level's left wall (bias +extra) was released the
 * moment it passed the native span plus one margin while still on screen.
 */
/* DKC3_CULL_WIDEN=0 keeps every cull and activation window native while
 * the margins still present, for before/after comparisons. */
static bool Dkc3VideoCullWidenEnabled(void) {
  static int enabled = -1;
  if (enabled < 0) {
    const char *setting = getenv("DKC3_CULL_WIDEN");
    enabled = (setting && setting[0] == '0') ? 0 : 1;
  }
  return enabled != 0;
}

/* DKC3_CULL_TRACE=1 prints the first widened calls of each helper. */
static void Dkc3VideoCullTrace(const char *helper, unsigned native,
                               unsigned widened) {
  static int remaining = -1;
  if (remaining < 0)
    remaining = getenv("DKC3_CULL_TRACE") ? 12 : 0;
  if (remaining > 0 && widened != native) {
    remaining--;
    fprintf(stderr, "cull %s native=%u widened=%u\n", helper, native,
            widened);
  }
}

uint16_t Dkc3VideoExpandCullLeft(uint16_t native_margin) {
  if (!Dkc3VideoTerrainReady() || !Dkc3VideoCullWidenEnabled())
    return native_margin;
  int left = g_ws_extra - s_presentation_bias;
  if (left < 0)
    left = 0;
  if (left > 2 * g_ws_extra)
    left = 2 * g_ws_extra;
  Dkc3VideoCullTrace("left", native_margin, (unsigned)(native_margin + left));
  return (uint16_t)(native_margin + left);
}

uint16_t Dkc3VideoExpandCullSpan(uint16_t native_span) {
  const uint16_t widened = (uint16_t)(
      native_span +
      (Dkc3VideoTerrainReady() && Dkc3VideoCullWidenEnabled()
           ? 2 * g_ws_extra : 0));
  Dkc3VideoCullTrace("span", native_span, widened);
  return widened;
}

/* A window expressed as a right edge (camera + $100 and the like) grows by
 * what the span gains beyond the left margin: extra + bias. */
uint16_t Dkc3VideoExpandCullRight(uint16_t native_right) {
  const unsigned span = Dkc3VideoExpandCullSpan(0);
  const unsigned left = Dkc3VideoExpandCullLeft(0);
  const uint16_t widened = (uint16_t)(native_right + (span - left));
  Dkc3VideoCullTrace("right", native_right, widened);
  return widened;
}

size_t Dkc3VideoPlacementScanCells(uint16_t native_cell_offset,
                                    uint16_t row_stride,
                                    uint16_t cell_offsets[3]) {
  if (!cell_offsets)
    return 0;
  cell_offsets[0] = native_cell_offset;
  if (!g_ws_active || !Dkc3VideoCullWidenEnabled() || row_stride == 0 ||
      (native_cell_offset & 1u) != 0)
    return 1;

  size_t count = 1;
  const uint16_t cell = (uint16_t)(native_cell_offset / 2u);
  const uint16_t column = (uint16_t)(cell % row_stride);
  if (column != 0)
    cell_offsets[count++] = (uint16_t)(native_cell_offset - 2u);
  if ((uint16_t)(column + 1u) < row_stride &&
      native_cell_offset <= UINT16_MAX - 2u)
    cell_offsets[count++] = (uint16_t)(native_cell_offset + 2u);
  return count;
}

bool Dkc3VideoUsesUnderwaterSubscreenTint(
    uint8_t bg_mode, uint8_t main_layers, uint8_t sub_layers,
    uint8_t main_windowed, uint8_t sub_windowed, uint32_t window_select,
    uint8_t window1_left, uint8_t window1_right,
    uint8_t color_math_control, uint8_t color_math_layers,
    uint16_t fixed_color) {
  const bool underwater_layer_split =
      (main_layers == 0x15u && sub_layers == 0x02u) ||
      (main_layers == 0x05u && sub_layers == 0x12u) ||
      (main_layers == 0x04u && sub_layers == 0x13u) ||
      (main_layers == 0x17u && sub_layers == 0x13u);
  return bg_mode == 0x09u && underwater_layer_split &&
         main_windowed == 0x04u && sub_windowed == 0 &&
         (window_select & 0x0f00u) == 0x0300u && window1_left == 0 &&
         window1_right == 0xffu && color_math_control == 0x02u &&
         color_math_layers == 0x64u && fixed_color == 0;
}

size_t Dkc3VideoRepeatTransparentSubscreenMargins(
    uint16_t *pixels, size_t pixel_count, size_t native_offset,
    unsigned left_margin, unsigned right_margin) {
  if (!pixels || native_offset + kDkc3VideoNativeWidth > pixel_count ||
      left_margin > native_offset ||
      left_margin > kDkc3VideoNativeWidth ||
      right_margin > kDkc3VideoNativeWidth ||
      right_margin >
          pixel_count - native_offset - kDkc3VideoNativeWidth)
    return 0;

  /* The fill's premise is a subscreen the native view covers completely:
   * a transparent margin pixel then means missing adjacent content. Where
   * the native row itself has transparent subscreen pixels (open water with
   * no reflection behind it), a transparent margin pixel is authored and
   * must stay. */
  for (size_t index = 0; index < kDkc3VideoNativeWidth; index++)
    if ((pixels[native_offset + index] & 0xffu) == 0)
      return 0;
  int nearest_left[kDkc3VideoNativeWidth];
  int nearest_right[kDkc3VideoNativeWidth];
  int nearest = -1;
  for (size_t index = 0; index < kDkc3VideoNativeWidth; index++) {
    if ((pixels[native_offset + index] & 0xffu) != 0)
      nearest = (int)index;
    nearest_left[index] = nearest;
  }
  nearest = -1;
  for (size_t index = kDkc3VideoNativeWidth; index-- > 0;) {
    if ((pixels[native_offset + index] & 0xffu) != 0)
      nearest = (int)index;
    nearest_right[index] = nearest;
  }

  size_t filled = 0;
  for (unsigned distance = 1; distance <= left_margin; distance++) {
    const size_t destination = native_offset - distance;
    const size_t wrapped = kDkc3VideoNativeWidth - distance;
    uint16_t source_pixel = pixels[native_offset + wrapped];
    if ((source_pixel & 0xffu) == 0 &&
        (pixels[destination + 1] & 0xffu) != 0)
      source_pixel = pixels[destination + 1];
    if ((source_pixel & 0xffu) == 0) {
      int source = nearest_left[wrapped];
      const int right = nearest_right[wrapped];
      if (source < 0 ||
          (right >= 0 && right - (int)wrapped < (int)wrapped - source))
        source = right;
      if (source >= 0)
        source_pixel = pixels[native_offset + (size_t)source];
    }
    if ((pixels[destination] & 0xffu) == 0 &&
        (source_pixel & 0xffu) != 0) {
      pixels[destination] = source_pixel;
      filled++;
    }
  }
  for (unsigned distance = 0; distance < right_margin; distance++) {
    const size_t destination =
        native_offset + kDkc3VideoNativeWidth + distance;
    const size_t wrapped = distance;
    uint16_t source_pixel = pixels[native_offset + wrapped];
    if ((source_pixel & 0xffu) == 0 &&
        (pixels[destination - 1] & 0xffu) != 0)
      source_pixel = pixels[destination - 1];
    if ((source_pixel & 0xffu) == 0) {
      int source = nearest_left[wrapped];
      const int right = nearest_right[wrapped];
      if (source < 0 ||
          (right >= 0 && right - (int)wrapped < (int)wrapped - source))
        source = right;
      if (source >= 0)
        source_pixel = pixels[native_offset + (size_t)source];
    }
    if ((pixels[destination] & 0xffu) == 0 &&
        (source_pixel & 0xffu) != 0) {
      pixels[destination] = source_pixel;
      filled++;
    }
  }
  return filled;
}

uint16_t Dkc3VideoPromoteOamXHigh(uint16_t screen_x) {
  /* DKC3's banana renderer derives OAM's ninth X bit from bit 15 because
   * native play only needs that bit for negative coordinates. In the
   * widened right margin, $0100-$012a must therefore mirror bit 8 into the
   * sign position before the original packing sequence performs XBA/ASL. */
  if (Dkc3VideoTerrainReady() && (screen_x & 0x0100u))
    return (uint16_t)(screen_x | 0x8000u);
  return screen_x;
}

bool Dkc3VideoTilemapPagesCollide(const uint8_t bg_xsc[4],
                                  unsigned layer,
                                  uint8_t enabled_layers) {
  if (!bg_xsc || layer >= 4 || !(bg_xsc[layer] & 1u))
    return false;
  /* Tilemap pages are $400-word aligned, so two pages overlap exactly when
   * their addresses are equal. The 64-column extension pages are the odd
   * pages of the allocation (1, and 3 for a 64x64 map). If one of them is
   * another enabled background's base page, that background owns it and
   * this layer's "adjacent columns" are not its own. */
  const uint16_t base = (uint16_t)((bg_xsc[layer] & 0xfcu) << 8);
  const unsigned extension_pages = (bg_xsc[layer] & 2u) ? 2u : 1u;
  for (unsigned extension = 0; extension < extension_pages; extension++) {
    const uint16_t page = (uint16_t)(
        (base + (2u * extension + 1u) * 0x400u) & 0x7fffu);
    for (unsigned other = 0; other < 4; other++) {
      if (other == layer || !(enabled_layers & (1u << other)))
        continue;
      const uint16_t other_base = (uint16_t)((bg_xsc[other] & 0xfcu) << 8);
      if (page == other_base)
        return true;
    }
  }
  return false;
}

uint8_t Dkc3VideoPpuWideLayerMask(uint8_t bg_mode,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers) {
  /* DKC3's audited rolling level tilemaps use Mode 1. Mode 7 and other
   * special screens need explicit reconstruction rather than stale BGxSC
   * state accidentally opting them into the generic path. */
  if (!bg_xsc || (bg_mode & 7u) != 1u) return 0;

  uint8_t enabled = (uint8_t)((main_layers | sub_layers) & 0x0f);
  uint8_t mask = 0;
  for (unsigned layer = 0; layer < 2; layer++) {
    uint8_t bit = (uint8_t)(1u << layer);
    if ((enabled & bit) && (bg_xsc[layer] & 1u) &&
        !Dkc3VideoTilemapPagesCollide(bg_xsc, layer, enabled))
      mask = (uint8_t)(mask | bit);
  }
  return mask;
}

uint8_t Dkc3VideoPhysicalWideLayerMask(uint8_t bg_mode,
                                       const uint8_t bg_xsc[4],
                                       uint8_t main_layers,
                                       uint8_t sub_layers) {
  if (!bg_xsc || (bg_mode & 7u) != 1u)
    return 0;

  const uint8_t enabled =
      (uint8_t)((main_layers | sub_layers) & 0x07u);
  uint8_t mask = 0;
  for (unsigned layer = 0; layer < 3; layer++) {
    const uint8_t bit = (uint8_t)(1u << layer);
    if ((enabled & bit) && (bg_xsc[layer] & 1u) &&
        !Dkc3VideoTilemapPagesCollide(bg_xsc, layer, enabled))
      mask = (uint8_t)(mask | bit);
  }
  return mask;
}

uint8_t Dkc3VideoPhysicalWindowedLayerMask(
    uint8_t bg_mode, const uint8_t bg_xsc[4], uint8_t main_layers,
    uint8_t sub_layers, uint8_t main_windowed, uint8_t sub_windowed) {
  if (!bg_xsc || (bg_mode & 7u) != 1u ||
      !Dkc3VideoCullWidenEnabled())
    return 0;

  const uint8_t windowed = (uint8_t)(
      ((main_layers & main_windowed) | (sub_layers & sub_windowed)) & 0x07u);
  uint8_t mask = 0;
  for (unsigned layer = 0; layer < 3; layer++) {
    const uint8_t bit = (uint8_t)(1u << layer);
    if ((windowed & bit) && !(bg_xsc[layer] & 1u))
      mask = (uint8_t)(mask | bit);
  }
  return mask;
}

uint8_t Dkc3VideoRepeatLayerMask(uint8_t bg_mode,
                                 uint8_t main_layers,
                                 uint8_t sub_layers,
                                 uint8_t wide_layer_mask) {
  if ((bg_mode & 7u) != 1u)
    return 0;
  const uint8_t enabled =
      (uint8_t)((main_layers | sub_layers) & 0x07u);
  return (uint8_t)(enabled & (uint8_t)~wide_layer_mask);
}

uint16_t Dkc3VideoScrollPhaseDistance(uint16_t a, uint16_t b) {
  uint16_t distance = (uint16_t)((a - b) & 0x03ffu);
  if (distance > 0x0200u)
    distance = (uint16_t)(0x0400u - distance);
  return distance;
}

bool Dkc3VideoScrollAtTerrainPhase(uint16_t h_scroll,
                                   uint16_t v_scroll,
                                   uint16_t terrain_h_scroll,
                                   uint16_t terrain_v_scroll) {
  return Dkc3VideoScrollPhaseDistance(h_scroll, terrain_h_scroll) <=
             kDkc3VideoTerrainPhaseLeadX &&
         Dkc3VideoScrollPhaseDistance(v_scroll, terrain_v_scroll) <=
             kDkc3VideoTerrainPhaseLeadY;
}

bool Dkc3VideoPpuCanExtend(uint8_t bg_mode,
                           const uint8_t bg_xsc[4],
                           uint8_t main_layers,
                           uint8_t sub_layers) {
  return Dkc3VideoPpuWideLayerMask(
             bg_mode, bg_xsc, main_layers, sub_layers) != 0;
}

int Dkc3VideoTerrainLayer(uint8_t wide_layer_mask,
                          const uint8_t bg_xsc[4],
                          uint16_t stream_vram_word_address) {
  if (!bg_xsc)
    return -1;

  const uint16_t stream_base =
      (uint16_t)(stream_vram_word_address & 0xfc00u);
  for (int layer = 0; layer < 2; layer++) {
    const uint8_t bit = (uint8_t)(1u << layer);
    const uint16_t map_base =
        (uint16_t)((uint16_t)(bg_xsc[layer] & 0xfcu) << 8);
    if ((wide_layer_mask & bit) && map_base == stream_base)
      return layer;
  }
  return -1;
}

/* The layout of the level the cartridge is presenting, from the map
 * shape nibble at $0470 (docs/BRINGUP.md). Shapes 0 and 8 are the
 * column-major sixteen-row maps DKC2 calls horizontal; 4, 5, 6/9, and 7
 * are row-major with 64-, 32-, 192-, and 160-byte rows, the vertical,
 * narrow-vertical, square, and ship-hold strides DKC2 already decodes.
 * Shape 1, column-major with thirty-two rows, has no decoder yet and
 * stays unknown, so its margins are black. Any value outside a level is
 * unknown too. */
Dkc3VideoLevelLayout Dkc3VideoLevelLayoutForScene(
    uint16_t map_shape, uint16_t level_number) {
  (void)level_number;
  switch (map_shape) {
    case 0x0:
    case 0x8:
      return kDkc3VideoLevelLayoutHorizontal;
    case 0x4:
      return kDkc3VideoLevelLayoutVertical;
    case 0x5:
      return kDkc3VideoLevelLayoutNarrowVertical;
    case 0x6:
    case 0x9:
      return kDkc3VideoLevelLayoutSquare;
    case 0x7:
      return kDkc3VideoLevelLayoutShipHold;
    default:
      return kDkc3VideoLevelLayoutUnknown;
  }
}

uint32_t Dkc3VideoUnwrapPpuScroll(uint16_t ppu_scroll, uint32_t anchor) {
  const uint32_t period = 0x400u;
  const uint32_t half_period = period / 2u;
  uint32_t candidate = (anchor & ~(period - 1u)) |
                       ((uint32_t)ppu_scroll & (period - 1u));
  if (candidate + half_period < anchor)
    candidate += period;
  else if (candidate > anchor + half_period && candidate >= period)
    candidate -= period;
  return candidate;
}

uint32_t Dkc3VideoTerrainShadowY(uint16_t ppu_scroll_y, uint32_t camera_y) {
  /* Keep the shadow origin in the same epoch as the tile-row decoder.
   * Dkc3VideoLevelSourceTileY aligns the PPU value to an 8-pixel row before
   * unwrapping it. Unwrapping the fine value independently can choose the
   * opposite 1024-pixel epoch at the exact +/-512 tie. Pirate Panic reaches
   * that boundary at camera Y=$0204 / PPU Y=$0004 after Rambi's charge: the
   * prefill was keyed at tile row 128 while margin lookup started at row 0,
   * producing a one-frame verified-blank strip. Unwrap the common tile
   * origin once, then restore the rendered fine phase. */
  const uint16_t tile_origin = (uint16_t)(ppu_scroll_y & 0x03f8u);
  return Dkc3VideoUnwrapPpuScroll(tile_origin, camera_y) +
         (uint32_t)(ppu_scroll_y & 7u);
}

uint32_t Dkc3VideoTerrainShadowX(uint16_t ppu_scroll_x, uint32_t camera_x) {
  return Dkc3VideoUnwrapPpuScroll(ppu_scroll_x, camera_x);
}

uint32_t Dkc3VideoLevelSourceTileY(uint16_t ppu_scroll_y,
                                   uint32_t camera_y,
                                   uint32_t viewport_tile_row) {
  /* Unwrap the top rendered tile once, then walk the viewport in a single
   * continuous world domain. Unwrapping each row independently can choose
   * opposite 1024-pixel epochs around the +/-512 midpoint: Mainbrace at
   * camera Y=$069c / PPU Y=$009b mapped row 0 to world tile 275 but row 1
   * backward to 148. The same discontinuity cut Parrot Chute Panic's BG2
   * margins during rapid vertical motion. */
  const uint32_t top = Dkc3VideoUnwrapPpuScroll(
      (uint16_t)(ppu_scroll_y & 0x03f8u), camera_y);
  return (top >> 3) + viewport_tile_row;
}

uint32_t Dkc3VideoLevelMapTileY(uint16_t ppu_scroll_y,
                                uint32_t camera_y,
                                uint32_t viewport_tile_row) {
  const int64_t source_anchor = (int64_t)camera_y - 0x0100;
  int64_t source_y = (int64_t)(ppu_scroll_y & 0x00f8u);
  while (source_y + 0x80 < source_anchor)
    source_y += 0x100;
  while (source_y > source_anchor + 0x80)
    source_y -= 0x100;
  source_y += (int64_t)viewport_tile_row * 8;
  /* The horizontal map address is periodic in its low 16-bit world Y. Keep
   * a negative page representable without passing an out-of-range uint32_t
   * to Dkc3VideoDecodeLevelTile. */
  return (uint32_t)(source_y / 8) & 0x1fffu;
}

static uint16_t Dkc3VideoReadBankWord(const uint8_t *bank_data,
                                      uint16_t address) {
  uint16_t next = (uint16_t)(address + 1u);
  return (uint16_t)bank_data[address] |
         ((uint16_t)bank_data[next] << 8);
}

bool Dkc3VideoDecodeLevelTile(const uint8_t *bank_data,
                              size_t bank_size,
                              uint16_t level_map_base,
                              uint16_t metatile_base,
                              Dkc3VideoLevelLayout layout,
                              uint32_t world_tile_x,
                              uint32_t world_tile_y,
                              uint16_t *tile_entry) {
  if (!bank_data || bank_size < 0x10000u || !tile_entry)
    return false;
  if (world_tile_x > 0x1fffu || world_tile_y > 0x1fffu)
    return false;

  const uint16_t world_x = (uint16_t)(world_tile_x << 3);
  const uint16_t world_y = (uint16_t)(world_tile_y << 3);
  uint16_t map_offset = 0;
  if (layout == kDkc3VideoLevelLayoutHorizontal) {
    map_offset = (uint16_t)((world_x & 0xffe0u) +
                            ((world_y & 0x01e0u) >> 4));
  } else {
    const unsigned row_bytes = Dkc3VideoLevelLayoutRowBytes(layout);
    if (row_bytes == 0)
      return false;
    return Dkc3VideoDecodeLevelTileRowMajor(
        bank_data, bank_size, level_map_base, metatile_base, row_bytes,
        world_tile_x, world_tile_y, tile_entry);
  }
  const uint16_t metatile = Dkc3VideoReadBankWord(
      bank_data, (uint16_t)(level_map_base + map_offset));

  unsigned sub_x = (unsigned)world_tile_x & 3u;
  unsigned sub_y = (unsigned)world_tile_y & 3u;
  const uint16_t flips = (uint16_t)(metatile & 0xc000u);
  if (flips & 0x4000u)
    sub_x = 3u - sub_x;
  if (flips & 0x8000u)
    sub_y = 3u - sub_y;

  /*
   * Match the cartridge's five ASLs followed immediately by ADC. The final
   * ASL carry (original metatile bit 11) participates in the address.
   */
  const uint16_t scaled =
      (uint16_t)((uint16_t)(metatile << 5) +
                 ((metatile & 0x0800u) ? 1u : 0u));
  const uint16_t definition_offset =
      (uint16_t)(scaled + (uint16_t)(sub_y * 8u + sub_x * 2u));
  const uint16_t source = Dkc3VideoReadBankWord(
      bank_data, (uint16_t)(metatile_base + definition_offset));
  *tile_entry = (uint16_t)(source ^ flips);
  return true;
}

bool Dkc3VideoFindTransparent4bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry) {
  if (!vram || word_count < 0x8000u || !tile_entry)
    return false;

  for (uint16_t tile = 0; tile < 0x0400u; tile++) {
    const uint16_t address =
        (uint16_t)(character_base + (uint16_t)(tile * 16u));
    bool transparent = true;
    for (unsigned word = 0; word < 16u; word++) {
      if (vram[(address + word) & 0x7fffu] != 0) {
        transparent = false;
        break;
      }
    }
    if (transparent) {
      *tile_entry = tile;
      return true;
    }
  }
  return false;
}

bool Dkc3VideoCharacterIsTransparent(const uint16_t *vram,
                                     size_t word_count,
                                     uint16_t character_base,
                                     uint16_t tile_entry) {
  if (!vram || word_count < 0x8000u)
    return false;
  const uint16_t address =
      (uint16_t)(character_base + (uint16_t)((tile_entry & 0x03ffu) * 16u));
  for (unsigned word = 0; word < 16u; word++) {
    if (vram[(address + word) & 0x7fffu] != 0)
      return false;
  }
  return true;
}

bool Dkc3VideoFindTransparent2bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry) {
  if (!vram || word_count < 0x8000u || !tile_entry)
    return false;

  for (uint16_t tile = 0; tile < 0x0400u; tile++) {
    const uint16_t address =
        (uint16_t)(character_base + (uint16_t)(tile * 8u));
    bool transparent = true;
    for (unsigned word = 0; word < 8u; word++) {
      if (vram[(address + word) & 0x7fffu] != 0) {
        transparent = false;
        break;
      }
    }
    if (transparent) {
      *tile_entry = tile;
      return true;
    }
  }
  return false;
}




static bool Dkc3VideoWallRelation(Dkc3VideoMetatileClassifier classify,
                                  void *context, uint32_t target_x,
                                  uint32_t source_x, uint32_t behind_x,
                                  uint32_t metatile_y) {
  return classify(context, target_x, metatile_y) == kDkc3VideoMetatileEmpty &&
         classify(context, source_x, metatile_y) == kDkc3VideoMetatileFull &&
         classify(context, behind_x, metatile_y) == kDkc3VideoMetatileFull;
}

bool Dkc3VideoFindStructuralWallSource(Dkc3VideoMetatileClassifier classify,
                                       void *context,
                                       bool east_side,
                                       uint32_t target_metatile_x,
                                       uint32_t edge_metatile_x,
                                       uint32_t metatile_y,
                                       uint32_t *source_metatile_x) {
  if (!classify || !source_metatile_x)
    return false;
  if (east_side ? target_metatile_x <= edge_metatile_x
                : target_metatile_x >= edge_metatile_x)
    return false;
  if (classify(context, target_metatile_x, metatile_y) !=
      kDkc3VideoMetatileEmpty)
    return false;
  /* Walk from the target toward the native edge; the first cell that is not
   * empty decides. A partial cell is an authored opening. */
  uint32_t candidate = target_metatile_x;
  bool found = false;
  for (;;) {
    if (east_side) {
      if (candidate == 0 || candidate - 1 < edge_metatile_x)
        break;
      candidate--;
    } else {
      if (candidate + 1 > edge_metatile_x)
        break;
      candidate++;
    }
    const Dkc3VideoMetatileFill fill =
        classify(context, candidate, metatile_y);
    if (fill == kDkc3VideoMetatileEmpty)
      continue;
    found = fill == kDkc3VideoMetatileFull;
    break;
  }
  if (!found)
    return false;
  /* A wall is at least two metatiles thick: the cell behind the source,
   * toward the native center, must be full as well. A one-cell mast, crate,
   * or post standing in open sky is not a wall to continue. */
  if (east_side && candidate == 0)
    return false;
  const uint32_t behind = east_side ? candidate - 1u : candidate + 1u;
  if (classify(context, behind, metatile_y) != kDkc3VideoMetatileFull)
    return false;
  /* The same empty-target/thick-wall relationship on an adjacent row
   * distinguishes a continuing wall from an isolated block or decoration.
   * The proving row must show the wall two thick as well: a one-cell mast
   * with a sign hung beside it on a single row is not a wall on that row
   * either (Topsail Trouble's rigging, where the rule once put mast wood
   * into the sky at the corner of a 16:9 frame). */
  const bool above =
      metatile_y > 0 &&
      Dkc3VideoWallRelation(classify, context, target_metatile_x, candidate,
                            behind, metatile_y - 1u);
  const bool below =
      Dkc3VideoWallRelation(classify, context, target_metatile_x, candidate,
                            behind, metatile_y + 1u);
  if (!above && !below)
    return false;
  *source_metatile_x = candidate;
  return true;
}

bool Dkc3VideoIsTransparentTileEntry(uint16_t tile_entry,
                                     uint16_t transparent_tile) {
  return (tile_entry & 0x03ffu) == (transparent_tile & 0x03ffu);
}





unsigned Dkc3VideoTilemapPages(uint8_t bg_sc, uint8_t pages[4]) {
  if (!pages)
    return 0;
  const unsigned first = (unsigned)((bg_sc & 0xfcu) << 8) >> 10;
  const unsigned count = (bg_sc & 3u) == 3u ? 4u : (bg_sc & 3u) ? 2u : 1u;
  for (unsigned index = 0; index < count; index++)
    pages[index] = (uint8_t)((first + index) & 31u);
  return count;
}

/* A tilemap cell paints nothing when its entry is zero or names a
 * character whose pixels are all zero. Toxic Tower's top-of-screen wall
 * map fills the cells beyond its slanted edge with entry $8000, a flip
 * flag over character 0, which a non-zero test took for painting. */
static bool Dkc3VideoCellBlank(const uint16_t *vram, size_t word_count,
                               uint16_t character_base, uint16_t entry) {
  return entry == 0 ||
         Dkc3VideoCharacterIsTransparent(vram, word_count, character_base,
                                         entry);
}

uint64_t Dkc3VideoTilemapBrokenRows(const uint16_t *vram, size_t word_count,
                                    uint8_t bg_sc, uint16_t character_base) {
  if (!vram || word_count < 0x8000u || !(bg_sc & 1u))
    return 0;
  const unsigned base = (unsigned)(bg_sc & 0xfcu) << 8;
  const unsigned rows = (bg_sc & 2u) ? 64u : 32u;
  uint64_t broken = 0;
  for (unsigned row = 0; row < rows; row++) {
    unsigned nonzero = 0, lead = 64, trail = 64;
    for (unsigned column = 0; column < 64; column++) {
      const unsigned word = base + (column >= 32u ? 0x400u : 0u) +
                            (row >= 32u ? 0x800u : 0u) +
                            ((row & 31u) << 5) + (column & 31u);
      if (!Dkc3VideoCellBlank(vram, word_count, character_base,
                              vram[word & 0x7fffu])) {
        nonzero++;
        if (lead == 64u)
          lead = column;
        trail = 63u - column;
      }
    }
    if (nonzero >= (unsigned)kDkc3VideoPlaneDenseCells &&
        (lead >= (unsigned)kDkc3VideoPlaneEdgeStrip ||
         trail >= (unsigned)kDkc3VideoPlaneEdgeStrip))
      broken |= (uint64_t)1 << row;
  }
  return broken;
}

bool Dkc3VideoTilemapWrapsAuthored(const uint16_t *vram, size_t word_count,
                                   uint8_t bg_sc, uint16_t character_base) {
  if (!vram || word_count < 0x8000u || !(bg_sc & 1u))
    return false;
  const unsigned base = (unsigned)(bg_sc & 0xfcu) << 8;
  const unsigned rows = (bg_sc & 2u) ? 64u : 32u;
  bool populated = false;
  unsigned leading = 64;
  unsigned trailing = 64;
  for (unsigned row = 0; row < rows; row++) {
    uint16_t entries[64];
    unsigned nonzero = 0;
    for (unsigned column = 0; column < 64; column++) {
      const unsigned word = base + (column >= 32u ? 0x400u : 0u) +
                            (row >= 32u ? 0x800u : 0u) +
                            ((row & 31u) << 5) + (column & 31u);
      entries[column] = vram[word & 0x7fffu];
      if (Dkc3VideoCellBlank(vram, word_count, character_base,
                             entries[column]))
        entries[column] = 0;
      else
        nonzero++;
    }
    if (!nonzero)
      continue;
    populated = true;
    unsigned lead = 0;
    while (lead < 64u && !entries[lead])
      lead++;
    unsigned trail = 0;
    while (trail < 64u && !entries[63u - trail])
      trail++;
    if (lead < leading)
      leading = lead;
    if (trail < trailing)
      trailing = trail;
    if (nonzero < 8u)
      continue;
    for (unsigned period = 1; period <= 32u; period++) {
      bool periodic = true;
      for (unsigned column = 0; column + period < 64u && periodic; column++)
        periodic = entries[column] == entries[column + period];
      if (periodic) {
        if (64u % period != 0u)
          return false;
        break;
      }
    }
  }
  return populated && leading < kDkc3VideoPlaneEdgeStrip &&
         trailing < kDkc3VideoPlaneEdgeStrip;
}

bool Dkc3VideoMirrorSourceTileAcrossEdge(uint32_t source_tile_x,
                                         uint32_t edge_source_tile,
                                         bool east_side,
                                         uint32_t *mirrored_tile_x) {
  if (!mirrored_tile_x)
    return false;
  if (east_side) {
    if (source_tile_x <= edge_source_tile)
      return false;
    const uint32_t distance = source_tile_x - edge_source_tile;
    if (distance > edge_source_tile + 1u)
      return false;
    *mirrored_tile_x = edge_source_tile + 1u - distance;
    return true;
  }
  if (source_tile_x >= edge_source_tile)
    return false;
  *mirrored_tile_x = 2u * edge_source_tile - 1u - source_tile_x;
  return true;
}

bool Dkc3VideoSelectTerrainPhase(const Dkc3HdmaBands *bands,
                                 int layer,
                                 uint16_t frame_h,
                                 uint16_t frame_v,
                                 uint32_t camera_x,
                                 uint32_t camera_y,
                                 uint16_t *phase_h,
                                 uint16_t *phase_v) {
  if (phase_h)
    *phase_h = frame_h;
  if (phase_v)
    *phase_v = frame_v;
  const uint16_t camera_h = (uint16_t)(camera_x & 0x03ffu);
  const uint16_t camera_v = (uint16_t)(camera_y & 0x03ffu);
  if (!bands || layer < 0 || layer >= 4 || !phase_h || !phase_v ||
      Dkc3VideoScrollAtTerrainPhase(frame_h, frame_v, camera_h, camera_v))
    return false;
  int best_lines = 0;
  uint16_t best_h = frame_h;
  uint16_t best_v = frame_v;
  for (int index = 0; index < bands->count; index++) {
    const Dkc3HdmaBand *band = &bands->band[index];
    const uint16_t h = band->h_scroll[layer];
    const uint16_t v = band->v_scroll[layer];
    if (!Dkc3VideoScrollAtTerrainPhase(h, v, camera_h, camera_v))
      continue;
    int lines = 0;
    for (int other = index; other < bands->count; other++) {
      const Dkc3HdmaBand *candidate = &bands->band[other];
      if (candidate->h_scroll[layer] == h && candidate->v_scroll[layer] == v)
        lines += (int)candidate->last_line - (int)candidate->first_line + 1;
    }
    if (lines > best_lines) {
      best_lines = lines;
      best_h = h;
      best_v = v;
    }
  }
  if (best_lines == 0) {
    /* Nothing renders at the camera phase. When the HDMA table sets the
     * terrain layer's scroll from its first line, the frame-start register
     * is not what the frame renders: Toxic Tower's Rattly bounces move the
     * camera five pixels a frame and BG1's scroll follows through HDMA,
     * leaving the register a frame behind; keyed on the register, every
     * band read as off-phase and the whole layer repeated its ring into the
     * margins for that frame. Adopt the scroll that covers at least half
     * the frame's lines when it lies within the follow distance of the
     * camera. */
    int dominant_lines = 0;
    uint16_t dominant_h = frame_h, dominant_v = frame_v;
    for (int index = 0; index < bands->count; index++) {
      const Dkc3HdmaBand *band = &bands->band[index];
      const uint16_t h = band->h_scroll[layer];
      const uint16_t v = band->v_scroll[layer];
      int lines = 0;
      for (int other = 0; other < bands->count; other++) {
        const Dkc3HdmaBand *candidate = &bands->band[other];
        if (candidate->h_scroll[layer] == h &&
            candidate->v_scroll[layer] == v)
          lines += (int)candidate->last_line - (int)candidate->first_line + 1;
      }
      if (lines > dominant_lines) {
        dominant_lines = lines;
        dominant_h = h;
        dominant_v = v;
      }
    }
    if (dominant_lines * 2 < kDkc3HdmaLastLine ||
        (dominant_h == frame_h && dominant_v == frame_v) ||
        Dkc3VideoScrollPhaseDistance(dominant_h, camera_h) >
            kDkc3VideoTerrainPhaseFollowX ||
        Dkc3VideoScrollPhaseDistance(dominant_v, camera_v) >
            kDkc3VideoTerrainPhaseFollowY)
      return false;
    *phase_h = dominant_h;
    *phase_v = dominant_v;
    return true;
  }
  *phase_h = best_h;
  *phase_v = best_v;
  return true;
}

unsigned Dkc3VideoLevelLayoutRowBytes(Dkc3VideoLevelLayout layout) {
  switch (layout) {
    case kDkc3VideoLevelLayoutVertical:
      return 64u;
    case kDkc3VideoLevelLayoutSquare:
      return 192u;
    case kDkc3VideoLevelLayoutNarrowVertical:
      return 32u;
    case kDkc3VideoLevelLayoutShipHold:
      return 160u;
    default:
      return 0u;
  }
}

bool Dkc3VideoDecodeLevelTileRowMajor(const uint8_t *bank_data,
                                      size_t bank_size,
                                      uint16_t level_map_base,
                                      uint16_t metatile_base,
                                      unsigned row_bytes,
                                      uint32_t world_tile_x,
                                      uint32_t world_tile_y,
                                      uint16_t *tile_entry) {
  if (!bank_data || bank_size < 0x10000u || !tile_entry || row_bytes == 0)
    return false;
  if (world_tile_x > 0x1fffu || world_tile_y > 0x1fffu)
    return false;
  const uint16_t world_x = (uint16_t)(world_tile_x << 3);
  const uint16_t world_y = (uint16_t)(world_tile_y << 3);
  const uint16_t map_offset =
      (uint16_t)(((world_x & 0xffe0u) >> 4) +
                 ((world_y & 0xffe0u) >> 5) * row_bytes);
  const uint16_t metatile = Dkc3VideoReadBankWord(
      bank_data, (uint16_t)(level_map_base + map_offset));
  unsigned sub_x = (unsigned)world_tile_x & 3u;
  unsigned sub_y = (unsigned)world_tile_y & 3u;
  const uint16_t flips = (uint16_t)(metatile & 0xc000u);
  if (flips & 0x4000u)
    sub_x = 3u - sub_x;
  if (flips & 0x8000u)
    sub_y = 3u - sub_y;
  const uint16_t scaled =
      (uint16_t)((uint16_t)(metatile << 5) +
                 ((metatile & 0x0800u) ? 1u : 0u));
  const uint16_t definition_offset =
      (uint16_t)(scaled + (uint16_t)(sub_y * 8u + sub_x * 2u));
  const uint16_t source = Dkc3VideoReadBankWord(
      bank_data, (uint16_t)(metatile_base + definition_offset));
  *tile_entry = (uint16_t)(source ^ flips);
  return true;
}

bool Dkc3VideoReadLevelMetatile(const uint8_t *bank_data, size_t bank_size,
                                uint16_t level_map_base,
                                Dkc3VideoLevelLayout layout,
                                unsigned row_bytes,
                                uint32_t metatile_x, uint32_t metatile_y,
                                uint16_t *metatile) {
  if (!bank_data || bank_size < 0x10000u || !metatile)
    return false;
  if (metatile_x > 0x7ffu || metatile_y > 0x7ffu)
    return false;
  uint16_t map_offset = 0;
  if (layout == kDkc3VideoLevelLayoutHorizontal) {
    map_offset = (uint16_t)((metatile_x << 5) + ((metatile_y & 15u) << 1));
  } else {
    if (row_bytes == 0)
      row_bytes = Dkc3VideoLevelLayoutRowBytes(layout);
    if (row_bytes == 0)
      return false;
    map_offset = (uint16_t)((metatile_x << 1) + metatile_y * row_bytes);
  }
  *metatile = (uint16_t)(Dkc3VideoReadBankWord(
                             bank_data, (uint16_t)(level_map_base + map_offset)) &
                         0x3fffu);
  return true;
}

static void Dkc3VideoLevelMapExtent(uint16_t level_map_base, uint16_t map_end,
                                    Dkc3VideoLevelLayout layout,
                                    unsigned row_bytes, uint32_t *columns,
                                    uint32_t *rows) {
  const uint32_t span = map_end > level_map_base
                            ? (uint32_t)map_end - level_map_base : 0u;
  *columns = 0u;
  *rows = 0u;
  if (layout == kDkc3VideoLevelLayoutHorizontal) {
    *columns = span / 32u;
    *rows = 16u;
    return;
  }
  if (row_bytes == 0)
    row_bytes = Dkc3VideoLevelLayoutRowBytes(layout);
  if (row_bytes == 0)
    return;
  *columns = row_bytes / 2u;
  *rows = span / row_bytes;
}

unsigned Dkc3VideoMetatileNeighbours(const uint8_t *bank_data,
                                     size_t bank_size,
                                     uint16_t level_map_base,
                                     uint16_t map_end,
                                     Dkc3VideoLevelLayout layout,
                                     unsigned row_bytes, uint16_t metatile,
                                     bool east_side, uint16_t *ids,
                                     uint16_t *counts, unsigned capacity) {
  enum { kSlots = 32 };
  uint16_t slot_id[kSlots];
  uint16_t slot_count[kSlots];
  unsigned slots = 0;
  uint32_t columns = 0, rows = 0;
  if (!bank_data || bank_size < 0x10000u || !ids || !counts || capacity == 0)
    return 0;
  Dkc3VideoLevelMapExtent(level_map_base, map_end, layout, row_bytes,
                          &columns, &rows);
  for (uint32_t my = 0; my < rows; my++) {
    for (uint32_t mx = 0; mx < columns; mx++) {
      uint16_t id = 0, beside = 0;
      if (!Dkc3VideoReadLevelMetatile(bank_data, bank_size, level_map_base,
                                      layout, row_bytes, mx, my, &id) ||
          id != metatile)
        continue;
      if (east_side ? mx + 1u >= columns : mx == 0)
        continue;
      if (!Dkc3VideoReadLevelMetatile(bank_data, bank_size, level_map_base,
                                      layout, row_bytes,
                                      east_side ? mx + 1u : mx - 1u, my,
                                      &beside) ||
          beside == 0)
        continue;
      unsigned s = 0;
      while (s < slots && slot_id[s] != beside)
        s++;
      if (s == slots) {
        if (slots == kSlots)
          continue;
        slot_id[slots] = beside;
        slot_count[slots] = 0;
        slots++;
      }
      if (slot_count[s] < 0xffffu)
        slot_count[s]++;
    }
  }
  /* Most frequent first; ties keep the lower id so the order is stable. */
  unsigned written = 0;
  while (written < capacity && written < slots) {
    unsigned best = written;
    for (unsigned s = written + 1u; s < slots; s++) {
      if (slot_count[s] > slot_count[best] ||
          (slot_count[s] == slot_count[best] && slot_id[s] < slot_id[best]))
        best = s;
    }
    const uint16_t tmp_id = slot_id[written];
    const uint16_t tmp_count = slot_count[written];
    slot_id[written] = slot_id[best];
    slot_count[written] = slot_count[best];
    slot_id[best] = tmp_id;
    slot_count[best] = tmp_count;
    ids[written] = slot_id[written];
    counts[written] = slot_count[written];
    written++;
  }
  return written;
}

bool Dkc3VideoDecodeMetatileEntry(const uint8_t *bank_data, size_t bank_size,
                                  uint16_t metatile_base,
                                  uint16_t metatile_word, unsigned sub_x,
                                  unsigned sub_y, uint16_t *tile_entry) {
  if (!bank_data || bank_size < 0x10000u || !tile_entry || sub_x > 3u ||
      sub_y > 3u)
    return false;
  const uint16_t flips = (uint16_t)(metatile_word & 0xc000u);
  if (flips & 0x4000u)
    sub_x = 3u - sub_x;
  if (flips & 0x8000u)
    sub_y = 3u - sub_y;
  const uint16_t scaled =
      (uint16_t)((uint16_t)(metatile_word << 5) +
                 ((metatile_word & 0x0800u) ? 1u : 0u));
  const uint16_t definition_offset =
      (uint16_t)(scaled + (uint16_t)(sub_y * 8u + sub_x * 2u));
  const uint16_t source = Dkc3VideoReadBankWord(
      bank_data, (uint16_t)(metatile_base + definition_offset));
  *tile_entry = (uint16_t)(source ^ flips);
  return true;
}

bool Dkc3VideoTilemapIsObjectPlane(const uint16_t *vram, size_t word_count,
                                   uint8_t bg_sc, uint16_t character_base) {
  if (!vram || word_count < 0x8000u || !(bg_sc & 1u))
    return false;
  const unsigned base = (unsigned)(bg_sc & 0xfcu) << 8;
  const unsigned rows = (bg_sc & 2u) ? 64u : 32u;
  unsigned populated[2] = {0u, 0u};
  for (unsigned row = 0; row < rows; row++) {
    for (unsigned column = 0; column < 64u; column++) {
      const unsigned word = base + (column >= 32u ? 0x400u : 0u) +
                            (row >= 32u ? 0x800u : 0u) +
                            ((row & 31u) << 5) + (column & 31u);
      if (!Dkc3VideoCellBlank(vram, word_count, character_base,
                              vram[word & 0x7fffu]))
        populated[column >= 32u ? 1 : 0]++;
    }
  }
  return (populated[0] == 0u) != (populated[1] == 0u) &&
         populated[0] + populated[1] >= kDkc3VideoObjectPlaneMinCells;
}
