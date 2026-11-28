/* Synthetic pixels only: verify fast PPU window combine logic (WBGLOG/WOBJLOG
 * OR/AND/XOR/XNOR) against the independent scalar legacy PPU across BG3, OBJ,
 * and Color Window, and verify widescreen presentation-X-bias window alignment.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "snes/ppu.h"
#include "snes/snes.h"

Snes *g_snes;
bool WsShadowLayerActive(int layer) { (void)layer; return false; }
int WsShadowNativeLeft(int layer) { (void)layer; return 0; }
int WsShadowNativeRight(int layer) { (void)layer; return 256; }
uint32_t WsShadowWorldX(int layer) { (void)layer; return 0; }
uint32_t WsShadowScrollY(int layer) { (void)layer; return 0; }
int32_t WsShadowPresentWorldX(int layer, int x, uint16_t scroll) {
  (void)layer; return x + scroll;
}
uint32_t WsShadowPresentWorldY(int layer, int x) {
  (void)layer; (void)x; return 0;
}
void WsShadowOnVramWrite(uint16_t address, uint16_t value) {
  (void)address; (void)value;
}
uint16_t WsShadowTile(int layer, int x, uint32_t y, uint16_t scroll,
                      uint16_t map, uint16_t tile) {
  (void)layer; (void)x; (void)y; (void)scroll; (void)map;
  return tile;
}

typedef struct {
  uint8_t w1l, w1r, w2l, w2r;
} WindowPair;

static void init_bg3_scene(Ppu *ppu, uint32_t *pixels, int width, bool fast) {
  ppu_reset(ppu);
  memset(pixels, 0, (size_t)width * sizeof(*pixels));
  PpuBeginDrawing(ppu, (uint8_t *)pixels, (size_t)width * 4u,
                  fast ? kPpuRenderFlags_NewRenderer : 0);
  ppu->inidisp = 15;
  ppu->bgmode = 0x09; /* Mode 1, BG3 high priority */
  ppu->bgXsc[0] = 0x40;
  ppu->bgXsc[2] = 0x70;
  ppu->bgTileAdr = 0x0011; /* BG1/BG2 at 0x1000, BG3 at 0x0000 */
  ppu->screenEnabled[0] = 0x05; /* BG1 + BG3 */
  ppu->screenWindowed[0] = 0x04; /* Window BG3 */
  for (int i = 0; i < 1024; i++) {
    ppu->vram[0x4000 + i] = 1; /* BG1 solid tile */
    ppu->vram[0x7000 + i] = 0x2001; /* BG3 high-prio tile */
  }
  for (int row = 0; row < 8; row++) {
    ppu->vram[0x1000 + 16 + row] = 0x00ff; /* BG1 4bpp pixel 1 */
    ppu->vram[0x0000 + 8 + row] = 0x00ff;  /* BG3 2bpp pixel 1 */
  }
  ppu->cgram[0] = 0x0000;
  ppu->cgram[1] = 0x03e0; /* Green (BG1/BG3 shared index 1? Use palette 1 for BG3!) */
  /* Give BG3 palette 1 (CGRAM index 5) so BG3 vs BG1 pixels are distinct. */
  for (int i = 0; i < 1024; i++) {
    ppu->vram[0x7000 + i] = 0x2401; /* BG3 high-prio, palette 1, tile 1 */
  }
  ppu->cgram[5] = 0x7c1f; /* Magenta (BG3) */
  for (int i = 0; i < 128; i++) ppu->oam[2 * i] = 0xf000;
}

