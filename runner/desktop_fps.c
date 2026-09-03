#include "desktop_fps.h"

#include <limits.h>
#include <stddef.h>

void Dkc3DesktopFpsInit(Dkc3DesktopFpsCounter *counter, uint64_t now) {
  if (!counter) return;
  counter->interval_start = now;
  counter->presented_frames = 0;
}

bool Dkc3DesktopFpsUpdate(Dkc3DesktopFpsCounter *counter, bool presented,
                          uint64_t now, uint64_t ticks_per_second,
                          unsigned *fps) {
  if (!counter || !fps || ticks_per_second == 0) return false;
  if (now < counter->interval_start) {
    Dkc3DesktopFpsInit(counter, now);
    return false;
  }
  if (presented && counter->presented_frames != UINT32_MAX)
    counter->presented_frames++;

  uint64_t elapsed = now - counter->interval_start;
  if (elapsed < ticks_per_second) return false;

  uint64_t rounded =
      ((uint64_t)counter->presented_frames * ticks_per_second + elapsed / 2) /
      elapsed;
  *fps = rounded > UINT_MAX ? UINT_MAX : (unsigned)rounded;
  counter->interval_start = now;
  counter->presented_frames = 0;
  return true;
}
