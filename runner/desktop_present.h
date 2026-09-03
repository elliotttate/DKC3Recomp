#ifndef DKC3_DESKTOP_PRESENT_H
#define DKC3_DESKTOP_PRESENT_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct Dkc3DesktopPresenter {
  HDC back_dc;
  HBITMAP back_bitmap;
  HGDIOBJ original_bitmap;
  int width;
  int height;
} Dkc3DesktopPresenter;

void Dkc3DesktopPresenterDestroy(Dkc3DesktopPresenter *presenter);

/* Compose the complete letterboxed frame off screen, then update the target
 * DC with one BitBlt. This keeps the visible surface from observing the black
 * clear that precedes the stretched SNES image. */
bool Dkc3DesktopPresent(Dkc3DesktopPresenter *presenter, HDC target,
                        const RECT *client, const uint8_t *pixels,
                        const BITMAPINFO *bitmap_info, int source_width,
                        int source_height, bool linear_filter);

#endif