static int test_bg3_window_oracle(Ppu *ppu) {
  static const uint8_t kSel[] = {0, 2, 3}; /* off, on, on+inv */
  static const WindowPair kPairs[] = {
      {33, 128, 80, 190},  /* overlapping */
      {20, 60, 140, 200},  /* disjoint */
      {33, 128, 255, 0},   /* Poisonous Pipeline shaft (empty W2) */
      {255, 0, 255, 0},    /* Poisonous Pipeline deep underwater (both empty) */
      {0, 255, 255, 0},    /* full-screen W1, empty W2 */
      {176, 255, 0, 40},   /* edge-pinned windows */
      {4, 211, 255, 255},  /* Barrel Shield Bust-Up: parked right W2 */
      {0, 0, 105, 255},    /* Barrel Shield Bust-Up: parked left W1 */
  };
  unsigned cases = 0;
  for (size_t p = 0; p < sizeof(kPairs) / sizeof(kPairs[0]); p++) {
    for (size_t s1 = 0; s1 < 3; s1++) {
      for (size_t s2 = 0; s2 < 3; s2++) {
        for (uint8_t logic = 0; logic < 4; logic++) {
          uint32_t fast[256], scalar[256];
          for (int mode = 0; mode < 2; mode++) {
            uint32_t *out = mode ? fast : scalar;
            init_bg3_scene(ppu, out, 256, mode != 0);
            ppu_write(ppu, 0x24, (uint8_t)(kSel[s1] | (kSel[s2] << 2))); /* W34SEL */
            ppu_write(ppu, 0x26, kPairs[p].w1l);
            ppu_write(ppu, 0x27, kPairs[p].w1r);
            ppu_write(ppu, 0x28, kPairs[p].w2l);
            ppu_write(ppu, 0x29, kPairs[p].w2r);
            ppu_write(ppu, 0x2a, (uint8_t)(logic << 4)); /* WBGLOG BG3 */
            ppu_runLine(ppu, 0);
            ppu_runLine(ppu, 1);
          }
          if (memcmp(fast, scalar, sizeof(fast)) != 0) {
            for (int x = 0; x < 256; x++) {
              if (fast[x] != scalar[x]) {
                fprintf(stderr,
                        "FAIL BG3 oracle pair=%zu s1=%u s2=%u logic=%u x=%d "
                        "fast=%08x scalar=%08x\n",
                        p, kSel[s1], kSel[s2], logic, x, fast[x], scalar[x]);
                return 1;
              }
            }
          }
          cases++;
        }
      }
    }
  }

  /* Explicit sanity check for Poisonous Pipeline's XNOR water-surface contract. */
  uint32_t shaft[256], deep[256];
  init_bg3_scene(ppu, shaft, 256, true);
  ppu_write(ppu, 0x24, 0x0a); /* BG3 W1+W2 enabled, non-inverted */
  ppu_write(ppu, 0x26, 33);
  ppu_write(ppu, 0x27, 128);
  ppu_write(ppu, 0x28, 255);
  ppu_write(ppu, 0x29, 0);
  ppu_write(ppu, 0x2a, 0x30); /* WBGLOG BG3 = XNOR */
  ppu_runLine(ppu, 0);
  ppu_runLine(ppu, 1);

  init_bg3_scene(ppu, deep, 256, true);
  ppu_write(ppu, 0x24, 0x0a);
  ppu_write(ppu, 0x26, 255);
  ppu_write(ppu, 0x27, 0);
  ppu_write(ppu, 0x28, 255);
  ppu_write(ppu, 0x29, 0);
  ppu_write(ppu, 0x2a, 0x30);
  ppu_runLine(ppu, 0);
  ppu_runLine(ppu, 1);

  const uint32_t bg1_color = shaft[0];
  const uint32_t bg3_color = shaft[50];
  if (bg1_color == bg3_color) {
    fprintf(stderr, "FAIL BG3 test setup: BG1 and BG3 colors identical (%08x)\n",
            bg1_color);
    return 1;
  }
  for (int x = 0; x < 256; x++) {
    uint32_t expect_shaft = (x >= 33 && x <= 128) ? bg3_color : bg1_color;
    if (shaft[x] != expect_shaft) {
      fprintf(stderr, "FAIL Poisonous Pipeline shaft x=%d got=%08x expect=%08x\n",
              x, shaft[x], expect_shaft);
      return 1;
    }
    if (deep[x] != bg1_color) {
      fprintf(stderr, "FAIL Poisonous Pipeline deep x=%d got=%08x expect=%08x\n",
              x, deep[x], bg1_color);
      return 1;
    }
  }
  printf("BG3 window oracle: %u cases passed\n", cases);
  return 0;
}

