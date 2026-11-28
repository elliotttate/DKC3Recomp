#include "dkc3_show_hidden.h"

#include <stdio.h>

static int s_failures;

static void Check(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    s_failures++;
  }
}

int main(void) {
  Check(!Dkc3ShowHiddenEnabledFromEnv(NULL), "NULL env enables");
  Check(!Dkc3ShowHiddenEnabledFromEnv(""), "empty env enables");
  Check(!Dkc3ShowHiddenEnabledFromEnv("0"), "\"0\" env enables");
  Check(Dkc3ShowHiddenEnabledFromEnv("1"), "\"1\" env does not enable");

  /* The hidden bear coin: slot $09C2, render word $C000. */
  Check(Dkc3ShowHiddenRenderFlags(0xC000u, 0x09C2u, 0u, false) == 0xC000u,
        "disabled cheat changed a hidden word");
  Check(Dkc3ShowHiddenRenderFlags(0xC000u, 0x09C2u, 0u, true) == 0x4000u,
        "enabled cheat did not route a hidden word to the normal draw");
  Check(Dkc3ShowHiddenRenderFlags(0xC01Eu, 0x09C2u, 0u, true) == 0x401Eu,
        "enabled cheat changed bits other than bit 15");

  /* Visible words, including renderer variants, are untouched. */
  Check(Dkc3ShowHiddenRenderFlags(0x0000u, 0x09C2u, 0u, true) == 0x0000u,
        "normal word changed");
  Check(Dkc3ShowHiddenRenderFlags(0x8004u, 0x09C2u, 0u, true) == 0x8004u,
        "variant word changed");
  Check(Dkc3ShowHiddenRenderFlags(0x4000u, 0x09C2u, 0u, true) == 0x4000u,
        "bit-14-only word changed");

  /* Kongs and the ridden animal buddy stay as the game draws them. */
  Check(Dkc3ShowHiddenRenderFlags(0xC000u, 0x0878u, 0u, true) == 0xC000u,
        "Dixie slot revealed");
  Check(Dkc3ShowHiddenRenderFlags(0xC000u, 0x08E6u, 0u, true) == 0xC000u,
        "Kiddy slot revealed");
  Check(Dkc3ShowHiddenRenderFlags(0xC000u, 0x0A30u, 0x0A30u, true) ==
            0xC000u,
        "mounted buddy slot revealed");
  Check(Dkc3ShowHiddenRenderFlags(0xC000u, 0x0A9Eu, 0x0A30u, true) ==
            0x4000u,
        "a slot other than the mounted buddy stayed hidden");

  /* Only sprites in level gameplay that hold a tile base or palette. */
  Check(Dkc3ShowHiddenSlotEligible(0xB38076u, 0x2760u),
        "bear coin in a level is not eligible");
  Check(!Dkc3ShowHiddenSlotEligible(0xB4B179u, 0x2760u),
        "a world-map slot is eligible");
  Check(!Dkc3ShowHiddenSlotEligible(0xB38076u, 0x3000u),
        "a priority-only attribute (no tiles, no palette) is eligible");
  Check(!Dkc3ShowHiddenSlotEligible(0xB38076u, 0x0000u),
        "an empty attribute is eligible");
  Check(Dkc3ShowHiddenSlotEligible(0xB38076u, 0x0E00u),
        "a palette-only attribute is not eligible");
  Check(Dkc3ShowHiddenSlotEligible(0xB38076u, 0x0001u),
        "a tile-only attribute is not eligible");

  /* A defeated K. Rool stays hidden; the bear coin type stays revealable. */
  Check(!Dkc3ShowHiddenTypeEligible(0x0118u), "hidden K. Rool is eligible");
  Check(Dkc3ShowHiddenTypeEligible(0x0284u), "bear coin type is not eligible");

  if (s_failures == 0) printf("dkc3_show_hidden: all checks passed\n");
  return s_failures == 0 ? 0 : 1;
}
