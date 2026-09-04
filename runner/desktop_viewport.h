#ifndef DKC3_DESKTOP_VIEWPORT_H
#define DKC3_DESKTOP_VIEWPORT_H

#include <stdbool.h>

typedef struct Dkc3DesktopViewport {
  int x;
  int y;
  int width;
  int height;
} Dkc3DesktopViewport;

/*
 * Compute the centered presentation rectangle using the SNES 7:6 pixel
 * aspect ratio. This preserves authentic 4:3 for 256x224, presents 342x224
 * at approximately 16:9, and presents the safe 446x224 maximum at
 * approximately 21:9.
 */
bool Dkc3DesktopComputeViewport(int output_width, int output_height,
                                int source_width, int source_height,
                                Dkc3DesktopViewport *viewport);

#endif