static void init_obj_scene(Ppu *ppu, uint32_t pixels[256], bool fast) {
  ppu_reset(ppu);
  memset(pixels, 0, 256 * sizeof(*pixels));
  PpuBeginDrawing(ppu, (uint8_t *)pixels, 256 * 4u,
                  fast ? kPpuRenderFlags_NewRenderer : 0);
  ppu->inidisp = 15;
  ppu->bgmode = 0x01;
  ppu->screenEnabled[0] = 0x10;  /* OBJ only */
  ppu->screenWindowed[0] = 0x10; /* Window OBJ */
  for (int row = 0; row < 8; row++)
    ppu->vram[row] = 0x00ff; /* Tile 0 pixel 1 */
  ppu->cgram[0] = 0x001f;   /* Backdrop red */
  ppu->cgram[129] = 0x7c00; /* OBJ blue */
  for (int i = 0; i < 128; i++) ppu->oam[2 * i] = 0xf000;
  /* 32 8x8 sprites covering x=0..255 on scanline 1 (y=0). */
  for (int i = 0; i < 32; i++) {
    ppu->oam[2 * i] = (uint16_t)(i * 8);
    ppu->oam[2 * i + 1] = 0x3000;
  }
}

static int test_obj_and_color_window_oracle(Ppu *ppu) {
  static const WindowPair kPairs[] = {
      {33, 128, 80, 190},
      {33, 128, 255, 0},
      {255, 0, 255, 0},
  };
  unsigned cases = 0;
  /* OBJ (layer 4): WOBJSEL bits 0-3, WOBJLOG bits 0-1 */
  for (size_t p = 0; p < sizeof(kPairs) / sizeof(kPairs[0]); p++) {
    for (uint8_t logic = 0; logic < 4; logic++) {
      uint32_t fast[256], scalar[256];
      for (int mode = 0; mode < 2; mode++) {
        uint32_t *out = mode ? fast : scalar;
        init_obj_scene(ppu, out, mode != 0);
        ppu_write(ppu, 0x25, 0x0a); /* OBJ W1+W2 enabled */
        ppu_write(ppu, 0x26, kPairs[p].w1l);
        ppu_write(ppu, 0x27, kPairs[p].w1r);
        ppu_write(ppu, 0x28, kPairs[p].w2l);
        ppu_write(ppu, 0x29, kPairs[p].w2r);
        ppu_write(ppu, 0x2b, logic); /* WOBJLOG OBJ */
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
      }
      if (memcmp(fast, scalar, sizeof(fast)) != 0) {
        fprintf(stderr, "FAIL OBJ oracle pair=%zu logic=%u\n", p, logic);
        return 1;
      }
      if (p == 0 && logic == 1) {
        /* Overlapping W1=[33,128], W2=[80,190] with AND masks OBJ on [80,128]
         * and leaves OBJ visible outside that intersection. */
        const uint32_t obj_color = fast[10];
        const uint32_t backdrop_color = fast[100];
        if (obj_color == backdrop_color) {
          fprintf(stderr,
                  "FAIL OBJ test setup: OBJ and backdrop colors identical (%08x)\n",
                  obj_color);
          return 1;
        }
        for (int x = 0; x < 256; x++) {
          uint32_t expect = (x >= 80 && x <= 128) ? backdrop_color : obj_color;
          if (fast[x] != expect) {
            fprintf(stderr,
                    "FAIL OBJ AND baseline x=%d got=%08x expect=%08x\n",
                    x, fast[x], expect);
            return 1;
          }
        }
      }
      cases++;
    }
  }

  /* Color Window (layer 5): WOBJSEL bits 4-7, WOBJLOG bits 2-3 */
  for (size_t p = 0; p < sizeof(kPairs) / sizeof(kPairs[0]); p++) {
    for (uint8_t logic = 0; logic < 4; logic++) {
      uint32_t fast[256], scalar[256];
      for (int mode = 0; mode < 2; mode++) {
        uint32_t *out = mode ? fast : scalar;
        init_bg3_scene(ppu, out, 256, mode != 0);
        ppu->screenEnabled[0] = 0x01; /* BG1 only */
        ppu->screenWindowed[0] = 0x00;
        ppu->cgwsel = 0x90; /* clip outside color window, prevent math inside */
        ppu->cgadsub = 0x01; /* add fixed color to BG1 */
        ppu->fixedColor = 0x001f;
        ppu_write(ppu, 0x25, 0xa0); /* Color Window W1+W2 enabled */
        ppu_write(ppu, 0x26, kPairs[p].w1l);
        ppu_write(ppu, 0x27, kPairs[p].w1r);
        ppu_write(ppu, 0x28, kPairs[p].w2l);
        ppu_write(ppu, 0x29, kPairs[p].w2r);
        ppu_write(ppu, 0x2b, (uint8_t)(logic << 2)); /* WOBJLOG Color Window */
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
      }
      if (memcmp(fast, scalar, sizeof(fast)) != 0) {
        fprintf(stderr, "FAIL Color Window oracle pair=%zu logic=%u\n", p, logic);
        return 1;
      }
      if (p == 0 && (logic == 0 || logic == 1)) {
        /* Inside the combined color window, BG1 is unclipped and math is
         * prevented (green); outside, main is clipped to black and fixedColor
         * (red) is added. */
        const int lo = (logic == 0) ? 33 : 80;
        const int hi = (logic == 0) ? 190 : 128;
        const uint32_t inside_color = fast[100];
        const uint32_t outside_color = fast[10];
        if (inside_color == outside_color) {
          fprintf(stderr,
                  "FAIL Color Window setup logic=%u: inside and outside "
                  "colors identical (%08x)\n",
                  logic, inside_color);
          return 1;
        }
        for (int x = 0; x < 256; x++) {
          uint32_t expect = (x >= lo && x <= hi) ? inside_color : outside_color;
          if (fast[x] != expect) {
            fprintf(stderr,
                    "FAIL Color Window baseline logic=%u x=%d got=%08x expect=%08x\n",
                    logic, x, fast[x], expect);
            return 1;
          }
        }
      }
      cases++;
    }
  }
  printf("OBJ & Color Window oracle: %u cases passed\n", cases);
  return 0;
}

