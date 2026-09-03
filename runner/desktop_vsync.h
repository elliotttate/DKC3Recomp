#ifndef DKC3_DESKTOP_VSYNC_H
#define DKC3_DESKTOP_VSYNC_H

#include <stdbool.h>

typedef enum Dkc3DesktopVsyncStatus {
  kDkc3DesktopVsyncUnsupported = 0,
  kDkc3DesktopVsyncDisabled,
  kDkc3DesktopVsyncEnabled,
  kDkc3DesktopVsyncRequestFailed,
} Dkc3DesktopVsyncStatus;

typedef bool (*Dkc3DesktopSwapIntervalSetter)(void *user, int interval);

/* GPU presenters request one swap per display refresh. The host's exact SNES
 * frame deadline remains the emulation clock; swap synchronization only makes
 * each completed presentation atomic with respect to monitor scanout. */
Dkc3DesktopVsyncStatus Dkc3DesktopEnableVsync(
    Dkc3DesktopSwapIntervalSetter setter, void *user);

const char *Dkc3DesktopVsyncStatusName(Dkc3DesktopVsyncStatus status);

#endif
