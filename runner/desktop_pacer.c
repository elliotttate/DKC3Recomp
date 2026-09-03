#include "desktop_pacer.h"

#include <math.h>

enum {
  kDkc3PacerLockObservations = 8,
  kDkc3PacerMaximumTicksPerFrame = 4,
};

static const double kDkc3PacerAverageWeight = 0.1;
/* Intervals longer than this are a stalled link, not a refresh period. */
static const double kDkc3PacerLongestPeriod = 0.1;

void Dkc3DesktopPacerInit(Dkc3DesktopPacer *pacer, double native_hz,
                          double tolerance) {
  if (!pacer) return;
  pacer->native_hz = native_hz;
  pacer->tolerance = tolerance;
  Dkc3DesktopPacerReset(pacer);
}

unsigned Dkc3DesktopPacerTicksPerFrame(double tick_hz, double native_hz,
                                       double tolerance, double *frame_hz) {
  if (frame_hz) *frame_hz = 0.0;
  if (!(tick_hz > 0.0) || !(native_hz > 0.0)) return 0;
  for (unsigned n = 1; n <= kDkc3PacerMaximumTicksPerFrame; n++) {
    double candidate = tick_hz / (double)n;
    if (fabs(candidate / native_hz - 1.0) <= tolerance) {
      if (frame_hz) *frame_hz = candidate;
      return n;
    }
  }
  return 0;
}

bool Dkc3DesktopPacerObserve(Dkc3DesktopPacer *pacer, double period_seconds) {
  if (!pacer) return false;
  if (!(period_seconds > 0.0) || period_seconds > kDkc3PacerLongestPeriod) {
    Dkc3DesktopPacerReset(pacer);
    return false;
  }
  if (pacer->observations == 0)
    pacer->period_average = period_seconds;
  else
    pacer->period_average +=
        (period_seconds - pacer->period_average) * kDkc3PacerAverageWeight;
  if (pacer->observations < kDkc3PacerLockObservations) {
    pacer->observations++;
    if (pacer->observations < kDkc3PacerLockObservations) return false;
  }
  double frame_hz = 0.0;
  pacer->ticks_per_frame = Dkc3DesktopPacerTicksPerFrame(
      1.0 / pacer->period_average, pacer->native_hz, pacer->tolerance,
      &frame_hz);
  pacer->frame_hz = frame_hz;
  return pacer->ticks_per_frame != 0;
}

void Dkc3DesktopPacerReset(Dkc3DesktopPacer *pacer) {
  if (!pacer) return;
  pacer->period_average = 0.0;
  pacer->observations = 0;
  pacer->ticks_per_frame = 0;
  pacer->frame_hz = 0.0;
}

bool Dkc3DesktopPacerLocked(const Dkc3DesktopPacer *pacer) {
  return pacer && pacer->ticks_per_frame != 0;
}
