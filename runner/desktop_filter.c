#include "desktop_filter.h"

#include "snes/color_lut.h"

#include <string.h>

bool Dkc3DesktopScreenFilterValid(int filter) {
  return filter >= 0 && filter < SNES_SCREEN_KIND_COUNT;
}

const char *Dkc3DesktopScreenFilterName(int filter) {
  return snes_color_lut_kind_name(filter);
}

bool Dkc3DesktopScreenFilterFromName(const char *name, int *filter) {
  return snes_color_lut_kind_from_name(name, filter) != 0;
}

bool Dkc3DesktopColorFilterInit(Dkc3DesktopColorFilter *filter,
                                int screen_kind) {
  if (!filter || !Dkc3DesktopScreenFilterValid(screen_kind)) return false;
  memset(filter, 0, sizeof *filter);
  filter->screen_kind = screen_kind;
  int setup = snes_color_lut_setup_kind(screen_kind);
  return setup >= 0;
}

void Dkc3DesktopColorFilterDestroy(Dkc3DesktopColorFilter *filter) {
  if (!filter) return;
  (void)snes_color_lut_setup_kind(SNES_SCREEN_RAW);
  filter->screen_kind = kDkc3ScreenRaw;
}

const uint8_t *Dkc3DesktopColorFilterApply(
    const Dkc3DesktopColorFilter *filter, const uint8_t *source,
    uint8_t *destination, size_t pixel_count) {
  if (!filter || !source || filter->screen_kind == kDkc3ScreenRaw)
    return source;
  if (!destination || !snes_color_lut_active()) return NULL;
  snes_color_lut_map((const uint32_t *)source, (uint32_t *)destination,
                     pixel_count);
  return destination;
}
