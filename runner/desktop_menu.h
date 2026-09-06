#ifndef DKC3_DESKTOP_MENU_H
#define DKC3_DESKTOP_MENU_H

#include <stdbool.h>

/* Stable native-menu IDs; ranges follow the existing persisted setting enums. */
enum {
  kDkc3MenuPause = 1000, kDkc3MenuSave, kDkc3MenuLoad,
  kDkc3MenuFullscreen, kDkc3MenuQuit, kDkc3MenuAbout,
  kDkc3MenuAspect = 1100,       /* four aspects */
  kDkc3MenuUpscaler = 1200,     /* nearest, bilinear, reconstruct */
  kDkc3MenuReconstruct = 1300,  /* five reconstruction levels */
  kDkc3MenuEdge = 1400,         /* reflect, bars, shift, glide */
  kDkc3MenuScreen = 1500,       /* raw, CRT, composite, Trinitron */
};

typedef struct Dkc3MenuState {
  int aspect, upscaler, reconstruct, edge, screen;
  bool fullscreen;
} Dkc3MenuState;

/* Only presentation selections mutate state. Actions are dispatched by the host. */
bool Dkc3MenuApply(Dkc3MenuState *state, unsigned command);
bool Dkc3MenuSelected(const Dkc3MenuState *state, unsigned command);

#endif
