#include "desktop_menu.h"

static int *Selection(Dkc3MenuState *state, unsigned command, unsigned *base) {
  if (command >= kDkc3MenuAspect && command < kDkc3MenuAspect + 4) {
    *base = kDkc3MenuAspect; return &state->aspect;
  }
  if (command >= kDkc3MenuUpscaler && command < kDkc3MenuUpscaler + 3) {
    *base = kDkc3MenuUpscaler; return &state->upscaler;
  }
  if (command >= kDkc3MenuReconstruct && command < kDkc3MenuReconstruct + 5) {
    *base = kDkc3MenuReconstruct; return &state->reconstruct;
  }
  if (command >= kDkc3MenuEdge && command < kDkc3MenuEdge + 4) {
    *base = kDkc3MenuEdge; return &state->edge;
  }
  if (command >= kDkc3MenuScreen && command < kDkc3MenuScreen + 4) {
    *base = kDkc3MenuScreen; return &state->screen;
  }
  return 0;
}

bool Dkc3MenuApply(Dkc3MenuState *state, unsigned command) {
  if (!state) return false;
  unsigned base = 0;
  int *value = Selection(state, command, &base);
  if (!value) return false;
  *value = (int)(command - base);
  /* Selecting a reconstruction level also enables the reconstruction shader. */
  if (base == kDkc3MenuReconstruct) state->upscaler = 2;
  return true;
}

bool Dkc3MenuSelected(const Dkc3MenuState *state, unsigned command) {
  if (!state) return false;
  if (command == kDkc3MenuFullscreen) return state->fullscreen;
  Dkc3MenuState copy = *state;
  unsigned base = 0;
  int *value = Selection(&copy, command, &base);
  return value && *value == (int)(command - base);
}