static int run_widescreen_bias_case(Ppu *ppu, const char *name, int extra,
                                    int bias, uint8_t w34sel, uint8_t wbglog,
                                    uint8_t w1l, uint8_t w1r,
                                    uint8_t w2l, uint8_t w2r,
                                    int expect_vis_lo, int expect_vis_hi) {
  const int width = 256 + 2 * extra;
  uint32_t pixels[kPpuBufWidth];
  init_bg3_scene(ppu, pixels, width, true);
  PpuSetExtraSpace(ppu, (uint8_t)extra);
  PpuSetWidescreenLayerMask(ppu, 0x05);
  PpuSetWidescreenBg3Widen(ppu, 1);
  PpuSetWidescreenPresentationXBias(ppu, bias);
  ppu_write(ppu, 0x24, w34sel);
  ppu_write(ppu, 0x26, w1l);
  ppu_write(ppu, 0x27, w1r);
  ppu_write(ppu, 0x28, w2l);
  ppu_write(ppu, 0x29, w2r);
  ppu_write(ppu, 0x2a, wbglog);
  ppu_runLine(ppu, 0);
  ppu_runLine(ppu, 1);

  /* Reference colors from unbiased single pixels. */
  uint32_t ref[256];
  init_bg3_scene(ppu, ref, 256, true);
  ppu_write(ppu, 0x24, 0x02); /* W1 only -> masks [0, 127], shows [128, 255] */
  ppu_write(ppu, 0x26, 0);
  ppu_write(ppu, 0x27, 127);
  ppu_runLine(ppu, 0);
  ppu_runLine(ppu, 1);
  const uint32_t bg1_color = ref[0];
  const uint32_t bg3_color = ref[200];

  for (int i = 0; i < width; i++) {
    int screen_x = i - extra;
    bool expect_bg3 = (expect_vis_lo <= expect_vis_hi) &&
                      (screen_x >= expect_vis_lo && screen_x <= expect_vis_hi);
    uint32_t expect = expect_bg3 ? bg3_color : bg1_color;
    if (pixels[i] != expect) {
      fprintf(stderr,
              "FAIL widescreen bias case '%s' screen_x=%d got=%08x expect=%08x\n",
              name, screen_x, pixels[i], expect);
      return 1;
    }
  }
  return 0;
}

