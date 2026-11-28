#ifndef DKC3_VIDEO_H
#define DKC3_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dkc3_hdma.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  kDkc3VideoNativeWidth = 256,
  kDkc3VideoHeight = 224,
  /*
   * SNES pixels are presented with a 7:6 pixel aspect ratio. A centered
   * 308x224 source is within one source pixel of 16:10 on that display grid.
   */
  kDkc3Video16x10Extra = 26,
  /*
   * SNES pixels are presented with a 7:6 pixel aspect ratio. At 224 lines,
   * 342 source columns produce a 1.78125 display aspect, within one source
   * pixel of exact 16:9. The odd ideal width (341 1/3) cannot be centered
   * symmetrically, so use 43 host-rendered columns on both sides.
   */
  kDkc3VideoWidescreenExtra = 43,
  kDkc3VideoWidescreenWidth =
      kDkc3VideoNativeWidth + 2 * kDkc3VideoWidescreenExtra,
  /*
   * Exact 21:9 would be 448x224 after the SNES 7:6 pixel aspect ratio, but
   * 96 columns per side exceeds the shared runtime's safe nine-bit OAM limit
   * by one. 95 columns per side gives the nearest symmetric safe width:
   * 446x224, or about 20.91:9 on the display grid.
   */
  kDkc3Video21x9Extra = 95,
  kDkc3Video21x9Width =
      kDkc3VideoNativeWidth + 2 * kDkc3Video21x9Extra,
  kDkc3VideoMaximumWidth = kDkc3Video21x9Width,
  kDkc3VideoBytesPerPixel = 4,
  /*
   * DKC3 can advance a scanline band's alternate terrain phase a few pixels
   * beyond the frame anchor while the camera reverses. Measured maxima are
   * six pixels horizontally and four vertically; larger differences are not
   * the same world plane.
   */
  kDkc3VideoTerrainPhaseLeadX = 6,
  kDkc3VideoTerrainPhaseLeadY = 4,
  /* How far from the camera the scroll covering most of a frame's lines
   * may lie and still be taken as the terrain phase when neither the
   * frame-start register nor any band is at the camera phase (see
   * Dkc3VideoSelectTerrainPhase). A parallax effect layer sits hundreds
   * of pixels off and never qualifies. */
  kDkc3VideoTerrainPhaseFollowX = 32,
  kDkc3VideoTerrainPhaseFollowY = 32,
  /* Extra horizontal lead-in (pixels per side) added in widescreen to
   * placement activation/despawn ($BB:AAD4) and sprite renderer culls ($B7)
   * so newly spawned objects can upload VRAM tiles before crossing the
   * widened screen edge. Overridable via DKC3_SPAWN_LEAD, clamped to
   * [0, kDkc3VideoMaxSpawnLead]. */
  kDkc3VideoDefaultSpawnLead = 48,
  kDkc3VideoMaxSpawnLead = 64,
  /* Maximum horizontal 256-pixel bucket radius/count returned by
   * Dkc3VideoPlacementScanCells: 1 neighbor per side at 16:10/16:9, and up to
   * 2 neighbors per side at 21:9 when wall bias + spawn lead (+ 32 px
   * asymmetric row slack) cross a second 256-pixel cell boundary. */
  kDkc3VideoPlacementScanMaxRadius = 2,
  kDkc3VideoPlacementScanMaxCells = 1 + 2 * kDkc3VideoPlacementScanMaxRadius,
};

typedef enum Dkc3VideoAspect {
  kDkc3VideoAspectNative = 0,
  kDkc3VideoAspect16x10,
  kDkc3VideoAspect16x9,
  kDkc3VideoAspect21x9,
  kDkc3VideoAspectCount,
} Dkc3VideoAspect;

typedef enum Dkc3VideoLevelLayout {
  kDkc3VideoLevelLayoutUnknown = 0,
  kDkc3VideoLevelLayoutHorizontal,
  kDkc3VideoLevelLayoutVertical,
  kDkc3VideoLevelLayoutSquare,
  kDkc3VideoLevelLayoutNarrowVertical,
  /* Ship-hold terrain is row-major with 80 metatiles (160 bytes) per row. */
  kDkc3VideoLevelLayoutShipHold,
  /* Shape 1: column-major, 32 metatile rows (64 bytes per column). */
  kDkc3VideoLevelLayoutTallHorizontal,
} Dkc3VideoLevelLayout;

/* Bleak's fixed, bounded Mode 2 maps wrap at 256 pixels. Reject other
 * scenes and active offset-per-tile tables before exposing that wrap. */
bool Dkc3VideoUsesSnowArenaPlanes(bool in_level, uint16_t level,
                                 uint8_t mode, const uint8_t maps[4],
                                 uint16_t character_bases, uint8_t hdma,
                                 const uint16_t *vram, size_t vram_words);

/* The KAOS arena of level $23 has no terrain stream: one static 64-column
 * plane ($71) holds the whole 512-pixel room. While KAOS fights, BG1 ($78)
 * is his 32-column body, BG2 the room and BG3 ($7C) the bounded sky; once
 * K. Roolenstein appears, BG1 is the room and BG2/BG3 share $7C. Requires
 * Mode 1, one of those map sets, and every scanline band of the room layer
 * verified as a static plane (plane_bands[layer] == band_count). Returns
 * the room's layer (1 or 0), or -1. */
int Dkc3VideoKaosArenaPlaneLayer(bool in_level, uint16_t level,
                                 uint8_t mode, const uint8_t maps[4],
                                 const int plane_bands[2], int band_count);

/* Margins that keep the arena plane inside its 512-pixel map: the view
 * never crosses the map's own wrap at either end. h_scroll is the room
 * layer's. */
