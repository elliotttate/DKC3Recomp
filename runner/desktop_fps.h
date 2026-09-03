#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Dkc3DesktopFpsCounter {
  uint64_t interval_start;
  uint32_t presented_frames;
} Dkc3DesktopFpsCounter;

void Dkc3DesktopFpsInit(Dkc3DesktopFpsCounter *counter, uint64_t now);

/* Records one host presentation when presented is true. Returns true once a
 * wall-clock interval of at least one second has completed and writes the
 * rounded presentation rate to fps. */
bool Dkc3DesktopFpsUpdate(Dkc3DesktopFpsCounter *counter, bool presented,
                          uint64_t now, uint64_t ticks_per_second,
                          unsigned *fps);
