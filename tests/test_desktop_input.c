#include "desktop_input.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void ExpectInput(const char *name, uint32_t expected,
                        uint32_t buttons, int16_t x, int16_t y) {
  uint32_t actual = Dkc3MapGamepad(buttons, x, y, 7849);
  if (actual != expected) {
    (void)fprintf(stderr, "%s: expected $%03x, got $%03x\n",
                  name, (unsigned)expected, (unsigned)actual);
    exit(EXIT_FAILURE);
  }
}

static bool SyntheticKeyPressed(int scancode, void *context) {
  const int *pressed = (const int *)context;
  return scancode == *pressed;
}

int main(void) {
  ExpectInput("neutral and stick deadzone", 0, 0, 7849, -7849);
  ExpectInput("face buttons", UINT32_C(0x303),
              kDkc3GamepadA | kDkc3GamepadB |
                  kDkc3GamepadX | kDkc3GamepadY,
              0, 0);
  ExpectInput("menu and shoulders", UINT32_C(0xC0C),
              kDkc3GamepadBack | kDkc3GamepadStart |
                  kDkc3GamepadLeftShoulder |
                  kDkc3GamepadRightShoulder,
              0, 0);
  ExpectInput("D-pad", UINT32_C(0x0F0),
              kDkc3GamepadDpadUp | kDkc3GamepadDpadDown |
                  kDkc3GamepadDpadLeft | kDkc3GamepadDpadRight,
              0, 0);
  ExpectInput("left stick", UINT32_C(0x090), 0, 7850, 7850);
  ExpectInput("left stick negative", UINT32_C(0x060), 0, -7850, -7850);
  if (Dkc3MapHostActions(30, 30, 30) != 0 ||
      Dkc3MapHostActions(31, 0, 30) != kDkc3HostRewind ||
      Dkc3MapHostActions(0, 31, 30) != kDkc3HostFastForward ||
      Dkc3MapHostActions(255, 255, 30) !=
          (kDkc3HostRewind | kDkc3HostFastForward)) {
    (void)fputs("host trigger mapping failed\n", stderr);
    return EXIT_FAILURE;
  }

  const Dkc3GamepadState pads[2] = {
      {.buttons = kDkc3GamepadA, .left_trigger = 31},
      {.buttons = kDkc3GamepadB, .right_trigger = 31},
  };
  const int deadzones[2] = {24, 24};
  uint32_t actions = 0;
  const int keyboard_gamepad[2] = {
      kDkc3InputSourceKeyboard, kDkc3InputSourceGamepad};
  uint32_t routed = Dkc3RoutePlayerInputs(
      UINT32_C(0x008), pads, 2, keyboard_gamepad, deadzones, 30, &actions);
  if (routed != UINT32_C(0x001008) || actions != kDkc3HostRewind) {
    (void)fprintf(stderr,
                  "keyboard/P2 gamepad route failed: $%06x actions=$%x\n",
                  (unsigned)routed, (unsigned)actions);
    return EXIT_FAILURE;
  }

  const int two_gamepads[2] = {
      kDkc3InputSourceGamepad, kDkc3InputSourceGamepad};
  routed = Dkc3RoutePlayerInputs(
      UINT32_C(0xFFF), pads, 2, two_gamepads, deadzones, 30, &actions);
  if (routed != UINT32_C(0x100001) ||
      actions != (kDkc3HostRewind | kDkc3HostFastForward)) {
    (void)fprintf(stderr,
                  "two-gamepad route failed: $%06x actions=$%x\n",
                  (unsigned)routed, (unsigned)actions);
    return EXIT_FAILURE;
  }

  const int two_keyboards[2] = {
      kDkc3InputSourceKeyboard, kDkc3InputSourceKeyboard};
  routed = Dkc3RoutePlayerInputs(
      UINT32_C(0x842), NULL, 0, two_keyboards, deadzones, 30, &actions);
  if (routed != UINT32_C(0x842842) || actions != 0) {
    (void)fprintf(stderr,
                  "two-keyboard route failed: $%06x actions=$%x\n",
                  (unsigned)routed, (unsigned)actions);
    return EXIT_FAILURE;
  }

  const int no_sources[2] = {kDkc3InputSourceNone, kDkc3InputSourceNone};
  routed = Dkc3RoutePlayerInputs(
      UINT32_C(0xFFF), pads, 2, no_sources, deadzones, 30, &actions);
  if (routed != 0 || actions != 0) {
    (void)fputs("disabled-player routing failed\n", stderr);
    return EXIT_FAILURE;
  }

  int key_bindings[RECOMP_LAUNCHER_MAX_BINDINGS] = {0};
  key_bindings[4] = 42; /* logical SNES A -> packed bit 8 */
  int pressed_key = 42;
  if (Dkc3MapKeyboardBindings(key_bindings, SyntheticKeyPressed,
                              &pressed_key) != UINT32_C(0x100)) {
    (void)fputs("custom keyboard binding failed\n", stderr);
    return EXIT_FAILURE;
  }

  int pad_bindings[RECOMP_LAUNCHER_MAX_BINDINGS] = {0};
  pad_bindings[5] = RECOMP_LAUNCHER_PAD_BUTTON(1); /* physical B -> SNES B */
  if (Dkc3MapGamepadBindings(&pads[1], 7849, 30, pad_bindings) != 1) {
    (void)fputs("custom gamepad binding failed\n", stderr);
    return EXIT_FAILURE;
  }

  int assist_keys[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS] = {0};
  int assist_pads[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS] = {0};
  assist_keys[2] = 42;
  assist_pads[0] = RECOMP_LAUNCHER_PAD_AXIS(4, 1);
  if (Dkc3MapAssistBindings(
          assist_keys, assist_pads, SyntheticKeyPressed, &pressed_key,
          pads, 2, 30) != (kDkc3HostRewind | kDkc3HostSaveState)) {
    (void)fputs("custom Assist binding failed\n", stderr);
    return EXIT_FAILURE;
  }

  if (Dkc3ApplyAssistGate(
          kDkc3HostRewind | kDkc3HostSaveState, 0, false) != 0 ||
      Dkc3ApplyAssistGate(
          kDkc3HostRewind, kDkc3HostSaveState, false) !=
          kDkc3HostSaveState ||
      Dkc3ApplyAssistGate(
          kDkc3HostFastForward, kDkc3HostLoadState, false) !=
          kDkc3HostLoadState ||
      Dkc3ApplyAssistGate(
          kDkc3HostRewind, kDkc3HostSaveState, true) !=
          (kDkc3HostRewind | kDkc3HostSaveState)) {
    (void)fputs("native Quick State Assist gate policy failed\n", stderr);
    return EXIT_FAILURE;
  }

  (void)puts("Desktop gamepad mapping tests passed");
  return EXIT_SUCCESS;
}
