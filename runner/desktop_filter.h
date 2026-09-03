#ifndef DKC3_DESKTOP_FILTER_H
#define DKC3_DESKTOP_FILTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum Dkc3DesktopScreenFilter {
  kDkc3ScreenRaw = 0,
  kDkc3ScreenCrt = 1,
  kDkc3ScreenComposite = 2,
  kDkc3ScreenTrinitron = 3,
  kDkc3ScreenFilterCount = 4,
} Dkc3DesktopScreenFilter;

typedef struct Dkc3DesktopColorFilter {
  int screen_kind;
} Dkc3DesktopColorFilter;

bool Dkc3DesktopScreenFilterValid(int filter);
const char *Dkc3DesktopScreenFilterName(int filter);
bool Dkc3DesktopScreenFilterFromName(const char *name, int *filter);
bool Dkc3DesktopColorFilterInit(Dkc3DesktopColorFilter *filter,
                                int screen_kind);
void Dkc3DesktopColorFilterDestroy(Dkc3DesktopColorFilter *filter);

/* Returns source unchanged for Raw. For an opted-in screen model, writes a
 * present-only BGRX8888 frame to destination and returns destination. */
const uint8_t *Dkc3DesktopColorFilterApply(
    const Dkc3DesktopColorFilter *filter, const uint8_t *source,
    uint8_t *destination, size_t pixel_count);

#endif