void Dkc3VideoKaosArenaMargins(uint16_t h_scroll, int extra, int *left,
                               int *right);

/* In the K. Roolenstein phase, windows 1 (BG2) and 2 (BG3) are room-anchored
 * spans clamped to the screen: room x [west, east] at BG1 scroll s is
 * [west - s, east - s]. A register at 0 or 255 hides how far the span runs
 * past the screen edge. Returns the unclamped span in native screen x when
 * every unpinned register is within two pixels of the room-anchored edge and
 * every pinned one is consistent with it; false otherwise. */
enum {
  kDkc3KaosRoomWindowWest = 75,
  kDkc3KaosRoomWindow1East = 436,
  kDkc3KaosRoomWindow2East = 348,
};
bool Dkc3VideoKaosRoomWindow(uint8_t reg_left, uint8_t reg_right,
                             uint16_t room_h_scroll, int west, int east,
                             int *left, int *right);

/* KAOS's BG1 body is shown through window 1, inverted, which the arena's
 * HDMA sets per scanline ($2126/$2127, mode 1, direct). On the body's lines
 * the window is the body map's 256-pixel extent clamped to the screen:
 * [0, r] when the map starts left of the screen, [l, 255] when it starts
 * inside it. Scan the table (count bytes, bit 7 repeat, zero ends) for the
 * first pair consistent with BG1's scroll within two pixels (the latched
 * scroll can trail the WRAM copy), and return its registers and the
 * unclamped extent [*left, *right] in native screen x. */
bool Dkc3VideoKaosBodyWindow(const uint8_t *table, size_t length,
                             uint16_t bg1_h_scroll, uint8_t *reg_left,
                             uint8_t *reg_right, int *left, int *right);

/*
 * The Northern Kremisphere overworld (main-loop vector $B4:B179) keeps its
 * whole picture resident: a 64x32 BG1 map spans world x [$100, $300), the
 * camera word $196D sits $100 ahead of BG1HOFS, and the game writes map
 * words only into rows the frame does not show: once the hovercraft opens
 * the north, it streams whole 64-column rows as the camera climbs
 * (docs/BRINGUP.md, 2026-11-22). The scene qualifies only
 * in Mode 1 with BG1 a 64-column, 32-row map whose rendered world x ($100 +
 * the nine-bit BG1HOFS) lies in [$100, $200] and within the camera lead of
 * $196D, every other enabled 64-column BG layer at BG1's phase, and every
 * enabled layer 32 rows tall. The measured camera word runs up to two
 * pixels ahead of the latched scroll while the boat moves.
 * kDkc3OverworldCameraLead accepts up to eight pixels either way: a margin
 * over that measurement, not a measured bound. Only the two-pixel forward
 * lead has been observed; the negative side (camera behind the scroll) is
 * unmeasured tolerance. The host still has to prove every map write lands in
 * a row the frame hides, or in at most kDkc3OverworldShownRowWrites shown
 * rows per frame (see Dkc3VideoOverworldWritesAreRows).
 */
enum {
  kDkc3OverworldMainLoop = 0xb4b179,
  kDkc3OverworldMinimumCameraX = 0x0100,
  kDkc3OverworldMaximumCameraX = 0x0200,
  kDkc3OverworldCameraLead = 8,
};
bool Dkc3VideoIsResidentOverworld(uint32_t main_loop_vector, uint8_t bg_mode,
                                  const uint8_t bg_sc[4],
                                  uint8_t enabled_layers,
                                  const uint16_t h_scroll[3],
                                  uint16_t camera_x);

/* The overworld's rendered world x: $100 plus BG1's nine-bit scroll. */
uint16_t Dkc3VideoOverworldRenderedX(uint16_t bg1_h_scroll);

/* Margins for the overworld at its rendered world x, each clamped to the
 * resident map [$100, $300) under every edge policy: the map's 512-pixel wrap is not authored, no
 * mirror decoder exists for it, and the raw background path cannot follow
 * a presentation bias, so the view stays locked to the camera. */
void Dkc3VideoOverworldMargins(uint16_t rendered_x, int *left_margin,
                               int *right_margin);

/* Whether any of the 224 displayed lines falls in tilemap row `row` of a
 * 32-row (256-line) map at vertical scroll `v_scroll`. */
bool Dkc3VideoTilemapRowVisible(uint16_t v_scroll, unsigned row);

/*
 * Whether one frame's tilemap writes to an overworld layer are row
 * streaming, which leaves the resident map whole across the margins.
 * `changed_rows` has bit n set when any word of row n changed since the
 * previous drawn frame. The game writes each streamed row as a 32-word run
 * into both 256-pixel pages, one word into the next row: up to
 * kDkc3OverworldShownRowWrites shown rows per frame, the rest hidden. A
 * game streaming columns instead would touch every shown row, and the
 * margins would show columns it never wrote. With `vertical_hdma` the
 * layer's shown rows vary per line, so every changed row counts as shown.
 */
enum { kDkc3OverworldShownRowWrites = 2 };
bool Dkc3VideoOverworldWritesAreRows(uint32_t changed_rows, uint16_t v_scroll,
                                     bool vertical_hdma);

/* The BG1-BG3 layers (bit n = BG n+1) whose vertical scroll register an
 * active HDMA channel writes, given each channel's B-bus address and
 * transfer mode. Such a layer's visible rows vary per line. */
uint8_t Dkc3VideoHdmaVerticalScrollLayers(uint8_t active_channels,
                                          const uint8_t b_address[8],
                                          const uint8_t mode[8]);

