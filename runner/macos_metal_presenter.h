#ifndef DKC3_MACOS_METAL_PRESENTER_H
#define DKC3_MACOS_METAL_PRESENTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "macos_display_link.h"

/* A CAMetalLayer presenter for the visible game window, after DKC1Recomp's
 * macos_metal_presenter. The emulation thread hands each finished frame to
 * a three-slot mailbox and never calls into the window system; a
 * CAMetalDisplayLink on its own thread presents the newest frame on every
 * refresh, repeating it while no newer one has arrived, and publishes each
 * callback as a display tick with the same fields the CADisplayLink path
 * reports, so the host's pacer and pacing log work unchanged.
 *
 * The presenter draws only game pixels. While the OpenGL overlay is open
 * the host hides the Metal view (Dkc3MacMetalPresenterSetVisible) and
 * presents through OpenGL as before; the ticks keep flowing. Needs
 * macOS 14. */

typedef struct Dkc3MacMetalFrameSettings {
  int upscaler;             /* kDkc3UpscalerNearest/Bilinear/Reconstruct */
  int reconstruct_mode;     /* 0..4, see desktop_present_sdl.h */
  float reconstruct_strength;
  float reconstruct_softness;
  float reconstruct_shading;
  bool linear_filter;       /* sampler for the fixed-function upscalers */
} Dkc3MacMetalFrameSettings;

/* Start presenting into the native NSWindow. preferred_hz asks the display
 * link for that rate (60 keeps a ProMotion panel at 60 like the CADisplayLink
 * path). Returns false, with a reason, when Metal or the display link is
 * unavailable; the host then keeps its OpenGL presentation. */
bool Dkc3MacMetalPresenterStart(void *native_window, double preferred_hz,
                                char *error, size_t error_capacity);
bool Dkc3MacMetalPresenterActive(void);

/* Hand over one finished frame: tightly packed BGRA8 rows, width*height*4
 * bytes. The pixels are copied before the call returns. */
void Dkc3MacMetalPresenterQueueFrame(
    const uint8_t *bgra, int width, int height,
    const Dkc3MacMetalFrameSettings *settings);

/* Show or hide the Metal view above the OpenGL view. Hidden, the presenter
 * stops encoding but its display link keeps publishing ticks. Call from the
 * main thread. */
void Dkc3MacMetalPresenterSetVisible(bool visible);

/* Display ticks published by the presenter's display link; same contract as
 * Dkc3MacDisplayLinkWait / Dkc3MacDisplayLinkLatest. */
bool Dkc3MacMetalPresenterWaitTick(uint64_t sequence, double timeout_seconds,
                                   Dkc3MacDisplayTick *tick);
bool Dkc3MacMetalPresenterLatestTick(Dkc3MacDisplayTick *tick);

/* Arm a one-shot readback of the next presented drawable into a caller-owned
 * RGB buffer (row 0 at the top) of width*height*3 bytes, the drawable size
 * (the same backing size Dkc3SdlPresenterDrawableSize reports). The drawable
 * is readable only when DKC3_DESKTOP_SCREENSHOT was set before the presenter
 * started. Dkc3MacMetalPresenterCaptureDone reports completion. */
void Dkc3MacMetalPresenterArmCapture(uint8_t *rgb, int width, int height);
bool Dkc3MacMetalPresenterCaptureDone(void);

/* Presented-frame counters for the pacing report. */
void Dkc3MacMetalPresenterStats(uint64_t *callbacks, uint64_t *presented,
                                uint64_t *repeated, uint64_t *dropped);

void Dkc3MacMetalPresenterStop(void);

#endif
