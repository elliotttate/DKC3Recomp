#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum Dkc3DesktopPerfPhase {
  kDkc3PerfInput = 0,
  kDkc3PerfEmulation,
  kDkc3PerfRewind,
  kDkc3PerfPpu,
  kDkc3PerfAudio,
  kDkc3PerfPresent,
  kDkc3PerfPace,
  kDkc3PerfPhaseCount,
} Dkc3DesktopPerfPhase;

typedef struct Dkc3DesktopPerfCounter {
  uint64_t interval_start;
  uint64_t phase_ticks[kDkc3PerfPhaseCount];
  uint32_t presented_frames;
} Dkc3DesktopPerfCounter;

typedef struct Dkc3DesktopPerfSample {
  double elapsed_seconds;
  double presented_fps;
  double phase_ms[kDkc3PerfPhaseCount];
  double active_ms;
  double main_thread_busy_percent;
  double untracked_ms;
  uint32_t presented_frames;
} Dkc3DesktopPerfSample;

void Dkc3DesktopPerfInit(Dkc3DesktopPerfCounter *counter, uint64_t now);
void Dkc3DesktopPerfAdd(Dkc3DesktopPerfCounter *counter,
                        Dkc3DesktopPerfPhase phase, uint64_t ticks);

/* Completes one sampling interval after at least one second. Phase values in
 * the returned sample are averages per presented host frame. Pace is excluded
 * from active_ms and main_thread_busy_percent because it is intentional idle
 * time. GPU time is deliberately not represented: the current gameplay
 * backend is GDI and has no GPU timestamp API. */
bool Dkc3DesktopPerfUpdate(Dkc3DesktopPerfCounter *counter, bool presented,
                           uint64_t now, uint64_t ticks_per_second,
                           Dkc3DesktopPerfSample *sample);