/*
 * What a host margin shows where the level authors nothing: within one
 * margin of a hard level wall, and in rooms narrower than two margins.
 *
 * reflect: the presented view stays locked to the cartridge camera and the
 *          unauthored strip mirrors the nearest authored terrain columns.
 * bars:    the presented view stays locked to the cartridge camera and the
 *          unauthored strip is left black (the visible margin shrinks).
 * shift:   the presented view is moved inward so the margin never leaves the
 *          authored extent; the view therefore stands still for the first
 *          margin's worth of camera motion away from a wall, and every
 *          sprite, HUD included, slides by the same amount.
 */
typedef enum Dkc3VideoEdgePolicy {
  kDkc3VideoEdgeReflect = 0,
  kDkc3VideoEdgeBars,
  kDkc3VideoEdgeShift,
  /* Like shift the wide frame never leaves the authored level, but the
   * inward slide is released one pixel per kDkc3VideoEdgeGlideSpan pixels
   * of camera travel, so the background scrolls at seven eighths of the
   * camera speed until the view is centered instead of standing still.
   * A room shorter than the two releases (2 * extra * span pixels; the
   * bonus rooms) gets a smaller shift, range / (2 * span), with no pins and
   * each margin clamped to the room; one narrower than two margins gets
   * none and is exactly bars. The default. */
  kDkc3VideoEdgeGlide,
  kDkc3VideoEdgePolicyCount,
} Dkc3VideoEdgePolicy;

enum {
  kDkc3VideoEdgeGlideSpan = 8,
};

/* These symbols are the shared snesrecomp widescreen runtime contract. */
extern bool g_ws_active;
extern int g_ws_extra;

void Dkc3VideoSetWidescreen(bool enabled);
bool Dkc3VideoIsWidescreen(void);
void Dkc3VideoSetAspect(Dkc3VideoAspect aspect);
Dkc3VideoAspect Dkc3VideoGetAspect(void);
bool Dkc3VideoAspectFromName(const char *name, Dkc3VideoAspect *aspect);
const char *Dkc3VideoAspectName(Dkc3VideoAspect aspect);
void Dkc3VideoSetTerrainReady(bool ready);
bool Dkc3VideoTerrainReady(void);
int Dkc3VideoWidth(void);
int Dkc3VideoExtra(void);
size_t Dkc3VideoPixelCount(void);

void Dkc3VideoSetEdgePolicy(Dkc3VideoEdgePolicy policy);
Dkc3VideoEdgePolicy Dkc3VideoGetEdgePolicy(void);
bool Dkc3VideoEdgePolicyFromName(const char *name,
                                 Dkc3VideoEdgePolicy *policy);
const char *Dkc3VideoEdgePolicyName(Dkc3VideoEdgePolicy policy);

/*
 * Host presentation geometry near DKC3's fixed level endpoints under the
 * active edge policy. Every known layout authors terrain from world
 * X=`$0100` through maximum_scroll_x+256. With `shift`, the presented
 * viewport is moved inward (bias) while the room can absorb the margin and
 * centered with clamped margins when it cannot. `glide` is shift's
 * geometry released with travel, except that a room shorter than the two
 * releases takes a smaller unpinned shift with clamped margins (none, as
 * bars, below two margins). With `bars`, the bias is
 * zero and each visible margin is clamped to the authored extent. With
 * `reflect`, the bias is zero and both margins stay fully visible; the
 * terrain decoder mirrors authored columns into the unauthored strip. The
 * cartridge camera, collision, exits, streaming, and WRAM stay stock; a
 * nonzero bias shifts BG scroll and OBJ placement together. An unknown
 * bound (maximum below the origin) keeps the full symmetric margin.
 */
/* The presentation margins with the level's left camera bound supplied:
 * the glide treats `minimum_scroll_x` as the west wall. A level whose
 * authored world begins where the player is held (Screech's Sprint starts
 * on a plank platform at world 608 with nothing west of it) has no
 * minimum-scroll word, so the host derives the bound from the level map
 * (Dkc3VideoHoldWest). Dkc3VideoPresentationMargins is the same with the
 * map's first page, $0100, as the bound. */
void Dkc3VideoPresentationMarginsBounded(uint16_t camera_x,
                                         uint16_t minimum_scroll_x,
                                         uint16_t maximum_scroll_x,
                                         int *bias,
                                         int *left_margin,
                                         int *right_margin);

/* One frame's step of the presented bias toward the glide's target. The bias
 * moves one pixel a frame, so a bound that appears or vanishes never snaps
 * the picture. It may also move by as much as the camera moved this frame
 * (camera_delta, new camera X minus the previous one) when that step works
 * against the camera's motion. That lets the pins hold the view still
 * while the camera scrolls into a wall: in a room narrower than two
 * margins of scroll, a 1-pixel step lets the view run up to a margin past
 * the wall (a clamped black strip) and drift back after every turn while
 * the camera's look-ahead moves 2-4 pixels a frame. A step never makes the
 * view move faster than the camera plus one pixel. Ported from
 * DKC2Recomp's Dkc2VideoStepPresentationBias. */
int Dkc3VideoStepPresentationBias(int presented_bias, int wanted_bias,
                                  int camera_delta);

/* The margins the bounds allow for a bias the host has already chosen
 * (Dkc3VideoStepPresentationBias moves it toward the glide's target, so a
 * bound that appears or vanishes never snaps the picture). */
void Dkc3VideoMarginsForBias(uint16_t camera_x, uint16_t minimum_scroll_x,
                             uint16_t maximum_scroll_x, int bias,
                             int *left_margin, int *right_margin);

