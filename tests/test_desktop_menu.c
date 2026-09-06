#include "desktop_menu.h"
#include "dkc3_video.h"
#include "desktop_present_sdl.h"
#include <stdio.h>

#define CHECK(value) do { if (!(value)) { \
  fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); return 1; \
} } while (0)

int main(void) {
  CHECK(kDkc3VideoAspectCount == 4 && kDkc3VideoAspectNative == 0);
  CHECK(kDkc3VideoEdgePolicyCount == 4 && kDkc3VideoEdgeReflect == 0);
  CHECK(kDkc3UpscalerCount == 3 && kDkc3UpscalerReconstruct == 2);
  Dkc3MenuState state = {0, 0, 3, 3, 0, false};
  static const unsigned starts[] = {kDkc3MenuAspect, kDkc3MenuUpscaler,
      kDkc3MenuReconstruct, kDkc3MenuEdge, kDkc3MenuScreen};
  static const unsigned counts[] = {4, 3, 5, 4, 4};
  for (unsigned group = 0; group < 5; ++group) {
    for (unsigned i = 0; i < counts[group]; ++i) {
      CHECK(Dkc3MenuApply(&state, starts[group] + i));
      for (unsigned j = 0; j < counts[group]; ++j)
        CHECK(Dkc3MenuSelected(&state, starts[group] + j) == (i == j));
    }
    CHECK(!Dkc3MenuApply(&state, starts[group] + counts[group]));
  }
  CHECK(state.aspect == 3 && state.upscaler == 2 && state.reconstruct == 4);
  CHECK(state.edge == 3 && state.screen == 3);
  CHECK(!Dkc3MenuApply(&state, kDkc3MenuFullscreen));
  CHECK(!Dkc3MenuSelected(&state, kDkc3MenuFullscreen));
  state.fullscreen = true;
  CHECK(Dkc3MenuSelected(&state, kDkc3MenuFullscreen));
  CHECK(!Dkc3MenuApply(NULL, kDkc3MenuAspect));
  CHECK(!Dkc3MenuSelected(NULL, kDkc3MenuAspect));
  puts("Desktop menu selections, bounds, enum parity and checkmarks passed");
  return 0;
}
