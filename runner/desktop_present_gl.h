#ifndef DKC3_DESKTOP_PRESENT_GL_H
#define DKC3_DESKTOP_PRESENT_GL_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "desktop_vsync.h"

/* Opaque so the Windows/OpenGL implementation does not leak GL headers into
 * the desktop host or its synthetic configuration tests. */
typedef struct Dkc3DesktopGlPresenter {
  void *state;
} Dkc3DesktopGlPresenter;

typedef void (*Dkc3DesktopGlOverlayDraw)(void *user, int width, int height);

bool Dkc3DesktopGlPresenterInit(Dkc3DesktopGlPresenter *presenter, HWND window,
                                bool enable_vsync,
                                char *error, size_t error_capacity);
void Dkc3DesktopGlPresenterDestroy(Dkc3DesktopGlPresenter *presenter);
bool Dkc3DesktopGlPresent(Dkc3DesktopGlPresenter *presenter,
                          const RECT *client, const uint8_t *pixels,
                          int source_width, int source_height,
                          bool linear_filter,
                          Dkc3DesktopGlOverlayDraw overlay_draw,
                          void *overlay_user);
const char *Dkc3DesktopGlVersion(const Dkc3DesktopGlPresenter *presenter);
Dkc3DesktopVsyncStatus Dkc3DesktopGlVsyncStatus(
    const Dkc3DesktopGlPresenter *presenter);

#endif