/* Scroll bounds from a camera region's span ([region_left, region_right),
 * the right edge one screen past the last camera x) capped by the level's
 * maximum. A camera still outside the span (converging on a region it has
 * just entered) gets the wall reflected about it, so the margin on that
 * side equals the distance left to travel and closes as the camera arrives
 * instead of vanishing on the frame `$0478` changes. */
void Dkc3VideoRegionScrollBounds(uint16_t camera_x, uint16_t region_left,
                                 uint16_t region_right,
                                 uint16_t level_maximum_x,
                                 uint16_t *minimum_scroll_x,
                                 uint16_t *maximum_scroll_x);

/* Pixels per frame a margin may open beyond the view's own motion when its
 * wall moves outward (a region change opening onto a wider room). */
enum { kDkc3VideoMarginOpenRate = 2 };

/* The margin to present this frame: at most the last presented margin plus
 * kDkc3VideoMarginOpenRate plus how far the view moved away from that side
 * (positive view_step_outward), so a wall fixed in the world keeps pace
 * and a wall that jumps outward wipes the margin open. Narrowing is never
 * limited. */
int Dkc3VideoLimitMarginGrowth(int presented_margin, int target_margin,
                               int view_step_outward);


/* `$B3:C0F4` (Ripsaw Rage's tree-trunk BG3 masks) writes W1 = [`$1A`,
 * `$1C`] and W2 = [`$1E`, `$20`] (world X in its direct page) minus the
 * camera, each edge clamped to the 256-pixel screen, so a trunk inside a
 * wide margin reaches the PPU as [255, 255] or [0, 0]. Given those four
 * world edges, the camera the routine read, and the four registers it
 * wrote (W1L, W1R, W2L, W2R), stores each window's unclamped screen edges
 * in `edges` (left, right per window) and returns a mask of the windows
 * whose registers equal the clamp of those edges (bit 0 W1, bit 1 W2). A
 * window that fails the check was not written by that clamp; leave it to
 * the hardware registers. */
unsigned Dkc3VideoUnclampedTrunkWindows(const uint16_t world[4],
                                        uint16_t camera,
                                        const uint8_t regs[4],
                                        int16_t edges[4]);

/* False when DKC3_CULL_WIDEN=0 asks for native object and window behavior
 * beside the presented margins (the before/after switch). */
bool Dkc3VideoCullWidenEnabled(void);

/* Black the host columns outside the live span of a wide frame: the first
 * `extra - left_margin` and the last `extra - right_margin` of each row of
 * `width` pixels (kDkc3VideoBytesPerPixel each). The PPU writes only the
 * clamped span on ordinary lines, so without this a margin that narrows at
 * a wall keeps the pixels it last showed, which shear as the camera moves
 * vertically. Lines that composite the full budget overwrite the strips. */
void Dkc3VideoClearOutsideMargins(uint8_t *buffer, size_t pitch, int width,
                                  int height, int extra, int left_margin,
                                  int right_margin);

void Dkc3VideoPresentationMargins(uint16_t camera_x,
                                  uint16_t maximum_scroll_x,
                                  int *bias,
                                  int *left_margin,
                                  int *right_margin);

/*
 * Resolve a world tile column to a decompressed level-map source column.
 * Inside the authored extent the source is the world column minus the
 * `$0100` origin (returns 0). Outside it, the `reflect` policy mirrors the
 * nearest authored columns across the boundary and requests the horizontal
 * flip (returns 1); any other policy, or a mirror that would leave the
 * authored range, yields no source (returns -1, verified transparent).
 */
int Dkc3VideoResolveEdgeTile(uint32_t world_tile_x,
                             uint16_t maximum_scroll_x,
                             uint32_t *source_tile_x,
                             bool *mirror_horizontally);

/* The world span [left_edge, right_edge) the cartridge camera may show from
 * its current camera region (`region_offset`, the region index * 2 kept at
 * $0478) in the 64 KiB WRAM bank `wram`: the region's own span, continued
 * through the left and right neighbours it links (the right link, or the
 * region after it when that one names this region as a left neighbour, as
 * $B7:E0ED tests) until a region has no further link or the neighbour is
 * closed (flag $20). The clamp at $B7:DBCC..DC37 also admits a neighbour
 * only while it overlaps the target's rows by $38 or, flagged $80, the
 * player is near it; those gates change with every step of vertical
 * travel, and a bound that comes and goes beside the camera would slide
 * the glide back and forth, so the span ignores them and moves only when
 * the cartridge changes region. False for no current region (entry 0, the
 * whole level) or a region narrower than one screen. */
bool Dkc3VideoCameraRegionSpan(const uint8_t *wram, uint16_t region_offset,
                               uint16_t *left_edge, uint16_t *right_edge);

/* True when a host margin would extend past the authored extent on either
 * side under the active edge policy without being clamped, which is where a
 * physical 64-column BG3 could expose unauthored ring columns. */
bool Dkc3VideoMarginLeavesAuthoredExtent(uint16_t camera_x,
                                         uint16_t maximum_scroll_x);

/* True when any part of an 8x8 world tile lies outside the authentic
 * 256-pixel viewport and can therefore be sampled by a widened margin. */
bool Dkc3VideoTileTouchesWidescreenMargin(uint32_t world_tile_x,
                                          uint32_t camera_x);

/* True when an 8x8 world tile lies wholly inside the span the cartridge's
 * own 256-pixel window can reach with the camera anywhere in
 * [minimum_scroll_x, maximum_scroll_x] (the current camera region's walls,
 * or a west hold). The decoded level map there is what the console shows
 * once the camera arrives, so a margin cell in that span must not be
 * replaced by a continued or mirrored wall. */
