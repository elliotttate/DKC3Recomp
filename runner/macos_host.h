#ifndef DKC3_MACOS_HOST_H
#define DKC3_MACOS_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  kDkc3MacCommandToggleOverlay = 1u << 0,
  kDkc3MacCommandQuickSave = 1u << 1,
  kDkc3MacCommandQuickLoad = 1u << 2,
  kDkc3MacCommandToggleFullscreen = 1u << 3,
  kDkc3MacCommandFilterNearest = 1u << 4,
  kDkc3MacCommandFilterBilinear = 1u << 5,
  kDkc3MacCommandAspectNative = 1u << 6,
  kDkc3MacCommandAspect16x10 = 1u << 7,
  kDkc3MacCommandAspect16x9 = 1u << 8,
  kDkc3MacCommandQuit = 1u << 9,
  kDkc3MacCommandAspect21x9 = 1u << 10,
};

/* Select a writable per-user directory and return the absolute launcher asset
 * path. DKC3_PORTABLE=1 keeps the executable-adjacent development behavior;
 * DKC3_USER_DIR provides an explicit isolated directory for automation. */
bool Dkc3MacPrepareRuntimeDirectory(char *assets_path,
                                    size_t assets_capacity,
                                    char *error,
                                    size_t error_capacity);

void Dkc3MacInstallMenu(void);
uint32_t Dkc3MacTakeCommands(void);
void Dkc3MacUpdateMenu(bool fullscreen, bool linear_filter, int aspect);

/* Wait one relative interval on an absolute Mach target. The final 1.5 ms is
 * a bounded CPU spin so scheduler coalescing cannot turn a stable deadline
 * into alternating early/late presentation intervals. */
void Dkc3MacWaitSeconds(double seconds);

#endif
