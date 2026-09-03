#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "recomp_launcher.h"

enum {
  kDkc3DesktopPlayerCount = 2,
  kDkc3InputSourceNone = 0,
  kDkc3InputSourceKeyboard = 1,
  kDkc3InputSourceGamepad = 2,
  kDkc3GamepadDpadUp = 0x0001,
  kDkc3GamepadDpadDown = 0x0002,
  kDkc3GamepadDpadLeft = 0x0004,
  kDkc3GamepadDpadRight = 0x0008,
  kDkc3GamepadStart = 0x0010,
  kDkc3GamepadBack = 0x0020,
  kDkc3GamepadLeftShoulder = 0x0100,
  kDkc3GamepadRightShoulder = 0x0200,
  kDkc3GamepadA = 0x1000,
  kDkc3GamepadB = 0x2000,
  kDkc3GamepadX = 0x4000,
  kDkc3GamepadY = 0x8000,
  kDkc3GamepadGuide = 0x00010000,
  kDkc3GamepadLeftStick = 0x00020000,
  kDkc3GamepadRightStick = 0x00040000,
  kDkc3HostRewind = 1u << 0,
  kDkc3HostFastForward = 1u << 1,
  kDkc3HostSaveState = 1u << 2,
  kDkc3HostLoadState = 1u << 3,
};

typedef struct Dkc3GamepadState {
  uint32_t buttons;
  int16_t left_x;
  int16_t left_y;
  int16_t right_x;
  int16_t right_y;
  uint8_t left_trigger;
  uint8_t right_trigger;
} Dkc3GamepadState;

typedef bool (*Dkc3KeyPressedFn)(int scancode, void *context);

uint32_t Dkc3MapGamepad(uint32_t buttons, int16_t left_x, int16_t left_y,
                        int16_t deadzone);
uint32_t Dkc3MapHostActions(uint8_t left_trigger, uint8_t right_trigger,
                            uint8_t threshold);
uint32_t Dkc3MapKeyboardBindings(
    const int bindings[RECOMP_LAUNCHER_MAX_BINDINGS],
    Dkc3KeyPressedFn pressed, void *context);
uint32_t Dkc3MapGamepadBindings(
    const Dkc3GamepadState *gamepad, int16_t deadzone, uint8_t axis_threshold,
    const int bindings[RECOMP_LAUNCHER_MAX_BINDINGS]);
uint32_t Dkc3MapAssistBindings(
    const int key_bindings[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS],
    const int pad_bindings[RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS],
    Dkc3KeyPressedFn pressed, void *context,
    const Dkc3GamepadState *gamepads, size_t gamepad_count,
    uint8_t axis_threshold);
/* Keeps configured Assist shortcuts behind the opt-in gate while allowing
 * explicit native-platform Quick Save/Load menu commands through. */
uint32_t Dkc3ApplyAssistGate(uint32_t mapped_actions,
                             uint32_t platform_actions,
                             bool assist_tools);
uint32_t Dkc3RoutePlayerInputsWithBindings(
    const uint32_t keyboard_inputs[kDkc3DesktopPlayerCount],
    const Dkc3GamepadState *gamepads, size_t gamepad_count,
    const int player_sources[kDkc3DesktopPlayerCount],
    const int deadzone_percent[kDkc3DesktopPlayerCount],
    const int pad_bindings[kDkc3DesktopPlayerCount]
                          [RECOMP_LAUNCHER_MAX_BINDINGS]);

/* Routes the launcher's None/Keyboard/Gamepad choices to the two packed
 * 12-bit controller words accepted by RtlRunFrame. Connected gamepads are
 * assigned in XInput user order to players that selected Gamepad. */
uint32_t Dkc3RoutePlayerInputs(
    uint32_t keyboard_input, const Dkc3GamepadState *gamepads,
    size_t gamepad_count, const int player_sources[kDkc3DesktopPlayerCount],
    const int deadzone_percent[kDkc3DesktopPlayerCount],
    uint8_t trigger_threshold, uint32_t *host_actions);
