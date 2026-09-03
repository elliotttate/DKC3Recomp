#include "desktop_vsync.h"

Dkc3DesktopVsyncStatus Dkc3DesktopEnableVsync(
    Dkc3DesktopSwapIntervalSetter setter, void *user) {
  if (!setter) return kDkc3DesktopVsyncUnsupported;
  return setter(user, 1) ? kDkc3DesktopVsyncEnabled
                         : kDkc3DesktopVsyncRequestFailed;
}

const char *Dkc3DesktopVsyncStatusName(Dkc3DesktopVsyncStatus status) {
  switch (status) {
    case kDkc3DesktopVsyncDisabled:
      return "off";
    case kDkc3DesktopVsyncEnabled:
      return "on";
    case kDkc3DesktopVsyncRequestFailed:
      return "request-failed";
    case kDkc3DesktopVsyncUnsupported:
    default:
      return "unsupported";
  }
}
