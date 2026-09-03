#ifndef DKC3_DESKTOP_OVERLAY_H
#define DKC3_DESKTOP_OVERLAY_H

#include "desktop_input.h"
#include "desktop_overlay_model.h"
#include "recomp_launcher.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Dkc3DesktopOverlay Dkc3DesktopOverlay;

Dkc3DesktopOverlay *Dkc3DesktopOverlayCreate(
    const RecompLauncherCSettings *settings);
bool Dkc3DesktopOverlayInitSdl(Dkc3DesktopOverlay *overlay, void *window,
                               void *gl_context);
bool Dkc3DesktopOverlayInitWin32(Dkc3DesktopOverlay *overlay, void *window);
void Dkc3DesktopOverlayDestroy(Dkc3DesktopOverlay *overlay);

bool Dkc3DesktopOverlayProcessSdlEvent(Dkc3DesktopOverlay *overlay,
                                       const void *event);
bool Dkc3DesktopOverlayProcessWin32Message(Dkc3DesktopOverlay *overlay,
                                           void *window, unsigned message,
                                           uintptr_t wparam,
                                           intptr_t lparam);
void Dkc3DesktopOverlaySetGamepad(Dkc3DesktopOverlay *overlay,
                                  const Dkc3GamepadState *gamepad);

void Dkc3DesktopOverlayToggle(Dkc3DesktopOverlay *overlay);
bool Dkc3DesktopOverlayIsOpen(const Dkc3DesktopOverlay *overlay);
bool Dkc3DesktopOverlayAssistTools(const Dkc3DesktopOverlay *overlay);
int Dkc3DesktopOverlaySelectedSlot(const Dkc3DesktopOverlay *overlay);
void Dkc3DesktopOverlayGetSettings(const Dkc3DesktopOverlay *overlay,
                                   RecompLauncherCSettings *settings);
void Dkc3DesktopOverlaySetSettings(Dkc3DesktopOverlay *overlay,
                                   const RecompLauncherCSettings *settings);
uint32_t Dkc3DesktopOverlayTakeActions(Dkc3DesktopOverlay *overlay);
void Dkc3DesktopOverlaySetStatus(Dkc3DesktopOverlay *overlay,
                                  const char *status, bool success);

/* Called by a presenter after drawing the game and before swapping buffers.
 * The presenter must have its OpenGL context current. */
void Dkc3DesktopOverlayRenderOpenGl(void *overlay, int width, int height);

#ifdef __cplusplus
}
#endif

#endif