bool Dkc3VideoTileInCameraReach(uint32_t world_tile_x,
                                uint16_t minimum_scroll_x,
                                uint16_t maximum_scroll_x);

/*
 * DKC3 stores left margins and total horizontal spans separately. Keeping
 * these calculations here makes the generated-code adaptations switch back
 * to the exact cartridge values whenever widescreen is disabled.
 */
/* The presentation bias the host rendered last (positive: the presented
 * window sits right of the cartridge camera). The camera-relative cull
 * helpers below widen asymmetrically by it. */
void Dkc3VideoSetPresentationBias(int bias);
int Dkc3VideoPresentationBias(void);

uint16_t Dkc3VideoExpandCullLeft(uint16_t native_margin);
uint16_t Dkc3VideoExpandCullSpan(uint16_t native_span);
uint16_t Dkc3VideoExpandCullRight(uint16_t native_right);
/* Configure or query the extra widescreen spawn-ahead lead (0 when native,
 * terrain is unproven, or DKC3_CULL_WIDEN=0). The sprite renderer lead is
 * further clamped so off-screen OAM coordinates never wrap in nine-bit X. */
void Dkc3VideoSetSpawnLead(int lead);
int Dkc3VideoSpawnLead(void);
int Dkc3VideoSpriteCullLead(void);
uint16_t Dkc3VideoExpandSpawnLeft(uint16_t native_margin);
uint16_t Dkc3VideoExpandSpawnSpan(uint16_t native_span);
uint16_t Dkc3VideoExpandSpriteCullLeft(uint16_t native_margin);
uint16_t Dkc3VideoExpandSpriteCullSpan(uint16_t native_span);
/* Return the current placement-index cell plus every horizontal neighbor cell
 * the widened placement interval (including wall bias, spawn lead, and
 * DATA_BBA8BE's 32-pixel asymmetric row slack) can reach. The final
 * activation compare rejects broad-phase candidates outside the live window. */
size_t Dkc3VideoPlacementScanCells(
    uint16_t native_cell_offset,
    uint16_t row_stride,
    uint16_t cell_offsets[kDkc3VideoPlacementScanMaxCells]);
/* Match the exact PPU register signature of Floodlit Fish's underwater
 * color-math composition. Keeping this as a pure, tested host policy avoids
 * changing unrelated full-screen effects that also use BG3. */
bool Dkc3VideoUsesUnderwaterSubscreenTint(
    uint8_t bg_mode, uint8_t main_layers, uint8_t sub_layers,
    uint8_t main_windowed, uint8_t sub_windowed, uint32_t window_select,
    uint8_t window1_left, uint8_t window1_right,
    uint8_t color_math_control, uint8_t color_math_layers,
    uint16_t fixed_color);
/* Fill only transparent subscreen pixels in the side margins from the
 * corresponding native-screen pixel. When that wrapped sample is also empty,
 * continue the nearest valid inner margin pixel outward before falling back
 * to a native neighbor. This preserves valid adjacent-world pixels without a
 * color boundary while giving full-screen half-color effects a backdrop to
 * blend against when their authored subscreen ends at 256 pixels. */
size_t Dkc3VideoRepeatTransparentSubscreenMargins(
    uint16_t *pixels, size_t pixel_count, size_t native_offset,
    unsigned left_margin, unsigned right_margin);
uint16_t Dkc3VideoPromoteOamXHigh(uint16_t screen_x);

/*
 * True when a background's 64-column tilemap allocation is not physically
 * its own: its second 32-column page overlaps another enabled background's
 * tilemap. DKC3's Mudhole Marsh BG3 advertises 64 columns at $6C00 while
 * BG1's terrain map occupies $7000, so its "adjacent columns" are terrain
 * rows. Such a layer is bounded content and repeats its rendered line.
 */
bool Dkc3VideoTilemapPagesCollide(const uint8_t bg_xsc[4],
                                  unsigned layer,
                                  uint8_t enabled_layers);

/*
 * Return the subset of currently enabled BG1/BG2 terrain candidates whose
 * tilemaps have 64 physically distinct columns. BG3 is handled by the
 * separate physical-width capability after the exact terrain source has
 * passed its readiness gate.
 */
uint8_t Dkc3VideoPpuWideLayerMask(uint8_t bg_mode,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers);

/* Include layers enabled later by the frame's HDMA tables before assigning
 * terrain/object-plane policies. The native register enables are untouched;
 * DKC3_CULL_WIDEN=0 retains the frame-start-only selection for comparison. */
uint8_t Dkc3VideoPpuFrameWideLayerMask(uint8_t bg_mode,
                                       const uint8_t bg_xsc[4],
                                       uint8_t main_layers,
                                       uint8_t sub_layers,
                                       const Dkc3HdmaBands *bands);

/*
 * Return every enabled Mode-1 background that owns a physical 64-column
 * tilemap. This is a presentation capability, not terrain ownership: BG1/BG2
 * still need the live stream destination and exact source prefill before a
 * scene may widen, while an enabled 64-column BG3 may join the final render
 * mask only after that terrain-ready gate succeeds.
 */
uint8_t Dkc3VideoPhysicalWideLayerMask(uint8_t bg_mode,
                                       const uint8_t bg_xsc[4],
                                       uint8_t main_layers,
                                       uint8_t sub_layers);

/* Bounded backgrounds normally repeat their already-rendered native line in
 * the margins. A screen-space hardware window must instead be evaluated over
 * the physical widened span: repeating the composited line repeats a clipped
 * window at the opposite edge. Select enabled, windowed 32-column layers for
 * that physical render path. DKC3_CULL_WIDEN=0 disables this together with
 * the cartridge's camera-relative object windows for an exact A/B run. */