static int run_widescreen_masked_case(Ppu *ppu, const char *name, int extra,
                                      int bias, uint8_t w34sel, uint8_t wbglog,
                                      uint8_t w1l, uint8_t w1r,
                                      uint8_t w2l, uint8_t w2r,
                                      int expect_mask_lo, int expect_mask_hi) {
  const int width = 256 + 2 * extra;
  uint32_t pixels[kPpuBufWidth];
  init_bg3_scene(ppu, pixels, width, true);
  PpuSetExtraSpace(ppu, (uint8_t)extra);
  PpuSetWidescreenLayerMask(ppu, 0x05);
  PpuSetWidescreenBg3Widen(ppu, 1);
  PpuSetWidescreenPresentationXBias(ppu, bias);
  ppu_write(ppu, 0x24, w34sel);
  ppu_write(ppu, 0x26, w1l);
  ppu_write(ppu, 0x27, w1r);
  ppu_write(ppu, 0x28, w2l);
  ppu_write(ppu, 0x29, w2r);
  ppu_write(ppu, 0x2a, wbglog);
  ppu_runLine(ppu, 0);
  ppu_runLine(ppu, 1);

  uint32_t ref[256];
  init_bg3_scene(ppu, ref, 256, true);
  ppu_write(ppu, 0x24, 0x02);
  ppu_write(ppu, 0x26, 0);
  ppu_write(ppu, 0x27, 127);
  ppu_runLine(ppu, 0);
  ppu_runLine(ppu, 1);
  const uint32_t bg1_color = ref[0];
  const uint32_t bg3_color = ref[200];

  for (int i = 0; i < width; i++) {
    int screen_x = i - extra;
    bool expect_masked =
        (expect_mask_lo <= expect_mask_hi) &&
        (screen_x >= expect_mask_lo && screen_x <= expect_mask_hi);
    uint32_t expect = expect_masked ? bg1_color : bg3_color;
    if (pixels[i] != expect) {
      fprintf(stderr,
              "FAIL widescreen masked case '%s' screen_x=%d got=%08x expect=%08x\n",
              name, screen_x, pixels[i], expect);
      return 1;
    }
  }
  return 0;
}

static int test_widescreen_presentation_bias(Ppu *ppu) {
  /* 16:9 extra = 43, visible screen_x in [-43, 298]. */
  if (run_widescreen_bias_case(ppu, "west-wall-shaft", 43, +36,
                               0x0a, 0x30, 33, 128, 255, 0, -3, 92))
    return 1;
  if (run_widescreen_bias_case(ppu, "east-wall-shaft", 43, -36,
                               0x0a, 0x30, 100, 180, 255, 0, 136, 216))
    return 1;
  if (run_widescreen_bias_case(ppu, "pinned-full-screen", 43, +36,
                               0x02, 0x00, 0, 255, 255, 0, 1, 0))
    return 1;
  if (run_widescreen_bias_case(ppu, "deep-empty-xnor", 43, +36,
                               0x0a, 0x30, 255, 0, 255, 0, 1, 0))
    return 1;
  if (run_widescreen_bias_case(ppu, "shifted-off-left", 43, +60,
                               0x0a, 0x30, 2, 10, 255, 0, 1, 0))
    return 1;
  if (run_widescreen_bias_case(ppu, "shifted-partial-right", 43, -55,
                               0x0a, 0x30, 240, 250, 255, 0, 295, 298))
    return 1;
  if (run_widescreen_bias_case(ppu, "left-pinned-partial", 43, +36,
                               0x0a, 0x30, 0, 128, 255, 0, -43, 92))
    return 1;
  /* Barrel Shield Bust-Up: 1-pixel edge-parked sentinels ([0,0], [255,255])
   * must not expand across widescreen margins or leave a 1px seam. */
  if (run_widescreen_masked_case(ppu, "parked-w2-right-zero-bias", 43, 0,
                                 0x0a, 0x00, 4, 211, 255, 255, 4, 211))
    return 1;
  if (run_widescreen_masked_case(ppu, "parked-w2-right-pos-bias", 43, +33,
                                 0x0a, 0x00, 4, 211, 255, 255, -29, 178))
    return 1;
  if (run_widescreen_masked_case(ppu, "parked-w1-left-zero-bias", 43, 0,
                                 0x0a, 0x00, 0, 0, 105, 255, 105, 298))
    return 1;
  if (run_widescreen_masked_case(ppu, "parked-w1-left-neg-bias", 43, -31,
                                 0x0a, 0x00, 0, 0, 105, 255, 136, 298))
    return 1;
  printf("Widescreen presentation-X-bias windows: 11 cases passed\n");
  return 0;
}

int main(void) {
  Ppu *ppu = ppu_init();
  if (!ppu) return 2;
  int rc = test_bg3_window_oracle(ppu);
  if (!rc) rc = test_obj_and_color_window_oracle(ppu);
  if (!rc) rc = test_widescreen_presentation_bias(ppu);
  ppu_free(ppu);
  return rc;
}
