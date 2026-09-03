#ifndef DKC3_VIDEO_H
#define DKC3_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Presentation geometry for the DKC3 hosts. The cartridge image is the
 * native 256x224 frame; the wider aspects present that frame centered
 * between black margins until a DKC3 terrain reconstruction is proven, so
 * nothing invented ever appears beside the game. The widths and the SNES
 * 7:6 pixel aspect follow DKC2Recomp: 308 columns are 16:10 and 342 are
 * 16:9 within a pixel. */
enum {
  kDkc3VideoNativeWidth = 256,
  kDkc3VideoHeight = 224,
  kDkc3VideoExtra16x10 = 26,
  kDkc3VideoExtra16x9 = 43,
  kDkc3VideoWidescreenExtra = kDkc3VideoExtra16x9,
  kDkc3VideoWidescreenWidth =
      kDkc3VideoNativeWidth + 2 * kDkc3VideoWidescreenExtra,
  kDkc3VideoBytesPerPixel = 4,
};

typedef enum Dkc3VideoAspect {
  kDkc3VideoAspectNative = 0,
  kDkc3VideoAspect16x10,
  kDkc3VideoAspect16x9,
  kDkc3VideoAspectCount,
} Dkc3VideoAspect;

/* The level-wall presentation choice DKC2Recomp offers. DKC3 has no
 * widescreen terrain yet, so the policy is recorded for the launcher and
 * menus and has no effect on the picture. */
typedef enum Dkc3VideoEdgePolicy {
  kDkc3VideoEdgeReflect = 0,
  kDkc3VideoEdgeBars,
  kDkc3VideoEdgeShift,
  kDkc3VideoEdgeGlide,
  kDkc3VideoEdgePolicyCount,
} Dkc3VideoEdgePolicy;

/* The shared snesrecomp widescreen runtime contract. */
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

#endif