uint8_t Dkc3VideoPhysicalWindowedLayerMask(
    uint8_t bg_mode, const uint8_t bg_xsc[4], uint8_t main_layers,
    uint8_t sub_layers, uint8_t main_windowed, uint8_t sub_windowed);

/*
 * Select the layers whose authentic rendered scanline is repeated into the
 * margins: every enabled Mode-1 background that is not in the physical render
 * mask. A non-windowed 32-column tilemap wraps at 256 pixels on hardware, so a
 * period-256 repeat of the rendered line is exactly what a wider PPU would
 * draw from that map; HDMA phase and color math are already in the rendered
 * line. Windowed 32-column layers join the physical mask so their window can
 * be evaluated across the presented span. Rolling 64-column layers never
 * repeat through this mask; they are world-keyed or band-classified by the
 * adapter.
 */
/*
 * VRAM word pages (1024 words each) that a background tilemap occupies for
 * one BGnSC value: one for 32x32, two for 64x32 or 32x64, four for 64x64,
 * consecutive from the map base. Returns the page count.
 */
unsigned Dkc3VideoTilemapPages(uint8_t bg_sc, uint8_t pages[4]);

/*
 * Whether a 64-column tilemap's content is authored to continue across its
 * own 512-pixel hardware wrap, so a host margin may read the map beyond the
 * native view. A map fails when any row with at least eight entries has a
 * shortest horizontal period that does not divide 64 columns (the ship
 * hold's 96-pixel cabin wall, which the cartridge re-bases to keep its
 * seam off screen) or when a strip of kDkc3VideoPlaneEdgeStrip or more
 * columns at either map edge is blank in every populated row (a backdrop
 * narrower than its allocation). An empty or 32-column map is not a plane.
 */
enum { kDkc3VideoPlaneEdgeStrip = 4 };
/* Rows of a 64-column tilemap that a static plane must not show in a
 * margin: rows populated in at least kDkc3VideoPlaneDenseCells of their
 * 64 cells (a painted strip, not scattered decoration) whose painting
 * stops kDkc3VideoPlaneEdgeStrip or more cells short of either wrap edge.
 * Toxic Tower's BG2 keeps its castle wall across all 64 columns but its
 * cornice strip across 55, and a scanline band whose scroll wraps a
 * margin into the blank nine showed the backdrop through the wall. Bit r
 * of the result is row r (32 or 64 rows by the map size). */
enum { kDkc3VideoPlaneDenseCells = 48 };
uint64_t Dkc3VideoTilemapBrokenRows(const uint16_t *vram, size_t word_count,
                                    uint8_t bg_sc, uint16_t character_base);

bool Dkc3VideoTilemapWrapsAuthored(const uint16_t *vram, size_t word_count,
                                   uint8_t bg_sc, uint16_t character_base);

/*
 * Whether a 64-column tilemap is an object plane: one 32-column half holds
 * the content and the other half is entirely blank. Haunted Hall draws
 * Kackle as a 32-column block into BG2's left page and positions him with
 * the layer's scroll; the map's hardware wrap beyond the block is blank by
 * design, so a margin may read the map raw (his off-screen part appears,
 * nothing else), while repeating the native line cut him at the edge and
 * copied him into the far margin. Such a map is rewritten as the object
 * animates, so it needs no static gate. Requires at least
 * kDkc3VideoObjectPlaneMinCells entries in the populated half.
 */
enum { kDkc3VideoObjectPlaneMinCells = 64 };
bool Dkc3VideoTilemapIsObjectPlane(const uint16_t *vram, size_t word_count,
                                   uint8_t bg_sc, uint16_t character_base);

uint8_t Dkc3VideoRepeatLayerMask(uint8_t bg_mode,
                                 uint8_t main_layers,
                                 uint8_t sub_layers,
                                 uint8_t wide_layer_mask);

/* The waterfall streamer at $B3:8609 reads a per-level sequence of 32px
 * column groups and 32-word column templates from the verified ROM's B3
 * bank. These helpers contain addresses and decoding rules, never art. */
bool Dkc3VideoUsesWaterfallColumns(uint16_t level_flags, uint16_t renderer,
                                    uint8_t bg_mode, uint8_t bg2_sc);
bool Dkc3VideoDecodeWaterfallColumn(const uint8_t *bank, size_t bank_size,
                                     uint16_t layout_pointer,
                                     uint32_t world_tile_x,
                                     uint16_t entries[32]);
bool Dkc3VideoVerifyWaterfallViewport(const uint8_t *bank, size_t bank_size,
                                       uint16_t layout_pointer,
                                       const uint16_t *vram, size_t words,
                                       uint16_t camera_x,
                                       unsigned *matching, unsigned *total);

/* Shortest distance between two 10-bit SNES scroll phases. */
uint16_t Dkc3VideoScrollPhaseDistance(uint16_t a, uint16_t b);

/*
 * True when a layer's scroll for one scanline band is the terrain phase:
 * within the measured lead tolerance of the scroll that the live terrain
 * owner rendered at the frame anchor. A 64-column layer at the terrain phase
 * displays the streamed world map and is served from the world-keyed store;
 * any other phase is a bounded effect plane and repeats its rendered line.
 * This is structural: it has no level, mode, or screen-composition
 * signature, and either physical layer may take either role in any band.
 */
/*
 * The terrain layer's rendered scroll phase for the frame. Normally the
 * frame-start BGnHOFS/BGnVOFS pair, but a stage's HDMA can leave another
 * value in the register at frame start and set the camera phase on every
 * rendered line (Slime Climb leaves BG1VOFS at $50 while its HDMA writes the
 * camera row for lines 1-224). The frame-start pair stands when it already
 * lies within the terrain lead of the camera phase; otherwise the band pair
 * within that lead covering the most lines replaces it. Returns true when a
 * band pair was chosen.
 */
