#include "desktop_perf.h"

#include <stddef.h>
#include <string.h>

void Dkc3DesktopPerfInit(Dkc3DesktopPerfCounter *counter, uint64_t now) {
  if (!counter) return;
  memset(counter, 0, sizeof *counter);
  counter->interval_start = now;
}

void Dkc3DesktopPerfAdd(Dkc3DesktopPerfCounter *counter,
                        Dkc3DesktopPerfPhase phase, uint64_t ticks) {
  if (!counter || phase < 0 || phase >= kDkc3PerfPhaseCount) return;
  if (UINT64_MAX - counter->phase_ticks[phase] < ticks)
    counter->phase_ticks[phase] = UINT64_MAX;
  else
    counter->phase_ticks[phase] += ticks;
}

bool Dkc3DesktopPerfUpdate(Dkc3DesktopPerfCounter *counter, bool presented,
                           uint64_t now, uint64_t ticks_per_second,
                           Dkc3DesktopPerfSample *sample) {
  if (!counter || !sample || ticks_per_second == 0) return false;
  if (now < counter->interval_start) {
    Dkc3DesktopPerfInit(counter, now);
    return false;
  }
  if (presented && counter->presented_frames != UINT32_MAX)
    counter->presented_frames++;

  uint64_t elapsed = now - counter->interval_start;
  if (elapsed < ticks_per_second) return false;

  memset(sample, 0, sizeof *sample);
  sample->elapsed_seconds = (double)elapsed / (double)ticks_per_second;
  sample->presented_frames = counter->presented_frames;
  sample->presented_fps = counter->presented_frames /
                          sample->elapsed_seconds;

  double tracked_ticks = 0.0;
  double active_ticks = 0.0;
  double divisor = counter->presented_frames
                     ? (double)counter->presented_frames
                     : 1.0;
  for (int phase = 0; phase < kDkc3PerfPhaseCount; phase++) {
    double ticks = (double)counter->phase_ticks[phase];
    tracked_ticks += ticks;
    if (phase != kDkc3PerfPace) active_ticks += ticks;
    sample->phase_ms[phase] =
        ticks * 1000.0 / (double)ticks_per_second / divisor;
  }
  sample->active_ms = active_ticks * 1000.0 /
                      (double)ticks_per_second / divisor;
  sample->main_thread_busy_percent = active_ticks * 100.0 / (double)elapsed;
  double untracked_ticks = (double)elapsed - tracked_ticks;
  if (untracked_ticks < 0.0) untracked_ticks = 0.0;
  sample->untracked_ms = untracked_ticks * 1000.0 /
                         (double)ticks_per_second / divisor;

  Dkc3DesktopPerfInit(counter, now);
  return true;
}
