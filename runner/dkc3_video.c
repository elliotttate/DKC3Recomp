#include "dkc3_video.h"

#include <string.h>

bool g_ws_active;
int g_ws_extra;

static Dkc3VideoAspect s_aspect = kDkc3VideoAspectNative;
static Dkc3VideoEdgePolicy s_edge_policy = kDkc3VideoEdgeGlide;
static bool s_terrain_ready;

static const char *const kAspectNames[kDkc3VideoAspectCount] = {
  "4:3", "16:10", "16:9",
};

static const char *const kEdgeNames[kDkc3VideoEdgePolicyCount] = {
  "reflect", "bars", "shift", "glide",
};

static int ExtraForAspect(Dkc3VideoAspect aspect) {
  switch (aspect) {
    case kDkc3VideoAspect16x10: return kDkc3VideoExtra16x10;
    case kDkc3VideoAspect16x9: return kDkc3VideoExtra16x9;
    default: return 0;
  }
}

void Dkc3VideoSetWidescreen(bool enabled) {
  Dkc3VideoSetAspect(enabled ? kDkc3VideoAspect16x9 : kDkc3VideoAspectNative);
}

bool Dkc3VideoIsWidescreen(void) {
  return g_ws_active;
}

void Dkc3VideoSetAspect(Dkc3VideoAspect aspect) {
  if (aspect < 0 || aspect >= kDkc3VideoAspectCount)
    aspect = kDkc3VideoAspectNative;
  if (aspect != s_aspect) s_terrain_ready = false;
  s_aspect = aspect;
  g_ws_extra = ExtraForAspect(aspect);
  g_ws_active = g_ws_extra != 0;
}

Dkc3VideoAspect Dkc3VideoGetAspect(void) {
  return s_aspect;
}

bool Dkc3VideoAspectFromName(const char *name, Dkc3VideoAspect *aspect) {
  if (!name || !aspect) return false;
  for (int i = 0; i < kDkc3VideoAspectCount; i++) {
    if (strcmp(name, kAspectNames[i]) == 0) {
      *aspect = (Dkc3VideoAspect)i;
      return true;
    }
  }
  if (strcmp(name, "native") == 0 || strcmp(name, "0") == 0) {
    *aspect = kDkc3VideoAspectNative;
    return true;
  }
  if (strcmp(name, "1") == 0) {
    *aspect = kDkc3VideoAspect16x10;
    return true;
  }
  if (strcmp(name, "2") == 0) {
    *aspect = kDkc3VideoAspect16x9;
    return true;
  }
  return false;
}

const char *Dkc3VideoAspectName(Dkc3VideoAspect aspect) {
  if (aspect < 0 || aspect >= kDkc3VideoAspectCount) return "4:3";
  return kAspectNames[aspect];
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

void Dkc3VideoSetEdgePolicy(Dkc3VideoEdgePolicy policy) {
  if (policy < 0 || policy >= kDkc3VideoEdgePolicyCount)
    policy = kDkc3VideoEdgeGlide;
  s_edge_policy = policy;
}

Dkc3VideoEdgePolicy Dkc3VideoGetEdgePolicy(void) {
  return s_edge_policy;
}

bool Dkc3VideoEdgePolicyFromName(const char *name,
                                 Dkc3VideoEdgePolicy *policy) {
  if (!name || !policy) return false;
  for (int i = 0; i < kDkc3VideoEdgePolicyCount; i++) {
    if (strcmp(name, kEdgeNames[i]) == 0) {
      *policy = (Dkc3VideoEdgePolicy)i;
      return true;
    }
  }
  return false;
}

const char *Dkc3VideoEdgePolicyName(Dkc3VideoEdgePolicy policy) {
  if (policy < 0 || policy >= kDkc3VideoEdgePolicyCount) return "glide";
  return kEdgeNames[policy];
}