bool Dkc3VideoSelectTerrainPhase(const Dkc3HdmaBands *bands,
                                 int layer,
                                 uint16_t frame_h,
                                 uint16_t frame_v,
                                 uint32_t camera_x,
                                 uint32_t camera_y,
                                 uint16_t *phase_h,
                                 uint16_t *phase_v);

bool Dkc3VideoScrollAtTerrainPhase(uint16_t h_scroll,
                                   uint16_t v_scroll,
                                   uint16_t terrain_h_scroll,
                                   uint16_t terrain_v_scroll);

bool Dkc3VideoPpuCanExtend(uint8_t bg_mode,
                           const uint8_t bg_xsc[4],
                           uint8_t main_layers,
                           uint8_t sub_layers);

/*
 * Identify which enabled wide layer owns DKC3's decompressed level stream.
 * $17B6 is a VRAM word address; BGxSC encodes the matching tilemap base in
 * 0x400-word units. Returns BG1/BG2 as 0/1, or -1 when the live destination
 * is not one of the audited wide layers.
 */
int Dkc3VideoTerrainLayer(uint8_t wide_layer_mask,
                          const uint8_t bg_xsc[4],
                          uint16_t stream_vram_word_address);

/* Classify a live game-loop/level pair into a proven map layout. */
Dkc3VideoLevelLayout Dkc3VideoLevelLayoutForScene(
    uint16_t map_shape, uint16_t level_number);

/* Expand a repeating 10-bit SNES scroll phase nearest a world-space anchor. */
uint32_t Dkc3VideoUnwrapPpuScroll(uint16_t ppu_scroll, uint32_t anchor);

/* Resolve terrain X from the scroll value latched by the PPU. The WRAM
 * camera can lead that value by a few pixels at an NMI boundary. */
uint32_t Dkc3VideoTerrainShadowX(uint16_t ppu_scroll_x, uint32_t camera_x);

/*
 * Select the world-Y domain used by DKC3's rolling terrain shadow. The
 * cartridge stages terrain one 256-pixel page above its camera coordinates,
 * so live tilemap captures and exact prefills must share the rendered PPU
 * phase rather than the raw WRAM camera Y.
 */
uint32_t Dkc3VideoTerrainShadowY(uint16_t ppu_scroll_y, uint32_t camera_y);

/*
 * Resolve a rendered terrain tile row to DKC3's decompressed level-map row.
 * The PPU scroll is the rendered source phase and may trail the WRAM camera
 * by one pixel at an NMI boundary. Unwrap the top tile once and advance later
 * rows continuously; independently unwrapping every row can select opposite
 * 1024-pixel epochs near the half-period boundary.
 */
uint32_t Dkc3VideoLevelSourceTileY(uint16_t ppu_scroll_y,
                                   uint32_t camera_y,
                                   uint32_t viewport_tile_row);

/*
 * DKC3's rolling column builders start from the 256-pixel source page above
 * the camera in horizontal, vertical, and narrow-vertical layouts. BG scroll
 * identifies that page by its low eight-bit phase, so select the matching
 * phase nearest cameraY-$0100 before advancing through the viewport. This
 * includes row zero: at scroll phase $00ff/camera $0100 its source is the
 * preceding row ($ffff), not row $00ff of the next physical page.
 */
uint32_t Dkc3VideoLevelMapTileY(uint16_t ppu_scroll_y,
                                uint32_t camera_y,
                                uint32_t viewport_tile_row);

/*
 * Decode one exact 8x8 BG tile from DKC3's decompressed level representation.
 * Horizontal, vertical, and square handlers store 32x32 metatiles in
 * different map orders; layout selects the proven address formula. A second
 * table contains each metatile's sixteen 8x8 tilemap entries. Both buffers
 * share one 64 KiB CPU bank during gameplay. No ROM-derived bytes are retained
 * here.
 */
/*
 * Bytes per metatile row of a row-major level map for a layout: 64 for the
 * vertical shafts (32 metatiles), 192 for the square scroller's audited
 * stage (96 metatiles: the cartridge's column builder $B5:B555 multiplies
 * the row by six), 32 for Parrot Chute Panic, 160 for the ship holds (80
 * metatiles), and 0 for the column-major horizontal layouts. A stage can
 * run a different builder than its sub-mode suggests (Bramble $002D uses
 * the 160-byte rows), so the host verifies the stride against the native
 * ring and calibrates it when the default fails.
 */
unsigned Dkc3VideoLevelLayoutRowBytes(Dkc3VideoLevelLayout layout);

/* Metatile rows per column: 16 for shapes 0/8, 32 for shape 1, and 0 for
 * layouts that are not column-major. Shape 1's column builder doubles the
 * X stride and masks Y to $03E0 ($B7:BE5D-$B7:BE76). */
unsigned Dkc3VideoLevelLayoutColumnRows(Dkc3VideoLevelLayout layout);

/*
 * Decode one tile of a row-major level map with an explicit row stride:
 * metatile (world_x / 32, world_y / 32) at map offset column * 2 + row *
 * row_bytes. The horizontal layout is column-major and uses
 * Dkc3VideoDecodeLevelTile.
 */
/* The id (flip bits stripped) of the metatile at a level-map cell:
 * column-major 16/32-row pages for the horizontal layouts, `row_bytes` strided
 * rows otherwise (0 takes the layout's stride). */
