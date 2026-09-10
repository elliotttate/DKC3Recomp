/* Synthetic pixels only: exercise the actual renderer against its independent
 * scalar implementation for every BG/OBJ priority combination. */
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

static void setup(Ppu *ppu, int mode, int bg1, int bg2, int obj,
                  int sub, uint32_t pixels[256], bool fast) {
  ppu_reset(ppu);
  memset(pixels, 0, 256 * sizeof(*pixels));
  PpuBeginDrawing(ppu, (uint8_t *)pixels, 256 * 4,
                  fast ? kPpuRenderFlags_NewRenderer : 0);
  ppu->inidisp = 15;
  ppu->bgmode = (uint8_t)mode;
  ppu->bgXsc[0] = 0x40;
  ppu->bgXsc[1] = 0x44;
  ppu->bgXsc[2] = 0x48; /* Empty OPT map. */
  ppu->bgTileAdr = 0x11;
  ppu->screenEnabled[sub] = (bg1 >= 0 ? 1 : 0) |
                            (bg2 >= 0 ? 2 : 0) | (obj >= 0 ? 16 : 0);
  if (sub) {
    ppu->cgwsel = 2;
    ppu->cgadsub = 0x20; /* Add subscreen to black main backdrop. */
  }
  for (int i = 0; i < 1024; i++) {
    ppu->vram[0x4000 + i] = (bg1 > 0 ? 0x2000 : 0) | 1;
    ppu->vram[0x4400 + i] = (bg2 > 0 ? 0x2000 : 0) | 2;
  }
  for (int row = 0; row < 8; row++) {
    ppu->vram[0x1000 + 16 + row] = 0x00ff; /* BG1 pixel 1. */
    ppu->vram[0x1000 + 32 + row] = 0xff00; /* BG2 pixel 2. */
    ppu->vram[row] = 0x00ff; /* OBJ pixel 129. */
  }
  ppu->cgram[1] = 0x001f;
  ppu->cgram[2] = 0x03e0;
  ppu->cgram[129] = 0x7c00;
  for (int i = 0; i < 128; i++) ppu->oam[2*i] = 0xf000;
  if (obj >= 0) {
    ppu->oam[0] = 0;
    ppu->oam[1] = (uint16_t)(obj << 12);
  }
  ppu_runLine(ppu, 0);
  ppu_runLine(ppu, 1);
}

int main(void) {
  Ppu *ppu = ppu_init();
  if (!ppu) return 2;
  unsigned cases = 0;
  for (int mode = 1; mode <= 2; mode++) {
    for (int sub = 0; sub < 2; sub++) {
      for (int bg1 = -1; bg1 < 2; bg1++) {
        for (int bg2 = -1; bg2 < 2; bg2++) {
          for (int obj = -1; obj < 4; obj++) {
            uint32_t fast[256], scalar[256];
            setup(ppu, mode, bg1, bg2, obj, sub, fast, true);
            setup(ppu, mode, bg1, bg2, obj, sub, scalar, false);
            if (memcmp(fast, scalar, sizeof(fast))) {
              fprintf(stderr, "FAIL: mode=%d sub=%d BG1=%d BG2=%d OBJ=%d fast=%08x scalar=%08x\n",
                      mode, sub, bg1, bg2, obj, fast[0], scalar[0]);
              ppu_free(ppu);
              return 1;
            }
            cases++;
          }
        }
      }
    }
  }
  ppu_free(ppu);
  printf("Mode 1/2 priority oracle: %u cases passed\n", cases);
  return 0;
}