bool Dkc3VideoReadLevelMetatile(const uint8_t *bank_data, size_t bank_size,
                                uint16_t level_map_base,
                                Dkc3VideoLevelLayout layout,
                                unsigned row_bytes,
                                uint32_t metatile_x, uint32_t metatile_y,
                                uint16_t *metatile);

/* The metatile ids the level map in [level_map_base, map_end) most often
 * places beside `metatile` on one side (empty cells, id 0, excluded), most
 * frequent first. Returns how many ids were written, at most `capacity`. */
unsigned Dkc3VideoMetatileNeighbours(const uint8_t *bank_data,
                                     size_t bank_size,
                                     uint16_t level_map_base,
                                     uint16_t map_end,
                                     Dkc3VideoLevelLayout layout,
                                     unsigned row_bytes, uint16_t metatile,
                                     bool east_side, uint16_t *ids,
                                     uint16_t *counts, unsigned capacity);

/* Decode one 8x8 entry of a metatile definition by id (flip bits honoured
 * as the cartridge does), without going through the level map. */
bool Dkc3VideoDecodeMetatileEntry(const uint8_t *bank_data, size_t bank_size,
                                  uint16_t metatile_base,
                                  uint16_t metatile_word, unsigned sub_x,
                                  unsigned sub_y, uint16_t *tile_entry);

bool Dkc3VideoDecodeLevelTileRowMajor(const uint8_t *bank_data,
                                      size_t bank_size,
                                      uint16_t level_map_base,
                                      uint16_t metatile_base,
                                      unsigned row_bytes,
                                      uint32_t world_tile_x,
                                      uint32_t world_tile_y,
                                      uint16_t *tile_entry);

bool Dkc3VideoDecodeLevelTile(const uint8_t *bank_data,
                              size_t bank_size,
                              uint16_t level_map_base,
                              uint16_t metatile_base,
                              Dkc3VideoLevelLayout layout,
                              uint32_t world_tile_x,
                              uint32_t world_tile_y,
                              uint16_t *tile_entry);

/*
 * Locate a fully transparent 4bpp character in live SNES VRAM. The returned
 * tilemap entry has palette, priority, and flip bits clear.
 */
bool Dkc3VideoFindTransparent4bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry);

/* A 4bpp character whose sixteen VRAM words are all zero draws nothing. */
bool Dkc3VideoCharacterIsTransparent(const uint16_t *vram,
                                     size_t word_count,
                                     uint16_t character_base,
                                     uint16_t tile_entry);

/*
 * Locate a fully transparent 2bpp character (mode 1 BG3) in live SNES VRAM.
 * The returned tilemap entry has palette, priority, and flip bits clear.
 */
bool Dkc3VideoFindTransparent2bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry);

typedef enum Dkc3VideoMetatileFill {
  kDkc3VideoMetatileUnknown = 0,
  kDkc3VideoMetatileEmpty,
  kDkc3VideoMetatilePartial,
  kDkc3VideoMetatileFull,
} Dkc3VideoMetatileFill;

typedef Dkc3VideoMetatileFill (*Dkc3VideoMetatileClassifier)(
    void *context, uint32_t metatile_x, uint32_t metatile_y);

/* A west hold in the level map: the columns beside the cartridge window's
 * first column, up to `reach` of them, are empty for every row in
 * [first_row, last_row]. The player is held at the authored world's edge
 * and the margin would show nothing there. `hold_column` receives the
 * window's first column, whose west edge is the bound. */
bool Dkc3VideoHoldWest(Dkc3VideoMetatileClassifier classify, void *context,
                       uint32_t window_column, unsigned reach,
                       uint32_t first_row, uint32_t last_row,
                       uint32_t *hold_column);

/*
 * Source tile column that mirrors a margin tile across a player-held wall.
 * The wall line is the boundary just outside the cartridge's edge tile
 * (edge_source_tile: the first native source tile on the west, the last on
 * the east), so the tile beside the edge mirrors the edge tile, the next
 * one the tile behind it, and so on; the caller flips the entry
 * horizontally. Returns false when the tile is not beyond that edge.
 */
bool Dkc3VideoMirrorSourceTileAcrossEdge(uint32_t source_tile_x,
                                         uint32_t edge_source_tile,
                                         bool east_side,
                                         uint32_t *mirrored_tile_x);

bool Dkc3VideoFindStructuralWallSource(Dkc3VideoMetatileClassifier classify,
                                       void *context,
                                       bool east_side,
                                       uint32_t target_metatile_x,
                                       uint32_t edge_metatile_x,
                                       uint32_t metatile_y,
                                       uint32_t *source_metatile_x);

/* A tilemap entry carries palette, priority, and flip bits in addition to its
 * 10-bit character index. This recognizes a transparent character without
 * discarding those presentation bits. */
bool Dkc3VideoIsTransparentTileEntry(uint16_t tile_entry,
                                     uint16_t transparent_tile);

/* The sprite palette that `sprite_type`'s init scripts request, read from
 * one bank of the $BB:8FF9 init-script language. A script opens with
 * `$FF type`. Commands below `$EF` write a field. `$FA`, `$FB`, `$FC`, and
 * `$FE` are three bytes each; only `$FC` (through $BB:95C8) sets a palette.
 * `$EF` ends the script, and so does `$F0` (which also sets priority 2 but
 * keeps the palette bits). $BB:8FF1 runs scripts with DB = $FF. Every such
 * script for the type in the bank must request the same palette exactly
 * once. A script that names no palette, uses any other
 * command, has no terminator, or disagrees with another makes the answer 0
 * (unknown). */
uint16_t Dkc3VideoInitScriptPaletteForType(const uint8_t *bank, size_t size,
                                           uint16_t sprite_type);

#ifdef __cplusplus
}
#endif

#endif
